extends SceneTree

class OverlayAdapter extends RefCounted:
	var values := {"health": 20, "shield": 4, "chips": 25}
	var calls: Array[Array] = []
	var icon_calls: Array[Array] = []
	var description_calls: Array[Array] = []
	var test_texture: Texture2D

	func snapshot(_tree: SceneTree) -> Dictionary:
		return {"active": true, "edit_reason": "", "health": values.health,
			"max_health": 30, "shield": values.shield, "chips": values.chips,
			"can_edit_vitals": true, "can_edit_items": true, "can_edit_hand": true,
			"charm_count": 1, "unique_chips": 0,
			"charms": [{"id": "owned-charm", "name": "Owned charm"}], "relics": [],
			"hand": [{"id": "1", "name": "Owned card"}], "sleeve": [], "next_card": ""}

	func catalog(kind: String) -> Array:
		if kind == "charms":
			return [{"id": "test-charm", "name": "Test charm"}]
		if kind == "cards":
			return [{"id": "2", "name": "Replacement card"},
				{"id": "", "name": "Secret card", "concealed": true}]
		return []

	func item_icon(_tree: SceneTree, kind: String, id: String) -> Texture2D:
		icon_calls.append([kind, id])
		return test_texture

	func item_description(_tree: SceneTree, kind: String, id: String, _locale: String = "") -> String:
		description_calls.append([kind, id])
		return "Fixture description"

	func perform(_tree: SceneTree, action: String, value: Variant = null, index: int = -1) -> String:
		calls.append([action, value, index])
		if action in values:
			values[action] = value
		return ""


var _checks := 0
var _failures := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, description: String) -> void:
	_checks += 1
	if not condition:
		_failures += 1
		push_error("FAIL: " + description)


func _panel_point(panel: Window, control: Control) -> Vector2:
	return Vector2(panel.position) + control.get_global_rect().get_center()


func _mouse(panel: Window, control: Control, pressed: bool, button: MouseButton = MOUSE_BUTTON_LEFT) -> void:
	var event := InputEventMouseButton.new()
	event.position = _panel_point(panel, control)
	event.global_position = event.position
	event.button_index = button
	event.pressed = pressed
	event.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed and button == MOUSE_BUTTON_LEFT else 0
	root.push_input(event, true)


func _right_click(panel: Window, control: Control) -> void:
	_mouse(panel, control, true, MOUSE_BUTTON_RIGHT)
	_mouse(panel, control, false, MOUSE_BUTTON_RIGHT)


func _key(keycode: Key, pressed: bool, unicode_value: int = 0, control: bool = false) -> void:
	var event := InputEventKey.new()
	event.keycode = keycode
	event.physical_keycode = keycode
	event.unicode = unicode_value
	event.pressed = pressed
	event.ctrl_pressed = control
	root.push_input(event, true)


func _replace_text(line_edit: LineEdit, text: String) -> void:
	var panel: Window = root.get_node("GlossTrainer")._panel
	_mouse(panel, line_edit, true)
	_mouse(panel, line_edit, false)
	_key(KEY_A, true, 0, true)
	_key(KEY_A, false, 0, true)
	_key(KEY_BACKSPACE, true)
	_key(KEY_BACKSPACE, false)
	for character in text:
		var keycode := KEY_0 + int(character) - int("0")
		_key(keycode, true, character.unicode_at(0))
		_key(keycode, false)


