import("core.base.option")
import("core.base.task")
import("core.project.config")
import("core.project.project")

function main()
    local game_dir = option.get("exedir")
    local name = option.get("name")
    assert(name == "BlackMythWukong", "--name must be BlackMythWukong")
    assert(game_dir and path.is_absolute(game_dir), "--exedir must be an existing absolute game executable directory")
    assert(os.isdir(game_dir), "game executable directory does not exist: %s", game_dir)
    assert(os.isfile(path.join(game_dir, "b1-Win64-Shipping.exe")),
        "b1-Win64-Shipping.exe was not found in --exedir: %s", game_dir)
    local installer = path.join(os.projectdir(), "Scripts", "install.ps1")
    local arguments = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", installer, "-ExeDir", path.absolute(game_dir)}
    if option.get("uninstall") then
        table.insert(arguments, "-Uninstall")
    else
        task.run("build", {yes = true, targets = {name}})
        local target = assert(project.target(name), "unknown project target: " .. name)
        table.insert(arguments, "-Artifact")
        table.insert(arguments, path.absolute(target:targetfile()))
    end
    if option.get("dry-run") then
        table.insert(arguments, "-DryRun")
    end
    os.execv("powershell.exe", arguments)
end
