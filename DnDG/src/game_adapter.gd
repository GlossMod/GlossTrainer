extends RefCounted
## DnDG 2.0.8.1 integration. No native addresses or original script overrides.

const GAME_VERSION := "2.0.8.1"
const CARD_LIMIT := 24
const CHARM_LIMIT := 7
const RELIC_LIMIT := 6
const MAX_SHIELD := 999
const MAX_CHIPS := 9999

var _catalogs: Dictionary = {}
var _catalog_locale := ""
var _item_icons: Dictionary = {}
var _tooltip_tokens := RegEx.create_from_string(r"\{([a-zA-Z0-9_/]+)\}")
var _tooltip_markup := RegEx.create_from_string(
    r"(?i)\[/?(?:b|i|u|s|code|color|font|font_size|bgcolor|fgcolor|outline_color|outline_size|url|hint|center|left|right|fill|indent)(?:=[^\]]*)?\]")

func _singleton(tree: SceneTree, name: String) -> Node:
    return tree.root.get_node_or_null(NodePath(name))

func _constants(node: Node) -> Dictionary:
    if node == null or not node.get_script() is Script:
        return {}
    return node.get_script().get_script_constant_map()

func _encounter_context(tree: SceneTree) -> Dictionary:
    if str(ProjectSettings.get_setting("application/config/version", "")) != GAME_VERSION:
        return {}
    var run := tree.current_scene
    if not is_instance_valid(run) or run.is_queued_for_deletion() or run.scene_file_path != "res://MacroController.tscn":
        return {}
    var game: Variant = run.get("game_controller")
    if typeof(game) != TYPE_OBJECT or not is_instance_valid(game) or not game is Node:
        return {}
    if game.is_queued_for_deletion() or game.get_parent() != run:
        return {}
    var player: Node = game.get_node_or_null("Player")
    if player == null or player.is_queued_for_deletion() or not player.is_node_ready() or bool(game.get("game_completed")):
        return {}
    var mode := _singleton(tree, "HighStakesUtils")
    if mode == null or mode.is_queued_for_deletion() or bool(mode.call("is_high_stakes")):
        return {"reason": "normal_only"}
    return {"run": run, "game": game, "player": player}

func _context(tree: SceneTree) -> Dictionary:
    var context := _encounter_context(tree)
    if not context.has("player"):
        return context
    var game: Node = context.game
    var player: Node = context.player
    var interaction := _singleton(tree, "InteractionState")
    if interaction == null:
        return {"reason": "normal_only"}
    var ready: bool = (game.get("current_state") == "PlayerAwaitInput"
        and game.get("current_player") == player and interaction.get("current_state") == 0
        and not bool(interaction.get("debouncing")) and int(player.get("hitpoints")) > 0)
    context["ready"] = ready
    context["reason"] = "" if ready else "wait_turn"
    return context

func _card_is_concealed(card: Node) -> bool:
    if bool(card.get("card_facedown")) or bool(card.get("card_shrouded")):
        return true
    # The normal face-down animation displays the shroud before setting its flag.
    var back := card.get_node_or_null("ShroudLiftOnHover/ShroudSprite") as Node2D
    # Face-up animation leaves the sprite visible but collapses its width to zero.
    return back != null and back.is_visible_in_tree() and not is_zero_approx(back.scale.x)

func _card_rows(pile: Array, conceal_all: bool) -> Array:
    var rows: Array = []
    for card: Node in pile:
        if conceal_all or not is_instance_valid(card) or _card_is_concealed(card):
            # Retain the original list index without passing an identity or tooltip lookup key.
            rows.append({"id": "", "name": "", "concealed": true})
        else:
            var row := {"id": str(card.get("card_id")), "name": str(card.call("get_card_name")),
                "token": card.get_instance_id()}
            if bool(card.get("card_locked")):
                row["locked"] = true
            rows.append(row)
    return rows

func _has_active_shroud(game: Node, player: Node) -> bool:
    var other := game.get_node_or_null("Opponent")
    return bool(player.get("moon_shroud_effect_active")) or (
        other != null and bool(other.get("moon_shroud_effect_active")))

