# 本机验证记录

最近更新：2026-10-25。Windows 11 x64、Visual Studio 2022、RTX 5080，游戏 Steam build 21393610。

## v0.3.4：目录清理、署名与 GitHub 正式版检查

顶栏增加 `By: 小莫`。当前目录保留32个场景、284个地标和17个有地标的分组，删除明确测试、QA、录制和占位项；原始统计与推断边界保存在[目录记录](xxj-reference/catalog-data.md)。根README只描述项目，游戏说明迁到 `BlackMythWukong/readme.md`。

先生成并查看[亮暗原型](xxj-reference/validation-v0.3.4/prototype.png)及[完整提示词](xxj-reference/validation-v0.3.4/prototype.prompt.txt)，来源为 `custom-imagegen`、`model_providers.custom` / `gpt-image-2.5`。实现后检查[暗色传送页](xxj-reference/validation-v0.3.4/ui-travel.png)、[亮色更新提示](xxj-reference/validation-v0.3.4/ui-update-required.png)、[检查失败状态](xxj-reference/validation-v0.3.4/ui-check-failed.png)和[失败重试解除限制](xxj-reference/validation-v0.3.4/ui-failure-unlocked.png)。更新提示和失败截图使用本地、Git忽略的UI fixture生成，不是真实远程新版，fixture不进入发行包。

| 验证 | 结果 |
| --- | --- |
| x64 Debug / Release | ASI构建通过；[Debug PE](xxj-reference/validation-v0.3.4/pe-debug.txt) / [Release PE](xxj-reference/validation-v0.3.4/pe-release.txt)确认x64、InitializeASI导出和WinHTTP/Shell32依赖 |
| 七组validation | 两种配置均通过Gameplay、Extension、AOT、AdditionalHook、ExtensionLifecycle、IME、Update；日志在[本版本证据目录](xxj-reference/validation-v0.3.4/) |
| 更新解析 | 严格正式版排序、草稿/预发布和错误JSON拒绝、固定仓库URL、大小/深度限制、失败解除上次更新要求 |
| 更新门控 | 禁止新启用/应用、关闭恢复、并发写事务、失败解锁不复开；真实Frame/Queue测试含多个异步开关、跨代排队启用取消、停用保留和失败恢复重试，共16个生命周期场景 |
| 同一生产ASI / DX11 | PASS，3302帧；主链、按住键释放、菜单开关、尺寸变化、最小化恢复，[日志](xxj-reference/validation-v0.3.4/host-dx11.log) |
| 同一生产ASI / DX12 | PASS，3360帧；同上，并先创建无关direct queue / 辅助链，[日志](xxj-reference/validation-v0.3.4/host-dx12.log) |
| 失败UI fixture / DX12 | PASS，3357帧；设置页只显示“检查更新失败”，导航和菜单可用，[日志](xxj-reference/validation-v0.3.4/fixture-failure-host.log) |
| 确认新版→重试失败 fixture / DX11 | PASS，3300帧；点击重新检查后回到普通页面，底部快捷键恢复可用，[宿主日志](xxj-reference/validation-v0.3.4/fixture-retry-host.log) / [状态日志](xxj-reference/validation-v0.3.4/fixture-retry-trainer.log) |
| 安装与打包 | 安装器18项、发行包12项回归通过；[安装回归](xxj-reference/validation-v0.3.4/install-regression.txt) / [打包回归](xxj-reference/validation-v0.3.4/package-regression.txt) |

IME候选窗和组合窗位置回读仍有两项环境SKIP，不视为搜狗真实选词验证。初次宿主运行曾因前台焦点未满足而失败，未计入通过；最终独立通过运行使用自己的窗口，测试脚本在菜单开关前恢复宿主前台，没有向游戏发送输入。本轮没有新增游戏功能效果验收。

发布前实网GET确认公开仓库存在，latest API当时返回404，生产ASI日志也报告“暂无正式发布版本”；没有用虚假Release测试强制更新。异步检查失败不保留旧锁定，详细原因只写日志；更新页仍允许关闭菜单。已排队扩展修改按请求代次取消，恢复继续推进；已经完成的游戏数据操作不回滚。

**2026-10-25 01:51 在游戏退出状态安装最终Release**，构建和安装哈希一致，ui.ini / hotkeys.ini哈希前后相同：[安装回执](xxj-reference/validation-v0.3.4/installation.json)。
本机构建 Release SHA-256：`4D37DE1373AC7192422DF4E89714E9E2DBE7A2E7D4F4F863AAC9EACC2B126F90`。Debug SHA-256：`7F3C82D50FAF5650DEBE43E12ED4FAF4F3D8BE1E99722BDF8F9D2B192D57718D`。

