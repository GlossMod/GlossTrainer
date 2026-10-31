#include "native_adapter.hpp"
#include "backend.hpp"
#include "item_images.hpp"
#include "item_quantity.hpp"
#include "item_scan_schedule.hpp"
#include "item_loading_budget.hpp"
#include "performance_scope.hpp"
#include "item_action_index.hpp"
#include "item_text_cache.hpp"
#include "item_culture.hpp"
#include "missing_name_cache.hpp"
#include "item_richtext.hpp"
#include "combat_rules.hpp"
#include "attribute_rules.hpp"
#include "movement_rules.hpp"
#include "extended_rules.hpp"
#include "resource_rules.hpp"
#include "cooldown_rules.hpp"
#include "tag_rules.hpp"
#include "medicine_rules.hpp"
#include "image_ranges.hpp"
#include "reflection_cache.hpp"
#include "status_schedule.hpp"
#include <MinHook.h>
#include "../runtime.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" { __declspec(dllexport) char PBZ_DebugBuffer[65536]{}; }

namespace gloss::gameplay::native {
    namespace {
        using Address = std::uintptr_t;
        using ProcessEvent = void(__fastcall*)(void*, void*, void*);
        constexpr Address ActorProcessEventRva = 0x3abd7f0;
        constexpr Address ObjectProcessEventRva = 0x1631410;
        constexpr Address GameThreadIdRva = 0xdcba820;
        constexpr std::string_view SupportedHash = "7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4";
        constexpr std::array<unsigned char, 11> ActorEntry{0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x57};
        constexpr std::array<unsigned char, 12> ObjectEntry{0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

        struct ExpectedParameter {
            std::string_view name;
            ParamType type{};
            std::uint16_t offset{};
        };
        struct ExpectedFunction {
            std::string_view name;
            std::uint32_t nativeRva{};
            std::uint16_t size{};
            std::uint8_t count{};
            std::array<ExpectedParameter, 8> parameters{};
        };
        #include "native_contracts.inc"

        struct NameValue { std::uint32_t comparison{}, number{}; };
        struct StringValue { wchar_t* data{}; std::int32_t count{}, capacity{}; };
        static_assert(sizeof(NameValue) == 8 && sizeof(StringValue) == 16);
        struct Context {
            Address world{}, instance{}, player{}, controller{}, pawn{}, cheat{}, cheatClass{};
            std::uint32_t worldIndex{}, controllerIndex{};
            std::string Key() const {
                return std::format("{:x}:{}:{:x}:{:x}:{}:{:x}", world, worldIndex,
                    instance, controller, controllerIndex, pawn);
            }
            // Key() equality without formatting either side.
            bool SameKey(const Context& other) const {
                return world == other.world && worldIndex == other.worldIndex && instance == other.instance &&
                    controller == other.controller && controllerIndex == other.controllerIndex && pawn == other.pawn;
            }
        };

        Address base{};
        ProcessEvent originalActorProcessEvent{};
        std::atomic<bool> initialized{}, installed{}, faulted{};
        std::atomic<bool> stopping{};
        std::atomic<bool> modulePinned{};
        std::atomic<std::uint32_t> callbacksInFlight{};
        std::mutex slotMutex;
        std::vector<Address> patchedSlots;
        HANDLE monitorThread{};
        std::atomic<ULONGLONG> lastControllerTick{};
        std::atomic<DWORD> observedThread{}, expectedThread{};
        std::atomic<std::uint64_t> observedTicks{}, dispatchedCalls{};
        std::string activeSession;
        Context activeContext; // Same identity as activeSession, compared without formatting.
        bool activeBound{};
        std::string creationContext;
        bool creationAttempted{};
        Address activeCheat{};
        thread_local bool insideAdapter{};
        std::mutex debugMutex;

        // Keep SEH confined to leaf helpers, away from C++ objects with destructors.
        std::atomic<std::uint64_t> readFaults{};
        bool ReadBytes(Address address, void* output, std::size_t size) noexcept {
            if (address < 0x10000 || size > 1024 * 1024) return false;
            __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++readFaults; return false; }
        }
        bool WriteAddress(Address address, Address value) noexcept {
            __try { *reinterpret_cast<Address*>(address) = value; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template<class T> T Read(Address address) noexcept {
            T value{};
            ReadBytes(address, &value, sizeof(value));
            return value;
        }
        bool CallProcessEvent(Address object, Address function, void* parameters) noexcept {
            __try {
                reinterpret_cast<ProcessEvent>(base + ObjectProcessEventRva)(
                    reinterpret_cast<void*>(object), reinterpret_cast<void*>(function), parameters);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool ReplaceSlot(Address slot, Address expected, Address replacement) noexcept {
            DWORD previous{};
            if (!VirtualProtect(reinterpret_cast<void*>(slot), sizeof(Address), PAGE_READWRITE, &previous)) return false;
            const auto observed = reinterpret_cast<Address>(InterlockedCompareExchangePointer(
                reinterpret_cast<void* volatile*>(slot), reinterpret_cast<void*>(replacement), reinterpret_cast<void*>(expected)));
            DWORD ignored{};
            const bool restored = VirtualProtect(reinterpret_cast<void*>(slot), sizeof(Address), previous, &ignored) != FALSE;
            if (!restored) {
                // Keep our DLL pinned even on a protection-restoration failure. Restore
                // the pointer if the exchange succeeded, then make a second best effort.
                if (observed == expected) InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot),
                    reinterpret_cast<void*>(expected), reinterpret_cast<void*>(replacement));
                VirtualProtect(reinterpret_cast<void*>(slot), sizeof(Address), previous, &ignored);
            }
            return observed == expected && restored;
        }
        thread_local bool cacheExecutableQueries{};
        thread_local std::unordered_map<Address, bool> executablePages;
        ExecutableImageRanges executableImage; // Written once in Initialize before any other thread reads it.
        std::atomic<std::uint64_t> slowExecutableQueries{};
        bool Executable(Address address) {
            if (executableImage.Contains(address)) return true;
            const auto page = address & ~Address{0xfff};
            if (cacheExecutableQueries) {
                const auto found = executablePages.find(page);
                if (found != executablePages.end()) return found->second;
            }
            MEMORY_BASIC_INFORMATION info{};
            ++slowExecutableQueries;
            if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
                return false;
            const bool executable = !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
            if (cacheExecutableQueries) executablePages.insert_or_assign(page, executable);
            return executable;
        }
        // The result stays valid until this thread next resolves an uncached name.
        const std::string* NameText(std::uint32_t id) {
            const auto blockIndex = id >> 16;
            const auto offset = (id & 0xffffu) * 2u;
            const auto blocks = base + 0xdd11ad0;
            const auto currentBlock = Read<std::uint32_t>(blocks - 8);
            const auto currentCursor = Read<std::uint32_t>(blocks - 4);
            if (blockIndex > currentBlock || blockIndex >= 8192) return nullptr;
            const auto block = Read<Address>(blocks + blockIndex * sizeof(Address));
            if (!block || (blockIndex == currentBlock && offset + 2 > currentCursor)) return nullptr;
            std::uint16_t header{};
            if (!ReadBytes(block + offset, &header, sizeof(header))) return nullptr;
            struct CachedName { Address block; std::uint16_t header; std::string text; };
            // FName entries are immutable; keep per-thread caches because the monitor
            // and game callback both resolve names. Validate pool identity on reuse.
            static thread_local std::unordered_map<std::uint32_t, CachedName> names;
            if (const auto found = names.find(id); found != names.end() &&
                found->second.block == block && found->second.header == header) return &found->second.text;
            const auto remember = [&](std::string text) {
                if (names.size() >= 32768) names.clear();
                return &names.insert_or_assign(id, CachedName{block, header, std::move(text)}).first->second.text;
            };
            const auto count = header >> 6;
            if (!count || count > 1023) return nullptr;
            const bool wide = (header & 1) != 0;
            const auto length = count * (wide ? sizeof(wchar_t) : 1);
            if (offset + 2 + length > 0x20000 ||
                (blockIndex == currentBlock && offset + 2 + length > currentCursor)) return nullptr;
            if (!wide) {
                std::string result(count, '\0');
                if (!ReadBytes(block + offset + 2, result.data(), count) || result.find('\0') != std::string::npos)
                    return nullptr;
                return remember(std::move(result));
            }
            std::wstring text(count, L'\0');
            if (!ReadBytes(block + offset + 2, text.data(), length)) return nullptr;
            const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) return nullptr;
            std::string result(bytes, '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, result.data(), bytes, nullptr, nullptr);
            return remember(std::move(result));
        }
        std::optional<std::string> Name(std::uint32_t id) {
            const auto* text = NameText(id);
            return text ? std::optional<std::string>(*text) : std::nullopt;
        }
        bool NameIs(std::uint32_t id, std::string_view expected) {
            const auto* text = NameText(id);
            return text ? *text == expected : expected.empty();
        }
        // Compare or test the result immediately; it is not an owning copy.
        std::string_view ObjectName(Address object) {
            if (!object) return {};
            const auto* text = NameText(Read<std::uint32_t>(object + 0x18));
            return text ? std::string_view(*text) : std::string_view{};
        }
        bool ObjectValid(Address object) {
            constexpr std::uint32_t UnusableFlags = 0x40018000; // ProcessEvent rejection plus Begin/FinishDestroyed.
            return object >= 0x10000 && (object & 7) == 0 &&
                !(Read<std::uint32_t>(object + 8) & UnusableFlags) &&
                Executable(Read<Address>(Read<Address>(object))) &&
                Read<Address>(object + 0x10) >= 0x10000 && !ObjectName(object).empty();
        }
        bool IsClass(Address type, Address expected) {
            for (unsigned depth = 0; type && depth < 64; ++depth) {
                if (type == expected) return true;
                const auto parent = Read<Address>(type + 0x40);
                if (parent == type) break;
                type = parent;
            }
            return false;
        }
        bool ClassNamed(Address type, std::string_view name) {
            for (unsigned depth = 0; type && depth < 64; ++depth) {
                if (ObjectName(type) == name) return true;
                const auto parent = Read<Address>(type + 0x40);
                if (parent == type) break;
                type = parent;
            }
            return false;
        }
        // Per-thread like the name cache. A hit re-reads the owner's index and the
        // member's name, so a reused address cannot return a stale member.
        thread_local ReflectionCache propertyLookups, functionLookups;
        Address WalkProperties(Address type, std::string_view name) {
            for (unsigned depth = 0; type && depth < 64; ++depth) {
                auto property = Read<Address>(type + 0x50);
                for (unsigned index = 0; property && index < 2048; ++index) {
                    if (NameIs(Read<std::uint32_t>(property + 0x20), name)) return property;
                    const auto next = Read<Address>(property + 0x10);
                    if (next == property) return 0;
                    property = next;
                }
                type = Read<Address>(type + 0x40);
            }
            return 0;
        }
        Address FindProperty(Address type, std::string_view name) {
            if (type < 0x10000) return 0;
            const auto typeIndex = Read<std::uint32_t>(type + 0xc);
            const auto now = GetTickCount64();
            ReflectionCache::Hit hit;
            if (propertyLookups.Find(type, typeIndex, name, now, hit) &&
                (!hit.member || NameIs(Read<std::uint32_t>(hit.member + 0x20), name))) return hit.member;
            const auto property = WalkProperties(type, name);
            propertyLookups.Remember(type, typeIndex, name, property, now);
            return property;
        }
        bool PropertyLayout(Address property, std::string_view type, std::int32_t size, std::int32_t offset) {
            return property && Read<std::int32_t>(property + 0x34) == size &&
                Read<std::int32_t>(property + 0x44) == offset &&
                NameIs(Read<std::uint32_t>(Read<Address>(property + 8) + 0x30), type);
        }
        struct HealthReadback { float current{}, maximum{}; };
        Address Backpack(const Context& context) {
            // This exact-build WorldSubsystem collection is a sparse map of class/object pairs.
            const auto map = context.world + 0x9e0;
            const auto data = Read<Address>(map);
            const auto count = Read<std::int32_t>(map + 8);
            const auto capacity = Read<std::int32_t>(map + 12);
            const auto numBits = Read<std::int32_t>(map + 40);
            const auto maxBits = Read<std::int32_t>(map + 44);
            if (!data || count < 1 || count > 2048 || capacity < count || capacity > 4096 ||
                numBits != count || maxBits < numBits || maxBits > 4096) return 0;
            const auto allocatedBits = Read<Address>(map + 32);
            const auto bits = allocatedBits ? allocatedBits : map + 16;
            if (!allocatedBits && numBits > 128) return 0;
            const auto expectedClass = Read<Address>(base + 0xe019f88);
            if (!ObjectValid(expectedClass) || ObjectName(expectedClass) != "PBZBackPackSubsystem") return 0;
            for (std::int32_t index = 0; index < count; ++index) {
                if (!(Read<std::uint32_t>(bits + (index / 32) * 4) & (1u << (index % 32)))) continue;
                const auto entry = data + index * 24;
                if (Read<Address>(entry) != expectedClass) continue;
                const auto object = Read<Address>(entry + 8);
                if (ObjectValid(object) && IsClass(Read<Address>(object + 0x10), expectedClass) &&
                    Read<Address>(object + 0x20) == context.world) return object;
            }
            return 0;
        }
        struct UltimateReadback { std::int32_t current, maximum; float percentage; };
        std::optional<UltimateReadback> ReadUltimate(const Context& context) {
            const auto backpack = Backpack(context);
            if (!backpack) return {};
            const auto type = Read<Address>(backpack + 0x10);
            if (!PropertyLayout(FindProperty(type, "CurrentUltimateCharge"), "IntProperty", 4, 0x880) ||
                !PropertyLayout(FindProperty(type, "MaxUltimateCharge"), "IntProperty", 4, 0x884) ||
                !PropertyLayout(FindProperty(type, "UltimateChargePercentage"), "FloatProperty", 4, 0x888)) return {};
            const UltimateReadback value{Read<std::int32_t>(backpack + 0x880), Read<std::int32_t>(backpack + 0x884),
                Read<float>(backpack + 0x888)};
            if (value.current < 0 || value.maximum < value.current || !std::isfinite(value.percentage)) return {};
            return value;
        }
        std::optional<HealthReadback> ReadHealth(const Context& context) {
            if (!ObjectValid(context.pawn)) return {};
            const auto attributeProperty = FindProperty(Read<Address>(context.pawn + 0x10), "AttributeSetBase");
            if (!PropertyLayout(attributeProperty, "ObjectProperty", 8, 0x758)) return {};
            const auto attributes = Read<Address>(context.pawn + Read<std::int32_t>(attributeProperty + 0x44));
            if (!ObjectValid(attributes) || !ClassNamed(Read<Address>(attributes + 0x10), "BasicAttributeSet")) return {};
            const auto currentProperty = FindProperty(Read<Address>(attributes + 0x10), "Health");
            const auto maximumProperty = FindProperty(Read<Address>(attributes + 0x10), "Health_Max");
            if (!PropertyLayout(currentProperty, "StructProperty", 16, 0x58) ||
                !PropertyLayout(maximumProperty, "StructProperty", 16, 0x68) ||
                ObjectName(Read<Address>(currentProperty + 0x70)) != "GameplayAttributeData" ||
                ObjectName(Read<Address>(maximumProperty + 0x70)) != "GameplayAttributeData") return {};
            HealthReadback health;
            if (!ReadBytes(attributes + Read<std::int32_t>(currentProperty + 0x44) + 0xc, &health.current, sizeof(float)) ||
                !ReadBytes(attributes + Read<std::int32_t>(maximumProperty + 0x44) + 0xc, &health.maximum, sizeof(float)) ||
                !std::isfinite(health.current) || !std::isfinite(health.maximum) || health.maximum < 0 ||
                std::abs(health.current) > 1e9f || health.maximum > 1e9f) return {};
            return health;
        }
        void PublishReadback(const std::optional<HealthReadback>& health, const Context* context = nullptr) {
            std::vector<Statistic> statistics{
                {"health", "当前生命", health ? std::format("{:.1f}", health->current) : "未回读"},
                {"max_health", "最大生命", health ? std::format("{:.1f}", health->maximum) : "未回读"},
                {"thread", "游戏线程", std::to_string(GetCurrentThreadId())},
                {"profile", "版本契约", "CL-301524_JKS-17219 · SHA-256 匹配"},
                {"verification", "效果验证", "调用仅在专属回读通过时显示为已验证"}};
            if (context) {
                if (const auto resource = ReadUltimate(*context)) {
                    statistics.push_back({"ultimate_count", "背包终结技次数", std::format("{} / {}", resource->current, resource->maximum)});
                    statistics.push_back({"ultimate_charge", "背包终结技充能", std::format("{:.1f}%", resource->percentage)});
                }
                const auto backpack = Backpack(*context);
                if (backpack) {
                    for (const auto& field : std::array<std::pair<const char*, const char*>, 3>{{
                        {"CurrentMainWeaponID", "当前主武器"}, {"BottomMedicineID", "下栏药品"}, {"TopMedicineID", "上栏药品"}}}) {
                        const auto property = FindProperty(Read<Address>(backpack + 0x10), field.first);
                        if (!PropertyLayout(property, "NameProperty", 8, Read<std::int32_t>(property + 0x44))) continue;
                        const auto offset = Read<std::int32_t>(property + 0x44);
                        if (offset < 0x28 || offset > 0x1000) continue;
                        const auto id = Read<NameValue>(backpack + offset);
                        if (const auto name = Name(id.comparison))
                            statistics.push_back({field.first, field.second, std::format("{} · ID 0x{:X}:{}", *name, id.comparison, id.number)});
                    }
                }
            }
            GetBackend().PublishStatistics(std::move(statistics));
        }
        std::optional<Context> CurrentContext() {
            const auto engine = Read<Address>(base + 0xdf741c8);
            const auto viewport = Read<Address>(engine + 0xc88);
            Context context;
            context.world = Read<Address>(viewport + 0x78);
            context.instance = Read<Address>(viewport + 0x80);
            const auto players = Read<Address>(context.instance + 0x38);
            const auto count = Read<std::int32_t>(context.instance + 0x40);
            const auto capacity = Read<std::int32_t>(context.instance + 0x44);
            if (count < 1 || count > 8 || capacity < count || capacity > 64) return {};
            context.player = Read<Address>(players);
            context.controller = Read<Address>(context.player + 0x30);
            context.pawn = Read<Address>(context.controller + 0x358);
            context.cheat = Read<Address>(context.controller + 0x418);
            context.cheatClass = Read<Address>(context.controller + 0x420);
            if (!ObjectValid(context.world) || !ObjectValid(context.instance) ||
                !ObjectValid(context.player) || !ObjectValid(context.controller) || !ObjectValid(context.pawn)) return {};
            context.worldIndex = Read<std::uint32_t>(context.world + 0xc);
            context.controllerIndex = Read<std::uint32_t>(context.controller + 0xc);
            return context;
        }
        bool LocalGameThread() {
            const auto tid = Read<DWORD>(base + GameThreadIdRva);
            if (expectedThread.load(std::memory_order_relaxed) != tid) expectedThread.store(tid);
            return tid && tid == GetCurrentThreadId();
        }
        const ExpectedFunction* Contract(std::string_view name) {
            for (const auto& item : ExpectedFunctions) if (item.name == name) return &item;
            return nullptr;
        }
        std::size_t SizeOf(ParamType type) {
            switch (type) {
            case ParamType::Float: case ParamType::Int: return 4;
            case ParamType::Name: return 8;
            case ParamType::String: return 16;
            case ParamType::Bool: return 1;
            }
            return 0;
        }
        std::string_view PropertyType(ParamType type) {
            switch (type) {
            case ParamType::Float: return "FloatProperty";
            case ParamType::Int: return "IntProperty";
            case ParamType::Name: return "NameProperty";
            case ParamType::String: return "StrProperty";
            case ParamType::Bool: return "BoolProperty";
            }
            return {};
        }
        Address WalkFunctions(Address owner, std::string_view name) {
            for (unsigned depth = 0; owner && depth < 64; ++depth) {
                auto function = Read<Address>(owner + 0x48);
                for (unsigned index = 0; function && index < 2048; ++index) {
                    if (ObjectName(function) == name) return function;
                    const auto next = Read<Address>(function + 0x28);
                    if (next == function) return 0;
                    function = next;
                }
                owner = Read<Address>(owner + 0x40);
            }
            return 0;
        }
        Address FindFunction(Address owner, std::string_view name) {
            if (owner < 0x10000) return 0;
            const auto ownerIndex = Read<std::uint32_t>(owner + 0xc);
            const auto now = GetTickCount64();
            ReflectionCache::Hit hit;
            if (functionLookups.Find(owner, ownerIndex, name, now, hit) &&
                (!hit.member || ObjectName(hit.member) == name)) return hit.member;
            const auto function = WalkFunctions(owner, name);
            functionLookups.Remember(owner, ownerIndex, name, function, now);
            return function;
        }
        // These engine helpers have different flags and return parameters from GM execs.
        bool TimeDilationSignature(Address function, bool getter) {
            if (!ObjectValid(function) || ObjectName(function) != (getter ? "GetGlobalTimeDilation" : "SetGlobalTimeDilation") ||
                Read<std::uint32_t>(function + 0x100) != (getter ? 0x14022403u : 0x04022403u) ||
                Read<std::uint8_t>(function + 0x104) != 2 || Read<std::uint16_t>(function + 0x106) != 12 ||
                Read<Address>(function + 0x128) != base + (getter ? 0x3f98a20 : 0x3fa0bf0)) return false;
            auto property = Read<Address>(function + 0x50);
            if (Name(Read<std::uint32_t>(property + 0x20)) != "WorldContextObject" ||
                !PropertyLayout(property, "ObjectProperty", 8, 0) ||
                Read<std::uint64_t>(property + 0x38) != 0x18001000000282ull) return false;
            property = Read<Address>(property + 0x10);
            return Name(Read<std::uint32_t>(property + 0x20)) == (getter ? "ReturnValue" : "TimeDilation") &&
                PropertyLayout(property, "FloatProperty", 4, 8) &&
                Read<std::uint64_t>(property + 0x38) == (getter ? 0x18001040000780ull : 0x18001040000280ull) &&
                !Read<Address>(property + 0x10);
        }
        bool TimeDilationReady() {
            const auto owner = Read<Address>(base + 0xdf45bf8);
            return ObjectValid(owner) && ObjectName(owner) == "GameplayStatics" &&
                ObjectValid(Read<Address>(owner + 0x160)) &&
                TimeDilationSignature(FindFunction(owner, "SetGlobalTimeDilation"), false) &&
                TimeDilationSignature(FindFunction(owner, "GetGlobalTimeDilation"), true);
        }
        ExecutionResult SetTimeDilation(const Context& context, const Request& request) {
            if (request.arguments.size() != 1 || !TimeDilationReady())
                return {RequestState::Failed, "时间缩放入口或参数契约不符"};
            float value{};
            const auto& argument = request.arguments.front();
            const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() ||
                !std::isfinite(value) || value < 0.1f || value > 3.0f)
                return {RequestState::Failed, "时间倍率必须在 0.1–3.0 之间"};
            const auto owner = Read<Address>(base + 0xdf45bf8);
            const auto cdo = Read<Address>(owner + 0x160);
            struct Parameters { Address world; float value; } parameters{context.world, value};
            if (!CallProcessEvent(cdo, FindFunction(owner, "SetGlobalTimeDilation"), &parameters)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "时间缩放调用异常，适配器停止");
                return {RequestState::Indeterminate, "时间缩放调用异常，结果无法确认"};
            }
            ++dispatchedCalls;
            const auto current = CurrentContext();
            if (!current || current->Key() != context.Key())
                return {RequestState::Indeterminate, "时间缩放调用后玩家会话变化"};
            parameters.value = std::numeric_limits<float>::quiet_NaN();
            if (!CallProcessEvent(cdo, FindFunction(owner, "GetGlobalTimeDilation"), &parameters)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "时间缩放回读异常，适配器停止");
                return {RequestState::Indeterminate, "时间缩放已调用，回读异常"};
            }
            if (std::isfinite(parameters.value) && std::abs(parameters.value - value) < 0.0001f)
                return {RequestState::Verified, std::format("全局时间倍率回读 {:.2f}，与目标一致；影响角色、AI 和世界时间", parameters.value)};
            return {RequestState::Dispatched, std::format("时间倍率调用已返回，回读值 {:.3f} 与目标不符", parameters.value)};
        }
        std::optional<NameValue> FindExistingName(std::string_view text) {
            // Search existing immutable entry bytes only. Do not create names or retain UObject pointers.
            static std::unordered_map<std::string, std::uint32_t> cache;
            if (const auto found = cache.find(std::string(text)); found != cache.end()) {
                if (Name(found->second) == text) return NameValue{found->second, 0};
                cache.erase(found);
            }
            const auto last = Read<std::uint32_t>(base + 0xdd11ac8);
            const auto cursor = Read<std::uint32_t>(base + 0xdd11acc);
            if (last >= 8192 || cursor > 0x20000) return {};
            // A missing name stays missing until the append-only pool grows.
            static thread_local MissingNameCache missing;
            const auto poolRevision = (static_cast<std::uint64_t>(last) << 32) | cursor;
            if (missing.Contains(text, poolRevision)) return {};
            // The game appends names constantly. Search only what was added since the
            // last complete search, or a name it never registers rescans the whole pool.
            auto scanned = missing.Scanned(text);
            if (scanned > poolRevision) scanned = 0;
            const auto scannedBlock = static_cast<std::uint32_t>(scanned >> 32);
            const auto scannedCursor = static_cast<std::uint32_t>(scanned);
            bool completeScan = true;
            static thread_local std::vector<unsigned char> bytes(0x20000);
            for (std::uint32_t block = scannedBlock; block <= last; ++block) {
                const auto size = block == last ? cursor : 0x20000;
                const std::uint32_t first = block == scannedBlock ? scannedCursor : 0;
                if (size < first + text.size() + 2) continue;
                if (!ReadBytes(Read<Address>(base + 0xdd11ad0 + block * 8) + first, bytes.data() + first, size - first)) {
                    completeScan = false;
                    continue;
                }
                auto begin = bytes.begin() + first + 2;
                const auto end = bytes.begin() + size;
                while (begin != end) {
                    const auto found = std::search(begin, end, text.begin(), text.end());
                    if (found == end) break;
                    const auto offset = static_cast<std::uint32_t>(found - bytes.begin() - 2);
                    std::uint16_t header{};
                    std::memcpy(&header, bytes.data() + offset, 2);
                    if (!(offset & 1) && !(header & 1) && (header >> 6) == text.size()) {
                        const auto id = (block << 16) | (offset / 2);
                        if (Name(id) == text) { cache.emplace(text, id); return NameValue{id, 0}; }
                    }
                    begin = found + 1;
                }
            }
            if (completeScan) missing.Remember(text, poolRevision);
            return {};
        }
        std::optional<bool> ReadState(const Context& context, std::string_view tag) {
            if (stopping.load() || faulted.load() || !LocalGameThread()) return {};
            const auto property = FindProperty(Read<Address>(context.pawn + 0x10), "Actor State Component");
            if (!PropertyLayout(property, "ObjectProperty", 8, 0xf28)) return {};
            const auto state = Read<Address>(context.pawn + 0xf28);
            if (!ObjectValid(state)) return {};
            const auto function = FindFunction(Read<Address>(state + 0x10), "InExactState");
            if (!ObjectValid(function) || Read<Address>(function + 0x128) != base + 0x5d962a0 ||
                Read<std::uint32_t>(function + 0x100) != 0x54420401 ||
                Read<std::uint8_t>(function + 0x104) != 2 || Read<std::uint16_t>(function + 0x106) != 9) return {};
            auto parameter = Read<Address>(function + 0x50);
            if (Name(Read<std::uint32_t>(parameter + 0x20)) != "StateTag" ||
                !PropertyLayout(parameter, "StructProperty", 8, 0) ||
                ObjectName(Read<Address>(parameter + 0x70)) != "GameplayTag" ||
                Read<std::uint64_t>(parameter + 0x38) != 0x18001008000182ull) return {};
            parameter = Read<Address>(parameter + 0x10);
            if (Name(Read<std::uint32_t>(parameter + 0x20)) != "ReturnValue" ||
                !PropertyLayout(parameter, "BoolProperty", 1, 8) ||
                Read<std::uint64_t>(parameter + 0x38) != 0x18001040000780ull ||
                Read<std::uint32_t>(parameter + 0x70) != 0xff010001 || Read<Address>(parameter + 0x10)) return {};
            const auto name = FindExistingName(tag);
            if (!name) return {};
            struct Parameters { NameValue tag; std::uint8_t result; } parameters{*name, 0};
            if (!CallProcessEvent(state, function, &parameters)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "状态回读异常，适配器停止");
                return {};
            }
            return parameters.result != 0;
        }
        ExecutionResult ReadLocation(const Context& context) {
            const auto function = FindFunction(Read<Address>(context.pawn + 0x10), "K2_GetActorLocation");
            const auto parameter = Read<Address>(function + 0x50);
            if (!ObjectValid(function) || Read<Address>(function + 0x128) != base + 0x3acceb0 ||
                Read<std::uint32_t>(function + 0x100) != 0x54820401 ||
                Read<std::uint8_t>(function + 0x104) != 1 || Read<std::uint16_t>(function + 0x106) != 24 ||
                Name(Read<std::uint32_t>(parameter + 0x20)) != "ReturnValue" ||
                !PropertyLayout(parameter, "StructProperty", 24, 0) ||
                ObjectName(Read<Address>(parameter + 0x70)) != "Vector" ||
                Read<std::uint64_t>(parameter + 0x38) != 0x18001040000780ull || Read<Address>(parameter + 0x10))
                return {RequestState::Failed, "坐标返回结构未通过契约核对"};
            struct Vector { double x, y, z; } value{};
            if (!CallProcessEvent(context.pawn, function, &value)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "坐标查询异常，适配器停止");
                return {RequestState::Indeterminate, "坐标查询异常"};
            }
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                return {RequestState::Failed, "坐标返回非有限数值"};
            return {RequestState::Verified, std::format("当前位置 X={:.3f},Y={:.3f},Z={:.3f}", value.x, value.y, value.z)};
        }
        std::optional<std::int32_t> ReadGameplayTagCount(const Context& context, std::string_view tag) {
            if (stopping.load() || faulted.load() || !LocalGameThread()) return {};
            const auto property = FindProperty(Read<Address>(context.pawn + 0x10), "AbilitySystemComponent");
            if (!PropertyLayout(property, "ObjectProperty", 8, 0x750)) return {};
            const auto component = Read<Address>(context.pawn + 0x750);
            if (!ObjectValid(component)) return {};
            const auto function = FindFunction(Read<Address>(component + 0x10), "GetGameplayTagCount");
            if (!ObjectValid(function) || Read<Address>(function + 0x128) != base + 0x4c8a000 ||
                Read<std::uint32_t>(function + 0x100) != 0x54020403 ||
                Read<std::uint8_t>(function + 0x104) != 2 || Read<std::uint16_t>(function + 0x106) != 12) return {};
            auto parameter = Read<Address>(function + 0x50);
            if (Name(Read<std::uint32_t>(parameter + 0x20)) != "GameplayTag" ||
                !PropertyLayout(parameter, "StructProperty", 8, 0) ||
                ObjectName(Read<Address>(parameter + 0x70)) != "GameplayTag" ||
                Read<std::uint64_t>(parameter + 0x38) != 0x18001000000080ull) return {};
            parameter = Read<Address>(parameter + 0x10);
            if (Name(Read<std::uint32_t>(parameter + 0x20)) != "ReturnValue" ||
                !PropertyLayout(parameter, "IntProperty", 4, 8) ||
                Read<std::uint64_t>(parameter + 0x38) != 0x18001040000780ull || Read<Address>(parameter + 0x10)) return {};
            const auto name = FindExistingName(tag);
            if (!name) return {};
            struct Parameters { NameValue tag; std::int32_t result; } parameters{*name, -1};
            if (!CallProcessEvent(component, function, &parameters)) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "能力标签回读异常，适配器停止");
                return {};
            }
            if (parameters.result < 0) return {};
            return parameters.result;
        }
        void ExtendedResourcePreChange(void* object, const void* attribute, float* proposed, bool baseValue) noexcept;
        void AppendExtendedToggleSlice(const Context& context, std::size_t slice, std::vector<ToggleState>& states);
        #include "native_attributes.inc"
        #include "native_combat.inc"
        #include "native_movement.inc"
        void RefreshExtendedCapabilities(const ToggleState& state) {
            // Components can arrive after the initial session bind. Reuse the
            // observed contract for both the toggle and its flight actions.
            for (const auto& toggle : ToggleCatalog()) {
                if (toggle.id != state.id) continue;
                const auto* feature = FindFeature(toggle.onFeatureId);
                if (feature && feature->functionName == "Native_ExtendedFeature" && state.known)
                    GetBackend().SetCapability(toggle.onFeatureId, true, "参数契约已核对，可提交调用");
            }
            if (state.id == "flight") {
                for (const auto id : {"flight_up", "flight_down"})
                    GetBackend().SetCapability(id, state.known, state.known ? "飞行契约已核对；执行时另行检查启用状态" : state.reason);
            }
        }
        // Slice 0 is the combat group, then each movement toggle, the four extended
        // groups and each economy switch, ending with the statistics read-back.
        constexpr std::size_t ToggleSliceCount = 1 + MovementToggleIds.size() + 4 + 3;
        constexpr std::size_t StatusSliceCount = ToggleSliceCount + 1;
        StatusSchedule statusSchedule;
        std::vector<ToggleState> statusStates;
        void AppendToggleSlice(const Context& context, std::size_t slice, std::vector<ToggleState>& states) {
            if (slice) { AppendExtendedToggleSlice(context, slice - 1, states); return; }
            const auto invulnerable = ReadGameplayTagCount(context, "CharacterState.Stand.Invulnerable.Debug");
            states.push_back(ShachiToggleState());
            states.push_back(DarkSourceToggleState());
            states.push_back(PerfectDeflectState(context));
            states.push_back(PerfectDodgeState(context));
            states.push_back({"invulnerable", invulnerable.has_value(), invulnerable.value_or(0) > 0, invulnerable ? "能力系统无敌标签实时回读" : "无敌状态未读回"});
        }
        void PublishToggleStatesFrom(std::vector<ToggleState> states) {
            for (const auto& state : states) RefreshExtendedCapabilities(state);
            GetBackend().PublishToggleStates(std::move(states));
        }
        // Immediate pass for a just-executed request. Results gathered by a pass in
        // progress may predate that request, so they are dropped.
        void PublishToggles(const Context& context) {
            std::vector<ToggleState> states;
            for (std::size_t slice = 0; slice < ToggleSliceCount; ++slice) AppendToggleSlice(context, slice, states);
            statusStates.clear();
            statusSchedule.Restart();
            PublishToggleStatesFrom(std::move(states));
        }
        void StatusTick(const Context& context, Clock::time_point now) {
            const auto& backend = GetBackend();
            const bool immediate = backend.StatusWanted() || backend.RestorationPending();
            const auto slice = statusSchedule.Next(now, immediate, StatusSliceCount);
            if (slice == StatusSliceCount) return;
            PerformanceScope performance(7);
            if (slice < ToggleSliceCount) AppendToggleSlice(context, slice, statusStates);
            else {
                PublishReadback(ReadHealth(context), &context);
                PublishToggleStatesFrom(std::exchange(statusStates, {}));
            }
            if (statusSchedule.Finished(StatusSliceCount)) statusSchedule.Complete(now);
        }
        bool Signature(Address function, const ExpectedFunction& expected, std::string& reason) {
            if (!ObjectValid(function) || ObjectName(function) != expected.name ||
                Read<std::uint32_t>(function + 0x100) != 0x20601 ||
                Read<std::uint8_t>(function + 0x104) != expected.count ||
                Read<std::uint16_t>(function + 0x106) != expected.size ||
                Read<Address>(function + 0x128) != base + expected.nativeRva) {
                reason = "UFunction 地址、flags 或参数尺寸与本版本契约不符";
                return false;
            }
            std::array<bool, 8> found{};
            auto property = Read<Address>(function + 0x50);
            unsigned visited{};
            for (; property && visited < 32; ++visited) {
                const auto flags = Read<std::uint64_t>(property + 0x38);
                if ((flags & 0x80) && !(flags & 0x500)) {
                    const auto name = Name(Read<std::uint32_t>(property + 0x20)).value_or("");
                    const auto fieldType = Read<Address>(property + 8);
                    const auto type = Name(Read<std::uint32_t>(fieldType + 0x30)).value_or("");
                    bool matched{};
                    for (unsigned i = 0; i < expected.count; ++i) {
                        const auto& parameter = expected.parameters[i];
                        // Unreal comparison names preserve the spelling of the first
                        // registered FName (e.g. MedicineId may read as MedicineID).
                        const auto sameName = name.size() == parameter.name.size() &&
                            std::equal(name.begin(), name.end(), parameter.name.begin(), [](unsigned char a, unsigned char b) {
                                const auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
                                return fold(a) == fold(b);
                            });
                        if (!sameName) continue;
                        if (found[i] || type != PropertyType(parameter.type) ||
                            Read<std::int32_t>(property + 0x34) != SizeOf(parameter.type) ||
                            Read<std::int32_t>(property + 0x44) != parameter.offset) {
                            reason = "FProperty 类型、尺寸或偏移不匹配";
                            return false;
                        }
                        found[i] = true;
                        matched = true;
                        break;
                    }
                    if (!matched) { reason = "发现契约以外的参数"; return false; }
                } else {
                    reason = "不支持输出、返回值或非参数属性";
                    return false;
                }
                const auto next = Read<Address>(property + 0x10);
                if (next == property) return false;
                property = next;
            }
            if (property || !std::all_of(found.begin(), found.begin() + expected.count, [](bool value) { return value; })) {
                reason = "参数链不完整";
                return false;
            }
            return true;
        }
        bool CatalogMatches(const FeatureDef& feature, const ExpectedFunction& expected, std::string& reason) {
            if (feature.fixedArguments.size() + feature.params.size() != expected.count) {
                reason = "功能目录参数数量与已恢复签名不符";
                return false;
            }
            for (std::size_t i = 0; i < feature.params.size(); ++i) {
                if (feature.params[i].type != expected.parameters[i + feature.fixedArguments.size()].type) {
                    reason = "功能目录参数类型与已恢复签名不符";
                    return false;
                }
            }
            return true;
        }
        bool SpawnSignature(Address function) {
            if (!ObjectValid(function) || ObjectName(function) != "SpawnObject" ||
                Read<std::uint32_t>(function + 0x100) != 0x04022403 ||
                Read<std::uint8_t>(function + 0x104) != 3 || Read<std::uint16_t>(function + 0x106) != 24 ||
                Read<Address>(function + 0x128) != base + 0x3fa4970) return false;
            std::array<bool, 3> found{};
            auto property = Read<Address>(function + 0x50);
            for (unsigned i = 0; property && i < 8; ++i) {
                const auto name = Name(Read<std::uint32_t>(property + 0x20)).value_or("");
                const auto offset = Read<std::int32_t>(property + 0x44);
                const auto size = Read<std::int32_t>(property + 0x34);
                const auto flags = Read<std::uint64_t>(property + 0x38);
                const auto type = Name(Read<std::uint32_t>(Read<Address>(property + 8) + 0x30)).value_or("");
                const int index = name == "ObjectClass" ? 0 : name == "Outer" ? 1 : name == "ReturnValue" ? 2 : -1;
                if (index < 0 || found[index] || offset != index * 8 || size != 8 || !(flags & 0x80) ||
                    ((flags & 0x400) != 0) != (index == 2) || type != (index == 0 ? "ClassProperty" : "ObjectProperty")) return false;
                found[index] = true;
                property = Read<Address>(property + 0x10);
            }
            return !property && std::all_of(found.begin(), found.end(), [](bool value) { return value; });
        }
        bool EnsureCheat(Context& context, std::string& reason) {
            const auto owner = Read<Address>(base + 0xe0121e0);
            if (!ObjectValid(owner) || ObjectName(owner) != "PBZCheatManager") {
                reason = "PBZCheatManager 类尚未就绪"; return false;
            }
            if (context.cheat) {
                if (!ObjectValid(context.cheat) || (Read<std::uint32_t>(context.cheat + 8) & 0x10) ||
                    Read<Address>(context.cheat + 0x20) != context.controller ||
                    !IsClass(Read<Address>(context.cheat + 0x10), owner)) {
                    reason = "控制器已有的 CheatManager 对象不符合契约"; return false;
                }
                return true;
            }
            const auto creationKey = std::format("{:x}:{}:{:x}:{}", context.world, context.worldIndex,
                context.controller, context.controllerIndex);
            if (creationContext != creationKey) { creationContext = creationKey; creationAttempted = false; }
            if (creationAttempted) { reason = "本会话 CheatManager 初始化未成功；不会自动重试"; return false; }
            creationAttempted = true;
            if (!ObjectValid(context.cheatClass) || !IsClass(context.cheatClass, owner)) {
                reason = "控制器 CheatClass 不是 PBZCheatManager 派生类"; return false;
            }
            const auto within = Read<Address>(context.cheatClass + 0x130);
            if (!within || !IsClass(Read<Address>(context.controller + 0x10), within)) {
                reason = "CheatClass 的 ClassWithin 与控制器不符"; return false;
            }
            const auto function = Read<Address>(base + 0xdf46760);
            const auto spawnOwner = Read<Address>(base + 0xdf45bf8);
            const auto cdo = Read<Address>(spawnOwner + 0x160);
            if (!SpawnSignature(function) || !ObjectValid(cdo) || ObjectName(spawnOwner) != "GameplayStatics") {
                reason = "SpawnObject 反射入口未通过完整签名核对"; return false;
            }
            struct SpawnParameters { Address type, outer, result; } parameters{context.cheatClass, context.controller, 0};
            if (stopping.load() || !LocalGameThread() || !CallProcessEvent(cdo, function, &parameters)) {
                faulted.store(true); reason = "SpawnObject 调用异常，停止适配器；不会重试"; return false;
            }
            if (!ObjectValid(parameters.result) || Read<Address>(parameters.result + 0x20) != context.controller ||
                !IsClass(Read<Address>(parameters.result + 0x10), owner)) {
                reason = "SpawnObject 没有返回符合契约的 CheatManager"; return false;
            }
            const auto refreshed = CurrentContext();
            if (!refreshed || refreshed->Key() != context.Key() || refreshed->cheat) {
                reason = "创建期间玩家会话变化；未覆盖控制器引用"; return false;
            }
            if (!WriteAddress(context.controller + 0x418, parameters.result) ||
                Read<Address>(context.controller + 0x418) != parameters.result) {
                reason = "CheatManager 引用写入未成功；不会重试"; return false;
            }
            context.cheat = parameters.result;
            gloss::Log("PBZ CheatManager constructed on the verified game thread and retained by its controller");
            return true;
        }
        bool ParseUnsigned(std::string_view text, std::uint32_t& value) {
            int radix = 10;
            if (text.starts_with("0x") || text.starts_with("0X")) { text.remove_prefix(2); radix = 16; }
            if (text.empty()) return false;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, radix);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        }
        bool Pack(const ExpectedFunction& expected, const Request& request, std::vector<std::byte>& storage,
            std::vector<std::vector<wchar_t>>& strings, std::string& error) {
            if (request.arguments.size() != expected.count) { error = "请求参数数量已变化"; return false; }
            storage.assign(std::max<std::size_t>(expected.size, 1), std::byte{});
            strings.resize(expected.count);
            for (unsigned i = 0; i < expected.count; ++i) {
                const auto& argument = request.arguments[i];
                const auto& parameter = expected.parameters[i];
                const auto size = SizeOf(parameter.type);
                if (parameter.offset + size > storage.size()) { error = "参数写入超出契约尺寸"; return false; }
                auto* output = storage.data() + parameter.offset;
                switch (parameter.type) {
                case ParamType::Float: {
                    float value{};
                    const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || !std::isfinite(value)) {
                        error = "浮点参数无效"; return false;
                    }
                    std::memcpy(output, &value, size); break;
                }
                case ParamType::Int: {
                    std::int32_t value{};
                    const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size()) { error = "整数参数无效"; return false; }
                    std::memcpy(output, &value, size); break;
                }
                case ParamType::Name: {
                    const std::string_view text = argument;
                    const auto colon = text.find(':');
                    NameValue value;
                    if (!ParseUnsigned(text.substr(0, colon), value.comparison) ||
                        (colon != text.npos && !ParseUnsigned(text.substr(colon + 1), value.number)) || !Name(value.comparison)) {
                        error = "FName ID 不对应现有名称"; return false;
                    }
                    std::memcpy(output, &value, size); break;
                }
                case ParamType::String: {
                    if (argument.size() > 4096) { error = "字符串参数过长"; return false; }
                    const int count = argument.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        argument.data(), static_cast<int>(argument.size()), nullptr, 0);
                    if (!argument.empty() && count <= 0) { error = "字符串不是有效 UTF-8"; return false; }
                    auto& buffer = strings[i];
                    buffer.assign(static_cast<std::size_t>(count) + 1, L'\0');
                    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(),
                        static_cast<int>(argument.size()), buffer.data(), count);
                    const StringValue value{buffer.data(), count + 1, count + 1};
                    std::memcpy(output, &value, size); break;
                }
                default: error = "该参数类型尚未支持"; return false;
                }
            }
            return true;
        }
        #include "native_items.inc"
        #include "native_damage.inc"
        #include "native_cooldown.inc"
        #include "native_stealth.inc"
        #include "native_progress.inc"
        #include "native_extended.inc"
        #include "native_medicine.inc"

        ExecutionResult Execute(const Request& request) {
            if (stopping.load() || faulted.load() || !LocalGameThread()) return {RequestState::Failed, "游戏线程条件失效"};
            const auto context = CurrentContext();
            if (!context || context->Key() != activeSession || context->cheat != activeCheat || !ObjectValid(context->cheat)) {
                GetBackend().SetRuntimeState(false, "世界或玩家已变化，等待重新核对");
                return {RequestState::Indeterminate, "调用前会话失效，未执行"};
            }
            const auto* feature = FindFeature(request.featureId);
            if (request.featureId == "grant_item") return GrantItem(*context, request);
            if (request.featureId == "set_max_health") return SetMaxHealth(*context, request);
            if (request.featureId == "refill_medicine") return RefillMedicine(*context);
            if (feature && (feature->functionName == "Native_ExtendedFeature" || feature->functionName == "Native_ExtendedAction")) {
                const auto result = ExecuteExtended(*context, request);
                PublishToggles(*context);
                return result;
            }
            if (feature && feature->functionName == "Native_InfiniteDarkSource") {
                const auto result = ExecuteDarkSource(request);
                PublishToggles(*context);
                return result;
            }
            if (feature && feature->functionName == "Native_InfiniteShachi") {
                const auto result = ExecuteShachi(request);
                PublishToggles(*context);
                return result;
            }
            if (feature && (feature->functionName == "Native_PerfectDeflect" || feature->functionName == "Native_PerfectDodge")) {
                const auto result = ExecutePerfect(*context, request);
                PublishToggles(*context);
                return result;
            }
            if (feature && feature->functionName == "SetGlobalTimeDilation") return SetTimeDilation(*context, request);
            const auto* expected = feature ? Contract(feature->functionName) : nullptr;
            std::string reason;
            if (!feature || !expected || !CatalogMatches(*feature, *expected, reason)) return {RequestState::Failed, reason};
            const auto function = FindFunction(Read<Address>(context->cheat + 0x10), expected->name);
            if (!Signature(function, *expected, reason)) return {RequestState::Failed, reason};
            if (request.featureId == "get_location") return ReadLocation(*context);
            if (request.featureId == "get_health") {
                const auto health = ReadHealth(*context);
                PublishReadback(health, &*context);
                if (!health) return {RequestState::Failed, "生命属性对象或反射布局未通过核对，未调用 GM 日志入口"};
                return {RequestState::Verified, std::format("当前生命 {:.1f} / {:.1f}（真实属性回读）", health->current, health->maximum)};
            }
            std::vector<std::byte> parameters;
            std::vector<std::vector<wchar_t>> strings;
            if (!Pack(*expected, request, parameters, strings, reason)) return {RequestState::Failed, reason};
            // Refresh immediately before the only engine call; never dispatch a saved object from an old world.
            const auto current = CurrentContext();
            if (stopping.load() || !current || current->Key() != context->Key() || current->cheat != context->cheat || !LocalGameThread())
                return {RequestState::Failed, "参数准备期间玩家会话变化，未执行"};
            if (!CallProcessEvent(current->cheat, function, parameters.data())) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "游戏调用异常，适配器已停用；不会自动重试");
                return {RequestState::Indeterminate, "游戏调用异常，结果无法确认"};
            }
            ++dispatchedCalls;
            const auto after = CurrentContext();
            if (!after || after->Key() != current->Key()) {
                GetBackend().SetRuntimeState(false, "调用后世界或玩家会话变化");
                return {RequestState::Indeterminate, "调用后会话变化，效果与异步任务完成情况未知"};
            }
            if (request.featureId == "full_heal") {
                const auto health = ReadHealth(*after);
                PublishReadback(health, &*after);
                if (health) {
                    float target = health->maximum;
                    if (std::abs(health->current - target) <= std::max(0.01f, std::abs(target) * 1e-5f))
                        return {RequestState::Verified, std::format("生命属性回读 {:.1f}，与目标 {:.1f} 一致", health->current, target)};
                    return {RequestState::Dispatched, std::format("调用已返回；生命回读 {:.1f} / {:.1f}，与目标 {:.1f} 不一致", health->current, health->maximum, target)};
                }
            }
            if (request.featureId == "refill_ultimate") {
                const auto resource = ReadUltimate(*after);
                PublishReadback(ReadHealth(*after), &*after);
                if (resource && resource->current == 2147083648 && resource->maximum == 2147083648 &&
                    std::abs(resource->percentage - 100.0f) < 0.01f)
                    return {RequestState::Verified, "背包终结技次数及上限回读为 2147083648，充能 100%；该 GM 会提高资源上限"};
            }
            if (feature->functionName == "GM_State" && request.arguments.size() == 3 && request.arguments[0] == "preset") {
                PublishToggles(*after);
                const bool enabled = request.arguments[2] == "on";
                const auto& preset = request.arguments[1];
                if (preset == "invulnerable") {
                    const auto tag = "CharacterState.Stand.Invulnerable.Debug";
                    std::optional<bool> value;
                    if (const auto count = ReadGameplayTagCount(*after, tag)) value = *count > 0;
                    if (value && *value == enabled)
                        return {RequestState::Verified, std::format("状态标签 {} 回读为{}；实战伤害与换图行为需另行验证", tag, enabled ? "存在" : "不存在")};
                    return {RequestState::Dispatched, "状态命令已返回，标签回读未匹配目标；没有标记为已验证"};
                }
            }
            return {RequestState::Dispatched, "游戏线程调用已返回；效果、保存与异步任务完成情况尚未验证"};
        }
        void Tick(Context context) {
            PerformanceScope performance(0);
            // Hundreds of identity checks can touch the same executable page.
            // Share its protection query only within this synchronous Tick.
            struct ExecutableQueryScope {
                ExecutableQueryScope() { executablePages.clear(); cacheExecutableQueries = true; }
                ~ExecutableQueryScope() { cacheExecutableQueries = false; executablePages.clear(); }
            } executableQueryScope;
            static auto nextFaultReport = Clock::now() + std::chrono::seconds(10);
            const auto now = Clock::now();
            if (now >= nextFaultReport) {
                gloss::Log(std::format("PBZ perf read faults: {} slow executable queries: {}",
                    readFaults.exchange(0), slowExecutableQueries.exchange(0)));
                nextFaultReport = now + std::chrono::seconds(10);
            }
            lastControllerTick.store(GetTickCount64());
            observedThread.store(GetCurrentThreadId());
            const auto tick = ++observedTicks;
            if (!LocalGameThread()) {
                GetBackend().SetRuntimeState(false, "观察到控制器 Tick，但线程 ID 与 GGameThreadId 不符", context.Key());
                return;
            }
            if (tick == 1) gloss::Log(std::format("PBZ local ReceiveTick observed on thread {}; GGameThreadId={}",
                GetCurrentThreadId(), expectedThread.load()));
            if (faulted.load()) return;
            std::string reason;
            if (!EnsureCheat(context, reason)) { GetBackend().SetRuntimeState(false, std::move(reason), context.Key()); return; }
            const auto attributes = Read<Address>(context.pawn + 0x758);
            const bool attributesChanged = combatBinding.attributes && (attributes != combatBinding.attributes ||
                Read<std::uint32_t>(attributes + 0xc) != combatBinding.attributeIndex ||
                Read<std::uint32_t>(context.pawn + 0xc) != combatBinding.pawnIndex);
            if (!activeBound || !context.SameKey(activeContext) || activeCheat != context.cheat || attributesChanged || !GetBackend().IsReady()) {
                const auto key = context.Key();
                // A new session may have reused addresses; nothing it finds can be stale.
                propertyLookups.Clear();
                functionLookups.Clear();
                statusStates.clear();
                statusSchedule.Restart();
                // Object storage can be reused without changing Context::Key().
                // Invalidate queued work/read-back and re-arm saved toggles before rebinding.
                GetBackend().SetRuntimeState(false, "玩家对象已变化，等待重新绑定", key);
                CombatSessionChanged(context);
                const bool movementRestored = MovementSessionChanged(context);
                const bool extendedRestored = ExtendedSessionChanged(context);
                if (!movementRestored || !extendedRestored) {
                    GetBackend().SetRuntimeState(false, "原参数尚未恢复；飞行时请移动到空旷处", key);
                    return;
                }
                const bool combatReady = BindCombat(context);
                if (darkSourceEnabled && (!combatReady || !FillDarkSource())) darkSourceEnabled = false;
                if (faulted.load()) return;
                activeSession = key;
                activeContext = context;
                activeBound = true;
                activeCheat = context.cheat;
                GetBackend().SetRuntimeState(true, "运行时契约已核对；调用结果仍需游戏内验证", key);
                ResetItemImages(GetBackend().GetSnapshot().generation);
                for (const auto& feature : Catalog()) {
                    const auto* expected = Contract(feature.functionName);
                    reason.clear();
                    bool ready = expected && CatalogMatches(feature, *expected, reason);
                    if (ready) ready = Signature(FindFunction(Read<Address>(context.cheat + 0x10), expected->name), *expected, reason);
                    if (feature.functionName == "SetGlobalTimeDilation") {
                        ready = TimeDilationReady();
                        reason = ready ? "" : "时间缩放 Set/Get 反射契约未通过";
                    }
                    if (feature.functionName == "Native_GrantItem") {
                        // Per-item signatures and live inventory are checked again before every grant.
                        ready = true;
                    }
                    if (feature.functionName == "Native_InfiniteShachi") ready = combatReady;
                    if (feature.functionName == "Native_InfiniteDarkSource") ready = DarkSourceReady();
                    if (feature.functionName == "Native_SetMaxHealth") ready = MaxHealthReady(context);
                    // The backpack can load after this bind; every refill checks its contract again.
                    if (feature.functionName == "Native_RefillMedicine") ready = true;
                    if (feature.functionName == "Native_PerfectDeflect") ready = PerfectTagFunction(true) && PerfectTagFunction(false);
                    if (feature.functionName == "Native_PerfectDodge") ready = DodgeContract(context) && DodgeInputAction(context);
                    if ((feature.functionName == "Native_ExtendedFeature" || feature.functionName == "Native_ExtendedAction") && !feature.fixedArguments.empty())
                        ready = feature.id.ends_with("_off") || ExtendedCapability(context, feature.fixedArguments[0], reason);
                    GetBackend().SetCapability(feature.id, ready, ready ? "参数契约已核对，可提交调用" :
                        (reason.empty() ? feature.unavailableReason : reason));
                }
                PublishReadback(ReadHealth(context), &context);
            }
            MovementTick(context);
            StatusTick(context, now);
            RefreshItems(context);
            GetBackend().Pump(Execute, 1);
            { PerformanceScope combatPerformance(4); CombatTick(context); }
            ProcessItemImage(context);
        }
        void __fastcall HookActorProcessEvent(void* object, void* function, void* parameters) {
            ++callbacksInFlight;
            struct CallbackGuard { ~CallbackGuard() { --callbacksInFlight; } } callbackGuard;
            originalActorProcessEvent(object, function, parameters);
            if (combatCleanupRequested.load() && !insideAdapter && LocalGameThread()) {
                insideAdapter = true;
                const bool combat = CombatCleanup();
                const bool movement = MovementCleanup();
                const bool extended = ExtendedCleanup();
                combatCleanupComplete.store(combat && movement && extended);
                insideAdapter = false;
                return;
            }
            if (stopping.load() || insideAdapter || faulted.load()) return;
            insideAdapter = true;
            struct Guard { ~Guard() { insideAdapter = false; } } guard;
            try {
                if (ObjectName(reinterpret_cast<Address>(function)) != "ReceiveTick") return;
                PerformanceScope performance(6);
                const auto context = CurrentContext();
                if (!context) { GetBackend().SetRuntimeState(false, "等待有效世界和本地玩家"); return; }
                if (reinterpret_cast<Address>(object) == context->controller) Tick(*context);
            } catch (...) {
                faulted.store(true);
                GetBackend().SetRuntimeState(false, "原生适配器异常，已停止处理请求");
            }
        }
        DWORD WINAPI MonitorController(void*) {
            // Startup injection normally precedes player creation. Only inspect and
            // attach vtables here; all UObject construction/engine calls stay in Tick.
            while (!stopping.load() && !faulted.load()) {
                try {
                    UpdateItemCulture();
                    const auto context = CurrentContext();
                    if (context) {
                        const auto slot = Read<Address>(context->controller) + 0x260;
                        std::lock_guard lock(slotMutex);
                        const auto current = Read<Address>(slot);
                        if (!stopping.load() && current == base + ActorProcessEventRva) {
                            if (std::find(patchedSlots.begin(), patchedSlots.end(), slot) == patchedSlots.end())
                                patchedSlots.push_back(slot);
                            if (ReplaceSlot(slot, current, reinterpret_cast<Address>(&HookActorProcessEvent))) {
                                installed.store(true);
                                gloss::Log("PBZ controller callback attached; waiting for game-thread Tick");
                            }
                        }
                    }
                    if (GetTickCount64() - lastControllerTick.load() > 2000 && GetBackend().IsReady())
                        GetBackend().SetRuntimeState(false, "等待本地玩家 Tick；加载或暂停期间停止接受请求");
                } catch (...) {
                    faulted.store(true);
                    GetBackend().SetRuntimeState(false, "控制器监测异常，适配器停止");
                }
                Sleep(250);
            }
            return 0;
        }
        std::optional<std::string> ExecutableHash(const std::wstring& path) {
            const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file == INVALID_HANDLE_VALUE) return {};
            struct FileGuard { HANDLE handle; ~FileGuard() { CloseHandle(handle); } } fileGuard{file};
            BCRYPT_ALG_HANDLE algorithm{};
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
            struct AlgorithmGuard { BCRYPT_ALG_HANDLE handle; ~AlgorithmGuard() { BCryptCloseAlgorithmProvider(handle, 0); } } algorithmGuard{algorithm};
            BCRYPT_HASH_HANDLE hash{};
            if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
            struct HashGuard { BCRYPT_HASH_HANDLE handle; ~HashGuard() { BCryptDestroyHash(handle); } } hashGuard{hash};
            std::vector<unsigned char> buffer(1024 * 1024);
            DWORD count{};
            for (;;) {
                if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) return {};
                if (!count) break;
                if (BCryptHashData(hash, buffer.data(), count, 0) < 0) return {};
            }
            std::array<unsigned char, 32> digest{};
            if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            result.reserve(64);
            for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
            return result;
        }
        std::string JsonString(std::string_view text) {
            std::string result = "\"";
            for (const unsigned char c : text) {
                if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
                else if (c < 0x20) result += std::format("\\u{:04x}", c);
                else result += static_cast<char>(c);
            }
            result += '"';
            return result;
        }
        const char* StoreDebug(std::string_view text) {
            if (text.size() >= sizeof(PBZ_DebugBuffer)) text = R"({"error":"debug snapshot too large"})";
            std::memcpy(PBZ_DebugBuffer, text.data(), text.size());
            PBZ_DebugBuffer[text.size()] = '\0';
            return PBZ_DebugBuffer;
        }
    }

    bool Initialize() {
        ItemPublish(false, "正在等待游戏物品目录验证");
        if (initialized.exchange(true)) return installed.load();
        base = reinterpret_cast<Address>(GetModuleHandleW(nullptr));
        executableImage.Capture(base);
        std::wstring path(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) { GetBackend().SetRuntimeState(false, "无法读取主程序路径"); return false; }
        path.resize(length);
        const auto slash = path.find_last_of(L"/\\");
        if (_wcsicmp(path.c_str() + (slash == path.npos ? 0 : slash + 1), L"PBZ.exe") != 0) {
            GetBackend().SetRuntimeState(false, "当前为图形测试宿主，PBZ 原生适配器未启用"); return false;
        }
        const auto hash = ExecutableHash(path);
        if (!hash || *hash != SupportedHash) {
            GetBackend().SetRuntimeState(false, "PBZ.exe SHA-256 与受支持版本不符，原生调用已禁用"); return false;
        }
        std::array<unsigned char, ActorEntry.size()> actor{};
        std::array<unsigned char, ObjectEntry.size()> object{};
        if (!ReadBytes(base + ActorProcessEventRva, actor.data(), actor.size()) || actor != ActorEntry ||
            !ReadBytes(base + ObjectProcessEventRva, object.data(), object.size()) || object != ObjectEntry) {
            GetBackend().SetRuntimeState(false, "ProcessEvent 入口指令与版本契约不符，未安装回调"); return false;
        }
        // Pin before publishing any pointer into this module. A thread can already
        // have loaded a vtable target before Shutdown restores it; refcounting the
        // callbacks alone cannot prove that unloading is safe.
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&HookActorProcessEvent), &pinned)) {
            GetBackend().SetRuntimeState(false, "无法固定适配器模块生命周期，未安装回调"); return false;
        }
        modulePinned.store(true);
        const auto minHookStatus = MH_Initialize();
        if (minHookStatus == MH_OK || minHookStatus == MH_ERROR_ALREADY_INITIALIZED) {
            PrepareEconomyHooks();
            PrepareCooldownHook();
            PrepareShachiCostHooks();
        }
        originalActorProcessEvent = reinterpret_cast<ProcessEvent>(base + ActorProcessEventRva);
        GetBackend().SetRuntimeState(false, "版本已核对，等待本地玩家与游戏线程 Tick");
        monitorThread = CreateThread(nullptr, 0, MonitorController, nullptr, 0, nullptr);
        if (!monitorThread) { GetBackend().SetRuntimeState(false, "无法启动玩家监测，未安装回调"); return false; }
        gloss::Log("PBZ exact-build native adapter monitor started; engine calls await verified local-controller ReceiveTick");
        return true;
    }

    bool Shutdown() {
        if (installed.load() && !stopping.load()) {
            combatCleanupRequested.store(true);
            for (unsigned wait = 0; wait < 1500 && !combatCleanupComplete.load(); ++wait) Sleep(1);
            if (!combatCleanupComplete.load()) {
                if (faulted.load()) RestoreShachiHooks();
                gloss::Log("PBZ shutdown awaits game-thread combat cleanup; module remains attached");
                return false;
            }
        }
        stopping.store(true);
        const bool shachiRestored = RestoreShachiHooks();
        const bool damageRestored = RestoreDamageHooks();
        const bool cooldownRestored = RestoreCooldownHooks();
        const bool economyRestored = RestoreExtendedHooks();
        const bool combatRestored = shachiRestored && damageRestored && cooldownRestored && economyRestored;
        GetBackend().SetRuntimeState(false, "适配器正在停止；模块固定到进程退出");
        if (monitorThread) {
            if (WaitForSingleObject(monitorThread, 1500) != WAIT_OBJECT_0) return false;
            CloseHandle(monitorThread);
            monitorThread = nullptr;
        }
        {
            std::lock_guard lock(slotMutex);
            for (const auto slot : patchedSlots) {
                const auto current = Read<Address>(slot);
                if (current == reinterpret_cast<Address>(&HookActorProcessEvent)) {
                    if (!ReplaceSlot(slot, current, base + ActorProcessEventRva)) return false;
                } else if (current != base + ActorProcessEventRva) {
                    gloss::Log("PBZ callback slot ownership changed; shutdown did not overwrite another hook");
                    return false;
                }
            }
        }
        installed.store(false);
        // This wait is diagnostic only. Pinning is what makes late entrants safe.
        for (unsigned wait = 0; wait < 1000 && callbacksInFlight.load(); ++wait) Sleep(1);
        return combatRestored && callbacksInFlight.load() == 0;
    }

    const char* DebugStatus() {
        std::lock_guard lock(debugMutex);
        const auto snapshot = GetBackend().GetSnapshot();
        std::string text = std::format("{{\"installed\":{},\"ready\":{},\"faulted\":{},\"observedThread\":{},\"expectedGameThread\":{},\"observedTicks\":{},\"dispatches\":{},\"generation\":{},\"pending\":{},\"status\":{},\"modulePinnedUntilExit\":{},\"callbacksInFlight\":{},\"capabilities\":[",
            installed.load(), snapshot.ready, faulted.load(), observedThread.load(), expectedThread.load(),
            observedTicks.load(), dispatchedCalls.load(), snapshot.generation, snapshot.pendingCount, JsonString(snapshot.status), modulePinned.load(), callbacksInFlight.load());
        bool first = true;
        for (const auto& capability : snapshot.capabilities) {
            if (!first) text += ',';
            first = false;
            text += std::format("{{\"id\":{},\"ready\":{},\"reason\":{}}}", JsonString(capability.featureId), capability.ready, JsonString(capability.reason));
        }
        text += "],\"statistics\":[";
        first = true;
        for (const auto& statistic : snapshot.statistics) {
            if (!first) text += ',';
            first = false;
            text += std::format("{{\"id\":{},\"label\":{},\"value\":{}}}",
                JsonString(statistic.id), JsonString(statistic.label), JsonString(statistic.value));
        }
        text += "],\"toggleStates\":[";
        first = true;
        for (const auto& state : snapshot.toggleStates) {
            if (!first) text += ',';
            first = false;
            text += std::format("{{\"id\":{},\"known\":{},\"enabled\":{},\"reason\":{},\"parameters\":[",
                JsonString(state.id), state.known, state.enabled, JsonString(state.reason));
            for (std::size_t i = 0; i < state.parameters.size(); ++i) {
                if (i) text += ',';
                text += JsonString(state.parameters[i]);
            }
            text += "]}";
        }
        text += std::format("],\"itemPageActive\":{},\"itemsReady\":{},\"itemCount\":{},\"itemCatalogStatus\":{},\"requests\":[",
            GetBackend().ItemPageActive(), snapshot.itemsReady, snapshot.items.size(), JsonString(snapshot.itemCatalogStatus));
        first = true;
        for (const auto& record : snapshot.requests) {
            if (!first) text += ',';
            first = false;
            text += std::format("{{\"id\":{},\"feature\":{},\"state\":{},\"stateCode\":{},\"message\":{}}}",
                record.request.requestId, JsonString(record.request.featureId), JsonString(RequestStateLabel(record.state)),
                static_cast<int>(record.state), JsonString(record.message));
        }
        text += std::format("],\"shachiPreventedWrites\":{},\"darkSourcePreventedWrites\":{}}}",
            shachiPreventedWrites.load(), darkSourcePreventedWrites.load());
        return StoreDebug(text);
    }
    const char* DebugSubmit(const char* command) {
        std::lock_guard lock(debugMutex);
        std::string text;
        bool terminated{};
        for (std::size_t i = 0; i < 4096; ++i) {
            char c{};
            if (!ReadBytes(reinterpret_cast<Address>(command) + i, &c, 1)) break;
            if (!c) { terminated = true; break; }
            text += c;
        }
        if (!terminated) return StoreDebug(R"({"accepted":false,"message":"invalid command"})");
        const auto separator = text.find('|');
        const auto id = text.substr(0, separator);
        std::vector<std::string> arguments;
        if (separator != text.npos) {
            std::size_t start = separator + 1;
            for (;;) {
                const auto end = text.find('|', start);
                arguments.push_back(text.substr(start, end == text.npos ? end : end - start));
                if (end == text.npos) break;
                start = end + 1;
            }
        }
        const auto result = GetBackend().Submit(id, std::move(arguments));
        return StoreDebug(std::format("{{\"accepted\":{},\"requestId\":{},\"message\":{}}}",
            result.accepted, result.requestId, JsonString(result.message)));
    }

    const char* DebugItems(int offset) {
        std::lock_guard lock(debugMutex);
        const auto snapshot = GetBackend().GetSnapshot();
        const auto start = std::min<std::size_t>(std::max(offset, 0), snapshot.items.size());
        const auto end = std::min(start + 20, snapshot.items.size());
        std::string text = std::format("{{\"ready\":{},\"total\":{},\"offset\":{},\"next\":{},\"items\":[",
            snapshot.itemsReady, snapshot.items.size(), start, end);
        for (auto i = start; i < end; ++i) {
            const auto& item = snapshot.items[i];
            if (i != start) text += ',';
            text += std::format("{{\"key\":{},\"name\":{},\"category\":{},\"owned\":{},\"maxQuantity\":{},\"available\":{},\"reason\":{},\"description\":{},\"iconKey\":{}}}",
                JsonString(item.key), JsonString(item.name), JsonString(item.category), item.owned,
                item.maxQuantity, item.available, JsonString(item.reason), JsonString(item.description), JsonString(item.iconKey));
        }
        text += "]}";
        return StoreDebug(text);
    }
}

extern "C" __declspec(dllexport) const char* PBZ_DebugSubmit(const char* command) {
    try { return gloss::gameplay::native::DebugSubmit(command); } catch (...) { return "{\"accepted\":false,\"message\":\"adapter exception\"}"; }
}
extern "C" __declspec(dllexport) const char* PBZ_DebugStatus() {
    try { return gloss::gameplay::native::DebugStatus(); } catch (...) { return "{\"error\":\"adapter exception\"}"; }
}
extern "C" __declspec(dllexport) const char* PBZ_DebugItems(int offset) {
    try { return gloss::gameplay::native::DebugItems(offset); } catch (...) { return "{\"error\":\"adapter exception\"}"; }
}
extern "C" __declspec(dllexport) int PBZ_InitializeGameplayOnly() {
    try { return gloss::gameplay::native::Initialize() ? 1 : 0; } catch (...) { return 0; }
}
extern "C" __declspec(dllexport) int PBZ_ShutdownGameplayOnly() {
    try { return gloss::gameplay::native::Shutdown() ? 1 : 0; } catch (...) { return 0; }
}
