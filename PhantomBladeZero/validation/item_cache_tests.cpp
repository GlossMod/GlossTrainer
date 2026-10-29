#include "../src/gameplay/item_text_cache.hpp"
#include <sstream>
#include <unordered_map>
#include <iostream>
struct Entry { std::string key, name, category, description; };
struct Value { Entry entry; std::string raw; };
int main() {
    int failures{};
    std::unordered_map<std::string, Value> values;
    const auto check = [&](bool value) { if (!value) ++failures; };
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
