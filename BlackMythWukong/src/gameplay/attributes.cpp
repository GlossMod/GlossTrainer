#include "attributes.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace gloss::gameplay::attributes
{
    namespace
    {
        using Id = features::Id;
        // EBGUAttrFloat IDs recovered from AA01 and checked against this game's enum.
        enum Attr : unsigned
        {
            HpMax = 1,
            MpMax = 2,
            StaminaMax = 8,
            TransEnergyMax = 11,
            EnergyConsumeSpeed = 13,
            EnergyIncreaseSpeed = 14,
            VigorEnergyMax = 17,
            FabaoEnergyMax = 16,
            PevalueMax = 39,
            HpMaxBase = 101,
            MpMaxBase = 102,
            StaminaMaxBase = 108,
            FreezeDefBase = 132,
            BurnDefBase = 133,
            PoisonDefBase = 134,
            ThunderDefBase = 135,
            Hp = 151,
            Mp = 152,
            Atk = 153,
            Def = 154,
            Stamina = 158,
            StaminaRecover = 159,
            CritRate = 161,
            CritMultiplier = 162,
            Tenacity = 164,
            DmgAddition = 169,
            DmgDef = 170,
            FreezeAbnormalAcc = 173,
            BurnAbnormalAcc = 174,
            PoisonAbnormalAcc = 175,
            ThunderAbnormalAcc = 176,
            BloodBottomNum = 186,
            CurEnergy = 188,
            Pevalue = 191,
            YinAbnormalAcc = 192,
            YangAbnormalAcc = 193,
            ExpDropAddition = 198,
            SpiritDropAddition = 199,
            StaminaCostMultiplier = 200,
            FabaoEnergy = 201,
            VigorEnergy = 202
        };
        constexpr ULONGLONG PlayerLifetime = 3000;
        constexpr ULONGLONG CandidateLifetime = 250;
        constexpr unsigned AttributeCount = 255;
        constexpr float LargeValue = 999999.f;
        constexpr float LargePercentage = 9999900.f;

        struct Candidate
        {
            std::uintptr_t actor{};
            std::uintptr_t vtable{};
            std::uintptr_t stack{};
            std::uintptr_t returnAddress{};
            ULONGLONG timestamp{};
            unsigned expected{};
            bool armed{};
        };
        thread_local Candidate candidate;
        struct QueuedAttribute
        {
            Id feature;
            unsigned attribute;
            float original{};
            float desired{};
            float dispatched{};
            bool saved{};
            bool pending{};
            bool inFlight{};
        };
        // Only base attributes/additions that require the game's change notification
        // enter this fixed queue. Getter-only effects never overwrite their storage.
        constexpr std::array<QueuedAttribute, 9> EmptyQueue{{{Id::WillMultiplier, SpiritDropAddition},
                                                             {Id::ExperienceMultiplier, ExpDropAddition},
                                                             {Id::MaximumHealth, HpMaxBase},
                                                             {Id::MaximumMana, MpMaxBase},
                                                             {Id::MaximumStamina, StaminaMaxBase},
                                                             {Id::ChillResistance, FreezeDefBase},
                                                             {Id::BurnResistance, BurnDefBase},
                                                             {Id::PoisonResistance, PoisonDefBase},
                                                             {Id::ShockResistance, ThunderDefBase}}};
        struct Session
        {
            std::uintptr_t actor{};
            std::uintptr_t vtable{};
            std::uintptr_t attributes{};
            ULONGLONG lastCapture{};
            std::array<QueuedAttribute, EmptyQueue.size()> queue = EmptyQueue;
            unsigned nextQueue{};
            float staminaToReplay{};
            bool replayStamina{};
        } session;
        // Callbacks may run on different game threads. Contention skips this optional
        // overlay effect rather than blocking a game thread or taking the UI mutex.
        std::atomic_flag sessionBusy = ATOMIC_FLAG_INIT;
        struct SessionGuard
        {
            bool acquired = !sessionBusy.test_and_set(std::memory_order_acquire);
            ~SessionGuard()
            {
                if (acquired)
                    sessionBusy.clear(std::memory_order_release);
            }
        };
        std::atomic<std::uintptr_t> publishedActor{};
        std::atomic<std::uintptr_t> publishedVtable{};
        std::atomic<std::uintptr_t> publishedAttributes{};
        std::atomic<ULONGLONG> publishedCapture{};

        bool Pointer(std::uintptr_t address) noexcept
        {
            return address >= 0x10000 && address < 0x0000800000000000ull;
        }
        bool ActorVtable(std::uintptr_t actor, std::uintptr_t &vtable) noexcept
        {
            return Pointer(actor) && ReadMemory(actor, &vtable, sizeof(vtable)) && Pointer(vtable);
        }
        std::uintptr_t Address(std::uintptr_t attributes, unsigned attribute) noexcept
        {
            return attributes + 0x20 + static_cast<std::uintptr_t>(attribute) * sizeof(float);
        }
        bool Stored(std::uintptr_t attributes, unsigned attribute, float &value) noexcept
        {
            return attribute < AttributeCount && Pointer(attributes) &&
                   ReadMemory(Address(attributes, attribute), &value, sizeof(value)) && std::isfinite(value);
        }
        float StoredOr(std::uintptr_t attributes, unsigned attribute, float fallback) noexcept
        {
            float result{};
            return Stored(attributes, attribute, result) ? result : fallback;
        }
        bool Fresh(ULONGLONG now) noexcept
        {
            std::uintptr_t vtable{};
            return session.actor && session.attributes && now - session.lastCapture <= PlayerLifetime &&
                   ActorVtable(session.actor, vtable) && vtable == session.vtable;
        }
        void Publish() noexcept
        {
            // Publish the timestamp last; readers also recheck identity below.
            publishedCapture.store(0, std::memory_order_release);
            publishedActor.store(session.actor, std::memory_order_relaxed);
            publishedVtable.store(session.vtable, std::memory_order_relaxed);
            publishedAttributes.store(session.attributes, std::memory_order_relaxed);
            publishedCapture.store(session.lastCapture, std::memory_order_release);
        }
        bool CandidateMatches(const Context &context, unsigned attribute, ULONGLONG now) noexcept
        {
            if (!candidate.armed)
                return false;
            const auto stack = context.rsp();
            // The accessor must still be inside the precise virtual call captured by
            // AA03. Do not let a leftover TLS marker identify a later NPC accessor.
            if (now - candidate.timestamp > CandidateLifetime || stack >= candidate.stack || candidate.stack - stack > 0x10000)
            {
                candidate.armed = false;
                return false;
            }
            if (candidate.returnAddress && Read<std::uintptr_t>(candidate.stack - sizeof(std::uintptr_t)) != candidate.returnAddress)
            {
                candidate.armed = false;
                return false;
            }
            if (attribute != candidate.expected)
                return false;
            std::uintptr_t vtable{};
            if (!ActorVtable(candidate.actor, vtable) || vtable != candidate.vtable)
            {
                candidate.armed = false;
                return false;
            }
            return true;
        }
        void Identify(std::uintptr_t attributes, ULONGLONG now) noexcept
        {
            if (session.actor != candidate.actor || session.vtable != candidate.vtable || session.attributes != attributes)
            {
                // Old objects may already have been destroyed. Never restore a previous
                // generation by blindly writing to its saved address.
                session = {};
                session.actor = candidate.actor;
                session.vtable = candidate.vtable;
                session.attributes = attributes;
            }
            session.lastCapture = now;
            candidate.armed = false;
            Publish();
        }
        float QueueValue(const QueuedAttribute &item) noexcept
        {
            const float value = Value(item.feature);
            if (item.attribute == SpiritDropAddition || item.attribute == ExpDropAddition)
                return (value - 1.f) * 10000.f;
            return value;
        }
        bool Equal(float left, float right) noexcept
        {
            return std::abs(left - right) <= .001f;
        }
        void PrepareQueue() noexcept
        {
            for (auto &item : session.queue)
            {
                const bool enabled = Enabled(item.feature);
                if (!enabled && !item.saved)
                {
                    item.pending = false;
                    continue;
                }
                float stored{};
                if (!Stored(session.attributes, item.attribute, stored))
                {
                    item.pending = false;
                    continue;
                }
                if (item.inFlight && Equal(stored, item.dispatched))
                    item.inFlight = false;
                if (enabled)
                {
                    const auto target = QueueValue(item);
                    if (!std::isfinite(target))
                    {
                        item.pending = false;
                        continue;
                    }
                    if (!item.saved)
                    {
                        item.original = stored;
                        item.saved = true;
                    }
                    item.desired = target;
                }
                else
                {
                    item.desired = item.original;
                    if (Equal(stored, item.original) && !item.inFlight)
                    {
                        item.saved = false;
                        item.pending = false;
                        continue;
                    }
                }
                // Keep requeuing until the game's own notification has updated storage.
                // A disable also replaces any queued enable with the original snapshot.
                item.pending = !Equal(stored, item.desired) || (!enabled && item.inFlight);
            }
        }
        float StaminaCeiling(std::uintptr_t attributes, float fallback) noexcept
        {
            return std::max(0.f, StoredOr(attributes, StaminaMax, fallback) - .1f);
        }
        float Override(unsigned attribute, float original, std::uintptr_t attributes) noexcept
        {
            switch (attribute)
            {
            case Hp:
                return Enabled(Id::InfiniteHealth) ? LargeValue : original;
            case Mp:
                return Enabled(Id::InfiniteMana) ? LargeValue : original;
            case Stamina:
                return Enabled(Id::InfiniteStamina) ? StaminaCeiling(attributes, original) : original;
            case Pevalue:
                return Enabled(Id::InfiniteFocus) ? StoredOr(attributes, PevalueMax, original) + 1.f : original;
            case BloodBottomNum:
                return Enabled(Id::InfiniteGourdUses) ? LargeValue : original;
            case VigorEnergy:
                return Enabled(Id::InfiniteSpiritEnergy) ? StoredOr(attributes, VigorEnergyMax, original) : original;
            case FabaoEnergy:
                return Enabled(Id::InfiniteVesselEnergy) ? StoredOr(attributes, FabaoEnergyMax, original) : original;
            case CurEnergy:
                return Enabled(Id::TransformationDuration) ? std::max(0.f, StoredOr(attributes, TransEnergyMax, original) - 1.f) : original;
            case EnergyIncreaseSpeed:
                return Enabled(Id::TransformationCooldown) ? 10000.f : original;
            case EnergyConsumeSpeed:
                return Enabled(Id::TransformationDuration) ? 0.f : original;
            case StaminaCostMultiplier:
                return Enabled(Id::StaminaConsumption) ? Value(Id::StaminaConsumption) : original;
            case Atk:
                return Enabled(Id::Attack) ? Value(Id::Attack) : original;
            case Def:
                return Enabled(Id::Defense) ? Value(Id::Defense) : original;
            case StaminaRecover:
                return Enabled(Id::StaminaRecovery) ? Value(Id::StaminaRecovery) : original;
            case CritRate:
                if (Enabled(Id::MaximumCriticalChance))
                    return LargePercentage;
                return Enabled(Id::CriticalChance) ? Value(Id::CriticalChance) * 100.f : original;
            case CritMultiplier:
                return Enabled(Id::CriticalDamage) ? Value(Id::CriticalDamage) * 100.f : original;
            case Tenacity:
                return Enabled(Id::MaximumPoise) ? LargePercentage : original;
            case DmgAddition:
                if (Enabled(Id::OneHitKill))
                    return LargePercentage;
                if (Enabled(Id::DamageMultiplier))
                    return (Value(Id::DamageMultiplier) - 1.f) * 10000.f;
                return Enabled(Id::DamageBonus) ? Value(Id::DamageBonus) * 100.f : original;
            case DmgDef:
                if (Enabled(Id::DefenseMultiplier))
                {
                    const auto multiplier = Value(Id::DefenseMultiplier);
                    if (multiplier > 0.f)
                        return (1.f - 1.f / multiplier) * 10000.f;
                }
                return Enabled(Id::DamageReduction) ? Value(Id::DamageReduction) * 100.f : original;
            case FreezeAbnormalAcc:
            case BurnAbnormalAcc:
            case PoisonAbnormalAcc:
            case ThunderAbnormalAcc:
            case YinAbnormalAcc:
            case YangAbnormalAcc:
                return Enabled(Id::PreventAbnormalStatus) ? std::min(original, .5f) : original;
            default:
                return original;
            }
        }
        float OverrideResourceChange(unsigned attribute, float original, std::uintptr_t attributes) noexcept
        {
            switch (attribute)
            {
            case Hp:
                return Enabled(Id::InfiniteHealth) ? std::max(1.f, StoredOr(attributes, HpMax, original)) : original;
            case Mp:
                return Enabled(Id::InfiniteMana) ? std::max(0.f, StoredOr(attributes, MpMax, original)) : original;
            case Stamina:
            case Pevalue:
            case VigorEnergy:
            case FabaoEnergy:
            case CurEnergy:
            case FreezeAbnormalAcc:
            case BurnAbnormalAcc:
            case PoisonAbnormalAcc:
            case ThunderAbnormalAcc:
            case YinAbnormalAcc:
            case YangAbnormalAcc:
                return Override(attribute, original, attributes);
            default:
                // Combat/stat getter overrides must never leak into persistent storage:
                // their disable operation simply resumes the unmodified game getter.
                return original;
            }
        }
    }

    void CapturePlayer(Context &context, void *user)
    {
        candidate.armed = false;
        const auto actor = Read<std::uintptr_t>(context.rsi + 0x10);
        std::uintptr_t vtable{};
        const auto expected = static_cast<unsigned>(context.rdx);
        if (expected >= AttributeCount || !ActorVtable(actor, vtable))
            return;
        candidate.actor = actor;
        candidate.vtable = vtable;
        candidate.expected = expected;
        candidate.stack = context.rsp();
        candidate.timestamp = GetTickCount64();
        // Installer passes the actual FF 50 xx call's trampoline return address
        // (context.resume + 3), not the original patched instruction's address.
        candidate.returnAddress = reinterpret_cast<std::uintptr_t>(user);
        candidate.armed = true;
        // Original FF 50 xx / movss instructions execute in the trampoline.
    }

    void ReadAttribute(Context &context, void *user)
    {
        const auto attributes = static_cast<std::uintptr_t>(context.rax);
        const auto attribute = static_cast<unsigned>(context.rcx);
        const auto address = Address(attributes, attribute);
        float result{};
        // If the original load itself is invalid, leave the entire context alone so
        // its trampoline retains the host's normal exception behavior.
        if (!ReadMemory(address, &result, sizeof(result)))
            return;
        const auto now = GetTickCount64();
        const bool identify = CandidateMatches(context, attribute, now);
        if (identify || attributes == publishedAttributes.load(std::memory_order_relaxed))
        {
            SessionGuard lock;
            if (lock.acquired)
            {
                if (identify)
                    Identify(attributes, now);
                if (attributes == session.attributes && Fresh(now))
                {
                    if (session.replayStamina)
                    {
                        if (Write(Address(attributes, Stamina), session.staminaToReplay))
                            session.replayStamina = false;
                        if (attribute == Stamina)
                            result = StoredOr(attributes, Stamina, result);
                    }
                    PrepareQueue();
                    const auto replacement = Override(attribute, result, attributes);
                    if (std::isfinite(replacement))
                        result = replacement;
                }
            }
        }
        // Exact emulation of lea rax,[rax+rcx*4+20] / movss xmm0,[rax].
        // Legacy MOVSS from memory clears the remaining 96 bits of XMM0.
        context.rax = address;
        context.xmm[0] = {};
        context.xmm[0].f[0] = result;
        context.resume = reinterpret_cast<std::uintptr_t>(user) + 9;
    }

    void ChangeAttribute(Context &context, void *)
    {
        SessionGuard lock;
        if (!lock.acquired || !Fresh(GetTickCount64()))
            return;
        if (Read<std::uintptr_t>(context.rdi + 0x10) != session.actor)
            return;
        const auto attributeAddress = static_cast<std::uintptr_t>(context.rbp) - 0x18;
        const auto valueAddress = static_cast<std::uintptr_t>(context.rbp) - 0x20;
        std::uint8_t attribute{};
        float value{};
        if (!ReadMemory(attributeAddress, &attribute, sizeof(attribute)) || !ReadMemory(valueAddress, &value, sizeof(value)) || !std::isfinite(value))
            return;
        // This callback sees the game's original event before we borrow a stamina
        // notification below. Preserve legitimate base changes (equipment, level,
        // buffs) as the latest restore baseline while our override is active.
        for (auto &item : session.queue)
        {
            if (item.saved && item.attribute == attribute)
            {
                item.original = value;
                item.inFlight = false;
            }
        }
        const auto replacement = OverrideResourceChange(attribute, value, session.attributes);
        if (std::isfinite(replacement))
            value = replacement;
        PrepareQueue();
        if (attribute == Stamina)
        {
            for (unsigned attempt = 0; attempt < session.queue.size(); ++attempt)
            {
                const auto index = (session.nextQueue + attempt) % session.queue.size();
                auto &item = session.queue[index];
                if (!item.pending)
                    continue;
                const auto queuedAttribute = static_cast<std::uint8_t>(item.attribute);
                // Preserve the original stamina notification before borrowing this
                // event for a base-attribute update, as the reference AA07 does.
                if (!Write(valueAddress, item.desired))
                    break;
                if (!Write(attributeAddress, queuedAttribute))
                {
                    Write(valueAddress, value);
                    break;
                }
                session.staminaToReplay = value;
                session.replayStamina = true;
                session.nextQueue = static_cast<unsigned>((index + 1) % session.queue.size());
                item.pending = false;
                item.dispatched = item.desired;
                item.inFlight = true;
                return;
            }
        }
        Write(valueAddress, value);
        // Trampoline replays movzx edx,[rbp-18] / movss xmm0,[rbp-20].
    }

    bool HasPlayer() noexcept
    {
        const auto stamp = publishedCapture.load(std::memory_order_acquire);
        if (!stamp || GetTickCount64() - stamp > PlayerLifetime)
            return false;
        const auto actor = publishedActor.load(std::memory_order_relaxed);
        const auto attributes = publishedAttributes.load(std::memory_order_relaxed);
        const auto expectedVtable = publishedVtable.load(std::memory_order_relaxed);
        std::uintptr_t vtable{};
        float probe{};
        return ActorVtable(actor, vtable) && vtable == expectedVtable && Stored(attributes, Hp, probe) &&
               stamp == publishedCapture.load(std::memory_order_acquire) &&
               actor == publishedActor.load(std::memory_order_relaxed) && attributes == publishedAttributes.load(std::memory_order_relaxed);
    }
    std::uintptr_t PlayerActor() noexcept
    {
        return HasPlayer() ? publishedActor.load(std::memory_order_relaxed) : 0;
    }
    bool Owns(Id id) noexcept
    {
        switch (id)
        {
        case Id::InfiniteHealth:
        case Id::InfiniteMana:
        case Id::InfiniteStamina:
        case Id::InfiniteFocus:
        case Id::InfiniteGourdUses:
        case Id::TransformationCooldown:
        case Id::TransformationDuration:
        case Id::InfiniteSpiritEnergy:
        case Id::InfiniteVesselEnergy:
        case Id::MaximumCriticalChance:
        case Id::MaximumPoise:
        case Id::PreventAbnormalStatus:
        case Id::WillMultiplier:
        case Id::ExperienceMultiplier:
        case Id::OneHitKill:
        case Id::DamageMultiplier:
        case Id::DefenseMultiplier:
        case Id::StaminaConsumption:
        case Id::MaximumHealth:
        case Id::MaximumMana:
        case Id::MaximumStamina:
        case Id::Attack:
        case Id::Defense:
        case Id::StaminaRecovery:
        case Id::CriticalChance:
        case Id::CriticalDamage:
        case Id::DamageBonus:
        case Id::DamageReduction:
        case Id::ChillResistance:
        case Id::BurnResistance:
        case Id::PoisonResistance:
        case Id::ShockResistance:
            return true;
        default:
            return false;
        }
    }
}
