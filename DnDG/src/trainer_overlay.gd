extends CanvasLayer
## Autoload as GlossTrainer. The adapter is the only game-specific dependency.

const Adapter = preload("res://gloss_trainer/game_adapter.gd")
const CONFIG_PATH := "user://gloss_trainer.cfg"
const REPOSITORY := "https://github.com/GlossMod/GlossTrainer"
const RELEASE_API := "https://api.github.com/repos/GlossMod/GlossTrainer/releases"
const PAGE_SIZE := 100
const HEAL_BUTTON_WIDTH := 48
const PANEL_INITIAL_SIZE := Vector2i(286, 222)
const PANEL_MIN_SIZE := Vector2i(252, 183)
const PANEL_INSET := Vector2i(12, 12)
const CATALOG_PAGE_SIZE := 30
const PANEL_FONT_SIZE := 10
const BRAND_FONT_SIZE := 13
const AUX_FONT_SIZE := 9
const NEXT_CARD_INTERVAL := 0.15
const NEXT_CARD_INSET := Vector2(8, 8)

class ArtTile extends Button:
    func _make_custom_tooltip(contents: String) -> Object:
        var detail := Label.new()
        detail.theme_type_variation = "TooltipLabel"
        detail.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
        detail.custom_minimum_size.x = 154
        detail.text = contents
        return detail

var _adapter = Adapter.new()
var _ui_font: FontFile
var _next_badge: PanelContainer
var _next_badge_label: Label
var _next_detail: PanelContainer
var _next_detail_label: Label
var _config := ConfigFile.new()
var _panel: Window
var _resize_grip: TextureRect
var _panel_opened := false
var _panel_dragging := false
var _drag_pointer := Vector2.ZERO
var _drag_rect := Rect2i()
var _surface: PanelContainer
var _tabs: TabContainer
var _navigation: Array[Button] = []
var _brand_accent: Label
var _header_version: Label
var _status: Label
var _feedback: Label
var _next: Label
var _shortcut: Button
var _footer: Label
var _footer_keycap: Label
var _update_label: Label
var _update_button: Button
var _themes: OptionButton
var _charm_count: Button
var _relic_count: Button
var _item_sections: Dictionary = {}
var _item_kind := "charms"
var _http: HTTPRequest
var _snapshot: Dictionary = {}
var _numbers: Dictionary = {}
var _lists: Dictionary = {}
var _menu: PanelContainer
var _menu_target: Dictionary = {}
var _pick_target: Dictionary = {}
var _edit_reasons: Dictionary = {}
var _localized: Array = []
var _vital_buttons: Array[Button] = []
var _dark := true
var _language := "auto"
var _show_next := false
var _toggle_key: int = KEY_INSERT
var _capturing_key := false
var _was_paused := false
var _mouse_mode: int = Input.MOUSE_MODE_VISIBLE
var _poll_time := 0.0
var _next_card_time := NEXT_CARD_INTERVAL
var _next_pointer := Vector2.INF
var _locale := ""
var _update_page := 0
var _best_release: Dictionary = {}
var _swallow_keys: Dictionary = {}
var _update_message: Array = ["Not checked", "尚未检查"]


func _ready() -> void:
    layer = 100
    process_mode = Node.PROCESS_MODE_ALWAYS
    _load_settings()
    _ui_font = _load_ui_font()
    _build_ui()
    _build_next_card_hud()
    _refresh_language()
    _apply_theme()
    _refresh()
    print("[GlossTrainer] ready")
    if "--gloss-trainer-open" in OS.get_cmdline_user_args():
        _set_open.call_deferred(true)


func _exit_tree() -> void:
    if is_instance_valid(_panel) and _panel.visible:
        get_tree().paused = _was_paused
        Input.mouse_mode = _mouse_mode


func _load_settings() -> void:
    if _config.load(CONFIG_PATH) != OK:
        return
    _dark = bool(_config.get_value("ui", "dark", true))
    _show_next = bool(_config.get_value("ui", "show_next", false))
    var language: String = str(_config.get_value("ui", "language", "auto"))
    _language = language if language in ["auto", "en", "zh"] else "auto"
    var key: Variant = _config.get_value("ui", "toggle_key", KEY_INSERT)
    if key is int and key > 0 and not OS.get_keycode_string(key).is_empty():
        _toggle_key = key


func _save_settings() -> void:
    _config.set_value("ui", "dark", _dark)
    _config.set_value("ui", "show_next", _show_next)
    _config.set_value("ui", "language", _language)
    _config.set_value("ui", "toggle_key", _toggle_key)
    if _config.save(CONFIG_PATH) != OK:
        _feedback.text = _t("Could not save settings.", "无法保存设置。")


func _t(en: String, zh: String) -> String:
    return zh if _locale == "zh" else en


func _text(object: Object, property: String, en: String, zh: String) -> void:
    _localized.append([object, property, en, zh])
    object.set(property, _t(en, zh))


func _refresh_language() -> void:
    var language := _language
    if language == "auto":
        language = "zh" if TranslationServer.get_locale().to_lower().begins_with("zh") else "en"
    _locale = language
    _next_card_time = NEXT_CARD_INTERVAL
    for entry: Array in _localized:
        if is_instance_valid(entry[0]):
            entry[0].set(entry[1], _t(entry[2], entry[3]))
    for i in range(4):
        _tabs.set_tab_title(i, _t(["Player", "Cards", "Items", "Settings"][i], ["玩家", "卡牌", "物品", "设置"][i]))
    _update_shortcut()
    _themes.set_item_text(0, _t("Dark", "暗色"))
    _themes.set_item_text(1, _t("Light", "亮色"))
    _update_label.text = _t(_update_message[0], _update_message[1])
    for kind: String in _lists:
        _lists[kind]["signature"] = ""
        _lists[kind]["catalog_signature"] = []
        _lists[kind]["entry_locale"] = ""


func _label(parent: Node, en: String, zh: String) -> Label:
    var label := Label.new()
    parent.add_child(label)
    label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    _text(label, "text", en, zh)
    return label


func _button(parent: Node, en: String, zh: String, callback: Callable, localize: bool = true) -> Button:
    var button := Button.new()
    parent.add_child(button)
    button.custom_minimum_size.y = 26
    if localize:
        _text(button, "text", en, zh)
    else:
        button.text = _t(en, zh)
    button.pressed.connect(callback)
    return button


func _row(parent: Node) -> HBoxContainer:
    var row := HBoxContainer.new()
    row.add_theme_constant_override("separation", 4)
    parent.add_child(row)
    return row


func _page(title: String) -> VBoxContainer:
    var scroll := ScrollContainer.new()
    scroll.name = title
    scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
    _tabs.add_child(scroll)
    var box := VBoxContainer.new()
    box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    box.add_theme_constant_override("separation", 8)
    scroll.add_child(box)
    return box


func _card(parent: Node) -> VBoxContainer:
    var panel := PanelContainer.new()
    panel.theme_type_variation = "GlossSection"
    panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    parent.add_child(panel)
    var column := VBoxContainer.new()
    column.add_theme_constant_override("separation", 3)
    panel.add_child(column)
    return column


func _navigate(index: int) -> void:
    _tabs.current_tab = index


func _page_changed(index: int) -> void:
    _hide_menu()
    _cancel_picker()
    _feedback.text = ""
    for i in range(_navigation.size()):
        _navigation[i].set_pressed_no_signal(i == index)
    _refresh()


