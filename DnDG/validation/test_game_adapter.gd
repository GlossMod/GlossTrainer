extends SceneTree

const AdapterScript = preload("res://../src/game_adapter.gd")
const FakeEncounter = preload("res://fake_encounter.gd")

class CatalogCards extends Node:
	const CARD_DICTIONARY := {
		"001": {"name": "CARD_NAME_PREFIX_ACE_OF", "name_format": {"suit_name": "SUIT_SPADES"},
			"description": "CARD_EFFECT_ACE"},
		"002": {"description": ""},
		"126": {"description": "CARD_EMPEROR_DESCRIPTION"},
		"999": {"name": "T_FIX_CARD_NAME", "description": "T_FIX_TEST", "description_format": {"count": 7}},
	}

class CatalogCharms extends Node:
	const LUCKY_CHARM_DICTIONARY := {
		1: {"description": "CHARM_LUCKY_PENNY_DESC", "description_format": {
			"advantage_percentage": 51, "advantage_amount": 1, "chip_percentage": 49, "chip_amount": 1}},
	}

class CatalogRelics extends Node:
	var harder_advantage_active := false
	var relic_dictionary := {"001": {
		"description": "RELIC_BARGAINING_CHIP_DESCRIPTION", "description_format": {"chip_number": 10, "advantage_number": 1},
		"hard_description": "RELIC_BARGAINING_CHIP_DESCRIPTION_HARD",
		"hard_description_format": {"chip_number": 21, "advantage_number": 1},
	}}

class CatalogArt extends Node:
	var card_art_array: Array[Texture2D] = [GradientTexture2D.new()]

class FakeDrawDisplay extends Node2D:
	var has_cards := true
	var foresight_active := false
	func has_cards_in_pile() -> bool:
		return has_cards

var passed := 0
var failed := 0
var adapter: RefCounted
var fixture: RefCounted

func _initialize() -> void:
	adapter = AdapterScript.new()
	fixture = FakeEncounter.new()
	fixture.build(self)
	await process_frame
	for test_name: String in [
		"test_context_and_snapshot_are_read_only",
		"test_next_card_ignores_deck_visuals_and_input_phase",
		"test_next_card_rejects_hidden_or_unavailable_cards",
		"test_concealed_card_rows_and_next_draw_are_redacted",
		"test_vitals_accept_valid_values_and_reject_negative_values",
		"test_out_of_turn_actions_are_rejected_without_mutation",
		"test_charm_and_relic_edits_are_disabled_out_of_turn",
		"test_invalid_charm_and_relic_ids_are_rejected_without_mutation",
		"test_table_add_replace_remove_refreshes_clickability",
		"test_sleeve_add_replace_remove",
		"test_catalog_presentation_uses_static_metadata",
	]:
		var result: Variant = await call(test_name)
		if result != true:
			_check(false, "%s completed (runtime error or false return)" % test_name)
	if "--fixture-force-failure" in OS.get_cmdline_user_args():
		failed += 1
		push_error("Forced sentinel failure for runner exit-code verification")
	fixture.cleanup()
	await process_frame
	fixture = null
	adapter = null
	await process_frame
	print("DnDG game_adapter fixture: %d passed, %d failed" % [passed, failed])
	quit(1 if failed > 0 else 0)

func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("FAIL: " + label)

func test_context_and_snapshot_are_read_only() -> bool:
	var context: Dictionary = adapter._context(self)
	_check(context.get("ready", false), "normal encounter context is ready")
	var draw_before: Array = fixture.player.draw_pile.duplicate()
	var card_before: Node = fixture.player.draw_pile[0]
	var snapshot: Dictionary = adapter.snapshot(self)
	_check(snapshot.get("active", false), "snapshot reports active encounter")
	_check(snapshot.get("next_card") == "First draw", "snapshot reads draw_pile[0] name")
	_check(fixture.player.draw_pile == draw_before, "snapshot leaves draw pile unchanged")
	_check(fixture.player.draw_pile[0] == card_before, "snapshot preserves draw_pile[0] identity")
	_check(snapshot.get("can_edit_vitals", false), "snapshot allows edits on player's turn")
	return true

