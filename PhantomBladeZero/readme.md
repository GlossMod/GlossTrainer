# 影之刃零 · GlossTrainer

版本 **1.2.0**，Windows x64 / C++20 / Dear ImGui / MinHook。适配游戏 **CL-302256_JKS-17280**。新增免费随意传送、移除传送限制、自动收集、原生战利品高亮、相机调节及全部背包饰品生效，并支持物品数量设置。旧 GM 空实现仍显示不可用。完整更新内容见 [v1.2.0 发布说明](docs/release-notes.md)，实机验证范围见 [验证记录](docs/verification.md)。

免费传送不消耗穿行符；移除传送限制可独立开启，允许在铃铛附近等受限位置传送到已激活铃铛。相机支持 FOV、距离和垂直偏移，距离仍受当前相机模式约束。全部饰品生效会应用背包中未装备饰品的效果，关闭只移除修改器添加的效果。

沿用黑神话：悟空模块的窗口比例、金色强调色、亮暗主题、导航与输入处理。显示概览、修改功能、物品、设置、关于五页；传送页已隐藏。God 模式及目标/NPC 清除入口已移除，金币、伤害/防御/姿态倍率、移速、无冷却等未适配项保持隐藏。用户已明确消耗条为杀气，使用一个“无限杀气”开关。“可提交”仅表示调用条件成立，状态读回与实际战斗效果分别记录。

修改功能页采用与悟空一致的“功能 / 快捷键 / 参数 / 状态”四列表格。开关使用金色拨动样式，状态未知或正在处理时禁用。有明确上下限的参数使用滑块，Ctrl+点击可精确输入，点击“应用”才提交。点击快捷键列录入键盘组合，支持 Ctrl / Shift / Alt，Escape 取消、Delete 清除，冲突时显示对应功能名；当前界面开关组合键不可分配给功能。绑定和参数保存在 `GlossTrainer/PBZ/hotkeys.ini`。功能快捷键仅在游戏前台且菜单隐藏时触发，避免输入时误操作。调试信息由设置页 Debug 开关控制，新配置默认关闭。

“修改功能 → 资源”中，自动收集默认 10 米，可调 1–50 米；附近战利品高亮默认 50 米，可调 1–100 米。两项距离滑块分别以米显示，启用后点击各自“应用”更新；成功应用的距离随开关状态保存在 `GlossTrainer/PBZ/features.ini`，重启后恢复。旧配置没有距离参数时保留原开关状态并使用新默认值。自动收集支持掉落物、纸条和宝箱，角色忙碌或战斗中等待合适交互状态。高亮复用澄心洞照的原生绿色光晕和距离控件，可隔墙显示；识别到单一种类的真实奖励时使用对应物品图，未知或多种奖励保留通用收藏标记。已开启宝箱会从高亮和自动收集候选中排除。高亮只包含当前世界已加载、游戏已登记且符合拾取类型的物品，最多显示最近 32 个；不再绘制黄色圆点及“战利品”悬浮文字，不操作手动 V 的技能和任务追踪，也不提供屏外贴边箭头。

附近目标查询和提示创建分批执行，优先处理近处物品；物品图标异步加载，稳定显示不重复查表或换图。物品目录与图片处理也分批进行，性能日志改在后台写盘，减少集中工作和周期卡顿。原生绿色提示已在实机画面中出现；本轮逐物品图标及已开宝箱过滤通过构建和离线回归，最终画面、关闭清理及密集场景性能仍待进一步实机验收，不据此承诺固定 FPS 提升。

物品页为动态图标网格，按道具、药品、饰品、主武器、煞兵、资源及收藏品分类，支持名称搜索和每页 20 项分页。从五张定义表提取 790 个条目，另有“意识尘埃”资源；也可搜索“意识粉尘”。默认隐藏没有有效本地化名称的条目。书页和花瓣使用父项名称与自己的副标题，保留独立 ID，详见[收藏品名称与分类](docs/collectible-names.md)。图片从运行中游戏的 Texture2D 动态加载、读取和解码，不打包图集。悬停显示描述，点击图标进入数量确认。连招标签通过游戏 DT_RichTextInputAction 和当前 Enhanced Input 映射解析，随键鼠/手柄与改键刷新；尚未解析出的动态数值明确显示待读取。

选择物品后可增加或设置数量，设置为 0 表示清零。减量仅开放已核对的普通道具与两种永久属性道具；未核对类型会明确拒绝。药品、饰品、主武器、煞兵及书籍/书页/记忆花/花瓣单次上限 1，已拥有时不重复发放；普通道具单次允许 1–999。这里是适配器请求限制，不代表已证明所有道具的游戏内堆叠上限。只有同一玩家会话内实际库存变化符合请求才显示完成；调用返回、排队或原物品仍存在都不算发放成功。

## 使用与构建

设置页提供“中文 / English”界面语言切换，即时生效并独立保存到 `GlossTrainer/PBZ/language.ini`，下次启动保留选择。导航、功能目录、状态和更新提示随界面语言切换；游戏返回的物品名称、描述和自定义内容保持原文，不改变游戏自身语言。

功能开关自动记忆到 `GlossTrainer/PBZ/features.ini`，下次启动或进入新的玩家场景时，在游戏与对应功能就绪后恢复。仅记住可逆开关；发放物品、恢复生命等一次性操作不会重放。启用须执行验证成功后保存，关闭会取消下次自动启用；恢复失败不会在同一场景反复执行。界面状态仍以游戏实际读回为准。

