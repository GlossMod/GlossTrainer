extends SceneTree

var _checks := 0
var _failures := 0


func _initialize() -> void:
    _run.call_deferred()


func _check(condition: bool, description: String) -> void:
    _checks += 1
    if not condition:
        _failures += 1
        push_error(description)


func _mouse_button(point: Vector2, pressed: bool, button: MouseButton = MOUSE_BUTTON_LEFT) -> void:
    var event := InputEventMouseButton.new()
    event.position = point
    event.global_position = point
    event.button_index = button
    event.pressed = pressed
    event.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed and button == MOUSE_BUTTON_LEFT else 0
    root.push_input(event, true)


func _mouse_motion(point: Vector2, left_down: bool) -> void:
    var event := InputEventMouseMotion.new()
    event.position = point
    event.global_position = point
    event.button_mask = MOUSE_BUTTON_MASK_LEFT if left_down else 0
    root.push_input(event, true)


func _key(keycode: Key, pressed: bool) -> void:
    var event := InputEventKey.new()
    event.physical_keycode = keycode
    event.pressed = pressed
    root.push_input(event, true)


func _root_center(panel: Window, control: Control) -> Vector2:
    return Vector2(panel.position) + control.get_global_rect().get_center()


func _drag(panel: Window, control: Control, delta: Vector2) -> void:
    var from := _root_center(panel, control)
    _mouse_button(from, true)
    _mouse_motion(from + delta * 0.5, true)
    _mouse_motion(from + delta, true)
    _mouse_button(from + delta, false)


func _run() -> void:
    await process_frame
    var overlay := root.get_node("GlossTrainer")
    overlay._set_open(true)
    await process_frame
    await process_frame
    var panel: Window = overlay._panel
    var title: Control = panel.find_child("TitleDragArea", true, false)
    var grip: Control = panel.find_child("ResizeGrip", true, false)
    var close: Control = panel.find_child("ClosePanel", true, false)
    _check(panel.is_embedded(), "fixture uses an embedded panel")
    if title == null or grip == null or close == null:
        push_error("Expected title drag area, resize grip, and close button")
        quit(1)
        return
    var original := Rect2i(panel.position, panel.size)
    var bounds := Rect2i(root.get_visible_rect())
    _check(original.size == Vector2i(286, 222) and paused, "opens at compact default size and pauses")
    _check(panel.position == Vector2i(bounds.end.x - 286 - 12, bounds.position.y + 12),
        "compact panel opens 12 logical pixels from the upper-right edges")
    _drag(panel, title, Vector2(-60, 15))
    _check(panel.position == original.position + Vector2i(-60, 15), "title drag follows pointer across consecutive motion events")
    _check(panel.size == original.size, "moving does not resize")
    var after_move := panel.position
    var center := _root_center(panel, title)
    _mouse_button(center, true, MOUSE_BUTTON_RIGHT)
    _mouse_motion(center + Vector2(10, 8), false)
    _mouse_button(center + Vector2(10, 8), false, MOUSE_BUTTON_RIGHT)
    _check(panel.position == after_move, "right button does not start drag")
    _drag(panel, grip, Vector2(35, 8))
    _check(panel.size == original.size + Vector2i(35, 8), "corner drag grows width and height")
    _check(panel.position == after_move, "resizing keeps the top left fixed")
    await process_frame
    await process_frame
    _drag(panel, grip, Vector2(-160, -130))
    _check(panel.size == panel.min_size, "corner drag respects minimum dimensions")
    await process_frame
    await process_frame
    var page: ScrollContainer = overlay._tabs.get_child(0)
    var number: LineEdit = overlay._numbers["health"]
    var row: HBoxContainer = number.get_parent()
    _check(row.get_global_rect().end.x <= page.get_global_rect().end.x, "player controls fit horizontally at minimum size")
    _drag(panel, title, Vector2(-1000, -1000))
    _check(panel.position == Vector2i(root.get_visible_rect().position), "dragging beyond the top left keeps the header in reach")
    await process_frame
    await process_frame
    _drag(panel, grip, Vector2(1000, 1000))
    _check(panel.size == Vector2i(root.get_visible_rect().size), "resizing stops at the viewport edge")
    await process_frame
    await process_frame
    _drag(panel, grip, Vector2(-175, -75))
    await process_frame
    await process_frame
    _drag(panel, title, Vector2(1000, 1000))
    var chosen := Rect2i(panel.position, panel.size)
    _check(chosen.end == Vector2i(root.get_visible_rect().end), "dragging beyond the bottom right keeps the grip in reach")
    _mouse_motion(_root_center(panel, title) - Vector2(15, 10), false)
    _check(panel.position == chosen.position, "motion after release does not move the panel")
    if DisplayServer.get_name() != "headless":
        await RenderingServer.frame_post_draw
        root.get_texture().get_image().save_png("res://resized-panel.png")
    var button_point := _root_center(panel, close)
    _mouse_button(button_point, true)
    _mouse_button(button_point, false)
    _check(not panel.visible and not paused, "close button still closes and restores pause state")
    _key(KEY_INSERT, true)
    _key(KEY_INSERT, false)
    await process_frame
    await process_frame
    _check(panel.visible and paused, "Insert reopens the same panel")
    _check(Rect2i(panel.position, panel.size) == chosen, "reopening keeps the new position and size")
    root.content_scale_size = Vector2i(520, 310)
    await process_frame
    await process_frame
    bounds = Rect2i(root.get_visible_rect())
    _check(bounds.size == Vector2i(520, 310), "fixture shrank the logical game viewport")
    _check(bounds.encloses(Rect2i(panel.position, panel.size)), "shrinking the host viewport keeps the panel reachable")
    overlay._set_open(false)
    print("DnDG window geometry: %d checks, %d failures" % [_checks, _failures])
    quit(1 if _failures else 0)