func test_next_card_ignores_deck_visuals_and_input_phase() -> bool:
	var before: Array = fixture.player.draw_pile.duplicate()
	var ordinary := {"name": "First draw", "description": ""}
	_check(before[0].get_parent() == null and adapter.next_card_preview(self) == ordinary,
		"a detached ordinary top keeps its name even without effect text or deck display")
	var display := FakeDrawDisplay.new()
	display.name = "DeckDisplay"
	fixture.player.add_child(display)
	var back := Sprite2D.new()
	back.name = "DeckBack"
	back.add_to_group("deck_sprites")
	display.add_child(back)
	_check(back.is_visible_in_tree() and adapter.next_card_preview(self) == ordinary,
		"the normal deck's visible card back is not an explicit concealment")
	var native := Node2D.new()
	native.name = "ForesightNodes"
	display.add_child(native)
	var eye := Sprite2D.new()
	eye.name = "EyeBG"
	native.add_child(eye)
	_check(eye.is_visible_in_tree() and adapter.next_card_preview(self) == ordinary,
		"a starting native eye does not gate the text preview")
	display.foresight_active = true
	display.has_cards = false
	display.hide()
	_check(adapter.next_card_preview(self) == ordinary and display.foresight_active and not display.visible,
		"text preview does not depend on or change native Foresight and deck visibility")
	fixture.game.current_state = "AfterRoundPhase"
	fixture.game.current_player = fixture.opponent
	fixture.interaction.current_state = 1
	fixture.interaction.debouncing = true
	_check(adapter.next_card_preview(self) == ordinary and not adapter.snapshot(self).can_edit_vitals,
		"resolving phases retain read access without allowing edits")
	fixture.interaction.name = "DetachedInteraction"
	_check(adapter.next_card_preview(self) == ordinary, "no InteractionState singleton is required for reading")
	fixture.interaction.name = "InteractionState"
	fixture.interaction.debouncing = false
	fixture.interaction.current_state = 0
	fixture.game.current_player = fixture.player
	fixture.game.current_state = "PlayerAwaitInput"
	var second := FakeEncounter.FakeCard.new("2", "Second draw")
	fixture.player.draw_pile.push_front(second)
	_check(adapter.next_card_preview(self).get("name") == "Second draw"
		and adapter.snapshot(self).next_card == "Second draw", "preview and compatibility snapshot read the current index zero")
	_check(fixture.player.draw_pile.pop_front() == second and fixture.player.draw_pile == before,
		"polling leaves pile order and object identities unchanged")
	second.free()
	display.free()
	return true

func test_next_card_rejects_hidden_or_unavailable_cards() -> bool:
	var before: Array = fixture.player.draw_pile.duplicate()
	var top: FakeEncounter.FakeCard = before[0]
	for flag: String in ["card_facedown", "card_shrouded"]:
		top.set(flag, true)
		_check(adapter.next_card_preview(self).is_empty() and adapter.snapshot(self).next_card == "",
			"explicit %s redacts even a detached card" % flag)
		top.set(flag, false)
	fixture.player.add_child(top)
	var shroud := Node2D.new()
	shroud.name = "ShroudLiftOnHover"
	var sprite := Sprite2D.new()
	sprite.name = "ShroudSprite"
	shroud.add_child(sprite)
	top.add_child(shroud)
	_check(adapter.next_card_preview(self).is_empty(), "in-progress shroud flip redacts before its flag is set")
	sprite.scale.x = 0.0
	_check(adapter.next_card_preview(self).get("name") == "First draw", "collapsed shroud sprite does not suppress preview")
	shroud.free()
	fixture.player.remove_child(top)
	for owner: Node in [fixture.player, fixture.opponent]:
		owner.set("moon_shroud_effect_active", true)
		_check(adapter.next_card_preview(self).is_empty(), "active %s shroud redacts the preview" % owner.name)
		owner.set("moon_shroud_effect_active", false)
	_check(adapter.next_card_preview(self).get("name") == "First draw", "preview returns after shroud ends")
	fixture.mode.high_stakes = true
	_check(adapter.next_card_preview(self).is_empty(), "non-normal modes are not supported")
	fixture.mode.high_stakes = false
	fixture.game.game_completed = true
	_check(adapter.next_card_preview(self).is_empty(), "a completed encounter is no longer active")
	fixture.game.game_completed = false
	fixture.run.scene_file_path = "res://Other.tscn"
	_check(adapter.next_card_preview(self).is_empty(), "an unrelated current scene cannot provide a preview")
	fixture.run.scene_file_path = "res://MacroController.tscn"
	fixture.player.draw_pile = []
	_check(adapter.next_card_preview(self).is_empty() and adapter.snapshot(self).next_card == "", "empty pile returns no preview")
	fixture.player.draw_pile = [null, top]
	_check(adapter.next_card_preview(self).is_empty(), "invalid top does not expose the following card")
	var retired := FakeEncounter.FakeCard.new("2", "Retired")
	fixture.player.add_child(retired)
	fixture.player.draw_pile = [retired, top]
	retired.queue_free()
	_check(adapter.next_card_preview(self).is_empty() and adapter.snapshot(self).next_card == "", "queued card is rejected immediately")
	await process_frame
	_check(not is_instance_valid(retired) and adapter.next_card_preview(self).is_empty(), "freed top is rejected safely")
	fixture.player.draw_pile = before
	return true

