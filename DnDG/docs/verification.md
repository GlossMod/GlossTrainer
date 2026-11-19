# DnDG 2.0.8.1 验证记录

## 0.2 原生 ASI：开发验证

范围：Windows Steam DnDG 2.0.8.1，本机隔离副本；尚未发布。下列证据区分真实游戏、最小烟测和模拟测试。

| 验证层 | 已观察结果 |
| --- | --- |
| 原生构建 | 本轮审计使用 MSVC 14.38 x64 重新配置并构建 Debug／Release；构建日志见 [Debug](../../output/dndg-final-validation-20261119/xmake-debug-current.log) / [Release](../../output/dndg-final-validation-20261119/xmake-release-current.log)。当前 ASI 散列见 [current-artifact-hashes.json](../../output/dndg-final-validation-20261119/current-artifact-hashes-final.json)：Debug `5001E59C570AB3C2DC541EBB7C134CE2868127D7DEF45521555795A46A6A9F4E`，Release `5C2EA6C9A0663D59B665500331EBCF40F40FB1A01B4B71EFC11563A1F2A364AF`。 |
| ASI Loader / D3D12 | 固定版本 Loader 与 Godot 先后调用同一原生模块的两个入口，[双入口探针](../../output/dndg-native-bootstrap-probe-20261118-official-sdk/results.json)。此前 Release 完整 ASI [原版引擎烟测](../../output/dndg-asi-20261118/smoke-full-msvc1438.log)确认 D3D12 和 C++ 节点启动。图标修复前的构建分别通过 [Debug](../../output/dndg-asi-20261119/official-headless-debug.log) / [Release](../../output/dndg-asi-20261119/official-headless-release.log) 官方引擎无头烟测；无头结果不验证 Loader 和图形路径。 |
| 隔离包预检 | 当前 Release 已从原版输入、v10.0.0 Loader 和当前 ASI 新建 [real-acceptance package manifest](../../output/dndg-real-acceptance-20261119-r2/bundle/manifest.json)；[smoke headless](../../output/dndg-real-acceptance-20261119-r2/smoke-headless.log) 和 [smoke dry-run](../../output/dndg-real-acceptance-20261119-r2/dry-run-smoke.log) / [normal dry-run](../../output/dndg-real-acceptance-20261119-r2/dry-run-normal.log) 均退出 0。dry-run 只校验命令、哈希和隔离用户目录；smoke headless 观察到原生桥接就绪和完成标记。 |
| 历史实机画面 | 早先 Release 的正常资源包进入真实对局，Insert 显示 ImGui 面板，[2560×1440 截图](../../output/dndg-asi-20261118/normal-insert-1440p.png)。游戏底图保持像素风格，面板未使用旧 640×360 画布；该截图只验证暗色玩家页，不覆盖实战功能和亮色截图。 |
| 读写夹具 | 历史基线为[读取 30/30](../../output/dndg-asi-20261119/read-fixture.log)与[写入 44/44](../../output/dndg-asi-20261119/write-fixture.log)；当前护符溢出/fail-closed 夹具另以当前 Debug／Release probe 各通过 74/74，见[当前 Release](../../output/dndg-audit-20261119/fixture-release-current-cached.log)／[当前 Debug](../../output/dndg-audit-20261119/fixture-debug-current-cached.log)。 |
| ImGui 无头回归 | [Debug 亮](../../output/dndg-asi-20261119/overlay-debug-light.log) / [暗](../../output/dndg-asi-20261119/overlay-debug-dark.log)，[Release 亮](../../output/dndg-asi-20261119/overlay-release-light.log) / [暗](../../output/dndg-asi-20261119/overlay-release-dark.log)。检查 720p 和 1440p 堆顶牌提示与悬停效果、遮蔽/空牌堆清理、语言跟随、设置恢复、默认 Insert 和保存的 Home、标题拖动和右下角缩放。 |
| 命令模型 / 更新选择 | [模型 Debug](../../output/dndg-asi-20261119/GlossTrainerDnDGModelTests-debug-result.log) / [Release](../../output/dndg-asi-20261119/GlossTrainerDnDGModelTests-release-result.log)，[更新 Debug](../../output/dndg-asi-20261119/GlossTrainerDnDGUpdateTests-debug-result.log) / [Release](../../output/dndg-asi-20261119/GlossTrainerDnDGUpdateTests-release-result.log)。已验证本地解析只接受 DnDG 稳定标签，不代表实时 GitHub 查询已实测。 |

### 2026-11-19 图标修复与旧隔离包（历史记录）

