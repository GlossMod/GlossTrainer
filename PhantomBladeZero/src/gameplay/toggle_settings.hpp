#pragma once

#include "catalog.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

namespace gloss::gameplay::toggle_settings {
    using Data = std::map<std::string, bool>;

    inline Data Defaults() {
        Data data;
        for (const auto& toggle : ToggleCatalog()) data.emplace(toggle.id, false);
        return data;
    }

    inline std::string_view Trim(std::string_view value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == value.npos) return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    // Stable catalog IDs are the on-disk keys. Unknown IDs cannot become actions.
    // Missing files are normal on first launch; malformed schemas stay untouched.
    inline bool Load(const std::filesystem::path& file, Data& data, std::string& error) {
        error.clear();
        std::error_code fileError;
        const bool exists = std::filesystem::exists(file, fileError);
        if (fileError) { error = "无法检查功能设置文件"; return false; }
        if (!exists) { data = Defaults(); return true; }
        if (std::filesystem::file_size(file, fileError) > 64 * 1024 || fileError) {
            error = "功能设置文件无法读取或超过 64 KiB";
            return false;
        }
        std::ifstream stream(file, std::ios::binary);
        if (!stream) { error = "无法读取功能设置文件"; return false; }
        auto parsed = Defaults();
        std::string section, line;
        bool versionValid{};
        while (std::getline(stream, line)) {
            auto text = Trim(line);
            if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
            if (text.empty() || text.front() == ';' || text.front() == '#') continue;
            if (text.front() == '[' && text.back() == ']') {
                section = Trim(text.substr(1, text.size() - 2));
                continue;
            }
            const auto equal = text.find('=');
            if (equal == text.npos) continue;
            const auto key = Trim(text.substr(0, equal));
            const auto value = Trim(text.substr(equal + 1));
            if (section == "meta" && key == "version") versionValid = value == "1";
            else if (section == "toggles" && (value == "0" || value == "1")) {
                const auto found = parsed.find(std::string(key));
                if (found != parsed.end()) found->second = value == "1";
            }
        }
        if (!stream.eof() || !versionValid) {
            error = "功能设置文件格式或版本无效，使用默认关闭状态";
            return false;
        }
        data = std::move(parsed);
        return true;
    }

    // Caller serializes writes. A flushed temporary file replaces the original
    // atomically on the same volume; every failure preserves the original file.
    inline bool Save(const std::filesystem::path& file, const Data& data, std::string& error) {
        error.clear();
        std::string text = "[meta]\nversion=1\n\n[toggles]\n";
        for (const auto& toggle : ToggleCatalog()) {
            const auto value = data.find(toggle.id);
            text += toggle.id + (value != data.end() && value->second ? "=1\n" : "=0\n");
        }
        std::error_code fileError;
        if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), fileError);
        if (fileError) { error = "无法创建功能设置目录，保留原文件"; return false; }
        auto temporary = file;
        temporary += L".tmp";
        const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { error = "无法写入功能设置临时文件，保留原文件"; return false; }
        DWORD written{};
        const bool complete = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
            written == text.size() && FlushFileBuffers(handle);
        const bool closed = CloseHandle(handle) != FALSE;
        if (complete && closed && MoveFileExW(temporary.c_str(), file.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temporary.c_str());
        error = "功能设置保存失败，保留原文件并等待下次保存";
        return false;
    }
}