func test_vitals_accept_valid_values_and_reject_negative_values() -> bool:
	_check(adapter.perform(self, "health", 18) == "", "health accepts in-range integer")
	_check(fixture.player.hitpoints == 18 and fixture.player.id_card.last_health == 18,
		"health updates player and ID card")
	_check(adapter.perform(self, "shield", 0) == "", "shield accepts zero")
	_check(fixture.player.shieldpoints == 0 and fixture.player.id_card.last_shield == 0,
		"shield updates player and ID card")
	var chip_refresh_before: int = fixture.utilities.refresh_count
	_check(adapter.perform(self, "chips", 50) == "", "chips accepts nonnegative integer")
	_check(fixture.player.chips == 50 and fixture.player.id_card.get_node("ChipCounter").last_value == 50,
		"chips updates player and counter")
	_check(fixture.utilities.refresh_count - chip_refresh_before == fixture.player.play_pile.size(),
		"chips refetches clickability for every table card")
	_check(adapter.perform(self, "chips", 9999) == "", "large absolute chip assignment accepts the allowed upper bound")
	_check(fixture.player.chips == 9999 and fixture.player.id_card.get_node("ChipCounter").last_value == 9999,
		"large chip assignment updates model and counter directly")
	_check(adapter.perform(self, "heal") == "" and fixture.player.hitpoints == fixture.player.max_hitpoints
		and fixture.player.id_card.last_health == fixture.player.max_hitpoints, "heal commits model and counter")
	_check(adapter.perform(self, "chips", 7.25) == "Enter a whole number.", "fractional chip assignment rejected")
	var health: int = fixture.player.hitpoints
	var shield: int = fixture.player.shieldpoints
	var chips: int = fixture.player.chips
	_check(adapter.perform(self, "health", -1) == "Health must be between 1 and your maximum health.",
		"negative health rejected")
	_check(adapter.perform(self, "shield", -1) == "Shield must be between 0 and 999.",
		"negative shield rejected")
	_check(adapter.perform(self, "chips", -1) == "Chips must be between 0 and 9,999.",
		"negative chips rejected")
	_check(fixture.player.hitpoints == health and fixture.player.shieldpoints == shield and fixture.player.chips == chips,
		"negative vitals leave state unchanged")
	return true

func test_out_of_turn_actions_are_rejected_without_mutation() -> bool:
	fixture.game.current_player = fixture.opponent
	var snapshot: Dictionary = adapter.snapshot(self)
	var old_health: int = fixture.player.hitpoints
	var old_chips: int = fixture.player.chips
	_check(not snapshot.get("can_edit_vitals", true), "out-of-turn snapshot disables vital edits")
	_check(adapter.perform(self, "health", 1) == "Wait for your turn in a normal encounter.",
		"out-of-turn health action rejected")
	_check(adapter.perform(self, "chips", 1) == "Wait for your turn in a normal encounter.",
		"out-of-turn chips action rejected")
	_check(fixture.player.hitpoints == old_health and fixture.player.chips == old_chips,
		"out-of-turn actions leave state unchanged")
	fixture.game.current_player = fixture.player
	return true

