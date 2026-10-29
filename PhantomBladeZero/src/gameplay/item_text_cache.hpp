#pragma once
#include <istream>
#include <iomanip>
#include <string>
#include <string_view>
namespace gloss::gameplay {
    template<class Map>
    bool ReadItemTextCache(std::istream& stream, std::string_view hash, Map& result) {
        std::string version;
        if (!std::getline(stream, version) || version != hash) return false;
        Map loaded;
        typename Map::mapped_type value;
        while (stream >> std::ws && stream.peek() != std::char_traits<char>::eof()) {
            if (!(stream >> std::quoted(value.entry.key) >> std::quoted(value.entry.name)
                >> std::quoted(value.entry.category) >> std::quoted(value.raw))) return false;
            if (loaded.size() >= 4096 || value.entry.key.empty() || value.entry.name.empty()) return false;
            value.entry.description = value.raw;
            loaded[value.entry.key] = value;
        }
        if (stream.bad()) return false;
        result = std::move(loaded);
        return true;
    }
}