func _relic_rows(tree: SceneTree, player: Node) -> Array:
    var list := _singleton(tree, "RelicList")
    var rows: Array = []
    for relic: Node in player.get_node("RelicController").get("relic_array"):
        var id := str(relic.get("this_relic_id"))
        if id not in list.get("chip_relics"):
            continue
        var entry: Dictionary = list.get("relic_dictionary").get(id, {})
        rows.append({"id": id, "name": _name(entry, id), "token": relic.get_instance_id()})
    return rows

func snapshot(tree: SceneTree) -> Dictionary:
    var context := _context(tree)
    if not context.has("player"):
        return {"active": false, "edit_reason": context.get("reason", "no_encounter")}
    var player: Node = context.player
    var charms: Array = []
    for charm: Node in player.get_node("LuckyCharmController").get("lucky_charm_list"):
        charms.append({"id": str(charm.get("id")), "name": TranslationServer.translate(str(charm.get("charm_name"))),
            "token": charm.get_instance_id()})
    var relics := _relic_rows(tree, player)
    var shroud := _has_active_shroud(context.game, player)
    var top := _next_draw_card(player, shroud)
    var next_card := "" if top == null else str(top.call("get_card_name"))
    return {"active": true, "edit_reason": context.reason, "health": player.get("hitpoints"),
        "max_health": player.get("max_hitpoints"), "shield": player.get("shieldpoints"),
        "chips": player.get("chips"), "charm_count": charms.size(), "charms": charms,
        "unique_chips": relics.size(), "relics": relics, "hand": _card_rows(player.get("play_pile"), shroud),
        "sleeve": _card_rows(player.get("sleeve_pile"), shroud), "can_edit_vitals": context.ready,
        "can_edit_items": context.ready, "can_edit_hand": context.ready and not shroud,
        "next_card": next_card}

func _next_draw_card(player: Node, shroud: bool) -> Node:
    if shroud:
        return null
    var draw: Array = player.get("draw_pile")
    if draw.is_empty():
        return null
    var top: Variant = draw[0]
    # The ordinary card back belongs to DeckDisplay, not the card stored in draw_pile.
    if typeof(top) != TYPE_OBJECT or not is_instance_valid(top) or not top is Node:
        return null
    if top.is_queued_for_deletion() or not top.has_method("get_card_name") or _card_is_concealed(top):
        return null
    return top

## Read the real player pile without depending on edit readiness or deck visuals.
func next_card_preview(tree: SceneTree, locale: String = "") -> Dictionary:
    var context := _encounter_context(tree)
    if not context.has("player"):
        return {}
    var player: Node = context.player
    var card := _next_draw_card(player, _has_active_shroud(context.game, player))
    if card == null:
        return {}
    locale = _tooltip_locale(locale)
    var use_live_locale := locale.is_empty() or locale == _tooltip_locale(TranslationServer.get_locale())
    var id := str(card.get("card_id"))
    var name := ""
    if not use_live_locale:
        var message: Variant = card.get("card_name")
        var fields: Variant = card.get("card_name_format")
        if message is String and fields is Dictionary:
            name = _tooltip_format(message, fields, locale)
        if name.is_empty():
            name = _tooltip_reference(tree, "card_" + id, locale)
    if name.is_empty():
        name = str(card.call("get_card_name"))
    if name.strip_edges().is_empty():
        return {}
    var description := ""
    if use_live_locale:
        # Card.get_card_description() dereferences get_parent(); the game's draw
        # pile normally contains detached cards, e.g. after building or reshuffling.
        if card.get_parent() == player and card.has_method("get_card_description"):
            description = _plain_effect_text(tree, str(card.call("get_card_description")), locale)
        if description.is_empty():
            var message: Variant = card.get("card_description")
            var fields: Variant = card.get("card_description_format")
            if message is String and fields is Dictionary:
                description = _plain_effect_text(tree, _tooltip_format(message, fields, locale), locale)
    if description.is_empty():
        description = item_description(tree, "cards", id, locale)
    return {"name": name, "description": description}

