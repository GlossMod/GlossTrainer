#pragma once
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gloss::gameplay {
    enum class ItemRichTextKind { InputAction, DynamicValue, InlineImage };

    struct ItemRichTextReference {
        ItemRichTextKind kind{};
        std::string tag;
        std::string id;
        bool operator==(const ItemRichTextReference&) const = default;
    };

    // Labels are resolved by the caller from live game data/current input device.
    // Return a bare label (without brackets), or nullopt when unavailable.
    using ItemRichTextResolver = std::function<std::optional<std::string>(const ItemRichTextReference&)>;

    struct ItemRichTextFormatResult {
        std::string text;
        std::vector<ItemRichTextReference> unresolved;
        bool truncated{};
    };

    inline constexpr std::size_t ItemRichTextMaxBytes = 64 * 1024;
    inline constexpr std::size_t ItemRichTextMaxReferences = 256;

    // References are unique and appear in their first-occurrence order.
    std::vector<ItemRichTextReference> CollectItemRichTextReferences(std::string_view raw);
    // Removes paired style wrappers while retaining their body. Semantic tags
    // become resolved labels or explicit [tag:id] placeholders, never blanks.
    // Repeated input actions remain repeated keycaps, separated by a space.
    ItemRichTextFormatResult FormatItemRichText(std::string_view raw, const ItemRichTextResolver& resolver = {});
}
