#include "midhook.hpp"
#include <MinHook.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <intrin.h>
#include <sstream>

namespace gloss::gameplay
{
    namespace
    {
        // Never unwind an exception across generated code (which has no unwind table).
        void Dispatch(Context *context, MidHook *hook) noexcept
        {
            const auto original = *context;
            __try
            {
                hook->callback(*context, hook->user);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                *context = original;
            }
        }
        struct Emitter
        {
            std::vector<unsigned char> code;
            void bytes(std::initializer_list<unsigned char> data) { code.insert(code.end(), data); }
            void imm64(std::uintptr_t value)
            {
                for (unsigned n = 0; n != 8; ++n)
                    code.push_back(static_cast<unsigned char>(value >> (n * 8)));
            }
            void dword(unsigned value)
            {
                for (unsigned n = 0; n != 4; ++n)
                    code.push_back(static_cast<unsigned char>(value >> (n * 8)));
            }
            void xmm(bool load, unsigned index)
            {
                bytes({0xf3});
                if (index >= 8)
                    bytes({0x44});
                bytes({0x0f, static_cast<unsigned char>(load ? 0x6f : 0x7f),
                       static_cast<unsigned char>(0x84 | ((index & 7) << 3)), 0x24});
                dword(index * 16);
            }
        };
        std::uintptr_t ScanRegion(const int *pattern, std::size_t length, std::uintptr_t begin, std::uintptr_t end) noexcept
        {
            __try
            {
                for (auto address = begin; address + length <= end; ++address)
                {
                    const auto *bytes = reinterpret_cast<const unsigned char *>(address);
                    bool equal = true;
                    for (std::size_t n = 0; n < length; ++n)
                    {
                        const auto expected = pattern[n];
                        const bool matches = expected < 0 || (expected <= 255 ? bytes[n] == expected : (bytes[n] >= ((expected >> 8) & 255) && bytes[n] <= (expected & 255)));
                        if (!matches)
                        {
                            equal = false;
                            break;
                        }
                    }
                    if (equal)
                        return address;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            return 0;
        }
    }
    bool ReadMemory(std::uintptr_t address, void *data, std::size_t size) noexcept
    {
        if (address < 0x10000 || size > 0x100000)
            return false;
        __try
        {
            std::memcpy(data, reinterpret_cast<const void *>(address), size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    bool WriteMemory(std::uintptr_t address, const void *data, std::size_t size) noexcept
    {
        if (address < 0x10000 || size > 0x100000)
            return false;
        __try
        {
            std::memcpy(reinterpret_cast<void *>(address), data, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    std::unique_ptr<MidHook> MidHook::Create(void *address, Callback callback, void *user, std::string &error)
    {
        auto hook = std::unique_ptr<MidHook>(new MidHook);
        hook->address_ = address;
        hook->callback = callback;
        hook->user = user;
        hook->bridge_ = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!hook->bridge_)
        {
            error = "VirtualAlloc failed";
            return {};
        }
        DWORD old{};
        if (!VirtualProtect(hook->bridge_, 4096, PAGE_EXECUTE_READ, &old))
        {
            error = "VirtualProtect failed";
            hook->address_ = nullptr;
            return {};
        }
        auto status = MH_CreateHook(address, hook->bridge_, &hook->trampoline_);
        if (status != MH_OK)
        {
            error = MH_StatusToString(status);
            hook->address_ = nullptr;
            return {};
        }
        Emitter out;
        out.bytes({0x50, 0x9c}); // reserved continuation slot; flags
        out.bytes({0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57});
        for (unsigned n = 0; n < 8; ++n)
            out.bytes({0x41, static_cast<unsigned char>(0x50 + n)});
        out.bytes({0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00});
        for (unsigned n = 0; n < 16; ++n)
            out.xmm(false, n);
        out.bytes({0x48, 0xb8});
        out.imm64(reinterpret_cast<std::uintptr_t>(hook->trampoline_));
        out.bytes({0x48, 0x89, 0x84, 0x24, 0x80, 0x01, 0x00, 0x00});
        // Preserve x87, MXCSR, and enabled extended vector state before C++/CRT code
        // can use AVX. Restore that state, then load the callback's edited XMM lanes.
        int cpu[4]{};
        __cpuid(cpu, 1);
        const bool xsave = (cpu[2] & (1 << 26)) && (cpu[2] & (1 << 27));
        const auto mask = xsave ? _xgetbv(0) : 0ull;
        unsigned stateSize = 512;
        if (xsave)
        {
            __cpuidex(cpu, 0xd, 0);
            stateSize = static_cast<unsigned>(cpu[1]);
        }
        if (stateSize > 65536)
        {
            error = "Unsupported extended CPU state size";
            return {};
        }
        out.bytes({0x48, 0x89, 0xe3}); // rbx = Context
        for (unsigned left = stateSize + 64; left;)
        {
            const auto step = (std::min)(left, 4096u);
            out.bytes({0x48, 0x81, 0xec});
            out.dword(step);
            out.bytes({0xf6, 0x04, 0x24, 0x00}); // probe each stack page
            left -= step;
        }
        out.bytes({0x48, 0x83, 0xe4, 0xc0, 0x48, 0x89, 0xe6}); // 64-byte aligned rsi
        if (xsave)
        {
            for (unsigned offset = 512; offset < 576; offset += 8)
            {
                out.bytes({0x48, 0xc7, 0x86});
                out.dword(offset);
                out.dword(0);
            }
            out.bytes({0xb8});
            out.dword(static_cast<unsigned>(mask));
            out.bytes({0xba});
            out.dword(static_cast<unsigned>(mask >> 32));
            out.bytes({0x48, 0x0f, 0xae, 0x26}); // xsave64 [rsi]
        }
        else
            out.bytes({0x48, 0x0f, 0xae, 0x06}); // fxsave64 [rsi]
        out.bytes({0x48, 0x83, 0xec, 0x20});
        out.bytes({0x48, 0x89, 0xd9, 0x48, 0xba});
        out.imm64(reinterpret_cast<std::uintptr_t>(hook.get()));
        out.bytes({0x48, 0xb8});
        out.imm64(reinterpret_cast<std::uintptr_t>(&Dispatch));
        out.bytes({0xff, 0xd0});
        if (xsave)
        {
            out.bytes({0xb8});
            out.dword(static_cast<unsigned>(mask));
            out.bytes({0xba});
            out.dword(static_cast<unsigned>(mask >> 32));
            out.bytes({0x48, 0x0f, 0xae, 0x2e}); // xrstor64 [rsi]
        }
        else
            out.bytes({0x48, 0x0f, 0xae, 0x0e});
        out.bytes({0x48, 0x89, 0xdc});
        for (unsigned n = 0; n < 16; ++n)
            out.xmm(true, n);
        out.bytes({0x48, 0x81, 0xc4, 0x00, 0x01, 0x00, 0x00});
        for (int n = 7; n >= 0; --n)
            out.bytes({0x41, static_cast<unsigned char>(0x58 + n)});
        out.bytes({0x5f, 0x5e, 0x5d, 0x5b, 0x5a, 0x59, 0x58, 0x9d});
        // Jump rather than ret: preserve the game's shadow-stack call/return balance.
        out.bytes({0x48, 0x8d, 0x64, 0x24, 0x08, 0xff, 0x64, 0x24, 0xf8});
        if (!VirtualProtect(hook->bridge_, 4096, PAGE_READWRITE, &old))
        {
            error = "VirtualProtect failed";
            return {};
        }
        std::memcpy(hook->bridge_, out.code.data(), out.code.size());
        if (!VirtualProtect(hook->bridge_, 4096, PAGE_EXECUTE_READ, &old))
        {
            error = "VirtualProtect failed";
            return {};
        }
        FlushInstructionCache(GetCurrentProcess(), hook->bridge_, out.code.size());
        status = MH_EnableHook(address);
        if (status != MH_OK)
        {
            error = MH_StatusToString(status);
            return {};
        }
        return hook;
    }
    MidHook::~MidHook()
    {
        // Process-lifetime hooks are intentionally retained by the gameplay owner.
        if (address_)
        {
            MH_DisableHook(address_);
            MH_RemoveHook(address_);
        }
        if (bridge_)
            VirtualFree(bridge_, 0, MEM_RELEASE);
    }
    Pattern Pattern::Parse(const char *text)
    {
        Pattern pattern;
        std::istringstream stream(text);
        std::string token;
        while (stream >> token)
        {
            if (token == "?" || token == "??" || token == "*")
                pattern.bytes.push_back(-1);
            else if (token.size() == 5 && token[2] == ':')
            {
                unsigned low{}, high{};
                const auto first = std::from_chars(token.data(), token.data() + 2, low, 16);
                const auto second = std::from_chars(token.data() + 3, token.data() + 5, high, 16);
                if (first.ec != std::errc{} || first.ptr != token.data() + 2 ||
                    second.ec != std::errc{} || second.ptr != token.data() + 5 || low > high)
                    return {};
                pattern.bytes.push_back(0x10000 | (low << 8) | high);
            }
            else
            {
                unsigned byte{};
                const auto result = std::from_chars(token.data(), token.data() + token.size(), byte, 16);
                if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || byte > 255)
                    return {};
                pattern.bytes.push_back(static_cast<int>(byte));
            }
        }
        return pattern;
    }
    std::vector<std::uintptr_t> FindPattern(const Pattern &pattern, std::uintptr_t begin, std::size_t size, std::size_t limit)
    {
        std::vector<std::uintptr_t> matches;
        if (pattern.bytes.empty() || pattern.bytes.size() > size || !limit)
            return matches;
        // Only scan committed readable regions; module code can contain guard pages.
        auto cursor = begin;
        const auto end = begin + size;
        while (cursor < end)
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void *>(cursor), &memory, sizeof(memory)))
                break;
            const auto regionEnd = (std::min)(end, reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize);
            if (regionEnd <= cursor)
                break;
            const auto protection = memory.Protect & 0xff;
            const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                                  protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (memory.State == MEM_COMMIT && readable && !(memory.Protect & PAGE_GUARD))
            {
                auto position = cursor;
                while (const auto address = ScanRegion(pattern.bytes.data(), pattern.bytes.size(), position, regionEnd))
                {
                    matches.push_back(address);
                    if (matches.size() >= limit)
                        return matches;
                    position = address + 1;
                }
            }
            cursor = regionEnd;
        }
        return matches;
    }
    std::vector<std::uintptr_t> FindExecutablePattern(HMODULE module, const char *text)
    {
        std::vector<std::uintptr_t> matches;
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto dos = Read<IMAGE_DOS_HEADER>(base);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
            return matches;
        const auto nt = Read<IMAGE_NT_HEADERS64>(base + dos.e_lfanew);
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
            return matches;
        auto sectionAddress = base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        const auto pattern = Pattern::Parse(text);
        for (unsigned n = 0; n < nt.FileHeader.NumberOfSections; ++n)
        {
            const auto section = Read<IMAGE_SECTION_HEADER>(sectionAddress + n * sizeof(IMAGE_SECTION_HEADER));
            if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            auto found = FindPattern(pattern, base + section.VirtualAddress, section.Misc.VirtualSize, 2);
            matches.insert(matches.end(), found.begin(), found.end());
            if (matches.size() >= 2)
                break;
        }
        return matches;
    }
}
