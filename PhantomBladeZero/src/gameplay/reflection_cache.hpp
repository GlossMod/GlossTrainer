#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gloss::gameplay {
    // Remembers a member lookup on a reflected type, so polling does not walk its
    // whole inheritance chain. Callers revalidate the owner and member on each hit.
    // Misses expire: a type still being linked may gain the member later.
    class ReflectionCache {
        struct Entry {
            std::uintptr_t owner{}, member{};
            std::uint32_t ownerIndex{};
            std::uint64_t missExpiresAt{};
            std::string name;
        };
        std::unordered_map<std::uint64_t, Entry> entries;
        static std::uint64_t KeyOf(std::uintptr_t owner, std::string_view name) {
            return static_cast<std::uint64_t>(owner) * 0x9e3779b97f4a7c15ull ^ std::hash<std::string_view>{}(name);
        }
    public:
        static constexpr std::uint64_t MissLifetimeMs = 1000;
        struct Hit { std::uintptr_t member{}; };
        bool Find(std::uintptr_t owner, std::uint32_t ownerIndex, std::string_view name, std::uint64_t now, Hit& hit) const {
            const auto found = entries.find(KeyOf(owner, name));
            if (found == entries.end()) return false;
            const auto& entry = found->second;
            if (entry.owner != owner || entry.ownerIndex != ownerIndex || entry.name != name) return false;
            if (!entry.member && now >= entry.missExpiresAt) return false;
            hit.member = entry.member;
            return true;
        }
        void Remember(std::uintptr_t owner, std::uint32_t ownerIndex, std::string_view name, std::uintptr_t member, std::uint64_t now) {
            if (entries.size() >= 8192) entries.clear();
            entries.insert_or_assign(KeyOf(owner, name),
                Entry{owner, member, ownerIndex, member ? 0 : now + MissLifetimeMs, std::string(name)});
        }
        void Clear() { entries.clear(); }
    };
}
