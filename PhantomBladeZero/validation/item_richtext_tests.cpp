#include "../src/gameplay/item_richtext.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace gloss::gameplay;
int failures{}, checks{};
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while (0)
int main(int argc, char** argv) {
    const std::string action = "<InputAction id=\"LightAttack\" height=\"26\" RightPadding=\"5\" LeftPadding=\"0\"/>";
    int calls{};
    auto keyboard = [&](const ItemRichTextReference& ref) -> std::optional<std::string> {
        ++calls;
        return ref.kind == ItemRichTextKind::InputAction ? std::optional<std::string>{"鼠标左键"} : std::nullopt;
    };
    const auto repeated = FormatItemRichText("普通连招\n" + action + action + action + action + action, keyboard);
    CHECK(repeated.text == "普通连招\n[鼠标左键] [鼠标左键] [鼠标左键] [鼠标左键] [鼠标左键]");
    CHECK(calls == 1);
    CHECK(repeated.unresolved.empty());
    const auto pad = FormatItemRichText(action + action, [](const auto&) -> std::optional<std::string> { return "方块键"; });
    CHECK(pad.text == "[方块键] [方块键]");
    const auto rebound = FormatItemRichText(action, [](const auto&) -> std::optional<std::string> { return "R"; });
    CHECK(rebound.text == "[R]");
    CHECK(FormatItemRichText("<WeaponDes_Num_Green>1</> 点<br/>继续").text == "1 点\n继续");
    CHECK(FormatItemRichText("<outer><inner>正文</></>").text == "正文");
    CHECK(FormatItemRichText("<dtv id='X032'/>").text == "[dtv:X032]");
    CHECK(FormatItemRichText("<dtv id='X032'/>", [](const auto&) -> std::optional<std::string> { return "20"; }).text == "20");
    CHECK(FormatItemRichText("<img id='Fire'/>", [](const auto&) -> std::optional<std::string> { return "火"; }).text == "[火]");
    CHECK(FormatItemRichText("A &amp; B &#x4e2d; &#25991;").text == "A & B 中 文");
    CHECK(FormatItemRichText("<broken title=unquoted>").text == "<broken title=unquoted>");
    CHECK(FormatItemRichText("<InputAction id='a' id='b'/>").text == "<InputAction id='a' id='b'/>");
    CHECK(CollectItemRichTextReferences(action + action + "<dtv id='x'/>").size() == 2);
    const auto large = FormatItemRichText(std::string(ItemRichTextMaxBytes + 100, 'x'));
    CHECK(large.truncated && large.text.size() == ItemRichTextMaxBytes);
    const auto unknown = FormatItemRichText("<Niagara id='Fire'/>" + action);
    CHECK(unknown.unresolved.size() == 2 && !unknown.text.empty());
    if (argc > 1) {
        std::ifstream corpus(argv[1], std::ios::binary);
        CHECK(corpus.good());
        unsigned records{}, actions{};
        for (std::uint32_t size{}; corpus.read(reinterpret_cast<char*>(&size), 4);) {
            CHECK(size <= ItemRichTextMaxBytes);
            if (size > ItemRichTextMaxBytes) break;
            std::string raw(size, '\0');
            CHECK(static_cast<bool>(corpus.read(raw.data(), size)));
            const auto formatted = FormatItemRichText(raw, [](const ItemRichTextReference& ref) -> std::optional<std::string> {
                return ref.kind == ItemRichTextKind::InputAction ? "LIVE_KEY" : "VALUE";
            });
            CHECK(!formatted.truncated);
            std::size_t expected{}, actual{}, at{};
            while ((at = raw.find("<InputAction ", at)) != raw.npos) { ++expected; ++at; }
            at = 0;
            while ((at = formatted.text.find("[LIVE_KEY]", at)) != formatted.text.npos) { ++actual; ++at; }
            CHECK(expected == actual);
            actions += static_cast<unsigned>(actual); ++records;
        }
        CHECK(records == 269);
        CHECK(actions == 323);
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
