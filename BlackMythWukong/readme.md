# GlossTrainer · 黑神话：悟空

《黑神话：悟空》Windows x64 游戏内修改器，当前源码版本为 **v0.3.4**。使用 C++20、Dear ImGui 1.92.9 和 MinHook 1.3.4，支持 DX11 / DX12 覆盖层，通过 [Ultimate ASI Loader v10.0.0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.0) 自动加载。

本版包含 **64 项功能**，采用支持搜索和分类的紧凑列表，适配亮色 / 暗色主题；另有 1,160 条物品、32 个场景和 284 个地标的物品与传送工具。影神图可选择故事阶段（0–5）和画像阶段（0–1），按场景传送使用游戏默认出生点。精魄、法术和影神图的单项删除尚未实现，界面中已禁用。新增功能的游戏效果仍待用户手测；操作和验证范围见 [功能说明](../docs/gameplay.md) 和 [验证记录](../docs/verification.md)。

传送地标按 17 个场景分组，支持逐组或全部展开 / 折叠；搜索会展开匹配组，清空搜索后恢复原折叠状态。

文本输入控件支持中文输入法：窗口线程管理菜单专用输入法上下文，候选位置跟随输入光标，关闭菜单或失焦后恢复原关联。搜狗输入法的实际候选选词需在更新后由玩家确认；自动回归范围见验证记录。

功能快捷键初始全部未绑定。菜单内点击功能行的快捷键按钮可录制；Esc 取消，Backspace 或 Delete 清除；冲突绑定会被拒绝。界面开关默认 **Insert**，可在“设置 → 快捷键”点击“修改”录制按键或 Ctrl / Shift / Alt 组合键，支持恢复默认，不能留空。菜单键始终可用，独立于功能快捷键总开关；当前菜单组合和 **Ctrl+Shift+Home** 不能再分配给功能。两类绑定保存在 `scripts\GlossTrainer\hotkeys.ini`，主题、缩放和窗口位置保存在 `scripts\GlossTrainer\ui.ini`。


作者：**By: 小莫**。

v0.3.4 清理了测试、录制和占位传送目录，新增 GitHub 正式版检查。初始化后检查一次，也可在“设置 → 版本更新”手动检查。成功确认有新版时暂停修改功能并引导前往对应 GitHub Release 手动下载；已启用开关会请求关闭，不撤销已经完成的物品或存档操作；扩展队列中尚未执行的修改请求会取消，恢复请求继续处理。菜单仍可关闭。检查失败只显示“检查更新失败”，不会限制使用，也不会自动恢复之前关闭的功能。没有正式 Release 时可正常使用。不会自动下载或安装更新。

## 下载发行包安装