func _name(entry: Dictionary, fallback: String) -> String:
    return str(TranslationServer.translate(str(entry.get("name", fallback))))

func catalog(kind: String) -> Array:
    var tree := Engine.get_main_loop() as SceneTree
    if tree == null:
        return []
    var locale := TranslationServer.get_locale()
    if _catalog_locale != locale:
        _catalog_locale = locale
        _catalogs.clear()
    if _catalogs.has(kind):
        return _catalogs[kind]
    var rows: Array = []
    match kind:
        "cards":
            var cards := _singleton(tree, "CardList")
            var ids: Array = _constants(_singleton(tree, "CardGroups")).get("ALL_OBTAINABLE", [])
            var dictionary: Dictionary = _constants(cards).get("CARD_DICTIONARY", {})
            for id: String in ids:
                if dictionary.has(id):
                    rows.append({"id": id, "name": str(cards.call("get_loc_card_name", id))})
        "charms":
            var dictionary: Dictionary = _constants(_singleton(tree, "LuckyCharmList")).get("LUCKY_CHARM_DICTIONARY", {})
            for id: int in dictionary:
                var entry: Dictionary = dictionary[id]
                if _charm_script_exists(entry):
                    rows.append({"id": str(id), "name": _name(entry, str(id))})
        "relics":
            var relics := _singleton(tree, "RelicList")
            if relics != null:
                var dictionary: Dictionary = relics.get("relic_dictionary")
                for id: String in relics.get("chip_relics"):
                    rows.append({"id": id, "name": _name(dictionary[id], id)})
    rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
        var cmp := str(a.name).naturalnocasecmp_to(str(b.name))
        return a.id < b.id if cmp == 0 else cmp < 0)
    if not rows.is_empty():
        _catalogs[kind] = rows
    return rows

func _item_entry(tree: SceneTree, kind: String, id: String) -> Dictionary:
    if id.is_empty() or not id.is_valid_int() or int(id) <= 0:
        return {}
    match kind:
        "cards", "sleeve":
            var cards: Dictionary = _constants(_singleton(tree, "CardList")).get("CARD_DICTIONARY", {})
            return cards.get(id, {})
        "charms":
            var charms: Dictionary = _constants(_singleton(tree, "LuckyCharmList")).get("LUCKY_CHARM_DICTIONARY", {})
            return charms.get(int(id), {})
        "relics":
            var relics := _singleton(tree, "RelicList")
            if relics != null:
                var entries: Dictionary = relics.get("relic_dictionary")
                return entries.get(id, {})
    return {}

## Static catalog art only: no game scene/effect instantiation or live card lookup.
func item_icon(tree: SceneTree, kind: String, id: String) -> Texture2D:
    if _item_entry(tree, kind, id).is_empty():
        return null
    if kind in ["cards", "sleeve"]:
        # Prefer the already-loaded CardArt slot to keep any alternate tarot art choice.
        var card_art := _singleton(tree, "CardArt")
        if card_art != null:
            var textures: Array = card_art.get("card_art_array")
            var offset := int(id) - 1
            if offset < textures.size() and textures[offset] is Texture2D:
                return textures[offset]
        kind = "cards"
    var key := kind + ":" + id
    if _item_icons.has(key):
        return _item_icons[key]
    var icon: Texture2D = null
    match kind:
        "cards":
            icon = _texture("res://assets/art/card_art/%d.png" % int(id))
        "charms":
            icon = _texture("res://assets/art/lucky_charms/%d.png" % int(id))
            if icon == null:
                icon = _texture("res://assets/art/lucky_charms/placeholder.png")
        "relics":
            var path := "res://relics/relic_sprite_frames_%s.tres" % id
            if ResourceLoader.exists(path, "SpriteFrames"):
                var frames := ResourceLoader.load(path, "SpriteFrames") as SpriteFrames
                if frames != null and frames.has_animation(&"default") and frames.get_frame_count(&"default") > 0:
                    icon = frames.get_frame_texture(&"default", 0)
    if icon != null:
        _item_icons[key] = icon
    return icon

