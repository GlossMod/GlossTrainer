extends SceneTree

const Overlay = preload("res://gloss_trainer/trainer_overlay.gd")

var _checks := 0
var _failures := 0


func _initialize() -> void:
    _run.call_deferred()


func _check(condition: bool, message: String) -> void:
    _checks += 1
    if not condition:
        _failures += 1
        push_error(message)


func _find_tooltip(parent: Node, text: String) -> Label:
    for child: Node in parent.get_children(true):
        if child is PopupPanel and child.theme_type_variation == "TooltipPanel":
            for content: Node in child.get_children(true):
                if content is Label and content.text == text:
                    return content as Label
        var nested := _find_tooltip(child, text)
        if nested != null:
            return nested
    return null


func _hover_tooltip(panel: Window, control: Control) -> Label:
    var motion := InputEventMouseMotion.new()
    motion.position = Vector2(panel.position) + control.get_global_rect().get_center()
    motion.global_position = motion.position
    root.push_input(motion, true)
    await create_timer(0.8).timeout
    return _find_tooltip(panel, control.tooltip_text)


func _has_cjk_glyphs(label: Label, text: String) -> bool:
    var line := TextLine.new()
    var font := label.get_theme_font("font")
    if not line.add_string(text, font, label.get_theme_font_size("font_size"), "zh"):
        return false
    var glyphs := TextServerManager.get_primary_interface().shaped_text_get_glyphs(line.get_rid())
    if glyphs.size() != text.length():
        return false
    for glyph: Dictionary in glyphs:
        if not glyph["font_rid"].is_valid() or glyph["index"] <= 0 or glyph["advance"] <= 0:
            return false
    return true


func _capture(name: String) -> void:
    if DisplayServer.get_name() == "headless":
        return
    await RenderingServer.frame_post_draw
    _check(root.get_texture().get_image().save_png("res://typography-" + name + ".png") == OK,
        "fixture screenshot saved: " + name)


func _check_panel(overlay: Overlay, locale: String, dark: bool) -> bool:
    var variant := ("dark" if dark else "light") + "-" + locale
    overlay._dark = dark
    overlay._language = locale
    overlay._refresh_language()
    overlay._apply_theme()
    overlay._navigate(0)
    await process_frame
    await process_frame
    var panel := overlay._panel
    var wordmark := overlay._brand_accent.get_parent()
    for part: Label in wordmark.get_children():
        _check(part.get_theme_font_size("font_size") == 13, variant + ": wordmark size")
    for auxiliary: Label in [overlay._header_version, overlay._footer_keycap, overlay._footer]:
        _check(auxiliary.get_theme_font_size("font_size") == 9, variant + ": auxiliary size")
    for control: Control in [overlay._status, overlay._numbers["health"], overlay._themes,
            overlay._tabs, overlay._tabs.get_tab_bar(), overlay._menu.find_child("CollectionMenu_remove", true, false)]:
        _check(control.get_theme_font_size("font_size") == 10, variant + ": body font for " + str(control))
    if overlay._ui_font != null:
        for control: Control in [overlay._status, overlay._numbers["health"], overlay._navigation[0], overlay._footer]:
            _check(control.get_theme_font("font") == overlay._ui_font, variant + ": trainer control uses YaHei")
        _check(overlay._themes.get_popup().get_theme_font("font") == overlay._ui_font, variant + ": popup uses YaHei")
        _check(overlay._next_badge_label.get_theme_font("font") == overlay._ui_font and
            overlay._next_detail_label.get_theme_font("font") == overlay._ui_font, variant + ": next-card HUD uses YaHei")
    var popup := overlay._themes.get_popup()
    _check(popup.get_theme_font_size("font_size") == 10, variant + ": internal dropdown menu size")
    _check(popup.get_item_text(0) == ("暗色" if locale == "zh" else "Dark"), variant + ": menu localization")
    _check(overlay._tabs.get_tab_title(0) == ("玩家" if locale == "zh" else "Player"), variant + ": tab localization")
    var close: Button = panel.find_child("ClosePanel", true, false)
    _check(close.custom_minimum_size == Vector2(20, 22) and overlay._resize_grip.custom_minimum_size == Vector2(18, 16),
        variant + ": close and resize targets retain their minima")
    for navigation: Button in overlay._navigation:
        _check(navigation.get_theme_font_size("font_size") == 10 and navigation.size.y >= 22,
            variant + ": visible tab label size and hit height")
    _check(Vector2i(286, 222) == panel.size and Vector2i(252, 183) == panel.min_size, variant + ": panel bounds")
    if locale == "zh":
        _check(_has_cjk_glyphs(overlay._status, "玩家生命护盾筹码预见"), variant + ": CJK body glyphs shape at 10 px")
        _check(_has_cjk_glyphs(overlay._footer, "显示隐藏关闭"), variant + ": CJK footer glyphs shape at 9 px")
    await _capture(variant)
    return true


