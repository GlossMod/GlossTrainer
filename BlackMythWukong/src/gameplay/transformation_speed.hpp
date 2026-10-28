#pragma once

#include "transformation_bindings.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace gloss::gameplay::transformation::speed
{
    using Setting = MovementSpeedSettings;

    using Config = std::map<std::string, Setting>;
    inline constexpr float MinimumMultiplier = .1f;
    inline constexpr float MaximumMultiplier = 5.f;
    inline constexpr std::size_t MaximumRecords = 4096;
    inline constexpr std::size_t MaximumFileBytes = 1024 * 1024;

    inline bool ValidValue(float value)
    {
        return std::isfinite(value) && value >= MinimumMultiplier && value <= MaximumMultiplier;
    }

    inline bool Valid(Setting value)
    {
        return ValidValue(value.normal) && ValidValue(value.sprint);
    }

    inline Setting Get(const Config &config, std::string_view id)
    {
        const auto found = config.find(std::string(id));
        return found == config.end() ? Setting{1.f, 1.f} : found->second;
    }

    inline float Multiplier(Setting value, bool sprint)
    {
        return sprint ? value.sprint : value.normal;
    }

    inline std::string Serialize(const Config &config)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1\n" << std::setprecision(9);
        for (const auto &[id, value] : config)
            out << std::quoted(id) << ' ' << value.normal << ' ' << value.sprint << '\n';
        return out.str();
    }

    inline features::Result Parse(std::string_view text, Config &output)
    {
        if (text.size() > MaximumFileBytes) return {false, "角色速度配置文件过大"};
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string magic;
        if (!std::getline(in, magic) || magic != "GLOSS_TRANSFORMATION_SPEED_MULTIPLIERS_1")
            return {false, "角色速度配置版本无效"};

        Config loaded;
        while (true)
        {
            in >> std::ws;
            if (in.eof()) break;
            std::string id;
            Setting value;
            if (in.peek() != '"' || !(in >> std::quoted(id) >> value.normal >> value.sprint) ||
                id.empty() || id.size() > 2048 || !bindings::ValidText(id) || !Valid(value) ||
                loaded.size() >= MaximumRecords || loaded.contains(id))
                return {false, "角色速度倍率配置记录无效或重复"};
            loaded.emplace(std::move(id), value);
        }
        output = std::move(loaded);
        return {true, {}};
    }
}
