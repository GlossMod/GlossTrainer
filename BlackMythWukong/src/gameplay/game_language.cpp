#include "game_language.hpp"
#include "engine_bridge.hpp"
#include "../localization.hpp"
#include <array>

namespace gloss::gameplay::game_language
{
    void Tick()
    {
        if (!engine::IsGameThread()) return;
        static ULONGLONG lastProbe{};
        static bool probed{};
        const auto now = GetTickCount64();
        if (probed && now - lastProbe < 5000) return;
        probed = true;
        lastProbe = now;
        // GetCurrentLanguage is the UI/text language; culture is the fallback
        // for builds that expose only the broader internationalization API.
        for (const auto *function : {L"/Script/Engine.KismetInternationalizationLibrary:GetCurrentLanguage",
                                     L"/Script/Engine.KismetInternationalizationLibrary:GetCurrentCulture"})
        {
            auto arguments = std::array{engine::Arg::String("ReturnValue", {}, true)};
            if (!engine::Call(function, arguments).success || arguments[0].text.empty() || arguments[0].text.size() > 85) continue;
            std::string culture;
            bool ascii = true;
            for (const auto c : arguments[0].text)
            {
                if (c > 127) { ascii = false; break; }
                culture.push_back(static_cast<char>(c));
            }
            if (ascii && localization::SetDetectedCulture(culture, localization::DetectionSource::GameRuntime)) return;
        }
    }
}
