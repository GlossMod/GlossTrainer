extends RefCounted

class FakeTouchInputHandler extends Node:
	var connect_count := 0
	func connect_touch_controller_signals() -> void:
		connect_count += 1

class FakeCard extends Node2D:
	signal card_clicked
	signal card_hover_ended(card: Node)
	var card_id := ""
	var card_name := ""
	var card_name_format: Dictionary = {}
	var card_description := ""
	var card_description_format: Dictionary = {}
	var live_description := ""
	var description_reads := 0
	var card_locked := false
	var card_facedown := false
	var card_shrouded := false
	var is_flexible := false
	var flexible_high_value := 0
	var greyed := false
	var card_value := 0
	func _init(id: String = "1", display_name: String = "Card") -> void:
		card_id = id
		card_name = display_name
		var hover := Area2D.new()
		hover.name = "HoverArea"
		var touch := FakeTouchInputHandler.new()
		touch.name = "TouchInputHandler"
		hover.add_child(touch)
		add_child(hover)
		var movement := FakeMovementHandler.new()
		movement.name = "MovementHandler"
		add_child(movement)
	func get_card_name() -> String:
		var translated: Dictionary = {}
		for field: String in card_name_format:
			translated[field] = tr(str(card_name_format[field]))
		return tr(card_name).format(translated)
	func get_card_description() -> String:
		assert(get_parent() != null, "shipped getter requires a parent")
		description_reads += 1
		return tr(live_description)
	func make_card_grey(value: bool) -> void:
		greyed = value
	func set_card_value(value: int) -> void:
		card_value = value

class FakeMovementHandler extends Node:
	var position_tween: Tween
	var y_position_tween: Tween

class FakeCounter extends Node:
	var last_value := -1
	var calls := 0
	func change_chip_number(value: int, _animate: bool) -> void:
		last_value = value
		calls += 1

class FakeIDCard extends Node:
	var last_health := -1
	var health_calls := 0
	var last_shield := -1
	var shield_calls := 0
	var advantage_counter: Node
	var chip_counter: Node
	func _init() -> void:
		var counter := FakeCounter.new()
		counter.name = "ChipCounter"
		add_child(counter)
		chip_counter = counter
		advantage_counter = Node.new()
		advantage_counter.name = "AdvantageCounter"
		add_child(advantage_counter)
	func set_hitpoints_without_animations(value: int) -> void:
		last_health = value
		health_calls += 1
	func change_shieldpoints(value: int) -> void:
		last_shield = value
		shield_calls += 1

class FakeCardController extends Node:
	var lucky_charm_list: Array = []
	var relic_array: Array = []
	var relic_id_list: Array = []

class FakePlayer extends Node2D:
	var hitpoints := 20
	var max_hitpoints := 30
	var shieldpoints := 4
	var chips := 25
	var draw_pile: Array = []
	var play_pile: Array = []
	var sleeve_pile: Array = []
	var deck_storage: Array = []
	var play_pile_pos_offset_array: Array = []
	var play_pile_pos := Vector2(100, 50)
	var number_non_card_children := 3
	var moon_shroud_effect_active := false
	var advantage := 1
	var remove_calls := 0
	var resort_calls := 0
	var visibility_calls := 0
	var shroud_checks := 0
	var id_card: Node
	func _init() -> void:
		id_card = FakeIDCard.new()
		id_card.name = "IDCard"
		add_child(id_card)
		var charms := FakeCardController.new()
		charms.name = "LuckyCharmController"
		add_child(charms)
		var relics := FakeCardController.new()
		relics.name = "RelicController"
		add_child(relics)
	func instance_new_card(id: String) -> Node2D:
		return FakeCard.new(id, "Card %s" % id)
	func _remove_card_from_deck(card: Node) -> void:
		remove_calls += 1
		deck_storage.erase(card)
	func resort_sleeve_pile() -> void:
		resort_calls += 1
	func update_hand_visibility() -> void:
		visibility_calls += 1
	func get_play_pile_spacing_with(_count: int) -> int:
		return 20
	func check_and_update_moon_shroud_effect_status() -> void:
		shroud_checks += 1

class FakeAwaitInput extends Node:
	var button_refreshes := 0
	func _on_PlayPile_Card_clicked(_card: Node) -> void:
		pass
	func _on_SleevePile_Card_clicked(_card: Node) -> void:
		pass
	func check_and_set_button_clickability() -> void:
		button_refreshes += 1

class FakeOpponent extends Node:
	var moon_shroud_effect_active := false
	var shroud_checks := 0
	func check_and_update_moon_shroud_effect_status() -> void:
		shroud_checks += 1

class FakeGame extends Node:
	var current_state := "PlayerAwaitInput"
	var current_player: Node
	var game_completed := false
	var score_updates := 0
	func update_scores() -> void:
		score_updates += 1

class FakeRun extends Node:
	var game_controller: Node
	var player_lucky_charms: Array = []
	var player_relics: Array = []