func _texture(path: String) -> Texture2D:
    if not ResourceLoader.exists(path, "Texture2D"):
        return null
    return ResourceLoader.load(path, "Texture2D") as Texture2D

func _tooltip_translate(message: String, locale: String) -> String:
    if message.is_empty():
        return ""
    if not locale.is_empty():
        var translation := TranslationServer.get_translation_object(locale)
        if translation != null:
            var text := str(translation.get_message(message))
            if not text.is_empty():
                return text
        var english := TranslationServer.get_translation_object("en")
        if english != null:
            var text := str(english.get_message(message))
            if not text.is_empty():
                return text
        return message
    return str(TranslationServer.translate(message))

func _tooltip_reference(tree: SceneTree, token: String, locale: String) -> String:
    var kind := ""
    var id := ""
    if token.begins_with("card_"):
        kind = "cards"
        id = token.trim_prefix("card_")
    elif token.begins_with("relic_"):
        kind = "relics"
        id = token.trim_prefix("relic_")
    elif token.begins_with("lucky_charm_"):
        kind = "charms"
        id = token.trim_prefix("lucky_charm_")
    var entry := _item_entry(tree, kind, id)
    return _tooltip_format(str(entry.get("name", "")), entry.get("name_format", {}), locale)

func _tooltip_locale(locale: String) -> String:
    # The UI uses zh for simplified Chinese. The game's stored locale is zh_Hans_CN.
    return "zh_Hans_CN" if locale == "zh" else locale

func _tooltip_format(message: String, fields: Dictionary, locale: String) -> String:
    var substitutions: Dictionary = {}
    for field: String in fields:
        substitutions[field] = _tooltip_translate(str(fields[field]), locale)
    return _tooltip_translate(message, locale).format(substitutions)

## Plain localized catalog description. It deliberately excludes encounter-dependent effect text.
func item_description(tree: SceneTree, kind: String, id: String, locale: String = "") -> String:
    var entry := _item_entry(tree, kind, id)
    if entry.is_empty():
        return ""
    locale = _tooltip_locale(locale)
    var hard := kind == "relics" and entry.has("hard_description") and bool(
        _singleton(tree, "RelicList").get("harder_advantage_active"))
    var message := str(entry.get("hard_description" if hard else "description", entry.get("description", "")))
    var fields: Dictionary = entry.get("hard_description_format" if hard else "description_format", {})
    return _plain_effect_text(tree, _tooltip_format(message, fields, locale), locale)

func _plain_effect_text(tree: SceneTree, text: String, locale: String) -> String:
    if text.is_empty():
        return ""
    var substitutions: Dictionary = {}
    var suit_keys := {"spades": "SUIT_SPADES", "clubs": "SUIT_CLUBS", "diamonds": "SUIT_DIAMONDS",
        "hearts": "SUIT_HEARTS", "special": "SUIT_NOTHINGS", "all_suits_at_once": "SUIT_ALL_AT_ONCE"}
    for result: RegExMatch in _tooltip_tokens.search_all(text):
        var token := result.get_string(1)
        if substitutions.has(token):
            continue
        if token.begins_with("keyword_"):
            substitutions[token] = _tooltip_translate(token.to_upper(), locale)
        elif suit_keys.has(token):
            substitutions[token] = _tooltip_translate(suit_keys[token], locale)
        elif token in ["adv", "/adv", "trigger_color", "start_trigger", "end_trigger",
                "start_chip_cost", "end_chip_cost", "start_health_cost", "end_health_cost"]:
            substitutions[token] = ""
        else:
            var reference := _tooltip_reference(tree, token, locale)
            if not reference.is_empty():
                substitutions[token] = reference
    return _tooltip_markup.sub(text.format(substitutions), "", true).strip_edges()

