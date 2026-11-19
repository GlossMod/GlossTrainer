#include "game_read.hpp"
#include "game_visibility.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/translation_server.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <algorithm>
#include <limits>
#include <regex>
#include <utility>

namespace gloss::dndg {
namespace {

using namespace godot;

constexpr auto GameVersion = "2.0.8.1";
constexpr auto CharmArt = "res://assets/art/lucky_charms/";
constexpr auto RelicFrames = "res://relics/relic_sprite_frames_";

enum class Item { Card, Relic, Charm };

String Text(const Variant& value) {
    return value.get_type() == Variant::NIL ? String() : value.stringify();
}

std::string Utf8(const String& text) {
    const auto bytes = text.utf8();
    return std::string(bytes.get_data(), static_cast<std::size_t>(bytes.length()));
}

String FromUtf8(std::string_view text) {
    return String::utf8(text.data(), static_cast<int64_t>(text.size()));
}

Array AsArray(const Variant& value) {
    return value.get_type() == Variant::ARRAY ? static_cast<Array>(value) : Array();
}

Dictionary AsDictionary(const Variant& value) {
    return value.get_type() == Variant::DICTIONARY ? static_cast<Dictionary>(value) : Dictionary();
}

Variant Get(Object* object, const StringName& property) {
    return object ? object->get(property) : Variant();
}

template <typename T = Node>
T* AsObject(const Variant& value) {
    if (value.get_type() != Variant::OBJECT)
        return nullptr;
    return Object::cast_to<T>(value.get_validated_object());
}

bool Usable(Node* node) {
    return node && !node->is_queued_for_deletion();
}

Node* Singleton(SceneTree* tree, const NodePath& name) {
    auto* root = tree ? tree->get_root() : nullptr;
    auto* node = root ? root->get_node_or_null(name) : nullptr;
    return Usable(node) ? node : nullptr;
}

Dictionary Constants(Node* node) {
    const Ref<Script> script(Get(node, "script"));
    return script.is_valid() ? script->get_script_constant_map() : Dictionary();
}

bool SupportedVersion() {
    const auto* settings = ProjectSettings::get_singleton();
    return settings && Text(settings->get_setting("application/config/version", "")) == GameVersion;
}

int Vital(Node* player, const StringName& property) {
    const auto value = Get(player, property);
    if (value.get_type() != Variant::INT)
        return 0;
    // Preserve the game's value (including above trainer edit limits), bounded
    // only by the native Snapshot's representation.
    return static_cast<int>(std::clamp(static_cast<int64_t>(value),
        static_cast<int64_t>(std::numeric_limits<int>::min()),
        static_cast<int64_t>(std::numeric_limits<int>::max())));
}

Node* DrawTop(Node* player) {
    const auto pile = AsArray(Get(player, "draw_pile"));
    if (pile.is_empty())
        return nullptr;
    auto* top = AsObject(pile[0]);
    // Index zero is authoritative; never skip an invalid/hidden top to expose a
    // later card or infer contents from the visible deck artwork.
    return Usable(top) && top->has_method("get_card_name") && CardFaceRevealed(top) ? top : nullptr;
}

String LocaleCode(String locale) {
    return locale == "zh" ? String("zh_Hans_CN") : locale;
}

class Reader {
public:
    Reader(SceneTree* tree, Locale locale) : tree_(tree), server_(TranslationServer::get_singleton()) {
        const auto current = server_ ? LocaleCode(server_->get_locale()) : String();
        requested_ = locale == Locale::Chinese ? String("zh_Hans_CN") :
            locale == Locale::English ? String("en") : String();
        locale_ = locale == Locale::Game ?
            (current.to_lower().begins_with("zh") ? Locale::Chinese : Locale::English) : locale;
        liveLocale_ = requested_.is_empty() || requested_ == current;
    }

    Locale DisplayLocale() const { return locale_; }

    Entry MakePreview(Node* card, Node* player) {
        const auto id = Text(card->get("card_id"));
        const auto info = ItemInfo(Item::Card, id);
        const auto name = CardName(card, info, {});
        if (name.strip_edges().is_empty())
            return {};
        String effect;
        if (liveLocale_) {
            // The shipped description getter dereferences get_parent(). The draw
            // pile commonly retains detached cards; read their instance data instead.
            if (card->get_parent() == player && card->has_method("get_card_description"))
                effect = PlainEffect(Text(card->call("get_card_description")));
            if (effect.is_empty())
                effect = PlainEffect(InstanceText(card, "card_description", "card_description_format"));
        }
        if (effect.is_empty())
            effect = Description(Item::Card, info);
        return MakeEntry(Item::Card, id, name, effect, 0, info);
    }

