#pragma once
#include "transformation.hpp"
#include <optional>

namespace gloss::gameplay::transformation::hotkeys
{
    struct CapturedBinding
    {
        std::string characterId;
        int skillId{};
        unsigned key{}, modifiers{};
    };
    void BeginCapture(std::string_view characterId, int skillId);
    void CancelCapture();
    bool AnyCapture();
    bool Capturing(std::string_view characterId, int skillId);
    std::optional<CapturedBinding> TakeCaptured();
    features::Result GetCaptureResult();
    features::Result Conflicts(unsigned key, unsigned modifiers);
    std::string Label(unsigned key, unsigned modifiers);
    // Called only once per physical initial key-down by the input router.
    bool HandleHotkey(unsigned key, unsigned modifiers);
    bool IsHotkey(unsigned key, unsigned modifiers);
}