func test_charm_and_relic_edits_are_disabled_out_of_turn() -> bool:
	fixture.game.current_player = fixture.opponent
	var snapshot: Dictionary = adapter.snapshot(self)
	var player_charms: Array = fixture.player.get_node("LuckyCharmController").get("lucky_charm_list").duplicate()
	var run_charms: Array = fixture.run.player_lucky_charms.duplicate()
	var player_relics: Array = fixture.player.get_node("RelicController").get("relic_array").duplicate()
	var run_relics: Array = fixture.run.player_relics.duplicate()
	_check(not snapshot.get("can_edit_items", true), "out-of-turn snapshot disables item edits")
	_check(adapter.perform(self, "add_charm", "1") == "Wait for your turn in a normal encounter.",
		"out-of-turn charm edit rejected")
	_check(adapter.perform(self, "replace_charm", "1", 0) == "Wait for your turn in a normal encounter.",
		"out-of-turn charm replacement rejected")
	_check(adapter.perform(self, "remove_charm", null, 0) == "Wait for your turn in a normal encounter.",
		"out-of-turn charm removal rejected")
	_check(adapter.perform(self, "add_relic", "001") == "Wait for your turn in a normal encounter.",
		"out-of-turn unique chip edit rejected")
	_check(adapter.perform(self, "replace_relic", "001", 0) == "Wait for your turn in a normal encounter.",
		"out-of-turn unique chip replacement rejected")
	_check(adapter.perform(self, "remove_relic", null, 0) == "Wait for your turn in a normal encounter.",
		"out-of-turn unique chip removal rejected")
	_check(fixture.player.get_node("LuckyCharmController").get("lucky_charm_list") == player_charms
		and fixture.run.player_lucky_charms == run_charms, "out-of-turn charm arrays unchanged")
	_check(fixture.player.get_node("RelicController").get("relic_array") == player_relics
		and fixture.run.player_relics == run_relics, "out-of-turn unique chip arrays unchanged")
	fixture.game.current_player = fixture.player
	return true

func test_invalid_charm_and_relic_ids_are_rejected_without_mutation() -> bool:
	var player_charms: Array = fixture.player.get_node("LuckyCharmController").get("lucky_charm_list").duplicate()
	var run_charms: Array = fixture.run.player_lucky_charms.duplicate()
	var player_relics: Array = fixture.player.get_node("RelicController").get("relic_array").duplicate()
	var run_relics: Array = fixture.run.player_relics.duplicate()
	_check(adapter.perform(self, "add_charm", "not-a-charm") == "This charm is not available.",
		"invalid charm ID rejected during player's turn")
	_check(adapter.perform(self, "replace_charm", "not-a-charm", 0) == "The selected charm is no longer available.",
		"charm replacement rejects a stale selection")
	_check(adapter.perform(self, "remove_charm", null, 0) == "The selected charm is no longer available.",
		"charm removal rejects a stale selection")
	_check(adapter.perform(self, "add_relic", "not-a-relic") == "This unique chip is not available.",
		"invalid unique chip ID rejected during player's turn")
	_check(adapter.perform(self, "replace_relic", "not-a-relic", 0) == "The selected unique chip is no longer available.",
		"unique chip replacement rejects a stale selection")
	_check(adapter.perform(self, "remove_relic", null, 0) == "The selected unique chip is no longer available.",
		"unique chip removal rejects a stale selection")
	_check(fixture.player.get_node("LuckyCharmController").get("lucky_charm_list") == player_charms
		and fixture.run.player_lucky_charms == run_charms, "rejected charm IDs leave arrays unchanged")
	_check(fixture.player.get_node("RelicController").get("relic_array") == player_relics
		and fixture.run.player_relics == run_relics, "rejected unique chip IDs leave arrays unchanged")
	return true