    std::vector<Entry> Cards(const Array& pile, bool concealAll) {
        std::vector<Entry> rows;
        rows.reserve(static_cast<std::size_t>(pile.size()));
        for (int64_t i = 0; i < pile.size(); ++i) {
            auto* card = concealAll ? nullptr : AsObject(pile[i]);
            if (!Usable(card) || !CardFaceRevealed(card) || !card->has_method("get_card_name")) {
                Entry row;
                row.concealed = true;
                rows.push_back(std::move(row));
                continue;
            }
            const auto id = Text(card->get("card_id"));
            const auto info = ItemInfo(Item::Card, id);
            auto row = MakeEntry(Item::Card, id, CardName(card, info, id), Description(Item::Card, info),
                card->get_instance_id(), info);
            row.locked = card->get("card_locked").booleanize();
            rows.push_back(std::move(row));
        }
        return rows;
    }

    std::vector<Entry> Relics(Node* player) {
        const auto chips = AsArray(Get(RelicList(), "chip_relics"));
        const auto items = AsArray(Get(player->get_node_or_null("RelicController"), "relic_array"));
        std::vector<Entry> rows;
        for (int64_t i = 0; i < items.size(); ++i) {
            auto* relic = AsObject(items[i]);
            if (!Usable(relic))
                continue;
            const auto id = Text(relic->get("this_relic_id"));
            if (id.is_empty() || !chips.has(id))
                continue;
            const auto info = ItemInfo(Item::Relic, id);
            rows.push_back(MakeEntry(Item::Relic, id, Name(info, id), Description(Item::Relic, info),
                relic->get_instance_id(), info));
        }
        return rows;
    }

    std::vector<Entry> Charms(Node* player) {
        const auto items = AsArray(Get(player->get_node_or_null("LuckyCharmController"), "lucky_charm_list"));
        std::vector<Entry> rows;
        for (int64_t i = 0; i < items.size(); ++i) {
            auto* charm = AsObject(items[i]);
            if (!Usable(charm))
                continue;
            const auto id = Text(charm->get("id"));
            if (id.is_empty())
                continue;
            const auto info = ItemInfo(Item::Charm, id);
            const auto message = Text(charm->get("charm_name"));
            const auto name = message.is_empty() ? Name(info, id) : Translate(message);
            rows.push_back(MakeEntry(Item::Charm, id, name, Description(Item::Charm, info),
                charm->get_instance_id(), info));
        }
        return rows;
    }

    Catalog ReadCatalog() {
        Catalog catalog;
        const auto ids = AsArray(Constants(Singleton(tree_, "CardGroups")).get("ALL_OBTAINABLE", Variant()));
        auto* cardList = Singleton(tree_, "CardList");
        const bool useGetter = liveLocale_ && cardList && cardList->has_method("get_loc_card_name");
        for (int64_t i = 0; i < ids.size(); ++i) {
            const auto id = Text(ids[i]);
            const auto info = ItemInfo(Item::Card, id);
            if (info.is_empty())
                continue;
            auto name = useGetter ? Text(cardList->call("get_loc_card_name", id)) : String();
            if (name.is_empty())
                name = Name(info, id);
            catalog.cards.push_back(MakeEntry(Item::Card, id, name, Description(Item::Card, info), 0, info));
        }

        const auto charms = Data(Item::Charm).keys();
        for (int64_t i = 0; i < charms.size(); ++i) {
            const auto id = Text(charms[i]);
            const auto info = ItemInfo(Item::Charm, id);
            const auto file = Text(info.get("file_name", Variant()));
            auto* loader = ResourceLoader::get_singleton();
            if (file.is_empty() || !loader ||
                !loader->exists(String("res://lucky_charm_effect_resources/LuckyCharm") + file + ".gd", "GDScript"))
                continue;
            catalog.charms.push_back(MakeEntry(Item::Charm, id, Name(info, id), Description(Item::Charm, info), 0, info));
        }

        const auto chips = AsArray(Get(RelicList(), "chip_relics"));
        for (int64_t i = 0; i < chips.size(); ++i) {
            const auto id = Text(chips[i]);
            const auto info = ItemInfo(Item::Relic, id);
            if (!info.is_empty())
                catalog.uniqueChips.push_back(MakeEntry(Item::Relic, id, Name(info, id), Description(Item::Relic, info), 0, info));
        }
        Sort(catalog.cards);
        Sort(catalog.charms);
        Sort(catalog.uniqueChips);
        return catalog;
    }

private:
    Node* RelicList() {
        if (!relicList_)
            relicList_ = Singleton(tree_, "RelicList");
        return *relicList_;
    }