func _navigation_icon(index: int, color: Color) -> Texture2D:
    var shapes: Array[String] = [
        '<circle cx="6" cy="3.5" r="2"/><path d="M2 11v-1a4 4 0 0 1 8 0v1"/>',
        '<rect x="1" y="3" width="7" height="8" rx="1"/><path d="M4 1h6a1 1 0 0 1 1 1v6"/>',
        '<path d="M1 3.5 6 1l5 2.5v5L6 11 1 8.5zM1 3.5 6 6l5-2.5M6 6v5"/>',
        '<circle cx="6" cy="6" r="3"/><path d="M6 0v3m0 6v3M0 6h3m6 0h3M2 2l2 2m4 4 2 2M2 10l2-2m4-4 2-2"/>'
    ]
    var image := Image.new()
    image.load_svg_from_string('<svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 12 12"><g fill="none" stroke="#%s" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">%s</g></svg>' % [color.to_html(false), shapes[index]])
    return ImageTexture.create_from_image(image)


func _switch_icon(enabled: bool, track: Color, knob: Color) -> Texture2D:
    var image := Image.new()
    image.load_svg_from_string('<svg xmlns="http://www.w3.org/2000/svg" width="26" height="14"><rect width="26" height="14" rx="7" fill="#%s"/><circle cx="%d" cy="7" r="5" fill="#%s"/></svg>' % [track.to_html(false), 19 if enabled else 7, knob.to_html(false)])
    return ImageTexture.create_from_image(image)


func _build_ui() -> void:
    # A separate exclusive viewport routes GUI input before the game's _input.
    # Pausing also prevents normal gameplay Input polling while editing.
    _panel = Window.new()
    _panel.name = "GlossTrainerPanel"
    _panel.title = "GlossTrainer | DnDG"
    _panel.size = PANEL_INITIAL_SIZE
    _panel.min_size = PANEL_MIN_SIZE
    _panel.transient = true
    _panel.exclusive = true
    _panel.borderless = true
    _panel.visible = false
    add_child(_panel)
    _panel.close_requested.connect(func() -> void: _set_open(false))
    _panel.window_input.connect(_window_input)
    _panel.focus_exited.connect(func() -> void: _panel_dragging = false)
    get_viewport().size_changed.connect(_fit_panel_to_viewport)
    _surface = PanelContainer.new()
    _panel.add_child(_surface)
    _surface.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
    var margin := MarginContainer.new()
    for side: String in ["left", "right", "top", "bottom"]:
        margin.add_theme_constant_override("margin_" + side, 5)
    _surface.add_child(margin)
    var column := VBoxContainer.new()
    margin.add_child(column)
    column.add_theme_constant_override("separation", 4)
    var header := _row(column)
    var title_bar := _row(header)
    title_bar.name = "TitleDragArea"
    title_bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    title_bar.mouse_filter = Control.MOUSE_FILTER_STOP
    title_bar.mouse_default_cursor_shape = Control.CURSOR_MOVE
    title_bar.gui_input.connect(_panel_pointer_input.bind(title_bar, false))
    var wordmark := HBoxContainer.new()
    wordmark.mouse_filter = Control.MOUSE_FILTER_IGNORE
    wordmark.add_theme_constant_override("separation", 0)
    title_bar.add_child(wordmark)
    var brand := _label(wordmark, "Gloss", "Gloss")
    brand.autowrap_mode = TextServer.AUTOWRAP_OFF
    brand.add_theme_font_size_override("font_size", BRAND_FONT_SIZE)
    _brand_accent = _label(wordmark, "Trainer", "Trainer")
    _brand_accent.autowrap_mode = TextServer.AUTOWRAP_OFF
    _brand_accent.add_theme_font_size_override("font_size", BRAND_FONT_SIZE)
    var title_rule := VSeparator.new()
    title_rule.mouse_filter = Control.MOUSE_FILTER_IGNORE
    title_bar.add_child(title_rule)
    _label(title_bar, "DnDG", "DnDG")
    _header_version = Label.new()
    var version := str(ProjectSettings.get_setting("gloss_trainer/version", ""))
    _header_version.text = "v" + version.trim_prefix("v") if not version.is_empty() else "dev"
    _header_version.tooltip_text = "By: 小莫"
    _header_version.add_theme_font_size_override("font_size", AUX_FONT_SIZE)
    title_bar.add_child(_header_version)
    var close := _button(header, "×", "×", func() -> void: _set_open(false))
    close.name = "ClosePanel"
    close.custom_minimum_size = Vector2(20, 22)
    close.theme_type_variation = "GlossClose"
    column.add_child(HSeparator.new())
    var nav := _row(column)
    var group := ButtonGroup.new()
    for i in range(4):
        var navigation := _button(nav, ["Player", "Cards", "Items", "Settings"][i], ["玩家", "卡牌", "物品", "设置"][i], _navigate.bind(i))
        navigation.theme_type_variation = "GlossNavigation"
        navigation.toggle_mode = true
        navigation.button_group = group
        navigation.size_flags_horizontal = Control.SIZE_EXPAND_FILL
        navigation.custom_minimum_size.y = 22
        _navigation.append(navigation)
    _navigation[0].button_pressed = true
    _tabs = TabContainer.new()
    _tabs.tabs_visible = false
    _tabs.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    _tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
    column.add_child(_tabs)
    var player := _page("Player")
    player.add_theme_constant_override("separation", 3)
    _status = _label(player, "Waiting for a run", "等待进入游戏")
    _status.max_lines_visible = 2
    _edit_reasons["can_edit_vitals"] = _status
    var vitals := _card(player)
    for info: Array in [["health", "Health", "生命"], ["shield", "Shield", "护盾"], ["chips", "Chips", "筹码"]]:
        _number_row(vitals, info[0], info[1], info[2])
    var draw := _card(player)
    var toggle := CheckButton.new()
    draw.add_child(toggle)
    _text(toggle, "text", "Show next card", "显示下一张牌")
    _text(toggle, "tooltip_text", "Close the panel, then hover over the next-card label at the top left.", "关闭面板后，将鼠标移到左上角的下一张牌提示。")
    toggle.button_pressed = _show_next
    toggle.toggled.connect(func(enabled: bool) -> void:
        _show_next = enabled
        if not enabled:
            _hide_next_card()
        _next_card_time = NEXT_CARD_INTERVAL
        _save_settings()
        _refresh()
    )
    _next = _label(draw, "Next card: —", "下一张：—")
    _next.max_lines_visible = 1
    _next.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
    var cards := _page("Cards")
    cards.add_theme_constant_override("separation", 3)
    _edit_reasons["can_edit_hand"] = _label(cards, "", "")
    _label(cards, "Sleeve", "袖中手牌")
    _build_collection(cards, "sleeve", "sleeve")
    _label(cards, "Table cards", "桌面卡牌")
    _build_collection(cards, "cards", "hand")
    var items := _page("Items")
    _edit_reasons["can_edit_items"] = _label(items, "", "")
    var categories := _row(items)
    var item_group := ButtonGroup.new()
    _charm_count = _button(categories, "Charms", "幸运护符", _select_item_kind.bind("charms"), false)
    _relic_count = _button(categories, "Unique chips", "独特筹码", _select_item_kind.bind("relics"), false)
    for category in [_charm_count, _relic_count]:
        category.toggle_mode = true
        category.button_group = item_group
        category.custom_minimum_size.y = 22
        category.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    _charm_count.button_pressed = true
    for kind: String in ["charms", "relics"]:
        var section := VBoxContainer.new()
        items.add_child(section)
        section.visible = kind == _item_kind
        _item_sections[kind] = section
        _build_collection(section, kind, kind)
    _build_settings(_page("Settings"))
    _feedback = _label(column, "", "")
    _feedback.max_lines_visible = 2
    column.add_child(HSeparator.new())
    var footer := _row(column)
    _footer_keycap = Label.new()
    _footer_keycap.add_theme_font_size_override("font_size", AUX_FONT_SIZE)
    footer.add_child(_footer_keycap)
    _footer = _label(footer, "", "")
    _footer.add_theme_font_size_override("font_size", AUX_FONT_SIZE)
    _resize_grip = TextureRect.new()
    _resize_grip.name = "ResizeGrip"
    _resize_grip.custom_minimum_size = Vector2(18, 16)
    _resize_grip.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
    _resize_grip.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
    _resize_grip.mouse_filter = Control.MOUSE_FILTER_STOP
    _resize_grip.mouse_default_cursor_shape = Control.CURSOR_FDIAGSIZE
    footer.add_child(_resize_grip)
    _text(_resize_grip, "tooltip_text", "Drag to resize", "拖动调整窗口大小")
    _resize_grip.gui_input.connect(_panel_pointer_input.bind(_resize_grip, true))
    _menu = PanelContainer.new()
    _menu.name = "CollectionMenu"
    _menu.theme_type_variation = "GlossSection"
    _menu.z_index = 10
    _menu.hide()
    _surface.add_child(_menu)
    # PanelContainer lays out Control children; top-level keeps this menu free-floating.
    _menu.top_level = true
    var menu_rows := VBoxContainer.new()
    menu_rows.add_theme_constant_override("separation", 2)
    _menu.add_child(menu_rows)
    for entry: Array in [["remove", "Discard", "丢弃"], ["replace", "Replace…", "更换…"], ["add", "Add…", "添加…"]]:
        var option := _button(menu_rows, entry[1], entry[2], _menu_action.bind(entry[0]))
        option.name = "CollectionMenu_" + entry[0]
        option.alignment = HORIZONTAL_ALIGNMENT_LEFT
        option.custom_minimum_size.y = 21
    _http = HTTPRequest.new()
    _http.timeout = 12.0
    _http.body_size_limit = 4 * 1024 * 1024
    add_child(_http)
    _http.request_completed.connect(_release_response)
    _tabs.tab_changed.connect(_page_changed)


