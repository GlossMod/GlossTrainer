#pragma once
#include "midhook.hpp"
#include "state.hpp"
namespace gloss::gameplay::attributes
{
    void CapturePlayer(Context &context, void *user);
    void ReadAttribute(Context &context, void *user);
    void ChangeAttribute(Context &context, void *user);
    bool HasPlayer() noexcept;
    std::uintptr_t PlayerActor() noexcept;
    bool Owns(features::Id id) noexcept;
}
