add_rules("mode.debug", "mode.release")

add_requires("imgui")
add_requires("minhook")

target("BlackMythWukong")
    set_kind("shared")
    add_files("src/**.cpp")
    set_targetdir("build/$(mode)")
    set_extension(".asi")               -- 设置后缀名为 .asi
    add_packages("imgui", "minhook")
    set_languages("cxx20")