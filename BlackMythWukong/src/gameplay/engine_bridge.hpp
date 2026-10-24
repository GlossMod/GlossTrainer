#pragma once
#include "extensions.hpp"
#include <Windows.h>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <unordered_map>

namespace gloss::gameplay::engine {
struct CallResult;
struct ScriptArray { void* data{}; std::int32_t num{}, capacity{}; };
static_assert(sizeof(ScriptArray) == 16);
// Only complete USharp registration instructions count as evidence. Duplicate
// names, pointers outside the image and non-executable targets are rejected.
std::unordered_map<std::string, std::uintptr_t> DiscoverBindings(std::span<const std::byte> image);
bool Initialize(HMODULE module);
bool Ready();
bool IsGameThread() noexcept;
bool RegisterFrame(void(*callback)()) noexcept;
bool IsPawn(std::uintptr_t object) noexcept;
bool IsActor(std::uintptr_t object) noexcept;
bool IsA(std::uintptr_t object,const wchar_t* unrealClass) noexcept;
struct WeakObject {std::int32_t index{-1},serial{};};
static_assert(sizeof(WeakObject)==8);
bool WeakReady();
WeakObject MakeWeak(std::uintptr_t object);
std::uintptr_t ResolveWeak(WeakObject object);
std::uintptr_t Find(std::wstring_view path);
std::uintptr_t Load(std::wstring_view path,std::wstring_view unrealClass);
std::uintptr_t Construct(std::uintptr_t unrealClass,std::uintptr_t outer);
std::uintptr_t World(std::uintptr_t object);
bool ConsoleExists(std::wstring_view name);
CallResult ExecuteConsole(std::wstring_view command,std::uintptr_t world);
enum class Type { Object, Int, Float, Double, Bool, Vector, String, ObjectArray, Enum };
struct Arg {
    const char* name;
    Type type;
    std::vector<std::byte> bytes;
    std::wstring text;
    bool output{};
    template<class T> static Arg Data(const char* name, Type type, T value, bool output = false) {
        Arg a{name, type, std::vector<std::byte>(sizeof(T)), {}, output};
        std::memcpy(a.bytes.data(), &value, sizeof(T)); return a;
    }
    template<class T> T Get() const { T value{}; if (bytes.size() == sizeof(T)) std::memcpy(&value, bytes.data(), sizeof(T)); return value; }
    static Arg Object(const char* n, std::uintptr_t p) { return Data(n,Type::Object,p); }
    static Arg Int(const char* n, int v) { return Data(n,Type::Int,v); }
    static Arg Enum(const char* n, std::uint8_t v) { return Data(n,Type::Enum,v); }
    static Arg Float(const char* n, float v) { return Data(n,Type::Float,v); }
    static Arg Boolean(const char* n, bool v) { return Data(n,Type::Bool,std::int32_t(v)); }
    static Arg Vector(const char* n, extensions::Vec3 v, bool out=false) { return Data(n,Type::Vector,v,out); }
    static Arg String(const char* n, std::wstring v) { return {n,Type::String,{},std::move(v)}; }
    static Arg Objects(const char* n) { return {n,Type::ObjectArray,{},{},true}; }
};
struct CallResult { bool success{}; std::string message; };
// All reflection and engine allocation happens on the game thread. Arguments
// are matched by reflected name, offset, size, direction and cast flags.
CallResult Call(std::wstring_view function, std::span<Arg> arguments, std::uintptr_t instance = 0);
bool Property(std::uintptr_t object, Arg& value, bool write = false);
std::string ObjectName(std::uintptr_t object);
}