func _run() -> void:
    await process_frame
    # Simulate a game theme with different sizes; the trainer must not change it.
    var stock := Theme.new()
    for type: String in ["Label", "Button", "TabBar", "PopupMenu", "TooltipLabel"]:
        stock.set_font_size("font_size", type, 19)
    root.theme = stock
    var stock_label := Label.new()
    stock_label.text = "DnDG · typography fixture"
    stock_label.position = Vector2(16, 16)
    root.add_child(stock_label)
    var overlay := root.get_node("GlossTrainer") as Overlay
    var path := OS.get_environment("WINDIR").path_join("Fonts/msyh.ttc")
    if OS.get_name() == "Windows" and FileAccess.file_exists(path):
        _check(overlay._ui_font != null and overlay._ui_font.get_font_name() == "Microsoft YaHei",
            "real installed Microsoft YaHei is loaded, not a fallback font")
        _check(overlay._ui_font.antialiasing == TextServer.FONT_ANTIALIASING_GRAY,
            "trainer uses grayscale antialiasing regardless of game settings")
    overlay._set_open(true)
    for dark: bool in [true, false]:
        for locale: String in ["en", "zh"]:
            if not await _check_panel(overlay, locale, dark):
                _check(false, "Panel checks did not complete")
    var panel := overlay._panel
    var tip := await _hover_tooltip(panel, overlay._resize_grip)
    _check(tip != null and tip.get_theme_font_size("font_size") == 10, "engine-created resize tooltip is 10 px")
    if tip != null:
        _check(tip.get_theme_color("font_color") == panel.get_theme_color("font_color", "TooltipLabel"),
            "built-in tooltip uses the panel's matching light-theme text color")
        var tip_style := (tip.get_parent() as PopupPanel).get_theme_stylebox("panel") as StyleBoxFlat
        _check(tip_style != null and tip_style.bg_color.a == 1.0, "tooltip background is opaque over artwork")
        if overlay._ui_font != null:
            _check(tip.get_theme_font("font") == overlay._ui_font, "built-in tooltip renders in YaHei")
        _check(_has_cjk_glyphs(tip, "拖动调整窗口大小"), "Chinese resize tooltip has glyphs")
    await _capture("light-zh-tooltip")
    overlay._navigate(2)
    var item_page := overlay._tabs.get_current_tab_control() as ScrollContainer
    var tile: Button = overlay._art_tile(item_page.get_child(0), "charms", {"name": "测试护符"}, "TypographyTile")
    await process_frame
    await process_frame
    item_page.ensure_control_visible(tile)
    await process_frame
    var art_tip := await _hover_tooltip(panel, tile)
    _check(art_tip != null and art_tip.get_theme_font_size("font_size") == 10, "custom artwork tooltip is 10 px")
    if art_tip != null:
        if overlay._ui_font != null:
            _check(art_tip.get_theme_font("font") == overlay._ui_font, "art tooltip renders in YaHei")
        _check(art_tip.get_theme_color("font_color") == panel.get_theme_color("font_color", "TooltipLabel"),
            "custom tooltip uses the same text color as the built-in tooltip")
        _check(art_tip.custom_minimum_size.x == 154 and _has_cjk_glyphs(art_tip, tile.tooltip_text),
            "custom tooltip retains its wrapping width and CJK glyphs")
    if overlay._ui_font != null:
        _check(stock_label.get_theme_font("font") != overlay._ui_font, "stock UI does not inherit the trainer font")
    _check(stock_label.get_theme_font_size("font_size") == 19, "stock label outside trainer retains its theme")
    _check(stock_label.get_theme_font_size("font_size", "TooltipLabel") == 19, "stock tooltip retains its theme")
    overlay._set_open(false)
    print("DnDG overlay typography: %d checks, %d failures" % [_checks, _failures])
    quit(1 if _failures else 0)