func test_table_add_replace_remove_refreshes_clickability() -> bool:
	var old_table_count: int = fixture.player.play_pile.size()
	var refresh_before: int = fixture.utilities.refresh_count
	var button_before: int = fixture.await_input.button_refreshes
	_check(adapter.perform(self, "add_card", "3") == "", "table card added")
	_check(fixture.player.play_pile.size() == old_table_count + 1, "table add updates pile")
	_check(fixture.player.play_pile_pos_offset_array.size() == fixture.player.play_pile.size(),
		"table add keeps position offsets aligned")
	_check(fixture.await_input.button_refreshes == button_before + 1, "table add refreshes action buttons")
	_check(fixture.utilities.refresh_count - refresh_before == fixture.player.play_pile.size(),
		"table add refetches clickability for each table card")
	var old_card: Node = fixture.player.play_pile[0]
	refresh_before = fixture.utilities.refresh_count
	_check(adapter.perform(self, "replace_card", "2", 0) == "", "table card replaced")
	_check(fixture.player.play_pile[0].get("card_id") == "2", "table replace updates selected entry")
	_check(not old_card.is_inside_tree(), "replaced table card detached")
	_check(fixture.await_input.button_refreshes == button_before + 2, "table replace refreshes action buttons")
	_check(fixture.utilities.refresh_count - refresh_before == fixture.player.play_pile.size(),
		"table replace refetches clickability for each table card")
	var count_before_remove: int = fixture.player.play_pile.size()
	refresh_before = fixture.utilities.refresh_count
	_check(adapter.perform(self, "remove_card", null, 1) == "", "table card removed")
	_check(fixture.player.play_pile.size() == count_before_remove - 1, "table remove updates pile")
	_check(fixture.player.play_pile_pos_offset_array.size() == fixture.player.play_pile.size(),
		"table remove keeps position offsets aligned")
	_check(fixture.await_input.button_refreshes == button_before + 3, "table remove refreshes action buttons")
	_check(fixture.utilities.refresh_count - refresh_before == fixture.player.play_pile.size(),
		"table remove refetches clickability for each table card")
	return true

func test_sleeve_add_replace_remove() -> bool:
	var old_count: int = fixture.player.sleeve_pile.size()
	_check(adapter.perform(self, "add_sleeve", "1") == "", "sleeve card added")
	_check(fixture.player.sleeve_pile.size() == old_count + 1, "sleeve add updates pile")
	_check(fixture.player.resort_calls == 1 and fixture.player.visibility_calls == 1,
		"sleeve add resorts and refreshes hand visibility")
	var old_card: Node = fixture.player.sleeve_pile[0]
	_check(adapter.perform(self, "replace_sleeve", "3", 0) == "", "sleeve card replaced")
	_check(fixture.player.sleeve_pile[0].get("card_id") == "3", "sleeve replace updates selected entry")
	_check(not old_card.is_inside_tree(), "replaced sleeve card detached")
	_check(fixture.player.resort_calls == 2 and fixture.player.visibility_calls == 2,
		"sleeve replace resorts and refreshes hand visibility")
	_check(adapter.perform(self, "remove_sleeve", null, 1) == "", "sleeve card removed")
	_check(fixture.player.sleeve_pile.size() == old_count, "sleeve remove updates pile")
	_check(fixture.player.resort_calls == 3 and fixture.player.visibility_calls == 3,
		"sleeve remove resorts and refreshes hand visibility")
	return true


func test_concealed_card_rows_and_next_draw_are_redacted() -> bool:
	var facedown: Node = fixture.player.play_pile[0]
	var shrouded: Node = fixture.player.sleeve_pile[1]
	facedown.set("card_facedown", true)
	shrouded.set("card_shrouded", true)
	var snapshot: Dictionary = adapter.snapshot(self)
	var hidden := {"id": "", "name": "", "concealed": true}
	_check(snapshot.hand[0] == hidden and snapshot.sleeve[1] == hidden,
		"face-down and shrouded slots retain their indices without identity or tooltip keys")
	_check(snapshot.hand[1].get("id") == "2" and snapshot.sleeve[0].get("id") == "2",
		"other card slots stay visible at their original indices")
	_check(adapter.perform(self, "replace_card", "2", 0) == "Locked or hidden cards cannot be edited.",
		"a direct action cannot replace the face-down card")
	_check(adapter.perform(self, "remove_sleeve", null, 1) == "Locked or hidden cards cannot be edited.",
		"a direct action cannot remove the shrouded card")
	facedown.set("card_facedown", false)
	shrouded.set("card_shrouded", false)
	facedown.set("card_locked", true)
	var locked: Dictionary = adapter.snapshot(self).hand[0]
	_check(locked.get("locked", false) and locked.get("id") == "1" and locked.get("name") == "Table A",
		"visible locked card stays identifiable and flags row edits as forbidden")
	_check(adapter.perform(self, "remove_card", null, 0) == "Locked or hidden cards cannot be edited.",
		"visible locked card also rejects direct edits")
	facedown.set("card_locked", false)
	var shroud_node := Node2D.new()
	shroud_node.name = "ShroudLiftOnHover"
	var back := Sprite2D.new()
	back.name = "ShroudSprite"
	shroud_node.add_child(back)
	facedown.add_child(shroud_node)
	_check(adapter.snapshot(self).hand[0] == hidden, "visible shroud redacts during flip before flag is set")
	back.scale.x = 0.0
	_check(adapter.snapshot(self).hand[0].get("id") == "1", "zero-width shroud does not redact a revealed card")
	shroud_node.free()
	var draw: Node = fixture.player.draw_pile[0]
	draw.set("card_facedown", true)
	_check(adapter.snapshot(self).next_card == "", "face-down draw card never reaches the preview")
	draw.set("card_facedown", false)
	fixture.opponent.moon_shroud_effect_active = true
	snapshot = adapter.snapshot(self)
	_check(snapshot.next_card == "" and not snapshot.can_edit_hand and snapshot.hand[0] == hidden
		and snapshot.sleeve[0] == hidden, "active shroud redacts card data and suppresses preview/edits")
	fixture.opponent.moon_shroud_effect_active = false
	_check(adapter.snapshot(self).next_card == "First draw", "normal draw preview returns after shroud ends")
	return true

