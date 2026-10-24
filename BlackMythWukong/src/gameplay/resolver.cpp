#include "resolver.hpp"
#include "midhook.hpp"
#include <array>
#include <limits>

namespace gloss::gameplay
{
    namespace
    {
        constexpr std::uintptr_t LowestPointer = 0x10000;
        constexpr std::uintptr_t HighestPointer = 0x0000800000000000ull;
        constexpr unsigned MaxForwarders = 4;

        bool Pointer(std::uintptr_t address) noexcept
        {
            return address >= LowestPointer && address < HighestPointer;
        }
        bool Executable(std::uintptr_t address, std::size_t size = 1) noexcept
        {
            if (!Pointer(address) || !size || size > HighestPointer - address)
                return false;
            const auto end = address + size;
            auto cursor = address;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION memory{};
                if (!VirtualQuery(reinterpret_cast<const void *>(cursor), &memory, sizeof(memory)))
                    return false;
                const auto protection = memory.Protect & 0xff;
                if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) ||
                    (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ &&
                     protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY))
                    return false;
                const auto begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
                if (memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - begin)
                    return false;
                const auto regionEnd = begin + memory.RegionSize;
                if (regionEnd <= cursor)
                    return false;
                cursor = regionEnd < end ? regionEnd : end;
            }
            return true;
        }
        std::uintptr_t Relative(std::uintptr_t instruction, unsigned displacementOffset, unsigned length) noexcept
        {
            if (!Pointer(instruction) || displacementOffset + sizeof(std::int32_t) > length || length > HighestPointer - instruction)
                return 0;
            std::int32_t displacement{};
            if (!ReadMemory(instruction + displacementOffset, &displacement, sizeof(displacement)))
                return 0;
            const auto result = static_cast<std::int64_t>(instruction + length) + displacement;
            return result >= static_cast<std::int64_t>(LowestPointer) && result < static_cast<std::int64_t>(HighestPointer)
                       ? static_cast<std::uintptr_t>(result)
                       : 0;
        }
        std::uintptr_t Follow(std::uintptr_t entry) noexcept
        {
            std::array<std::uintptr_t, MaxForwarders + 1> seen{};
            for (unsigned depth = 0; depth <= MaxForwarders; ++depth)
            {
                if (!Executable(entry))
                    return 0;
                for (unsigned index = 0; index < depth; ++index)
                    if (seen[index] == entry)
                        return 0;
                seen[depth] = entry;
                std::array<unsigned char, 2> opcode{};
                if (!ReadMemory(entry, opcode.data(), opcode.size()))
                    return 0;
                const bool indirect = opcode[0] == 0xff && opcode[1] == 0x25;
                const bool direct = opcode[0] == 0xe9;
                if (!indirect && !direct)
                    return entry;
                if (depth == MaxForwarders || !Executable(entry, indirect ? 6 : 5))
                    return 0;
                if (indirect)
                {
                    const auto slot = Relative(entry, 2, 6);
                    entry = slot ? Read<std::uintptr_t>(slot) : 0;
                }
                else
                    entry = Relative(entry, 1, 5);
            }
            return 0;
        }
        bool Matches(std::uintptr_t address, const char *text)
        {
            const auto pattern = Pattern::Parse(text);
            if (pattern.bytes.empty() || !Executable(address, pattern.bytes.size()))
                return false;
            const auto found = FindPattern(pattern, address, pattern.bytes.size(), 2);
            return found.size() == 1 && found[0] == address;
        }
        std::uintptr_t Call(std::uintptr_t instruction) noexcept
        {
            if (!Executable(instruction, 5) || Read<unsigned char>(instruction) != 0xe8)
                return 0;
            return Follow(Relative(instruction, 1, 5));
        }
    }

    std::uintptr_t ResolveListProbe(std::uintptr_t method) noexcept
    {
        try
        {
            method = Follow(method);
            // This is the actual captured wrapper, not an address derived from an
            // assembly IL RVA. Its two calls and generic-context load are decoded
            // only after the complete observed instruction shape has matched.
            if (!method || !Matches(method,
                                    "55 48 8B EC 48 83 EC 30 48 89 4D F8 48 89 55 F0 "
                                    "E8 ?? ?? ?? ?? 48 8B D0 ?? 8B 15 ?? ?? ?? ?? "
                                    "48 8B 4D F8 44 0F B6 45 F0 E8 ?? ?? ?? ?? 48 8D 65 00 5D C3"))
                return 0;
            // REX.B is ignored for RIP-relative addressing. The live capture uses
            // 4D; 4C encodes the identical mov r10,[rip+disp32] operation.
            const auto contextRex = Read<unsigned char>(method + 0x18);
            if (contextRex != 0x4c && contextRex != 0x4d)
                return 0;

            // Verify the first call returns this.FloatAttrs from [rcx+0x20], so a
            // structurally similar generic wrapper for another field is rejected.
            const auto fieldGetter = Call(method + 0x10);
            if (!fieldGetter || !Matches(fieldGetter,
                                         "55 48 8B EC 48 83 EC 10 48 89 4D F8 48 8B C1 48 8B 40 20 48 8D 65 00 5D C3"))
                return 0;

            const auto getValue = Call(method + 0x28);
            if (!getValue || !Matches(getValue,
                                      "55 48 8B EC 48 83 EC 40 4C 89 55 F8 48 89 4D F0 "
                                      "48 89 55 E8 4C 89 45 E0 48 8B C2 83 38 00 48 8B 4D F8 "
                                      "E8 ?? ?? ?? ?? 48 8B 4D E8 0F B6 55 E0 FF D0 48 8D 65 00 5D C3"))
                return 0;

            const auto helper = Call(getValue + 0x22);
            // A non-null context[0x18] must be returned directly. Never invoke the
            // helper's null-slot initialization branch from the installer thread.
            if (!helper || !Matches(helper, "48 8B C1 48 8B 40 18 48 85 C0 74 01 C3"))
                return 0;
            const auto contextSlot = Relative(method + 0x18, 3, 7);
            const auto context = contextSlot ? Read<std::uintptr_t>(contextSlot) : 0;
            if (!Pointer(context) || context + 0x18 >= HighestPointer)
                return 0;
            const auto getter = Follow(Read<std::uintptr_t>(context + 0x18));
            if (!getter || !Matches(getter,
                                    "55 48 8B EC 48 83 EC 40 48 89 4D F8 48 89 55 F0 "
                                    "48 8B C1 48 8B 40 30 48 89 45 E0 48 8B 45 F8 48 8B 08 "
                                    "E8 ?? ?? ?? ?? 48 8B C8 40 F7 41 2D 01 00 00 00 75 05 E8 ?? ?? ?? ?? "
                                    "48 8B 45 E0 83 38 00 48 89 45 E8 48 8B 45 F8 48 8B 08 E8 ?? ?? ?? ?? "
                                    "48 8B 4D E8 48 63 55 F0 FF D0 48 8D 65 00 5D C3"))
                return 0;

            // Bound the uniqueness check to the validated 0x60-byte method body,
            // ending at its ret; scanning into adjacent generic methods is unsafe.
            const auto probes = FindPattern(Pattern::Parse("48 8B 4D E8 48 63 55 F0 FF D0 48 8D 65 00 5D C3"), getter, 0x60, 2);
            if (probes.size() != 1 || !Executable(probes[0] + 8, 2))
                return 0;
            // Re-read the mutable generic slot: a concurrent lazy initialization
            // must not make us return a probe belonging to a different target.
            if (Follow(Read<std::uintptr_t>(context + 0x18)) != getter ||
                Read<std::uintptr_t>(contextSlot) != context)
                return 0;
            return probes[0] + 8;
        }
        catch (...)
        {
            // Pattern allocation can fail. Resolution remains optional and retryable.
            return 0;
        }
    }
}
