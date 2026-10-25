#pragma once
#include "transformation.hpp"
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace gloss::gameplay::transformation::bindings
{
    using Config = std::map<std::string, std::vector<SkillBinding>>;
    inline constexpr std::size_t MaximumBindings = 4096;
    inline constexpr std::size_t MaximumNameBytes = 256;
    inline constexpr std::size_t MaximumFileBytes = 16 * 1024 * 1024;

    inline bool ModifierKey(unsigned key)
    {
        return key == 0x10 || key == 0x11 || key == 0x12 || key == 0x5b || key == 0x5c ||
               (key >= 0xa0 && key <= 0xa5);
    }
    inline bool Bindable(unsigned key, unsigned modifiers)
    {
        if (!key) return !modifiers;
        return key >= 8 && key < 0xff && !(modifiers & ~7u) && !ModifierKey(key) &&
               key != 0x1b && key != 0xe5 && key != 0xe7 &&
               !(key == 0x24 && modifiers == (features::Ctrl | features::Shift));
    }
    inline bool Matches(const SkillBinding &binding, unsigned key, unsigned modifiers)
    {
        return key && binding.key == key && binding.modifiers == modifiers;
    }
    inline bool ValidText(std::string_view text)
    {
        // Reject control characters and malformed/overlong UTF-8, including
        // surrogate code points and values outside Unicode's scalar range.
        for (std::size_t i = 0; i < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[i++]);
            if (first < 0x20 || first == 0x7f) return false;
            if (first < 0x80) continue;
            unsigned code{}, remaining{}, minimum{};
            if (first >= 0xc2 && first <= 0xdf) { code = first & 0x1f; remaining = 1; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef) { code = first & 0x0f; remaining = 2; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; remaining = 3; minimum = 0x10000; }
            else return false;
            if (remaining > text.size() - i) return false;
            while (remaining--)
            {
                const auto next = static_cast<unsigned char>(text[i++]);
                if ((next & 0xc0) != 0x80) return false;
                code = (code << 6) | (next & 0x3f);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
                (code >= 0x80 && code <= 0x9f)) return false;
        }
        return true;
    }
    inline features::Result ValidShape(const std::vector<SkillBinding> &values)
    {
        if (values.size() > MaximumBindings) return {false, "角色技能配置超过数量限制"};
        std::set<int> ids;
        std::set<std::pair<unsigned, unsigned>> chords;
        for (const auto &value : values)
        {
            if (value.skillId <= 0 || !ids.insert(value.skillId).second) return {false, "技能 ID 无效或重复"};
            if (value.customName.size() > MaximumNameBytes || !ValidText(value.customName)) return {false, "自定义名称须为有效文字，且不超过 256 字节"};
            if (!Bindable(value.key, value.modifiers)) return {false, "按键或组合键无效，或由系统保留"};
            if (value.key && !chords.emplace(value.key, value.modifiers).second) return {false, "同一角色的多个技能不能使用相同组合键"};
        }
        return {true, {}};
    }
    inline features::Result ValidBindings(const std::vector<SkillBinding> &values, const std::vector<SkillEntry> &skills)
    {
        if (auto valid = ValidShape(values); !valid.success) return valid;
        for (const auto &value : values)
            if (std::none_of(skills.begin(), skills.end(), [&](const auto &skill) { return value.skillId == skill.id; }))
                return {false, "配置含不属于此角色或已经失效的技能 ID"};
        return {true, {}};
    }
    inline std::string Serialize(const Config &config)
    {
        std::ostringstream out;
        out << "GLOSS_TRANSFORMATION_BINDINGS_1\n";
        for (const auto &[id, values] : config)
        {
            out << std::quoted(id) << ' ' << values.size() << '\n';
            for (const auto &value : values)
                out << value.skillId << ' ' << value.key << ' ' << value.modifiers << ' ' << std::quoted(value.customName) << '\n';
        }
        return out.str();
    }
    inline features::Result Parse(std::string_view text, Config &output)
    {
        if (text.size() > MaximumFileBytes) return {false, "技能配置文件过大"};
        std::istringstream in{std::string(text)};
        std::string magic;
        if (!std::getline(in, magic) || magic != "GLOSS_TRANSFORMATION_BINDINGS_1") return {false, "技能配置文件版本无效"};
        Config loaded;
        while (true)
        {
            in >> std::ws;
            if (in.eof()) break;
            std::string id;
            std::size_t count{};
            if (in.peek() != '"' || !(in >> std::quoted(id) >> count) || id.empty() || id.size() > 2048 || !ValidText(id) ||
                count > MaximumBindings || loaded.size() >= 4096 || loaded.contains(id)) return {false, "角色技能配置记录损坏或重复"};
            std::vector<SkillBinding> values;
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                SkillBinding value;
                if (!(in >> value.skillId >> value.key >> value.modifiers)) return {false, "技能配置记录不完整"};
                in >> std::ws;
                if (in.peek() != '"' || !(in >> std::quoted(value.customName))) return {false, "技能名称记录无效"};
                values.push_back(std::move(value));
            }
            if (auto valid = ValidShape(values); !valid.success) return valid;
            loaded.emplace(std::move(id), std::move(values));
        }
        output = std::move(loaded);
        return {true, {}};
    }
}
