#include "additional_hooks.hpp"
#include "aot.hpp"
#include "attributes.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <vector>

namespace gloss::gameplay::extra
{
    using features::Id;
    namespace
    {
        struct Control
        {
            std::atomic_bool supported{}, enabled{};
        };
        std::array<Control, 3> controls;
        std::atomic_bool cycleSupported{};
        int Index(Id id) noexcept
        {
            switch (id)
            {
            case Id::InfiniteItems:
                return 0;
            case Id::InfiniteBuffDuration:
                return 1;
            case Id::InfiniteVesselDuration:
                return 2;
            default:
                return -1;
            }
        }
        auto &Hooks()
        {
            static auto *hooks = new std::vector<std::unique_ptr<MidHook>>;
            return *hooks;
        }
        std::mutex initializeMutex;
        struct NativeIdentity
        {
            std::atomic<std::uintptr_t> pawn{}, controller{};
            std::atomic<ULONGLONG> time{};
            std::atomic<unsigned> sequence{};
        } nativeIdentity;
        std::atomic<std::uint64_t> playerEpoch{1};
        struct Removal
        {
            std::mutex mutex;
            int id{}, count{};
            ULONGLONG time{};
            std::uintptr_t actor{};
            std::uint64_t epoch{};
            bool pending{};
        } removal;
        struct Medicines
        {
            std::mutex mutex;
            std::uintptr_t actor{};
            std::array<int, 256> ids{};
            std::size_t size{};
            std::uint64_t epoch{};
        } medicines;
        struct CycleRequest
        {
            std::mutex mutex;
            bool pending{};
            int value{};
            ULONGLONG time{};
            std::uintptr_t actor{};
            std::uint64_t epoch{};
        } cycle;

        // Field layout is accepted only together with the mapped method fingerprints
        // in Initialize. These are managed-object offsets, never UObject offsets.
        struct InventoryLayout
        {
            std::size_t nativeAddress{}, gamePlayerRpc{}, rpcChannel{}, channelController{};
        } inventory;
        std::atomic_bool inventoryVerified{};
        std::uintptr_t medicineCallerBegin{}, medicineCallerEnd{};
        std::uintptr_t medicineReturn{};
        std::uintptr_t itemReturn{};

