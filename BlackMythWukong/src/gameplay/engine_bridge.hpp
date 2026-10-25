#pragma once
#include "extensions.hpp"
#include <Windows.h>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <unordered_map>

namespace gloss::gameplay::engine
{
    struct CallResult;
    struct ScriptArray
    {
        void *data{};
        std::int32_t num{}, capacity{};
    };
    static_assert(sizeof(ScriptArray) == 16);
    struct NameValue
    {
        std::int32_t comparisonIndex{}, number{};
    };
    static_assert(sizeof(NameValue) == 8);
    struct Rotator
    {
        double pitch{}, yaw{}, roll{};
    };
    struct alignas(16) Transform
    {
        double rotation[4]{0., 0., 0., 1.};
        extensions::Vec3 translation{};
        double translationPadding{};
        extensions::Vec3 scale{1., 1., 1.};
        double scalePadding{};
    };
    static_assert(sizeof(Rotator) == 24 && sizeof(Transform) == 96);
    struct ScriptInterface
    {
        std::uintptr_t object{}, interfaceAddress{};
    };
    static_assert(sizeof(ScriptInterface) == 16);
    // Only complete USharp registration instructions count as evidence. Duplicate
    // names, pointers outside the image and non-executable targets are rejected.
    std::unordered_map<std::string, std::uintptr_t> DiscoverBindings(std::span<const std::byte> image);
    bool Initialize(HMODULE module);
    bool Ready();
    bool IsGameThread() noexcept;
    bool RegisterFrame(void (*callback)()) noexcept;
    bool IsPawn(std::uintptr_t object) noexcept;
    bool IsActor(std::uintptr_t object) noexcept;
    bool IsA(std::uintptr_t object, const wchar_t *unrealClass) noexcept;
    struct WeakObject
    {
        std::int32_t index{-1}, serial{};
    };
    static_assert(sizeof(WeakObject) == 8);
    bool WeakReady();
    WeakObject MakeWeak(std::uintptr_t object);
    std::uintptr_t ResolveWeak(WeakObject object);
    // Serial/lifetime check only, including PendingKill/unreachable objects.
    // Never grants permission to dereference or operate on an object.
    bool WeakLifetimeCurrent(WeakObject object);
    std::uintptr_t Find(std::wstring_view path);
    std::uintptr_t Load(std::wstring_view path, std::wstring_view unrealClass);
    std::uintptr_t Construct(std::uintptr_t unrealClass, std::uintptr_t outer);
    std::uintptr_t DefaultObject(std::uintptr_t unrealClass);
    // Uses the reflected property's native text exporter; no soft-pointer
    // internal offsets are assumed. Empty path represents an unset reference.
    bool SoftClassPath(std::uintptr_t object, const char *property, std::wstring &path);
    std::uintptr_t World(std::uintptr_t object);
    bool ConsoleExists(std::wstring_view name);
    CallResult ExecuteConsole(std::wstring_view command, std::uintptr_t world);
    enum class Type
    {
        Object,
        Int,
        Float,
        Double,
        Bool,
        Vector,
        String,
        ObjectArray,
        Enum,
        Name,
        Rotator,
        Transform,
        IntArray,
        StructArray,
        Interface,
        Byte
    };
    struct Arg
    {
        const char *name;
        Type type;
        std::vector<std::byte> bytes;
        std::wstring text;
        bool output{};
        // Struct arrays are decoded while the engine owns their storage. Only
        // the requested reflected fields are copied into records.
        std::wstring structPath;
        std::vector<Arg> fields;
        std::vector<std::vector<Arg>> records;
        template <class T>
        static Arg Data(const char *name, Type type, T value, bool output = false)
        {
            Arg a{name, type, std::vector<std::byte>(sizeof(T)), {}, output};
            std::memcpy(a.bytes.data(), &value, sizeof(T));
            return a;
        }
        template <class T>
        T Get() const
        {
            T value{};
            if (bytes.size() == sizeof(T))
                std::memcpy(&value, bytes.data(), sizeof(T));
            return value;
        }
        static Arg Object(const char *n, std::uintptr_t p) { return Data(n, Type::Object, p); }
        static Arg Int(const char *n, int v) { return Data(n, Type::Int, v); }
        static Arg Enum(const char *n, std::uint8_t v) { return Data(n, Type::Enum, v); }
        static Arg Byte(const char *n, std::uint8_t v, bool out = false) { return Data(n, Type::Byte, v, out); }
        static Arg Float(const char *n, float v) { return Data(n, Type::Float, v); }
        static Arg Boolean(const char *n, bool v) { return Data(n, Type::Bool, std::int32_t(v)); }
        static Arg Vector(const char *n, extensions::Vec3 v, bool out = false) { return Data(n, Type::Vector, v, out); }
        static Arg String(const char *n, std::wstring v = {}, bool out = false) { return {n, Type::String, {}, std::move(v), out}; }
        static Arg Objects(const char *n) { return {n, Type::ObjectArray, {}, {}, true}; }
        static Arg Integers(const char *n) { return {n, Type::IntArray, {}, {}, true}; }
        static Arg Name(const char *n, std::wstring v = {}, bool out = false) { return {n, Type::Name, std::vector<std::byte>(sizeof(NameValue)), std::move(v), out}; }
        static Arg Rotation(const char *n, Rotator v = {}, bool out = false) { return Data(n, Type::Rotator, v, out); }
        static Arg TransformValue(const char *n, Transform v = {}, bool out = false) { return Data(n, Type::Transform, v, out); }
        static Arg Interface(const char *n, bool out = true) { return Data(n, Type::Interface, ScriptInterface{}, out); }
        static Arg Structs(const char *n, std::wstring path, std::vector<Arg> fields)
        {
            Arg a{n, Type::StructArray, {}, {}, true};
            a.structPath = std::move(path);
            a.fields = std::move(fields);
            return a;
        }
    };
    // ProcessEvent execution state, independent of output/cleanup success.
    enum class CallState { NotCalled, Completed, Indeterminate };
    struct CallResult
    {
        bool success{};
        std::string message;
        CallState state{CallState::NotCalled};
    };
    // All reflection and engine allocation happens on the game thread. Arguments
    // are matched by reflected name, offset, size, direction and cast flags.
    CallResult Call(std::wstring_view function, std::span<Arg> arguments, std::uintptr_t instance = 0);
    bool Property(std::uintptr_t object, Arg &value, bool write = false);
    // Path names identify enclosing FStructProperty values; value.name is the
    // leaf property. Every hop uses its reflected struct and container bounds.
    bool PropertyPath(std::uintptr_t object, std::span<const std::string_view> path, Arg &value, bool write = false);
    // Both objects must expose the same reflected struct property. The engine
    // performs the copy, including owned strings, arrays and maps. Destination
    // must already be initialized; callers discard their object on failure.
    bool CopyStructProperty(std::uintptr_t source, std::uintptr_t destination, const char *name);
    std::string ObjectName(std::uintptr_t object);
}