func perform(tree: SceneTree, action: String, value: Variant = null, index: int = -1) -> String:
    var context := _context(tree)
    if not context.get("ready", false):
        return "Wait for your turn in a normal encounter."
    var player: Node = context.player
    if action in ["health", "heal", "shield", "chips"]:
        if action != "heal" and not value is int:
            return "Enter a whole number."
        var id_card := player.get_node("IDCard")
        match action:
            "health", "heal":
                var maximum := int(player.get("max_hitpoints"))
                if maximum <= 0 or (action != "heal" and (value < 1 or value > maximum)):
                    return "Health must be between 1 and your maximum health."
                var health := maximum if action == "heal" else int(value)
                player.set("hitpoints", health)
                id_card.call("set_hitpoints_without_animations", health)
            "shield":
                if value < 0 or value > MAX_SHIELD:
                    return "Shield must be between 0 and 999."
                player.set("shieldpoints", value)
                id_card.call("change_shieldpoints", value)
            "chips":
                if value < 0 or value > MAX_CHIPS:
                    return "Chips must be between 0 and 9,999."
                player.set("chips", value)
                # Set the counter without the game’s _change_chips reward animation: its
                # large-delta branch writes into read-only chip-stack layout defaults.
                id_card.get_node("ChipCounter").call("change_chip_number", value, true)
                for card: Node in player.get("play_pile"):
                    _refresh_card_clickability(tree, card)
        return ""
    var verb := action.get_slice("_", 0)
    var kind := action.get_slice("_", 1)
    if verb not in ["add", "replace", "remove"] or action != verb + "_" + kind:
        return "Unknown trainer action."
    if verb != "remove" and (not value is String or value.is_empty()):
        return "Select an item first."
    match kind:
        "card", "sleeve":
            return _edit_cards(tree, context, kind == "sleeve", verb, str(value), index)
        "charm":
            return _edit_charms(tree, context, verb, str(value), index)
        "relic":
            return _edit_relics(tree, context, verb, str(value), index)
    return "Unknown trainer action."

func _clear_focus(tree: SceneTree, node: Node) -> void:
    var navigation := _singleton(tree, "NavigationController")
    if navigation != null and navigation.call("get_currently_focused_node") == node:
        navigation.call("clear_currently_focused_node")