> 本节及其 dndg-native-icons-20261119、dndg-native-dx12-20261119、dndg-asi-20261119 证据均保留为历史回归记录，不代表当前 Debug／Release ASI 身份或当前推荐包。当前身份以本页后文的 5001E59C...／5C2EA6C9... 和 dndg-real-acceptance-20261119-r2 为准。

- 图集读图先解压再裁切，空交集不入图集，按比例缩放到单格并转成 RGBA8。Godot 4.6.3 无头夹具 [Debug](../../output/dndg-native-icons-20261119/pixels-debug/result.log) 和 [Release](../../output/dndg-native-icons-20261119/pixels-release/result.log) 各通过 **13/13**：包括合成压缩图集，以及从原版 PCK 通过生产读图函数取出卡牌、幸运护符和独特筹码各一项的非空、非纯色 RGBA 像素。此结果不覆盖 D3D12 上传、实际图标显示或完整目录。
- 已重建图标修复后的 Debug／Release 隔离包。两种配置的普通包与烟测包都通过文件与隔离路径预检（[普通 Debug](../../output/dndg-native-icons-20261119/dry-run-debug-normal.log) / [Release](../../output/dndg-native-icons-20261119/dry-run-release-normal.log)；[烟测 Debug](../../output/dndg-native-icons-20261119/dry-run-debug-smoke.log) / [Release](../../output/dndg-native-icons-20261119/dry-run-release-smoke.log)）。官方引擎无头烟测确认实际 C++ 桥接启动并退出（[Debug](../../output/dndg-native-icons-20261119/headless-debug-captured.log) / [Release](../../output/dndg-native-icons-20261119/headless-release-captured.log)）；[Debug](../../output/dndg-native-icons-20261119/asi-debug-hashes.json) 与 [Release](../../output/dndg-native-icons-20261119/asi-release-hashes.json) 分别确认两种包使用对应构建的同一 ASI。[Steam 文件校验](../../output/dndg-native-icons-20261119/steam-input-hashes.json) 前后相同。新版还未在游戏窗口中复测。
- 独立测试程序调用生产 WinHTTP 代码查询 GitHub：[Debug](../../output/dndg-update-live-20261119/live-debug.log) 与 [Release](../../output/dndg-update-live-20261119/live-release.log) 在测试当时均返回 `NoRelease`（没有匹配的 DnDG 稳定发行条目），未使用其他游戏版本；这不验证游戏内「关于」按钮的显示。[本地打包工具回归](../../output/dndg-update-live-20261119/packaging-tests.log) 24/24。

### 2026-11-19 原版导出引擎无头烟测

图标修复后的 [Debug](../../output/dndg-native-dx12-20261119/stock-headless-debug.log) 和 [Release](../../output/dndg-native-dx12-20261119/stock-headless-release.log) 隔离包，均由包内原版 `DnDG_64.exe`（Godot `4.6.3.stable.custom_build`）直接无头加载同名最小 PCK。两次均确认实际 C++ 桥接启动、独立烟测用户目录和正常退出。启动器的无头模式只允许最小烟测包，[已验证拒绝无头启动普通包](../../output/dndg-native-dx12-20261119/normal-headless-rejected.log)。这不是新版 ASI 在真实游戏窗口的 DXGI Hook 和玩法验收。

### 2026-11-19 D3D12 离屏像素回归

同一份生产 ImGui 面板、图标图集和微软雅黑加载代码，接入无窗口 D3D12 WARP 渲染目标，用 GPU API 回读 PNG。[8 组结果及散列](../../output/dndg-native-dx12-20261119/capture/results.json)覆盖 Debug／Release、亮／暗色和 1280×720／2560×1440。绘制坐标与帧缓冲 1:1，默认面板分别为 380×314 和 597×493，游戏中心位置保持未覆盖；两种分辨率均校验中文字形、首次和增量图集上传、悬停堆顶效果、卡牌右键菜单，以及重排后丢弃命令仍指向最初选中的 ObjectID。没有调用游戏进程或 Win32 输入。

- 1440p 原图：[亮色玩家页](../../output/dndg-native-dx12-20261119/capture/release-light-1440/player.png) / [暗色玩家页](../../output/dndg-native-dx12-20261119/capture/release-dark-1440/player.png) / [卡牌右键菜单](../../output/dndg-native-dx12-20261119/capture/release-dark-1440/card-menu.png) / [堆顶悬停](../../output/dndg-native-dx12-20261119/capture/release-dark-1440/preview.png)。
- 该回归使用模拟对局快照和合成图标，命令只读回本进程队列。它证明可见像素和界面操作变化，不验证原版贴图在游戏中的显示、MinHook／DXGI 交换链、原版 HUD 更新或保存行为。未改动本轮交付的 ASI。

