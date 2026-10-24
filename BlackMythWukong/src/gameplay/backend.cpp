#include "attributes.hpp"
#include "action.hpp"
#include "resolver.hpp"
#include "extensions.hpp"
#include "../runtime.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <limits>
#include <fstream>
#include <thread>

namespace gloss::gameplay {
using features::Id;
namespace {
constexpr auto Count = features::Count;
struct Control {
    std::atomic_bool enabled{};
    std::atomic<double> value{};
    std::atomic_bool supported{};
    PendingAction action;
};
std::array<Control, Count> controls;
std::atomic_bool scanned{}, captureReady{}, attributeReady{}, changesReady{};
std::atomic<std::uintptr_t> attributeEntry{}, nativePlayer{};
std::atomic<std::uintptr_t> listEntry{};
std::atomic<ULONGLONG> nativePlayerAt{};
// Process-lifetime allocation: never tear down executing stubs from DllMain.
auto& Hooks() { static auto* hooks = new std::vector<std::unique_ptr<MidHook>>; return *hooks; }
Control& State(Id id) { return controls[static_cast<std::size_t>(id)]; }
bool NativePlayerReady() {
    return nativePlayer.load() && GetTickCount64() - nativePlayerAt.load() < 10000;
}
void Support(std::initializer_list<Id> ids) { for (auto id : ids) State(id).supported = true; }
bool AddHook(std::uintptr_t target, Callback callback, void* user = nullptr) {
    std::string error;
    auto hook = MidHook::Create(reinterpret_cast<void*>(target), callback,
        user ? user : reinterpret_cast<void*>(target), error);
    if (!hook) { Log(std::format("Gameplay hook at {:X} rejected: {}", target, error)); return false; }
    Log(std::format("Gameplay hook installed at {:X}", target));
    Hooks().push_back(std::move(hook));
    return true;
}
std::uintptr_t Unique(const char* label, const char* pattern) {
    const auto found = FindExecutablePattern(GetModuleHandleW(nullptr), pattern);
    Log(std::format("Gameplay {}: {} executable signature match(es)", label, found.size()));
    return found.size() == 1 ? found.front() : 0;
}
void Capture(Context& c, void* target) {
    // Resolve the exact virtual method the player query is ABOUT TO call.
    const auto slot = Read<std::int8_t>(c.resume + 2);
    const auto method = Read<std::uintptr_t>(c.rax + slot);
    if (method) attributeEntry = method;
    attributes::CapturePlayer(c, reinterpret_cast<void*>(c.resume + 3));
}
void ListProbe(Context& c, void*) {
    // Captured immediately before GSBindList<float> calls its REAL List getter.
    // This is read-only; no managed method is invoked by our installer thread.
    if (attributeReady || c.rdx != 191) return;
    // ResolveListProbe already verified the float specialization. A List's
    // backing array has capacity (usually 256), not its logical count (255).
    // Validate the actual reader's machine code on the worker instead of
    // rejecting a legitimate list based on its spare array capacity.
    if (c.rax) listEntry = c.rax;
}
void GodR12(Context& c, void* target) {
    if (Enabled(Id::GodMode) && attributes::HasPlayer() && attributes::PlayerActor() == c.r12) {
        c.rax = 1;
        c.resume = reinterpret_cast<std::uintptr_t>(target) + 10; // skip mov/xor/call, retain test eax
    }
}
void GodRbx(Context& c, void* target) {
    if (Enabled(Id::GodMode) && attributes::HasPlayer() && attributes::PlayerActor() == c.rbx) {
        c.rax = 1;
        c.resume = reinterpret_cast<std::uintptr_t>(target) + 10;
    }
}
void Spell(Context& c, void*) {
    if (Enabled(Id::SpellCooldown) && attributes::HasPlayer() &&
        Read<std::uintptr_t>(c.rsi + 0x10) == attributes::PlayerActor() && c.xmm[0].f[0] > 2.f)
        c.xmm[0].f[0] = .1f;
}
bool Consume(Id id, std::uintptr_t address, bool add = false) {
    return State(id).action.Consume(GetTickCount64(), [&](int value) {
        auto result = value;
        if (add) {
            const auto current = Read<int>(address, -1);
            if (current < 0 || current > INT_MAX - value) return false;
            result += current;
        }
        return Write(address, result);
    });
}
void Item(Context& c, void*) {
    if (Read<int>(c.rax + 0x18) != 0) Consume(Id::EditSelectedItemCount, c.rax + 0x1c);
}
void Will(Context& c, void*) { Consume(Id::EditWill, c.rax + 0x20); }
void Experience(Context& c, void*) {
    Consume(Id::AddExperience, c.rsi + 0x48, true);
    Consume(Id::EditSparks, c.rsi + 0x4c);
}
void Relics(Context& c, void*) {
    if (Enabled(Id::RelicPoints)) Write(c.rcx + 0x24, static_cast<int>(Value(Id::RelicPoints)));
}
void NativeCapture(Context& c, void*) {
    nativePlayer = c.r15;
    nativePlayerAt = GetTickCount64();
    extensions::CapturePlayer(c.r15);
}
bool IsNativeOwner(std::uintptr_t object) {
    const auto player = nativePlayer.load();
    return NativePlayerReady() && (player == object || Read<std::uintptr_t>(object + 0x20) == player);
}
void Move(Context& c, void*) {
    if (Enabled(Id::MovementSpeed) && IsNativeOwner(c.rbx)) c.xmm[0].f[0] *= Value(Id::MovementSpeed);
}
void PlayerSpeed(Context& c, void* target) {
    // Original movss xmm1,[rbx+disp32] is replayed here before scaling.
    const auto displacement = Read<std::int32_t>(c.resume + 4);
    const auto scalar = Read<float>(c.rbx + displacement);
    c.xmm[1].f[0] = scalar;
    c.xmm[1].f[1] = c.xmm[1].f[2] = c.xmm[1].f[3] = 0;
    if (Enabled(Id::PlayerSpeed) && IsNativeOwner(c.rbx)) c.xmm[1].f[0] *= Value(Id::PlayerSpeed);
    c.resume = reinterpret_cast<std::uintptr_t>(target) + 8;
}
void Jump(Context& c, void*) {
    if (Enabled(Id::JumpHeight) && IsNativeOwner(c.rbx) && c.xmm[0].f[0] > 0.f)
        c.xmm[0].f[0] *= Value(Id::JumpHeight);
}
__declspec(noinline) bool ReturnTrue() { return true; }
void Drop(Context& c, void*) {
    if (Enabled(Id::GuaranteedDrop)) c.resume = reinterpret_cast<std::uintptr_t>(&ReturnTrue);
}
using CounterFn = BOOL(WINAPI*)(LARGE_INTEGER*);
CounterFn originalCounter{};
SRWLOCK clockLock = SRWLOCK_INIT;
LONGLONG lastReal{};
double virtualCounter{}, clockScale = 1.;
BOOL WINAPI Counter(LARGE_INTEGER* counter) {
    LARGE_INTEGER real{};
    const auto result = originalCounter(&real);
    if (!result || !counter) return result;
    AcquireSRWLockExclusive(&clockLock);
    if (!lastReal) virtualCounter = static_cast<double>(real.QuadPart);
    else if (real.QuadPart > lastReal) virtualCounter += static_cast<double>(real.QuadPart - lastReal) * clockScale;
    lastReal = (std::max)(lastReal, real.QuadPart);
    clockScale = Enabled(Id::GameSpeed) ? Value(Id::GameSpeed) : 1.;
    counter->QuadPart = static_cast<LONGLONG>(virtualCounter);
    ReleaseSRWLockExclusive(&clockLock);
    return result;
}
bool InstallClock() {
    auto* target = reinterpret_cast<void*>(&QueryPerformanceCounter);
    auto status = MH_CreateHook(target, reinterpret_cast<void*>(&Counter), reinterpret_cast<void**>(&originalCounter));
    if (status != MH_OK) return false;
    status = MH_EnableHook(target);
    if (status != MH_OK) { MH_RemoveHook(target); return false; }
    Support({Id::GameSpeed});
    return true;
}
bool RequiresPlayer(Id id) {
    return attributes::Owns(id) || id == Id::GodMode || id == Id::SpellCooldown;
}
features::BackendState Query(Id id) {
    if (extensions::Owns(id)) return extensions::Query(id);
    const auto& state = State(id);
    const bool enabled = state.enabled.load();
    if (!scanned) return {features::Availability::Waiting, enabled, "正在识别游戏版本和功能特征"};
    if (attributes::Owns(id) && captureReady && !attributeReady)
        return {features::Availability::Waiting, enabled, "等待玩家的 Mono 泛型属性读取器"};
    if (!state.supported) return {features::Availability::Unavailable, enabled, "当前版本未找到唯一且可验证的功能入口"};
    if (RequiresPlayer(id) && !attributes::HasPlayer()) return {features::Availability::Waiting, enabled, "等待进入游戏并捕获玩家属性"};
    if ((id == Id::PlayerSpeed || id == Id::MovementSpeed || id == Id::JumpHeight) && !NativePlayerReady())
        return {features::Availability::Waiting, enabled, "等待捕获玩家角色"};
    const auto actionStatus = state.action.State();
    if (actionStatus == PendingAction::Queued) return {features::Availability::Ready, enabled, "已排队：请打开对应背包或角色页面触发更新（30秒内）"};
    if (actionStatus == PendingAction::Completed) return {features::Availability::Ready, enabled, "已在游戏数据更新时应用"};
    if (actionStatus == PendingAction::Expired) return {features::Availability::Ready, enabled, "操作未执行或已过期，请重试"};
    return {features::Availability::Ready, enabled, "已就绪"};
}
features::Result SetEnabled(Id id, bool enabled, double value) {
    if (extensions::Owns(id)) return extensions::SetEnabled(id, enabled, value);
    auto& state = State(id);
    if (enabled && Query(id).availability != features::Availability::Ready) return {false, "目标尚未就绪，未启用"};
    if (!std::isfinite(value)) return {false, "数值无效"};
    if (enabled && id == Id::DefenseMultiplier && value <= 0) return {false, "防御倍率必须大于0"};
    state.value = value;
    state.enabled = enabled;
    Log(std::format("Feature {} {} value {}", static_cast<unsigned>(id), enabled ? "enabled" : "disabled", value));
    return {true, enabled ? "已启用" : (attributes::Owns(id) ? "已关闭；事件属性将在下一次气力更新时恢复" : "已关闭")};
}
features::Result Apply(Id id, double value) {
    if (extensions::Owns(id)) return extensions::Apply(id, value);
    if (Query(id).availability != features::Availability::Ready) return {false, "目标尚未就绪，未写入"};
    const auto& definition = features::GetDefinition(id);
    if (definition.kind != features::Kind::Action || !std::isfinite(value) || value < 0 || value > INT_MAX)
        return {false, "不支持此操作或数值无效"};
    State(id).action.Queue(static_cast<int>(value), GetTickCount64());
    return {true, "已排队：请打开对应背包或角色页面，等待游戏更新数据"};
}
void Install() {
    WCHAR executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (_wcsicmp(std::filesystem::path(executable).filename().c_str(), L"b1-Win64-Shipping.exe") != 0) {
        scanned = true; Log("Gameplay disabled: executable is not b1-Win64-Shipping.exe"); return;
    }
    extensions::Initialize();
    auto target = Unique("player attributes", "FF 50 ?? F3 0F 11 45 ?? F3 0F 10 ?? ?? 0F 57 ?? ?? 0F 2F ?? 0F 82 ?? ?? 00 00 E9 ?? ?? ?? ?? 48 8B 86 ?? ?? 00 00");
    if (target) captureReady = AddHook(target, Capture);
    target = Unique("attribute changes", "F3 0F 11 45 F4 48 8B 87 ?? ?? 00 00 ?? ?? ?? 0F B6 55 E8 F3 0F 10 45 E0 F2 0F 10 ?? ?? ?? ?? E8");
    if (target) changesReady = AddHook(target + 0xf, attributes::ChangeAttribute);
    target = Unique("god mode r12", "8B ?? 10 49 8B CC 33 D2 E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? 00 00 ?? 8B ?? 10 ?? 89");
    bool god = target && AddHook(target + 3, GodR12);
    if (!god) {
        target = Unique("god mode rbx", "48 8B CB 33 D2 E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? 00 00 ?? 8B ?? 24 10 ?? 89 ?? ?? ?? ?? ?? ?? 89 ?? ?? ?? ?? ?? ?? 85");
        god = target && AddHook(target, GodRbx);
    }
    if (god && captureReady) Support({Id::GodMode});
    target = Unique("spell cooldown", "48 8B 86 ?? ?? 00 00 ?? 8B ?? 30:40 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 83 ?? 00 E8 ?? ?? ?? ?? F3 0F 10 4D ?? F3 0F 5C C1");
    if (target && captureReady && AddHook(target + 0x1d, Spell)) Support({Id::SpellCooldown});
    target = Unique("selected item", "48 63 40 18 41 89 46 18 ?? 8B ?? ?? ?? 00 00 ?? 8B");
    if (target && AddHook(target, Item)) Support({Id::EditSelectedItemCount});
    target = Unique("will", "48 8B 40 20 4C 63 C0 ?? 8B ?? E8 ?? ?? ?? ?? ?? 8B ?? ?? ?? 8B");
    if (target && AddHook(target, Will)) Support({Id::EditWill});
    target = Unique("experience/sparks", "48 63 46 48 89 47 48 48 63 46 4C 89 ?? ?? ?? 8B ?? 18 ?? ?? ?? ?? ?? ?? E8");
    if (target && AddHook(target, Experience)) Support({Id::AddExperience, Id::EditSparks});
    target = Unique("relic points", "4C 63 41 24 48 8B C8 ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? 63");
    if (target && AddHook(target, Relics)) Support({Id::RelicPoints});
    target = Unique("drop", "55 48 8B EC 48 83 EC ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? B9 01 00 00 00 ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ?? 83 ?? 01 7C");
    if (target && AddHook(target, Drop)) Support({Id::GuaranteedDrop});
    target = Unique("native player", "41 8B 87 ?? ?? 00 00 C1 E8 04 A8 01 0F 84 ?? ?? 00 00 ?? 8B ?? ?? ?? 00 00 ?? 85 ?? 74");
    const bool native = target && AddHook(target + 7, NativeCapture);
    target = Unique("movement", "0F 28 ?? FF 90 ?? ?? 00 00 0F 10 ?? ?? ?? ?? ?? 48 8B 03 ?? 8D ?? 24");
    // The callback runs AFTER the virtual speed getter (aa12+9), before movups.
    if (native && target && AddHook(target + 9, Move)) Support({Id::MovementSpeed});
    target = Unique("player speed", "F3 0F 10 8B ?? ?? 00 00 EB ?? F3 0F 10 ?? ?? ?? ?? ?? 48 8B ?? ?? ?? 8B ?? F3 0F 59 ?? ?? ?? ?? 48 8B ?? FF 90");
    if (native && target && AddHook(target, PlayerSpeed)) Support({Id::PlayerSpeed});
    target = Unique("jump", "0F 5A C0 41 8D 50 03 F2 0F 5F 83 ?? ?? 00 00 F2 0F 11 83 ?? ?? 00 00 FF 90");
    if (native && target && AddHook(target, Jump)) Support({Id::JumpHeight});
    InstallClock();
    scanned = true;
    Log("Gameplay static hook pass complete; waiting for the player's virtual attribute method");
    std::uintptr_t attempted{};
    bool probeReady{}, playerReported{};
    ULONGLONG lastResolution{};
    for (;;) {
        const auto now = GetTickCount64();
        if (const auto method = attributeEntry.load(); captureReady && method && !attributeReady && now - lastResolution > 1000) {
            lastResolution = now;
            if (!probeReady) {
                const auto probe = ResolveListProbe(method);
                if (probe && AddHook(probe, ListProbe)) {
                    probeReady = true;
                    Log(std::format("Mono generic List<float> call probe installed at {:X}", probe));
                }
            }
            const auto reader = listEntry.load() ? listEntry.load() : method;
            if (reader != attempted) {
                attempted = reader;
                // This range comes from the verified player call chain.
                const auto matches = FindPattern(Pattern::Parse("0F 86 ?? ?? 00 00 48 8D 44 88 20 F3 0F 10 00 ?? 8D ?? ?? ?? C3"), reader, 0x100);
                Log(std::format("Player attribute method {:X}: {} scoped matches", reader, matches.size()));
                std::array<unsigned char, 512> diagnostic{};
                if (ReadMemory(reader, diagnostic.data(), diagnostic.size())) {
                    const auto path = DataDirectory() / "attribute-entry.bin";
                    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
                    stream.write(reinterpret_cast<const char*>(diagnostic.data()), diagnostic.size());
                }
                if (matches.size() == 1 && AddHook(matches[0] + 6, attributes::ReadAttribute)) {
                    attributeReady = true;
                    for (const auto& definition : features::Definitions())
                        if (attributes::Owns(definition.id)) {
                            const auto id = definition.id;
                            const bool event = id == Id::MaximumHealth || id == Id::MaximumMana || id == Id::MaximumStamina ||
                                id == Id::ChillResistance || id == Id::BurnResistance || id == Id::PoisonResistance ||
                                id == Id::ShockResistance || id == Id::WillMultiplier || id == Id::ExperienceMultiplier;
                            State(id).supported = !event || changesReady.load();
                        }
                }
            }
        }
        if (attributeReady && !playerReported && attributes::HasPlayer()) {
            playerReported = true;
            unsigned supported{};
            for (const auto& state : controls) if (state.supported) ++supported;
            Log(std::format("Player attribute identity verified; {}/44 original feature handlers connected", supported));
        }
        for (auto& state : controls) state.action.Expire(GetTickCount64());
        Sleep(100);
    }
}
}
bool Enabled(Id id) noexcept { return State(id).enabled.load(std::memory_order_relaxed); }
float Value(Id id) noexcept { return static_cast<float>(State(id).value.load(std::memory_order_relaxed)); }
void Initialize() {
    features::InitializeHotkeys();
    for (const auto& definition : features::Definitions()) State(definition.id).value = definition.initialValue;
    features::RegisterBackend({Query, SetEnabled, Apply});
    std::thread([] {
        try { Install(); }
        catch (const std::exception& error) { Log(std::format("Gameplay initialization failed: {}", error.what())); scanned = true; }
    }).detach();
}
}