func _number_row(parent: Node, action: String, en: String, zh: String) -> void:
    var row := _row(parent)
    var label := _label(row, en, zh)
    label.custom_minimum_size.x = 45
    label.size_flags_horizontal = Control.SIZE_FILL
    var number := LineEdit.new()
    number.name = "NumericDraft_" + action
    number.alignment = HORIZONTAL_ALIGNMENT_RIGHT
    number.custom_minimum_size.x = 58
    number.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    number.select_all_on_focus = true
    row.add_child(number)
    _numbers[action] = number
    number.set_meta("live_text", "")
    number.text_changed.connect(func(_text_value: String) -> void:
        number.set_meta("dirty", number.text != number.get_meta("live_text"))
    )
    number.text_submitted.connect(func(_text_value: String) -> void: _commit_number(action))
    var apply := _button(row, "Apply", "应用", _commit_number.bind(action))
    apply.name = "NumericApply_" + action
    apply.custom_minimum_size = Vector2(44, 22)
    _vital_buttons.append(apply)
    if action == "health":
        var heal := _button(row, "Heal", "回满", func() -> void: _perform("heal"))
        heal.custom_minimum_size = Vector2(HEAL_BUTTON_WIDTH, 22)
        _text(heal, "tooltip_text", "Heal to full", "恢复全部生命")
        _vital_buttons.append(heal)
    else:
        var spacer := Control.new()
        spacer.custom_minimum_size.x = HEAL_BUTTON_WIDTH
        spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
        row.add_child(spacer)


func _commit_number(action: String) -> void:
    var draft: LineEdit = _numbers[action]
    var value := draft.text.strip_edges()
    if value.is_empty() or value.length() > 10 or not value.is_valid_int() or int(value) < 0 or int(value) > 2147483647:
        _feedback.text = _t("Enter a whole number from 0 to 2147483647.", "请输入 0 到 2147483647 之间的整数。")
        _feedback.show()
        return
    _perform(action, int(value))


func _is_cards(kind: String) -> bool:
    return kind == "cards" or kind == "sleeve"


func _flow(parent: Node) -> HFlowContainer:
    var flow := HFlowContainer.new()
    flow.add_theme_constant_override("h_separation", 4)
    flow.add_theme_constant_override("v_separation", 4)
    parent.add_child(flow)
    return flow


func _build_collection(parent: Node, kind: String, state_key: String) -> void:
    parent = _card(parent)
    var current := _flow(parent)
    var picker := VBoxContainer.new()
    parent.add_child(picker)
    picker.visible = not _is_cards(kind)
    var caption_row := _row(picker)
    var caption := _label(caption_row, "", "")
    var cancel := _button(caption_row, "Cancel", "取消", _cancel_picker)
    cancel.custom_minimum_size.y = 21
    var search_row := _row(picker)
    var search := LineEdit.new()
    search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
    search_row.add_child(search)
    _text(search, "placeholder_text", "Search name or ID…", "搜索名称或 ID…")
    var page_controls := _row(search_row)
    var back := _button(page_controls, "‹", "‹", _catalog_step.bind(kind, -1))
    _text(back, "tooltip_text", "Previous page", "上一页")
    var page := _label(page_controls, "", "")
    page.autowrap_mode = TextServer.AUTOWRAP_OFF
    page.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
    page.custom_minimum_size.x = 33
    var next := _button(page_controls, "›", "›", _catalog_step.bind(kind, 1))
    var catalog := _flow(picker)
    var owned_caption: Label
    if not _is_cards(kind):
        owned_caption = _label(parent, "Owned · right-click to edit", "已获得 · 右键编辑")
        owned_caption.hide()
        parent.move_child(current, -1)
    _text(next, "tooltip_text", "Next page", "下一页")
    _lists[kind] = {"search": search, "catalog": catalog, "current": current, "picker": picker,
        "caption": caption, "cancel": cancel, "state_key": state_key, "signature": "", "catalog_signature": [],
        "page": 0, "owned_caption": owned_caption, "page_controls": page_controls, "page_label": page, "back": back, "next": next}
    search.text_changed.connect(func(_query: String) -> void:
        _lists[kind]["page"] = 0
        _filter_catalog(kind)
    )


func _select_item_kind(kind: String) -> void:
    if kind == _item_kind:
        return
    _hide_menu()
    _cancel_picker()
    _feedback.text = ""
    _item_kind = kind
    for name_key: String in _item_sections:
        _item_sections[name_key].visible = name_key == kind
    _refresh()
    var page: ScrollContainer = _tabs.get_current_tab_control()
    page.scroll_vertical = 0


func _catalog_step(kind: String, direction: int) -> void:
    _lists[kind]["page"] += direction
    _filter_catalog(kind)