### 实战尚待验收（当前构建）

推荐优先使用当前已绑定 Release ASI 的 [最新隔离包](../../output/dndg-real-acceptance-20261119-r2/bundle/manifest.json)。如需从头复核，使用 `output/dndg-real-acceptance-20261119-r2/bundle`；旧的 final2 和 audit 包只保留为历史证据。运行 `native_bundle.py launch --bundle <bundle 绝对路径>` 前，必须确认没有其他 `DnDG_64.exe` 进程；只在普通遇敌且轮到玩家的时段修改，不使用原版存档。旧的 `dndg-native-icons-20261119/verified-release` 仅保留为历史包，不作为当前验收目标。

1. 将生命、护盾、筹码分别改为合法的新数值，确认面板读回、原版 HUD、关面板后继续出牌均一致；用超范围值确认拒绝。
2. 确认袖中牌和桌面牌显示原图，在普通牌和带效果牌上依次执行悬停、右键丢弃／更换／添加，验证只改所选卡且游戏牌堆、分数及下一轮状态一致。
3. 用图标格栅点击添加独特筹码和幸运护符，然后各替换／移除一次，验证原版 HUD 与下一场遭遇仍一致。
4. 启用下一张牌、收起面板，悬停左上角确认描述，抽走牌后确认更新；开关反复切换并验证空堆、场景切换和遮蔽不会泄漏牌面或闪退。
5. 比较 1280×720／2560×1440 的亮暗截图、中文及英文截断、中文搜索输入；切换并重启确认主题、显示牌开关及自定义快捷键恢复；验证关闭面板后原版输入恢复。最后从「关于」检查 GitHub，确认按 DnDG 通道反馈，不以无关游戏的版本提示更新。

## 适配依据

- REA MCP 确认 `DnDG_64.exe` 是 PE x64，并记录了 SHA-256。当前 REA Windows provider 无法打开这份 Godot PCK；脚本和项目配置使用 GDRE Tools 2.7.4 在本地只读分析。
- CE MCP 通过本机 CE Bridge 只读确认原版进程和文件版本 2.0.8.1。未写内存，未注入。CE 的两条模块枚举接口报告计数不一致，未作为适配依据。
- `DeckController` 的 `sleeve_pile` 和 `play_pile` 对应袖中手牌和桌面牌；抽牌流程先取 `draw_pile[0]`。独特筹码和幸运护符分别取自 `RelicController` 和 `LuckyCharmController`；难度修饰筹码不在可编辑列表中。
- 原版 EXE 不接受 `--main-pack`。构建把字节相同的 EXE、Steam DLL 与同名修改版 PCK 放在独立目录下，不覆盖 Steam 安装，也不绕过 Steam 校验。

## 已验证

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 本地取证 | 原版 PCK 共 9,309 项资源；REA PE 身份与 CE 版本核验 | [GDRE 日志](../../output/dndg-trainer-20261118/gdre-recover.log)、[REA 记录](../../output/dndg-trainer-20261118/rea-evidence.json) |
| 构建和启动器 | 24 项 Python 测试通过：版本门禁、哈希及路径检查、拒绝覆盖、进程门禁、隔离目录及失败处理 | [测试日志](../../output/dndg-trainer-20261118/recheck-packaging-tests.log) |
| 模拟对局 | 63 项断言通过：生命、护盾、筹码、只读堆顶预览、袖牌和桌牌增删替换；对局状态门禁与无效道具操作不改变游戏状态 | [测试日志](../../output/dndg-trainer-20261118/recheck-godot-fixture.log) |
| 原游戏正常启动 | 初版独立副本的 Steam 初始化返回成功；原版 4.6.3 引擎加载资源、运行修改器 autoload 并创建隔离进度 | [启动日志](../../output/dndg-trainer-20261118/normal-startup-redacted.log) |
| 原版完整性 | 原 EXE、PCK、Steam DLL 和纳入基线的 5 个原版 `.cfg` 均保持同一 SHA-256；不包含缓存和日志比对 | [安装文件比对](../../output/dndg-trainer-20261118/vanilla-game-comparison.json)、[存档文件比对](../../output/dndg-trainer-20261118/vanilla-save-comparison.json) |
| 双主题 UI | 参考《影之刃零》《黑神话：悟空》的源码和界面，用 `custom-imagegen` (`model_providers.custom`, `gpt-image-2.5`) 生成并检查原型；Godot 4.6 的无头和图形测试均通过，含导航、编辑门禁、开关及快捷键持久化、640×360 中英文布局 | [原型](../../output/dndg-shared-ui-20261118/prototype-dual-theme.png)、[提示词](../../output/dndg-shared-ui-20261118/prototype-prompt.txt)、[实装截图](../../output/dndg-shared-ui-20261118/player-theme-comparison.png)、[验收记录](../../output/dndg-shared-ui-20261118/results.json) |
| 最终构建 | 核对包内脚本与当前源码的 SHA-256；原版 4.6.3 引擎在隔离 smoke 包中装载两份脚本，退出码 0，无脚本错误；正常模式仅做非启动的 dry run | [构建](../../output/dndg-shared-ui-20261118/verified-build.log)、[smoke](../../output/dndg-shared-ui-20261118/verified-smoke.log)、[dry run](../../output/dndg-shared-ui-20261118/verified-dry-run.log) |

