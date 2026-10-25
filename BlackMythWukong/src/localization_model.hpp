#pragma once
#include "localization.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <span>
#include <vector>

namespace gloss::localization::model
{
    inline std::string_view Trim(std::string_view value)
    {
        constexpr auto space = " \t\r\n";
        const auto first = value.find_first_not_of(space);
        return first == std::string_view::npos ? std::string_view{} : value.substr(first, value.find_last_not_of(space) - first + 1);
    }
    inline bool EqualAscii(std::string_view left, std::string_view right)
    {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b)
        { return std::tolower(a) == std::tolower(b); });
    }
    inline std::optional<Language> CultureToLanguage(std::string_view culture)
    {
        culture = Trim(culture);
        if (culture.empty() || culture.size() > 85)
            return {};
        std::size_t start{};
        bool first = true;
        std::string_view language;
        while (start < culture.size())
        {
            const auto end = culture.find_first_of("-_", start);
            const auto part = culture.substr(start, end == std::string_view::npos ? culture.size() - start : end - start);
            if (part.empty() || part.size() > 8 || (first && part.size() < 2))
                return {};
            if (!std::all_of(part.begin(), part.end(), [first](unsigned char c)
                { return first ? ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) :
                    ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')); }))
                return {};
            if (first)
                language = part;
            first = false;
            if (end == std::string_view::npos)
                break;
            start = end + 1;
            if (start == culture.size())
                return {};
        }
        return EqualAscii(language, "zh") ? Language::Chinese : Language::English;
    }
    inline std::optional<Preference> ParsePreference(std::string_view value)
    {
        value = Trim(value);
        if (EqualAscii(value, "auto")) return Preference::FollowGame;
        if (EqualAscii(value, "zh-CN")) return Preference::Chinese;
        if (EqualAscii(value, "en")) return Preference::English;
        return {};
    }
    inline std::string_view PreferenceValue(Preference preference)
    {
        switch (preference)
        {
        case Preference::FollowGame: return "auto";
        case Preference::Chinese: return "zh-CN";
        case Preference::English: return "en";
        }
        return {};
    }
    inline Language EffectiveLanguage(Preference preference, Language detected)
    {
        return preference == Preference::Chinese ? Language::Chinese : preference == Preference::English ? Language::English : detected;
    }
    inline std::optional<std::string> ReadIniCulture(std::string_view ini)
    {
        if (ini.starts_with("\xEF\xBB\xBF")) ini.remove_prefix(3);
        bool internationalization{};
        std::optional<std::string> culture, language;
        while (!ini.empty())
        {
            const auto newline = ini.find('\n');
            auto line = Trim(ini.substr(0, newline));
            if (newline == std::string_view::npos) ini = {};
            else ini.remove_prefix(newline + 1);
            if (line.empty() || line.front() == ';' || line.front() == '#') continue;
            if (line.front() == '[')
            {
                internationalization = EqualAscii(line, "[Internationalization]");
                continue;
            }
            if (!internationalization) continue;
            const auto equal = line.find('=');
            if (equal == std::string_view::npos) continue;
            const auto key = Trim(line.substr(0, equal));
            auto value = Trim(line.substr(equal + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (!CultureToLanguage(value)) continue;
            if (EqualAscii(key, "Language")) language = value;
            else if (EqualAscii(key, "Culture")) culture = value;
        }
        return language ? language : culture;
    }
    struct TemplatePart
    {
        std::string_view literal;
        int index = -1;
    };
    inline std::optional<std::vector<TemplatePart>> ParseTemplate(std::string_view pattern)
    {
        std::vector<TemplatePart> result;
        std::size_t start{};
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            if (pattern[i] == '}') return {};
            if (pattern[i] != '{') continue;
            if (i + 2 >= pattern.size() || pattern[i + 1] < '0' || pattern[i + 1] > '7' || pattern[i + 2] != '}') return {};
            if (i != start) result.push_back({pattern.substr(start, i - start), -1});
            result.push_back({{}, pattern[i + 1] - '0'});
            i += 2;
            start = i + 1;
        }
        if (start != pattern.size()) result.push_back({pattern.substr(start), -1});
        return result;
    }
    inline bool ValidTranslation(std::string_view source, std::string_view translation)
    {
        const auto left = ParseTemplate(source), right = ParseTemplate(translation);
        if (!left || !right) return false;
        std::array<unsigned, 8> sourceCount{}, translatedCount{};
        for (const auto &part : *left) if (part.index >= 0) ++sourceCount[part.index];
        for (const auto &part : *right) if (part.index >= 0) ++translatedCount[part.index];
        return sourceCount == translatedCount;
    }
    inline std::optional<std::vector<std::string>> MatchTemplate(std::string_view pattern, std::string_view text)
    {
        const auto parts = ParseTemplate(pattern);
        if (!parts || text.size() > 16384) return {};
        std::vector<std::string> captures(8);
        std::array<bool, 8> present{};
        std::size_t offset{};
        int maximum = -1;
        for (std::size_t i = 0; i < parts->size(); ++i)
        {
            const auto &part = (*parts)[i];
            if (part.index < 0)
            {
                if (!text.substr(offset).starts_with(part.literal)) return {};
                offset += part.literal.size();
                continue;
            }
            // Adjacent placeholders have no stable separator and are rejected.
            if (i + 1 < parts->size() && (*parts)[i + 1].index >= 0) return {};
            const auto next = i + 1 < parts->size() ? (*parts)[i + 1].literal : std::string_view{};
            const auto end = next.empty() ? text.size() : text.find(next, offset);
            if (end == std::string_view::npos) return {};
            const auto capture = text.substr(offset, end - offset);
            if (present[part.index] && captures[part.index] != capture) return {};
            captures[part.index] = capture;
            present[part.index] = true;
            maximum = std::max(maximum, part.index);
            offset = end;
        }
        if (offset != text.size()) return {};
        for (int i = 0; i <= maximum; ++i) if (!present[i]) return {};
        captures.resize(static_cast<std::size_t>(maximum + 1));
        return captures;
    }
    inline std::optional<std::string> FormatTemplate(std::string_view pattern, std::span<const std::string> captures)
    {
        const auto parts = ParseTemplate(pattern);
        if (!parts) return {};
        std::string result;
        for (const auto &part : *parts)
        {
            if (part.index < 0) result += part.literal;
            else if (static_cast<std::size_t>(part.index) < captures.size()) result += captures[part.index];
            else return {};
        }
        return result;
    }
}
