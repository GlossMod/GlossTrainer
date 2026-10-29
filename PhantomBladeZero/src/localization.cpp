#include "localization.hpp"
#include "runtime.hpp"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace gloss::localization {
    namespace {
        struct Translation { std::string_view source, english; };
        constexpr Translation translations[] = {
#include "localization_strings.inc"
        };
        std::mutex stateMutex;
        bool initialized{};
        std::filesystem::path settingsFile;
        std::atomic<Language> language{Language::Chinese};
        thread_local std::map<std::pair<Language, std::string>, std::string> frameStrings;

        const auto& Dictionary() {
            static const auto dictionary = [] {
                std::map<std::string_view, std::string_view, std::less<>> result;
                for (const auto& entry : translations) result.insert_or_assign(entry.source, entry.english);
                return result;
            }();
            return dictionary;
        }

        // Translate interpolated application diagnostics without translating captured
        // item names, paths or user input. Adjacent placeholders are not guessed.
        struct Pattern { std::vector<std::string_view> literals; std::string_view english; };
        std::vector<std::string_view> SplitPattern(std::string_view text) {
            std::vector<std::string_view> parts;
            std::size_t start{};
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '{') continue;
                const auto end = text.find('}', i + 1);
                if (end == text.npos) return {};
                parts.push_back(text.substr(start, i - start));
                i = end;
                start = end + 1;
            }
            if (parts.empty()) return {};
            parts.push_back(text.substr(start));
            return parts;
        }
        std::string TranslateEnglish(std::string_view source) {
            const auto& dictionary = Dictionary();
            if (const auto found = dictionary.find(source); found != dictionary.end())
                return std::string(found->second);
            constexpr std::string_view invalidParameter = "：参数为空、格式错误或超出允许范围";
            if (source.ends_with(invalidParameter))
                return TranslateEnglish(source.substr(0, source.size() - invalidParameter.size())) + TranslateEnglish(invalidParameter);
            constexpr std::string_view hotkeyConflict = "此快捷键已绑定：";
            if (source.starts_with(hotkeyConflict))
                return TranslateEnglish(hotkeyConflict) + TranslateEnglish(source.substr(hotkeyConflict.size()));
            const auto marker = source.find("##");
            if (marker != source.npos)
                return TranslateEnglish(source.substr(0, marker)) + std::string(source.substr(marker));
            const auto newline = source.find('\n');
            if (newline != source.npos)
                return TranslateEnglish(source.substr(0, newline)) + "\n" + TranslateEnglish(source.substr(newline + 1));
            static const auto patterns = [] {
                std::vector<Pattern> result;
                for (const auto& [key, value] : Dictionary()) {
                    auto literals = SplitPattern(key);
                    if (literals.size() >= 2 && std::any_of(literals.begin(), literals.end(), [](auto literal) { return !literal.empty(); }))
                        result.push_back({std::move(literals), value});
                }
                return result;
            }();
            for (const auto& pattern : patterns) {
                if (!source.starts_with(pattern.literals.front())) continue;
                std::size_t offset = pattern.literals.front().size();
                std::vector<std::string_view> captures;
                bool matched = true;
                for (std::size_t i = 1; i < pattern.literals.size(); ++i) {
                    const auto literal = pattern.literals[i];
                    const bool last = i + 1 == pattern.literals.size();
                    if (literal.empty() && !last) { matched = false; break; }
                    const auto end = literal.empty() ? source.size() : source.find(literal, offset);
                    if (end == source.npos || (last && end + literal.size() != source.size())) { matched = false; break; }
                    captures.push_back(source.substr(offset, end - offset));
                    offset = end + literal.size();
                }
                if (!matched) continue;
                std::string result;
                std::size_t index{}, start{};
                for (std::size_t i = 0; i < pattern.english.size(); ++i) {
                    if (pattern.english[i] != '{') continue;
                    const auto end = pattern.english.find('}', i + 1);
                    if (end == pattern.english.npos || index >= captures.size()) { matched = false; break; }
                    result += pattern.english.substr(start, i - start);
                    const auto token = pattern.english.substr(i + 1, end - i - 1);
                    const auto selected = !token.empty() && token.front() >= '0' && token.front() <= '9'
                        ? static_cast<std::size_t>(token.front() - '0') : index;
                    if (selected >= captures.size()) { matched = false; break; }
                    result += captures[selected];
                    ++index;
                    i = end;
                    start = end + 1;
                }
                if (matched && index == captures.size()) {
                    result += pattern.english.substr(start);
                    return result;
                }
            }
            return std::string(source);
        }
    }

    void Initialize(const std::filesystem::path& file) {
        std::lock_guard lock(stateMutex);
        if (initialized && file.empty()) return;
        settingsFile = file.empty() ? DataDirectory() / L"language.ini" : file;
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Interface", L"Language", L"zh-CN", value,
            static_cast<DWORD>(std::size(value)), settingsFile.c_str());
        language.store(std::wstring_view(value) == L"en" ? Language::English : Language::Chinese);
        initialized = true;
    }

    bool SetLanguage(Language selected, std::string* error) {
        Initialize();
        if (error) error->clear();
        std::lock_guard lock(stateMutex);
        const auto fail = [&] {
            if (error) *error = "语言设置保存失败，已保留原设置";
            return false;
        };
        if (selected != Language::Chinese && selected != Language::English) return fail();
        if (selected == language.load()) return true;
        try {
            std::error_code ec;
            std::filesystem::create_directories(settingsFile.parent_path(), ec);
            if (ec) return fail();
            auto temporary = settingsFile;
            temporary += L".tmp";
            const std::string_view contents = selected == Language::English
                ? "[Interface]\r\nLanguage=en\r\n" : "[Interface]\r\nLanguage=zh-CN\r\n";
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return fail();
            DWORD written{};
            const bool complete = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
                && written == contents.size() && FlushFileBuffers(file);
            const bool closed = CloseHandle(file) != FALSE;
            if (!complete || !closed || !MoveFileExW(temporary.c_str(), settingsFile.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DeleteFileW(temporary.c_str());
                return fail();
            }
            // Do not clear frameStrings: ImGui may still hold a pointer this frame.
            language.store(selected);
            return true;
        } catch (...) { return fail(); }
    }
    Language GetLanguage() { Initialize(); return language.load(); }
    std::string Translate(std::string_view source) {
        return GetLanguage() == Language::Chinese ? std::string(source) : TranslateEnglish(source);
    }
    const char* Text(std::string_view source) {
        const auto selected = GetLanguage();
        const auto [entry, inserted] = frameStrings.try_emplace({selected, std::string(source)});
        if (inserted) entry->second = selected == Language::Chinese ? std::string(source) : TranslateEnglish(source);
        return entry->second.c_str();
    }
    void BeginFrame() { frameStrings.clear(); }
}