func _clear_children(parent: Node) -> void:
    for child: Node in parent.get_children():
        parent.remove_child(child)
        child.queue_free()


func _art_tile(parent: Node, kind: String, entry: Dictionary, node_name: String) -> Button:
    var id := str(entry.get("id", ""))
    var title := str(entry.get("name", id))
    var hidden: bool = entry.get("concealed", false)
    var tile := ArtTile.new()
    tile.name = node_name.validate_node_name()
    tile.theme_type_variation = "GlossTile"
    tile.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
    tile.custom_minimum_size = Vector2(33, 38) if _is_cards(kind) else Vector2(37, 35)
    if not hidden and not id.is_empty():
        tile.icon = _adapter.item_icon(get_tree(), kind, id)
    tile.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
    tile.expand_icon = true
    tile.text = "?" if hidden or id.is_empty() else id if tile.icon == null else ""
    tile.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
    var details: String = "" if hidden or id.is_empty() else _adapter.item_description(get_tree(), kind, id, _locale)
    if hidden:
        title = _t("Hidden card", "未公开的牌")
    tile.tooltip_text = title + ("\n" + details if not details.is_empty() else "")
    tile.set_meta("locked", hidden or bool(entry.get("locked", false)))
    parent.add_child(tile)
    return tile


func _filter_catalog(kind: String) -> void:
    var data: Dictionary = _lists[kind]
    var picker: Control = data["picker"]
    var picking: bool = _pick_target.get("kind", "") == kind
    picker.visible = not _is_cards(kind) or picking
    if not picker.visible:
        return
    var replacing: bool = picking and _pick_target.get("verb", "") == "replace"
    data["cancel"].visible = picking
    data["caption"].text = _t("Choose a replacement", "选择替换项") if replacing else _t("Click an icon to add", "点击图标添加")
    var query: String = data["search"].text.strip_edges().to_lower()
    var locale_key := _locale + ":" + TranslationServer.get_locale()
    var signature := [locale_key, query, data["page"]]
    if data["catalog_signature"] == signature:
        return
    if data.get("entry_locale", "") != locale_key:
        data["entries"] = _adapter.catalog("cards" if _is_cards(kind) else kind)
        data["entry_locale"] = locale_key
    var catalog: HFlowContainer = data["catalog"]
    _clear_children(catalog)
    var matches: Array[Dictionary] = []
    for entry: Dictionary in data["entries"]:
        var id := str(entry.get("id", ""))
        if query.is_empty() or query in (str(entry.get("name", id)) + " " + id).to_lower():
            matches.append(entry)
    var total_pages := maxi(1, ceili(float(matches.size()) / CATALOG_PAGE_SIZE))
    var page := clampi(int(data["page"]), 0, total_pages - 1)
    data["page"] = page
    signature[2] = page
    data["catalog_signature"] = signature
    data["page_controls"].visible = total_pages > 1
    data["page_label"].text = "%d / %d" % [page + 1, total_pages]
    data["back"].disabled = page == 0
    data["next"].disabled = page == total_pages - 1
    if matches.is_empty():
        var empty := Label.new()
        empty.text = _t("No matching icons", "没有匹配项")
        catalog.add_child(empty)
    var allowed := _collection_allowed(kind, _snapshot)
    for index in range(page * CATALOG_PAGE_SIZE, mini((page + 1) * CATALOG_PAGE_SIZE, matches.size())):
        var entry := matches[index]
        var id := str(entry.get("id", ""))
        var tile := _art_tile(catalog, kind, entry, "Catalog_" + kind + "_" + id)
        tile.pressed.connect(_pick_entry.bind(kind, id))
        tile.disabled = not allowed




func _collection_allowed(kind: String, state: Dictionary) -> bool:
    return bool(state.get("active", false)) and bool(state.get("can_edit_hand" if kind in ["cards", "sleeve"] else "can_edit_items", false))


func _edit_reason(state: Dictionary) -> String:
    var reason := str(state.get("edit_reason", "")).strip_edges()
    match reason:
        "normal_only":
            return _t("Available in normal mode only.", "仅支持普通模式。")
        "no_encounter":
            return _t("Enter an encounter to edit.", "进入遭遇战后可修改。")
        "wait_turn":
            return _t("Wait for your turn and animations to finish.", "请等待玩家回合及动画结束。")
    return reason


func _editing_unavailable(state: Dictionary) -> String:
    var reason := _edit_reason(state)
    var message := _t("Editing is unavailable right now.", "当前阶段无法修改。")
    return message if reason.is_empty() else message + " " + reason


func _refresh_permissions() -> void:
    for kind: String in _lists:
        var data: Dictionary = _lists[kind]
        var allowed := _collection_allowed(kind, _snapshot)
        if data.get("allowed", null) == allowed:
            continue
        data["allowed"] = allowed
        for tile: Node in data["catalog"].get_children():
            if tile is Button:
                tile.disabled = not allowed
        for tile: Node in data["current"].get_children():
            if tile is Button:
                tile.disabled = not allowed or tile.get_meta("locked", false)
    if not _menu_target.is_empty() and not _collection_allowed(_menu_target.kind, _snapshot):
        _hide_menu()
    if not _pick_target.is_empty() and not _collection_allowed(_pick_target.kind, _snapshot):
        _cancel_picker()


func _refresh_rows(kind: String) -> void:
    var data: Dictionary = _lists[kind]
    var entries: Array = _snapshot.get(data["state_key"], [])
    var signature := JSON.stringify(entries)
    if signature == data["signature"]:
        return
    data["signature"] = signature
    if _menu_target.get("kind", "") == kind:
        _hide_menu()
    var current: HFlowContainer = data["current"]
    _clear_children(current)
    var allowed := _collection_allowed(kind, _snapshot)
    for index in range(entries.size()):
        var entry: Dictionary = entries[index]
        var tile := _art_tile(current, kind, entry, "Owned_%s_%d" % [kind, index])
        tile.disabled = not allowed or tile.get_meta("locked", false)
        tile.gui_input.connect(_owned_input.bind(kind, index, entries, tile))
    if _is_cards(kind):
        var add := _button(current, "+", "+", _start_picker.bind(kind, "add", -1, []), false)
        add.name = "Add_" + kind
        add.theme_type_variation = "GlossTile"
        add.custom_minimum_size = Vector2(33, 38)
        add.tooltip_text = _t("Add a card", "添加卡牌")
        add.disabled = not allowed
    else:
        data["owned_caption"].visible = not entries.is_empty()
        current.visible = not entries.is_empty()


func _owned_input(event: InputEvent, kind: String, index: int, expected: Array, tile: Button) -> void:
    var right_click: bool = event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT and event.pressed
    var keyboard := event is InputEventKey and event.is_action_pressed("ui_accept")
    if not right_click and not keyboard:
        return
    tile.accept_event()
    if tile.disabled:
        return
    _menu_target = {"kind": kind, "index": index, "expected": expected}
    var rows: Node = _menu.get_child(0)
    rows.get_node("CollectionMenu_add").visible = _is_cards(kind)
    var remove: Button = rows.get_node("CollectionMenu_remove")
    remove.text = _t("Discard", "丢弃") if _is_cards(kind) else _t("Remove", "移除")
    _menu.show()
    _menu.reset_size()
    var anchor := tile.get_global_rect()
    var menu_size := _menu.get_combined_minimum_size()
    _menu.size = menu_size
    var xy := Vector2(anchor.end.x + 3, anchor.position.y)
    if xy.x + menu_size.x > _panel.size.x - 4:
        xy.x = anchor.position.x - menu_size.x - 3
    _menu.position = xy.clamp(Vector2(4, 4), (Vector2(_panel.size) - menu_size - Vector2(4, 4)).max(Vector2(4, 4)))
    if keyboard:
        remove.grab_focus()


