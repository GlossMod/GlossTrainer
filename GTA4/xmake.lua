
add_rules("mode.debug", "mode.release")

add_requires("imgui", {  configs = { win32 = true, dx11 = true, opengl3 = true, glfw_opengl3 = true} })
add_requires("minhook")

target("GTA4")
    set_kind("shared")
    add_files("src/**.cpp")
    set_targetdir("build/$(mode)")
    set_extension(".asi")               -- 设置后缀名为 .asi
    add_packages("imgui", "minhook")
    set_languages("cxx20")
    add_links("D3D11")                  -- 引入 D3D11.lib
    -- set_arch("x86")                     -- 32位游戏