func _run() -> void:
	await process_frame
	var overlay: Node = root.get_node("GlossTrainer")
	var fake := OverlayAdapter.new()
	fake.test_texture = ImageTexture.create_from_image(Image.create(2, 2, false, Image.FORMAT_RGBA8))
	overlay._adapter = fake
	overlay._refresh()
	overlay._set_open(true)
	await process_frame
	await process_frame
	var panel: Window = overlay._panel
	var health: LineEdit = panel.find_child("NumericDraft_health", true, false)
	var shield: LineEdit = panel.find_child("NumericDraft_shield", true, false)
	var chips: LineEdit = panel.find_child("NumericDraft_chips", true, false)
	var apply_health: Button = panel.find_child("NumericApply_health", true, false)
	var apply_shield: Button = panel.find_child("NumericApply_shield", true, false)
	var apply_chips: Button = panel.find_child("NumericApply_chips", true, false)
	_check(health != null and shield != null and chips != null,
		"all action-named numeric drafts exist as LineEdit controls")
	_check(apply_health != null and apply_shield != null and apply_chips != null,
		"all numeric apply buttons are named from their action")
	if health == null or shield == null or chips == null or apply_health == null or apply_shield == null or apply_chips == null:
		quit(1)
		return

	var text_changes := [0]
	health.text_changed.connect(func(_value: String) -> void: text_changes[0] += 1)
	_replace_text(health, "17")
	_check(health.has_focus() and health.text == "17", "root input types into focused health draft")
	# Injected keys update LineEdit.text under --headless but do not emit its native signal.
	# Emit the signal so this fixture also exercises the UI's dirty-draft connection.
	if text_changes[0] == 0:
		health.text_changed.emit(health.text)
	_check(health.get_meta("dirty", false), "typing marks the health draft dirty")
	fake.values.health = 19
	overlay._refresh()
	_check(health.text == "17", "snapshot polling preserves dirty draft (text=%s, dirty=%s, focus=%s)" % [
		health.text, health.get_meta("dirty", false), health.has_focus()])
	_mouse(panel, apply_health, true)
	_mouse(panel, apply_health, false)
	await process_frame
	_check(not fake.calls.is_empty() and fake.calls.back()[0] == "health" and fake.calls.back()[1] == 17,
		"health apply click submits the typed value (calls=%s)" % [fake.calls])
	_check(not health.get_meta("dirty", false), "successful apply clears the dirty draft")
	# Keep the blur/poll assertion independent if the apply-clears assertion above fails.
	health.set_meta("dirty", false)
	shield.grab_focus()
	fake.values.health = 21
	overlay._refresh()
	_check(health.text == "21", "blurred clean draft follows latest snapshot (text=%s, dirty=%s)" % [
		health.text, health.get_meta("dirty", false)])

	for pair: Array in [[shield, apply_shield, "8", "shield"],
		[chips, apply_chips, "61", "chips"]]:
		_replace_text(pair[0], pair[2])
		_mouse(panel, pair[1], true)
		_mouse(panel, pair[1], false)
		await process_frame
		_check(fake.calls.back()[0] == pair[3] and fake.calls.back()[1] == int(pair[2]),
			"%s apply click submits the typed value" % pair[3])

	_replace_text(shield, "9")
	_key(KEY_ENTER, true)
	_key(KEY_ENTER, false)
	await process_frame
	_check(fake.calls.back() == ["shield", 9, -1], "Enter shares the same numeric commit path")
	_replace_text(chips, "2147483648")
	var before_invalid := fake.calls.size()
	_key(KEY_ENTER, true)
	_key(KEY_ENTER, false)
	await process_frame
	_check(fake.calls.size() == before_invalid and chips.text == "2147483648",
		"invalid numeric input is not sent to the game and remains available to correct")

	overlay._navigate(2)
	await process_frame
	var owned: Control = panel.find_child("Owned_charms_0", true, false)
	_check(owned != null, "owned charm tile uses the specified stable name")
	if owned == null:
		quit(1)
		return
	var items: ScrollContainer = overlay._tabs.get_current_tab_control()
	items.ensure_control_visible(owned)
	await process_frame
	var before_right_click := fake.calls.size()
	var item_point := _panel_point(panel, owned)
	var right_down := InputEventMouseButton.new()
	right_down.position = item_point
	right_down.global_position = item_point
	right_down.button_index = MOUSE_BUTTON_RIGHT
	right_down.pressed = true
	root.push_input(right_down, true)
	var right_up := right_down.duplicate() as InputEventMouseButton
	right_up.pressed = false
	root.push_input(right_up, true)
	await process_frame
	var menu: Node = panel.find_child("CollectionMenu", true, false)
	var menu_visible := menu is CanvasItem and (menu as CanvasItem).visible
	_check(menu_visible, "right-click opens CollectionMenu")
	_check(fake.calls.size() == before_right_click, "opening the context menu does not perform an item action")
	var remove_action: Button = panel.find_child("CollectionMenu_remove", true, false)
	var replace_action: Button = panel.find_child("CollectionMenu_replace", true, false)
	var add_action: Button = panel.find_child("CollectionMenu_add", true, false)
	_check(remove_action != null and replace_action != null and add_action != null,
		"collection menu exposes named remove, replace, and add actions")
	if remove_action != null:
		_mouse(panel, remove_action, true)
		_mouse(panel, remove_action, false)
		await process_frame
		_check(fake.calls.back()[0] == "remove_charm" and fake.calls.back()[2] == 0,
			"right-click menu remove action targets the owned item")
	var icon_was_requested := fake.icon_calls.any(func(call: Array) -> bool:
		return call == ["charms", "test-charm"])
	var description_was_requested := fake.description_calls.any(func(call: Array) -> bool:
		return call == ["charms", "test-charm"])
	_check(icon_was_requested, "overlay requests catalog icons through the adapter contract")
	_check(description_was_requested, "overlay requests catalog descriptions through the adapter contract")
	var catalog_name := "Catalog_charms_" + "test-charm".validate_node_name()
	var catalog_item: Control = panel.find_child(catalog_name, true, false)
	_check(catalog_item != null, "catalog charm tile uses the sanitized kind/id name")
	if catalog_item == null:
		quit(1)
		return
	var item_page: ScrollContainer = overlay._tabs.get_current_tab_control()
	item_page.ensure_control_visible(catalog_item)
	await process_frame
	_mouse(panel, catalog_item, true)
	_mouse(panel, catalog_item, false)
	await process_frame
	_check(not fake.calls.is_empty() and fake.calls.back()[0] == "add_charm"
		and fake.calls.back()[1] == "test-charm", "one catalog click adds the selected charm (calls=%s, disabled=%s, rect=%s)" % [
		fake.calls, catalog_item.disabled, catalog_item.get_global_rect()])

	overlay._navigate(1)
	await process_frame
	var page: ScrollContainer = overlay._tabs.get_current_tab_control()
	var owned_card: Control = panel.find_child("Owned_cards_0", true, false)
	_check(owned_card != null, "owned card fixture is rendered for discard/replace coverage")
	if owned_card != null:
		page.ensure_control_visible(owned_card)
		await process_frame
		_right_click(panel, owned_card)
		await process_frame
		var discard_action: Button = panel.find_child("CollectionMenu_remove", true, false)
		_check(discard_action != null and discard_action.text in ["Discard", "丢弃"],
			"owned card menu labels removal as discard (text=%s)" % ("missing" if discard_action == null else discard_action.text))
		var replace_action_card: Button = panel.find_child("CollectionMenu_replace", true, false)
		if replace_action_card != null:
			_mouse(panel, replace_action_card, true)
			_mouse(panel, replace_action_card, false)
			await process_frame
			var replacement_tile: Control = panel.find_child("Catalog_cards_2", true, false)
			_check(replacement_tile != null, "replace action opens the card picker")
			if replacement_tile != null:
				var concealed_tile: Button = panel.find_child("Catalog_cards_", true, false)
				_check(concealed_tile != null and concealed_tile.get_meta("locked", false),
					"concealed catalog entries render as locked")
				_check(concealed_tile != null and not concealed_tile.tooltip_text.contains("Secret card"),
					"concealed card identity is not exposed in its tooltip")
				_check(not fake.icon_calls.has(["cards", ""]) and not fake.description_calls.has(["cards", ""]),
					"concealed entries do not request art or descriptions")
				page.ensure_control_visible(replacement_tile)
				await process_frame
				_mouse(panel, replacement_tile, true)
				_mouse(panel, replacement_tile, false)
				await process_frame
				_check(fake.calls.back()[0] == "replace_card" and fake.calls.back()[1] == "2"
					and fake.calls.back()[2] == 0, "picking a card replaces the targeted owned card")
		var card_again: Control = panel.find_child("Owned_cards_0", true, false)
		if card_again != null:
			page.ensure_control_visible(card_again)
			await process_frame
			_right_click(panel, card_again)
			await process_frame
			discard_action = panel.find_child("CollectionMenu_remove", true, false)
			if discard_action != null:
				_mouse(panel, discard_action, true)
				_mouse(panel, discard_action, false)
				await process_frame
				_check(fake.calls.back()[0] == "remove_card" and fake.calls.back()[2] == 0,
					"discard action removes the targeted owned card")

	overlay._set_open(false)
	print("DnDG compact overlay: %d checks, %d failures" % [_checks, _failures])
	quit(1 if _failures else 0)
