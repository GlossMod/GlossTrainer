option("game")
    set_default("BlackMythWukong")
    set_showmenu(true)
    set_description("Build only this registered game module")
    after_check(function (option)
        import("core.base.json")
        local selected = option:value() or "BlackMythWukong"
        assert(selected:match("^[A-Za-z][A-Za-z0-9]*$"), "Invalid game ID")
        local registry = json.loadfile(path.join(os.projectdir(), "Scripts", "release-games.json"))
        assert(registry and registry.schemaVersion == 1, "Invalid release game registry")
        local registered = false
        for _, entry in ipairs(registry.games) do
            if entry.id == selected then registered = true end
        end
        assert(registered, "Unknown registered game: " .. selected)
        assert(os.isfile(path.join(os.projectdir(), selected, "xmake.lua")), "Game build module is missing: " .. selected)
    end)
option_end()

local game = get_config("game") or "BlackMythWukong"
includes("Scripts")
-- Guard the include before the sandboxed option check can report invalid IDs.
if game:match("^[A-Za-z][A-Za-z0-9]*$") and os.isfile(path.join(game, "xmake.lua")) then
    includes(game)
end
-- The graphics host is a local, Git-ignored development tool.
if game == "BlackMythWukong" and os.isfile("Tests/xmake.lua") then
    includes("Tests")
end
