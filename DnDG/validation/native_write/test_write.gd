extends SceneTree

const Fake = preload("res://fake_encounter.gd")
enum {HEALTH, HEAL, SHIELD, CHIPS, ADD, DISCARD, REPLACE}
enum {SLEEVE, HAND, RELIC, CHARM}
enum {NONE, APPLIED, STALE, UNAVAILABLE, INVALID, BUSY}
var fixture = Fake.new()
var probe: Node
var encounter: int
var checks := 0
var failures := 0
class Params extends Node:
	const RELIC_SPACING := Vector2(4, 37)
	const LUCKY_CHARM_PLAYER_POSITIONS := [
		Vector2(100, 98), Vector2(130, 98), Vector2(85, 110),
		Vector2(115, 110), Vector2(145, 110), Vector2(175, 110), Vector2(205, 110)
	]
class EmptyParams extends Node:
	const LUCKY_CHARM_PLAYER_POSITIONS := []
class MalformedParams extends Node:
	const LUCKY_CHARM_PLAYER_POSITIONS := [Vector2(100, 98), 1]
class RelicParams extends Node:
	const RELIC_CLICKED_EFFECT := "clicked_effect"
class Charms extends Node:
	const LUCKY_CHARM_DICTIONARY := {
		1: {"file_name": "One", "x_value": 4}, 2: {"file_name": "Two"},
		3: {"file_name": "One", "x_value": 3}, 4: {"file_name": "One", "x_value": 4},
		5: {"file_name": "One", "x_value": 5}, 6: {"file_name": "One", "x_value": 6},
		7: {"file_name": "One", "x_value": 7}, 8: {"file_name": "One", "x_value": 8},
		9: {"file_name": "One", "x_value": 9}
	}
class Scenes extends Node:
	enum SceneTypes {RELIC, LUCKYCHARM}
	func get_packed_scene(kind: int) -> PackedScene:
		var node: Node2D
		if kind == SceneTypes.RELIC:
			node = preload("res://fake_relic.gd").new()
			var area := Area2D.new()
			area.name = "Area2D"
			node.add_child(area)
			area.owner = node
		else:
			node = preload("res://fake_charm.gd").new()
		var scene := PackedScene.new()
		scene.pack(node)
		node.free()
		return scene

func expect(result: bool, message: String) -> void:
	checks += 1
	if not result:
		failures += 1
		push_error("FAIL: " + message)

func edit(verb: int, kind: int = HAND, token: int = 0, value: int = 0, id: String = "") -> int:
	return probe.edit(verb, kind, encounter, token, value, id)

