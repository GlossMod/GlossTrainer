# GlossTrainer · 黑神话：悟空

[文档目录](docs/README.md) · [安装说明](docs/install.md) · [验证记录](docs/verification.md)

《黑神话：悟空》Windows x64 游戏内修改器，当前源码版本为 **v1.1.0**。使用 C++20、Dear ImGui 1.92.9 和 MinHook 1.3.4，支持 DX11 / DX12 覆盖层，通过 [Ultimate ASI Loader v10.0.0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.0) 自动加载。

本版包含 **64 项功能**，采用支持搜索和分类的紧凑列表，适配亮色 / 暗色主题；另有 1,160 条物品、32 个场景和 284 个地标的物品与传送工具。影神图可选择故事阶段（0–5）和画像阶段（0–1），按场景传送使用游戏默认出生点。精魄、法术和影神图的单项删除尚未实现，界面中已禁用。新增功能的游戏效果仍待用户手测；操作和验证范围见 [功能说明](docs/gameplay.md) 和 [验证记录](docs/verification.md)。

传送地标按 17 个场景分组，支持逐组或全部展开 / 折叠；搜索会展开匹配组，清空搜索后恢复原折叠状态。

“七十二变（实验性）”页提供角色搜索、分类、支持状态、技能配置和恢复悟空入口。目录中所有角色均可直接点击“开始变身”，无需先检查技能；验证状态和此前的不可用提示不阻止重新尝试。资源与初始化失败时显示原因并恢复悟空。参考 Mod 的名称和资源路径仅作为目录线索；实际目录覆盖、原生接口要求和逐角色验证范围见 [变身实现记录](docs/transformation.md)。角色的“未验证”状态不能视为稳定可玩承诺。

角色检查按实际组件与初始化结果选择地面、游泳或普通飞行代理；阶段图、场景关联和专用移动驱动不因出现在目录中就开放。通过结构检查的角色仍为实验性，完整多阶段形态和游戏内验证范围见[全形态审计](docs/transformation-all-forms-audit.md)。

文本输入控件支持中文输入法：窗口线程管理菜单专用输入法上下文，候选位置跟随输入光标，关闭菜单或失焦后恢复原关联。搜狗输入法的实际候选选词需在更新后由玩家确认；自动回归范围见验证记录。

界面支持简体中文和 English，默认跟随游戏文本语言。“设置 → 语言”可选择“跟随游戏 / 简体中文 / English”，切换立即生效，偏好保存在 `scripts\GlossTrainer\language.ini`。设置页显示检测到的游戏语言及来源；暂时无法读取游戏语言时，明确显示使用系统语言临时回退。玩家自定义技能名和书签名保持原样。物品长篇说明目前保留原文，详细范围见[语言功能说明](docs/localization.md)。

功能快捷键初始全部未绑定。菜单内点击功能行的快捷键按钮可录制；Esc 取消，Backspace 或 Delete 清除；冲突绑定会被拒绝。界面开关默认 **Insert**，可在“设置 → 快捷键”点击“修改”录制按键或 Ctrl / Shift / Alt 组合键，支持恢复默认，不能留空。菜单键始终可用，独立于功能快捷键总开关；当前菜单组合和 **Ctrl+Shift+Home** 不能再分配给功能。两类绑定保存在 `scripts\GlossTrainer\hotkeys.ini`，主题、缩放和窗口位置保存在 `scripts\GlossTrainer\ui.ini`。


修改项的开关、数值和功能快捷键总开关会自动保存到 `scripts\GlossTrainer\features.ini`。下次启动游戏时先读取配置，进入存档且对应功能就绪后自动恢复已启用项；等待中的开关可以手动关闭以取消恢复。数值输入与已应用参数分别保存：尚未点击“应用”的输入不会在重启时直接写入游戏。“增加经验”“解锁”等一次性动作只记住输入值，启动时不会再次执行。

作者：**By: 小莫**。

