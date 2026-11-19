#include "game_write.hpp"
#include "game_read.hpp"
#include "game_visibility.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/tween.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace gloss::dndg {
namespace {
using namespace godot;

constexpr int kCardLimit = 24;
constexpr int kRelicLimit = 6;
constexpr int kMaxShield = 999;
constexpr int kMaxChips = 9999;

struct Encounter {
    SceneTree* tree;
    Node* run;
    Node* game;
    Node* player;
    uint64_t runId;
    uint64_t gameId;
    uint64_t playerId;
};

Object* GetObject(const Variant& value) {
    return value.get_type() == Variant::OBJECT ? value.get_validated_object() : nullptr;
}

template<class T = Node>
T* LiveNode(const Variant& value) {
    auto* node = Object::cast_to<T>(GetObject(value));
    return node && !node->is_queued_for_deletion() ? node : nullptr;
}

Node* Child(Node* parent, const char* path) {
    return parent ? LiveNode(parent->get_node_or_null(NodePath(path))) : nullptr;
}

std::optional<Array> ArrayValue(const Variant& value) {
    return value.get_type() == Variant::ARRAY ? std::optional<Array>(static_cast<Array>(value)) : std::nullopt;
}

std::optional<Array> MutableArray(Object* object, const StringName& name, Variant::Type elementType) {
    auto array = object ? ArrayValue(object->get(name)) : std::nullopt;
    if (!array || array->is_read_only())
        return std::nullopt;
    if (array->is_typed()) {
        const auto elementClass = array->get_typed_class_name();
        if (array->get_typed_builtin() != elementType || GetObject(array->get_typed_script()) ||
            (elementClass != StringName() && elementClass != StringName("Object") &&
                elementClass != StringName("Node") && elementClass != StringName("Node2D")))
            return std::nullopt;
    }
    return array;
}

bool IsText(const Variant& value) {
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME;
}

bool IsInteger(const Variant& value) {
    return value.get_type() == Variant::INT;
}

bool IsFalse(const Variant& value) {
    return value.get_type() == Variant::BOOL && !static_cast<bool>(value);
}

Dictionary Constants(Node* node) {
    if (!node)
        return {};
    const Variant scriptValue = node->get_script();
    if (auto* script = Object::cast_to<Script>(GetObject(scriptValue)))
        return script->get_script_constant_map();
    return {};
}

template<class... Args>
bool Call(Object* object, const StringName& method, Variant& result, const Args&... args) {
    if (!object || !object->has_method(method))
        return false;
    Variant receiver(object);
    const std::array<Variant, sizeof...(args)> values{Variant(args)...};
    std::array<const Variant*, sizeof...(args)> pointers{};
    for (size_t i = 0; i < values.size(); ++i)
        pointers[i] = &values[i];
    GDExtensionCallError error{};
    receiver.callp(method, pointers.data(), static_cast<int>(pointers.size()), result, error);
    return error.error == GDEXTENSION_CALL_OK;
}

template<class... Args>
bool Invoke(Object* object, const StringName& method, const Args&... args) {
    Variant unused;
    return Call(object, method, unused, args...);
}

bool Ready(const Dictionary& context, SceneTree* tree, Node* run, Node* game, Node* player) {
    const Variant ready = context.get("ready", false);
    auto* interaction = Child(tree->get_root(), "InteractionState");
    return run && run == tree->get_current_scene() && game && game->get_parent() == run &&
        IsFalse(game->get("game_completed")) && player && Child(game, "Player") == player &&
        player->is_node_ready() && interaction && IsFalse(interaction->get("debouncing")) &&
        ready.get_type() == Variant::BOOL && static_cast<bool>(ready);
}

bool StillReady(const Encounter& ctx) {
    const auto now = EncounterContext(ctx.tree);
    auto* run = LiveNode(now.get("run", Variant()));
    auto* game = LiveNode(now.get("game", Variant()));
    auto* player = LiveNode(now.get("player", Variant()));
    return run && run->get_instance_id() == ctx.runId && game && game->get_instance_id() == ctx.gameId &&
        player && player->get_instance_id() == ctx.playerId && Ready(now, ctx.tree, run, game, player);
}

bool SameArray(Object* owner, const char* property, const Array& array) {
    return UtilityFunctions::is_same(owner->get(property), array);
}

// Resolve an ObjectID *in* the currently-owned live array, not globally and not
// by its position in a previous snapshot.
int64_t FindToken(const Array& items, Node* owner, uint64_t token) {
    for (int64_t i = 0; token && i < items.size(); ++i) {
        auto* node = LiveNode<Node2D>(items[i]);
        if (node && node->get_parent() == owner && node->get_instance_id() == token)
            return i;
    }
    return -1;
}

bool OwnsNodes(const Array& items, Node* owner) {
    for (int64_t i = 0; i < items.size(); ++i) {
        auto* node = LiveNode<Node2D>(items[i]);
        if (!node || node->get_parent() != owner || !node->is_node_ready() || items.find(items[i]) != i)
            return false;
    }
    return true;
}

bool CardEditable(Node2D* card) {
    return CardFaceRevealed(card) && IsFalse(card->get("card_locked"));
}

bool ClearFocus(SceneTree* tree, Node* node) {
    if (!node)
        return false;
    auto* navigation = Child(tree->get_root(), "NavigationController");
    if (!navigation)
        return true;
    Variant focused;
    return Call(navigation, "get_currently_focused_node", focused) &&
        (GetObject(focused) != node || Invoke(navigation, "clear_currently_focused_node"));
}

struct Clickability {
    Node* utilities;
    Variant flag;
};

std::optional<Clickability> ClickabilityContext(SceneTree* tree) {
    auto* utilities = Child(tree->get_root(), "Utilities");
    const Variant flag = Constants(Child(tree->get_root(), "CardParameters"))
        .get("CARD_EFFECT_CLICKED_CONDITIONAL", Variant());
    if (!utilities || !utilities->has_method("get_effective_card_effect_resource") || !IsText(flag))
        return std::nullopt;
    return Clickability{utilities, flag};
}

bool RefreshClickability(const Clickability& ctx, const Array& cards, Node* player) {
    for (int64_t i = 0; i < cards.size(); ++i) {
        auto* card = LiveNode(cards[i]);
        Variant value;
        if (!card || card->get_parent() != player ||
            !Call(ctx.utilities, "get_effective_card_effect_resource", value, card))
            return false;
        auto* effect = GetObject(value);
        if (!effect)
            continue;
        const auto flags = ArrayValue(effect->get("effects_list"));
        if (!flags || (flags->has(ctx.flag) && !Invoke(effect, "conditionally_set_can_be_clicked", card)))
            return false;
    }
    return true;
}

Feedback EditVital(const Encounter& ctx, const Command& cmd) {
    auto* badge = Child(ctx.player, "IDCard");
    if (!badge)
        return Feedback::Unavailable;
    const char* property;
    const char* update;
    int64_t value = cmd.value;
    switch (cmd.edit) {
    case Edit::Health:
    case Edit::Heal: {
        const Variant maximum = ctx.player->get("max_hitpoints");
        if (!IsInteger(maximum) || static_cast<int64_t>(maximum) < 1)
            return Feedback::Unavailable;
        if (cmd.edit == Edit::Heal)
            value = static_cast<int64_t>(maximum);
        if (value < 1 || value > static_cast<int64_t>(maximum))
            return Feedback::Invalid;
        property = "hitpoints";
        update = "set_hitpoints_without_animations";
        break;
    }
    case Edit::Shield:
        if (value < 0 || value > kMaxShield)
            return Feedback::Invalid;
        property = "shieldpoints";
        update = "change_shieldpoints";
        break;
    case Edit::Chips:
        if (value < 0 || value > kMaxChips)
            return Feedback::Invalid;
        badge = Child(badge, "ChipCounter");
        property = "chips";
        update = "change_chip_number";
        break;
    default:
        return Feedback::Invalid;
    }
    const bool chips = cmd.edit == Edit::Chips;
    const auto table = chips ? ArrayValue(ctx.player->get("play_pile")) : std::nullopt;
    const auto clickability = chips ? ClickabilityContext(ctx.tree) : std::nullopt;
    const Variant old = ctx.player->get(property);
    if (!badge || !badge->has_method(update) || !IsInteger(old) ||
        (chips && (!table || !clickability || !OwnsNodes(*table, ctx.player))))
        return Feedback::Unavailable;

    ctx.player->set(property, value);
    // Bypass _change_chips: large deltas in the shipped reward animation try to
    // modify read-only layout dictionaries. The normal direct counter path works.
    if (ctx.player->get(property) != Variant(value) ||
        !(chips ? Invoke(badge, update, value, true) : Invoke(badge, update, value))) {
        ctx.player->set(property, old);
        return Feedback::Unavailable;
    }
    if (chips && !RefreshClickability(*clickability, *table, ctx.player))
        return Feedback::Unavailable;
    return Feedback::Applied;
}

void Retire(Node2D* node) {
    node->hide();
    if (auto* parent = node->get_parent())
        parent->remove_child(node);
    node->queue_free();
}

bool TableLayoutKnown(const Array& table) {
    for (int64_t i = 0; i < table.size(); ++i) {
        auto* handler = Child(LiveNode(table[i]), "MovementHandler");
        if (!handler)
            return false;
        for (const char* tweenName : {"position_tween", "y_position_tween"}) {
            const Variant value = handler->get(tweenName);
            if (value.get_type() != Variant::NIL && value.get_type() != Variant::OBJECT)
                return false;
            if (auto* tween = GetObject(value); tween && !Object::cast_to<Tween>(tween))
                return false;
        }
    }
    return true;
}

Feedback EditCards(const Encounter& ctx, const Command& cmd, const String& id) {
    const bool sleeve = cmd.kind == Kind::Sleeve;
    const bool add = cmd.edit == Edit::Add;
    const bool discard = cmd.edit == Edit::Discard;
    const char* pileName = sleeve ? "sleeve_pile" : "play_pile";
    auto pile = MutableArray(ctx.player, pileName, Variant::OBJECT);
    auto storage = MutableArray(ctx.player, "deck_storage", Variant::OBJECT);
    auto offsets = sleeve ? std::nullopt : MutableArray(ctx.player, "play_pile_pos_offset_array", Variant::VECTOR2);
    const auto table = sleeve ? ArrayValue(ctx.player->get("play_pile")) : pile;
    auto* other = Child(ctx.game, "Opponent");
    auto* awaitInput = Child(ctx.game, "PlayerAwaitInput");
    if (!CardsRevealed(ctx.player, other) || !pile || !storage || !table ||
        !OwnsNodes(*pile, ctx.player) || (sleeve && !OwnsNodes(*table, ctx.player)) ||
        UtilityFunctions::is_same(*pile, *storage) ||
        (sleeve && UtilityFunctions::is_same(*pile, *table)))
        return Feedback::Unavailable;
    if (!sleeve && (!offsets || offsets->size() != pile->size() || !TableLayoutKnown(*pile) ||
        UtilityFunctions::is_same(*offsets, *pile) || UtilityFunctions::is_same(*offsets, *storage)))
        return Feedback::Busy;

    const int64_t index = add ? pile->size() : FindToken(*pile, ctx.player, cmd.token);
    if (index < 0)
        return Feedback::Stale;
    auto* old = add ? nullptr : LiveNode<Node2D>((*pile)[index]);
    if (!add && !CardEditable(old))
        return Feedback::Unavailable;
    if (add && index >= kCardLimit)
        return Feedback::Invalid;
    const int64_t deckIndex = old ? storage->find(old) : -1;
    if (deckIndex >= 0 && storage->rfind(old) != deckIndex)
        return Feedback::Unavailable;
    if (discard && deckIndex >= 0 && storage->size() <= 1)
        return Feedback::Invalid;
    if (discard && !ctx.player->has_method("_remove_card_from_deck"))
        return Feedback::Unavailable;
    if (old && !old->has_signal("card_hover_ended"))
        return Feedback::Unavailable;

    const auto clickability = ClickabilityContext(ctx.tree);
    const Variant advantage = sleeve && !discard ? ctx.player->get("advantage") : Variant();
    if (!awaitInput || !clickability || !ctx.game->has_method("update_scores") ||
        !awaitInput->has_method("check_and_set_button_clickability") ||
        (sleeve && (!ctx.player->has_method("resort_sleeve_pile") ||
            !ctx.player->has_method("update_hand_visibility") || (!discard && !IsInteger(advantage)))))
        return Feedback::Unavailable;

    int64_t spacing = 0;
    int64_t firstChild = 0;
    Vector2 base;
    if (!sleeve) {
        const Variant pos = ctx.player->get("play_pile_pos");
        const Variant boundary = ctx.player->get("number_non_card_children");
        const auto newSize = pile->size() + (add ? 1 : (discard ? -1 : 0));
        if (pos.get_type() != Variant::VECTOR2 || !IsInteger(boundary) ||
            !ctx.player->has_method("check_and_update_moon_shroud_effect_status") ||
            !other->has_method("check_and_update_moon_shroud_effect_status"))
            return Feedback::Unavailable;
        firstChild = static_cast<int64_t>(boundary);
        if (firstChild < 0 || firstChild > ctx.player->get_child_count() - pile->size())
            return Feedback::Unavailable;
        for (int64_t i = 0; i < offsets->size(); ++i) {
            if ((*offsets)[i].get_type() != Variant::VECTOR2)
                return Feedback::Unavailable;
        }
        Variant result;
        if (!Call(ctx.player, "get_play_pile_spacing_with", result, newSize) || !IsInteger(result))
            return Feedback::Unavailable;
        spacing = static_cast<int64_t>(result);
        // The observed engine method returns a small integer pixel spacing.
        if (spacing < std::numeric_limits<int>::min() || spacing > std::numeric_limits<int>::max())
            return Feedback::Unavailable;
        base = pos;
    }

    const char* callback = sleeve ? "_on_SleevePile_Card_clicked" : "_on_PlayPile_Card_clicked";
    Variant fresh;
    Node2D* card = nullptr;
    if (!discard) {
        auto* cardList = Child(ctx.tree->get_root(), "CardList");
        const auto obtainable = ArrayValue(Constants(Child(ctx.tree->get_root(), "CardGroups"))
            .get("ALL_OBTAINABLE", Variant()));
        const auto effects = cardList ? ArrayValue(cardList->get("card_effect_scripts")) : std::nullopt;
        if (!obtainable || !effects)
            return Feedback::Unavailable;
        if (!id.is_valid_int() || !obtainable->has(id) || id.to_int() < 1 || id.to_int() > effects->size())
            return Feedback::Invalid;
        if (old && old->get("card_id") == Variant(id))
            return Feedback::Applied;
        if (!awaitInput->has_method(callback) || !Call(ctx.player, "instance_new_card", fresh, id))
            return Feedback::Unavailable;
        card = LiveNode<Node2D>(fresh);
        if (!card) {
            if (auto* unexpected = LiveNode(fresh); unexpected && !unexpected->get_parent())
                unexpected->queue_free();
            return Feedback::Unavailable;
        }
        if (card->get_parent() || card->is_inside_tree())
            return Feedback::Unavailable;
        if (card->get("card_id") != Variant(id) || !card->has_signal("card_clicked") ||
            (!sleeve && !Child(card, "MovementHandler")) ||
            (sleeve && !card->has_method("make_card_grey"))) {
            Retire(card);
            return Feedback::Unavailable;
        }
        ctx.player->add_child(card);
        if (!card->is_node_ready() || card->get_parent() != ctx.player ||
            card->connect("card_clicked", Callable(awaitInput, callback).bind(card)) != OK ||
            (sleeve && !Invoke(card, "make_card_grey", static_cast<int64_t>(advantage) <= 0))) {
            Retire(card);
            return Feedback::Unavailable;
        }
        const Variant flexible = card->get("is_flexible");
        if (sleeve && (flexible.get_type() != Variant::BOOL ||
            (static_cast<bool>(flexible) && !Invoke(card, "set_card_value", card->get("flexible_high_value"))))) {
            Retire(card);
            return Feedback::Unavailable;
        }
    }

    // Node initialization and hover handlers call back into the game. Recheck
    // the encounter, target, and owning arrays before mutating any pile.
    const auto canCommit = [&] {
        return StillReady(ctx) && CardsRevealed(ctx.player, Child(ctx.game, "Opponent")) &&
            SameArray(ctx.player, pileName, *pile) && SameArray(ctx.player, "deck_storage", *storage) &&
            SameArray(ctx.player, "play_pile", *table) && OwnsNodes(*pile, ctx.player) &&
            (!card || (LiveNode<Node2D>(fresh) == card && card->get_parent() == ctx.player)) &&
            (sleeve || (SameArray(ctx.player, "play_pile_pos_offset_array", *offsets) &&
                pile->size() == offsets->size())) &&
            (add ? pile->size() == index : FindToken(*pile, ctx.player, cmd.token) == index &&
                CardEditable(LiveNode<Node2D>((*pile)[index])) && storage->find(old) == deckIndex &&
                (!discard || deckIndex < 0 || storage->size() > 1));
    };
    if (!canCommit() || (old && (!ClearFocus(ctx.tree, old) ||
        !canCommit() || (old->has_connections("card_hover_ended") &&
            old->emit_signal("card_hover_ended", old) != OK) || !canCommit()))) {
        if (auto* uncommitted = LiveNode<Node2D>(fresh))
            Retire(uncommitted);
        return Feedback::Stale;
    }

    if (add) {
        pile->append(card);
        storage->append(card);
        if (!sleeve)
            offsets->append(Vector2());
    } else if (discard) {
        // Original method also updates White Elephant's stored deck index.
        if (!Invoke(ctx.player, "_remove_card_from_deck", old) || storage->has(old))
            return Feedback::Unavailable;
        pile->remove_at(index);
        if (!sleeve)
            offsets->remove_at(index);
    } else {
        if (deckIndex >= 0)
            (*storage)[deckIndex] = card;
        (*pile)[index] = card;
    }
    if (old)
        Retire(old);

    if (sleeve) {
        if (!Invoke(ctx.player, "resort_sleeve_pile") || !Invoke(ctx.player, "update_hand_visibility"))
            return Feedback::Unavailable;
    } else {
        for (int64_t i = 0; i < pile->size(); ++i) {
            auto* at = LiveNode<Node2D>((*pile)[i]);
            auto* handler = Child(at, "MovementHandler");
            if (!handler)
                return Feedback::Unavailable;
            for (const char* tweenName : {"position_tween", "y_position_tween"}) {
                const Variant value = handler->get(tweenName);
                auto* tween = Object::cast_to<Tween>(GetObject(value));
                if (tween && tween->is_valid())
                    tween->kill();
            }
            at->set_position(base + Vector2(static_cast<real_t>(i * spacing), 0) + static_cast<Vector2>((*offsets)[i]));
            ctx.player->move_child(at, static_cast<int>(firstChild + i));
        }
        if (!Invoke(ctx.player, "check_and_update_moon_shroud_effect_status") ||
            !Invoke(other, "check_and_update_moon_shroud_effect_status"))
            return Feedback::Unavailable;
    }
    if (!Invoke(ctx.game, "update_scores") || !Invoke(awaitInput, "check_and_set_button_clickability") ||
        !RefreshClickability(*clickability, *table, ctx.player))
        return Feedback::Unavailable;
    return Feedback::Applied;
}

struct Equipment {
    Node* controller;
    Node* mirror;
    Array live;
    Array shadows;
    Array saved;
};

std::optional<Equipment> EquipmentContext(const Encounter& ctx, const char* controllerName,
        const char* listName, const char* savedName, Variant::Type savedType) {
    auto* controller = Child(ctx.player, controllerName);
    auto* mirror = Child(ctx.run, controllerName);
    auto live = MutableArray(controller, listName, Variant::OBJECT);
    auto shadows = MutableArray(mirror, listName, Variant::OBJECT);
    auto saved = MutableArray(ctx.run, savedName, savedType);
    if (!live || !shadows || !saved || live->size() != shadows->size() || live->size() != saved->size() ||
        !OwnsNodes(*live, controller) || !OwnsNodes(*shadows, mirror) ||
        UtilityFunctions::is_same(*live, *shadows) || UtilityFunctions::is_same(*live, *saved) ||
        UtilityFunctions::is_same(*shadows, *saved))
        return std::nullopt;
    return Equipment{controller, mirror, *live, *shadows, *saved};
}

Node2D* Instantiate(SceneTree* tree, const String& kind) {
    auto* scenes = Child(tree->get_root(), "Scenes");
    const Variant types = Constants(scenes).get("SceneTypes", Variant());
    if (types.get_type() != Variant::DICTIONARY)
        return nullptr;
    const Variant type = static_cast<Dictionary>(types).get(kind, Variant());
    Variant value;
    if (!IsInteger(type) || !Call(scenes, "get_packed_scene", value, type))
        return nullptr;
    auto* packed = Object::cast_to<PackedScene>(GetObject(value));
    auto* node = packed && packed->can_instantiate() ? packed->instantiate() : nullptr;
    auto* visual = Object::cast_to<Node2D>(node);
    if (node && !visual)
        node->queue_free();
    return visual;
}

struct PreparedPair {
    Variant active;
    Variant standby;
    bool committed{};

