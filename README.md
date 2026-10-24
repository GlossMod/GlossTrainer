# GlossTrainer

《黑神话：悟空》Windows x64 游戏内修改器框架。使用 C++20、Dear ImGui 1.92.9 和 MinHook 1.3.4，支持 DX11 / DX12 覆盖层，通过 [Ultimate ASI Loader v10.0.0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.0) 自动加载。

首版包含界面、输入接管、配置和日志；“修改功能”页是占位页，尚无游戏数值修改。菜单默认隐藏，进入游戏后按 **Insert** 打开或关闭。界面提供“概览 / 修改功能 / 设置”，支持简体中文、亮暗主题和界面缩放。关闭按钮隐藏菜单；游戏世界继续运行。

## 构建与安装

需要 Windows、Visual Studio 2022 C++ 桌面开发工具（含 Windows SDK）及 [xmake](https://xmake.io/)。在仓库根目录执行：

```powershell
xmake f -p windows -a x64 -m release
xmake

# 退出游戏后，构建并安装到实际游戏 EXE 所在目录。
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong

# 只预览游戏目录的安装；仍会先构建 ASI。
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong --dry-run
```

调试版本使用 `xmake f -p windows -a x64 -m debug` 后执行 `xmake`。安装器不会创建不存在的游戏目录，必须找到该目录下的 `b1-Win64-Shipping.exe`，并验证 EXE、ASI、Loader 为 x64 PE。

安装器下载并核验固定版本 Loader，安装 `version.dll`、`version.ini` 和 `scripts\GlossTrainer.asi`，保留现有 `dwmapi.dll`、小地图和其他插件。重复安装仅更新清单记录且哈希仍匹配的文件；同名未知文件或外部改写文件会导致安装整体拒绝，错误信息会指出具体路径。不要直接覆盖其他插件的代理 DLL。

## 使用与文件位置

- **Insert**：切换菜单；设置页切换亮暗主题和缩放，窗口位置自动保存。
- 菜单打开时接管键鼠、释放鼠标限位；关闭菜单或游戏失焦时恢复输入。支持 Alt+Tab 和窗口尺寸变化。
- 配置：游戏 EXE 目录下的 `scripts\GlossTrainer\ui.ini`。
- 运行日志：`scripts\GlossTrainer\logs\trainer.log`，超过 2 MiB 后轮换为 `trainer.previous.log`。
- 安装清单：`scripts\GlossTrainer\install-manifest.json`。卸载后配置和日志保留。
- 不提供运行中卸载；更新或卸载前需正常退出游戏。

```powershell
# 卸载前预览
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong --uninstall --dry-run

# 卸载清单记录且未被改写的文件，不构建项目
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong --uninstall
```

直接安装已有 ASI、缓存目录、冲突排查和验证方法见 [安装说明](docs/install.md)。

## 验证范围

DX11 / DX12 测试宿主用于验证实际 MinHook 链路、交换链和尺寸变化，不代表真实游戏验收。真实游戏还需检查 Loader 自动加载、Insert、中文、亮暗主题、输入恢复、最小化、退出及现有插件共存。游戏启动参数可分别使用 `-dx11`、`-dx12`；若当前游戏拒绝某模式，应记录该限制，只报告实际完成的游戏内验证。

界面原型使用 `custom-imagegen` 的 `gpt-image-2.5` 生成，随后按原型实现；构建和验证结果以任务实际记录为准，本 README 不代替验收记录。

本机实际结果和未完成项目见 [验证记录](docs/verification.md)，模块及 Hook 生命周期见 [架构说明](docs/architecture.md)。第三方许可保存在 `licenses/`；分发 ASI 时应同时保留这些许可。Loader 由安装器从固定官方发布下载。
