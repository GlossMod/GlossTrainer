task("bi")
    on_run("build_and_install")
    set_menu {
        usage = "xmake bi [options]",
        description = "Build and safely install Black Myth: Wukong GlossTrainer (or uninstall owned files)",
        options = 
        {
            {'d', 'exedir', "kv", "E:\\steam\\steamapps\\common\\BlackMythWukong\\b1\\Binaries\\Win64", "Existing game executable folder"},
            {nil, 'name', "kv", "BlackMythWukong", "Target to build and install"},
            {nil, 'dry-run', "k", nil, "Validate paths and preview without changing game files"},
            {nil, 'uninstall', "k", nil, "Remove only unchanged files owned by the installer; preserve settings/logs"},
        }
    }
