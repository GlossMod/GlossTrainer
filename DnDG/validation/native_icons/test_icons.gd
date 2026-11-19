extends SceneTree

var failures := 0
var count := 0

func _initialize() -> void:
    _test.call_deferred()

func _check(condition: bool, label: String) -> void:
    count += 1
    if not condition:
        failures += 1
        push_error(label)

func _test() -> void:
    if not ClassDB.can_instantiate("NativeIconProbe"):
        push_error("native icon extension missing")
        quit(1)
        return
    var probe: Node = ClassDB.instantiate("NativeIconProbe")
    root.add_child(probe)

    var image := Image.create_empty(32, 16, false, Image.FORMAT_RGBA8)
    image.fill(Color.RED)
    image.fill_rect(Rect2i(16, 0, 16, 16), Color.BLUE)
    var row: Dictionary = probe.pixels(image, Rect2i(16, 1, 20, 5), true)
    _check(row.size == Vector2i(16, 5) and row.bytes == 16 * 5 * 4 and row.first == Color.BLUE,
        "crop is clipped to atlas bounds and returns the intended pixels")
    row = probe.pixels(image, Rect2i(64, 64, 2, 2), true)
    _check(row.size == Vector2i.ZERO and row.bytes == 0, "empty atlas intersection is rejected")
    _check(image.compress(Image.COMPRESS_S3TC) == OK and image.is_compressed(), "fixture source is compressed")
    row = probe.pixels(image, Rect2i(16, 0, 16, 16), true)
    _check(row.size == Vector2i(16, 16) and row.bytes == 1024 and row.first.b > 0.9 and row.first.r < 0.1,
        "compressed atlas is decoded before its region is cropped")

    var large := Image.create_empty(124, 62, true, Image.FORMAT_RGB8)
    large.fill(Color.RED)
    row = probe.pixels(large, Rect2i(), false)
    _check(row.size == Vector2i(62, 31) and row.bytes == 62 * 31 * 4 and row.first == Color.RED,
        "proportional icon downscale and RGBA8 conversion exclude mipmaps")
    row = probe.pixels(Image.new(), Rect2i(), false)
    _check(row.size == Vector2i.ZERO and row.bytes == 0, "empty input is rejected")

    var arguments := OS.get_cmdline_user_args()
    if arguments.size() != 1 or not ProjectSettings.load_resource_pack(arguments[0], false):
        push_error("could not mount the original resource pack read-only")
        quit(1)
        return
    var samples := {
        "res://assets/art/card_art/126.png": [Vector2i(57, 89), Vector2i(39, 62)],
        "res://assets/art/lucky_charms/1.png": [Vector2i(26, 26), Vector2i(26, 26)],
        "res://relics/relic_sprite_frames_001.tres": [Vector2i(47, 64), Vector2i(45, 62)],
    }
    for path: String in samples:
        var loaded: Vector2i = probe.loaded_size(path)
        _check(loaded == samples[path][0], "original icon resolves: " + path)
        row = probe.game_pixels(path)
        var encoded: Vector2i = samples[path][1]
        _check(row.size == encoded and row.bytes == encoded.x * encoded.y * 4 and row.visible and row.varied,
            "production reader yields visible RGBA pixels for " + path)
        print(path, ": source=", loaded, ", encoded=", row.size, ", bytes=", row.bytes)
    row = probe.game_pixels("res://assets/art/card_art/no-such-icon.png")
    _check(row.size == Vector2i.ZERO and row.bytes == 0, "missing asset does not enter the texture atlas")
    print("DnDG native icon fixture: ", count - failures, "/", count, " passed")
    quit(1 if failures else 0)