func add_named(parent: Node, name: String, node: Node) -> void:
	node.name = name
	parent.add_child(node)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if not ClassDB.class_exists("NativeWriteProbe"):
		push_error("native extension missing")
		quit(2)
		return
	fixture.build(self)
	probe = ClassDB.instantiate("NativeWriteProbe")
	root.add_child(probe)
	encounter = fixture.game.get_instance_id()
	var p = fixture.player
	var g = fixture.game
	var foe = fixture.opponent
	var ic = p.id_card
	expect(probe.edit(HEALTH, HAND, encounter + 1, 0, 1, "") == STALE and p.hitpoints == 20, "wrong encounter rejects without write")
	g.current_player = foe
	expect(edit(CHIPS, HAND, 0, 100) == UNAVAILABLE and p.chips == 25, "out of turn rejects")
	g.current_player = p
	fixture.interaction.debouncing = true
	expect(edit(SHIELD, HAND, 0, 10) == UNAVAILABLE and p.shieldpoints == 4, "debounce rejects")
	fixture.interaction.debouncing = false
	expect(edit(HEALTH, HAND, 0, 0) == INVALID and edit(HEALTH, HAND, 0, 31) == INVALID, "health ranges")
	expect(edit(HEALTH, HAND, 0, 18) == APPLIED and p.hitpoints == 18 and ic.last_health == 18, "health and badge")
	expect(edit(HEAL) == APPLIED and p.hitpoints == 30 and ic.last_health == 30, "heal and badge")
	expect(edit(SHIELD, HAND, 0, 0) == APPLIED and p.shieldpoints == 0 and ic.last_shield == 0, "shield and badge")
	expect(edit(SHIELD, HAND, 0, 1000) == INVALID and p.shieldpoints == 0, "shield range")
	expect(edit(CHIPS, HAND, 0, 10000) == INVALID and p.chips == 25, "chip range")
	expect(edit(CHIPS, HAND, 0, 9999) == APPLIED and p.chips == 9999 and ic.get_node("ChipCounter").last_value == 9999, "chip counter at upper bound")
	expect(fixture.utilities.refresh_count == 2, "chip clickability refreshes two cards")
	var first = p.play_pile[0]
	var first_id: int = first.get_instance_id()
	var sleeve_token: int = p.sleeve_pile[0].get_instance_id()
	expect(edit(REPLACE, HAND, sleeve_token, 0, "3") == STALE, "wrong pile does not resolve card")
	first.card_locked = true
	expect(edit(DISCARD, HAND, first_id) == UNAVAILABLE and p.play_pile.has(first), "locked card protected")
	first.card_locked = false
	first.card_shrouded = true
	expect(edit(DISCARD, HAND, first_id) == UNAVAILABLE, "hidden card protected")
	first.card_shrouded = false
	var lift := Node2D.new()
	add_named(first, "ShroudLiftOnHover", lift)
	var back := Node2D.new()
	add_named(lift, "ShroudSprite", back)
	expect(edit(DISCARD, HAND, first_id) == UNAVAILABLE, "face-down transition protected while back visible")
	back.scale.x = 0.0
	foe.moon_shroud_effect_active = true
	expect(edit(ADD, SLEEVE, 0, 0, "3") == UNAVAILABLE, "opponent shroud blocks addition")
	foe.moon_shroud_effect_active = false
	p.play_pile_pos_offset_array.append(Vector2.ZERO)
	expect(edit(ADD, HAND, 0, 0, "3") == BUSY, "misaligned table blocks addition")
	p.play_pile_pos_offset_array.pop_back()
	expect(edit(ADD, HAND, 0, 0, "4") == INVALID, "unknown card ID rejected")
	first.card_hover_ended.connect(func(_card: Node) -> void: fixture.interaction.debouncing = true, CONNECT_ONE_SHOT)
	expect(edit(REPLACE, HAND, first_id, 0, "3") == STALE and p.play_pile.has(first) and p.deck_storage.has(first) and p.play_pile.size() == 2, "synchronous hover callback invalidates a pending replacement")
	fixture.interaction.debouncing = false
	var copy_ids: Array = p.play_pile.duplicate()
	p.play_pile.make_read_only()
	expect(edit(ADD, HAND, 0, 0, "3") == UNAVAILABLE and p.play_pile.size() == 2, "read-only piles cannot be mutated")
	p.play_pile = copy_ids
	var tween: Tween = create_tween().set_loops()
	tween.tween_interval(1)
	first.get_node("MovementHandler").position_tween = tween
	expect(edit(ADD, HAND, 0, 0, "3") == APPLIED, "add table card")
	var last = p.play_pile[-1]
	expect(p.deck_storage.has(last) and p.play_pile.size() == 3 and p.play_pile_pos_offset_array.size() == 3 and last.position == Vector2(140, 50) and not tween.is_valid(), "table deck, offsets, layout and movement")
	expect(last.is_connected("card_clicked", Callable(fixture.await_input, "_on_PlayPile_Card_clicked").bind(last)), "table card wired to shipped state callback")
	p.play_pile.reverse()
	var old_index: int = p.play_pile.find(first)
	expect(edit(REPLACE, HAND, first_id, 0, "3") == APPLIED and p.play_pile[old_index].card_id == "3" and p.play_pile[0] == last and not p.deck_storage.has(first), "replacement resolves original ObjectID after reorder")
	expect(first.is_queued_for_deletion() and edit(DISCARD, HAND, first_id) == STALE, "old target retired and stale")
	expect(edit(DISCARD, HAND, last.get_instance_id()) == APPLIED and p.remove_calls == 1 and p.play_pile_pos_offset_array.size() == 2, "discard uses original deck removal")
	var saved_deck: Array = p.deck_storage.duplicate()
	p.deck_storage = [p.play_pile[0]]
	expect(edit(DISCARD, HAND, p.play_pile[0].get_instance_id()) == INVALID, "cannot discard last permanent card")
	p.deck_storage = saved_deck
	expect(edit(ADD, SLEEVE, 0, 0, "1") == APPLIED and p.resort_calls == 1 and p.visibility_calls == 1, "sleeve add and layout")
	expect(edit(REPLACE, SLEEVE, sleeve_token, 0, "3") == APPLIED and p.sleeve_pile[0].card_id == "3", "sleeve replace")
	expect(edit(DISCARD, SLEEVE, p.sleeve_pile[0].get_instance_id()) == APPLIED and p.sleeve_pile.size() == 2, "sleeve discard")
	expect(g.score_updates == 6 and fixture.await_input.button_refreshes == 6, "every card edit refreshes score and buttons")

	add_named(root, "SystemParameters", Params.new())
	add_named(root, "RelicParameters", RelicParams.new())
	add_named(root, "Scenes", Scenes.new())
	root.get_node("LuckyCharmList").free()
	add_named(root, "LuckyCharmList", Charms.new())
	add_named(fixture.run, "IDCard", Fake.FakeIDCard.new())
	add_named(foe, "IDCard", Fake.FakeIDCard.new())
	var rc := root.get_node("RelicList")
	rc.chip_relics = ["013", "016"]
	rc.effect_scripts = {"013": preload("res://lucky_charm_effect_resources/LuckyCharmOne.gd"), "016": preload("res://lucky_charm_effect_resources/LuckyCharmOne.gd")}
	var live_relic = p.get_node("RelicController")
	var mirror_relic = fixture.run.get_node("RelicController")
	# A stand-in controller receives the bound clicked signal.
	live_relic.set_script(preload("res://fake_relic_controller.gd"))
	expect(edit(ADD, RELIC, 0, 0, "100") == INVALID, "difficulty modifier rejected")
	if edit(ADD, RELIC, 0, 0, "013") != APPLIED:
		expect(false, "unique chip add")
	else:
		var active: Node2D = live_relic.relic_array[0]
		var standby: Node2D = mirror_relic.relic_array[0]
		expect(active != standby and fixture.run.player_relics == ["013"] and live_relic.relic_id_list == ["013"] and mirror_relic.relic_id_list == ["013"], "unique chip live/mirror/run synchronized")
		expect(active.position == Vector2(-7, 3) and active.get_node("Area2D").position == Vector2(-7, 3), "special relic layout")
		active.relic_effect_resource.effect_in_progress = true
		expect(edit(DISCARD, RELIC, active.get_instance_id()) == BUSY, "active relic effect blocks mutation")
		active.relic_effect_resource.effect_in_progress = false
		expect(edit(DISCARD, RELIC, standby.get_instance_id()) == STALE, "unique chip mirror ObjectID is stale")
		expect(edit(ADD, RELIC, 0, 0, "013") == INVALID, "duplicate unique chip blocked")
		expect(edit(REPLACE, RELIC, active.get_instance_id(), 0, "016") == APPLIED and fixture.run.player_relics == ["016"], "replace live/mirror/run unique chip")
		expect(edit(DISCARD, RELIC, live_relic.relic_array[0].get_instance_id()) == APPLIED and mirror_relic.relic_array.is_empty() and fixture.run.player_relics.is_empty(), "discard unique chip")
	fixture.await_input.set_script(preload("res://fake_await_input.gd"))
	var live_charm = p.get_node("LuckyCharmController")
	var mirror_charm = fixture.run.get_node("LuckyCharmController")
	if edit(ADD, CHARM, 0, 0, "1") != APPLIED:
		expect(false, "charm add")
	else:
		var active: Node2D = live_charm.lucky_charm_list[0]
		var standby: Node2D = mirror_charm.lucky_charm_list[0]
		expect(active != standby and fixture.run.player_lucky_charms == [{"id":1,"x_value":4}] and active.effect_resource.touch_calls == 1, "charm clone, x_value, and conditional click wiring")
		expect(active.position == Vector2(100, 98) and standby.position == Vector2(100, 98), "first charm layout is mirrored")
		expect(active.position_at_initialise == Vector2(100, 98) and standby.position_at_initialise == Vector2(100, 98), "new charm is positioned before initialise")
		expect(edit(DISCARD, CHARM, standby.get_instance_id()) == STALE, "charm mirror ObjectID is stale")
		expect(edit(ADD, CHARM, 0, 0, "1") == INVALID, "duplicate charm blocked")
		expect(edit(REPLACE, CHARM, active.get_instance_id(), 0, "2") == APPLIED and fixture.run.player_lucky_charms == [{"id":2,"x_value":-1}], "replace charm and update run data")
		expect(edit(DISCARD, CHARM, live_charm.lucky_charm_list[0].get_instance_id()) == APPLIED and mirror_charm.lucky_charm_list.is_empty() and fixture.run.player_lucky_charms.is_empty(), "discard charm")
	for charm_id in ["1", "2", "3", "4", "5", "6", "7"]:
		expect(edit(ADD, CHARM, 0, 0, charm_id) == APPLIED, "add charm %s" % charm_id)
	expect(edit(ADD, CHARM, 0, 0, "8") == APPLIED, "eighth charm exceeds the old arbitrary cap")
	expect(edit(ADD, CHARM, 0, 0, "9") == APPLIED, "ninth charm keeps extending the shipped fallback")
	expect(live_charm.lucky_charm_list.size() == 9 and mirror_charm.lucky_charm_list.size() == 9 and fixture.run.player_lucky_charms.size() == 9, "nine charms synchronize live, mirror, and saved data")
	var expected_positions := [
		Vector2(100, 98), Vector2(130, 98), Vector2(85, 110), Vector2(115, 110),
		Vector2(145, 110), Vector2(175, 110), Vector2(205, 110), Vector2(231, 110),
		Vector2(257, 110)
	]
	for i in range(9):
		expect(live_charm.lucky_charm_list[i].position == expected_positions[i] and mirror_charm.lucky_charm_list[i].position == expected_positions[i], "charm %d position is mirrored" % i)
	var overflow_live: Node2D = live_charm.lucky_charm_list[7]
	var overflow_mirror: Node2D = mirror_charm.lucky_charm_list[7]
	var overflow_live_twice: Node2D = live_charm.lucky_charm_list[8]
	var overflow_mirror_twice: Node2D = mirror_charm.lucky_charm_list[8]
	expect(overflow_live.position == Vector2(231, 110) and overflow_mirror.position == Vector2(231, 110), "first overflow charm uses shipped fallback in both lists")
	expect(overflow_live.position_at_initialise == Vector2(231, 110) and overflow_mirror.position_at_initialise == Vector2(231, 110), "first overflow is positioned before initialise")
	expect(overflow_live_twice.position == Vector2(257, 110) and overflow_mirror_twice.position == Vector2(257, 110), "second overflow charm continues from the final configured slot")
	expect(overflow_live_twice.position_at_initialise == Vector2(257, 110) and overflow_mirror_twice.position_at_initialise == Vector2(257, 110), "second overflow is positioned before initialise")
	var removed_token: int = live_charm.lucky_charm_list[0].get_instance_id()
	expect(edit(DISCARD, CHARM, removed_token) == APPLIED, "remove an earlier charm")
	var shifted_live: Node2D = live_charm.lucky_charm_list[7]
	var shifted_mirror: Node2D = mirror_charm.lucky_charm_list[7]
	expect(shifted_live.id == 9 and shifted_mirror.id == 9 and shifted_live.position == Vector2(231, 110) and shifted_mirror.position == Vector2(231, 110), "removing an earlier charm shifts overflow into the freed slot")
	var before_live: Array = live_charm.lucky_charm_list.duplicate()
	var before_mirror: Array = mirror_charm.lucky_charm_list.duplicate()
	var before_saved: Array = fixture.run.player_lucky_charms.duplicate(true)
	var before_token: int = live_charm.lucky_charm_list[0].get_instance_id()
	root.get_node("SystemParameters").free()
	add_named(root, "SystemParameters", MalformedParams.new())
	expect(edit(DISCARD, CHARM, before_token) == UNAVAILABLE and live_charm.lucky_charm_list == before_live and mirror_charm.lucky_charm_list == before_mirror and fixture.run.player_lucky_charms == before_saved, "malformed charm positions fail closed before discard")
	expect(edit(ADD, CHARM, 0, 0, "1") == UNAVAILABLE and live_charm.lucky_charm_list == before_live and mirror_charm.lucky_charm_list == before_mirror and fixture.run.player_lucky_charms == before_saved, "malformed charm positions fail closed before add")
	root.get_node("SystemParameters").free()
	add_named(root, "SystemParameters", EmptyParams.new())
	expect(edit(ADD, CHARM, 0, 0, "1") == UNAVAILABLE and live_charm.lucky_charm_list == before_live and mirror_charm.lucky_charm_list == before_mirror and fixture.run.player_lucky_charms == before_saved, "empty charm positions fail before mutation")
	fixture.cleanup()
	await process_frame
	print("Native mutation fixture: ", checks - failures, "/", checks, " checks passed")
	quit(0 if failures == 0 else 1)