云端手动运行 [40118206227](https://github.com/GlossMod/GlossTrainer/actions/runs/40118206227) 已成功，x64 Debug / Release 两个作业均 success；手动运行的发布作业按设计 skipped。正式标签仍单独触发构建和发布。


正式标签 [v0.3.4](https://github.com/GlossMod/GlossTrainer/releases/tag/v0.3.4) 已发布；[正式工作流 40118603542](https://github.com/GlossMod/GlossTrainer/actions/runs/40118603542) 的构建和发布均成功，[运行状态回读](xxj-reference/validation-v0.3.4/cloud-runs.json)。发布源码为 `303837d9b4ce8ad97a01b01f5db0772aafcb86b3`。从GitHub重新下载ZIP和SHA256SUMS，核对外部校验和、版本、源码提交及全部7个有效载荷哈希，均通过：[发布回执](xxj-reference/validation-v0.3.4/published-release.json)。

02:06 最终安装来自正式Release的ASI，安装文件与发布清单一致，原有配置哈希不变：[当前安装预检](xxj-reference/validation-v0.3.4/published-install-preflight.txt) / [安装回执](xxj-reference/validation-v0.3.4/published-installation.json)。正式包ASI的DX12宿主核心自动序列PASS（3235帧），[宿主日志](xxj-reference/validation-v0.3.4/published-host-dx12.log)；恢复后额外点击设置页因宿主未处前台而未执行，不将其计作设置页导航通过。该ASI的真实WinHTTP请求读取已发布v0.3.4并报告“当前已是最新版本”，见[运行日志](xxj-reference/validation-v0.3.4/published-trainer.log)。本轮未重新启动游戏，不把宿主结果作为游戏内新验收。
正式ZIP SHA-256：`9918509dd02a541328a200897a5f7aea68120fb194eb0efbd108ee408ad84cbe`。最终已安装ASI SHA-256：`5a4a2f170eb51fd325c0344a263979588b2b67642065be7e8117277886857386`。

## v0.3.3：中文输入法与自定义界面开关按键

搜索和书签等文本框新增窗口线程专用 HIMC 管理；文本输入结束、菜单隐藏、失焦和销毁恢复原关联，包括原关联为 NULL 的情况。IME 消息在 ImGui backend 前只走一次默认处理，避免结果字符串重复提交。仅在文本输入期间临时解除键盘 NOLEGACY，并只在注册仍属于本项目时恢复；保留其他组件的新注册，恢复失败保留快照。没有改变游戏功能处理器。

菜单键默认 Insert，可在设置页录制单键或 Ctrl / Shift / Alt 组合键；与功能键双向检查精确组合冲突，不能清空，损坏配置回退 Insert，支持恢复默认。按键与功能绑定写入同一个 hotkeys.ini，底部和启动提示显示当前组合。设置页先生成并查看 [亮暗原型](../output/imagegen/menu-key-v1.png)（`custom-imagegen`、`model_providers.custom` / `gpt-image-2.5`；[完整提示词](../output/imagegen/menu-key-v1.prompt.txt)），再实现两张留白卡片。

Debug / Release x64 ASI 均已构建，架构、IMM32 依赖及 `InitializeASI` 导出已核对：[Debug PE](xxj-reference/validation-v0.3.3/pe-debug.txt)、[Release PE](xxj-reference/validation-v0.3.3/pe-release.txt)。

| 检查 | 结果与证据 |
| --- | --- |
| IME 独立窗口 | Debug / Release 五组 PASS：默认 / NULL / 独立 HIMC 恢复、跨线程请求与窗口线程执行、反复文本焦点 / 隐藏、Raw Input 条件恢复和外部注册保留、单次默认组合消息与合成 WM_CHAR 经真实 InputText 消费。[Debug](xxj-reference/validation-v0.3.3/ime-debug.txt) / [Release](xxj-reference/validation-v0.3.3/ime-release.txt) |
| 快捷键和已有功能模型 | Debug / Release PASS；覆盖菜单默认、录制、Esc / 修饰键、严格组合匹配、双向冲突、保存 / 重载、非法配置回退、总开关隔离，及现有功能回归。[Debug](xxj-reference/validation-v0.3.3/gameplay-debug.txt) / [Release](xxj-reference/validation-v0.3.3/gameplay-release.txt) |
| DX11 / 默认 Insert | PASS，3316 帧；实际 ASI / MinHook 主链、开关、按住键释放、尺寸变化、最小化恢复，[日志](xxj-reference/validation-v0.3.3/host-dx11.log) |
| DX12 / 自定义 F8 | PASS，3303 帧；同上，并先创建无关 direct queue / 辅助交换链，[日志](xxj-reference/validation-v0.3.3/host-dx12.log) |
| 设置页录制 | DX12 PASS，3358 帧；独立窗口点击“修改”并发送 F9，录制当次菜单保持显示，保存为 `Toggle=120,0`，底部键帽同步 F9。[宿主日志](xxj-reference/validation-v0.3.3/host-dx12-ui-record.log)、[ASI 日志](xxj-reference/validation-v0.3.3/recording-trainer.log)、[保存的 INI](xxj-reference/validation-v0.3.3/recorded-menu-key.ini) |
| 实际界面 | 已查看 [暗色设置](xxj-reference/validation-v0.3.3/ui-dark.png) / [亮色设置及录制结果](xxj-reference/validation-v0.3.3/ui-light.png)，两张卡片、按键行、底部提示与内边距正常 |
| Release SHA-256 | `B7EED564D9D1C24E649104508653C6DC37749F26E89FC319804A49E1FCD5E657` |
| Debug SHA-256 | `3D1D24133002B048883EE8957CA69B982C96DE3B0D672ED7EB7062EB5A218325` |

测试线程的 IMM 环境不能通过 ImmGetCandidateWindow / ImmGetCompositionWindow 读回位置，两项明确 SKIP，未标记定位通过。合成 WM_CHAR 仅验证提交终点，不等于搜狗候选选词。用户已确认使用搜狗输入法，真实候选显示、数字 / 空格选词、中文搜索命中和组合时关闭 / Alt+Tab 恢复仍需玩家手测。所有自动消息仅发往独立宿主窗口，没有向游戏发送输入。

图形自动测试对前台焦点敏感；首次 DX11 运行和暗色设置截图所在的独立运行因开关时失焦失败，未将这些运行记为 PASS。暗色 [截图运行日志](xxj-reference/validation-v0.3.3/ui-dark-run.log) 仅支撑最后恢复窗口后的显示检查。表中的 DX11 / DX12 PASS 分别来自所链接的完整通过运行；自定义组合键覆盖来自模型测试，真实物理 raw 键包不在宿主消息测试范围内。

**2026-10-25 00:45 已在游戏退出状态安装 v0.3.3 Release**。构建与安装文件 SHA-256 一致，安装清单三项逐一核对；已有 `ui.ini` 和 `hotkeys.ini` 的哈希前后不变，没有将宿主使用的 F8 / F9 测试绑定写入游戏配置。证据：[安装输出](xxj-reference/validation-v0.3.3/install.txt)、[安装回执](xxj-reference/validation-v0.3.3/installation.json)。尚未重启游戏验证新版搜狗选词和物理菜单组合键。

## v0.3.2：传送地标折叠分类

先使用 `custom-imagegen`、`model_providers.custom` / `gpt-image-2.5` 生成并查看 [亮暗原型](../output/imagegen/travel-groups-v1.png)（[提示词](../output/imagegen/travel-groups-v1.prompt.txt)），再实现传送页。298 个地标按 scene ID 归为 21 组，所有 ID 均存在于 79 条场景目录中；只展示有匹配地标的组，组内顺序保持不变。标题显示箭头、场景名与数量，默认折叠，增加全部展开 / 折叠；单一滚动区避免嵌套滚动，标题与行保留内边距。搜索与普通折叠状态独立保存，清空后恢复普通状态；搜索 / 场景筛选变化时回到顶部。代码审查核对了这些状态转换和稳定 ID，传送调用、禁用条件及坐标提示保持原行为。

Debug / Release x64 构建成功，架构和 `InitializeASI` 导出已核对：[Debug PE](xxj-reference/validation-v0.3.2/pe-debug.txt)、[Release PE](xxj-reference/validation-v0.3.2/pe-release.txt)。同一 Release ASI 的宿主记录：

| 项目 | 结果 / 证据 |
| --- | --- |
| DX11 | PASS，3248 帧；主链选择、菜单开关、按住键释放、尺寸变化、最小化恢复，[日志](xxj-reference/validation-v0.3.2/host-dx11.log) |
| DX12 | PASS，3335 帧；同上，并覆盖辅助链 / 无关队列先创建，[日志](xxj-reference/validation-v0.3.2/host-dx12.log) |
| 展示检查 | 已查看实际 GPU 截图：[暗色折叠](xxj-reference/validation-v0.3.2/ui-dark.png)、[亮色全部展开](xxj-reference/validation-v0.3.2/ui-light.png)，标题数量、行缩进、按钮对齐与内边距正常 |
| Release / 已安装 | `9A78DBBAAFB9B1893548FCE5FF5AC3BF0F48ACFEBD92F92560736AAE279D4B95` |
| Debug | `79931B5282D4FE6323742BFEDBA49CEA3BA4DC9CE5E9FACC9DE4D758BA75A7EF` |

宿主导航试验受到窗口焦点变化和测试脚本消息编码影响，未将中文搜索、清空恢复等导航试验统一标记为自动通过；其状态逻辑已做静态审查。暗色截图的独立导航运行未观察到宿主的恢复事件，因此仅用于展示检查；上表的 DX11 / DX12 PASS 分别来自所链接的通过运行。上述截图均来自独立宿主，不代表游戏内传送效果验证。

**23:31 已在游戏退出状态安装 v0.3.2**，构建与安装 ASI 的 SHA-256 一致，安装清单逐项校验通过；`ui.ini` 和 `hotkeys.ini` 的哈希安装前后不变。证据：[安装输出](xxj-reference/validation-v0.3.2/install.txt)、[回执](xxj-reference/validation-v0.3.2/installation.json)。本轮没有新增游戏传送效果验收。

## v0.3.1：原型对齐与悬浮提示留白

本次仅调整 UI 与版本号，延续 v0.3.0 的功能逻辑。沿用已生成并查看的 [custom-imagegen 原型](../output/imagegen/compact-list-v1.png)：窗口 960×680、内容区外侧 24px、表格单元格 14×8、行高至少 44px；标题 / 分类 / 表格之间单独留白。分类选中使用金色下划线，补齐导航图标、品牌粗体、搜索图标和底部键帽；快捷键仍默认未绑定。所有悬浮提示统一 14×10 内边距、6px 圆角、1px 边框，内部说明与分隔线均保留左右留白。物品列表为底部说明预留空间，避免新增行间距导致内容区溢出。

Debug / Release x64 均构建成功，PE 均导出 `InitializeASI`：[Debug PE](xxj-reference/validation-v0.3.1/pe-debug.txt)、[Release PE](xxj-reference/validation-v0.3.1/pe-release.txt)。最终 Release 的实际 ASI / MinHook 链路宿主结果：

| 项目 | 结果 / 证据 |
| --- | --- |
| DX11，22:54 | PASS，3352 帧；主链、显示隐藏、按住键释放、尺寸变化、最小化恢复；[日志](xxj-reference/validation-v0.3.1/host-dx11.log) |
| DX12，22:54 | PASS，3345 帧；同上，且无关队列 / 辅助链先创建时关联正确主链队列；[日志](xxj-reference/validation-v0.3.1/host-dx12.log) |
| 实际 UI 检查 | [暗色](xxj-reference/validation-v0.3.1/ui-dark.png)、[亮色](xxj-reference/validation-v0.3.1/ui-light.png)、[悬浮提示](xxj-reference/validation-v0.3.1/ui-tooltip.png)；来自独立宿主 GPU 截图并裁去外围背景 |
| Release，已安装 | `3C371DBB8D4CE31AE840E821218B8D95A845FF32366FF8A45474EF8FB69CC153` |
| Debug | `11EFC4D47F4A88891CACABB6150200E6BC9896898015F1D837F53A8778EDFDBA` |

两组最终宿主均重复调用初始化入口，菜单显示时相差 652732 个像素，隐藏后与基线差异为 0。**22:55 已在游戏退出状态安装最终文件**；构建与安装文件 SHA-256 一致，安装清单逐项校验通过，已有 `ui.ini` 与 `hotkeys.ini` 安装前后哈希不变。证据：[安装输出](xxj-reference/validation-v0.3.1/install.txt)、[回执](xxj-reference/validation-v0.3.1/installation.json)。本轮不将 UI 宿主结果扩展为新的游戏功能效果验收。

## v0.3.0：构建、部署与回归

当前源码版本 v0.3.0 的功能目录扩展为 64 项，并加入物品与传送目录。参考目录静态统计为 1,160 项物品、79 个场景和 298 个地标，来源、哈希及过滤规则见 [catalog 数据记录](xxj-reference/catalog-data.md)。影神图操作由用户选择故事阶段 0–5 和画像阶段 0–1；按场景进入使用游戏默认出生点；精魄、法术和影神图的删除操作在界面中禁用。静态目录、接口映射、合成测试或图形宿主结果均不代表游戏中已经成功添加物品、修改影神图、跨场景移动或恢复状态。

Debug / Release x64 均构建成功。PE 检查确认导出 `InitializeASI`，ImGui / MinHook 静态链接；Debug 依赖开发机的调试运行库，部署使用 Release。证据：[Debug PE](xxj-reference/validation-v0.3.0/pe-debug.txt)、[Release PE](xxj-reference/validation-v0.3.0/pe-release.txt)。

| v0.3.0 构建 | SHA-256 |
| --- | --- |
| Release，已安装 | `3BC45ACC68822ED1440BAC5B6712E2B8C3966336FD2BB1790EC21655C55F7BBF` |
| Debug | `AA6F2D05A85F614A687E83711BAB09B8E26D1E361D665FB355EC7BC8EA3BF9DE` |

五组自动测试在两种构建模式均 PASS：

| 测试 | 覆盖 | Debug / Release 证据 |
| --- | --- | --- |
| Gameplay | 原有功能、动作并发、属性恢复、默认空快捷键、绑定冲突 / 清除 / 录制 / 持久化 | [Debug](xxj-reference/validation-v0.3.0/GlossTrainerGameplayTests-debug.txt) / [Release](xxj-reference/validation-v0.3.0/GlossTrainerGameplayTests-release.txt) |
| Extension | 原生导出注册解析边界与目录完整性 | [Debug](xxj-reference/validation-v0.3.0/GlossTrainerExtensionTests-debug.txt) / [Release](xxj-reference/validation-v0.3.0/GlossTrainerExtensionTests-release.txt) |
| AOT | 四程序集方法解析，RID / MVID 拒绝及可选程序集降级 | [Debug](xxj-reference/validation-v0.3.0/GlossTrainerAotTests-debug.txt) / [Release](xxj-reference/validation-v0.3.0/GlossTrainerAotTests-release.txt) |
| AdditionalHook | 物品扣数、主动删除许可、丹药 / 法宝计时、玩家换代与周目请求 | [Debug](xxj-reference/validation-v0.3.0/GlossTrainerAdditionalHookTests-debug.txt) / [Release](xxj-reference/validation-v0.3.0/GlossTrainerAdditionalHookTests-release.txt) |
| ExtensionLifecycle | 11 组：组件 / 控制器换代、旧世界恢复重试、自由相机无 Pawn 关闭，以及 UMG 新 root、弱引用换代、显示基线、关闭重试和场景变化 | [Debug](xxj-reference/validation-v0.3.0/GlossTrainerExtensionLifecycleTests-debug.txt) / [Release](xxj-reference/validation-v0.3.0/GlossTrainerExtensionLifecycleTests-release.txt) |

最终 Release 使用实际 ASI / MinHook 链路通过图形宿主回归：

| 宿主 | 结果 | 证据 |
| --- | --- | --- |
| DX11，22:19 | PASS，3355 帧；主交换链、显示 / 隐藏、按住键释放、尺寸变化、最小化恢复 | [日志](xxj-reference/validation-v0.3.0/host-dx11.log) |
| DX12，22:20 | PASS，3348 帧；同上，且在无关 direct queue / 辅助链先创建时正确关联主链队列 | [日志](xxj-reference/validation-v0.3.0/host-dx12.log) |

两组宿主均重复调用初始化入口；菜单显示时相差 492732 个像素，隐藏后与基线差异为 0。亮暗主题、中文紧凑列表和新页面已检查：[暗色](xxj-reference/validation-v0.3.0/ui-dark.png)、[亮色](xxj-reference/validation-v0.3.0/ui-light.png)、[物品](xxj-reference/validation-v0.3.0/ui-items.png)、[传送](xxj-reference/validation-v0.3.0/ui-travel.png)。这些截图来自独立宿主，不是游戏效果截图。

**22:22 已在游戏退出状态安装最终 Release。** 安装后 ASI 与构建文件哈希一致，安装清单中三份项目文件逐一校验通过；Loader 保持 v10.0.0、`DontLoadFromDllMain=1`，未改写用户 UI / 快捷键配置。证据：[安装输出](xxj-reference/validation-v0.3.0/install.txt)、[哈希回执](xxj-reference/validation-v0.3.0/installation.json)。

**v0.3.0 的新增游戏内操作与效果尚待用户手测；本记录没有将其标记为通过。** 已请用户自行启动游戏，检查快捷键、物品不减、丹药 / 法宝、UMG 与自由相机关闭恢复，以及物品 / 传送结果。当前没有新版自动加载的游戏运行日志，不以宿主结果替代游戏结果。精魄实际收录、`Card` 所选阶段和跨图目的地仍需结合具体条目与流程检查；精魄、法术、影神图的单项删除尚未实现。参考修改器的“隐藏结局”页面为攻略文本，没有新增自动完成结局的功能；场景对象列表目前使用原生对象名，没有完整中文名称映射。

## v0.2.1：上一已记录构建、部署与回归

Debug / Release 构建与 `GlossTrainerGameplayTests` 均 PASS：[Debug 测试](evidence/gameplay-tests-v0.2.1-debug.txt)、[Release 测试](evidence/gameplay-tests-v0.2.1-release.txt)。PE 检查确认两者为 x64 并导出 `InitializeASI`：[Debug PE](evidence/pe-v0.2.1-debug.txt)、[Release PE](evidence/pe-v0.2.1-release.txt)。

此次修复和新增回归覆盖两个边界：

- 动作请求的时间、数值和状态采用同一事务保护；旧采样时间不再下溢触发立即过期，旧请求的清理也不能取消新请求。游戏回调和过期检查使用不等待的 `try_lock`。
- 属性功能启用期间，游戏自身正常基值更新成为新的恢复基准；关闭后恢复最新真实值，不回滚正常升级、装备或增益更新。九项事件属性均有该恢复场景回归。

| v0.2.1 构建 | SHA-256 |
| --- | --- |
| Release | `12A5043772B6E91B5E55BB073D0B0A5B81F57C1DF3A5182B18DFE4BBB0D58300` |
| Debug | `721A4CECEA50E1F68E486E5B94233BC86B3CA736F927FEF09BB423BE9CC37004` |

最终 Release 已在游戏正常退出后安装，安装文件 SHA-256 与上表 Release 一致。随后使用该构建完成实际图形 Hook 宿主回归：

| v0.2.1 Release 宿主 | 结果 | 证据 |
| --- | --- | --- |
| DX11，20:32 | PASS，3355 帧；显示 / 隐藏、legacy 按住键释放、尺寸变化、最小化恢复及主交换链选择全部通过 | [日志](evidence/host-v0.2.1-dx11.log) |
| DX12，20:33 | PASS，3332 帧；同上，且无关 direct queue / 辅助链先创建时仍正确关联主链队列 | [日志](evidence/host-v0.2.1-dx12.log) |

两者打开菜单时与基线相差 563591 个像素，隐藏后恢复为 0 差异；均重复调用 `InitializeASI` 检查幂等性。GPU backbuffer 截图分别保存在 `build/test-output/v0.2.1-dx11` 和 `build/test-output/v0.2.1-dx12`。

Steam 以 `-dx12` 重启游戏后，PID 75660 在 **20:33:27** 记录 `GlossTrainer 0.2.1 initializing`，**20:33:37** 完成真实交换链队列验证与 DX12 后端初始化，**20:34:33** 再次记录 `Player attribute identity verified; 44/44 feature handlers connected`。证据见 [v0.2.1 运行日志](fling-reference/runtime-v0.2.1.log)。这确认最终文件已自动加载并连接处理器；下文的功能效果和关闭恢复用户手测来自 v0.2.0，不将其改写为 v0.2.1 的逐项效果验收。

## v0.2.0：真实功能连接与用户手测

该轮 Debug / Release 已构建并通过游戏功能测试，Release 已安装，游戏可正常进入存档。**20:24:01 的实际游戏日志确认 `Player attribute identity verified; 44/44 feature handlers connected`。这证明全部入口和玩家识别已接通，不是 44 项功能效果全部通过的验收结论。**

用户先确认当时可用的首批功能没有问题，新增 32 项属性链路接通后又反馈：“新功能正常，关闭恢复也正常。”据此记录新增功能及关闭恢复的用户手测确认；会话未逐项列出操作、数值和留证结果，因此不编造 44 项逐项通过统计。运行日志还曾记录无敌模式、无限法力、无限气力的启用请求，这些请求记录与用户的效果反馈属于不同层次的证据。

| 范围 | 已有证据 | 当前结论 |
| --- | --- | --- |
| 44 项目录、名称、快捷键 | [逐项清单](fling-reference/features.json)、源码静态完整性检查 | 目录与处理器已实现，不代表效果全通过 |
| 静态功能特征 | [唯一匹配与歧义记录](fling-reference/signature-matches.md)、[原始数据](fling-reference/signature-matches.json) | 能定位当前构建的入口；不能以 AOB 命中代替行为验收 |
| Mono AOT 调用链 | [现场包装链](fling-reference/live-attribute-callgraph.json)、[现场泛型 getter](fling-reference/live-attribute-getter.json) | 已实际只读确认链路，五段解析模式与留存指令逐字节匹配 |
| 真实玩家与全部处理器连接 | [v0.2.0 运行日志](fling-reference/runtime-v0.2.0.log)、[实际 List reader](fling-reference/live-list-reader.json) | PID 68236；真实 List getter `0x140B5AE90`、Hook `0x140B5AEC8`、GSBind 探针 `0x1438377F8`，44/44 已连接 |
| v0.2.0 Debug / Release 功能测试 | [Debug](evidence/gameplay-tests-debug.txt)、[Release](evidence/gameplay-tests-release.txt) | 均 PASS；覆盖目录、参数零自动写入、快捷键、特征扫描、真实 trampoline、属性队列与恢复 |
| CPU 状态与受保护内存回归 | 同上功能测试及测试源码 | XSAVE / XMM / YMM / flags / MXCSR 保留，扫描跳过 PAGE_NOACCESS / PAGE_GUARD 场景通过 |
| 新版 UI 的 DX11 宿主 | [19:55 日志](evidence/host-dx11-gameplay-ui-1955.log) | PASS，3387 帧、显示隐藏、尺寸变化、最小化恢复及 legacy 按住键释放；属于此前构建，不是最终哈希回归 |
| v0.2.0 最终 DX12 宿主 | 该轮没有保留可复核的最终构建宿主记录 | 不标记为该轮最终构建通过；v0.2.1 DX12 回归见上文 |
| 当前游戏启动 | v0.2.0 日志及本轮进入存档的实机观察 | 自动加载、DX12 后端初始化、进入存档已完成；旧版资源加载崩溃目前不是阻塞 |
| 新版功能效果及关闭恢复 | 用户对首批功能的确认，及“新功能正常，关闭恢复也正常”的后续手测反馈 | 已获得新增功能及关闭恢复确认；未逐项留证，不生成 44 项逐项通过统计 |
| 插件共存 | 当前目标目录没有旧 `dwmapi.dll` / 小地图插件 | 本轮未验证共存；下文旧文件哈希仅属历史记录 |

v0.2.0 ASI 的 PE 检查确认 x64 与 `InitializeASI` 导出：[Debug](evidence/pe-gameplay-debug.txt)、[Release](evidence/pe-gameplay-release.txt)。该轮文件 SHA-256（不是当前 v0.2.1 构建哈希）：

| 构建 | SHA-256 |
| --- | --- |
| Release，已安装 | `0B1C3E9E1AC960A351BBCC3C24E065247478400017DF2016E9C38A3D4C6235A5` |
| Debug | `1EC880C908B8F0103D2057F34583BEFFDB16D8CC10A386C4509E410A93CD2CA3` |

功能测试可在配置对应模式后运行 `xmake build GlossTrainerGameplayTests`、`build\release\GlossTrainerGameplayTests.exe`，Debug 则使用 `build\debug`。测试使用合成目标和真实 Hook 桥，不执行游戏存档修改；它证明实现回归条件，不证明真实游戏的各项效果。

用户已确认新增功能及关闭恢复正常，但没有逐项留存游戏内操作证据。动作并发 / 超时竞争和真实基值变动后的恢复已在 v0.2.1 增加合成回归；这不等于这些边界均在游戏内独立实测。切图 / 变身后对象替换的实机专项记录仍待完善，最终图形回归状态见本文开头。当前主 EXE 身份和托管接口依据见 [游戏映射](fling-reference/game-mapping.md)。

## v0.1.0 构建与宿主：历史记录

以下证据属于 18:54 左右完成的 v0.1.0 图形框架版本，保留供回归比较。该版本 x64 Debug、Release 的 ASI 和宿主均构建成功。`dumpbin` 确认两份 ASI 为 x64、导出 `InitializeASI`，依赖为 Windows 与 MSVC 运行库，未动态依赖外部 ImGui/MinHook DLL。Debug 需要开发机的调试运行库，部署使用 Release。PE 检查见 [Debug](evidence/pe-debug.txt)、[Release](evidence/pe-release.txt)。

该轮 Release ASI SHA-256（不是当前发布文件哈希）：

```text
9EEA8C1D8C06DF987651D6B92D81681A11A5F243936E9D8A7C3898D09666A79B
```

宿主使用真实 DXGI / D3D 设备，通过同一个 ASI 的 `InitializeASI()` 和实际 MinHook 链路运行，不是独立 ImGui 演示程序。

| v0.1.0 Release 宿主 | 结果 | 证据 |
| --- | --- | --- |
| DX11 | PASS，3383 帧；显示/隐藏、尺寸变化、最小化恢复、主链选择、legacy 按住键释放全部通过 | [日志](evidence/host-dx11-release.log) |
| DX12 | PASS，3375 帧；同上，并在无关队列/辅助链先创建时正确选择主链真实队列 | [日志](evidence/host-dx12-release.log) |
| DX12 late-init | PASS，1194 次成功 Present；队列关联未知时记录错误，Insert 前后主 backbuffer 差异为 0 | [日志](evidence/host-dx12-late-release.log) |

标准测试均重复调用初始化入口，主链开菜单时有 563591 个像素变化，隐藏后与基线差异为 0。截图来自 GPU backbuffer readback，保存在 `build/test-output/*-final`。Debug 的 DX11 与 late-init 也通过过对应自动序列；最终 Debug 完成重建，正式最终宿主结论以上表 Release 为准。

安装器的 18 个回归场景已分别在 Windows PowerShell 5.1、PowerShell 7 通过，涵盖首次安装、重复安装、损坏缓存、文件冲突、清单校验、运行进程拦截、预览、卸载与保留其他插件等行为。可用 `Scripts/test-install.ps1` 复现。

## v0.1.0 交互检查：历史记录

在 DX11 宿主中通过 Computer Use 实际输入验证：

- 启动菜单隐藏，短暂显示欢迎提示；Insert 显示菜单。
- 简体中文、概览状态、修改功能占位页、设置页正常显示。
- 暗色 / 亮色切换，界面缩放到 1.07，拖动窗口后 `ui.ini` 保存主题、缩放和坐标。
- 退出后重启宿主，亮色、1.07 缩放及窗口坐标仍保留；[实际截图](../output/ui-light.png) 来自最终构建的 GPU 读回。
- 关闭按钮只隐藏菜单，再按 Insert 可以重开。
- Alt+Tab 失焦时菜单隐藏，正常关闭宿主退出。

这组检查不代表游戏中的输入验收。完整 raw-only 按住输入、GetRawInputBuffer、延迟读取 raw handle、DirectInput/XInput/异步轮询路径，以及跨显示器 DPI 切换尚未实测。当前 raw 处理覆盖同步 WM_INPUT / GetRawInputData 路径；已交给游戏的 raw 按下状态会在真实释放包到来时释放。

未强制触发设备移除、GPU fence 超时或后端对象创建失败；这些防御路径已实现，但未经过故障注入验证。缺少 DX12 队列关联的初始化回退由专门宿主模式验证。

## v0.1.0 真实游戏：当时的环境与限制

已在 `E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64` 安装固定 Loader、`version.ini` 和 `scripts\GlossTrainer.asi`，保留原插件。`version.log` 及 trainer 日志确认 Loader 自动加载 ASI，并调用初始化入口。

| 启动模式 | 实际结果 | 验收结论 |
| --- | --- | --- |
| 安装前 `-dx12` | 游戏在 FAsyncLoadingThread 访问违规后退出 | 现有环境基线失败 |
| 安装后 `-dx12` | DX12 交换链、中文 ImGui、真实命令队列和后端初始化成功；随后游戏仍在同一资源加载线程崩溃 | 自动加载和后端初始化通过；游戏内菜单未验收 |
| 安装后 `-dx11` | 崩溃记录的 RHI 为 D3D11；捕获交换链前发生空地址访问违规 | 未进入可操作画面，不能标记游戏内通过，也没有证据将其称为明确拒绝 DX11 |

安装前后的 DX12 `PCallStackHash` 完全相同，均为 `F2A524F9359A1D73069B2179CE56656A912EF0C4`，顶部地址为 `b1-Win64-Shipping.exe + 0xCC63F52`。这证明同一崩溃在安装前已存在；尚未确定原始原因，也不证明全部插件的运行兼容性。经过提取的异常、崩溃线程和栈信息见 [基线证据](evidence/baseline-dx12-crash.json)、[安装后证据](evidence/installed-dx12-crash.json)。[Trainer 游戏日志](evidence/trainer-game-attempts.log) 记录了两次模式尝试。

18:54 更新并安装最终 Release 后再次尝试 DX12：ASI 哈希与上述构建一致，18:55:04 后端初始化成功，18:55:12 仍出现相同崩溃栈。[最终加载日志](evidence/trainer-game-final.log)、[最终崩溃记录](evidence/final-dx12-crash.json) 已保存。验证结束时游戏与测试宿主均已退出。

首轮安装后的早期初始化曾停滞。移除主动创建 DXGI 工厂、窗口及 D3D11 设备的启动探针后，游戏已能继续至真实 DX12 后端初始化。最终方案只同步安装发现 Hook，在捕获真实交换链后创建资源。

该轮原插件文件 SHA-256 在安装后保持不变。这不是当前目录仍装有这些插件的证明；后续磁盘调查时它们已不在原目录，详见 [游戏映射的文件身份](fling-reference/game-mapping.md)。

```text
dwmapi.dll         6AEA3C390442053FA64F306D7AC6ECDA6F82C8FF1B7AC909920F94636D96E1AB
wukong_minimap.dll 63903ED2CA7DACD9516090141F7C5015AFB46D5B9AE9FC3A8A738B769EE047B7
```

该轮未能完成游戏里的反复开关菜单、按住输入切换、最小化恢复、退出和插件功能共存验收；当时需先恢复游戏环境正常启动。该轮没有改动原有插件配置或游戏存档；后续已进入游戏的 v0.2.0 验证按本文开头的当前状态单独记录。
