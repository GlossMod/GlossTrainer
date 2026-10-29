#pragma once
#include <istream>
#include <string>
namespace gloss::gameplay {
    inline std::string ReadItemCulture(std::istream& input) {
        std::string line;
        while (std::getline(input, line)) {
            if (!line.starts_with("CultureName=")) continue;
            std::string culture;
            for (unsigned char c : line.substr(12)) {
                if (c == '\r' || c == ' ' || c == '\t') continue;
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return {};
                culture += static_cast<char>(c);
            }
            return !culture.empty() && culture.size() <= 32 ? culture : std::string{};
        }
        return {};
    }
}
