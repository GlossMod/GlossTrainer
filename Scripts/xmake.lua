task("bi")
    on_run("build_and_install")
    set_menu {
        usage = "xmake bi [options]",
        description = "Build and install a mod. Equivalent to 'xmake build' followed by 'xmake installmod'",
        options = 
        {
            {'d', 'exedir', "v", nil, "Path of the game's executable folder"},
            {nil, 'name', "v", nil, "Name of the mod to build and install."},
        }
    }