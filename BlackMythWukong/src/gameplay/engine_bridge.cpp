#include "engine_bridge.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <algorithm>
#include <atomic>
#include <format>
#include <set>

namespace gloss::gameplay::engine
{
    namespace
    {
        template <class F, class R, class... A>
        bool Safe(F f, R &result, A... args) noexcept
        {
            __try
            {
                result = f(args...);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        template <class F, class... A>
        bool SafeVoid(F f, A... args) noexcept
        {
            __try
            {
                f(args...);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        struct Api
        {
            std::int32_t (*thread)();
            void (*string)(const wchar_t *, ScriptArray *);
            void (*destroy)(ScriptArray *);
            void *(*find)(void *, void *, ScriptArray *, std::int32_t);
            void *(*load)(void *, void *, ScriptArray *, ScriptArray *, std::uint32_t, void *, std::int32_t);
            void *(*cdo)(void *, std::int32_t);
            void (*event)(void *, void *, void *);
            std::uint16_t (*size)(void *);
            void *(*children)(void *);
            void *(*objectClass)(void *);
            void *(*world)(void *);
            void *(*super)(void *);
            int (*structureSize)(void *);
            void *(*next)(void *);
            void (*name)(void *, ScriptArray *);
            void (*objectName)(void *, ScriptArray *);
            int (*offset)(void *);
            int (*internalOffset)(void *);
            int (*element)(void *);
            int (*dim)(void *);
            std::uint64_t (*flags)(void *);
            std::int32_t (*cast)(void *, std::uint64_t);
            void (*setBool)(void *, void *, std::int32_t);
            std::int32_t (*getBool)(void *, void *);
            std::int32_t (*isA)(void *, void *);
            std::int32_t (*valid)(void *);
            std::int32_t (*pendingKill)(void *);
            std::int32_t (*gc)();
            void *(*inner)(void *);
            void *(*construct)(void *, void *, void *, std::uint32_t, std::int32_t, void *, std::int32_t, void *);
            void *(*console)();
            void *(*findConsole)(void *, ScriptArray *);
            std::int32_t (*executeConsole)(void *, ScriptArray *, void *, void *);
            void *(*log)();
            void (*frame)(void *, void (*)(), std::uint64_t *, std::int32_t);
            void (*weakSet)(WeakObject *, void *);
            void *(*weakGet)(WeakObject *);
            std::int32_t (*weakValid)(WeakObject *, std::int32_t, std::int32_t);
        } api{};
        std::atomic_bool ready{};
        std::uint64_t frameHandle{};
        std::uint64_t CastFlag(Type type)
        {
            switch (type)
            {
            case Type::Object:
                return 0x4000000ull;
            case Type::Int:
                return 0x80ull;
            case Type::Float:
                return 0x100ull;
            case Type::Double:
                return 0x100000000ull;
            case Type::Bool:
                return 0x20000ull;
            case Type::Vector:
                return 0x100000ull;
            case Type::String:
                return 0x4000ull;
            case Type::ObjectArray:
                return 0x200000ull;
            case Type::Enum:
                return 0x1000000000000ull;
            }
            return 0;
        }
        bool ValidObject(std::uintptr_t object)
        {
            std::int32_t valid{}, dead{}, gc = 1;
            return IsGameThread() && object && Read<std::uintptr_t>(object) && Safe(api.gc, gc) && !gc &&
                   Safe(api.valid, valid, reinterpret_cast<void *>(object)) && valid == 1 &&
                   Safe(api.pendingKill, dead, reinterpret_cast<void *>(object)) && !dead;
        }
        std::wstring Wide(ScriptArray value)
        {
            if (!value.data || value.num < 1 || value.num > 4096 || value.capacity < value.num)
                return {};
            std::wstring result(value.num, L'\0');
            if (!ReadMemory(reinterpret_cast<std::uintptr_t>(value.data), result.data(), result.size() * 2) || result.back())
                return {};
            result.pop_back();
            return result;
        }
        std::string Utf8(std::wstring_view value)
        {
            const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(count, '\0');
            if (count)
                WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
            return result;
        }
        bool IsObject(std::uintptr_t object, const wchar_t *type) noexcept
        {
            if (!IsGameThread() || !object || !Read<std::uintptr_t>(object))
                return false;
            std::int32_t valid{};
            if (!ValidObject(object))
                return false;
            try
            {
                const auto cls = Find(type);
                return cls && Safe(api.isA, valid, reinterpret_cast<void *>(object), reinterpret_cast<void *>(cls)) && valid == 1;
            }
            catch (...)
            {
                return false;
            }
        }
    }
    std::unordered_map<std::string, std::uintptr_t> DiscoverBindings(std::span<const std::byte> image)
    {
        std::unordered_map<std::string, std::uintptr_t> result;
        if (image.size() < sizeof(IMAGE_DOS_HEADER))
            return result;
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || image.size() < sizeof(IMAGE_NT_HEADERS64) || static_cast<std::size_t>(dos->e_lfanew) > image.size() - sizeof(IMAGE_NT_HEADERS64))
            return result;
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return result;
        const auto sectionOffset = static_cast<std::size_t>(dos->e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
        if (sectionOffset > image.size() || nt->FileHeader.NumberOfSections > (image.size() - sectionOffset) / sizeof(IMAGE_SECTION_HEADER))
            return result;
        const auto sections = std::span(reinterpret_cast<const IMAGE_SECTION_HEADER *>(image.data() + sectionOffset), nt->FileHeader.NumberOfSections);
        const auto inSection = [&](std::int64_t address, std::size_t length, DWORD flags, bool writable = false)
        {
            if (address < 0 || static_cast<std::uint64_t>(address) > image.size() || length > image.size() - static_cast<std::size_t>(address))
                return false;
            for (const auto &s : sections)
                if ((s.Characteristics & flags) == flags && (writable || !(s.Characteristics & IMAGE_SCN_MEM_WRITE)) && address >= s.VirtualAddress &&
                    static_cast<std::uint64_t>(address) + length <= static_cast<std::uint64_t>(s.VirtualAddress) + s.Misc.VirtualSize)
                    return true;
            return false;
        };
        std::set<std::string> duplicate;
        for (const auto &section : sections)
        {
            if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || section.VirtualAddress >= image.size())
                continue;
            const auto end = (std::min)(image.size(), static_cast<std::size_t>(section.VirtualAddress) + section.Misc.VirtualSize);
            for (std::size_t i = section.VirtualAddress; i + 16 <= end; ++i)
            {
                const auto *p = reinterpret_cast<const unsigned char *>(image.data() + i);
                if (p[0] != 0x48 || p[1] != 0x8d || p[2] != 0x15 || p[7] != 0x48 || p[8] != 0x8d || p[9] != 0x0d || p[14] != 0xff || p[15] != 0xd3)
                    continue;
                std::int32_t a{}, b{};
                std::memcpy(&a, p + 3, 4);
                std::memcpy(&b, p + 10, 4);
                const auto name = static_cast<std::int64_t>(i) + 7 + a, fn = static_cast<std::int64_t>(i) + 14 + b;
                if (!inSection(name, 8, IMAGE_SCN_MEM_READ) || !inSection(fn, 1, IMAGE_SCN_MEM_EXECUTE))
                    continue;
                std::string key;
                for (std::size_t j = 0; j < 160 && inSection(name + j, 1, IMAGE_SCN_MEM_READ); ++j)
                {
                    const auto c = static_cast<char>(image[static_cast<std::size_t>(name) + j]);
                    if (!c)
                        break;
                    if (c < 32 || c > 126)
                    {
                        key.clear();
                        break;
                    }
                    key += c;
                }
                if (!key.starts_with("Export_") || key.size() >= 160 || !inSection(name + key.size(), 1, IMAGE_SCN_MEM_READ) || image[name + key.size()] != std::byte{})
                    continue;
                if (result.contains(key))
                    duplicate.insert(key);
                else
                    result.emplace(key, reinterpret_cast<std::uintptr_t>(image.data()) + fn);
            }
        }
        for (const auto &key : duplicate)
            result.erase(key);
        return result;
    }
    bool Initialize(HMODULE module)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto dos = Read<IMAGE_DOS_HEADER>(base);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 0x10000)
            return false;
        const auto nt = Read<IMAGE_NT_HEADERS64>(base + dos.e_lfanew);
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.SizeOfImage > 0x70000000)
            return false;
        const auto found = DiscoverBindings({reinterpret_cast<const std::byte *>(module), nt.OptionalHeader.SizeOfImage});
        bool complete = true;
        const auto bind = [&](auto &fn, const char *name)
        {
            const auto it = found.find(name);
            if (it == found.end())
            {
                Log(std::format("Engine binding missing or ambiguous: {}", name));
                complete = false;
            }
            else
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(it->second);
        };
        bind(api.thread, "Export_FThreading_IsInGameThread");
        bind(api.string, "Export_FString_FromCharPtr");
        bind(api.destroy, "Export_FScriptArray_Destroy");
        bind(api.find, "Export_UObjectGlobals_StaticFindObject");
        bind(api.cdo, "Export_UClass_GetDefaultObject");
        bind(api.load, "Export_UObjectGlobals_StaticLoadObject");
        bind(api.event, "Export_UObject_ProcessEvent");
        bind(api.size, "Export_UFunction_Get_ParmsSize");
        bind(api.children, "Export_UStruct_Get_ChildProperties");
        bind(api.next, "Export_FField_Get_Next");
        bind(api.objectClass, "Export_UObjectBase_GetClass");
        bind(api.super, "Export_UStruct_GetSuperStruct");
        bind(api.world, "Export_UObject_GetWorld");
        bind(api.structureSize, "Export_UStruct_GetPropertiesSize");
        bind(api.internalOffset, "Export_FProperty_GetOffset_ForInternal");
        bind(api.name, "Export_FField_GetName");
        bind(api.objectName, "Export_UObjectBaseUtility_GetName");
        bind(api.offset, "Export_FProperty_GetOffset_ForUFunction");
        bind(api.element, "Export_FProperty_Get_ElementSize");
        bind(api.dim, "Export_FProperty_Get_ArrayDim");
        bind(api.flags, "Export_FProperty_Get_PropertyFlags");
        bind(api.cast, "Export_FField_HasAnyCastFlags");
        bind(api.setBool, "Export_FBoolProperty_SetPropertyValue");
        bind(api.getBool, "Export_FBoolProperty_GetPropertyValue");
        bind(api.isA, "Export_UObjectBaseUtility_IsA");
        bind(api.valid, "Export_UObjectBase_IsValidLowLevel");
        bind(api.frame, "Export_FCoreDelegates_Reg_OnEndFrame");
        bind(api.pendingKill, "Export_UObjectBaseUtility_IsPendingKillOrUnreachable");
        bind(api.gc, "Export_UObjectGlobals_IsGarbageCollecting");
        bind(api.construct, "Export_UObjectGlobals_StaticConstructObject_Internal");
        bind(api.inner, "Export_FArrayProperty_Get_Inner");
        bind(api.console, "Export_IConsoleManager_Get");
        bind(api.findConsole, "Export_IConsoleManager_FindConsoleObject");
        bind(api.executeConsole, "Export_IConsoleManager_ProcessUserConsoleInput");
        bind(api.log, "Export_FGlobals_Get_GLog");
        const auto optional = [&](auto &fn, const char *name)
        {const auto it=found.find(name);fn=it==found.end()?nullptr:reinterpret_cast<std::remove_reference_t<decltype(fn)>>(it->second); };
        optional(api.weakSet, "Export_FWeakObjectPtr_SetUObject");
        optional(api.weakGet, "Export_FWeakObjectPtr_Get");
        optional(api.weakValid, "Export_FWeakObjectPtr_IsValid");
        ready.store(complete, std::memory_order_release);
        Log(std::format("Engine bridge: {} validated registrations; bindings {}", found.size(), complete ? "ready" : "unavailable"));
        return complete;
    }
    bool Ready() { return ready.load(std::memory_order_acquire); }
    bool IsGameThread() noexcept
    {
        std::int32_t result{};
        return Ready() && Safe(api.thread, result) && result == 1;
    }
    bool RegisterFrame(void (*callback)()) noexcept
    {
        return IsGameThread() && (frameHandle || (SafeVoid(api.frame, nullptr, callback, &frameHandle, 1) && frameHandle));
    }
    bool IsPawn(std::uintptr_t object) noexcept { return IsObject(object, L"/Script/Engine.Pawn"); }
    bool IsActor(std::uintptr_t object) noexcept { return IsObject(object, L"/Script/Engine.Actor"); }
    std::uintptr_t Find(std::wstring_view path)
    {
        if (!IsGameThread() || path.empty() || path.size() > 512)
            return 0;
        ScriptArray name{};
        const std::wstring text(path);
        if (!SafeVoid(api.string, text.c_str(), &name))
            return 0;
        void *result{};
        Safe(api.find, result, nullptr, nullptr, &name, 0);
        SafeVoid(api.destroy, &name);
        return reinterpret_cast<std::uintptr_t>(result);
    }
    std::uintptr_t Construct(std::uintptr_t unrealClass, std::uintptr_t outer)
    {
        if (!ValidObject(unrealClass) || !ValidObject(outer))
            return 0;
        std::uint64_t name{};
        void *object{};
        return Safe(api.construct, object, reinterpret_cast<void *>(unrealClass), reinterpret_cast<void *>(outer), &name, 0u, 0, nullptr, 0, nullptr) ? reinterpret_cast<std::uintptr_t>(object) : 0;
    }
    std::uintptr_t Load(std::wstring_view path, std::wstring_view unrealClass)
    {
        if (!IsGameThread())
            return 0;
        if (const auto existing = Find(path))
            return existing;
        const auto cls = Find(unrealClass);
        if (!cls || path.empty() || path.size() > 512)
            return 0;
        ScriptArray name{}, filename{};
        const std::wstring text(path);
        void *result{};
        if (!SafeVoid(api.string, text.c_str(), &name))
            return 0;
        Safe(api.load, result, reinterpret_cast<void *>(cls), nullptr, &name, &filename, 0u, nullptr, 1);
        SafeVoid(api.destroy, &name);
        return reinterpret_cast<std::uintptr_t>(result);
    }
    std::uintptr_t World(std::uintptr_t object)
    {
        void *world{};
        return ValidObject(object) && Safe(api.world, world, reinterpret_cast<void *>(object)) ? reinterpret_cast<std::uintptr_t>(world) : 0;
    }
    bool ConsoleExists(std::wstring_view name)
    {
        if (!IsGameThread() || name.empty() || name.size() > 128)
            return false;
        ScriptArray text{};
        void *manager{};
        void *object{};
        const std::wstring terminated(name);
        if (!Safe(api.console, manager) || !manager || !SafeVoid(api.string, terminated.c_str(), &text))
            return false;
        Safe(api.findConsole, object, manager, &text);
        SafeVoid(api.destroy, &text);
        return object != nullptr;
    }
    CallResult ExecuteConsole(std::wstring_view command, std::uintptr_t world)
    {
        if (!IsGameThread() || !ValidObject(world) || command.empty() || command.size() > 1024)
            return {false, "控制台上下文无效"};
        const auto name = command.substr(0, command.find(L' '));
        if (!ConsoleExists(name))
            return {false, "游戏尚未注册此指令"};
        void *manager{};
        void *log{};
        ScriptArray text{};
        std::int32_t accepted{};
        const std::wstring terminated(command);
        if (!Safe(api.console, manager) || !manager || !Safe(api.log, log) || !log || !SafeVoid(api.string, terminated.c_str(), &text))
            return {false, "游戏控制台未就绪"};
        const bool ok = Safe(api.executeConsole, accepted, manager, &text, log, reinterpret_cast<void *>(world));
        SafeVoid(api.destroy, &text);
        return {ok && accepted == 1, ok && accepted == 1 ? "游戏指令已执行" : "游戏拒绝了此指令"};
    }
    std::string ObjectName(std::uintptr_t object)
    {
        if (!IsActor(object))
            return {};
        ScriptArray name{};
        SafeVoid(api.objectName, reinterpret_cast<void *>(object), &name);
        auto result = Utf8(Wide(name));
        SafeVoid(api.destroy, &name);
        return result;
    }
    CallResult Call(std::wstring_view path, std::span<Arg> args, std::uintptr_t instance)
    {
        if (!IsGameThread())
            return {false, "操作必须在游戏线程执行"};
        if (instance && !ValidObject(instance))
            return {false, "游戏对象已经失效"};
        const auto fn = Find(path);
        if (!fn)
            return {false, "当前游戏未找到此接口"};
        if (!instance)
        {
            const auto colon = path.find(L':');
            const auto cls = Find(path.substr(0, colon));
            void *object{};
            if (!cls || !Safe(api.cdo, object, reinterpret_cast<void *>(cls), 1) || !object)
                return {false, "接口默认对象未就绪"};
            instance = reinterpret_cast<std::uintptr_t>(object);
        }
        std::uint16_t size{};
        if (!Safe(api.size, size, reinterpret_cast<void *>(fn)) || size > 16384 || (!size && !args.empty()))
            return {false, "接口参数大小无效"};
        std::vector<std::uint64_t> storage((size + 7) / 8);
        auto *buffer = reinterpret_cast<std::byte *>(storage.data());
        struct Property
        {
            void *ptr;
            int offset, size;
            std::uint64_t flags;
        };
        std::unordered_map<std::string, Property> properties;
        void *field{};
        Safe(api.children, field, reinterpret_cast<void *>(fn));
        for (int i = 0; field && i < 128; ++i)
        {
            ScriptArray name{};
            std::uint64_t flags{};
            int offset{}, element{}, dim{};
            const bool ok = SafeVoid(api.name, field, &name) && Safe(api.flags, flags, field) && Safe(api.offset, offset, field) &&
                            Safe(api.element, element, field) && Safe(api.dim, dim, field);
            auto key = Utf8(Wide(name));
            SafeVoid(api.destroy, &name);
            if (!ok || key.empty() || !(flags & 0x80) || offset < 0 || offset > size || element <= 0 || element > size - offset || dim != 1 || properties.contains(key))
                return {false, "反射参数结构不匹配"};
            properties.emplace(key, Property{field, offset, element, flags});
            void *next{};
            if (!Safe(api.next, next, field))
                return {false, "反射参数读取失败"};
            field = next;
        }
        if (field || properties.size() != args.size())
            return {false, "接口参数数量不匹配"};
        std::set<std::string> supplied;
        // Validate the entire signature before constructing strings or calling game code.
        for (const auto &arg : args)
        {
            if (arg.type == Type::Object && !arg.output && arg.Get<std::uintptr_t>() && !ValidObject(arg.Get<std::uintptr_t>()))
                return {false, "参数对象已经失效"};
            const auto it = properties.find(arg.name);
            if (it == properties.end() || !supplied.insert(arg.name).second)
                return {false, "接口参数名不匹配"};
            const auto &p = it->second;
            const std::uint64_t castFlag = CastFlag(arg.type);
            std::int32_t matches{};
            if (!Safe(api.cast, matches, p.ptr, castFlag) || matches != 1)
                return {false, "接口参数类型不匹配"};
            if (arg.type == Type::ObjectArray)
            {
                void *inner{};
                int element{}, dim{};
                if (!Safe(api.inner, inner, p.ptr) || !inner || !Safe(api.cast, matches, inner, CastFlag(Type::Object)) || matches != 1 ||
                    !Safe(api.element, element, inner) || element != 8 || !Safe(api.dim, dim, inner) || dim != 1)
                    return {false, "接口数组元素类型不匹配"};
            }
            if (arg.output != bool(p.flags & (0x100 | 0x400)))
                return {false, "接口参数方向不匹配"};
            const auto expected = arg.type == Type::String || arg.type == Type::ObjectArray ? 16u : arg.bytes.size();
            if (arg.type == Type::Bool)
            {
                if (p.size != 1 && p.size != 4)
                    return {false, "布尔参数布局不匹配"};
            }
            else if (p.size != expected)
                return {false, "接口参数大小不匹配"};
        }
        std::vector<ScriptArray *> arrays;
        bool ok = true;
        for (auto &arg : args)
        {
            const auto &p = properties.at(arg.name);
            auto *address = buffer + p.offset;
            if (arg.type == Type::String || arg.type == Type::ObjectArray)
            {
                arrays.push_back(reinterpret_cast<ScriptArray *>(address));
                if (arg.type == Type::String)
                    ok = SafeVoid(api.string, arg.text.c_str(), arrays.back()) && ok;
            }
            else if (arg.type == Type::Bool)
                ok = SafeVoid(api.setBool, p.ptr, address, arg.Get<std::int32_t>()) && ok;
            else if (!arg.output)
                std::memcpy(address, arg.bytes.data(), arg.bytes.size());
        }
        if (ok)
            ok = SafeVoid(api.event, reinterpret_cast<void *>(instance), reinterpret_cast<void *>(fn), buffer);
        if (ok)
            for (auto &arg : args)
                if (arg.output)
                {
                    const auto &p = properties.at(arg.name);
                    auto *address = buffer + p.offset;
                    if (arg.type == Type::ObjectArray)
                    {
                        const auto array = *reinterpret_cast<ScriptArray *>(address);
                        if (array.num < 0 || array.num > 32768 || array.capacity < array.num)
                        {
                            ok = false;
                            break;
                        }
                        arg.bytes.resize(static_cast<std::size_t>(array.num) * 8);
                        if (!arg.bytes.empty() && !ReadMemory(reinterpret_cast<std::uintptr_t>(array.data), arg.bytes.data(), arg.bytes.size()))
                        {
                            ok = false;
                            break;
                        }
                    }
                    else if (arg.type == Type::Bool)
                    {
                        std::int32_t v{};
                        ok = Safe(api.getBool, v, p.ptr, address) && ok;
                        arg.bytes.resize(sizeof(v));
                        std::memcpy(arg.bytes.data(), &v, sizeof(v));
                    }
                    else
                        std::memcpy(arg.bytes.data(), address, arg.bytes.size());
                }
        for (auto *array : arrays)
            SafeVoid(api.destroy, array);
        return {ok, ok ? "游戏接口已执行" : "游戏接口调用失败"};
    }
    bool Property(std::uintptr_t object, Arg &value, bool write)
    {
        if (!ValidObject(object) || value.type == Type::String || value.type == Type::ObjectArray)
            return false;
        void *cls{};
        if (!Safe(api.objectClass, cls, reinterpret_cast<void *>(object)))
            return false;
        int size{};
        if (!cls || !Safe(api.structureSize, size, cls) || size < 8 || size > 1024 * 1024)
            return false;
        for (int depth = 0; cls && depth < 64; ++depth)
        {
            void *field{};
            if (!Safe(api.children, field, cls))
                return false;
            for (int n = 0; field && n < 4096; ++n)
            {
                ScriptArray name{};
                if (!SafeVoid(api.name, field, &name))
                    return false;
                const auto key = Utf8(Wide(name));
                SafeVoid(api.destroy, &name);
                if (key == value.name)
                {
                    int offset{}, element{}, dim{};
                    std::int32_t matches{};
                    if (!Safe(api.internalOffset, offset, field) || !Safe(api.element, element, field) || !Safe(api.dim, dim, field) ||
                        !Safe(api.cast, matches, field, CastFlag(value.type)) || matches != 1 || dim != 1 || offset < 0 || offset > size || element <= 0 || element > size - offset)
                        return false;
                    auto *address = reinterpret_cast<void *>(object + offset);
                    if (value.type == Type::Bool)
                    {
                        if (element != 1 && element != 4)
                            return false;
                        if (write)
                            return SafeVoid(api.setBool, field, address, value.Get<std::int32_t>());
                        std::int32_t v{};
                        if (!Safe(api.getBool, v, field, address))
                            return false;
                        value = Arg::Data(value.name, Type::Bool, v);
                        return true;
                    }
                    if (element != value.bytes.size())
                        return false;
                    return write ? WriteMemory(object + offset, value.bytes.data(), value.bytes.size()) : ReadMemory(object + offset, value.bytes.data(), value.bytes.size());
                }
                void *next{};
                if (!Safe(api.next, next, field))
                    return false;
                field = next;
            }
            void *parent{};
            if (!Safe(api.super, parent, cls))
                return false;
            cls = parent;
        }
        return false;
    }
    bool IsA(std::uintptr_t object, const wchar_t *unrealClass) noexcept { return IsObject(object, unrealClass); }
    bool WeakReady() { return Ready() && api.weakSet && api.weakGet && api.weakValid; }
    WeakObject MakeWeak(std::uintptr_t object)
    {
        WeakObject weak;
        if (WeakReady() && ValidObject(object))
            SafeVoid(api.weakSet, &weak, reinterpret_cast<void *>(object));
        return weak;
    }
    std::uintptr_t ResolveWeak(WeakObject weak)
    {
        if (!WeakReady() || !IsGameThread() || weak.index < 0 || weak.serial <= 0)
            return 0;
        std::int32_t valid{};
        void *object{};
        if (!Safe(api.weakValid, valid, &weak, 0, 0) || valid != 1 || !Safe(api.weakGet, object, &weak))
            return 0;
        const auto address = reinterpret_cast<std::uintptr_t>(object);
        return ValidObject(address) ? address : 0;
    }
}
