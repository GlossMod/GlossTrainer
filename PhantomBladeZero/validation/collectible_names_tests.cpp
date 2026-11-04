#include "../src/gameplay/collectible_names.hpp"
#include <iostream>
#include <limits>

namespace {
    int checks{}, failures{};
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #__VA_ARGS__ "\n"; } } while (false)
}
int main() {
    using namespace gloss::gameplay;
    using Type = CollectibleType;
    CHECK(CollectibleCategory(true, Type::Book) == "书籍");
    CHECK(CollectibleCategory(true, Type::BookPage) == "书页");
    CHECK(CollectibleCategory(true, Type::MemoryFlower) == "记忆花");
    CHECK(CollectibleCategory(true, Type::MemoryFlowerPetal) == "花瓣");
    for (const auto type : {Type::Book, Type::BookPage, Type::MemoryFlower, Type::MemoryFlowerPetal}) {
        CHECK(CollectibleIsUnique(true, type));
        CHECK(!CollectibleIsUnique(false, type));
    }
    for (const auto type : {Type::Item, Type::Letter, Type::Unknown, static_cast<Type>(6)}) {
        CHECK(CollectibleCategory(true, type).empty());
        CHECK(!CollectibleIsUnique(true, type));
        CHECK(!CollectibleFallbackAllowed(true, type, true, Type::Book));
        CHECK(!ComposeCollectibleName(type, "父名", "副标题", 1));
    }
    for (const auto type : {Type::Book, Type::BookPage, Type::MemoryFlower, Type::MemoryFlowerPetal})
        CHECK(CollectibleCategory(false, type).empty());
    CHECK(CollectibleFallbackAllowed(true, Type::BookPage, true, Type::Book));
    CHECK(CollectibleFallbackAllowed(true, Type::MemoryFlowerPetal, true, Type::MemoryFlower));
    CHECK(!CollectibleFallbackAllowed(false, Type::BookPage, true, Type::Book));
    CHECK(!CollectibleFallbackAllowed(true, Type::BookPage, false, Type::Book));
    CHECK(!CollectibleFallbackAllowed(true, Type::BookPage, true, Type::MemoryFlower));
    CHECK(!CollectibleFallbackAllowed(true, Type::MemoryFlowerPetal, true, Type::Book));
    CHECK(ComposeCollectibleName(Type::BookPage, "尘世异闻", "故人", 3) == "尘世异闻 · 故人");
    CHECK(ComposeCollectibleName(Type::BookPage, "尘世异闻", "", 1) == "尘世异闻 · 第1页");
    CHECK(ComposeCollectibleName(Type::MemoryFlowerPetal, "记忆", "风雨", 2) == "记忆 · 风雨");
    CHECK(ComposeCollectibleName(Type::MemoryFlowerPetal, "记忆", "", 6) == "记忆 · 第6片花瓣");
    CHECK(!ComposeCollectibleName(Type::BookPage, "", "副标题", 1));
    CHECK(!ComposeCollectibleName(Type::BookPage, "父名", std::nullopt, 1));
    CHECK(!ComposeCollectibleName(Type::BookPage, "父名", "", 0));
    CHECK(!ComposeCollectibleName(Type::BookPage, "父名", "副标题", -1));
    CHECK(ComposeCollectibleName(Type::BookPage, "父名", "", std::numeric_limits<std::int32_t>::max()) == "父名 · 第2147483647页");
    CHECK(ComposeCollectibleName(Type::BookPage, "父名", "", 1) != ComposeCollectibleName(Type::BookPage, "父名", "", 2));
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