    PreparedPair(SceneTree* tree, const String& kind) : active(Instantiate(tree, kind)), standby(Instantiate(tree, kind)) {}
    PreparedPair(const PreparedPair&) = delete;
    PreparedPair& operator=(const PreparedPair&) = delete;
    ~PreparedPair() {
        if (!committed) {
            if (auto* node = LiveNode<Node2D>(active))
                Retire(node);
            if (auto* node = LiveNode<Node2D>(standby))
                Retire(node);
        }
    }
};

void Nav(Node* node, const char* neighbour, Node* target) {
    if (!node)
        return;
    const Variant value = node->get("navigation_interpreter");
    if (auto* interpreter = GetObject(value))
        interpreter->set(neighbour, target);
}

Node* Counter(Node* badge, const char* property) {
    return badge ? LiveNode(badge->get(property)) : nullptr;
}

bool EquipmentUnchanged(const Encounter& ctx, const Equipment& eq, const char* listName,
        const char* savedName, const Command& cmd, int64_t index, const Variant& previous,
        const Variant& oldShadow) {
    const char* controllerName = cmd.kind == Kind::Charm ? "LuckyCharmController" : "RelicController";
    if (!StillReady(ctx) || Child(ctx.player, controllerName) != eq.controller ||
        Child(ctx.run, controllerName) != eq.mirror || !SameArray(eq.controller, listName, eq.live) ||
        !SameArray(eq.mirror, listName, eq.shadows) || !SameArray(ctx.run, savedName, eq.saved) ||
        eq.live.size() != eq.shadows.size() || eq.live.size() != eq.saved.size() ||
        !OwnsNodes(eq.live, eq.controller) || !OwnsNodes(eq.shadows, eq.mirror))
        return false;
    return cmd.edit == Edit::Add ? index == eq.live.size() :
        index == FindToken(eq.live, eq.controller, cmd.token) && eq.saved[index] == previous &&
        eq.shadows[index] == oldShadow && LiveNode<Node2D>(oldShadow);
}

void CommitEquipment(Equipment& eq, Edit edit, int64_t index, const Variant& data, PreparedPair* pair) {
    if (edit == Edit::Add) {
        eq.live.append(pair->active);
        eq.shadows.append(pair->standby);
        eq.saved.append(data);
    } else if (edit == Edit::Discard) {
        eq.live.remove_at(index);
        eq.shadows.remove_at(index);
        eq.saved.remove_at(index);
    } else {
        eq.live[index] = pair->active;
        eq.shadows[index] = pair->standby;
        eq.saved[index] = data;
    }
    if (pair)
        pair->committed = true;
}

Feedback EditCharms(const Encounter& ctx, const Command& cmd, const String& id) {
    constexpr const char* listName = "lucky_charm_list";
    constexpr const char* savedName = "player_lucky_charms";
    auto eq = EquipmentContext(ctx, "LuckyCharmController", listName, savedName, Variant::DICTIONARY);
    if (!eq)
        return Feedback::Unavailable;
    for (int64_t i = 0; i < eq->saved.size(); ++i) {
        if (eq->saved[i].get_type() != Variant::DICTIONARY)
            return Feedback::Unavailable;
        const Variant savedId = static_cast<Dictionary>(eq->saved[i]).get("id", Variant());
        if (!IsInteger(savedId) || eq->saved.find(eq->saved[i]) != i ||
            LiveNode(eq->live[i])->get("id") != savedId || LiveNode(eq->shadows[i])->get("id") != savedId)
            return Feedback::Busy;
    }
    const bool add = cmd.edit == Edit::Add;
    const bool discard = cmd.edit == Edit::Discard;
    const int64_t index = add ? eq->live.size() : FindToken(eq->live, eq->controller, cmd.token);
    if (index < 0)
        return Feedback::Stale;
    auto* liveBadge = Child(ctx.player, "IDCard");
    auto* shadowBadge = Child(ctx.run, "IDCard");
    const auto positions = ArrayValue(Constants(Child(ctx.tree->get_root(), "SystemParameters"))
        .get("LUCKY_CHARM_PLAYER_POSITIONS", Variant()));
    auto* liveAdvantage = Counter(liveBadge, "advantage_counter");
    auto* shadowAdvantage = Counter(shadowBadge, "advantage_counter");
    // LuckyCharmController.gd uses the final configured slot as the anchor for
    // charms beyond the explicit positions.  Treat a missing, empty, or malformed
    // layout as unavailable before preparing or attaching any new nodes.
    if (!positions || positions->is_empty() || !liveAdvantage || !shadowAdvantage)
        return Feedback::Unavailable;
    for (int64_t i = 0; i < positions->size(); ++i) {
        if ((*positions)[i].get_type() != Variant::VECTOR2)
            return Feedback::Unavailable;
    }
    const auto positionFor = [&](int64_t index) {
        if (index < positions->size())
            return static_cast<Vector2>((*positions)[index]);
        const auto overflow = static_cast<real_t>(index + 1 - positions->size());
        return static_cast<Vector2>((*positions)[positions->size() - 1]) + overflow * Vector2(26, 0);
    };

    Dictionary data;
    std::optional<PreparedPair> pair;
    if (!discard) {
        const Variant charmCatalog = Constants(Child(ctx.tree->get_root(), "LuckyCharmList"))
            .get("LUCKY_CHARM_DICTIONARY", Variant());
        if (charmCatalog.get_type() != Variant::DICTIONARY)
            return Feedback::Unavailable;
        const auto charmId = id.to_int();
        if (!id.is_valid_int() || charmId <= 0)
            return Feedback::Invalid;
        const Variant value = static_cast<Dictionary>(charmCatalog).get(charmId, Variant());
        if (value.get_type() != Variant::DICTIONARY)
            return Feedback::Invalid;
        const Dictionary info = value;
        const Variant filename = info.get("file_name", Variant());
        auto* loader = ResourceLoader::get_singleton();
        if (!IsText(filename) || !loader || !loader->exists(String("res://lucky_charm_effect_resources/LuckyCharm") +
                static_cast<String>(filename) + ".gd", "GDScript"))
            return Feedback::Unavailable;
        const Variant x = info.get("x_value", -1);
        if (!IsInteger(x))
            return Feedback::Unavailable;
        for (int64_t i = 0; i < eq->live.size(); ++i) {
            if (LiveNode(eq->live[i])->get("id") == Variant(charmId))
                return !add && i == index ? Feedback::Applied : Feedback::Invalid;
        }
        data["id"] = charmId;
        data["x_value"] = x;
        pair.emplace(ctx.tree, "LUCKYCHARM");
        auto* active = LiveNode<Node2D>(pair->active);
        auto* standby = LiveNode<Node2D>(pair->standby);
        if (!active || !standby || !active->has_method("initialise_lucky_charm") ||
            !standby->has_method("initialise_lucky_charm"))
            return Feedback::Unavailable;
        // LuckyCharmController.gd positions a new charm before add_child() and
        // initialise_lucky_charm(). Keep that ordering so _ready/initialise
        // observers see the same overflow position as the shipped controller.
        const Vector2 initialPosition = positionFor(eq->live.size());
        active->set_position(initialPosition);
        standby->set_position(initialPosition);
        for (auto [parent, charm] : {std::pair{eq->controller, active}, std::pair{eq->mirror, standby}}) {
            parent->add_child(charm);
            if (charm->get_parent() != parent || !charm->is_node_ready())
                return Feedback::Unavailable;
            charm->add_to_group("top_track");
            if (!Invoke(charm, "initialise_lucky_charm", data.duplicate(true)) || charm->get("id") != Variant(charmId))
                return Feedback::Unavailable;
        }
        const Variant effectValue = active->get("effect_resource");
        auto* effect = GetObject(effectValue);
        const auto flags = effect ? ArrayValue(effect->get("effects_list")) : std::nullopt;
        if (!flags)
            return Feedback::Unavailable;
        if (flags->has("clicked_conditional_effect")) {
            auto* await = Child(ctx.game, "PlayerAwaitInput");
            constexpr const char* callback = "_on_LuckyCharm_clicked";
            if (!await || !await->has_method(callback) || !active->has_signal("lucky_charm_clicked") ||
                active->connect("lucky_charm_clicked", Callable(await, callback).bind(active)) != OK ||
                !Invoke(effect, "conditionally_set_can_be_clicked", active))
                return Feedback::Unavailable;
        }
    }

    const Variant prior = add ? Variant() : eq->saved[index];
    const Variant priorLive = add ? Variant() : eq->live[index];
    const Variant priorShadow = add ? Variant() : eq->shadows[index];
    if (!add && (!ClearFocus(ctx.tree, LiveNode(priorLive)) || !ClearFocus(ctx.tree, LiveNode(priorShadow))))
        return Feedback::Unavailable;
    if (!EquipmentUnchanged(ctx, *eq, listName, savedName, cmd, index, prior, priorShadow))
        return Feedback::Stale;
    CommitEquipment(*eq, cmd.edit, index, data, pair ? &*pair : nullptr);
    if (!add) {
        Retire(LiveNode<Node2D>(priorLive));
        Retire(LiveNode<Node2D>(priorShadow));
    }
    const auto layout = [&](const Array& charms, Node* badge, Node* advantage) {
        Nav(badge, "neighbour_bottom", charms.is_empty() ? nullptr : LiveNode(charms[0]));
        for (int64_t i = 0; i < charms.size(); ++i) {
            auto* charm = LiveNode<Node2D>(charms[i]);
            charm->set_position(positionFor(i));
            Nav(charm, "neighbour_top", badge);
            Nav(charm, "neighbour_left", i == (charms.size() > 2 ? 2 : 0) ? advantage : nullptr);
        }
    };
    layout(eq->live, liveBadge, liveAdvantage);
    layout(eq->shadows, shadowBadge, shadowAdvantage);
    return Feedback::Applied;
}

Vector2 RelicOffset(const String& id) {
    if (id == "013")
        return Vector2(-7, 3);
    if (id == "016")
        return Vector2(-2, 0);
    return {};
}

bool RelicInProgress(const Variant& value) {
    auto* node = LiveNode(value);
    if (!node)
        return true;
    const auto effectValue = node->get("relic_effect_resource");
    auto* effect = GetObject(effectValue);
    if (!effect)
        return false;
    const auto busy = effect->get("effect_in_progress");
    return busy.get_type() != Variant::NIL && !IsFalse(busy);
}

bool PrepareRelic(Node2D* relic, Node* controller, const String& id, bool active, const Variant& clickedFlag) {
    if (!relic || !relic->has_method("set_relic_art") ||
        (active && !relic->has_method("set_relic_effect_resource")))
        return false;
    if (active && (id == "001" || id == "002"))
        relic->add_to_group("view_my_deck");
    else
        relic->add_to_group("no_glyph");
    if (controller->is_in_group("top_track")) {
        relic->add_to_group("top_track");
        relic->add_to_group("navigatable_node");
    }
    controller->add_child(relic);
    auto* area = Object::cast_to<Node2D>(Child(relic, "Area2D"));
    if (relic->get_parent() != controller || !relic->is_node_ready() || !area)
        return false;
    area->set_position(area->get_position() + RelicOffset(id));
    if (!Invoke(relic, "set_relic_art", id) || relic->get("this_relic_id") != Variant(id))
        return false;
    if (!active)
        return true;
    if (!Invoke(relic, "set_relic_effect_resource", id))
        return false;
    const auto effectValue = relic->get("relic_effect_resource");
    auto* effect = GetObject(effectValue);
    const auto flags = effect ? ArrayValue(effect->get("effects_list")) : std::nullopt;
    constexpr const char* callback = "_on_ThisPlayer_relic_clicked";
    return flags && (!flags->has(clickedFlag) ||
        (controller->has_method(callback) && relic->has_signal("relic_clicked") &&
            relic->connect("relic_clicked", Callable(controller, callback).bind(controller->get_parent(), relic)) == OK));
}

Feedback EditRelics(const Encounter& ctx, const Command& cmd, const String& id) {
    constexpr const char* listName = "relic_array";
    constexpr const char* savedName = "player_relics";
    constexpr const char* idsName = "relic_id_list";
    auto eq = EquipmentContext(ctx, "RelicController", listName, savedName, Variant::STRING);
    if (!eq)
        return Feedback::Unavailable;
    auto ids = MutableArray(eq->controller, idsName, Variant::STRING);
    auto shadowIds = MutableArray(eq->mirror, idsName, Variant::STRING);
    auto* relicList = Child(ctx.tree->get_root(), "RelicList");
    const auto catalog = relicList ? ArrayValue(relicList->get("chip_relics")) : std::nullopt;
    if (!ids || !shadowIds || !catalog || *ids != eq->saved || *shadowIds != eq->saved)
        return Feedback::Busy;
    int chips = 0;
    for (int64_t i = 0; i < eq->live.size(); ++i) {
        if (!IsText((*ids)[i]) || LiveNode(eq->live[i])->get("this_relic_id") != (*ids)[i] ||
            LiveNode(eq->shadows[i])->get("this_relic_id") != (*ids)[i])
            return Feedback::Busy;
        if (RelicInProgress(eq->live[i]))
            return Feedback::Busy;
        if (catalog->has((*ids)[i]))
            ++chips;
    }
    const bool add = cmd.edit == Edit::Add;
    const bool discard = cmd.edit == Edit::Discard;
    const int64_t index = add ? eq->live.size() : FindToken(eq->live, eq->controller, cmd.token);
    if (index < 0 || (!add && !catalog->has((*ids)[index])))
        return Feedback::Stale;
    if (add && chips >= kRelicLimit)
        return Feedback::Invalid;

    auto* liveBadge = Child(ctx.player, "IDCard");
    auto* shadowBadge = Child(ctx.run, "IDCard");
    auto* liveCounter = Counter(liveBadge, "chip_counter");
    auto* shadowCounter = Counter(shadowBadge, "chip_counter");
    auto* liveAdvantage = Counter(liveBadge, "advantage_counter");
    auto* shadowAdvantage = Counter(shadowBadge, "advantage_counter");
    auto* other = Child(ctx.game, "Opponent");
    auto* foeCounter = Counter(Child(other, "IDCard"), "chip_counter");
    const Variant spacingValue = Constants(Child(ctx.tree->get_root(), "SystemParameters"))
        .get("RELIC_SPACING", Variant());
    if (!liveCounter || !shadowCounter || !liveAdvantage || !shadowAdvantage || !foeCounter ||
        spacingValue.get_type() != Variant::VECTOR2)
        return Feedback::Unavailable;
    const Vector2 spacing = spacingValue;
    const Variant prior = add ? Variant() : eq->saved[index];
    const Variant priorLive = add ? Variant() : eq->live[index];
    const Variant priorShadow = add ? Variant() : eq->shadows[index];
    std::optional<PreparedPair> pair;
    if (!discard) {
        const Variant scriptMap = relicList->get("effect_scripts");
        if (scriptMap.get_type() != Variant::DICTIONARY)
            return Feedback::Unavailable;
        if (!catalog->has(id) || !static_cast<Dictionary>(scriptMap).has(id))
            return Feedback::Invalid;
        if (ids->has(id))
            return !add && prior == Variant(id) ? Feedback::Applied : Feedback::Invalid;
        const Variant clickedFlag = Constants(Child(ctx.tree->get_root(), "RelicParameters"))
            .get("RELIC_CLICKED_EFFECT", Variant());
        if (!IsText(clickedFlag))
            return Feedback::Unavailable;
        pair.emplace(ctx.tree, "RELIC");
        if (!PrepareRelic(LiveNode<Node2D>(pair->active), eq->controller, id, true, clickedFlag) ||
            !PrepareRelic(LiveNode<Node2D>(pair->standby), eq->mirror, id, false, clickedFlag))
            return Feedback::Unavailable;
    }
    if (!add && (!ClearFocus(ctx.tree, LiveNode(priorLive)) || !ClearFocus(ctx.tree, LiveNode(priorShadow))))
        return Feedback::Unavailable;
    if (!EquipmentUnchanged(ctx, *eq, listName, savedName, cmd, index, prior, priorShadow) ||
        !SameArray(eq->controller, idsName, *ids) || !SameArray(eq->mirror, idsName, *shadowIds) ||
        *ids != eq->saved || *shadowIds != eq->saved)
        return Feedback::Stale;
    for (int64_t i = 0; i < eq->live.size(); ++i) {
        if (RelicInProgress(eq->live[i]))
            return Feedback::Busy;
    }
    // The controller lists are normally distinct, but avoid a second mutation
    // if the game has aliased either ID array to the run's backing array.
    if (UtilityFunctions::is_same(*ids, eq->live) || UtilityFunctions::is_same(*ids, eq->shadows) ||
        UtilityFunctions::is_same(*shadowIds, eq->live) || UtilityFunctions::is_same(*shadowIds, eq->shadows))
        return Feedback::Unavailable;
    const auto editIds = [&](Array& array) {
        if (add)
            array.append(id);
        else if (discard)
            array.remove_at(index);
        else
            array[index] = id;
    };
    if (!UtilityFunctions::is_same(*ids, eq->saved))
        editIds(*ids);
    if (!UtilityFunctions::is_same(*shadowIds, eq->saved) && !UtilityFunctions::is_same(*ids, *shadowIds))
        editIds(*shadowIds);
    CommitEquipment(*eq, cmd.edit, index, id, pair ? &*pair : nullptr);
    if (!add) {
        Retire(LiveNode<Node2D>(priorLive));
        Retire(LiveNode<Node2D>(priorShadow));
    }
    const auto layout = [&](const Array& relics, Node* counter, Node* advantage, Node* badge) {
        auto* first = relics.is_empty() ? nullptr : LiveNode(relics[0]);
        Nav(counter, "neighbour_left", first);
        Nav(advantage, "neighbour_left", first);
        for (int64_t i = 0; i < relics.size(); ++i) {
            auto* relic = LiveNode<Node2D>(relics[i]);
            const auto j = static_cast<real_t>(i);
            const auto position = i <= 1 ? spacing * j : Vector2(-3, spacing.y * j);
            relic->set_position(position + RelicOffset(relic->get("this_relic_id")));
            Nav(relic, "neighbour_top", i == 0 ? counter : LiveNode(relics[i - 1]));
            Nav(relic, "neighbour_bottom", i + 1 == relics.size() ? nullptr : LiveNode(relics[i + 1]));
            Nav(relic, "neighbour_right", i == 0 ? counter : advantage);
        }
        Nav(badge, "neighbour_right", first);
        Nav(first, "neighbour_left", badge);
    };
    layout(eq->live, liveCounter, liveAdvantage, nullptr);
    layout(eq->shadows, shadowCounter, shadowAdvantage, shadowBadge);
    auto* foeTarget = eq->live.is_empty() ? liveCounter : LiveNode(eq->live[0]);
    Nav(foeCounter, "neighbour_right", foeTarget);
    Nav(Child(other, "IntentIndicator"), "neighbour_right", foeTarget);
    return Feedback::Applied;
}

} // namespace

