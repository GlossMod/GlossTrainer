# 安装、卸载与排错

## 发行包复制安装

新的发行打包流程将 `version.dll`、`version.ini`、`scripts/GlossTrainer.asi` 和 `README.md` 放入 ZIP。正常退出游戏，将 DLL、INI 和 scripts 文件夹复制到实际 `b1/Binaries/Win64`（与 `b1-Win64-Shipping.exe` 同级），然后启动游戏即可；Loader 已随包提供，无需安装时联网。历史发行包以各自包内 README 为准。

更新也需先退出游戏；包内不含用户配置，不会覆盖 `scripts/GlossTrainer` 中已有设置。同名 DLL / INI 若来自其他插件，应先核对归属。复制安装不创建安装清单，卸载时先移除 `scripts/GlossTrainer.asi`，确认 Loader 文件仅供本项目使用后再移除 `version.dll` 和 `version.ini`，不要删除整个 scripts 目录。

以下独立安装器用于源码工作区的清单式安装与卸载，不随上述复制安装包分发。

## 独立安装器

`Scripts/install.ps1` 兼容 Windows PowerShell 5.1 / PowerShell 7，使用绝对路径定位游戏目录。命令必须在游戏退出后执行。

```powershell
# 将 Artifact 改为实际已构建 ASI 的绝对路径。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\install.ps1 `
  -ExeDir "E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" `
  -Artifact "E:\path\to\BlackMythWukong.asi" -DryRun

# 移除 -DryRun 后执行安装；也可使用 PowerShell 标准参数 -WhatIf 预览。
# 直接卸载无需 Artifact；加 -DryRun 或 -WhatIf 可预览。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\install.ps1 `
  -ExeDir "E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" -Uninstall
```

退出码 `0` 表示成功、预览完成或无安装可卸载，`1` 表示预检或操作失败。`-DryRun` / `-WhatIf` 不下载 Loader、不创建缓存或修改游戏文件；`xmake bi --dry-run` 会先构建 ASI，再进行安装预览。

## Loader 来源与清单

固定使用 [Ultimate ASI Loader v10.0.0 x64 发布包](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.0/Ultimate-ASI-Loader_x64.zip)，预期 SHA-256：

```text
3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7
```

默认缓存为 `%LOCALAPPDATA%\GlossTrainer\Cache\Ultimate-ASI-Loader\v10.0.0\Ultimate-ASI-Loader_x64.zip`，可用 `-CacheDir` 指定已有缓存目录。每次使用缓存都会验证哈希；缓存校验失败时拒绝安装，需自行删除该缓存文件后重试。可以预先下载发布包到上述位置，再离线安装。

验证后的发布包优先取 `version.dll`，若只有 `dinput8.dll` 则重命名为 `version.dll`，并校验 DLL 的 x64 PE 头。生成的 `version.ini` 固定为：

```ini
[GlobalSets]
DontLoadFromDllMain=1
LoadPlugins=1
LoadFromScriptsOnly=1
DebugLog=1
```

三个受管理文件为 `version.dll`、`version.ini` 和 `scripts\GlossTrainer.asi`。清单存储其路径和 SHA-256，并绑定安装目录。首次安装若存在同名未知文件会拒绝覆盖；更新和卸载若发现哈希不同则整体拒绝修改，缺失的自有文件可在更新时补回。清单不是“认领”已有插件的工具，请勿手工改写。

卸载只删除三个受管理文件和清单，不递归删除游戏目录，不删除存档、`dwmapi.dll`、小地图插件、`scripts\GlossTrainer\ui.ini`、运行日志或其他插件。外部改写文件导致卸载失败时，先核查其来源；需要保留它时，应由用户自行移动并重新执行卸载。

## 常见问题

- **游戏目录不存在或找不到 EXE**：`-ExeDir` 指向 `b1\Binaries\Win64`，不是 Steam 游戏根目录。不会为错误路径创建目录。
- **游戏仍在运行**：退出 `b1.exe` / `b1-Win64-Shipping.exe` 后重试；脚本会在下载前及写入前检查。
- **文件冲突或外部改写**：查看错误中的路径和安装清单，不要覆盖其他模组提供的 `version.dll` / `version.ini`。
- **没有菜单**：先按 Insert，查看 `scripts\GlossTrainer\logs\trainer.log`；没有该日志时检查 ASI 是否位于 `scripts` 及 Loader 是否实际加载。日志若报告无法确认 DX12 命令队列，会跳过覆盖层绘制以保持游戏正常渲染。
- **缺失运行库或构建工具**：安装 Visual Studio 2022 C++ 桌面开发和 Windows SDK，重新执行 `xmake f -p windows -a x64 -m release`。使用的外部依赖和构建架构必须为 x64。

## 验证方法

安装器回归检查通过临时目录和系统 x64 PE 文件模拟游戏目录；这种测试只验证部署行为，不加载 ASI，也不代表真实游戏测试。可运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-install.ps1`，需要网络下载固定 Loader，或传入 `-LoaderArchive` 指定已下载的原始发布 ZIP。

完整验收应分别记录 Debug / Release 构建结果、ASI 导出与依赖、DX11 / DX12 宿主结果，以及真实游戏中的自动加载、Insert 反复切换、按住按键切菜单、Alt+Tab、最小化恢复、DPI、配置持久化、正常退出和其他插件共存。不能将宿主通过写成游戏内通过。