        bool Pointer(std::uintptr_t address) noexcept
        {
            return address >= 0x10000 && address <= 0x00007fffffffffff && !(address & 7);
        }
        bool LocalComponent(std::uintptr_t component, std::uintptr_t player) noexcept
        {
            return player && Pointer(component) && Read<std::uintptr_t>(component + 0x10) == player;
        }
        bool FreshNative(std::uintptr_t player, std::uintptr_t &controller) noexcept
        {
            if (!inventoryVerified.load(std::memory_order_acquire) || !player)
                return false;
            const auto sequence = nativeIdentity.sequence.load(std::memory_order_acquire);
            if (sequence & 1)
                return false;
            const auto stamp = nativeIdentity.time.load(std::memory_order_acquire);
            const auto now = GetTickCount64();
            const auto pawn = nativeIdentity.pawn.load(std::memory_order_relaxed);
            controller = nativeIdentity.controller.load(std::memory_order_relaxed);
            return stamp && now >= stamp && now - stamp <= 5000 && pawn &&
                   Read<std::uintptr_t>(player + inventory.nativeAddress) == pawn &&
                   sequence == nativeIdentity.sequence.load(std::memory_order_acquire);
        }
        bool LocalBag(std::uintptr_t bag, std::uintptr_t player) noexcept
        {
            std::uintptr_t canonicalController{};
            if (!FreshNative(player, canonicalController) || !Pointer(bag))
                return false;
            const auto gamePlayer = Read<std::uintptr_t>(bag + 0x20);
            if (!Pointer(gamePlayer) || Read<unsigned char>(gamePlayer + 0x140) != 1)
                return false;
            const auto rpc = Pointer(gamePlayer) ? Read<std::uintptr_t>(gamePlayer + inventory.gamePlayerRpc) : 0;
            const auto channel = Pointer(rpc) ? Read<std::uintptr_t>(rpc + inventory.rpcChannel) : 0;
            const auto controller = Pointer(channel) ? Read<std::uintptr_t>(channel + inventory.channelController) : 0;
            const auto nativeController = Pointer(controller) ? Read<std::uintptr_t>(controller + inventory.nativeAddress) : 0;
            return nativeController && nativeController == canonicalController &&
                   player == attributes::PlayerActor();
        }
        bool LocalHelper(std::uintptr_t helper, std::uintptr_t player) noexcept
        {
            if (!LocalBag(helper, player) || Read<unsigned char>(helper + 0x30) != 0)
                return false;
            const auto gamePlayer = Read<std::uintptr_t>(helper + 0x20);
            if (Read<std::uintptr_t>(gamePlayer + 0x20) != helper || Read<int>(gamePlayer + 0x130, -1) != 0)
                return false;
            const auto rootData = Read<std::uintptr_t>(gamePlayer + 0x90);
            const auto roleData = Pointer(rootData) ? Read<std::uintptr_t>(rootData + 0x10) : 0;
            const auto roleCs = Pointer(roleData) ? Read<std::uintptr_t>(roleData + 0x18) : 0;
            const auto roleActor = Pointer(roleCs) ? Read<std::uintptr_t>(roleCs + 0x28) : 0;
            return roleActor && Read<std::uintptr_t>(helper + 0x10) == roleActor;
        }
        void ExpireCycle(ULONGLONG now)
        {
            std::unique_lock lock(cycle.mutex, std::try_to_lock);
            if (!lock || !cycle.pending || now < cycle.time || now - cycle.time <= 30000)
                return;
            cycle.pending = false;
            features::CompleteAction(Id::NewGameCycle, {false, "周目请求已超时，请在游戏读取周目信息时重试"});
        }
        void ClearExpiredIdentity()
        {
            const auto epoch = playerEpoch.load(std::memory_order_acquire);
            {
                std::unique_lock lock(removal.mutex, std::try_to_lock);
                if (lock && removal.epoch != epoch)
                    removal.pending = false;
            }
            {
                std::unique_lock lock(medicines.mutex, std::try_to_lock);
                if (lock && medicines.epoch != epoch)
                {
                    medicines.actor = 0;
                    medicines.size = 0;
                    medicines.epoch = epoch;
                }
            }
            std::unique_lock lock(cycle.mutex, std::try_to_lock);
            if (lock && cycle.pending && cycle.epoch != epoch)
            {
                cycle.pending = false;
                features::CompleteAction(Id::NewGameCycle, {false, "周目请求已取消，玩家或场景已变化"});
            }
        }
        void CycleGetter(Context &c, void *)
        {
            std::unique_lock lock(cycle.mutex, std::try_to_lock);
            if (!lock || !cycle.pending)
                return;
            const auto now = GetTickCount64();
            if (now < cycle.time)
                return;
            const auto player = attributes::PlayerActor();
            if (!player || player != cycle.actor || cycle.epoch != playerEpoch.load(std::memory_order_acquire) || now - cycle.time > 30000)
            {
                cycle.pending = false;
                features::CompleteAction(Id::NewGameCycle, {false, "周目请求已取消，玩家或场景已变化"});
                return;
            }
            const auto helper = Read<std::uintptr_t>(c.rbp - 8);
            if (!LocalHelper(helper, player) || Read<std::uintptr_t>(helper + 0x10) != c.rax)
                return;
            const auto original = Read<int>(c.rax + 0x28, -1);
            if (original < 1 || original > 10000)
                return;
            const bool written = Write(c.rax + 0x28, cycle.value);
            cycle.pending = false;
            features::CompleteAction(Id::NewGameCycle, {written, written ? "周目已更新，随游戏保存" : "周目写入失败"});
        }
        bool ConsumeRemoval(int id, int count, std::uintptr_t player, ULONGLONG now, bool *busy = nullptr) noexcept
        {
            std::unique_lock lock(removal.mutex, std::try_to_lock);
            if (busy)
                *busy = !lock;
            if (!lock || !removal.pending)
                return false;
            if (player != removal.actor || removal.epoch != playerEpoch.load(std::memory_order_acquire) ||
                (now >= removal.time && now - removal.time > 30000))
            {
                removal.pending = false;
                return false;
            }
            // An event with an older timestamp must neither consume nor cancel a newer
            // request (UI submission and an asynchronous game callback may interleave).
            if (now < removal.time || id != removal.id || count != removal.count)
                return false;
            removal.pending = false;
            return true;
        }
        bool Medicine(int id, std::uintptr_t player) noexcept
        {
            std::unique_lock lock(medicines.mutex, std::try_to_lock);
            if (!lock || medicines.actor != player || medicines.epoch != playerEpoch.load(std::memory_order_acquire))
                return false;
            return std::find(medicines.ids.begin(), medicines.ids.begin() + medicines.size, id) !=
                   medicines.ids.begin() + medicines.size;
        }
        void RememberMedicine(int id, std::uintptr_t player) noexcept
        {
            if (id <= 0 || !player)
                return;
            std::unique_lock lock(medicines.mutex, std::try_to_lock);
            if (!lock)
                return;
            const auto epoch = playerEpoch.load(std::memory_order_acquire);
            if (medicines.actor != player || medicines.epoch != epoch)
            {
                medicines.actor = player;
                medicines.size = 0;
                medicines.epoch = epoch;
            }
            if (std::find(medicines.ids.begin(), medicines.ids.begin() + medicines.size, id) !=
                medicines.ids.begin() + medicines.size)
                return;
            if (medicines.size < medicines.ids.size())
                medicines.ids[medicines.size++] = id;
        }
        void ForgetMedicine(int id, std::uintptr_t player) noexcept
        {
            std::unique_lock lock(medicines.mutex, std::try_to_lock);
            if (!lock || medicines.actor != player)
                return;
            const auto end = medicines.ids.begin() + medicines.size;
            const auto found = std::find(medicines.ids.begin(), end, id);
            if (found != end)
            {
                *found = medicines.ids[medicines.size - 1];
                --medicines.size;
            }
        }
        bool MedicineCallStack(const Context &context) noexcept
        {
            // Mono AOT methods in this verified path preserve RBP. Inspect only a
            // strictly ascending chain inside the current thread's committed stack;
            // no scan of arbitrary words or guesses from a stale TLS "inside" flag.
            const auto tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
            const auto bottom = reinterpret_cast<std::uintptr_t>(tib->StackLimit);
            const auto top = reinterpret_cast<std::uintptr_t>(tib->StackBase);
            auto frame = static_cast<std::uintptr_t>(context.rbp);
            if (context.rsp() < bottom || context.rsp() >= top)
                return false;
            for (unsigned depth = 0; depth < 32; ++depth)
            {
                if ((frame & 7) || frame < context.rsp() || frame < bottom || frame > top - 16)
                    return false;
                const auto caller = Read<std::uintptr_t>(frame + 8);
                if (medicineReturn && caller == medicineReturn)
                    return true;
                const auto previous = Read<std::uintptr_t>(frame);
                if (previous <= frame || previous - frame > 0x10000)
                    return false;
                frame = previous;
            }
            return false;
        }
        void CaptureMedicine(Context &c, void *)
        {
            const auto player = attributes::PlayerActor();
            // Win64 instance argument 6 is source at original RSP+0x30. Only the
            // actual use-item handler is an ancestor; wine/soak/hulu callers differ.
            if (!LocalComponent(c.rcx, player) || Read<unsigned char>(c.rsp() + 0x30) != 33 ||
                c.r8 != player || c.r9 != player)
                return;
            if (MedicineCallStack(c))
                RememberMedicine(static_cast<int>(c.rdx), player);
            else
                ForgetMedicine(static_cast<int>(c.rdx), player);
        }
        bool FrozenMedicine(Context &c) noexcept
        {
            const auto player = attributes::PlayerActor();
            return Enabled(Id::InfiniteBuffDuration) && LocalComponent(c.rsi, player) && Pointer(c.rax) &&
                   Read<unsigned char>(c.rax + 0xb4) == 33 && Medicine(Read<int>(c.rax + 0xa8), player);
        }
        void BuffCountdown(Context &c, void *)
        {
            if (FrozenMedicine(c) && std::isfinite(c.xmm[0].f[0]) && c.xmm[0].f[0] > 0.f)
                c.xmm[1].f[0] = 0.f;
        }
        void VesselCountdown(Context &c, void *)
        {
            const auto player = attributes::PlayerActor();
            if (!Enabled(Id::InfiniteVesselDuration) || !LocalComponent(c.rsi, player) || !Pointer(c.rax) ||
                Read<std::uintptr_t>(c.rsi + 0x98) != c.rax || Read<int>(c.rax + 0x38) != 2)
                return;
            if (std::isfinite(c.xmm[0].f[0]) && c.xmm[0].f[0] > 0.f)
                c.xmm[1].f[0] = 0.f;
        }
        void ItemCost(Context &c, void *user)
        {
            (void)user;
            // The helper has already found the item and verified sufficient inventory.
            // Only CostItem's exact non-money callsite qualifies. Sales/storage have a
            // different return address and must retain their normal debits.
            const auto player = attributes::PlayerActor();
            const auto id = static_cast<int>(c.r15), count = static_cast<int>(c.rdi);
            const auto bag = Read<std::uintptr_t>(c.rbp - 0x60);
            if (!itemReturn || Read<std::uintptr_t>(c.rbp + 8) != itemReturn ||
                Read<unsigned char>(c.rbp - 0x68, 1) != 0 || id <= 0 || count <= 0 ||
                !LocalBag(bag, player) || Read<int>(c.r13 + 0x1c, -1) != static_cast<int>(c.rax) ||
                static_cast<int>(c.rax) < count)
                return;
            bool busy{};
            if (ConsumeRemoval(id, count, player, GetTickCount64(), &busy) || busy)
                return;
            if (!Enabled(Id::InfiniteItems))
                return;
            c.rdi = 0; // Original sub/store is replayed, retaining the real item count.
        }
        bool Matches(std::uintptr_t address, const char *pattern)
        {
            if (!address)
                return false;
            const auto parsed = Pattern::Parse(pattern);
            return FindPattern(parsed, address, parsed.bytes.size()).size() == 1;
        }
        std::uintptr_t Unique(std::uintptr_t begin, std::size_t size, const char *pattern, std::size_t offset = 0)
        {
            if (!begin || !size || size > 0x10000)
                return 0;
            const auto found = FindPattern(Pattern::Parse(pattern), begin, size);
            return found.size() == 1 ? found.front() + offset : 0;
        }
        bool Add(std::uintptr_t target, Callback callback)
        {
            if (!target)
                return false;
            std::string error;
            auto hook = MidHook::Create(reinterpret_cast<void *>(target), callback, reinterpret_cast<void *>(target), error);
            if (!hook)
            {
                Log(std::format("Additional hook {:X} rejected: {}", target, error));
                return false;
            }
            Hooks().push_back(std::move(hook));
            Log(std::format("Additional gameplay hook installed at {:X}", target));
            return true;
        }
        bool VerifyInventoryLayout()
        {
            if (inventoryVerified.load(std::memory_order_acquire))
                return true;
            // Every member of the pointer chain is grounded in the actual assembly's
            // mapped method body. In particular UObject.Address is +58, not +20.
            const bool valid =
                Unique(aot::ScriptMethod(0x36eb), 0x90, "49 8D 4E 20 48 8B 45 F0 48 89 01") &&
                Unique(aot::ScriptMethod(0x3ae5), 0x50, "49 8D 8F B8 00 00 00 48 89 01") &&
                Matches(aot::ScriptMethod(0x274a), "55 48 8B EC 48 83 EC 30 48 89 7D F8 48 8B F9 48 89 55 F0 48 8B 87 D0 03 00 00") &&
                Matches(aot::ScriptMethod(0x3c86), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 10 48 85 C0") &&
                Matches(aot::ScriptMethod(0x3ac5), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 80 40 01 00 00") &&
                Matches(aot::RuntimeMethod(0x145f), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 58 48 8D 65 00 5D C3");
            if (!valid)
                return false;
            inventory = {0x58, 0xb8, 0x3d0, 0x10};
            inventoryVerified.store(true, std::memory_order_release);
            return true;
        }
        bool VerifyCycleLayout()
        {
            return VerifyInventoryLayout() &&
                   Unique(aot::ScriptMethod(0x3786), 0x70, "49 8D 4F 10 48 8B 45 E8 48 89 01") &&
                   Unique(aot::ScriptMethod(0x3786), 0x70, "49 8D 4F 20 48 8B 45 F0 48 89 01") &&
                   Unique(aot::ScriptMethod(0x3786), 0x70, "41 C6 47 30 00") &&
                   Matches(aot::ScriptMethod(0x3aaf), "55 48 8B EC 48 83 EC 30 48 89 75 F8 48 8B F1 48 8B 46 20") &&
                   Matches(aot::ScriptMethod(0x3ace), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 63 80 30 01 00 00 85 C0 0F 94 C0") &&
                   Matches(aot::ScriptMethod(0x3aef), "55 48 8B EC 48 83 EC 30 48 89 4D F8 48 8B C1 48 8B 80 90 00 00 00") &&
                   Matches(aot::ScriptMethod(0x2b33), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 10") &&
                   Matches(aot::ProtoMethod(0x73d4), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 18") &&
                   Matches(aot::ProtoMethod(0x7415), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 28");
        }
    }

    bool Supported(Id id)
    {
        if (id == Id::NewGameCycle)
            return cycleSupported.load(std::memory_order_acquire);
        const auto index = Index(id);
        return index >= 0 && controls[static_cast<std::size_t>(index)].supported.load(std::memory_order_acquire);
    }
    bool Enabled(Id id) noexcept
    {
        const auto index = Index(id);
        return index >= 0 && controls[static_cast<std::size_t>(index)].enabled.load(std::memory_order_relaxed);
    }
    features::Result SetEnabled(Id id, bool enabled)
    {
        const auto index = Index(id);
        if (index < 0 || !Supported(id))
            return {false, "当前版本未连接此功能"};
        if (enabled && !attributes::PlayerActor())
            return {false, "请先进入存档"};
        controls[static_cast<std::size_t>(index)].enabled.store(enabled, std::memory_order_release);
        if (!enabled)
            return {true, "已关闭，后续消耗和计时恢复"};
        return {true, id == Id::InfiniteBuffDuration ? "已开启，对此后使用的丹药生效" : "已开启"};
    }
    void CaptureNativePlayer(std::uintptr_t pawn, std::uintptr_t controller) noexcept
    {
        if (nativeIdentity.pawn.load(std::memory_order_relaxed) != pawn ||
            nativeIdentity.controller.load(std::memory_order_relaxed) != controller)
            playerEpoch.fetch_add(1, std::memory_order_acq_rel);
        nativeIdentity.sequence.fetch_add(1, std::memory_order_acq_rel);
        nativeIdentity.pawn.store(pawn, std::memory_order_relaxed);
        nativeIdentity.controller.store(controller, std::memory_order_relaxed);
        nativeIdentity.time.store(pawn && controller ? GetTickCount64() : 0, std::memory_order_release);
        nativeIdentity.sequence.fetch_add(1, std::memory_order_release);
        ClearExpiredIdentity();
        ExpireCycle(GetTickCount64());
    }
    void PermitRemoval(int id, int count)
    {
        std::lock_guard lock(removal.mutex);
        removal.id = id;
        removal.count = count;
        removal.time = GetTickCount64();
        removal.actor = attributes::PlayerActor();
        removal.epoch = playerEpoch.load(std::memory_order_acquire);
        removal.pending = id > 0 && count > 0 && removal.actor;
    }
    features::Result SetCycle(int displayedCycle)
    {
        if (!Supported(Id::NewGameCycle))
            return {false, "当前版本未连接周目入口"};
        if (displayedCycle < 1 || displayedCycle > 10000)
            return {false, "周目必须在 1 至 10000 之间"};
        const auto player = attributes::PlayerActor();
        if (!player)
            return {false, "请先进入存档"};
        std::lock_guard lock(cycle.mutex);
        if (cycle.pending)
            return {false, "已有周目请求待处理"};
        cycle.value = displayedCycle;
        cycle.time = GetTickCount64();
        cycle.actor = player;
        cycle.epoch = playerEpoch.load(std::memory_order_acquire);
        cycle.pending = true;
        return {true, "已排队，等待游戏读取周目信息（30秒内）"};
    }
    void Initialize()
    {
        std::lock_guard lock(initializeMutex);
        if (!aot::Ready())
            return;
        if (!Supported(Id::InfiniteVesselDuration))
        {
            const auto method = aot::Method(0xb14d);
            const auto point = Unique(method, 0x330,
                                      "F3 0F 10 4D A8 F3 0F 59 CA F3 0F 5C C1 F3 0F 11 45 C4 83 39 00 F3 0F 11 40 5C", 9);
            // Verify the RIP constant consumed by mulss is 1000 milliseconds.
            const auto load = point ? point - 17 : 0;
            const auto constant = load && Matches(load, "F3 0F 10 15 ? ? ? ?") ? load + 8 + Read<std::int32_t>(load + 4) : 0;
            if (Matches(aot::Method(0x8daa), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 F3 0F 10 40 5C") &&
                Matches(aot::Method(0x8da2), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 63 40 38") &&
                constant && Read<float>(constant) == 1000.f &&
                Add(point, VesselCountdown))
                controls[2].supported = true;
        }
        if (!Supported(Id::InfiniteBuffDuration))
        {
            const auto method = aot::Method(0xfd61);
            const auto countdown = Unique(method, 0x930,
                                          "F3 0F 10 8D E8 FD FF FF F3 0F 5C C1 F3 0F 11 85 F0 FD FF FF 83 39 00 F3 0F 11 80 C0 00 00 00", 8);
            const auto ahead = Unique(method, 0x930,
                                      "F3 0F 10 8D E8 FD FF FF F3 0F 5C C1 F3 0F 11 80 D0 00 00 00", 8);
            const auto capture = aot::Method(0xfd66);
            medicineCallerBegin = aot::Method(0x17d53);
            medicineCallerEnd = aot::Method(0x17d54);
            medicineReturn = Unique(medicineCallerBegin, 0x50, "48 8B CF 48 63 55 E8 E8 ? ? ? ?", 12);
            const bool methods = Matches(capture, "55 48 8B EC 48 81 EC 50 03 00 00 48 89 5D C8") &&
                                 Matches(medicineCallerBegin, "55 48 8B EC 48 83 EC 50 48 89 7D F0 4C 89 7D F8 48 8B F9") &&
                                 medicineReturn && medicineCallerEnd > medicineCallerBegin && medicineCallerEnd - medicineCallerBegin <= 0x2000 &&
                                 Matches(aot::Method(0x8d09), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 F3 0F 10 80 C0 00 00 00") &&
                                 Matches(aot::Method(0x8d15), "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 0F B6 80 B4 00 00 00");
            if (methods && countdown && ahead && Add(capture, CaptureMedicine) && Add(countdown, BuffCountdown) && Add(ahead, BuffCountdown))
                controls[1].supported = true;
        }
        if (!Supported(Id::InfiniteItems) && VerifyInventoryLayout())
        {
            const auto method = aot::ScriptMethod(0x374c);
            const auto call = Unique(method, 0x70,
                                     "48 8B 4D F8 48 63 55 F0 4C 63 45 E8 45 33 C9 E8 ? ? ? ? 48 8D 65 00 5D C3", 15);
            const auto point = Unique(aot::ScriptMethod(0x374b), 0x230,
                                      "41 83 7D 00 00 49 63 45 1C 2B C7 41 83 7D 00 00 41 89 45 1C", 9);
            itemReturn = call ? call + 5 : 0;
            if (itemReturn && Add(point, ItemCost))
                controls[0].supported = true;
        }
        if (!cycleSupported && VerifyCycleLayout())
        {
            const auto method = aot::ScriptMethod(0x37c6);
            const auto point = Unique(method, 0x90,
                                      "48 8B 45 F8 48 8B 40 10 48 8B C8 83 39 00 48 63 40 28 48 8D 65 00 5D C3", 14);
            // RoleSvc's initial-character check upgrades a new player's zero to 1;
            // this stored field and the UI are both one-based.
            if (Matches(method, "55 48 8B EC 48 83 EC 10 48 89 4D F8 49 8B 0D") && Add(point, CycleGetter))
                cycleSupported = true;
        }
    }
}