func _edit_cards(tree: SceneTree, ctx: Dictionary, sleeve: bool, verb: String, id: String, index: int) -> String:
    var player: Node = ctx.player
    var game: Node = ctx.game
    var pile: Array = player.get("sleeve_pile" if sleeve else "play_pile")
    var offsets: Array = player.get("play_pile_pos_offset_array")
    var storage: Array = player.get("deck_storage")
    if not sleeve and offsets.size() != pile.size():
        return "The table pile is being rearranged."
    if verb != "add" and (index < 0 or index >= pile.size() or not is_instance_valid(pile[index])):
        return "The selected card is no longer available."
    if verb == "add" and pile.size() >= CARD_LIMIT:
        return "The trainer card limit for this pile is 24."
    if verb == "remove" and storage.size() == 1 and storage.has(pile[index]):
        return "Cannot remove the last permanent card in the deck."
    var old: Node2D = null if verb == "add" else pile[index]
    if old != null and (old.get("card_locked") or _card_is_concealed(old)):
        return "Locked or hidden cards cannot be edited."
    var other: Node = game.get_node("Opponent")
    if _has_active_shroud(game, player):
        return "Card edits are disabled while a shroud effect is active."
    var card: Node2D = null
    if verb != "remove":
        var group := _constants(_singleton(tree, "CardGroups"))
        var effects: Array = _singleton(tree, "CardList").get("card_effect_scripts")
        if id not in group.get("ALL_OBTAINABLE", []) or int(id) < 1 or int(id) > effects.size():
            return "This card is not available."
        if old != null and str(old.get("card_id")) == id:
            return ""
        card = player.call("instance_new_card", id)
        if card == null:
            return "The game could not create this card."
        player.add_child(card)
        var callback := Callable(game.get_node("PlayerAwaitInput"),
            "_on_SleevePile_Card_clicked" if sleeve else "_on_PlayPile_Card_clicked").bind(card)
        card.connect("card_clicked", callback)
        if sleeve:
            card.call("make_card_grey", int(player.get("advantage")) <= 0)
            if bool(card.get("is_flexible")):
                card.call("set_card_value", card.get("flexible_high_value"))
    if verb == "add":
        pile.append(card)
        storage.append(card)
        if not sleeve:
            offsets.append(Vector2.ZERO)
    else:
        _clear_focus(tree, old)
        old.emit_signal("card_hover_ended", old)
        if verb == "replace":
            var deck_index := storage.find(old)
            if deck_index >= 0:
                storage[deck_index] = card
            pile[index] = card
        else:
            player.call("_remove_card_from_deck", old)
            pile.remove_at(index)
            if not sleeve:
                offsets.remove_at(index)
        old.hide()
        player.remove_child(old)
        old.queue_free()
    if sleeve:
        player.call("resort_sleeve_pile")
        player.call("update_hand_visibility")
    else:
        var spacing := int(player.call("get_play_pile_spacing_with", pile.size()))
        var base: Vector2 = player.get("play_pile_pos")
        for i in pile.size():
            var at: Node2D = pile[i]
            var handler := at.get_node("MovementHandler")
            for tween_name in [&"position_tween", &"y_position_tween"]:
                var tween: Tween = handler.get(tween_name)
                if tween and tween.is_valid():
                    tween.kill()
            at.position = base + Vector2(i * spacing, 0) + offsets[i]
            player.move_child(at, int(player.get("number_non_card_children")) + i)
        player.call("check_and_update_moon_shroud_effect_status")
        other.call("check_and_update_moon_shroud_effect_status")
    game.call("update_scores")
    var await_input := game.get_node("PlayerAwaitInput")
    await_input.call("check_and_set_button_clickability")
    for played: Node in player.get("play_pile"):
        _refresh_card_clickability(tree, played)
    return ""

func _refresh_card_clickability(tree: SceneTree, card: Node) -> void:
    var effect: Variant = _singleton(tree, "Utilities").call("get_effective_card_effect_resource", card)
    var flag: Variant = _constants(_singleton(tree, "CardParameters")).get("CARD_EFFECT_CLICKED_CONDITIONAL")
    if effect != null and flag in effect.get("effects_list") and effect.has_method("conditionally_set_can_be_clicked"):
        effect.call("conditionally_set_can_be_clicked", card)

func _charm_script_exists(entry: Dictionary) -> bool:
    return entry.has("file_name") and ResourceLoader.exists(
        "res://lucky_charm_effect_resources/LuckyCharm%s.gd" % entry.file_name, "GDScript")

func _instantiate(tree: SceneTree, kind: String) -> Node2D:
    var scenes := _singleton(tree, "Scenes")
    var types: Dictionary = _constants(scenes).get("SceneTypes", {})
    if not types.has(kind):
        return null
    var packed: PackedScene = scenes.call("get_packed_scene", types[kind])
    return null if packed == null else packed.instantiate() as Node2D

