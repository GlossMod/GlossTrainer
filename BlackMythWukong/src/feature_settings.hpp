#pragma once
#include "features.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace gloss::features::settings
{
    // These names are part of the on-disk format. Keep them when catalog labels
    // or enum ordering change; new features need a new, unique name here.
    inline constexpr std::array<std::string_view, Count> Names{
        "GodMode", "InfiniteHealth", "InfiniteMana", "InfiniteStamina", "InfiniteFocus",
        "InfiniteGourdUses", "SpellCooldown", "TransformationCooldown", "TransformationDuration",
        "InfiniteSpiritEnergy", "InfiniteVesselEnergy", "MaximumCriticalChance", "MaximumPoise",
        "PreventAbnormalStatus", "WillMultiplier", "ExperienceMultiplier", "RelicPoints", "GuaranteedDrop",
        "OneHitKill", "DamageMultiplier", "DefenseMultiplier", "StaminaConsumption", "GameSpeed", "EditWill",
        "EditSelectedItemCount", "AddExperience", "EditSparks", "PlayerSpeed", "MovementSpeed", "JumpHeight",
        "MaximumHealth", "MaximumMana", "MaximumStamina", "Attack", "Defense", "StaminaRecovery",
        "CriticalChance", "CriticalDamage", "DamageBonus", "DamageReduction", "ChillResistance",
        "BurnResistance", "PoisonResistance", "ShockResistance", "InfiniteItems", "Stealth", "BossStealth",
        "NoHitCollision", "InfiniteBuffDuration", "InfiniteVesselDuration", "BulletTime", "HideHud", "HidePlayer",
        "PauseBackground", "FreezeWorld", "ContinuousJump", "NoGravity", "FreeCamera", "FixedCameraDistance",
        "SummonCloud", "UnlockCloud", "UnlockShrines", "UnlockBossRush", "NewGameCycle"};
    static_assert([] {
        for (std::size_t i = 0; i < Count; ++i) {
            if (Names[i].empty()) return false;
            for (std::size_t j = i + 1; j < Count; ++j)
                if (Names[i] == Names[j]) return false;
        }
        return true;
    }());

    struct Feature
    {
        bool enabled{};
        double value{};
        double appliedValue{};
    };
    struct Data
    {
        bool hotkeysEnabled = true;
        std::array<Feature, Count> features{};
    };
    inline double Constrain(const Definition &definition, double value)
    {
        value = (std::max)(definition.minimum, (std::min)(definition.maximum, value));
        return definition.integer ? std::round(value) : value;
    }
    inline Data Defaults()
    {
        Data result;
        for (const auto &definition : Definitions()) {
            auto &feature = result.features[static_cast<std::size_t>(definition.id)];
            feature.value = feature.appliedValue = definition.initialValue;
        }
        return result;
    }
    inline std::string_view Trim(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == value.npos) return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }
    inline bool Boolean(std::string_view text, bool &value)
    {
        if (text != "0" && text != "1") return false;
        value = text == "1";
        return true;
    }
    inline bool Number(std::string_view text, double &value)
    {
        double parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    }
    // Start from defaults and ignore malformed individual fields. An unknown or
    // missing schema version rejects the file as a whole without rewriting it.
    inline bool Load(const std::filesystem::path &file, Data &data)
    {
        std::error_code error;
        if (std::filesystem::file_size(file, error) > 128 * 1024 || error) return false;
        std::ifstream stream(file, std::ios::binary);
        if (!stream) return false;
        auto parsed = Defaults();
        std::string section, line;
        bool versionValid{};
        while (std::getline(stream, line)) {
            std::string_view text = Trim(line);
            if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
            if (text.empty() || text.front() == ';' || text.front() == '#') continue;
            if (text.front() == '[' && text.back() == ']') {
                section = Trim(text.substr(1, text.size() - 2));
                continue;
            }
            const auto equal = text.find('=');
            if (equal == text.npos) continue;
            const auto key = Trim(text.substr(0, equal)), value = Trim(text.substr(equal + 1));
            if (section == "meta" && key == "version") versionValid = value == "1";
            else if (section == "global" && key == "hotkeys_enabled") Boolean(value, parsed.hotkeysEnabled);
            else for (std::size_t i = 0; i < Count; ++i) {
                if (section != Names[i]) continue;
                const auto &definition = GetDefinition(static_cast<Id>(i));
                auto &feature = parsed.features[i];
                if (key == "enabled" && definition.kind != Kind::Action) Boolean(value, feature.enabled);
                else if (definition.kind != Kind::Toggle && (key == "value" || key == "applied_value")) {
                    double number{};
                    if (Number(value, number)) (key == "value" ? feature.value : feature.appliedValue) = Constrain(definition, number);
                }
                break;
            }
        }
        if (!stream.eof() || !versionValid) return false;
        data = parsed;
        return true;
    }
    inline void AppendNumber(std::string &text, double value)
    {
        char buffer[128];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                          std::numeric_limits<double>::max_digits10);
        text.append(buffer, result.ptr);
    }
    // Caller serializes saves. The original file survives all write/flush/rename
    // failures; the temporary file always lives beside it on the same volume.
    inline bool Save(const std::filesystem::path &file, const Data &data)
    {
        std::string text = "[meta]\nversion=1\n\n[global]\nhotkeys_enabled=";
        text += data.hotkeysEnabled ? "1\n" : "0\n";
        for (std::size_t i = 0; i < Count; ++i) {
            const auto &definition = GetDefinition(static_cast<Id>(i));
            const auto &feature = data.features[i];
            text += "\n[";
            text += Names[i];
            text += "]\n";
            if (definition.kind != Kind::Action) text += feature.enabled ? "enabled=1\n" : "enabled=0\n";
            if (definition.kind != Kind::Toggle) {
                text += "value=";
                AppendNumber(text, feature.value);
                if (definition.kind == Kind::ToggleValue) {
                    text += "\napplied_value=";
                    AppendNumber(text, feature.appliedValue);
                }
                text += '\n';
            }
        }
        std::filesystem::create_directories(file.parent_path());
        auto temporary = file;
        temporary += L".tmp";
        const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        const bool complete = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                              written == text.size() && FlushFileBuffers(handle);
        const bool closed = CloseHandle(handle) != FALSE;
        if (complete && closed && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temporary.c_str());
        return false;
    }
}
