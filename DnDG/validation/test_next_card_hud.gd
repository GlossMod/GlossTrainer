extends SceneTree

const Overlay = preload("res://gloss_trainer/trainer_overlay.gd")

class PreviewAdapter extends RefCounted:
    var next := {}
    var calls := 0
    var locale := ""

    func snapshot(_tree: SceneTree) -> Dictionary:
        return {"active": false}

    func next_card_preview(_tree: SceneTree, requested_locale: String) -> Dictionary:
        calls += 1
        locale = requested_locale
        return next

var _checks := 0
var _failures := 0


func _initialize() -> void:
    _run.call_deferred()


func _check(condition: bool, message: String) -> void:
    _checks += 1
    if not condition:
        _failures += 1
        push_error(message)


func _move_mouse(at: Vector2) -> void:
    var motion := InputEventMouseMotion.new()
    motion.position = at
    motion.global_position = at
    root.push_input(motion, true)
    await process_frame
    await process_frame


func _capture(file: String) -> void:
    if DisplayServer.get_name() == "headless":
        return
    await RenderingServer.frame_post_draw
    var result := root.get_texture().get_image().save_png("res://" + file)
    _check(result == OK, "save " + file)


func _run() -> void:
    await process_frame
    var overlay := root.get_node("GlossTrainer") as Overlay
    var background := ColorRect.new()
    background.color = Color("20342d")
    background.mouse_filter = Control.MOUSE_FILTER_IGNORE
    root.add_child(background)
    background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    var adapter := PreviewAdapter.new()
    overlay._adapter = adapter
    overlay._language = "zh"
    overlay._dark = true
    overlay._refresh_language()
    overlay._apply_theme()
    adapter.next = {"name": "四条", "description": "出牌时：随机选择 1 到 3，打出每个花色的该点数各一张。\n弃牌时：烧毁。"}

    overlay._show_next = false
    overlay._update_next_card(0.2)
    _check(adapter.calls == 0 and not overlay._next_badge.visible, "disabled HUD does not read the adapter")
    overlay._set_open(true)
    var toggle: CheckButton
    for node: Node in overlay._panel.find_children("*", "CheckButton", true, false):
        if node.text == "显示下一张牌":
            toggle = node
            break
    if toggle == null:
        push_error("missing Show Next Card check button")
        quit(1)
        return
    toggle.button_pressed = true
    overlay._update_next_card(0.2)
    _check(overlay._show_next and adapter.calls == 0 and not overlay._next_badge.visible,
        "real setting enables HUD but does not read/display while panel is open")
    overlay._set_open(false)
    overlay._update_next_card(0.2)
    await _move_mouse(Vector2(300, 170))
    _check(overlay._next_badge.visible and overlay._next_badge_label.text == "下一张：四条",
        "left corner label replaces the eye")
    _check(not overlay._next_detail.visible and adapter.locale == "zh", "effect stays hidden outside the badge")
    await _move_mouse(overlay._next_badge.get_global_rect().get_center())
    _check(overlay._next_detail.visible and overlay._next_detail_label.text == adapter.next.description,
        "hover via the real root input route shows the effect")
    _check(overlay._next_detail.get_global_rect().position.y >= overlay._next_badge.get_global_rect().end.y,
        "details open below, not over the badge itself")
    for control: Control in [overlay._next_badge, overlay._next_badge_label, overlay._next_detail, overlay._next_detail_label]:
        _check(control.mouse_filter == Control.MOUSE_FILTER_IGNORE, "overlay does not intercept gameplay mouse events")
    await _capture("next-card-dark-zh.png")

    adapter.next = {"name": "红心七", "description": ""}
    overlay._update_next_card(0.2)
    _check(overlay._next_badge_label.text.ends_with("红心七") and overlay._next_detail_label.text == "无特殊效果。",
        "new top replaces both name and effect, including ordinary cards")
    await _move_mouse(Vector2(300, 170))
    _check(not overlay._next_detail.visible, "leaving the badge hides the effect")
    overlay._dark = false
    overlay._language = "en"
    overlay._refresh_language()
    overlay._apply_theme()
    adapter.next = {"name": "Four of a Kind", "description": "On play: choose 1 to 3 and play one card of each suit with that value."}
    overlay._update_next_card(0.2)
    await _move_mouse(overlay._next_badge.get_global_rect().get_center())
    _check(overlay._next_detail.visible and overlay._next_badge_label.text == "Next card: Four of a Kind"
        and adapter.locale == "en", "badge and details update after changing language and theme")
    await _capture("next-card-light-en.png")

    adapter.next = {}
    overlay._update_next_card(0.2)
    _check(not overlay._next_badge.visible and not overlay._next_detail.visible,
        "unavailable card immediately clears both parts of the HUD")
    adapter.next = {"name": "Four of a Kind", "description": ""}
    overlay._update_next_card(0.2)
    _check(overlay._next_badge.visible, "available card restores the badge")
    overlay._set_open(true)
    _check(not overlay._next_badge.visible and not overlay._next_detail.visible, "reopening panel immediately hides the HUD")
    toggle.button_pressed = false
    overlay._set_open(false)
    overlay._update_next_card(0.2)
    _check(not overlay._show_next and not overlay._next_badge.visible, "switching off stops the HUD")
    print("DnDG next-card HUD: %d checks, %d failures" % [_checks, _failures])
    quit(1 if _failures else 0)
