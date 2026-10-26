#pragma once
#include "transformation_bindings.hpp"
#include <cmath>
#include <locale>

namespace gloss::gameplay::transformation::camera
{
    using Config = std::map<std::string, float>;
    inline constexpr float MinimumMeters = 1.f, MaximumMeters = 200.f;
    inline constexpr std::size_t MaximumFileBytes = 1024 * 1024;

    inline bool Valid(float meters)
    {
        return std::isfinite(meters) && (meters == 0.f || (meters >= MinimumMeters && meters <= MaximumMeters));
    }
    inline float Distance(const Config &config, std::string_view id)
    {
        const auto found = config.find(std::string(id));
        return found == config.end() ? 0.f : found->second;
    }
    inline float ArmLength(float automaticCentimeters, float meters)
    {
        return meters > 0.f ? meters * 100.f : automaticCentimeters;
    }
    inline std::string Serialize(const Config &config)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "GLOSS_TRANSFORMATION_CAMERA_1\n" << std::setprecision(9);
        for (const auto &[id, meters] : config) out << std::quoted(id) << ' ' << meters << '\n';
        return out.str();
    }
    inline features::Result Parse(std::string_view text, Config &output)
    {
        if (text.size() > MaximumFileBytes) return {false, "角色视距配置文件过大"};
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string magic;
        if (!std::getline(in, magic) || magic != "GLOSS_TRANSFORMATION_CAMERA_1") return {false, "角色视距配置版本无效"};
        Config loaded;
        while (true)
        {
            in >> std::ws;
            if (in.eof()) break;
            std::string id;
            float meters{};
            if (in.peek() != '"' || !(in >> std::quoted(id) >> meters) || id.empty() || id.size() > 2048 ||
                !bindings::ValidText(id) || !Valid(meters) || loaded.size() >= 4096 || loaded.contains(id))
                return {false, "角色视距配置记录无效或重复"};
            loaded.emplace(std::move(id), meters);
        }
        output = std::move(loaded);
        return {true, {}};
    }
}