## 0.1.1：移动和缩放窗口

- 只在标题栏拖动窗口；右下角手柄同时调整宽度和高度。嵌入式窗口在游戏视口内移动和缩放，窗口重新打开时保留本次运行的位置和尺寸，不写入配置。
- 18 项几何与输入断言分别在 Godot 4.6 无头、640×360 图形和 1280×720 图形模式通过。覆盖连续拖动、最小尺寸和边界、视口缩小、关闭按钮与 Insert 重新打开。亮暗主题和快捷键的既有界面测试也通过。
- 原版 4.6.3 引擎通过新版包的加载检查，未启动正常对局。参考手柄的[原型](../../output/dndg-window-20261118/prototype.png)由 `custom-imagegen`（`model_providers.custom`，`gpt-image-2.5`）生成；另见[原型提示词](../../output/dndg-window-20261118/prototype-prompt.txt)、[缩放画面](../../output/dndg-window-20261118/fixture/resized-panel.png)、[图形测试日志](../../output/dndg-window-20261118/window-visual.log)和[原版引擎检查](../../output/dndg-window-20261118/smoke.log)。

从仓库根目录复测（将 `$godot` 设为本机 Godot 4.6 控制台可执行文件的绝对路径）：

```powershell
$fixture = 'output/dndg-window-geometry-local'
New-Item -ItemType Directory -Force "$fixture/gloss_trainer" | Out-Null
Copy-Item DnDG/validation/window_fixture/project.godot,DnDG/validation/test_window_geometry.gd "$fixture/"
Copy-Item DnDG/src/*.gd "$fixture/gloss_trainer/"
& $godot --headless --path $fixture --script res://test_window_geometry.gd
# 去掉 --headless 可额外输出 resized-panel.png。
```

## 0.1.2：紧凑面板、图标编辑与预见

- 640×360 逻辑视口默认 286×222、右上内距 12，占视口约 28%，而 0.1.1 约 62%。继续支持标题栏移动和右下角缩放，重开保留本次运行的几何状态。
- 先生成并查看 [原型](../../output/dndg-compact-20261118/prototype.png)（`model_providers.custom` / `gpt-image-2.5`，[提示词](../../output/dndg-compact-20261118/prototype-prompt.txt)），再修改 UI。图形夹具只读挂载原版牌面和道具贴图并用模拟对局数据渲染：[中文玩家页](../../output/dndg-compact-20261118/main-fixture/player-dark-zh.png)、[中文卡牌菜单](../../output/dndg-compact-20261118/main-fixture/cards-dark-zh.png)、[英文亮色道具网格](../../output/dndg-compact-20261118/main-fixture/items-light-en.png)。
- 旧数值输入的 `SpinBox` 静默刷新会同步 Value、却可能留下旧的显示文本，后续 Apply 会再用旧文本提交。新版独立管理原始输入和上次读回值，避免轮询与失焦覆盖草稿。原版日志中另有 `+100` 击杀奖励触发的只读字典错误，调用栈不经过修改器；修改器直接设置筹码值与 HUD，不使用该奖励动画路径。详见 [原因与资源映射](../../output/dndg-adapter-20261118/evidence.md)。
- 预见复用原版 `ForesightNodes.tscn` 的眼睛、云雾、遮罩资源和 `HoverBubble.tscn` 的详情气泡。不调用 `DeckController` / `DeckDisplay` 的预见入口，也不实例化会触发卡牌逻辑的副本。抽走堆顶、原版预见正在显示、玩家操作条件变化或关掉开关时，清理修改器自己的节点，不清除原版状态。
- Godot 4.6 无头： [几何 19 项](../../output/dndg-compact-20261118/test_window_geometry.log)、[输入/交互 29 项](../../output/dndg-compact-20261118/test_compact_overlay.log)、[对局适配 91 项](../../output/dndg-compact-20261118/test_game_adapter.log)、[预见视图生命周期 11 项](../../output/dndg-compact-20261118/test_foresight.log)全过。预见视图生命周期使用模拟牌堆和资源节点，不代表实战验收。另有 [原包资源](../../output/dndg-adapter-20261118/original-resources.log)只读加载证据。
- [构建](../../output/dndg-compact-20261118/final/build.log)、[原版引擎 smoke](../../output/dndg-compact-20261118/final/smoke.log)、[正常模式 dry run](../../output/dndg-compact-20261118/final/dry-run.log)、[原版与包内脚本哈希核对](../../output/dndg-compact-20261118/final/check-hashes.log)及 [24 项 Python 包装回归](../../output/dndg-compact-20261118/packaging-tests.log)。