func _catalog_swap(node: Node, name: String, swaps: Array[Node]) -> void:
	var previous := root.get_node_or_null(name)
	if previous != null:
		previous.name = name + "BeforeCatalogTest"
		previous.set_meta("restore_name", name)
		swaps.append(previous)
	node.name = name
	root.add_child(node)
	swaps.append(node)

func test_catalog_presentation_uses_static_metadata() -> bool:
	var swaps: Array[Node] = []
	_catalog_swap(CatalogCards.new(), "CardList", swaps)
	_catalog_swap(CatalogCharms.new(), "LuckyCharmList", swaps)
	var relics := CatalogRelics.new()
	_catalog_swap(relics, "RelicList", swaps)
	var art := CatalogArt.new()
	_catalog_swap(art, "CardArt", swaps)
	var translations: Array[Translation] = []
	var original_locale := TranslationServer.get_locale()
	for locale in ["en", "zh_Hans_CN"]:
		var translated := Translation.new()
		translated.locale = locale
		translated.add_message("T_FIX_TEST", "[b]Count {count} {keyword_burn} {card_001}[/b]"
			if locale == "en" else "[b]{count} 次{keyword_burn}：{card_001}[/b]")
		translated.add_message("T_FIX_CARD_NAME", "Catalog card" if locale == "en" else "目录牌")
		translated.add_message("T_FIX_TOP_NAME", "Living {suit_name}" if locale == "en" else "实例牌·{suit_name}")
		translated.add_message("T_FIX_TOP_LIVE", "[color=#77ffff][i]Live {keyword_burn}[/i][/color]"
			if locale == "en" else "[b]实战烧毁[/b]")
		translated.add_message("KEYWORD_BURN", "Burn" if locale == "en" else "烧毁")
		translated.add_message("CARD_NAME_PREFIX_ACE_OF", "Ace of {suit_name}" if locale == "en" else "{suit_name}A")
		translated.add_message("SUIT_SPADES", "Spades" if locale == "en" else "黑桃")
		TranslationServer.add_translation(translated)
		translations.append(translated)
	TranslationServer.set_locale("en")
	_check(adapter.item_icon(self, "cards", "001") == art.card_art_array[0], "card icon uses the existing CardArt slot")
	_check(adapter.item_icon(self, "sleeve", "001") == art.card_art_array[0], "sleeve uses the same card mapping")
	_check(adapter.item_icon(self, "cards", "../001") == null and adapter.item_icon(self, "other", "001") == null,
		"unknown IDs and kinds do not load resources")
	_check(adapter.item_description(self, "cards", "002", "en") == "" and adapter.item_description(self, "cards", "0", "en") == "",
		"missing descriptions and redacted IDs return blank")
	_check(adapter.item_description(self, "cards", "999", "en") == "Count 7 Burn Ace of Spades",
		"English tooltip resolves number/keyword/reference and removes markup")
	_check(adapter.item_description(self, "cards", "999", "zh") == "7 次烧毁：黑桃A",
		"explicit Chinese locale resolves tooltip and nested display names")
	_check(TranslationServer.get_locale() == "en" and adapter.item_description(self, "cards", "999") == "Count 7 Burn Ace of Spades",
		"tooltip translation does not change game locale")
	var draw_before: Array = fixture.player.draw_pile
	var card := FakeEncounter.FakeCard.new("999", "T_FIX_TOP_NAME")
	card.card_name_format = {"suit_name": "SUIT_SPADES"}
	card.live_description = "T_FIX_TOP_LIVE"
	fixture.player.draw_pile = [card]
	_check(adapter.next_card_preview(self, "en") == {"name": "Living Spades", "description": "Count 7 Burn Ace of Spades"}
		and card.description_reads == 0, "detached card falls back to the catalog without calling the parent-dependent getter")
	card.card_description = "T_FIX_TEST"
	card.card_description_format = {"count": 19}
	_check(adapter.next_card_preview(self).get("description") == "Count 19 Burn Ace of Spades"
		and card.description_reads == 0, "detached card safely formats available instance effect fields")
	fixture.player.add_child(card)
	_check(adapter.next_card_preview(self, "en") == {"name": "Living Spades", "description": "Live Burn"}
		and card.description_reads == 1, "matching locale prefers the live effect, without BBCode or template markers")
	_check(adapter.next_card_preview(self, "zh") == {"name": "实例牌·黑桃", "description": "7 次烧毁：黑桃A"}
		and card.description_reads == 1 and TranslationServer.get_locale() == "en",
		"cross-locale preview localizes the instance name and uses the catalog effect without changing game locale")
	TranslationServer.set_locale("zh_Hans_CN")
	_check(adapter.next_card_preview(self, "zh") == {"name": "实例牌·黑桃", "description": "实战烧毁"}
		and card.description_reads == 2, "the UI's Chinese alias also matches the live game locale")
	TranslationServer.set_locale("en")
	card.card_name = ""
	_check(adapter.next_card_preview(self, "zh").get("name") == "目录牌",
		"missing instance name falls back to the requested catalog locale")
	fixture.player.draw_pile = draw_before
	card.free()
	for translation: Translation in translations:
		TranslationServer.remove_translation(translation)
	TranslationServer.set_locale(original_locale)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--fixture-original-pack="):
			_verify_installed_assets(arg.trim_prefix("--fixture-original-pack="), relics)
	swaps.reverse()
	for node: Node in swaps:
		if node.has_meta("restore_name"):
			node.name = node.get_meta("restore_name")
			node.remove_meta("restore_name")
		else:
			node.free()
	return true