v0.3.5 将 GitHub 更新检查按游戏隔离，仅接受本游戏的正式版本与对应安装包，避免同仓库其他游戏触发更新。旧 v0.3.4 需从本游戏发行版页面手动升级一次。初始化后检查一次，也可在“设置 → 版本更新”手动检查。成功确认有新版时暂停修改功能并引导前往对应 GitHub Release 手动下载；已启用开关会请求关闭，不撤销已经完成的物品或存档操作；扩展队列中尚未执行的修改请求会取消，恢复请求继续处理。菜单仍可关闭。检查失败只显示“检查更新失败”，不会限制使用，也不会自动恢复之前关闭的功能。没有正式 Release 时可正常使用。不会自动下载或安装更新。

## 下载发行包安装

发行打包流程生成 `GlossTrainer-BlackMythWukong-vX.Y.Z-win64.zip`，包含根目录的 `version.dll`、`version.ini`、默认中文 `README.md`、英文 `readme_en.md` 和 `scripts\GlossTrainer.asi`。下载入口为 [本游戏发行版](https://github.com/GlossMod/GlossTrainer/releases?q=BlackMythWukong%2Fv&expanded=true)，标签为 `BlackMythWukong/vX.Y.Z`，可使用同页对应包名的 `.SHA256SUMS.txt` 核对文件；已发布的历史版本以各自包内说明为准。

对于上述复制安装包，正常退出游戏后，将 `version.dll`、`version.ini` 和 `scripts` 文件夹整体复制到实际 `b1\Binaries\Win64`，与 `b1-Win64-Shipping.exe` 同级。包内已有经过固定 SHA-256 校验的 Ultimate ASI Loader v10.0.0 x64，安装时无需联网。未知来源的同名 DLL / INI 应先核对归属；保留 `dwmapi.dll` 和其他插件。默认按 Insert 打开菜单，配置和日志在首次运行时自动创建，包内不放用户配置。

更新前退出游戏，再复制新版文件。卸载时移除 `scripts\GlossTrainer.asi`；确认 `version.dll`、`version.ini` 仅供本修改器使用后再移除。按需保留 `scripts\GlossTrainer` 配置和日志，不要删除整个 scripts 目录。此方式不创建安装器清单；源码安装器的清单式安装和卸载见下一节。

玩家版说明模板见[中文版](docs/release-readme.md)和[英文版](docs/release-readme-en.md)，包含修改器名称、安装方法、功能、关于信息及完整许可证；打包规则见 [发布说明](../docs/releasing.md)。

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
- 修改项配置：`scripts\GlossTrainer\features.ini`，记录开关、输入值、上次成功应用值及功能快捷键总开关。恢复遇到队列繁忙会稍后重试，实际执行失败保留错误和配置；不会反复强制启用。更新限制解除后不自动恢复，本次待恢复项已取消；下次启动仍读取保留的偏好。退出游戏后删除此文件可将这些偏好恢复为默认。
- 变身技能配置：`scripts\GlossTrainer\transformation-bindings.cfg`，按角色稳定标识和实际技能 ID 保存自定义名称及按键。角色检查后显示完整技能列表，可搜索、选填名称、录制单键或 Ctrl/Shift/Alt 组合键，再点“保存技能配置”；名称留空使用原名。旧 `transformation-slots.cfg` 保留游戏动作/手柄映射。“试招”需关闭菜单并返回游戏才施放，30 秒未执行则取消。
- 技能可绑定鼠标左键、右键、中键、两个侧键，也可组合 Ctrl/Shift/Alt；绑定命中时消费对应游戏点击。
- 变身角色详情提供“角色视距”：拖动滑块或 Ctrl+点击输入 1–200 米，结束编辑后自动保存；变身中即时应用。每个目录角色单独配置，“自动”恢复按体型计算，保存在 `scripts\GlossTrainer\transformation-camera.cfg`，退出变身恢复悟空原相机。
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

## 测试与适配范围

```powershell
xmake build GlossTrainerFeatureSettingsTests
.\build\release\GlossTrainerFeatureSettingsTests.exe .\output\feature-settings-tests
xmake build GlossTrainerGameplayTests
.\build\release\GlossTrainerGameplayTests.exe
xmake build GlossTrainerTransformationTests GlossTrainerEngineBridgeTests GlossTrainerExtensionLifecycleTests
.\build\release\GlossTrainerTransformationTests.exe
.\build\release\GlossTrainerEngineBridgeTests.exe
.\build\release\GlossTrainerExtensionLifecycleTests.exe
xmake build GlossTrainerTransformationNativeTests GlossTrainerTransformationRuntimeTests
.\build\release\GlossTrainerTransformationNativeTests.exe
.\build\release\GlossTrainerTransformationRuntimeTests.exe
xmake build GlossTrainerTransformationBindingTests GlossTrainerTransformationHotkeyTests
.\build\release\GlossTrainerTransformationBindingTests.exe
.\build\release\GlossTrainerTransformationHotkeyTests.exe
xmake build GlossTrainerImeTests
.\build\release\GlossTrainerImeTests.exe
xmake build GlossTrainerHost
.\Tests\run-automated.ps1 -Renderer dx11 -AsiPath .\build\release\BlackMythWukong.asi -OutputDirectory .\build\test-output\dx11
.\Tests\run-automated.ps1 -Renderer dx12 -AsiPath .\build\release\BlackMythWukong.asi -OutputDirectory .\build\test-output\dx12
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-install.ps1
```

当前适配基于本机 Steam build **21393610** 的唯一特征和 Mono AOT 属性调用链。版本变动、入口歧义或玩家尚未捕获时，对应功能保持未支持或等待状态；不会在多个数组 getter 中任选一个。参考修改器标题的 `v1.0–v1.0.20` 不是本项目全版本兼容承诺。

游戏功能测试覆盖参数暂存、快捷键、特征扫描、真实 MinHook trampoline、CPU 扩展状态保留及合成玩家属性队列。DX11 / DX12 宿主验证实际图形 Hook、交换链、输入消息和尺寸变化；宿主不执行游戏功能。这些测试均不能替代游戏中的逐项效果验收。v0.3.0 当前游戏内逐项验收待用户手测，不能从构建、静态映射或宿主测试推断通过；以 [验证记录](docs/verification.md) 为准。

界面原型使用 `custom-imagegen` 的 `gpt-image-2.5` 生成，随后按原型实现；构建和验证结果以任务实际记录为准，本 README 不代替验收记录。

模块及 Hook 生命周期见 [架构说明](docs/architecture.md)，参考包研究依据见 [静态分析](../output/fling-reference/static-analysis.md) 和 [当前游戏映射](../output/fling-reference/game-mapping.md)。第三方许可保存在 [licenses](../licenses/)；发行包 README 内嵌项目与依赖的完整许可。Loader 由打包器或源码安装器从固定官方发布下载并校验，复制安装包已包含 Loader。

## 关于

- **当前版本号**：v1.1.0；运行版本与最新版本号可在修改器“关于”页查看。
- **最新版本**：[黑神话：悟空 GitHub Releases](https://github.com/GlossMod/GlossTrainer/releases?q=BlackMythWukong%2Fv&expanded=true)，仅比较本游戏正式版本。检查失败仅显示“检查更新失败”，不限制使用。
- **作者：小莫**（By: 小莫）。
- **主 LICENSE**：[GNU GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE)。
- **开源地址**：[GlossMod / GlossTrainer](https://github.com/GlossMod/GlossTrainer)。
- **赞助作者**：[sponsor.aoe.top](https://sponsor.aoe.top/)。
- **申请制作游戏修改器**：[GlossMod 委托制作](https://www.glossmod.com/commissions)。

发行包默认附带中文 `README.md`，英文说明为 `readme_en.md`，两份均含安装方法、功能及完整许可。