## 0.1.3：预见闪退防护与面板字号

- 对上一版独立存档日志和 Windows 应用故障记录的检查没有找到可归因的用户闪退堆栈；不能确认原始闪退的唯一根因。移除了实例化原版预见场景再拆除脚本和输入节点的做法。新预见只借用现有游戏节点的眼睛、云雾、遮罩和堆顶牌面纹理，不引用牌面的运行时材质，也不改原版预见状态。修改器视图是独立的内部子节点；只有悬停时才加载原版详情气泡。资源缺失、堆顶失效或原版预见接管时，清掉自有视图或跳过不安全的展示。
- 修改器正文/导航/输入/悬停提示 12→10，品牌标题 16→13，版本和页脚 11→9（逻辑像素）。布局尺寸、拖动和缩放方式不变，也不修改原版 HUD 和预见气泡字号。参见 [原型](../../output/dndg-font-20261118/prototype.png)（`model_providers.custom` / `gpt-image-2.5`，[提示词](../../output/dndg-font-20261118/prototype-prompt.txt)）、[中文暗色](../../output/dndg-font-20261118/typography-fixture/typography-dark-zh.png) 和[英文亮色](../../output/dndg-font-20261118/typography-fixture/typography-light-en.png)实装截图。
- 隔离夹具：[预见生命周期 19 项](../../output/dndg-foresight-fix-20261118/test_foresight.log)、[适配 91 项](../../output/dndg-foresight-fix-20261118/test_game_adapter.log)、[几何 19 项](../../output/dndg-foresight-fix-20261118/test_window_geometry.log)、[交互 29 项](../../output/dndg-foresight-fix-20261118/test_compact_overlay.log)、[字号无头 92 项](../../output/dndg-foresight-fix-20261118/test_overlay_typography.log)、[字号图形 97 项](../../output/dndg-font-20261118/typography-fixture/test_overlay_typography-graphics.log)和[包装 24 项](../../output/dndg-foresight-fix-20261118/packaging-tests.log)通过。
- [原版引擎图形夹具](../../output/dndg-foresight-fix-20261118/toggle-final-redacted.log)使用 4.6.3 / Vulkan Forward+、原包场景和两张原版卡，在模拟牌桌连续 8 次通过「启用→关闭面板→显示预见→悬停详情→切换堆顶→关闭预见」并以 0 退出。不是实际对局；只使用专用隔离目录。另见[预见截图](../../output/dndg-foresight-fix-20261118/stock-engine-toggle-hover.png)和发布包的 [smoke](../../output/dndg-foresight-fix-20261118/smoke.log)、[dry run](../../output/dndg-foresight-fix-20261118/dry-run.log)。

## 0.1.4：微软雅黑与堆顶牌悬停提示