func _verify_installed_assets(pack_path: String, relics: CatalogRelics) -> void:
	if not ProjectSettings.load_resource_pack(pack_path, false):
		_check(false, "original package mounts read-only with no replacement")
		return
	for row: Array in [["cards", "126"], ["charms", "1"], ["relics", "001"]]:
		var icon: Texture2D = adapter.item_icon(self, row[0], row[1])
		_check(icon != null and icon.get_width() > 0 and icon.get_height() > 0,
			"installed game art resolves for %s/%s" % row)
		if icon != null:
			print("Installed %s/%s texture %dx%d" % [row[0], row[1], icon.get_width(), icon.get_height()])
	var translations: Array[Translation] = []
	for locale in ["en", "zh_Hans_CN"]:
		var translation := load("res://localisation/localisation.%s.translation" % locale) as Translation
		_check(translation != null, "installed %s translation resource resolves" % locale)
		if translation != null:
			TranslationServer.add_translation(translation)
			translations.append(translation)
	for row: Array in [["cards", "126"], ["charms", "1"], ["relics", "001"]]:
		var en: String = adapter.item_description(self, row[0], row[1], "en")
		var zh: String = adapter.item_description(self, row[0], row[1], "zh")
		print("Installed %s/%s tooltip: en=%s | zh=%s" % [row[0], row[1], en, zh])
		_check(not en.is_empty() and not zh.is_empty() and en != zh and not en.contains("{") and not zh.contains("{")
			and not en.contains("[color") and not zh.contains("[color"),
			"installed tooltip is fully expanded in both locales for %s/%s" % row)
	var normal: String = adapter.item_description(self, "relics", "001", "en")
	relics.harder_advantage_active = true
	_check(adapter.item_description(self, "relics", "001", "en") != normal, "hard relic tooltip uses the hard variant")
	for translation: Translation in translations:
		TranslationServer.remove_translation(translation)