func _edit_charms(tree: SceneTree, ctx: Dictionary, verb: String, id: String, index: int) -> String:
    var player: Node = ctx.player
    var run: Node = ctx.run
    var game: Node = ctx.game
    var controller := player.get_node("LuckyCharmController")
    var mirror := run.get_node("LuckyCharmController")
    var live: Array = controller.get("lucky_charm_list")
    var saved: Array = run.get("player_lucky_charms")
    var views: Array = mirror.get("lucky_charm_list")
    if saved.size() != live.size() or saved.size() != views.size():
        return "Charms changed during battle. Try again in the next encounter."
    for i in saved.size():
        if live[i].get("id") != saved[i].get("id") or views[i].get("id") != saved[i].get("id"):
            return "Charms changed during battle. Try again in the next encounter."
    if verb != "add" and (index < 0 or index >= live.size()):
        return "The selected charm is no longer available."
    if verb == "add" and live.size() >= CHARM_LIMIT:
        return "The trainer charm limit is seven."
    var charm_id := int(id)
    var info: Dictionary = _constants(_singleton(tree, "LuckyCharmList")).get("LUCKY_CHARM_DICTIONARY", {}).get(charm_id, {})
    if verb != "remove":
        if not id.is_valid_int() or not _charm_script_exists(info):
            return "This charm is not available."
        for i in live.size():
            if live[i].get("id") == charm_id:
                return "" if verb == "replace" and i == index else "This charm is already equipped."
    var target := live.size() if verb == "add" else index
    if verb != "remove":
        var active := _instantiate(tree, "LUCKYCHARM")
        var standby := _instantiate(tree, "LUCKYCHARM")
        if active == null or standby == null:
            if active != null: active.free()
            if standby != null: standby.free()
            return "The game could not create this charm."
        var data := {"id": charm_id, "x_value": info.get("x_value", -1)}
        for pair: Array in [[controller, active], [mirror, standby]]:
            pair[0].add_child(pair[1])
            pair[1].add_to_group(&"top_track")
            pair[1].call("initialise_lucky_charm", data.duplicate(true))
        var effect: Resource = active.get("effect_resource")
        if "clicked_conditional_effect" in effect.get("effects_list"):
            active.connect("lucky_charm_clicked", Callable(game.get_node("PlayerAwaitInput"), "_on_LuckyCharm_clicked").bind(active))
            effect.call("conditionally_set_can_be_clicked", active)
        if verb == "add":
            live.append(active)
            views.append(standby)
            saved.append(data)
        else:
            _retire(tree, live[target])
            _retire(tree, views[target])
            live[target] = active
            views[target] = standby
            saved[target] = data
    else:
        _retire(tree, live.pop_at(target))
        _retire(tree, views.pop_at(target))
        saved.remove_at(target)
    var positions: Array = _constants(_singleton(tree, "SystemParameters")).get("LUCKY_CHARM_PLAYER_POSITIONS", [])
    for pair: Array in [[live, player.get_node("IDCard")], [views, run.get_node("IDCard")]]:
        var nodes: Array = pair[0]
        var badge: Node = pair[1]
        _nav(badge, "bottom", null if nodes.is_empty() else nodes[0])
        for i in nodes.size():
            if i < positions.size(): nodes[i].position = positions[i]
            _nav(nodes[i], "top", badge)
            _nav(nodes[i], "left", badge.get("advantage_counter") if i == (2 if nodes.size() > 2 else 0) else null)
    return ""

func _retire(tree: SceneTree, node: Node2D) -> void:
    _clear_focus(tree, node)
    node.hide()
    node.get_parent().remove_child(node)
    node.queue_free()

func _nav(node: Node, direction: String, target: Node) -> void:
    if node != null:
        var navigation: Variant = node.get("navigation_interpreter")
        if is_instance_valid(navigation):
            navigation.set("neighbour_" + direction, target)

