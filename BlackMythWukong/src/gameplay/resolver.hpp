#pragma once
#include <cstdint>

namespace gloss::gameplay
{
    // Read-only resolver for the observed Windows x64 Mono AOT shape:
    // BUC_AttrContainer.GetFloatValue -> GetValue<TEnum,float> -> context[0x18]
    // -> GSBindList<float>.get_Item -> call rax (List<float>.get_Item).
    // Every body/forwarding instruction is validated; no target code is invoked.
    // Returns the exact call-rax probe address, or zero for unknown code, an
    // ambiguous match, inaccessible memory, or a still-uninitialized generic slot.
    // Failures are not cached: the installer may retry after Mono initializes it.
    std::uintptr_t ResolveListProbe(std::uintptr_t method) noexcept;
}