Feedback ApplyGameCommand(godot::SceneTree* tree, const Command& command) {
    const auto* os = tree ? godot::OS::get_singleton() : nullptr;
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id())
        return Feedback::Unavailable;
    const auto context = EncounterContext(tree);
    auto* run = LiveNode(context.get("run", Variant()));
    auto* game = LiveNode(context.get("game", Variant()));
    auto* player = LiveNode(context.get("player", Variant()));
    if (!command.encounter || !game || game->get_instance_id() != command.encounter)
        return Feedback::Stale;
    if (!Ready(context, tree, run, game, player))
        return Feedback::Unavailable;
    const Encounter ctx{tree, run, game, player, run->get_instance_id(), command.encounter, player->get_instance_id()};
    switch (command.edit) {
    case Edit::Health:
    case Edit::Heal:
    case Edit::Shield:
    case Edit::Chips:
        return command.token ? Feedback::Invalid : EditVital(ctx, command);
    case Edit::Add:
        if (command.token)
            return Feedback::Invalid;
        break;
    case Edit::Discard:
    case Edit::Replace:
        if (!command.token)
            return Feedback::Stale;
        break;
    default:
        return Feedback::Invalid;
    }
    // Catalog identifiers are small decimal strings (padded for cards/relics).
    // Check the shape before passing externally chosen text to the game.
    if (command.edit != Edit::Discard && (command.id.empty() || command.id.size() > 10 ||
        command.id.find_first_not_of("0123456789") != std::string::npos))
        return Feedback::Invalid;
    const String id = command.edit == Edit::Discard ? String() : String::utf8(command.id.c_str());
    switch (command.kind) {
    case Kind::Sleeve:
    case Kind::Hand:
        return EditCards(ctx, command, id);
    case Kind::UniqueChip:
        return EditRelics(ctx, command, id);
    case Kind::Charm:
        return EditCharms(ctx, command, id);
    default:
        return Feedback::Invalid;
    }
}

} // namespace gloss::dndg