    const Dictionary& Data(Item item) {
        switch (item) {
        case Item::Card:
            if (!cards_)
                cards_ = AsDictionary(Constants(Singleton(tree_, "CardList")).get("CARD_DICTIONARY", Variant()));
            return *cards_;
        case Item::Charm:
            if (!charms_)
                charms_ = AsDictionary(Constants(Singleton(tree_, "LuckyCharmList")).get("LUCKY_CHARM_DICTIONARY", Variant()));
            return *charms_;
        case Item::Relic:
            if (!relics_)
                relics_ = AsDictionary(Get(RelicList(), "relic_dictionary"));
            return *relics_;
        }
        return Data(Item::Card);
    }

    Dictionary ItemInfo(Item item, const String& id) {
        if (!id.is_valid_int() || id.to_int() <= 0)
            return {};
        const auto key = item == Item::Charm ? Variant(id.to_int()) : Variant(id);
        return AsDictionary(Data(item).get(key, Variant()));
    }

    String Translate(const String& message) {
        if (message.is_empty() || !server_)
            return message;
        const StringName key(message);
        if (requested_.is_empty())
            return String(server_->translate(key));
        if (!translationsChecked_) {
            translation_ = Ref<Resource>(server_->call("get_translation_object", requested_));
            english_ = requested_ == "en" ? translation_ : Ref<Resource>(server_->call("get_translation_object", "en"));
            translationsChecked_ = true;
        }
        if (translation_.is_valid()) {
            const auto value = Text(translation_->call("get_message", key));
            if (!value.is_empty())
                return value;
        }
        if (english_.is_valid() && english_ != translation_) {
            const auto value = Text(english_->call("get_message", key));
            if (!value.is_empty())
                return value;
        }
        return message;
    }

    String Format(const String& message, const Dictionary& fields) {
        const auto translated = Translate(message);
        if (translated.is_empty() || fields.is_empty())
            return translated;
        Dictionary substitutions;
        const auto keys = fields.keys();
        for (int64_t i = 0; i < keys.size(); ++i) {
            const auto field = keys[i];
            substitutions[field] = Translate(Text(fields.get(field, Variant())));
        }
        return translated.format(substitutions);
    }

    String Name(const Dictionary& info, const String& fallback) {
        return Format(Text(info.get("name", fallback)), AsDictionary(info.get("name_format", Variant())));
    }

    String InstanceText(Node* card, const StringName& messageKey, const StringName& formatKey) {
        const auto message = card->get(messageKey);
        const auto fields = card->get(formatKey);
        if (message.get_type() != Variant::STRING || fields.get_type() != Variant::DICTIONARY)
            return {};
        return Format(static_cast<String>(message), static_cast<Dictionary>(fields));
    }

    String CardName(Node* card, const Dictionary& info, const String& fallback) {
        if (!liveLocale_) {
            auto name = InstanceText(card, "card_name", "card_name_format");
            if (name.is_empty())
                name = Name(info, {});
            if (!name.is_empty())
                return name;
        }
        auto name = Text(card->call("get_card_name"));
        return name.is_empty() ? fallback : name;
    }

    String Description(Item item, const Dictionary& info) {
        const bool hard = item == Item::Relic && info.has("hard_description") &&
            Get(RelicList(), "harder_advantage_active").booleanize();
        const auto message = Text(info.get(hard ? "hard_description" : "description", Variant()));
        return PlainEffect(Format(message, AsDictionary(info.get(
            hard ? "hard_description_format" : "description_format", Variant()))));
    }

    String Reference(const String& token) {
        if (token.begins_with("card_"))
            return Name(ItemInfo(Item::Card, token.substr(5)), {});
        if (token.begins_with("relic_"))
            return Name(ItemInfo(Item::Relic, token.substr(6)), {});
        if (token.begins_with("lucky_charm_"))
            return Name(ItemInfo(Item::Charm, token.substr(12)), {});
        return {};
    }