从 [GitHub Releases](https://github.com/GlossMod/GlossTrainer/releases/latest) 下载最新 `GlossTrainer-BlackMythWukong-vX.Y.Z-win64.zip`，可使用同页 `SHA256SUMS.txt` 核对文件。解压后正常退出游戏，在解压目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\install.ps1 -Artifact .\scripts\GlossTrainer.asi -ExeDir "E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64"
```

Steam 安装位置不同请更改 `-ExeDir`。安装器从官方来源下载并校验固定 Loader，配置和日志保留。卸载可在同目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\install.ps1 -Uninstall -ExeDir "E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64"
```

## 从源码构建与安装

需要 Windows、Visual Studio 2022 C++ 桌面开发工具（含 Windows SDK）及 [xmake](https://xmake.io/)。在仓库根目录执行：

```powershell
xmake f -p windows -a x64 -m release
xmake build BlackMythWukong

# 退出游戏后，构建并安装到实际游戏 EXE 所在目录。
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong

# 只预览游戏目录的安装；仍会先构建 ASI。
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong --dry-run
```

输出为 `build\release\BlackMythWukong.asi`。调试版本使用 `xmake f -p windows -a x64 -m debug` 后执行同一构建命令，输出位于 `build\debug`。安装器不会创建不存在的游戏目录，必须找到该目录下的 `b1-Win64-Shipping.exe`，并验证 EXE、ASI、Loader 为 x64 PE。

安装器下载并核验固定版本 Loader，安装 `version.dll`、`version.ini` 和 `scripts\GlossTrainer.asi`，保留现有 `dwmapi.dll`、小地图和其他插件。重复安装仅更新清单记录且哈希仍匹配的文件；同名未知文件或外部改写文件会导致安装整体拒绝，错误信息会指出具体路径。不要直接覆盖其他插件的代理 DLL。

## 使用与文件位置

- **Insert**（默认）：切换菜单；设置页可修改界面开关按键、亮暗主题和缩放，窗口位置自动保存。底部提示与启动提示显示当前菜单键。
- **Ctrl+Shift+Home**：禁用或启用功能快捷键；此组合保留，不能绑定到功能。正在输入数值时不触发功能快捷键。
- 功能快捷键默认未绑定；在“修改功能”页点每个功能行的“设置”按钮开始录制。Esc 取消，Backspace / Delete 清除，重复组合键会被拒绝。
- “应用”类动作先排队：30 秒内在游戏中打开相应背包或角色页面触发更新；超时取消，可以重新应用。
- 生命 / 法力 / 气力上限、四种抗性及经验 / 灵蕴倍率通过气力事件更新；关闭后也需游戏气力事件推进恢复，并保留启用期间收到的游戏真实基值更新。
- “设置游戏速度”使用进程内 QPC 计时缩放，会影响同进程使用该时钟的世界逻辑、界面动画及其他组件。
- 菜单打开时接管键鼠、释放鼠标限位；关闭菜单或游戏失焦时恢复输入。支持 Alt+Tab 和窗口尺寸变化。
- 配置：游戏 EXE 目录下的 `scripts\GlossTrainer\ui.ini`（主题、缩放、窗口位置）与 `scripts\GlossTrainer\hotkeys.ini`（功能快捷键和 `[Menu]` 的 `Toggle=虚拟键码,修饰键掩码`）。
- 运行日志：`scripts\GlossTrainer\logs\trainer.log`，超过 2 MiB 后轮换为 `trainer.previous.log`。
- 安装清单：`scripts\GlossTrainer\install-manifest.json`。卸载后配置和日志保留。
- 不提供运行中卸载；更新或卸载前需正常退出游戏。

```powershell
# 卸载前预览
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong --uninstall --dry-run

# 卸载清单记录且未被改写的文件，不构建项目
xmake bi --exedir="E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64" --name=BlackMythWukong --uninstall
```

直接安装已有 ASI、缓存目录、冲突排查和验证方法见 [安装说明](../docs/install.md)。

## 测试与适配范围

```powershell
xmake build GlossTrainerGameplayTests
.\build\release\GlossTrainerGameplayTests.exe
xmake build GlossTrainerImeTests
.\build\release\GlossTrainerImeTests.exe
xmake build GlossTrainerHost
.\Tests\run-automated.ps1 -Renderer dx11 -AsiPath .\build\release\BlackMythWukong.asi -OutputDirectory .\build\test-output\dx11
.\Tests\run-automated.ps1 -Renderer dx12 -AsiPath .\build\release\BlackMythWukong.asi -OutputDirectory .\build\test-output\dx12
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-install.ps1
```

当前适配基于本机 Steam build **21393610** 的唯一特征和 Mono AOT 属性调用链。版本变动、入口歧义或玩家尚未捕获时，对应功能保持未支持或等待状态；不会在多个数组 getter 中任选一个。参考修改器标题的 `v1.0–v1.0.20` 不是本项目全版本兼容承诺。

游戏功能测试覆盖参数暂存、快捷键、特征扫描、真实 MinHook trampoline、CPU 扩展状态保留及合成玩家属性队列。DX11 / DX12 宿主验证实际图形 Hook、交换链、输入消息和尺寸变化；宿主不执行游戏功能。这些测试均不能替代游戏中的逐项效果验收。v0.3.0 当前游戏内逐项验收待用户手测，不能从构建、静态映射或宿主测试推断通过；以 [验证记录](../docs/verification.md) 为准。

界面原型使用 `custom-imagegen` 的 `gpt-image-2.5` 生成，随后按原型实现；构建和验证结果以任务实际记录为准，本 README 不代替验收记录。

模块及 Hook 生命周期见 [架构说明](../docs/architecture.md)，参考包研究依据见 [静态分析](../docs/fling-reference/static-analysis.md) 和 [当前游戏映射](../docs/fling-reference/game-mapping.md)。第三方许可保存在 [licenses](../licenses/)；分发 ASI 时应同时保留这些许可。Loader 由安装器从固定官方发布下载。
