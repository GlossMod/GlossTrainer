import("core.base.option")
import("core.base.task")
import("core.project.config")
import("core.project.project")

function main()
    task.run("build", { yes = {} }, option.get("name"))
    installmod()
end

-- 安装
function installmod()
    -- 获取游戏目录
    local game_dir = option.get("exedir")
    local name = option.get("name")

    -- 获取项目名称.
    local target = project.target(name)

    -- 如果没有则创建目录
    if not os.exists(game_dir) then
        print("%s not found. Creating...", game_dir)
        os.mkdir(game_dir)
    end

    -- 拷贝文件
    os.cp(target:targetfile(), path.join(game_dir, "GlossTrainer.asi"))
end
