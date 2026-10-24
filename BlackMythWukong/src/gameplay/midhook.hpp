#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gloss::gameplay {
// Layout matches the generated x64 bridge. rsp is the game's ORIGINAL stack.
struct Context {
    struct Vector { float f[4]; } xmm[16];
    std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    std::uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax, flags, resume;
    std::uintptr_t rsp() const { return reinterpret_cast<std::uintptr_t>(this) + sizeof(Context); }
};
static_assert(sizeof(Context) == 392);
using Callback = void(*)(Context&, void*);
class MidHook {
public:
    static std::unique_ptr<MidHook> Create(void* address, Callback callback, void* user, std::string& error);
    ~MidHook();
    MidHook(const MidHook&) = delete;
    void* Address() const { return address_; }
    void* Trampoline() const { return trampoline_; }
    Callback callback{};
    void* user{};
private:
    MidHook() = default;
    void* address_{};
    void* trampoline_{};
    void* bridge_{};
};
// Safe local-process reads: no stale pointer is dereferenced without SEH.
bool ReadMemory(std::uintptr_t address, void* data, std::size_t size) noexcept;
bool WriteMemory(std::uintptr_t address, const void* data, std::size_t size) noexcept;
template<class T> T Read(std::uintptr_t address, T fallback = {}) noexcept {
    T value{}; return ReadMemory(address, &value, sizeof(value)) ? value : fallback;
}
template<class T> bool Write(std::uintptr_t address, const T& value) noexcept {
    return WriteMemory(address, &value, sizeof(value));
}
struct Pattern {
    std::vector<int> bytes;
    static Pattern Parse(const char* text);
};
std::vector<std::uintptr_t> FindPattern(const Pattern& pattern, std::uintptr_t begin, std::size_t size, std::size_t limit = 2);
std::vector<std::uintptr_t> FindExecutablePattern(HMODULE module, const char* pattern);
}
