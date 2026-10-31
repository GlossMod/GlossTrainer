#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
namespace gloss::gameplay {
    class MissingNameCache {
        std::unordered_map<std::string, std::uint64_t> entries;
    public:
        bool Contains(std::string_view name, std::uint64_t revision) const {
            const auto found = entries.find(std::string(name));
            return found != entries.end() && found->second == revision;
        }
        // The pool is append-only, so a later search resumes from this revision.
        std::uint64_t Scanned(std::string_view name) const {
            const auto found = entries.find(std::string(name));
            return found == entries.end() ? 0 : found->second;
        }
        void Remember(std::string_view name, std::uint64_t revision) {
            if (entries.size() >= 1024) entries.clear();
            entries.insert_or_assign(std::string(name), revision);
        }
    };
}