- 修改器面板、菜单和提示使用本机微软雅黑（`Fonts/msyh.ttc`）及灰度抗锯齿；未安装时沿用原字体。字号、窗口布局及游戏原版字体和缩放不变。卡片悬停提示使用不透明底。参见[双主题原型](../../output/dndg-next-card-yahei-20261118/prototype.png)、[原型提示词](../../output/dndg-next-card-yahei-20261118/prototype-retry-prompt.txt)和[中文亮色实装图](../../output/dndg-next-card-yahei-20261118/fixture/typography-light-zh-tooltip.png)。
- 开启预览并收起面板后，左上角显示「下一张：牌名」，鼠标悬停显示卡牌效果。不再依赖牌堆上的预见图标，不拦截游戏鼠标事件。预览只读取堆顶牌数据；遮蔽或无有效堆顶时不显示。
- 隔离夹具：[游戏适配 113 项](../../output/dndg-next-card-adapter-20261118/test_game_adapter.log)、[窗口几何 19 项](../../output/dndg-next-card-yahei-20261118/test_window_geometry-headless.log)、[交互 29 项](../../output/dndg-next-card-yahei-20261118/test_compact_overlay-headless.log)、预览 [无头 17 项](../../output/dndg-next-card-yahei-20261118/test_next_card_hud-headless.log) / [图形 19 项](../../output/dndg-next-card-yahei-20261118/test_next_card_hud-graphics.log)、字体 [无头 122 项](../../output/dndg-next-card-yahei-20261118/test_overlay_typography-headless.log) / [图形 127 项](../../output/dndg-next-card-yahei-20261118/test_overlay_typography-graphics.log)、[包装 24 项](../../output/dndg-next-card-yahei-20261118/packaging-tests.log)全部通过。图形测试确认加载了 Microsoft YaHei，悬停提示使用同字体，游戏主题不变。
- [原包资源夹具](../../output/dndg-next-card-yahei-20261118/game-assets-final.log)在 Godot 4.6 / Vulkan 中用原版卡片和翻译数据验证未挂载到场景树的堆顶牌、中文效果、换牌刷新及遮蔽后隐藏。参见[悬停实装图](../../output/dndg-next-card-yahei-20261118/stock-cards-hover-zh.png)。这是隔离模拟，不是实际对局验收。
- [构建](../../output/dndg-next-card-yahei-20261118/build.log)、[原版引擎 smoke](../../output/dndg-next-card-yahei-20261118/smoke.log)、[正常模式 dry run](../../output/dndg-next-card-yahei-20261118/dry-run.log)和[两个包的源码哈希检查](../../output/dndg-next-card-yahei-20261118/check-hashes.log)通过。微软雅黑未打包，Steam 原版安装文件未改动。

## 原生 ASI 迁移预研：图形后端

- 原版 `project.godot` 使用 640×360 与 `viewport` 整数拉伸。0.1.4 将面板和 HUD 渲染到同一低分辨率画面，从而在大窗口中把字、边框和图标一起放大。继续换字体不能改变该画布的分辨率。
- 原版 4.6.3 Windows EXE 使用隔离空场景实测启动了 [Vulkan](../../output/dndg-native-ui-20261118/stock-engine-smoke-vulkan.log)、[D3D12](../../output/dndg-native-ui-20261118/stock-engine-smoke-d3d12.log) 和 [OpenGL](../../output/dndg-native-ui-20261118/stock-engine-smoke-opengl3.log)，三次均以退出码 0 完成。只证明该 EXE 能启动这些后端，不证明正常场景或 ASI 接入兼容性。未生成、安装或加载原生 DnDG 插件，未改动 Steam 安装。

## 尚未验收

未操作用户正在使用的游戏窗口；未验证真实对局中的预见悬停、新版逐项编辑及跨对局保存。模拟对局和隔离 smoke 测试不能替代这两项验收。独立存档目录也不等同于隔离 Steam 账号状态。

### 2026-11-19 原生最终回归（历史记录）

该节记录上一轮原生迁移、护符溢出修复和物品 picker 测试定位修复的历史结果；后续当前构建身份以本页“当前构建与真实隔离对局边界”小节为准：