class FakeCardList extends Node:
	var card_effect_scripts: Array = [Resource.new(), Resource.new(), Resource.new()]

class FakeRelicList extends Node:
	var chip_relics: Array = []
	var relic_dictionary: Dictionary = {}
	var effect_scripts: Dictionary = {}

class FakeLuckyCharmList extends Node:
	const LUCKY_CHARM_DICTIONARY := {}

class FakeSingleton extends Node:
	var current_state := 0
	var debouncing := false
	var high_stakes := false
	var refresh_count := 0
	func is_high_stakes() -> bool:
		return high_stakes
	func get_effective_card_effect_resource(_card: Node) -> Resource:
		refresh_count += 1
		return null

class CardGroupsConstants extends Node:
	const ALL_OBTAINABLE := ["1", "2", "3"]

class CardParametersConstants extends Node:
	const CARD_EFFECT_CLICKED_CONDITIONAL := "clicked_conditional_effect"

var run: Node
var game: FakeGame
var player: FakePlayer
var opponent: FakeOpponent
var await_input: FakeAwaitInput
var interaction: FakeSingleton
var mode: FakeSingleton
var utilities: FakeSingleton
var card_list: Node

func build(tree: SceneTree) -> void:
	ProjectSettings.set_setting("application/config/version", "2.0.8.1")
	run = FakeRun.new()
	run.name = "FakeMacroController"
	run.scene_file_path = "res://MacroController.tscn"
	game = FakeGame.new()
	game.name = "GameController"
	player = FakePlayer.new()
	player.name = "Player"
	game.current_player = player
	game.add_child(player)
	await_input = FakeAwaitInput.new()
	await_input.name = "PlayerAwaitInput"
	game.add_child(await_input)
	opponent = FakeOpponent.new()
	opponent.name = "Opponent"
	game.add_child(opponent)
	run.game_controller = game
	run.add_child(game)
	tree.root.add_child(run)
	tree.current_scene = run
	interaction = FakeSingleton.new()
	interaction.name = "InteractionState"
	tree.root.add_child(interaction)
	mode = FakeSingleton.new()
	mode.name = "HighStakesUtils"
	tree.root.add_child(mode)
	utilities = FakeSingleton.new()
	utilities.name = "Utilities"
	tree.root.add_child(utilities)
	var groups := CardGroupsConstants.new()
	groups.name = "CardGroups"
	tree.root.add_child(groups)
	var params := CardParametersConstants.new()
	params.name = "CardParameters"
	tree.root.add_child(params)
	card_list = FakeCardList.new()
	card_list.name = "CardList"
	tree.root.add_child(card_list)
	var relic_list := FakeRelicList.new()
	relic_list.name = "RelicList"
	tree.root.add_child(relic_list)
	var charm_list := FakeLuckyCharmList.new()
	charm_list.name = "LuckyCharmList"
	tree.root.add_child(charm_list)
	var charm_mirror := FakeCardController.new()
	charm_mirror.name = "LuckyCharmController"
	run.add_child(charm_mirror)
	var relic_mirror := FakeCardController.new()
	relic_mirror.name = "RelicController"
	run.add_child(relic_mirror)
	var first := FakeCard.new("1", "First draw")
	player.draw_pile.append(first)
	var table_a := FakeCard.new("1", "Table A")
	var table_b := FakeCard.new("2", "Table B")
	for card: FakeCard in [table_a, table_b]:
		player.add_child(card)
		player.play_pile.append(card)
		player.deck_storage.append(card)
		player.play_pile_pos_offset_array.append(Vector2.ZERO)
	var sleeve_a := FakeCard.new("2", "Sleeve A")
	var sleeve_b := FakeCard.new("3", "Sleeve B")
	for card: FakeCard in [sleeve_a, sleeve_b]:
		player.add_child(card)
		player.sleeve_pile.append(card)
		player.deck_storage.append(card)

func cleanup() -> void:
	if is_instance_valid(run):
		run.queue_free()
	if is_instance_valid(interaction):
		interaction.queue_free()
	if is_instance_valid(mode):
		mode.queue_free()
	if is_instance_valid(utilities):
		utilities.queue_free()
	if is_instance_valid(card_list):
		card_list.queue_free()
	var tree := Engine.get_main_loop() as SceneTree
	var groups: Node = tree.root.get_node_or_null("CardGroups")
	if groups != null:
		groups.queue_free()
	var params: Node = tree.root.get_node_or_null("CardParameters")
	if params != null:
		params.queue_free()
	var relics: Node = tree.root.get_node_or_null("RelicList")
	if relics != null:
		relics.queue_free()
	var charms: Node = tree.root.get_node_or_null("LuckyCharmList")
	if charms != null:
		charms.queue_free()
	if is_instance_valid(player) and not player.draw_pile.is_empty():
		var unparented_draw_card: Node = player.draw_pile[0]
		if not unparented_draw_card.is_inside_tree():
			unparented_draw_card.free()
