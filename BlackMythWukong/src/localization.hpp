#pragma once
#include "features.hpp"
#include <string>
#include <string_view>

namespace gloss::localization
{
    enum class Preference { FollowGame, Chinese, English };
    enum class Language { Chinese, English };
    // OS culture is only a temporary UI fallback, never evidence of game language.
    enum class DetectionSource { OperatingSystem, GameConfig, GameRuntime };
    struct Detection
    {
        std::string culture;
        DetectionSource source = DetectionSource::OperatingSystem;
    };
    void Initialize();
    Preference GetPreference();
    features::Result SetPreference(Preference preference);
    Language GetLanguage();
    Detection GetDetection();
    std::string GetGameCulture();
    // Configuration and game-thread probes report cultures through this boundary.
    // A lower-priority observation cannot replace successful runtime evidence.
    bool SetDetectedCulture(std::string_view culture, DetectionSource source);
    // Only application-owned text belongs here. User names/labels remain verbatim.
    const char *Text(const char *chinese);
    std::string Translate(std::string_view chinese);
}
