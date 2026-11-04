#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gloss::gameplay {
    enum class CollectibleType : std::uint8_t { Item = 0, Book = 1, BookPage = 2, Letter = 3, MemoryFlower = 4, MemoryFlowerPetal = 5, Unknown = 255 };

    constexpr bool CollectibleIsUnique(bool schemaVerified, CollectibleType type) {
        return schemaVerified && (type == CollectibleType::Book || type == CollectibleType::BookPage ||
            type == CollectibleType::MemoryFlower || type == CollectibleType::MemoryFlowerPetal);
    }

    constexpr std::string_view CollectibleCategory(bool schemaVerified, CollectibleType type) {
        if (!schemaVerified) return {};
        switch (type) {
        case CollectibleType::Book: return "书籍";
        case CollectibleType::BookPage: return "书页";
        case CollectibleType::MemoryFlower: return "记忆花";
        case CollectibleType::MemoryFlowerPetal: return "花瓣";
        default: return {};
        }
    }
    constexpr bool CollectibleFallbackAllowed(bool schemaVerified, CollectibleType type, bool ownNameKeyEmpty, CollectibleType parentType) {
        return schemaVerified && ownNameKeyEmpty &&
            ((type == CollectibleType::BookPage && parentType == CollectibleType::Book) ||
             (type == CollectibleType::MemoryFlowerPetal && parentType == CollectibleType::MemoryFlower));
    }
    // A missing optional subtitle means the localization could not be read.
    // An empty subtitle means its checked key/text was empty and an ordinal is allowed.
    inline std::optional<std::string> ComposeCollectibleName(CollectibleType type, std::string_view parentName,
        std::optional<std::string_view> subhead, std::int32_t sequentialIndex) {
        if ((type != CollectibleType::BookPage && type != CollectibleType::MemoryFlowerPetal) ||
            parentName.empty() || !subhead || sequentialIndex <= 0) return {};
        std::string result(parentName);
        result += " · ";
        if (!subhead->empty()) result += *subhead;
        else {
            result += "第";
            result += std::to_string(sequentialIndex); // The game stores a one-based index.
            result += type == CollectibleType::BookPage ? "页" : "片花瓣";
        }
        return result;
    }
}