    String PlainEffect(String text) {
        if (text.is_empty())
            return {};
        if (text.contains("{")) {
            static const std::regex tokens(R"(\{([a-zA-Z0-9_/]+)\})");
            const auto utf8 = Utf8(text);
            Dictionary substitutions;
            for (auto it = std::sregex_iterator(utf8.begin(), utf8.end(), tokens); it != std::sregex_iterator(); ++it) {
                const std::string raw = (*it)[1].str();
                const auto token = FromUtf8(raw);
                if (substitutions.has(token))
                    continue;
                if (raw.starts_with("keyword_"))
                    substitutions[token] = Translate(token.to_upper());
                else if (raw == "spades") substitutions[token] = Translate("SUIT_SPADES");
                else if (raw == "clubs") substitutions[token] = Translate("SUIT_CLUBS");
                else if (raw == "diamonds") substitutions[token] = Translate("SUIT_DIAMONDS");
                else if (raw == "hearts") substitutions[token] = Translate("SUIT_HEARTS");
                else if (raw == "special") substitutions[token] = Translate("SUIT_NOTHINGS");
                else if (raw == "all_suits_at_once") substitutions[token] = Translate("SUIT_ALL_AT_ONCE");
                else if (raw == "adv" || raw == "/adv" || raw == "trigger_color" ||
                    raw == "start_trigger" || raw == "end_trigger" || raw == "start_chip_cost" ||
                    raw == "end_chip_cost" || raw == "start_health_cost" || raw == "end_health_cost") {
                    substitutions[token] = String();
                } else if (const auto name = Reference(token); !name.is_empty()) {
                    substitutions[token] = name;
                }
            }
            text = text.format(substitutions);
        }
        if (text.contains("[")) {
            static const std::regex markup(
                R"(\[/?(?:b|i|u|s|code|color|font|font_size|bgcolor|fgcolor|outline_color|outline_size|url|hint|center|left|right|fill|indent)(?:=[^\]]*)?\])",
                std::regex_constants::icase);
            text = FromUtf8(std::regex_replace(Utf8(text), markup, ""));
        }
        return text.strip_edges();
    }

    String IconKey(Item item, const String& id, const Dictionary& info) {
        if (info.is_empty() || !id.is_valid_int() || id.to_int() <= 0)
            return {};
        const auto index = id.to_int();
        if (item == Item::Card) {
            if (!cardArt_)
                cardArt_ = AsArray(Get(Singleton(tree_, "CardArt"), "card_art_array"));
            if (index <= cardArt_->size()) {
                const Ref<Texture2D> texture((*cardArt_)[index - 1]);
                if (texture.is_valid() && texture->get_path().begins_with("res://"))
                    return texture->get_path();
            }
            return String("res://assets/art/card_art/") + String::num_int64(index) + ".png";
        }
        if (item == Item::Charm)
            return String(CharmArt) + String::num_int64(index) + ".png";
        return String(RelicFrames) + id + ".tres";
    }

    Entry MakeEntry(Item item, const String& id, const String& name, const String& effect,
        std::uint64_t token, const Dictionary& info) {
        return {token, Utf8(id), Utf8(name), Utf8(effect), Utf8(IconKey(item, id, info))};
    }

    static void Sort(std::vector<Entry>& rows) {
        std::ranges::sort(rows, [](const Entry& lhs, const Entry& rhs) {
            const int64_t cmp = FromUtf8(lhs.name).naturalnocasecmp_to(FromUtf8(rhs.name));
            return cmp == 0 ? lhs.id < rhs.id : cmp < 0;
        });
    }

