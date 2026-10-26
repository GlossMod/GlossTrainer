#include "localization.hpp"
#include "localization_model.hpp"
#include "runtime.hpp"
#include <Windows.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gloss::localization
{
    namespace
    {
        struct State
        {
            Preference preference = Preference::FollowGame;
            Detection detection;
            Language detected = Language::English;
        } state;
        std::mutex stateMutex;
        std::atomic_bool initialized{};
        std::atomic<Language> language{Language::English};
        struct Translation { const char *source; const char *english; };
        constexpr Translation translations[] = {
#include "localization_strings.inc"
#include "localization_catalog.inc"
        };
        constexpr Translation patterns[] = {
#include "localization_patterns.inc"
        };
        const auto &Dictionary()
        {
            static const auto dictionary = []
            {
                std::unordered_map<std::string_view, const char *> result;
                for (const auto &entry : translations)
                    result.try_emplace(entry.source, entry.english);
                return result;
            }();
            return dictionary;
        }
        std::string Utf8(std::wstring_view value)
        {
            if (value.empty()) return {};
            const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (!count) return {};
            std::string result(static_cast<std::size_t>(count), '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr)) return {};
            return result;
        }
        std::optional<std::string> ReadConfigCulture(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > 512 * 1024) return {};
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            std::string bytes(static_cast<std::size_t>(size), '\0');
            if (!stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) && !bytes.empty()) return {};
            if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE)
            {
                if (bytes.size() % 2) return {};
                std::wstring wide;
                wide.reserve((bytes.size() - 2) / 2);
                for (std::size_t i = 2; i + 1 < bytes.size(); i += 2)
                    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8)));
                bytes = Utf8(wide);
            }
            return model::ReadIniCulture(bytes);
        }
        Detection StartupDetection()
        {
            wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
            const auto localeId = MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT);
            Detection result{LCIDToLocaleName(localeId, locale, static_cast<int>(std::size(locale)), 0) ? Utf8(locale) : "en",
                             DetectionSource::OperatingSystem};
            std::vector<std::filesystem::path> roots;
            wchar_t local[32768]{};
            const auto localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
            if (localLength && localLength < std::size(local))
                roots.emplace_back(std::filesystem::path(local) / L"b1" / L"Saved" / L"Config");
            wchar_t executable[32768]{};
            const auto exeLength = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
            if (exeLength && exeLength < std::size(executable))
                roots.emplace_back(std::filesystem::path(executable).parent_path().parent_path().parent_path() / L"Saved" / L"Config");
            for (const auto &root : roots)
                for (const auto *platform : {L"Windows", L"WindowsNoEditor"})
                    for (const auto *file : {L"GameUserSettings.ini", L"Engine.ini"})
                        if (const auto culture = ReadConfigCulture(root / platform / file))
                            return {*culture, DetectionSource::GameConfig};
            return result;
        }
        bool WritePreference(Preference preference)
        {
            std::error_code error;
            std::filesystem::create_directories(DataDirectory(), error);
            if (error) return false;
            const auto path = DataDirectory() / L"language.ini";
            const auto temporary = DataDirectory() / L"language.ini.tmp";
            const auto bytes = std::string("[Interface]\r\nLanguage=") + std::string(model::PreferenceValue(preference)) + "\r\n";
            const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            DWORD written{};
            const bool complete = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                written == bytes.size() && FlushFileBuffers(file);
            const bool closed = CloseHandle(file) != FALSE;
            if (complete && closed && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return true;
            DeleteFileW(temporary.c_str());
            return false;
        }
        std::string TranslateEnglish(std::string_view source, bool translateRecoveryReason = true)
        {
            const auto &dictionary = Dictionary();
            if (const auto found = dictionary.find(source); found != dictionary.end()) return found->second;
            // Chord modifiers are canonical English tokens; translate only the
            // known mouse-button suffix, preserving user-provided names.
            std::size_t chordPrefix{};
            for (const auto modifier : {std::string_view("Ctrl+"), std::string_view("Shift+"), std::string_view("Alt+")})
                if (source.substr(chordPrefix).starts_with(modifier)) chordPrefix += modifier.size();
            if (chordPrefix)
                for (const auto mouse : {"鼠标左键", "鼠标右键", "鼠标中键", "鼠标侧键 1", "鼠标侧键 2"})
                    if (source.substr(chordPrefix) == mouse)
                        return std::string(source.substr(0, chordPrefix)) + dictionary.at(mouse);
            for (const auto &pattern : patterns)
            {
                if (!model::ValidTranslation(pattern.source, pattern.english)) continue;
                if (auto captures = model::MatchTemplate(pattern.source, source))
                {
                    // This capture is an application-owned exit reason. Other
                    // captures may contain player names and must stay verbatim.
                    // Limit nesting to one level even for unexpected input.
                    if (translateRecoveryReason && std::string_view(pattern.source) == "已恢复悟空：{0}" && !captures->empty())
                        (*captures)[0] = TranslateEnglish((*captures)[0], false);
                    if (const auto result = model::FormatTemplate(pattern.english, *captures)) return *result;
                }
            }
            return std::string(source);
        }
    }
    void Initialize()
    {
        if (initialized.load(std::memory_order_acquire)) return;
        std::lock_guard lock(stateMutex);
        if (initialized.load(std::memory_order_relaxed)) return;
        wchar_t value[32]{};
        const auto path = DataDirectory() / L"language.ini";
        GetPrivateProfileStringW(L"Interface", L"Language", L"auto", value, static_cast<DWORD>(std::size(value)), path.c_str());
        state.preference = model::ParsePreference(Utf8(value)).value_or(Preference::FollowGame);
        state.detection = StartupDetection();
        state.detected = model::CultureToLanguage(state.detection.culture).value_or(Language::English);
        language.store(model::EffectiveLanguage(state.preference, state.detected), std::memory_order_relaxed);
        initialized.store(true, std::memory_order_release);
    }
    Preference GetPreference()
    {
        Initialize();
        std::lock_guard lock(stateMutex);
        return state.preference;
    }
    features::Result SetPreference(Preference preference)
    {
        Initialize();
        if (model::PreferenceValue(preference).empty()) return {false, "语言选项无效"};
        std::lock_guard lock(stateMutex);
        // Commit after the complete, atomically replaced preference file exists.
        if (!WritePreference(preference)) return {false, "语言设置保存失败，已保留原设置"};
        state.preference = preference;
        language.store(model::EffectiveLanguage(state.preference, state.detected), std::memory_order_release);
        return {true, "语言设置已保存"};
    }
    Language GetLanguage()
    {
        Initialize();
        return language.load(std::memory_order_acquire);
    }
    Detection GetDetection()
    {
        Initialize();
        std::lock_guard lock(stateMutex);
        return state.detection;
    }
    std::string GetGameCulture()
    {
        const auto result = GetDetection();
        return result.source == DetectionSource::OperatingSystem ? std::string{} : result.culture;
    }
    bool SetDetectedCulture(std::string_view culture, DetectionSource source)
    {
        const auto detected = model::CultureToLanguage(culture);
        if (!detected || source < DetectionSource::OperatingSystem || source > DetectionSource::GameRuntime) return false;
        Initialize();
        std::lock_guard lock(stateMutex);
        if (source < state.detection.source) return false;
        state.detection = {std::string(model::Trim(culture)), source};
        state.detected = *detected;
        language.store(model::EffectiveLanguage(state.preference, state.detected), std::memory_order_release);
        return true;
    }
    const char *Text(const char *chinese)
    {
        if (!chinese || GetLanguage() == Language::Chinese) return chinese;
        const auto &dictionary = Dictionary();
        const auto found = dictionary.find(chinese);
        return found == dictionary.end() ? chinese : found->second;
    }
    std::string Translate(std::string_view chinese)
    {
        if (GetLanguage() == Language::Chinese) return std::string(chinese);
        return TranslateEnglish(chinese);
    }
}
