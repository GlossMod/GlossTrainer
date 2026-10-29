#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace gloss::localization {
    enum class Language { Chinese, English };
    // Explicit paths reload preferences for that file; default initialization is idempotent.
    void Initialize(const std::filesystem::path& file = {});
    bool SetLanguage(Language language, std::string* error = nullptr);
    Language GetLanguage();
    std::string Translate(std::string_view source);
    // Pointers remain valid until the next BeginFrame on the calling thread.
    const char* Text(std::string_view source);
    void BeginFrame();
}
