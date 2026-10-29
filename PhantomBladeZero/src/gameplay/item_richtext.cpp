#include "item_richtext.hpp"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace gloss::gameplay {
    namespace {
        constexpr std::size_t MaxTags = 1024;
        constexpr std::size_t MaxTagBytes = 1024;
        constexpr std::size_t MaxLabelBytes = 1024;

        bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
        bool NameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
        bool NameChar(char c) { return NameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == ':' || c == '.'; }

        std::size_t Utf8Prefix(std::string_view text, std::size_t limit) {
            auto count = std::min(text.size(), limit);
            if (count < text.size()) {
                while (count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80) --count;
            }
            return count;
        }

        void AppendCodepoint(std::string& out, std::uint32_t value) {
            if (value <= 0x7f) out.push_back(static_cast<char>(value));
            else if (value <= 0x7ff) {
                out.push_back(static_cast<char>(0xc0 | (value >> 6)));
                out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            } else if (value <= 0xffff) {
                out.push_back(static_cast<char>(0xe0 | (value >> 12)));
                out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            } else {
                out.push_back(static_cast<char>(0xf0 | (value >> 18)));
                out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (value & 0x3f)));
            }
        }

        std::string DecodeEntities(std::string_view value) {
            std::string out;
            out.reserve(value.size());
            for (std::size_t i = 0; i < value.size();) {
                if (value[i] == '&') {
                    const auto end = value.find(';', i + 1);
                    if (end != std::string_view::npos && end - i <= 12) {
                        const auto entity = value.substr(i + 1, end - i - 1);
                        std::optional<std::uint32_t> cp;
                        if (entity == "amp") cp = '&';
                        else if (entity == "lt") cp = '<';
                        else if (entity == "gt") cp = '>';
                        else if (entity == "quot") cp = '"';
                        else if (entity == "apos") cp = '\'';
                        else if (entity.starts_with('#')) {
                            std::size_t digit = 1;
                            std::uint32_t base = 10, number = 0;
                            if (digit < entity.size() && (entity[digit] == 'x' || entity[digit] == 'X')) { base = 16; ++digit; }
                            const auto first = digit;
                            bool valid = true;
                            for (; digit < entity.size(); ++digit) {
                                const auto c = entity[digit];
                                const auto n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
                                if (static_cast<std::uint32_t>(n) >= base || number > (0x10ffffu - n) / base) { valid = false; break; }
                                number = number * base + n;
                            }
                            if (valid && digit > first && number && number <= 0x10ffff && !(number >= 0xd800 && number <= 0xdfff)) cp = number;
                        }
                        if (cp) { AppendCodepoint(out, *cp); i = end + 1; continue; }
                    }
                }
                out.push_back(value[i++]);
            }
            return out;
        }

        struct Token {
            std::size_t begin{}, size{};
            std::string name, id;
            bool closing{}, selfClosing{}, hasAttributes{}, style{}, lineBreak{};
            std::optional<ItemRichTextReference> reference;
        };

        std::optional<Token> ParseTag(std::string_view raw, std::size_t begin) {
            Token token;
            token.begin = begin;
            auto i = begin + 1;
            const auto limit = begin + std::min(MaxTagBytes, raw.size() - begin);
            if (i < limit && raw[i] == '/') { token.closing = true; ++i; }
            if (i < limit && NameStart(raw[i])) {
                const auto start = i++;
                while (i < limit && NameChar(raw[i])) ++i;
                token.name = raw.substr(start, i - start);
            } else if (!(token.closing && i < limit && raw[i] == '>')) return std::nullopt;
            bool foundId = false;
            while (i < limit) {
                const auto beforeSpace = i;
                while (i < limit && Space(raw[i])) ++i;
                if (i == limit) break;
                if (raw[i] == '>') {
                    token.size = i + 1 - begin;
                    if (token.closing) return token;
                    token.lineBreak = token.name == "br" && !token.hasAttributes;
                    if (token.selfClosing && !token.lineBreak) {
                        const auto kind = token.name == "InputAction" ? ItemRichTextKind::InputAction : token.name == "dtv" ? ItemRichTextKind::DynamicValue : ItemRichTextKind::InlineImage;
                        token.reference = ItemRichTextReference{kind, token.name, token.id};
                    }
                    return token;
                }
                if (!token.closing && raw[i] == '/' && i + 1 < limit && raw[i + 1] == '>') {
                    token.selfClosing = true; ++i; continue;
                }
                if (token.closing || i == beforeSpace || !NameStart(raw[i])) return std::nullopt;
                const auto start = i++;
                while (i < limit && NameChar(raw[i])) ++i;
                const auto attribute = raw.substr(start, i - start);
                while (i < limit && Space(raw[i])) ++i;
                if (i == limit || raw[i++] != '=') return std::nullopt;
                while (i < limit && Space(raw[i])) ++i;
                if (i == limit || (raw[i] != '"' && raw[i] != '\'')) return std::nullopt;
                const auto quote = raw[i++];
                const auto valueStart = i;
                while (i < limit && raw[i] != quote) ++i;
                if (i == limit) return std::nullopt;
                if (attribute == "id") {
                    if (foundId) return std::nullopt;
                    token.id = DecodeEntities(raw.substr(valueStart, i - valueStart));
                    foundId = true;
                }
                ++i;
                token.hasAttributes = true;
            }
            return std::nullopt;
        }

        struct Parsed {
            std::string_view raw;
            std::vector<Token> tokens;
            bool truncated{};
        };

        Parsed Parse(std::string_view raw) {
            Parsed parsed;
            parsed.raw = raw.substr(0, Utf8Prefix(raw, ItemRichTextMaxBytes));
            parsed.truncated = parsed.raw.size() != raw.size();
            std::vector<std::size_t> styles;
            std::size_t cursor = 0, textStart = 0;
            while (cursor < parsed.raw.size()) {
                if (parsed.raw[cursor] != '<') { ++cursor; continue; }
                auto tag = ParseTag(parsed.raw, cursor);
                if (!tag) { ++cursor; continue; }
                if (parsed.tokens.size() >= MaxTags * 2) { parsed.truncated = true; break; }
                if (cursor > textStart) parsed.tokens.push_back(Token{textStart, cursor - textStart});
                const auto index = parsed.tokens.size();
                if (!tag->selfClosing && !tag->lineBreak && !tag->closing) styles.push_back(index);
                else if (tag->closing && !styles.empty()) {
                    auto& opening = parsed.tokens[styles.back()];
                    if (tag->name.empty() || tag->name == opening.name) {
                        opening.style = true; tag->style = true; styles.pop_back();
                    }
                }
                cursor += tag->size;
                textStart = cursor;
                parsed.tokens.push_back(std::move(*tag));
            }
            if (textStart < parsed.raw.size()) parsed.tokens.push_back(Token{textStart, parsed.raw.size() - textStart});
            return parsed;
        }

        void UniquePush(std::vector<ItemRichTextReference>& values, const ItemRichTextReference& value) {
            if (values.size() < ItemRichTextMaxReferences && std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
        }

        void Append(ItemRichTextFormatResult& result, std::string_view value) {
            const auto size = Utf8Prefix(value, ItemRichTextMaxBytes - result.text.size());
            result.text.append(value.substr(0, size));
            result.truncated = result.truncated || size != value.size();
        }
    }

    std::vector<ItemRichTextReference> CollectItemRichTextReferences(std::string_view raw) {
        std::vector<ItemRichTextReference> references;
        for (const auto& token : Parse(raw).tokens) if (token.reference) UniquePush(references, *token.reference);
        return references;
    }

    ItemRichTextFormatResult FormatItemRichText(std::string_view raw, const ItemRichTextResolver& resolver) {
        const auto parsed = Parse(raw);
        ItemRichTextFormatResult result;
        result.truncated = parsed.truncated;
        struct Resolved { ItemRichTextReference reference; std::optional<std::string> value; };
        std::vector<Resolved> resolved;
        bool previousInput = false;
        for (const auto& token : parsed.tokens) {
            if (token.style) continue;
            if (token.lineBreak) { Append(result, "\n"); previousInput = false; continue; }
            if (!token.reference) {
                Append(result, DecodeEntities(parsed.raw.substr(token.begin, token.size)));
                previousInput = false;
                continue;
            }
            const auto& reference = *token.reference;
            auto found = std::find_if(resolved.begin(), resolved.end(), [&](const auto& entry) { return entry.reference == reference; });
            if (found == resolved.end()) {
                if (resolved.size() >= ItemRichTextMaxReferences) { result.truncated = true; Append(result, "[...]"); previousInput = false; continue; }
                auto label = resolver ? resolver(reference) : std::nullopt;
                if (label && label->empty()) label.reset();
                if (label && label->size() > MaxLabelBytes) { label->resize(Utf8Prefix(*label, MaxLabelBytes)); result.truncated = true; }
                resolved.push_back({reference, std::move(label)});
                found = resolved.end() - 1;
            }
            const auto input = reference.kind == ItemRichTextKind::InputAction;
            if (input && previousInput) Append(result, " ");
            if (found->value) {
                if (reference.kind != ItemRichTextKind::DynamicValue) Append(result, "[");
                Append(result, *found->value);
                if (reference.kind != ItemRichTextKind::DynamicValue) Append(result, "]");
            } else {
                UniquePush(result.unresolved, reference);
                Append(result, "["); Append(result, reference.tag);
                if (!reference.id.empty()) { Append(result, ":"); Append(result, reference.id); }
                Append(result, "]");
            }
            previousInput = input;
        }
        return result;
    }
}
