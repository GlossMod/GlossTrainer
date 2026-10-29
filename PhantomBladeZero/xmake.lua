add_rules("mode.debug", "mode.release")
add_requires("imgui 1.92.9", {configs = {win32 = true, dx11 = true, dx12 = true}})
add_requires("minhook 1.3.4")
add_requires("nlohmann_json 3.12.0")

target("PBZ")
    set_kind("shared")
    set_basename("GlossTrainer")
    set_arch("x64")
    add_files("src/*.cpp")
    add_files("src/gameplay/*.cpp")
    set_targetdir(path.join(os.projectdir(), "build", "$(mode)"))
    set_extension(".asi")
    add_packages("imgui", "minhook", "nlohmann_json")
    set_languages("cxx20")
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "UNICODE", "_UNICODE")
    add_cxxflags("/utf-8", "/EHsc", {tools = "cl"})
    add_syslinks("d3d11", "d3d12", "dxgi", "dxguid", "d3dcompiler", "user32", "gdi32", "imm32", "dwmapi", "shell32", "bcrypt", "winhttp")

includes("validation")

-- The optional graphics host is a local, Git-ignored development tool.
if os.isfile(path.join(os.projectdir(), "Tests", "host.cpp")) then
    target("PBZHost")
        set_default(false)
        set_kind("binary")
        set_basename("GlossTrainer.PBZ.Host")
        add_files("../Tests/host.cpp")
        set_languages("cxx20")
        set_targetdir(path.join(os.projectdir(), "build", "$(mode)"))
        add_defines("UNICODE", "_UNICODE", "_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
        add_syslinks("d3d11", "d3d12", "dxgi", "user32", "gdi32", "shell32")
        add_cxxflags("/W4", "/permissive-", {tools = "cl"})
end
