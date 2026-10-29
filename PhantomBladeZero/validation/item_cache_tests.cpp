#include "../src/gameplay/item_culture.hpp"
#include "../src/gameplay/item_text_cache.hpp"
#include "../src/gameplay/missing_name_cache.hpp"
#include <sstream>
#include <unordered_map>
#include <iostream>
struct Entry { std::string key, name, category, description; };
struct Value { Entry entry; std::string raw; };
int main() {
    int failures{};
    std::unordered_map<std::string, Value> values;
    const auto check = [&](bool value) { if (!value) ++failures; };
    gloss::gameplay::MissingNameCache missing;
    check(!missing.Contains("new_name", 10));
    missing.Remember("new_name", 10);
    check(missing.Contains("new_name", 10));
    check(!missing.Contains("new_name", 11)); // Newly appended names must be searched.
    check(!missing.Contains("other", 10));
    missing.Remember("new_name", 11);
    check(missing.Contains("new_name", 11));
    check(!missing.Contains("new_name", 10));
    const auto culture = [](std::string text) { std::istringstream input(text); return gloss::gameplay::ReadItemCulture(input); };
    check(culture("[Settings]\nCultureName=EN-us\r\n") == "en-us");
    check(culture("CultureName=zh-CN\n") == "zh-cn");
    check(culture("Language=zh-CN\n").empty());
    check(culture("CultureName=../en\n").empty());
    check(culture("CultureName=\n").empty());
    std::stringstream valid;
    valid << "hash\n" << std::quoted("prop:id") << ' ' << std::quoted("name") << ' '
        << std::quoted("category") << ' ' << std::quoted("line1\nline2 \"quoted\"") << '\n';
    check(gloss::gameplay::ReadItemTextCache(valid, "hash", values));
    check(values.size() == 1 && values.at("prop:id").raw == "line1\nline2 \"quoted\"");
    std::istringstream wrong("wrong\n");
    check(!gloss::gameplay::ReadItemTextCache(wrong, "hash", values));
    check(values.size() == 1);
    std::istringstream truncated("hash\n\"key\" \"name\" \"category\" \"unfinished");
    check(!gloss::gameplay::ReadItemTextCache(truncated, "hash", values));
    check(values.size() == 1);
    std::cout << "Item cache: " << failures << " failures\n";
    return failures ? 1 : 0;
}
