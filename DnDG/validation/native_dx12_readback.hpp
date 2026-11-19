#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct ImDrawData;

namespace gloss::dndg::validation {

// The caller owns the current ImGui context and must keep it alive until this
// helper is destroyed. Use on the fixture thread with no other renderer backend.
class Dx12Readback {
public:
    Dx12Readback(unsigned width, unsigned height);
    ~Dx12Readback();

    Dx12Readback(const Dx12Readback&) = delete;
    Dx12Readback& operator=(const Dx12Readback&) = delete;

    // The caller runs ImGui_ImplDX12_NewFrame before building draw data. A null
    // pointer captures just the clear. Pixels are tightly packed RGBA8, top-down.
    std::vector<std::uint8_t> Render(ImDrawData* drawData);
    unsigned Width() const noexcept;
    unsigned Height() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gloss::dndg::validation