- **护符写入适配**：移除任意 7 个护符上限；显式槽位沿用游戏 `LuckyCharmController.gd`，溢出槽位使用最后槽位向右 26 px 的 fallback；在创建／挂载／初始化前完成位置预检和设置。独立 Godot 4.6.3 native-write fixture 在 Debug／Release 均通过 **74/74**，证据见 [`results.json`](../../output/dndg-native-write-20261119/results.json)、[`comparison.md`](../../output/dndg-native-write-20261119/comparison.md)。
- **D3D12 ImGui 图形回归**：修正 1440p 物品页 picker 测试不再依赖错误的全屏纵向比例或右侧误命中；按当前面板和物品行选择添加格。Release／Debug × 亮色／暗色 × 1280×720／2560×1440 共 **8/8** 通过，均覆盖生命／护盾／筹码输入应用、卡牌悬停／右键菜单、卡牌替换／添加、护符添加、独特筹码添加、堆顶牌悬停和 1:1 最终输出。完整结果见当前最终矩阵 [`results.json`](../../output/dndg-final-validation-20261119/dx12-final/results.json)；历史 picker 矩阵仍保留在 `dndg-native-picker-20261119-matrix`。
- **历史 ASI**：该历史回归记录的 Debug `05D0872BFFFF5675174CF6255528312EBF903FBAE86497051E3439BDF2880EEE`、Release `11BAE526F83C35C4434B95575E441C06B5C109DAC0D17525EF03D84B8B3B69D4` 保留在 [`hashes.txt`](../../output/dndg-native-final-20261119/hashes.txt)，不再作为当前构建身份依据。
- **历史隔离包**：`dndg-native-final-20261119` 的 Debug／Release dry-run 和 smoke/headless 结果保留在该目录；它是历史包，当前推荐包改为 [`dndg-real-acceptance-20261119-r2`](../../output/dndg-real-acceptance-20261119-r2/bundle)。原版 Steam 文件和存档未修改。
- **工具可用性记录**：REA 已对原版 `DnDG_64.exe` 做只读身份校验，SHA-256 为 `2b1283b94097c32c8e87c3b3e1bd6e0c07fdb80b2685d5235cc7ba32bcc78d09`；本机当前 REA 深度分析 provider 未配置，因此没有伪造函数级结论。CE MCP 本轮连接不可用，未执行进程附加、内存扫描或写入。

仍未宣称完成真实对局验收：尚未在用户明确授权下启动最终隔离包并逐项操作真实遇敌。当前缺口包括真实 HUD 回读、真实牌堆／物品状态变化、开启／关闭堆顶牌后的多场景生命周期，以及真实窗口下的最终视觉确认。

### 2026-11-19 当前 Real Acceptance 包

本轮重新构建并打包了当前堆顶牌悬停实现，最终包为 r2，避免继续把旧包当作最新产物：

- 推荐隔离包：[`output/dndg-real-acceptance-20261119-r2/bundle`](../../output/dndg-real-acceptance-20261119-r2/bundle)。普通包和 smoke 包均包含 Ultimate ASI Loader v10.0.0 的 `dinput8.dll`，ASI 位于 `scripts/GlossTrainer.asi`。
- 当前 Debug：`5001e59c570ab3c2dc541ebb7c134ce2868127d7def45521555795a46a6a9f4e`；当前 Release：`5c2ea6c9a0663d59b665500331ebcf40f40fb1a01b4b71efc11563a1f2a364af`。
- 当前包已完成 `normal`／`smoke` dry-run；smoke headless 观察到 `GLOSS_NATIVE_BRIDGE_READY` 与 `GLOSS_NATIVE_SMOKE_COMPLETE`。
- 本轮通过 CUA 启动当前 normal 隔离副本，确认游戏窗口和内置 ImGui 面板可见，并进入牌组／手牌选择流程；这不是完整真实普通对局验收。
- 仍未宣称已验证真实对局中的下一张牌效果、抽牌刷新、空堆／遮蔽清理、数值写回和所有物品生命周期；这些项目需要继续逐项记录。

### 2026-11-19 堆顶牌读取防御性补丁（历史构建记录）

针对“开启显示下一张牌时游戏闪退”的风险路径，`DnDG/native/game_read.cpp` 的 Godot `Variant` → `Object` 转换现在先检查 `Variant::OBJECT`，遇到空值、数字或其他非对象堆顶牌时直接 fail-closed，不再把非对象交给 `get_validated_object()`。原有“只读取 draw_pile[0]、不跳过无效顶牌、不调用原版预见”的策略保持不变。

该补丁的历史 Debug／Release ASI 分别为 `4D63AA95F0BCF3E5C635BC5B0A15B3A13847089E4FA300732BF5FACBA0AAA45` 与 `BEC4B46EDC456E8F685812F9F21EA97ADA1E995226832EE0D2CE6F6FED71C26`，对应的 final2 包仅作历史记录。当前应使用上一节的 real-acceptance 包和新散列。

### 2026-11-19 当前构建与真实隔离对局边界

当前构建和本轮审计证据如下：

