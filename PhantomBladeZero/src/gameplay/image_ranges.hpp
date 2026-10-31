#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace gloss::gameplay {
    // VirtualQuery walks page tables to size the region it reports, so one call into
    // a large image's code section can take milliseconds. Section headers answer the
    // same question for an address in a loaded module without leaving user mode.
    class ExecutableImageRanges {
        struct Range { std::uintptr_t begin{}, end{}; };
        std::array<Range, 32> ranges{};
        std::size_t count{};
    public:
        bool Capture(std::uintptr_t module) noexcept {
            count = 0;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
            const auto* section = IMAGE_FIRST_SECTION(nt);
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && count < ranges.size(); ++i, ++section) {
                if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !section->Misc.VirtualSize) continue;
                const auto begin = module + section->VirtualAddress;
                ranges[count++] = {begin, begin + section->Misc.VirtualSize};
            }
            return count != 0;
        }
        bool Contains(std::uintptr_t address) const noexcept {
            for (std::size_t i = 0; i < count; ++i)
                if (address >= ranges[i].begin && address < ranges[i].end) return true;
            return false;
        }
    };
}
