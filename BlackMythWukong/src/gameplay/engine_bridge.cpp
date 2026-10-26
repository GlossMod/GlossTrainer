#include "engine_bridge.hpp"
#include "midhook.hpp"
#include "../runtime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <limits>
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
            void *(*structType)(void *);
            void (*initializeValue)(void *, void *);
            void (*destroyValue)(void *, void *);
            void (*copyValue)(void *, void *, void *);
            void (*nameFromString)(NameValue *, ScriptArray *, std::int32_t);
            void (*nameToString)(NameValue *, ScriptArray *);
            int (*structureStorageSize)(void *);
            void *(*cppStructOps)(void *);
            int (*cppStructSize)(void *);
            void (*exportTextItem)(void *, ScriptArray *, void *, void *, void *, std::int32_t, void *);
        } api{};
        std::atomic_bool ready{};
        std::uint64_t frameHandle{};
        std::string_view TypeName(Type type)
        {
            switch (type)
            {
            case Type::Object: return "Object";
            case Type::Int: return "Int";
            case Type::Float: return "Float";
            case Type::Double: return "Double";
            case Type::Bool: return "Bool";
            case Type::Vector: return "Vector";
            case Type::String: return "String";
            case Type::ObjectArray: return "ObjectArray";
            case Type::Enum: return "Enum";
            case Type::Name: return "Name";
            case Type::Rotator: return "Rotator";
            case Type::Transform: return "Transform";
            case Type::IntArray: return "IntArray";
            case Type::StructArray: return "StructArray";
            case Type::Interface: return "Interface";
            case Type::Byte: return "Byte";
            }
            return "Unknown";
        }
        std::uint64_t CastFlag(Type type)
        {
            switch (type)
            {
            case Type::Object:
                return 0x10000ull; // Hard UObject pointer, never a weak/soft reference.
            case Type::Int:
                return 0x80ull;
            case Type::Float:
                return 0x100ull;
            case Type::Double:
                return 0x100000000ull;
            case Type::Bool:
                return 0x20000ull;
            case Type::Vector:
            case Type::Rotator:
            case Type::Transform:
                return 0x100000ull;
            case Type::String:
                return 0x4000ull;
            case Type::ObjectArray:
            case Type::IntArray:
            case Type::StructArray:
                return 0x200000ull;
            case Type::Enum:
                return 0x1000000000000ull;
            case Type::Name:
                return 0x2000ull;
            case Type::Interface:
                return 0x1000ull;
            case Type::Byte:
                return 0x40ull;
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
        bool ReadWide(ScriptArray value, std::wstring &result, int limit = 131072)
        {
            result.clear();
            if (value.num == 0)
                return value.capacity >= 0;
            if (!value.data || value.num < 1 || value.num > limit || value.capacity < value.num)
                return false;
            result.resize(value.num);
            if (!ReadMemory(reinterpret_cast<std::uintptr_t>(value.data), result.data(), result.size() * 2) || result.back())
            {
                result.clear();
                return false;
            }
            result.pop_back();
            return true;
        }
        std::wstring Wide(ScriptArray value)
        {
            std::wstring result;
            ReadWide(value, result, 4096);
            return result;
        }
        struct EngineString
        {
            ScriptArray value{};
            ~EngineString() { SafeVoid(api.destroy, &value); }
        };
        std::string Utf8(std::wstring_view value)
        {
            const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string result(count, '\0');
            if (count)
                WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
            return result;
        }
        struct ReflectedProperty
        {
            void *ptr{};
            int offset{}, size{};
            std::uint64_t flags{};
        };
        bool Describe(void *field, int containerSize, bool parameter, ReflectedProperty &property)
        {
            int dim{};
            property.ptr = field;
            return field && Safe(api.flags, property.flags, field) &&
                   Safe(parameter ? api.offset : api.internalOffset, property.offset, field) &&
                   Safe(api.element, property.size, field) && Safe(api.dim, dim, field) && dim == 1 &&
                   property.offset >= 0 && property.offset <= containerSize && property.size > 0 && property.size <= containerSize - property.offset;
        }
        bool FieldName(void *field, std::string &name)
        {
            EngineString text;
            std::wstring wide;
            if (!SafeVoid(api.name, field, &text.value) || !ReadWide(text.value, wide, 4096))
                return false;
            name = Utf8(wide);
            return !name.empty();
        }
        std::string FieldKey(std::string_view name)
        {
            // Native FName identity ignores ASCII case; its exported display
            // spelling can depend on an earlier NamePool insertion (for example
            // the live Position parameter is displayed as "position"). Fold
            // lookup keys only, without changing display text or FName payloads.
            // Non-ASCII bytes retain their existing exact-match behavior.
            std::string key(name);
            for (auto &character : key)
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character + ('a' - 'A'));
            return key;
        }
        bool HasCast(void *property, std::uint64_t flag)
        {
            std::int32_t result{};
            return flag && Safe(api.cast, result, property, flag) && result == 1;
        }
        bool FindField(void *structure, int containerSize, std::string_view name, ReflectedProperty &result)
        {
            if (name.empty() || name.size() > 256)
                return false;
            const auto requested = FieldKey(name);
            for (int depth = 0; structure && depth < 64; ++depth)
            {
                void *field{};
                bool found = false;
                if (!Safe(api.children, field, structure))
                    return false;
                for (int count = 0; field && count < 4096; ++count)
                {
                    std::string key;
                    if (!FieldName(field, key))
                        return false;
                    if (FieldKey(key) == requested)
                    {
                        // Two spellings of the same FName within one declaring
                        // structure are ambiguous metadata, never two fields.
                        if (found || !Describe(field, containerSize, false, result))
                            return false;
                        found = true;
                    }
                    void *next{};
                    if (!Safe(api.next, next, field))
                        return false;
                    field = next;
                }
                if (field)
                    return false;
                if (found)
                    return true;
                void *parent{};
                if (!Safe(api.super, parent, structure) || parent == structure)
                    return false;
                structure = parent;
            }
            return false;
        }
        bool StructStorageSize(void *structure, int &size)
        {
            // Native structs can contain storage that is not a UPROPERTY.
            // Mirror NativeReflection.GetStructSize instead of using the last
            // reflected property's end as the array stride.
            void *ops{};
            return api.structureStorageSize && api.cppStructOps && api.cppStructSize && Safe(api.cppStructOps, ops, structure) &&
                   (ops ? Safe(api.cppStructSize, size, ops) : Safe(api.structureStorageSize, size, structure));
        }
        bool StructIdentity(void *property, std::wstring_view path, int expectedSize, void *&structure)
        {
            int size{};
            const auto expected = Find(path);
            return api.structType && expected && HasCast(property, 0x100000ull) && Safe(api.structType, structure, property) &&
                   structure == reinterpret_cast<void *>(expected) && StructStorageSize(structure, size) && size == expectedSize;
        }
        bool DoubleFields(void *structure, int size, std::span<const char *const> names)
        {
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                ReflectedProperty property;
                if (!FindField(structure, size, names[i], property) || property.offset != static_cast<int>(i * sizeof(double)) ||
                    property.size != sizeof(double) || !HasCast(property.ptr, CastFlag(Type::Double)))
                    return false;
            }
            return true;
        }
        bool KnownStruct(void *property, Type type)
        {
            void *structure{};
            static constexpr const char *vectorNames[]{"X", "Y", "Z"};
            static constexpr const char *rotatorNames[]{"Pitch", "Yaw", "Roll"};
            static constexpr const char *quatNames[]{"X", "Y", "Z", "W"};
            if (type == Type::Vector)
                return StructIdentity(property, L"/Script/CoreUObject.Vector", sizeof(extensions::Vec3), structure) &&
                       DoubleFields(structure, sizeof(extensions::Vec3), vectorNames);
            if (type == Type::Rotator)
                return StructIdentity(property, L"/Script/CoreUObject.Rotator", sizeof(Rotator), structure) &&
                       DoubleFields(structure, sizeof(Rotator), rotatorNames);
            if (type != Type::Transform || !StructIdentity(property, L"/Script/CoreUObject.Transform", sizeof(Transform), structure))
                return false;
            ReflectedProperty rotation, translation, scale;
            void *quat{};
            return FindField(structure, sizeof(Transform), "Rotation", rotation) && rotation.offset == offsetof(Transform, rotation) && rotation.size == 32 &&
                   StructIdentity(rotation.ptr, L"/Script/CoreUObject.Quat", 32, quat) && DoubleFields(quat, 32, quatNames) &&
                   FindField(structure, sizeof(Transform), "Translation", translation) && translation.offset == offsetof(Transform, translation) &&
                   translation.size == sizeof(extensions::Vec3) && KnownStruct(translation.ptr, Type::Vector) &&
                   FindField(structure, sizeof(Transform), "Scale3D", scale) && scale.offset == offsetof(Transform, scale) &&
                   scale.size == sizeof(extensions::Vec3) && KnownStruct(scale.ptr, Type::Vector);
        }
        bool IsArray(Type type) { return type == Type::ObjectArray || type == Type::IntArray || type == Type::StructArray; }
        bool IsDynamic(Type type) { return type == Type::String || IsArray(type); }
        std::size_t FixedSize(Type type)
        {
            switch (type)
            {
            case Type::Object: return sizeof(std::uintptr_t);
            case Type::Int: case Type::Float: case Type::Bool: return 4;
            case Type::Double: return 8;
            case Type::Vector: return sizeof(extensions::Vec3);
            case Type::Rotator: return sizeof(Rotator);
            case Type::Transform: return sizeof(Transform);
            case Type::Name: return sizeof(NameValue);
            case Type::Interface: return sizeof(ScriptInterface);
            case Type::Enum: case Type::Byte: return 1;
            default: return sizeof(ScriptArray);
            }
        }
        bool ValidateValue(const ReflectedProperty &property, const Arg &arg, bool nested = false)
        {
            if (!HasCast(property.ptr, CastFlag(arg.type)))
                return false;
            if (IsArray(arg.type))
            {
                if (nested || !arg.output || property.size != sizeof(ScriptArray))
                    return false;
                void *inner{};
                int size{}, dim{};
                if (!Safe(api.inner, inner, property.ptr) || !inner || !Safe(api.element, size, inner) || !Safe(api.dim, dim, inner) || dim != 1)
                    return false;
                if (arg.type == Type::ObjectArray || arg.type == Type::IntArray)
                    return size == (arg.type == Type::ObjectArray ? 8 : 4) && HasCast(inner, CastFlag(arg.type == Type::ObjectArray ? Type::Object : Type::Int));
                if (!api.initializeValue || !api.destroyValue || !api.structType || arg.structPath.empty() || arg.fields.empty() || arg.fields.size() > 32 || size < 1 || size > 65536)
                    return false;
                void *structure{};
                if (!StructIdentity(inner, arg.structPath, size, structure))
                    return false;
                std::set<std::string> names;
                for (const auto &field : arg.fields)
                {
                    ReflectedProperty reflected;
                    if (!field.name || !names.insert(FieldKey(field.name)).second || !FindField(structure, size, field.name, reflected) || !ValidateValue(reflected, field, true))
                        return false;
                }
                return true;
            }
            if (arg.type == Type::Bool)
                return (property.size == 1 || property.size == 4) && arg.bytes.size() == 4;
            if (property.size != FixedSize(arg.type))
                return false;
            if (arg.type == Type::String)
                return arg.text.size() < 131071 && arg.text.find(L'\0') == std::wstring::npos;
            if (arg.bytes.size() != FixedSize(arg.type))
                return false;
            if (arg.type == Type::Name)
                return api.nameFromString && api.nameToString && arg.text.size() < 1024 && arg.text.find(L'\0') == std::wstring::npos;
            if (arg.type == Type::Vector || arg.type == Type::Rotator || arg.type == Type::Transform)
            {
                if (property.offset % (arg.type == Type::Transform ? 16 : 8) || !KnownStruct(property.ptr, arg.type))
                    return false;
                for (std::size_t offset = 0; offset < arg.bytes.size(); offset += sizeof(double))
                {
                    double value{};
                    std::memcpy(&value, arg.bytes.data() + offset, sizeof(value));
                    if (!std::isfinite(value))
                        return false;
                }
            }
            return true;
        }
        bool DecodeValue(const ReflectedProperty &property, const void *address, Arg &arg)
        {
            if (arg.type == Type::String)
            {
                ScriptArray value{};
                return ReadMemory(reinterpret_cast<std::uintptr_t>(address), &value, sizeof(value)) && ReadWide(value, arg.text);
            }
            if (arg.type == Type::Name)
            {
                NameValue value{};
                EngineString text;
                return ReadMemory(reinterpret_cast<std::uintptr_t>(address), &value, sizeof(value)) && value.comparisonIndex >= 0 && value.number >= 0 &&
                       SafeVoid(api.nameToString, &value, &text.value) && ReadWide(text.value, arg.text, 1024) &&
                       ReadMemory(reinterpret_cast<std::uintptr_t>(address), arg.bytes.data(), arg.bytes.size());
            }
            if (IsArray(arg.type))
            {
                ScriptArray array{};
                void *inner{};
                int element{};
                if (!ReadMemory(reinterpret_cast<std::uintptr_t>(address), &array, sizeof(array)) || array.num < 0 || array.num > 131072 ||
                    array.capacity < array.num || (array.num && !array.data) || !Safe(api.inner, inner, property.ptr) || !inner || !Safe(api.element, element, inner) || element < 1 || element > 65536 ||
                    static_cast<std::size_t>(array.num) * element > 64u * 1024u * 1024u)
                    return false;
                const auto storage = static_cast<std::size_t>(array.num) * element;
                if (storage > (std::numeric_limits<std::uintptr_t>::max)() - reinterpret_cast<std::uintptr_t>(array.data))
                    return false;
                if (arg.type != Type::StructArray)
                {
                    arg.bytes.resize(static_cast<std::size_t>(array.num) * element);
                    return arg.bytes.empty() || ReadMemory(reinterpret_cast<std::uintptr_t>(array.data), arg.bytes.data(), arg.bytes.size());
                }
                void *structure{};
                if (!StructIdentity(inner, arg.structPath, element, structure))
                    return false;
                std::vector<ReflectedProperty> fields(arg.fields.size());
                for (std::size_t i = 0; i < fields.size(); ++i)
                    if (!FindField(structure, element, arg.fields[i].name, fields[i]))
                        return false;
                arg.records.clear();
                arg.records.reserve(array.num);
                for (int index = 0; index < array.num; ++index)
                {
                    auto record = arg.fields;
                    const auto base = reinterpret_cast<std::uintptr_t>(array.data) + static_cast<std::size_t>(index) * element;
                    for (std::size_t i = 0; i < fields.size(); ++i)
                        if (!DecodeValue(fields[i], reinterpret_cast<void *>(base + fields[i].offset), record[i]))
                            return false;
                    arg.records.push_back(std::move(record));
                }
                return true;
            }
            if (arg.type == Type::Bool)
            {
                std::int32_t value{};
                if (!Safe(api.getBool, value, property.ptr, const_cast<void *>(address)))
                    return false;
                arg.bytes.resize(sizeof(value));
                std::memcpy(arg.bytes.data(), &value, sizeof(value));
                return true;
            }
            return ReadMemory(reinterpret_cast<std::uintptr_t>(address), arg.bytes.data(), arg.bytes.size());
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
        optional(api.structType, "Export_FStructProperty_Get_Struct");
        optional(api.initializeValue, "Export_FProperty_InitializeValue");
        optional(api.destroyValue, "Export_FProperty_DestroyValue");
        optional(api.copyValue, "Export_FProperty_CopySingleValue");
        optional(api.nameFromString, "Export_FName_FromString");
        optional(api.nameToString, "Export_FName_ToString");
        optional(api.structureStorageSize, "Export_UStruct_GetStructureSize");
        optional(api.cppStructOps, "Export_UScriptStruct_GetCppStructOps");
        optional(api.cppStructSize, "Export_ICppStructOps_GetSize");
        // ExportText_Direct is a differential serializer: it returns false and
        // emits nothing for values identical to a null default. Read-only
        // inspection requires ExportTextItem's unconditional void ABI instead.
        optional(api.exportTextItem, "Export_FProperty_ExportTextItem");
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
        if (!IsGameThread() || path.empty() || path.size() > 512 || path.find(L'\0') != std::wstring_view::npos)
            return 0;
        EngineString name;
        const std::wstring text(path);
        if (!SafeVoid(api.string, text.c_str(), &name.value))
            return 0;
        void *result{};
        Safe(api.find, result, nullptr, nullptr, &name.value, 0);
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
    std::uintptr_t DefaultObject(std::uintptr_t unrealClass)
    {
        void *object{};
        if (!IsA(unrealClass, L"/Script/CoreUObject.Class") || !Safe(api.cdo, object, reinterpret_cast<void *>(unrealClass), 1))
            return 0;
        const auto address = reinterpret_cast<std::uintptr_t>(object);
        return ValidObject(address) ? address : 0;
    }
    bool SoftClassPath(std::uintptr_t object, const char *name, std::wstring &path)
    {
        path.clear();
        if (!ValidObject(object) || !name || !api.exportTextItem)
            return false;
        void *cls{};
        int size{};
        ReflectedProperty property;
        if (!Safe(api.objectClass, cls, reinterpret_cast<void *>(object)) || !cls || !Safe(api.structureSize, size, cls) || size < 8 || size > 1024 * 1024 ||
            !FindField(cls, size, name, property) || !HasCast(property.ptr, 0x200000000ull))
            return false;
        EngineString exported;
        std::wstring text;
        if (!SafeVoid(api.exportTextItem, property.ptr, &exported.value, reinterpret_cast<void *>(object + property.offset), nullptr,
                      reinterpret_cast<void *>(object), 0, nullptr) || !ReadWide(exported.value, text, 2048) || text.find(L'\0') != std::wstring::npos)
            return false;
        if (text.empty() || text == L"None")
            return true;
        // ExportText can emit either the object path or Class'object path'.
        // Reject malformed or escaped exports rather than guessing a load path.
        if (const auto quote = text.find(L'\''); quote != std::wstring::npos)
        {
            if (quote == 0 || text.back() != L'\'' || text.find(L'\'', quote + 1) != text.size() - 1)
                return false;
            text = text.substr(quote + 1, text.size() - quote - 2);
        }
        if (text.empty() || text.size() > 512 || text.front() != L'/' || text.find(L'.') == std::wstring::npos ||
            text.find_first_of(L"\"'\\\r\n\t ") != std::wstring::npos || text.find(L'\0') != std::wstring::npos)
            return false;
        path = std::move(text);
        return true;
    }
    bool ObjectReferenceAssigned(std::uintptr_t object, const char *name, bool &assigned)
    {
        assigned = false;
        if (!ValidObject(object) || !name)
            return false;
        void *cls{};
        int size{};
        ReflectedProperty property;
        if (!Safe(api.objectClass, cls, reinterpret_cast<void *>(object)) || !cls || !Safe(api.structureSize, size, cls) || size < 8 || size > 1024 * 1024 ||
            !FindField(cls, size, name, property))
            return false;
        const bool hard = HasCast(property.ptr, CastFlag(Type::Object));
        const bool weak = HasCast(property.ptr, 0x8000000ull); // FWeakObjectProperty
        const bool soft = HasCast(property.ptr, 0x20000000ull); // FSoftObjectProperty (including soft classes)
        if (static_cast<int>(hard) + static_cast<int>(weak) + static_cast<int>(soft) != 1)
            return false;
        if (hard)
        {
            std::uintptr_t value{};
            if (property.size != sizeof(value) || !ReadMemory(object + property.offset, &value, sizeof(value)))
                return false;
            assigned = value != 0;
            return true;
        }
        if (weak)
        {
            WeakObject value;
            if (property.size != sizeof(value) || !ReadMemory(object + property.offset, &value, sizeof(value)))
                return false;
            // The managed FWeakObjectPtr carrier contains two signed int32s.
            // This build's native Reset/SetUObject(nullptr) write {-1, 0};
            // {0, 0} is the managed carrier's default initialization. A token
            // with a serial retains a dependency even after its target dies;
            // resolving it would erase that distinction.
            if (value.serial == 0 && (value.index == -1 || value.index == 0))
                return true;
            if (value.index < 0 || value.serial <= 0)
                return false;
            assigned = true;
            return true;
        }
        if (!api.exportTextItem)
            return false;
        EngineString exported;
        std::wstring text;
        if (!SafeVoid(api.exportTextItem, property.ptr, &exported.value, reinterpret_cast<void *>(object + property.offset), nullptr,
                      reinterpret_cast<void *>(object), 0, nullptr) || !ReadWide(exported.value, text, 2048) || text.find(L'\0') != std::wstring::npos)
            return false;
        if (text.empty() || text == L"None")
            return true;
        // Native export preserves unloaded asset and level/subobject paths.
        // Recognize only path syntax; unknown export text is not evidence of
        // an empty reference. No soft-pointer storage layout is assumed.
        if (const auto quote = text.find(L'\''); quote != std::wstring::npos)
        {
            if (quote == 0 || text.back() != L'\'' || text.find(L'\'', quote + 1) != text.size() - 1)
                return false;
            text = text.substr(quote + 1, text.size() - quote - 2);
        }
        if (text.empty() || text.size() > 2047 || text.front() != L'/' || text.find(L'.') == std::wstring::npos ||
            text.find_first_of(L"\"'\\\r\n\t ") != std::wstring::npos)
            return false;
        assigned = true;
        return true;
    }
    bool ExportPropertyText(std::uintptr_t object, const char *name, std::wstring &text)
    {
        return ExportPropertyPathText(object, {}, name, text);
    }
    bool ExportPropertyPathText(std::uintptr_t object, std::span<const std::string_view> path, const char *name, std::wstring &text)
    {
        text.clear();
        if (!ValidObject(object) || !name || !api.exportTextItem || path.size() > 16)
            return false;
        void *cls{};
        int size{};
        if (!Safe(api.objectClass, cls, reinterpret_cast<void *>(object)) || !cls || !Safe(api.structureSize, size, cls) || size < 8 || size > 1024 * 1024)
            return false;
        auto address = object;
        for (const auto part : path)
        {
            ReflectedProperty property;
            void *nested{};
            int nestedSize{};
            if (!api.structType || !FindField(cls, size, part, property) || !HasCast(property.ptr, 0x100000ull) ||
                !Safe(api.structType, nested, property.ptr) || !nested || !StructStorageSize(nested, nestedSize) ||
                nestedSize != property.size || nestedSize < 1 || nestedSize > 65536)
                return false;
            address += property.offset;
            cls = nested;
            size = nestedSize;
        }
        ReflectedProperty property;
        if (!FindField(cls, size, name, property))
            return false;
        EngineString exported;
        std::wstring value;
        if (!SafeVoid(api.exportTextItem, property.ptr, &exported.value, reinterpret_cast<void *>(address + property.offset), nullptr,
                      reinterpret_cast<void *>(object), 0, nullptr) || !ReadWide(exported.value, value) || value.find(L'\0') != std::wstring::npos)
            return false;
        text = std::move(value);
        return true;
    }
    std::uintptr_t Load(std::wstring_view path, std::wstring_view unrealClass)
    {
        if (!IsGameThread())
            return 0;
        if (const auto existing = Find(path))
            return existing;
        const auto cls = Find(unrealClass);
        if (!cls || path.empty() || path.size() > 512 || path.find(L'\0') != std::wstring_view::npos)
            return 0;
        EngineString name, filename;
        const std::wstring text(path);
        void *result{};
        if (!SafeVoid(api.string, text.c_str(), &name.value))
            return 0;
        Safe(api.load, result, reinterpret_cast<void *>(cls), nullptr, &name.value, &filename.value, 0u, nullptr, 1);
        return reinterpret_cast<std::uintptr_t>(result);
    }
    std::uintptr_t World(std::uintptr_t object)
    {
        void *world{};
        return ValidObject(object) && Safe(api.world, world, reinterpret_cast<void *>(object)) ? reinterpret_cast<std::uintptr_t>(world) : 0;
    }
    bool ConsoleExists(std::wstring_view name)
    {
        if (!IsGameThread() || name.empty() || name.size() > 128 || name.find(L'\0') != std::wstring_view::npos)
            return false;
        EngineString text;
        void *manager{};
        void *object{};
        const std::wstring terminated(name);
        if (!Safe(api.console, manager) || !manager || !SafeVoid(api.string, terminated.c_str(), &text.value))
            return false;
        Safe(api.findConsole, object, manager, &text.value);
        return object != nullptr;
    }
    CallResult ExecuteConsole(std::wstring_view command, std::uintptr_t world)
    {
        if (!IsGameThread() || !ValidObject(world) || command.empty() || command.size() > 1024 || command.find(L'\0') != std::wstring_view::npos)
            return {false, "控制台上下文无效"};
        const auto name = command.substr(0, command.find(L' '));
        if (!ConsoleExists(name))
            return {false, "游戏尚未注册此指令"};
        void *manager{};
        void *log{};
        EngineString text;
        std::int32_t accepted{};
        const std::wstring terminated(command);
        if (!Safe(api.console, manager) || !manager || !Safe(api.log, log) || !log || !SafeVoid(api.string, terminated.c_str(), &text.value))
            return {false, "游戏控制台未就绪"};
        const bool ok = Safe(api.executeConsole, accepted, manager, &text.value, log, reinterpret_cast<void *>(world));
        return {ok && accepted == 1, ok && accepted == 1 ? "游戏指令已执行" : "游戏拒绝了此指令"};
    }
    std::string ObjectName(std::uintptr_t object)
    {
        if (!ValidObject(object))
            return {};
        EngineString name;
        return SafeVoid(api.objectName, reinterpret_cast<void *>(object), &name.value) ? Utf8(Wide(name.value)) : std::string{};
    }
    CallResult Call(std::wstring_view path, std::span<Arg> args, std::uintptr_t instance)
    {
        struct OutputGuard
        {
            std::span<Arg> args;
            bool committed{};
            ~OutputGuard()
            {
                if (!committed)
                    for (auto &arg : args)
                        if (arg.output)
                        {
                            arg.text.clear();
                            arg.records.clear();
                            if (IsArray(arg.type) || arg.type == Type::String)
                                arg.bytes.clear();
                            else
                                std::fill(arg.bytes.begin(), arg.bytes.end(), std::byte{});
                        }
            }
        } outputs{args};
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
        struct alignas(16) Block { std::byte bytes[16]{}; };
        std::vector<Block> storage((size + 15) / 16);
        auto *buffer = reinterpret_cast<std::byte *>(storage.data());
        std::unordered_map<std::string, ReflectedProperty> properties;
        void *field{};
        if (!Safe(api.children, field, reinterpret_cast<void *>(fn)))
            return {false, "反射参数读取失败"};
        for (int i = 0; field && i < 128; ++i)
        {
            ReflectedProperty property;
            std::string key;
            if (!FieldName(field, key) || !Describe(field, size, true, property) || !(property.flags & 0x80))
                return {false, "反射参数结构不匹配"};
            if (!properties.emplace(FieldKey(key), property).second)
            {
                Log(std::format("Engine bridge parameter name mismatch: function={} requested=<metadata> reflected={} reason=duplicate reflected FName", Utf8(path), key));
                return {false, "反射参数结构不匹配"};
            }
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
            if (!arg.name)
            {
                Log(std::format("Engine bridge parameter name mismatch: function={} requested=<null> reason=missing name", Utf8(path)));
                return {false, "接口参数名不匹配"};
            }
            const auto key = FieldKey(arg.name);
            const auto it = properties.find(key);
            if (it == properties.end() || !supplied.insert(key).second)
            {
                Log(std::format("Engine bridge parameter name mismatch: function={} requested={} reason={}", Utf8(path), arg.name,
                    it == properties.end() ? "unknown name" : "duplicate supplied FName"));
                return {false, "接口参数名不匹配"};
            }
            const auto &p = it->second;
            // Const references can carry OutParm in native metadata, but must
            // never be treated as writable output arguments.
            const bool output = (p.flags & 0x400) || ((p.flags & 0x100) && !(p.flags & 0x2));
            if (arg.output != output)
                return {false, "接口参数方向不匹配"};
            if (!ValidateValue(p, arg))
            {
                Log(std::format("Engine bridge signature mismatch: function={} parameter={} expected={} expected_size={} argument_size={} actual_size={} offset={} flags=0x{:X} cast_match={} enum_cast={} byte_cast={} struct_cast={}",
                    Utf8(path), arg.name, TypeName(arg.type), FixedSize(arg.type), arg.bytes.size(), p.size, p.offset, p.flags,
                    HasCast(p.ptr, CastFlag(arg.type)) ? 1 : 0, HasCast(p.ptr, CastFlag(Type::Enum)) ? 1 : 0,
                    HasCast(p.ptr, CastFlag(Type::Byte)) ? 1 : 0, HasCast(p.ptr, CastFlag(Type::Transform)) ? 1 : 0));
                return {false, "接口参数类型或布局不匹配"};
            }
        }
        struct DynamicValues
        {
            std::vector<std::pair<void *, void *>> values;
            bool Clear()
            {
                bool result = true;
                for (auto it = values.rbegin(); it != values.rend(); ++it)
                    result = (api.destroyValue ? SafeVoid(api.destroyValue, it->first, it->second) : SafeVoid(api.destroy, static_cast<ScriptArray *>(it->second))) && result;
                values.clear();
                return result;
            }
            ~DynamicValues() { Clear(); }
        } dynamic;
        dynamic.values.reserve(args.size());
        bool ok = true;
        CallState state = CallState::NotCalled;
        for (auto &arg : args)
        {
            const auto &p = properties.at(FieldKey(arg.name));
            auto *address = buffer + p.offset;
            if (IsDynamic(arg.type))
            {
                dynamic.values.emplace_back(p.ptr, address);
                if (api.initializeValue)
                    ok = SafeVoid(api.initializeValue, p.ptr, address) && ok;
                if (ok && arg.type == Type::String && !arg.output)
                    ok = SafeVoid(api.string, arg.text.c_str(), reinterpret_cast<ScriptArray *>(address));
            }
            else if (arg.type == Type::Name && !arg.output)
            {
                EngineString text;
                ok = SafeVoid(api.string, arg.text.c_str(), &text.value) &&
                     SafeVoid(api.nameFromString, reinterpret_cast<NameValue *>(address), &text.value, 1) && ok;
            }
            else if (arg.type == Type::Bool)
                ok = SafeVoid(api.setBool, p.ptr, address, arg.Get<std::int32_t>()) && ok;
            else
                std::memcpy(address, arg.bytes.data(), arg.bytes.size());
            if (!ok)
                break;
        }
        if (ok)
        {
            state = CallState::Indeterminate;
            ok = SafeVoid(api.event, reinterpret_cast<void *>(instance), reinterpret_cast<void *>(fn), buffer);
            if (ok)
                state = CallState::Completed;
        }
        if (ok)
            for (auto &arg : args)
                if (arg.output)
                {
                    const auto &p = properties.at(FieldKey(arg.name));
                    if (!DecodeValue(p, buffer + p.offset, arg))
                    {
                        ok = false;
                        break;
                    }
                }
        ok = dynamic.Clear() && ok;
        outputs.committed = ok;
        return {ok, ok ? "游戏接口已执行" : "游戏接口调用失败", state};
    }
    bool Property(std::uintptr_t object, Arg &value, bool write)
    {
        return PropertyPath(object, {}, value, write);
    }
    bool CopyStructProperty(std::uintptr_t source, std::uintptr_t destination, const char *name)
    {
        if (!name || !api.copyValue || !api.structType || !ValidObject(source) || !ValidObject(destination))
            return false;
        void *sourceClass{}, *destinationClass{}, *structure{};
        int sourceSize{}, destinationSize{}, storageSize{};
        ReflectedProperty from, to;
        if (!Safe(api.objectClass, sourceClass, reinterpret_cast<void *>(source)) || !sourceClass ||
            !Safe(api.objectClass, destinationClass, reinterpret_cast<void *>(destination)) || !destinationClass ||
            !Safe(api.structureSize, sourceSize, sourceClass) || !Safe(api.structureSize, destinationSize, destinationClass) ||
            sourceSize < 8 || destinationSize < 8 || sourceSize > 1024 * 1024 || destinationSize > 1024 * 1024 ||
            !FindField(sourceClass, sourceSize, name, from) || !FindField(destinationClass, destinationSize, name, to) ||
            from.ptr != to.ptr || from.size != to.size || !HasCast(from.ptr, 0x100000ull) ||
            !Safe(api.structType, structure, from.ptr) || !structure || !StructStorageSize(structure, storageSize) || storageSize != from.size)
            return false;
        return source == destination || SafeVoid(api.copyValue, from.ptr, reinterpret_cast<void *>(destination + to.offset), reinterpret_cast<void *>(source + from.offset));
    }
    bool PropertyPath(std::uintptr_t object, std::span<const std::string_view> path, Arg &value, bool write)
    {
        if (!ValidObject(object) || !value.name || path.size() > 16 || (write && IsArray(value.type)))
            return false;
        void *cls{};
        if (!Safe(api.objectClass, cls, reinterpret_cast<void *>(object)))
            return false;
        int size{};
        if (!cls || !Safe(api.structureSize, size, cls) || size < 8 || size > 1024 * 1024)
            return false;
        auto address = object;
        for (const auto name : path)
        {
            ReflectedProperty property;
            void *nested{};
            int nestedSize{};
            if (!api.structType || !FindField(cls, size, name, property) || !HasCast(property.ptr, 0x100000ull) ||
                !Safe(api.structType, nested, property.ptr) || !nested || !StructStorageSize(nested, nestedSize) ||
                nestedSize != property.size || nestedSize < 1 || nestedSize > 65536)
                return false;
            address += property.offset;
            cls = nested;
            size = nestedSize;
        }
        ReflectedProperty property;
        if (!FindField(cls, size, value.name, property) || !ValidateValue(property, value))
            return false;
        address += property.offset;
        if (!write)
            return DecodeValue(property, reinterpret_cast<void *>(address), value);
        if (value.type == Type::Object && value.Get<std::uintptr_t>() && !ValidObject(value.Get<std::uintptr_t>()))
            return false;
        if (value.type == Type::Bool)
            return SafeVoid(api.setBool, property.ptr, reinterpret_cast<void *>(address), value.Get<std::int32_t>());
        if (value.type == Type::Name)
        {
            EngineString text;
            NameValue name{};
            return SafeVoid(api.string, value.text.c_str(), &text.value) && SafeVoid(api.nameFromString, &name, &text.value, 1) &&
                   WriteMemory(address, &name, sizeof(name));
        }
        if (value.type == Type::String)
        {
            if (!api.copyValue)
                return false;
            EngineString text;
            return SafeVoid(api.string, value.text.c_str(), &text.value) && SafeVoid(api.copyValue, property.ptr, reinterpret_cast<void *>(address), &text.value);
        }
        return WriteMemory(address, value.bytes.data(), value.bytes.size());
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
    bool WeakLifetimeCurrent(WeakObject weak)
    {
        if (!WeakReady() || !IsGameThread() || weak.index < 0 || weak.serial <= 0)
            return false;
        std::int32_t valid{};
        // Native_FWeakObjectPtr.Del_IsValid(ref instance, csbool
        // evenIfPendingKill, csbool threadsafeTest). The thread-safe test
        // checks the slot/serial without turning a dying UObject into a usable
        // pointer. Scoped cleanup separately checks its captured identities.
        return Safe(api.weakValid, valid, &weak, 1, 1) && valid == 1;
    }
}