func _edit_relics(tree: SceneTree, ctx: Dictionary, verb: String, id: String, index: int) -> String:
    var run: Node = ctx.run
    var player: Node = ctx.player
    var controller := player.get_node("RelicController")
    var mirror := run.get_node("RelicController")
    var list := _singleton(tree, "RelicList")
    var saved: Array = run.get("player_relics")
    var ids: Array = controller.get("relic_id_list")
    var views: Array = mirror.get("relic_id_list")
    var live: Array = controller.get("relic_array")
    var shadows: Array = mirror.get("relic_array")
    if ids != saved or views != saved or live.size() != ids.size() or shadows.size() != ids.size():
        return "Unique chips changed during battle. Try again in the next encounter."
    for relic: Node in live:
        var effect: Resource = relic.get("relic_effect_resource")
        if effect != null and bool(effect.get("effect_in_progress")):
            return "Wait for the unique chip effect to finish."
    var slots: Array[int] = []
    for i in ids.size():
        if ids[i] in list.get("chip_relics"):
            slots.append(i)
    if verb != "add" and (index < 0 or index >= slots.size()):
        return "The selected unique chip is no longer available."
    var i := ids.size() if verb == "add" else slots[index]
    if verb == "add" and slots.size() >= RELIC_LIMIT:
        return "The trainer unique chip limit is six."
    if verb != "remove":
        if id not in list.get("chip_relics") or not list.get("effect_scripts").has(id):
            return "This unique chip is not available."
        if ids.has(id):
            return "" if verb == "replace" and ids[i] == id else "This unique chip is already equipped."
        var active := _instantiate(tree, "RELIC")
        var standby := _instantiate(tree, "RELIC")
        if active == null or standby == null:
            if active != null: active.free()
            if standby != null: standby.free()
            return "The game could not create this unique chip."
        _prepare_relic(active, controller, id, player)
        _prepare_relic(standby, mirror, id, null)
        if verb == "add":
            live.append(active)
            shadows.append(standby)
            for items: Array in [ids, views, saved]: items.append(id)
        else:
            _retire(tree, live[i])
            _retire(tree, shadows[i])
            live[i] = active
            shadows[i] = standby
            for items: Array in [ids, views, saved]: items[i] = id
    else:
        _retire(tree, live.pop_at(i))
        _retire(tree, shadows.pop_at(i))
        for items: Array in [ids, views, saved]: items.remove_at(i)
    var spacing: Vector2 = _constants(_singleton(tree, "SystemParameters")).get("RELIC_SPACING", Vector2(4, 37))
    for pair: Array in [[live, player.get_node("IDCard")], [shadows, run.get_node("IDCard")]]:
        var nodes: Array = pair[0]
        var badge: Node = pair[1]
        var counter: Node = badge.get("chip_counter")
        var first: Node = null if nodes.is_empty() else nodes[0]
        _nav(counter, "left", first)
        _nav(badge.get("advantage_counter"), "left", first)
        for j in nodes.size():
            var relic: Node2D = nodes[j]
            var offset := _relic_offset(str(relic.get("this_relic_id")))
            relic.position = (spacing * j if j <= 1 else Vector2(-3, spacing.y * j)) + offset
            _nav(relic, "top", counter if j == 0 else nodes[j - 1])
            _nav(relic, "bottom", null if j + 1 >= nodes.size() else nodes[j + 1])
            _nav(relic, "right", counter if j == 0 else badge.get("advantage_counter"))
        if nodes == shadows:
            _nav(badge, "right", first)
    var foe: Node = ctx.game.get_node("Opponent")
    var player_counter: Node = player.get_node("IDCard").get("chip_counter")
    var foe_target: Node = player_counter if live.is_empty() else live[0]
    _nav(foe.get_node("IDCard").get("chip_counter"), "right", foe_target)
    _nav(foe.get_node_or_null("IntentIndicator"), "right", foe_target)
    return ""

func _relic_offset(id: String) -> Vector2:
    match id:
        "013": return Vector2(-7, 3)
        "016": return Vector2(-2, 0)
    return Vector2.ZERO

func _prepare_relic(relic: Node2D, controller: Node, id: String, player: Node) -> void:
    if player != null and id in ["001", "002"]:
        relic.add_to_group(&"view_my_deck")
    else:
        relic.add_to_group(&"no_glyph")
    if controller.is_in_group(&"top_track"):
        relic.add_to_group(&"top_track")
        relic.add_to_group(&"navigatable_node")
    controller.add_child(relic)
    relic.get_node("Area2D").position += _relic_offset(id)
    relic.call("set_relic_art", id)
    if player != null:
        relic.call("set_relic_effect_resource", id)
        var effect: Resource = relic.get("relic_effect_resource")
        var params := _singleton(controller.get_tree(), "RelicParameters")
        if _constants(params).get("RELIC_CLICKED_EFFECT") in effect.get("effects_list"):
            relic.connect("relic_clicked", Callable(controller, "_on_ThisPlayer_relic_clicked").bind(player, relic))