启动界面时异步检查更新，也可在设置或关于页手动检查。只识别 `PhantomBladeZero/vX.Y.Z` 及对应 `GlossTrainer-PhantomBladeZero-vX.Y.Z-win64.zip`，不会把悟空或旧裸版本当作 PBZ 更新。无正式发布时显示“暂无正式发布版本”，网络失败可重试；检查结果不限制功能。发现新版本后打开 GitHub 发布页下载，不自动替换运行中的文件。关于页同步当前／最新版本、作者、许可证、源码、下载、赞助及修改器制作申请链接。

界面开关默认是 `Insert`，可在设置页“菜单与布局”点击按键框录制组合键，支持 Ctrl / Shift / Alt，Escape 取消；不能留空，可恢复默认。新按键原子保存到 `GlossTrainer/PBZ/hotkeys.ini` 后立即生效，下次启动保留；录制或保存失败时保留原键。与功能快捷键冲突时需重新选择，底部和启动提示同步显示当前按键。失去焦点时菜单自动收起。设置页可切换亮暗主题、调整 0.8–1.5 倍缩放及重置布局，拖动顶部空白区域可移动窗口。主题、缩放、位置、当前页面和 Debug 开关自动保存至 ASI 所在目录的 `GlossTrainer/PBZ/ui.ini`；日志在 `GlossTrainer/PBZ/logs/trainer.log`。Debug 关闭时隐藏请求历史、实际读回和开发诊断。界面实际尺寸会适配窗口大小。

```powershell
xmake f -y -p windows -a x64 -m release --game=PhantomBladeZero
xmake build PBZ
xmake build PBZGameplayTests PBZMouseShortcutTests PBZImeTests
xmake build PBZUpdateTests
xmake build PBZLocalizationTests
.\build\release\PBZGameplayTests.exe
.\build\release\PBZMouseShortcutTests.exe
.\build\release\PBZImeTests.exe
.\build\release\PBZUpdateTests.exe
.\build\release\PBZLocalizationTests.exe
```

产物为 `build/release/GlossTrainer.asi`，构建目标仍叫 `PBZ`；Debug 使用 `-m debug`，产物位于 `build/debug/`。旧文件 `PBZ.asi` 必须移出游戏的 scripts 目录，避免重复加载。PBZ 已注册独立的正式发布通道 `PhantomBladeZero/vX.Y.Z`，默认构建仍为悟空模块。

存在本地 `Tests/host.cpp` 时，可构建和运行图形宿主预览：

```powershell
xmake build PBZHost
.\build\release\GlossTrainer.PBZ.Host.exe --dx12 --asi="E:\GitHub\GlossMod\GlossTrainer\build\release\GlossTrainer.asi"
```

将 `--asi` 替换为本机实际绝对路径，宿主中按 Insert 打开界面。`Tests/` 为本地工具，未随 Git 仓库分发，不影响 PBZ 本体和输入测试构建。

自动化验证（每次使用新的输出目录）：

```powershell
.\PhantomBladeZero\validation\run-host.ps1 -Mode release -Renderer dx12 -Light -Page 1 -OutputDirectory "$PWD/output/pbz-host-release-light"
```

## 当前适配范围

支持的主程序为 `CL-302256_JKS-17280`，SHA-256 `4415933ce33476f14c837abb61a1b60af8b78cd81783896501913a7aac69e423`。旧版本需要使用与其匹配的历史修改器。原生层核对哈希、入口字节、对象归属、UFunction/FProperty 和游戏线程后才调用。启动/换图时等待有效玩家，队列会在会话变化时失效；异常不重试。`InitializeASI` 为加载入口，`DllMain` 只记录模块句柄，初始化后的模块固定至进程退出，不支持热卸载。

本次更新的构建、回归和实机结果见验证记录最后一节。旧版曾动态验证生命读取、设置、恢复，免死/无敌标签开关、时间倍率设置及恢复，以及坐标读取；God 模式的旧验证仅保留在历史记录中，当前功能已移除。状态标签验证不等同于全部实战、死亡或换图行为验证。终结技 GM 实际把背包次数与上限提高到 `2147083648` 并充能 100%，重启后仍观察到该值；可能修改存档，不能当作普通临时补满。新版旧 GM 路径（终结技大量补给、批量材料、升级/满级、天赋、图鉴等）因游戏移除实现而禁用；传送与上述 7 项未适配功能已隐藏。

历史部署使用 `platform/steam/version.dll`、`version.ini`、`scripts/PBZ.asi` 的加载链，主程序为同目录的 `PBZ.exe`；Loader 为已核验的 Ultimate ASI Loader v10.0.0 x64。新部署改用 `scripts/GlossTrainer.asi`。实际动态图标与资源单次 +1 已验证；新战斗开关、输入设备切换和改键显示需使用本轮模块继续实测。更新前退出游戏，保留存档及旧文件备份；现有 `xmake bi` 与根安装脚本仍为悟空专用，不能用于 PBZ。

验证范围与证据见 [验证记录](docs/verification.md)。图形宿主成功不代表真实游戏内兼容或功能验证。

正式发行 ZIP 内含 Loader、`scripts/GlossTrainer.asi` 和双语说明。将包内 `version.dll`、`version.ini` 与 `scripts` 一起复制到 `platform/steam`（与该目录 `PBZ.exe` 同级），升级保留配置。完整玩家说明见[中文发行说明](docs/release-readme.md)与[English](docs/release-readme-en.md)。

实现分层与已知边界见 [原生适配说明](docs/native-adapter.md)。早期静态调查见 [修改项可行性分析](docs/feature-feasibility.md)，后续动态结论以验证记录为准。

作者：小莫。项目遵循仓库根目录 GNU GPL v3，第三方许可证见 `../licenses/`。