    SceneTree* tree_;
    TranslationServer* server_;
    Locale locale_;
    String requested_;
    bool liveLocale_{};
    bool translationsChecked_{};
    Ref<Resource> translation_;
    Ref<Resource> english_;
    std::optional<Node*> relicList_;
    std::optional<Dictionary> cards_;
    std::optional<Dictionary> charms_;
    std::optional<Dictionary> relics_;
    std::optional<Array> cardArt_;
};

} // namespace

godot::Dictionary EncounterContext(godot::SceneTree* tree) {
    using namespace godot;
    Dictionary context;
    if (!tree || !SupportedVersion())
        return context;
    auto* run = tree->get_current_scene();
    if (!Usable(run) || run->get_scene_file_path() != "res://MacroController.tscn")
        return context;
    auto* game = AsObject(run->get("game_controller"));
    if (!Usable(game) || game->get_parent() != run || game->get("game_completed").booleanize())
        return context;
    auto* player = game->get_node_or_null("Player");
    if (!Usable(player) || !player->is_node_ready())
        return context;
    auto* mode = Singleton(tree, "HighStakesUtils");
    if (!mode || !mode->has_method("is_high_stakes")) {
        context["reason"] = "normal_only";
        return context;
    }
    const auto highStakes = mode->call("is_high_stakes");
    if (highStakes.get_type() != Variant::BOOL || highStakes.booleanize()) {
        context["reason"] = "normal_only";
        return context;
    }

    auto* interaction = Singleton(tree, "InteractionState");
    const auto state = Get(interaction, "current_state");
    const bool ready = interaction && Text(game->get("current_state")) == "PlayerAwaitInput" &&
        AsObject(game->get("current_player")) == player && state.get_type() == Variant::INT &&
        static_cast<int64_t>(state) == 0 && !interaction->get("debouncing").booleanize() &&
        Vital(player, "hitpoints") > 0;
    context["run"] = run;
    context["game"] = game;
    context["player"] = player;
    context["ready"] = ready;
    context["reason"] = !interaction ? "normal_only" : ready ? "" : "wait_turn";
    return context;
}

Snapshot ReadGameSnapshot(godot::SceneTree* tree, UiRequest request) {
    using namespace godot;
    Reader reader(tree, request.locale);
    Snapshot snapshot;
    snapshot.locale = reader.DisplayLocale();
    if (!request.panelOpen && !request.showNext)
        return snapshot;

    const auto context = EncounterContext(tree);
    auto* player = AsObject(context.get("player", Variant()));
    auto* game = AsObject(context.get("game", Variant()));
    if (!player || !game)
        return snapshot;
    const bool shroud = !CardsRevealed(player, game->get_node_or_null("Opponent"));
    if (request.panelOpen && Text(context.get("reason", "")) != "normal_only") {
        const bool ready = context.get("ready", false).booleanize();
        snapshot.encounter = game->get_instance_id();
        snapshot.health = Vital(player, "hitpoints");
        snapshot.maxHealth = Vital(player, "max_hitpoints");
        snapshot.shield = Vital(player, "shieldpoints");
        snapshot.chips = Vital(player, "chips");
        snapshot.canEditVitals = ready;
        snapshot.canEditItems = ready;
        snapshot.canEditHand = ready && !shroud;
        snapshot.hand = reader.Cards(AsArray(player->get("play_pile")), shroud);
        snapshot.sleeve = reader.Cards(AsArray(player->get("sleeve_pile")), shroud);
        snapshot.uniqueChips = reader.Relics(player);
        snapshot.charms = reader.Charms(player);
    }
    if (request.showNext && !shroud) {
        if (auto* top = DrawTop(player)) {
            auto entry = reader.MakePreview(top, player);
            if (!entry.name.empty())
                snapshot.nextCard = std::move(entry);
        }
    }
    return snapshot;
}

Catalog ReadGameCatalog(godot::SceneTree* tree, Locale locale) {
    return tree && SupportedVersion() ? Reader(tree, locale).ReadCatalog() : Catalog();
}

godot::Ref<godot::Texture2D> LoadGameIcon(std::string_view iconKey) {
    using namespace godot;
    auto* loader = ResourceLoader::get_singleton();
    if (!loader || !iconKey.starts_with("res://"))
        return {};
    const auto path = FromUtf8(iconKey);
    if (iconKey.starts_with(RelicFrames)) {
        if (!loader->exists(path, "SpriteFrames"))
            return {};
        const auto frames = loader->load(path, "SpriteFrames");
        const StringName animation("default");
        if (frames.is_valid() && frames->is_class("SpriteFrames") &&
            frames->call("has_animation", animation).booleanize() &&
            static_cast<int64_t>(frames->call("get_frame_count", animation)) > 0)
            return Ref<Texture2D>(frames->call("get_frame_texture", animation, 0));
        return {};
    }
    if (loader->exists(path, "Texture2D")) {
        const Ref<Texture2D> texture(loader->load(path, "Texture2D"));
        if (texture.is_valid())
            return texture;
    }
    if (iconKey.starts_with(CharmArt)) {
        const String placeholder = String(CharmArt) + "placeholder.png";
        if (loader->exists(placeholder, "Texture2D"))
            return Ref<Texture2D>(loader->load(placeholder, "Texture2D"));
    }
    return {};
}

} // namespace gloss::dndg