func _hide_menu() -> void:
    _menu_target = {}
    if is_instance_valid(_menu):
        _menu.hide()


func _menu_action(verb: String) -> void:
    var target := _menu_target
    _hide_menu()
    if target.is_empty():
        return
    match verb:
        "remove":
            _collection_action(target.kind, verb, target.index, target.expected)
        "replace":
            _start_picker(target.kind, verb, target.index, target.expected)
        "add":
            _start_picker(target.kind, verb, -1, [])


func _start_picker(kind: String, verb: String, index: int, expected: Array) -> void:
    if not _collection_allowed(kind, _snapshot):
        return
    _cancel_picker()
    _pick_target = {"kind": kind, "verb": verb, "index": index, "expected": expected}
    _filter_catalog(kind)
    var page: ScrollContainer = _tabs.get_current_tab_control()
    page.ensure_control_visible.call_deferred(_lists[kind]["search"])


func _cancel_picker() -> void:
    if _pick_target.is_empty():
        return
    var kind: String = _pick_target.kind
    _pick_target = {}
    _filter_catalog(kind)


func _pick_entry(kind: String, id: String) -> void:
    if _pick_target.get("kind", "") == kind:
        if _collection_action(kind, _pick_target.verb, _pick_target.index, _pick_target.expected, id):
            _cancel_picker()
    elif not _is_cards(kind):
        if not _pick_target.is_empty():
            _feedback.text = _t("Finish or cancel the replacement first.", "请先完成或取消更换。")
            _feedback.show()
            return
        _collection_action(kind, "add", -1, [], id)


func _collection_action(kind: String, verb: String, index: int, expected: Array, id: String = "") -> bool:
    var fresh: Dictionary = _adapter.snapshot(get_tree())
    if not _collection_allowed(kind, fresh):
        _feedback.text = _editing_unavailable(fresh)
        _refresh()
        return false
    if index >= 0 and fresh.get(_lists[kind]["state_key"], []) != expected:
        _cancel_picker()
        _feedback.text = _t("The collection changed; select the icon again.", "列表已变化，请重新选择图标。")
        _refresh()
        return false
    if verb != "remove" and id.is_empty():
        return false
    var singular: String = {"cards": "card", "sleeve": "sleeve", "charms": "charm", "relics": "relic"}[kind]
    return _perform(verb + "_" + singular, null if verb == "remove" else id, index)


func _perform(action: String, value: Variant = null, index: int = -1) -> bool:
    if action in ["health", "shield", "heal", "chips"]:
        var fresh: Dictionary = _adapter.snapshot(get_tree())
        if not fresh.get("active", false) or not fresh.get("can_edit_vitals", false):
            _feedback.text = _editing_unavailable(fresh)
            _refresh()
            return false
    var error: String = _adapter.perform(get_tree(), action, value, index)
    var vital_action := "health" if action == "heal" else action
    if error.is_empty() and _numbers.has(vital_action):
        var state: Dictionary = _adapter.snapshot(get_tree())
        var draft: LineEdit = _numbers[vital_action]
        _sync_number(draft, str(state.get(vital_action, 0)))
    _feedback.text = _t("Applied.", "已应用。") if error.is_empty() else _t("Could not apply: ", "修改失败：") + error
    _refresh()
    return error.is_empty()




func _sync_number(draft: LineEdit, live: String) -> void:
    draft.set_meta("live_text", live)
    if draft.text != live:
        draft.text = live
    draft.set_meta("dirty", false)


func _refresh() -> void:
    _feedback.visible = not _feedback.text.is_empty()
    _snapshot = _adapter.snapshot(get_tree())
    _charm_count.text = _t("Charms: ", "幸运护符：") + str(_snapshot.get("charm_count", 0))
    _relic_count.text = _t("Unique chips: ", "独特筹码：") + str(_snapshot.get("unique_chips", 0))
    var active: bool = _snapshot.get("active", false)
    var reason := _edit_reason(_snapshot)
    for permission: String in _edit_reasons:
        if permission == "can_edit_vitals":
            continue
        var label: Label = _edit_reasons[permission]
        var allowed: bool = active and bool(_snapshot.get(permission, false))
        label.visible = not allowed or not reason.is_empty()
        label.text = _t("Editing: ", "修改条件：") + reason if not reason.is_empty() else _t("Wait for a stable player turn.", "请等待稳定的玩家操作回合。")
    _status.text = _t("Play: In progress", "游戏：进行中") if active else _t("Play: Waiting for a run", "游戏：等待开始")
    _status.text += "  ·  " + _t("HP", "生命") + " %s/%s" % [_snapshot.get("health", 0), _snapshot.get("max_health", 0)]
    var vital: bool = active and bool(_snapshot.get("can_edit_vitals", false))
    if not reason.is_empty():
        _status.text += " · " + reason
    elif not vital:
        _status.text += _t(" · Wait for player input.", " · 请等待玩家操作。")
    _status.tooltip_text = _status.text
    for button: Button in _vital_buttons:
        button.disabled = not vital
    for action: String in _numbers:
        var number: LineEdit = _numbers[action]
        number.editable = vital
        # Mouse-clicking Apply moves focus away before the pressed signal.
        # Keep the raw draft through the entire focus/commit transition.
        if not active or number.text == number.get_meta("live_text"):
            _sync_number(number, str(_snapshot.get(action, 0)) if active else "")
    _next.visible = _show_next
    _next.text = _t("Hover over the top-left label for details.", "悬停左上角牌名查看效果。")
    _next.tooltip_text = _next.text
    if _panel.visible and _tabs.current_tab in [1, 2]:
        var kinds := ["sleeve", "cards"] if _tabs.current_tab == 1 else [_item_kind]
        for kind: String in kinds:
            _refresh_rows(kind)
            _filter_catalog(kind)
    _refresh_permissions()


func _load_ui_font() -> FontFile:
    # Use the local Windows installation; never embed or redistribute the font.
    if OS.get_name() != "Windows":
        return null
    var windows_dir := OS.get_environment("WINDIR")
    if windows_dir.is_empty():
        return null
    var path := windows_dir.path_join("Fonts/msyh.ttc")
    if not FileAccess.file_exists(path):
        return null
    var font := FontFile.new()
    if font.load_dynamic_font(path) != OK:
        return null
    font.antialiasing = TextServer.FONT_ANTIALIASING_GRAY
    font.hinting = TextServer.HINTING_LIGHT
    font.subpixel_positioning = TextServer.SUBPIXEL_POSITIONING_AUTO
    return font


func _build_next_card_hud() -> void:
    _next_badge = PanelContainer.new()
    _next_badge.name = "NextCardBadge"
    _next_badge.position = NEXT_CARD_INSET
    _next_badge.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _next_badge.hide()
    add_child(_next_badge)
    _next_badge_label = Label.new()
    _next_badge_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _next_badge_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
    _next_badge.add_child(_next_badge_label)
    get_viewport().mouse_exited.connect(func() -> void: _next_pointer = Vector2.INF)
    get_viewport().size_changed.connect(func() -> void: _next_pointer = Vector2.INF)

    _next_detail = PanelContainer.new()
    _next_detail.name = "NextCardDetail"
    _next_detail.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _next_detail.hide()
    add_child(_next_detail)
    _next_detail_label = Label.new()
    _next_detail_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
    _next_detail_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
    _next_detail_label.custom_minimum_size.x = 176.0
    _next_detail.add_child(_next_detail_label)