| 项目 | 本轮状态 | 证据 |
| --- | --- | --- |
| 当前 Debug／Release ASI | 已验证；Debug 为 `5001E59C570AB3C2DC541EBB7C134CE2868127D7DEF45521555795A46A6A9F4E`，Release 为 `5C2EA6C9A0663D59B665500331EBCF40F40FB1A01B4B71EFC11563A1F2A364AF` | [current-artifact-hashes-final.json](../../output/dndg-final-validation-20261119/current-artifact-hashes-final.json) |
| 护符溢出与 fail-closed | 已验证；当前 Debug／Release probe 的 Godot 4.6.3 headless fixture 均为 74/74 | [Release fixture](../../output/dndg-audit-20261119/fixture-release-current-cached.log) / [Debug fixture](../../output/dndg-audit-20261119/fixture-debug-current-cached.log) |
| 原生构建和离线回归 | 已验证；当前 Debug／Release 构建与 model/update 离线测试通过；overlay 4/4、D3D12 最终矩阵 8/8，覆盖 1280×720／2560×1440 与亮色／暗色 | [D3D12 results.json](../../output/dndg-final-validation-20261119/dx12-final/results.json)、[final-delivery-audit.json](../../output/dndg-final-validation-20261119/final-delivery-audit.json) |
| 当前隔离包 build/dry-run | 已验证；本轮 real-acceptance 包使用当前 Release ASI，smoke headless／smoke dry-run／normal dry-run 均退出 0 | [package manifest](../../output/dndg-real-acceptance-20261119-r2/bundle/manifest.json)、[validation logs](../../output/dndg-real-acceptance-20261119-r2/) |
| 原版输入文件安全 | 已验证为只读观察结果；Steam EXE/PCK/DLL 当前哈希与隔离包 manifest 的 original 哈希一致 | [steam-input-hashes-after.json](../../output/dndg-audit-20261119/steam-input-hashes-after.json) |
| REA 结论 | 仅完成原版 `DnDG_64.exe` 的只读 PE 身份与 SHA-256 观察；当前 REA 深度分析 provider 未配置，没有函数级结论 | [REA 记录](../../output/dndg-trainer-20261118/rea-evidence.json) |
| CE Bridge | 本轮不可用；未执行进程附加、内存扫描、注入或写入 | 本节记录与审计日志 |

**本轮隔离包前置已验证（不等同于真实普通对局）：**

- 隔离包构建、文件哈希、独立用户目录和 dry-run 预检。
- 当前 Release ASI 被写入本轮 real-acceptance 包，且 manifest 回读一致。
- 未启动 Steam 原版目录；未修改原版文件或存档。

**本轮真实隔离对局未验证：**

- 未启动当前 `final2` 或 audit 包的普通模式进行真实遇敌操作。
- 未验证真实 HUD 对生命、护盾、筹码写入的回读。
- 未验证真实牌堆中的卡牌添加、替换、丢弃和分数变化。
- 未验证真实遭遇中的护符、独特筹码添加／替换／移除及场景生命周期。
- 未验证下一张牌在抽牌、空堆、遮蔽和场景切换中的实际显示与清理。
- 未验证真实窗口下的亮暗主题、中文／英文、快捷键恢复和关闭面板后的原版输入恢复。
- 未验证跨对局保存；隔离用户目录不等同于隔离 Steam 账号状态。

此前的 `normal-insert-1440p.png` 只证明旧 Release 包在真实窗口中显示过 ImGui 面板，不证明当前 Debug／Release ASI 或上述玩法功能已经完成真实验收。真实对局仍需在明确授权后使用当前推荐隔离包逐项记录日志、截图和退出码。



### v1.0.0 发行验证

- Debug 和 Release 的原生 ASI 构建通过。两个配置的模型与更新解析测试通过，亮暗主题 ImGui 夹具 4/4、D3D12 WARP 像素与交互矩阵 8/8，Python 打包回归 24/24。[D3D12 测试结果](../../output/dndg-release-v1.0.0-20261119/dx12/results.json)
- 最终 Release ASI 在原版 Godot 引擎的隔离 D3D12 smoke 中成功初始化游戏线程桥接，达到完成标记并退出 0。[隔离启动日志](../../output/dndg-release-v1.0.0-20261119/final-smoke-d3d12.log)
- 从最终发行 ZIP 解压进行安装，在包含空格与中文的干净模拟 Steam 目录中成功添加 7 个文件，重复执行预检为 0 个。原版 EXE、PCK 和 Steam DLL 的 SHA-256 前后相同。[安装回执](../../output/dndg-release-v1.0.0-20261119/steam-fixture-install.json)
- 此前的 Steam 原位测试到达主菜单。普通遭遇内的实际数据读回与玩法写入仍待实战验证。[原位安装记录](../../output/dndg-steam-bridge-20261119/install.json)
- Release ASI SHA-256 为 ecec3da4793d59283784cd51f4d2c40c7744f00bc3794dde655f468f16520073，发行 ZIP SHA-256 为 e547cf11be185fbc8751c3ee44ace4fc89e750d4292e979f3842ced1f7045411。