func _hide_next_card() -> void:
    _next_badge.hide()
    _next_detail.hide()


func _set_next_card(info: Dictionary) -> void:
    var name := str(info.get("name", ""))
    if name.is_empty():
        _hide_next_card()
        return
    var caption := _t("Next card: ", "下一张：") + name
    if caption != _next_badge_label.text:
        _next_badge_label.text = caption
        var font := _next_badge_label.get_theme_font("font")
        var font_size := _next_badge_label.get_theme_font_size("font_size")
        var caption_width := font.get_string_size(caption, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
        _next_badge.size.x = clampf(ceilf(caption_width) + 14.0, 74.0, 190.0)
    var text := str(info.get("description", ""))
    if text.is_empty():
        text = _t("No special effect.", "无特殊效果。")
    if _next_detail_label.text != text:
        _next_detail_label.text = text
        _next_detail.reset_size()
    _next_badge.show()


func _update_next_card(delta: float) -> void:
    if not _show_next or _panel.visible or get_tree().paused:
        if _next_badge.visible:
            _hide_next_card()
        _next_card_time = NEXT_CARD_INTERVAL
        return
    _next_card_time += delta
    if _next_card_time >= NEXT_CARD_INTERVAL:
        _next_card_time = 0.0
        _set_next_card(_adapter.next_card_preview(get_tree(), _locale))
    if not _next_badge.visible:
        return
    var local_pointer := _next_badge.get_global_transform_with_canvas().affine_inverse() * _next_pointer if _next_pointer.is_finite() else Vector2.INF
    if Rect2(Vector2.ZERO, _next_badge.size).has_point(local_pointer):
        _next_detail.position = _next_badge.position + Vector2(0.0, _next_badge.size.y + 3.0)
        _next_detail.show()
    else:
        _next_detail.hide()


func _process(delta: float) -> void:
    _update_next_card(delta)
    _poll_time += delta
    if _poll_time < 0.25:
        return
    _poll_time = 0.0
    if _language == "auto":
        var locale := "zh" if TranslationServer.get_locale().to_lower().begins_with("zh") else "en"
        if locale != _locale:
            _refresh_language()
    if _panel.visible:
        _refresh()


func _input(event: InputEvent) -> void:
    # Observe motion before GUI hit-testing, without consuming game input.
    if event is InputEventMouseMotion and _show_next:
        _next_pointer = event.position
        return
    if event is InputEventKey:
        var key: int = event.physical_keycode if event.physical_keycode else event.keycode
        if _swallow_keys.has(key):
            get_viewport().set_input_as_handled()
            if not event.pressed:
                _swallow_keys.erase(key)
            return
        if key == _toggle_key:
            get_viewport().set_input_as_handled()
            if event.pressed and not event.echo:
                _swallow_keys[key] = true
                _set_open(not _panel.visible)


func _window_input(event: InputEvent) -> void:
    if event is InputEventMouseButton and event.pressed and _menu.visible:
        if not _menu.get_rect().has_point(event.position):
            _hide_menu()
    if not event is InputEventKey:
        return
    var key: int = event.physical_keycode if event.physical_keycode else event.keycode
    if _swallow_keys.has(key):
        _panel.set_input_as_handled()
        if not event.pressed:
            _swallow_keys.erase(key)
        return
    if _capturing_key:
        _panel.set_input_as_handled()
        if event.pressed and not event.echo:
            if key == KEY_ESCAPE:
                _capturing_key = false
            elif key not in [KEY_SHIFT, KEY_CTRL, KEY_ALT, KEY_META, KEY_NONE]:
                _toggle_key = key
                _capturing_key = false
                _save_settings()
            _swallow_keys[key] = true
            _update_shortcut()
        return
    # Let text fields use a rebound letter normally; Escape remains a way out.
    var focus := _panel.gui_get_focus_owner()
    var typing := focus is LineEdit or focus is TextEdit
    if key == KEY_ESCAPE and event.pressed and not event.echo and (_menu.visible or not _pick_target.is_empty()):
        _panel.set_input_as_handled()
        _swallow_keys[key] = true
        if _menu.visible:
            _hide_menu()
        else:
            _cancel_picker()
        return
    if key == KEY_ESCAPE or (key == _toggle_key and (key == KEY_INSERT or not typing)):
        _panel.set_input_as_handled()
        if event.pressed and not event.echo:
            _swallow_keys[key] = true
            _set_open.call_deferred(false)


func _panel_pointer_input(event: InputEvent, hit_area: Control, resize: bool) -> void:
    if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
        hit_area.accept_event()
        _panel_dragging = false
        if not event.pressed:
            return
        if not _panel.is_embedded():
            if resize:
                _panel.start_resize(DisplayServer.WINDOW_EDGE_BOTTOM_RIGHT)
            else:
                _panel.start_drag()
            return
        _panel_dragging = true
        _drag_rect = Rect2i(_panel.position, _panel.size)
        _drag_pointer = Vector2(_panel.position) + event.global_position
    elif event is InputEventMouseMotion and _panel_dragging:
        if not (event.button_mask & MOUSE_BUTTON_MASK_LEFT):
            _panel_dragging = false
            return
        hit_area.accept_event()
        # GUI capture keeps delivering motion to the pressed control. Reconstruct
        # the parent-space pointer from the *current* subwindow origin each time.
        var delta := Vector2i((Vector2(_panel.position) + event.global_position - _drag_pointer).round())
        var bounds := _panel_bounds()
        if resize:
            var available := (bounds.end - _drag_rect.position).max(PANEL_MIN_SIZE)
            _panel.size = (_drag_rect.size + delta).clamp(PANEL_MIN_SIZE, available)
        else:
            _panel.position = (_drag_rect.position + delta).clamp(bounds.position,
                (bounds.end - _panel.size).max(bounds.position))


func _panel_bounds() -> Rect2i:
    var viewport := get_viewport()
    var bounds := Rect2i(viewport.get_visible_rect())
    if bounds.size.x <= 0 or bounds.size.y <= 0:
        if viewport is Window and viewport.content_scale_size != Vector2i.ZERO:
            return Rect2i(Vector2i.ZERO, viewport.content_scale_size)
        return Rect2i(Vector2i.ZERO, Vector2i(640, 360))
    return bounds


func _fit_panel_to_viewport() -> void:
    if not _panel.visible or not _panel.is_embedded():
        return
    _panel_dragging = false
    _hide_menu()
    var bounds := _panel_bounds()
    _panel.size = _panel.size.min(bounds.size.max(PANEL_MIN_SIZE))
    _panel.position = _panel.position.clamp(bounds.position, (bounds.end - _panel.size).max(bounds.position))


func _set_open(open: bool) -> void:
    if _panel.visible == open:
        return
    if open:
        _hide_next_card()
        _was_paused = get_tree().paused
        _mouse_mode = Input.mouse_mode
        get_tree().paused = true
        Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
        if not _panel_opened:
            var bounds := _panel_bounds() if get_viewport().gui_embed_subwindows else DisplayServer.screen_get_usable_rect(get_viewport().current_screen)
            _panel.size = PANEL_INITIAL_SIZE
            _panel.position = Vector2i(maxi(bounds.position.x, bounds.end.x - PANEL_INITIAL_SIZE.x - PANEL_INSET.x), bounds.position.y + PANEL_INSET.y)
        _panel.popup()
        _panel_opened = true
        _fit_panel_to_viewport()
        _navigation[_tabs.current_tab].grab_focus()
    else:
        _capturing_key = false
        _panel_dragging = false
        _hide_menu()
        _cancel_picker()
        for draft: LineEdit in _numbers.values():
            _sync_number(draft, "")
        _panel.hide()
        _next_pointer = get_viewport().get_mouse_position()
        get_tree().paused = _was_paused
        Input.mouse_mode = _mouse_mode
        _next_card_time = NEXT_CARD_INTERVAL
        _update_shortcut()
    _refresh()


func _update_shortcut() -> void:
    var key := OS.get_keycode_string(_toggle_key)
    _shortcut.text = _t("Press a key… (Esc cancels)", "请按键…（Esc 取消）") if _capturing_key else key
    _footer_keycap.text = key
    _footer.text = _t("Toggle · Esc close", "显示/隐藏 · Esc 关闭")


func _build_settings(parent: Node) -> void:
    parent = _card(parent)
    var theme_row := _row(parent)
    _label(theme_row, "Theme", "主题")
    _themes = OptionButton.new()
    theme_row.add_child(_themes)
    _themes.add_item("Dark")
    _themes.add_item("Light")
    _themes.select(0 if _dark else 1)
    _themes.item_selected.connect(func(index: int) -> void:
        _dark = index == 0
        _apply_theme()
        _save_settings()
    )
    var language_row := _row(parent)
    _label(language_row, "Language", "语言")
    var languages := OptionButton.new()
    language_row.add_child(languages)
    for title: String in ["Auto / 自动", "English", "简体中文"]:
        languages.add_item(title)
    languages.select(["auto", "en", "zh"].find(_language))
    languages.item_selected.connect(func(index: int) -> void:
        _language = ["auto", "en", "zh"][index]
        _refresh_language()
        _refresh()
        _save_settings()
    )
    var key_row := _row(parent)
    _label(key_row, "Toggle shortcut", "显示/隐藏快捷键")
    _shortcut = _button(key_row, "Insert", "Insert", func() -> void:
        _capturing_key = true
        _update_shortcut()
    )
    _button(key_row, "Reset", "重置", func() -> void:
        _toggle_key = KEY_INSERT
        _capturing_key = false
        _update_shortcut()
        _save_settings()
    )
    _label(parent, "The game pauses while this panel is open.", "面板打开期间游戏暂停。")
    parent.add_child(HSeparator.new())
    _label(parent, "About GlossTrainer", "关于 GlossTrainer")
    _label(parent, "By: 小莫 · GPL-3.0 · Game-specific trainer modules", "作者：小莫 · GPL-3.0 · 每款游戏独立适配")
    var version := str(ProjectSettings.get_setting("gloss_trainer/version", ""))
    _label(parent, "DnDG version: " + (version if not version.is_empty() else "development"), "DnDG 版本：" + (version if not version.is_empty() else "开发版"))
    var links := _row(parent)
    _button(links, "Source", "源码", func() -> void: OS.shell_open(REPOSITORY))
    _button(links, "Sponsor", "赞助", func() -> void: OS.shell_open("https://sponsor.aoe.top/"))
    _button(links, "Requests", "委托制作", func() -> void: OS.shell_open("https://www.glossmod.com/commissions"))
    _update_button = _button(parent, "Check DnDG updates", "检查 DnDG 更新", _check_updates)
    _update_label = _label(parent, "Not checked", "尚未检查")
    _button(parent, "Open releases", "打开发行版", func() -> void: OS.shell_open(REPOSITORY + "/releases"))


func _box(background: Color, border: Color, radius: int = 5) -> StyleBoxFlat:
    var box := StyleBoxFlat.new()
    box.bg_color = background
    box.border_color = border
    box.set_border_width_all(1)
    box.set_corner_radius_all(radius)
    box.content_margin_left = 5
    box.content_margin_right = 5
    box.content_margin_top = 2
    box.content_margin_bottom = 2
    return box


func _apply_theme() -> void:
    # Shared GlossTrainer palette from BlackMythWukong / PhantomBladeZero.
    var background := Color("17191e") if _dark else Color("f4f5f7")
    var card := Color("202329") if _dark else Color.WHITE
    var field := Color("2d323b") if _dark else Color("ebedf2")
    var foreground := Color("ebeef3") if _dark else Color("20242b")
    var muted := Color("8c95a4") if _dark else Color("707884")
    var border := Color("353b45") if _dark else Color("dadee5")
    var accent := Color("c6a56b") if _dark else Color("9c793d")
    var button_fill := Color("312e29") if _dark else Color("f2ede5")
    var button_hover := Color("433b2d") if _dark else Color("e7dece")
    var selected := Color("51442f") if _dark else Color("ded1b9")
    var theme := Theme.new()
    theme.default_font_size = PANEL_FONT_SIZE
    if _ui_font != null:
        theme.default_font = _ui_font
    # Explicit sizes also cover Godot's internal tab/popup controls and built-in tooltip labels.
    theme.set_stylebox("panel", "TooltipPanel", _box(background, border))
    theme.set_color("font_color", "TooltipLabel", foreground)
    theme.set_type_variation("GlossSection", "PanelContainer")
    theme.set_type_variation("GlossNavigation", "Button")
    theme.set_type_variation("GlossClose", "Button")
    theme.set_type_variation("GlossTile", "Button")
    theme.set_stylebox("panel", "GlossSection", _box(card, border, 5))
    for type: String in ["Label", "Button", "LineEdit", "SpinBox", "OptionButton", "CheckButton", "ItemList", "PopupMenu", "TabBar", "TabContainer", "TooltipLabel"]:
        if _ui_font != null:
            theme.set_font("font", type, _ui_font)
        theme.set_font_size("font_size", type, PANEL_FONT_SIZE)
        theme.set_color("font_color", type, foreground)
        theme.set_color("font_hover_color", type, foreground)
        theme.set_color("font_pressed_color", type, foreground)
        theme.set_color("font_hover_pressed_color", type, foreground)
        theme.set_color("font_focus_color", type, foreground)
        theme.set_color("font_disabled_color", type, muted)
    for type: String in ["Button", "OptionButton"]:
        theme.set_stylebox("normal", type, _box(button_fill, button_fill, 4))
        theme.set_stylebox("hover", type, _box(button_hover, border, 4))
        theme.set_stylebox("pressed", type, _box(selected, accent, 4))
        theme.set_stylebox("disabled", type, _box(card, border, 4))
        theme.set_stylebox("focus", type, _box(Color.TRANSPARENT, accent))
    var navigation_selected := _box(selected, selected, 4)
    navigation_selected.border_color = accent
    navigation_selected.set_border_width_all(0)
    navigation_selected.border_width_left = 2
    theme.set_stylebox("normal", "GlossNavigation", _box(background, background, 4))
    theme.set_stylebox("hover", "GlossNavigation", _box(button_fill, button_fill, 4))
    theme.set_stylebox("pressed", "GlossNavigation", navigation_selected)
    theme.set_color("font_pressed_color", "GlossNavigation", accent)
    theme.set_color("icon_normal_color", "GlossNavigation", foreground)
    theme.set_color("icon_focus_color", "GlossNavigation", foreground)
    theme.set_color("icon_hover_color", "GlossNavigation", foreground)
    theme.set_color("icon_pressed_color", "GlossNavigation", accent)
    theme.set_color("icon_hover_pressed_color", "GlossNavigation", accent)
    theme.set_color("font_hover_pressed_color", "GlossNavigation", accent)
    theme.set_constant("h_separation", "GlossNavigation", 6)
    for state: String in ["normal", "hover", "pressed", "disabled", "focus"]:
        var tile_style: StyleBoxFlat = theme.get_stylebox(state, "Button").duplicate()
        tile_style.content_margin_left = 2
        tile_style.content_margin_right = 2
        theme.set_stylebox(state, "GlossTile", tile_style)
    theme.set_color("icon_disabled_color", "GlossTile", Color(1, 1, 1, 0.75))
    theme.set_stylebox("normal", "GlossClose", _box(background, background, 3))
    for state: String in ["normal", "hover", "pressed", "disabled"]:
        var switch_background := _box(Color.TRANSPARENT, Color.TRANSPARENT, 0)
        switch_background.content_margin_left = 0
        switch_background.content_margin_right = 0
        switch_background.content_margin_top = 2
        switch_background.content_margin_bottom = 2
        theme.set_stylebox(state, "CheckButton", switch_background)
    theme.set_icon("checked", "CheckButton", _switch_icon(true, accent, Color.WHITE))
    theme.set_icon("unchecked", "CheckButton", _switch_icon(false, field, muted))
    for type: String in ["LineEdit"]:
        theme.set_stylebox("normal" if type == "LineEdit" else "panel", type, _box(field, border))
        theme.set_stylebox("focus", type, _box(Color.TRANSPARENT, accent))
        theme.set_color("font_placeholder_color", type, muted)
        theme.set_color("selection_color", type, accent.darkened(0.3))
    theme.set_color("font_color_uneditable", "LineEdit", muted)
    theme.set_stylebox("read_only", "LineEdit", _box(field, border))
    theme.set_color("font_uneditable_color", "LineEdit", muted)
    var content := StyleBoxEmpty.new()
    theme.set_stylebox("panel", "TabContainer", content)
    theme.set_stylebox("panel", "PopupMenu", _box(background, border))
    var horizontal_rule := StyleBoxLine.new()
    horizontal_rule.color = border
    horizontal_rule.thickness = 1
    theme.set_stylebox("separator", "HSeparator", horizontal_rule)
    theme.set_constant("separation", "HSeparator", 1)
    var vertical_rule := StyleBoxLine.new()
    vertical_rule.color = border
    vertical_rule.thickness = 1
    vertical_rule.vertical = true
    theme.set_stylebox("separator", "VSeparator", vertical_rule)
    theme.set_constant("separation", "VSeparator", 1)
    _panel.theme = theme
    _surface.theme = theme
    _next_badge.theme = theme
    _next_detail.theme = theme
    _next_badge.add_theme_stylebox_override("panel", _box(background, accent))
    _next_detail.add_theme_stylebox_override("panel", _box(background, border))
    _surface.add_theme_stylebox_override("panel", _box(background, border, 6))
    _brand_accent.add_theme_color_override("font_color", accent)
    _header_version.add_theme_color_override("font_color", muted)
    _footer.add_theme_color_override("font_color", muted)
    var grip_image := Image.new()
    grip_image.load_svg_from_string('<svg xmlns="http://www.w3.org/2000/svg" width="11" height="11"><path d="M1 9l8-8m-4 9l5-5" fill="none" stroke="#%s" stroke-width="1.25" stroke-linecap="round"/></svg>' % muted.to_html(false))
    _resize_grip.texture = ImageTexture.create_from_image(grip_image)
    var keycap := _box(field, border, 3)
    keycap.content_margin_left = 5
    keycap.content_margin_right = 5
    keycap.content_margin_top = 1
    keycap.content_margin_bottom = 1
    _footer_keycap.add_theme_stylebox_override("normal", keycap)
    for i in range(_navigation.size()):
        _navigation[i].icon = _navigation_icon(i, Color.WHITE)


func _update_status(en: String, zh: String) -> void:
    _update_message = [en, zh]
    _update_label.text = _t(en, zh)


func _check_updates() -> void:
    if _update_page > 0:
        return
    _best_release = {}
    _update_page = 1
    _update_button.disabled = true
    _update_status("Checking DnDG releases…", "正在检查 DnDG 发行版…")
    _request_release_page()


func _request_release_page() -> void:
    var error := _http.request(RELEASE_API + "?per_page=%d&page=%d" % [PAGE_SIZE, _update_page],
        ["Accept: application/vnd.github+json", "User-Agent: GlossTrainer-DnDG"])
    if error != OK:
        _finish_update(false)


func _tag_version(tag: String) -> PackedInt64Array:
    var regex := RegEx.new()
    regex.compile("^DnDG/v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    var result := regex.search(tag)
    if result == null:
        return PackedInt64Array()
    return PackedInt64Array([int(result.get_string(1)), int(result.get_string(2)), int(result.get_string(3))])


func _newer(left: PackedInt64Array, right: PackedInt64Array) -> bool:
    if right.is_empty():
        return not left.is_empty()
    for i in range(3):
        if left[i] != right[i]:
            return left[i] > right[i]
    return false


func _release_response(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
    if result != HTTPRequest.RESULT_SUCCESS or code != 200:
        _finish_update(false)
        return
    var releases: Variant = JSON.parse_string(body.get_string_from_utf8())
    if not releases is Array:
        _finish_update(false)
        return
    for release: Variant in releases:
        if not release is Dictionary or release.get("draft", false) or release.get("prerelease", false):
            continue
        var version := _tag_version(str(release.get("tag_name", "")))
        if not version.is_empty() and _newer(version, _tag_version(str(_best_release.get("tag_name", "")))):
            _best_release = release
    if releases.size() == PAGE_SIZE:
        if _update_page >= 20:
            # Do not claim a complete check if the bounded pagination was exhausted.
            _finish_update(false)
            return
        _update_page += 1
        _request_release_page.call_deferred()
        return
    _finish_update(true)


func _finish_update(success: bool) -> void:
    _update_page = 0
    _update_button.disabled = false
    if not success:
        _update_status("Update check failed. Try again later.", "检查更新失败，请稍后重试。")
    elif _best_release.is_empty():
        _update_status("No DnDG release published yet.", "暂未发布 DnDG 版本。")
    else:
        var tag := str(_best_release["tag_name"])
        var current := str(ProjectSettings.get_setting("gloss_trainer/version", ""))
        if current.begins_with("v"):
            current = current.substr(1)
        var current_version := _tag_version("DnDG/v" + current)
        if current_version.is_empty():
            _update_status("Published DnDG release: " + tag, "已发布 DnDG 版本：" + tag)
        elif _newer(_tag_version(tag), current_version):
            _update_status("Update available: " + tag, "发现新版本：" + tag)
        else:
            _update_status("DnDG is up to date (" + tag + ").", "DnDG 已是最新版本（" + tag + "）。")
