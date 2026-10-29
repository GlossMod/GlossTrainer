# PBZ 实现与验证记录

日期：2026-10-29。当前版本：1.0.0。本文按阶段保留历史验证；最新版本与验证范围见本文末尾。0.1.0 阶段的部署记录不代表当前产物已部署，旧版实机结果不能替代新版验证。

## 初始实现范围（历史）

独立 PBZ 模块提供六页导航、功能搜索与分类、亮暗主题、0.8–1.5 倍缩放、位置与页面持久化、Insert 开关、失焦收起、中文字体与 IME 请求传递。示例功能全部为禁用状态，物品与传送为待接入空状态。构建不包含悟空的 gameplay、features、update 或本地化目录。

平台实现从悟空模块隔离复制，未更改悟空源码。PBZ 配置与日志位于 `GlossTrainer/PBZ/`。根构建允许 `--game=PhantomBladeZero` 选择开发模块；正式发布注册表及悟空安装脚本保持原状。

## 构建与自动化结果

原始记录统一位于 [output/pbz-ui-20261029](../../output/pbz-ui-20261029/)。

| 检查                                       | 结果                        | 证据                                                                                                                           |
| ------------------------------------------ | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| PBZ Debug / Release 最终构建               | 通过                        | [Debug](../../output/pbz-ui-20261029/build-debug-final.log)、[Release](../../output/pbz-ui-20261029/build-release-final.log)   |
| 鼠标输入状态机 Debug / Release             | 通过，0 failures            | [Debug](../../output/pbz-ui-20261029/mouse-debug.log)、[Release](../../output/pbz-ui-20261029/mouse-release.log)               |
| IME 集成 Debug / Release                   | 通过，部分 IMM 窗口回读跳过 | [Debug](../../output/pbz-ui-20261029/ime-debug.log)、[Release](../../output/pbz-ui-20261029/ime-release.log)                   |
| Release DX11 暗色功能页                    | PASS                        | [宿主日志](../../output/pbz-ui-20261029/release-dx11-dark-retry/GlossTrainer.Host.log)                                         |
| Release DX12 亮色功能页                    | PASS                        | [结果](../../output/pbz-ui-20261029/release-dx12-light-retry/result.json)                                                      |
| Debug DX11 暗色设置页，0.8 倍              | PASS                        | [结果](../../output/pbz-ui-20261029/debug-dx11-settings/result.json)                                                           |
| Debug DX12 亮色概览，设置 1.5 倍并自动适配 | PASS                        | [结果](../../output/pbz-ui-20261029/debug-dx12-overview-retry/result.json)                                                     |
| 最终 Release DX12 亮色设置页，0.8 倍       | PASS，卡片布局修正后复核    | [结果](../../output/pbz-ui-20261029/final-settings-light/result.json)、[截图](../../output/pbz-ui-20261029/settings-final.png) |
| 最终 Debug DX11 暗色关于页，0.8 倍         | PASS                        | [结果](../../output/pbz-ui-20261029/final-debug-about/result.json)、[截图](../../output/pbz-ui-20261029/about.png)             |
| 悟空发布解析，显式 SourceRoot              | 通过                        | [JSON](../../output/pbz-ui-20261029/wukong-release-resolution-explicit-root.json)                                              |
| 最终 ASI 哈希                              | 已记录                      | [SHA-256](../../output/pbz-ui-20261029/build-hashes-final.json)                                                                |

图形宿主检查菜单显示/隐藏的像素差异、正确主交换链选择、已按下 W 键在打开菜单时释放、后续 keyup、窗口缩放及最小化/恢复。PASS 不表示真实游戏兼容，也不表示每张恢复截图必然保留菜单：外部前台焦点变化仍会正常收起菜单。已人工查看功能页双主题、概览、设置和关于页截图；0.8 倍设置卡片的多余滚动条已修正。

首轮 DX11、DX12 和一次 Debug DX12 运行受前台焦点影响，菜单未在截图时保持显示而失败；对应日志和截图保留在 `release-dx11-dark/`、`release-dx12-light/`、`debug-dx12-overview/`。重试使用本地宿主已有的启动最小化/恢复流程取得焦点，没有修改生产 ASI 的失焦逻辑。`validation/run-host.ps1` 固化此流程、隔离配置并记录每次运行的 ASI 哈希。

IME 测试确认跨线程请求、原 HIMC 精确恢复、NOLEGACY 注册恢复和合成 WM_CHAR 中文提交；当前 IMM 环境不提供候选/组合窗口位置回读，不能由此断言真实输入法候选窗口视觉验证完成。未自动操作真实游戏或修改游戏目录。

旧发布解析脚本在 Windows PowerShell 5.1 下不传 SourceRoot 时，其参数默认值中的 PSScriptRoot 为空；此既有问题记录于 [原始错误](../../output/pbz-ui-20261029/wukong-release-resolution.json)。显式传仓库根目录成功，本次未改动发布工具。

## 原型与截图

按要求在 UI 实现前使用 `custom-imagegen` 生成并查看原型。Provider：`model_providers.custom`；固定模型：`gpt-image-2.5`。

- [最终原型](../../output/pbz-ui-20261029/prototype.png)
- [完整最终提示词](../../output/pbz-ui-20261029/prototype-prompt.txt)：同款 GlossTrainer 双主题、六页导航、金色强调、禁用功能表、固定 Insert 页脚。
- [实际双主题截图](../../output/pbz-ui-20261029/ui-preview.png)

## 后续验证边界

游戏目录静态检查发现 `platform/steam/PBZ.exe` 为约 511 MB 主程序候选，`steam/PBZ.exe` 约 1 MB；`ProjectPBZ/Binaries/Win64` 未发现主程序。尚未确定实际加载链和部署目录，因此本阶段交付构建产物及宿主预览，不提供 PBZ 自动安装，也未进入正式发布注册表。

后续接入真实游戏后需验证 ASI 加载、实际 DXGI 后端、输入焦点/鼠标恢复，再逐项实现游戏功能。当前没有任何地址扫描或游戏数据写入。

## 2026-10-29 修改项静态分析

UI 验证之后，使用 REA MCP 检查 `platform/steam/PBZ.exe` 的身份与 PE 资源，并以本地只读字符串／指针表扫描和 LLVM 反汇编补充玩法调查。生命设置、状态预设、药品补给和 UltimateShachi 资源入口具有实际代码证据；材料、升级、天赋、图鉴等仍为待运行验证候选。完整结论见 [修改项可行性分析](feature-feasibility.md)。

REA 原生反编译后端不可用，核心资源容器未解密；本轮没有伪代码或玩法资产解析结果，也没有注入、调用 GM 或读写玩法数据。进程与模块只读清单支持主程序识别，不能代替 ASI 兼容性或修改功能验证。证据保存在 [output/pbz-rea-20261029](../../output/pbz-rea-20261029/)。本轮仅更新分析文档，未重新构建 UI。

## 2026-10-29 模块目录更名

源码目录更名为 `PhantomBladeZero/`，根构建选择更新为 `--game=PhantomBladeZero`，同步 README、宿主脚本调用示例及分析报告校验脚本中的目录引用。构建目标仍为 `PBZ`。

更名后 Release 配置和构建通过，文档链接及目录引用检查通过。证据：[配置日志](../../output/phantombladezero-rename-20261029/configure-release.log)、[构建日志](../../output/phantombladezero-rename-20261029/build-release.log)、[路径校验](../../output/phantombladezero-rename-20261029/rename-validation.json)。

## 2026-10-29 CE 动态适配与部署

使用 CE MCP 在已确认主进程中验证（最初 PID 39432；用户退出并部署后为 43492）。原生代码只支持 SHA-256 `7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4`。完整分层与生命周期见 [原生适配说明](native-adapter.md)。

运行时核对 90 项 UFunction 的 native 地址/flags/参数数目，87 项无输出 exec 契约进入编译表。目录有 46 个动作或占位，其中 39 个可提交、7 个未适配。可提交表示入口和参数通过，不表示全部业务功能已经验证。

| 项目                   | 实际结果                                                                                                        | 证据                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| ---------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 游戏线程调度           | 控制器 Tick 39696 / GGameThreadId 39696；新进程为 43220 / 43220，单次构建 CheatManager                          | [v2 状态](../../output/pbz-dynamic-20261029/runtime-v2/initial-status.json)、[启动状态](../../output/pbz-dynamic-20261029/integrated-startup-status.json)                                                                                                                                                                                                                                                                                  |
| 生命读取/设置/恢复     | 640 → 750 → 850，真实属性逐次匹配；游戏内菜单设置 500 后恢复 850 也通过；1000 被游戏限制至 850，未伪报成功      | [v2 回读](../../output/pbz-dynamic-20261029/runtime-v2/health-verification.json)、[菜单回读](../../output/pbz-dynamic-20261029/integrated-startup-status.json)                                                                                                                                                                                                                                                                             |
| 免死标签               | 开、关均匹配 ActorStateComponent                                                                                | [v3 请求](../../output/pbz-dynamic-20261029/runtime-v3/tests.json)                                                                                                                                                                                                                                                                                                                                                                         |
| 无敌/God mode 标签     | 无敌能力标签及 GodMode/UnlimitedHealth 双标签开、关均匹配                                                       | [v4 请求](../../output/pbz-dynamic-20261029/runtime-v4/tests.json)                                                                                                                                                                                                                                                                                                                                                                         |
| 时间倍率/坐标          | 0.8 → 1.0 经 getter 比较通过；读取实际 double Vector 坐标                                                       | [v3 请求](../../output/pbz-dynamic-20261029/runtime-v3/tests.json)                                                                                                                                                                                                                                                                                                                                                                         |
| 终结技大量补给         | 背包次数与上限 2147083648、充能 100%，重启后仍保持；玩家 BasicAttributeSet 的 UltimateCharge 不是此入口修改对象 | [游戏内请求与统计](../../output/pbz-dynamic-20261029/integrated-startup-status.json)                                                                                                                                                                                                                                                                                                                                                       |
| 药品大量补给           | 已拥有 Hulu 的 CurrentTime/DisplayTime/MaxTime 从 2 提高到 2147083647                                           | [进度回读](../../output/pbz-dynamic-20261029/progress-validation.json)                                                                                                                                                                                                                                                                                                                                                                     |
| 单项升级               | Tang 传入 2 后等级 1→3，再传入 1 后 3→4；Hulu 和 AC_Loot 传入 1 后 0→1，参数是提升级数                          | [进度回读](../../output/pbz-dynamic-20261029/progress-validation.json)                                                                                                                                                                                                                                                                                                                                                                     |
| 发放已有药品/饰品      | 调用返回，已有物品仍存在；没有证明未拥有物品的首次发放                                                          | [请求记录](../../output/pbz-dynamic-20261029/progress-requests.json)                                                                                                                                                                                                                                                                                                                                                                       |
| Debug/Release 构建     | 通过                                                                                                            | [Debug](../../output/pbz-dynamic-20261029/build-debug.log)、[Release](../../output/pbz-dynamic-20261029/adapter-final-release.log)                                                                                                                                                                                                                                                                                                         |
| Gameplay/鼠标/IME 测试 | 两配置通过；IMM 候选/组合窗口位置读取环境不支持，明确跳过                                                       | [Gameplay Debug](../../output/pbz-dynamic-20261029/gameplay-tests-debug.log)、[Release](../../output/pbz-dynamic-20261029/gameplay-tests-release.log)、[IME Debug](../../output/pbz-dynamic-20261029/ime-tests-debug.log)、[Release](../../output/pbz-dynamic-20261029/ime-tests-release.log)、[鼠标 Debug](../../output/pbz-dynamic-20261029/mouse-tests-debug.log)、[Release](../../output/pbz-dynamic-20261029/mouse-tests-release.log) |
| 宿主亮暗 UI            | Release DX11 暗色 / DX12 亮色 PASS，含显示隐藏、输入释放、缩放恢复                                              | [暗色](../../output/pbz-dynamic-20261029/host-dx11-dark-r3/result.json)、[亮色](../../output/pbz-dynamic-20261029/host-dx12-light/result.json)                                                                                                                                                                                                                                                                                             |
| 正式启动加载           | 用户退出后部署，三文件哈希一致；新进程自动载入 ASI、DX12 渲染器和原生层，Insert 菜单可见，用户确认菜单正常      | [安装回执](../../output/pbz-dynamic-20261029/install-receipt.json)、[启动日志](../../output/pbz-dynamic-20261029/integrated-trainer.log)、[游戏内截图](../../output/pbz-dynamic-20261029/ui/in-game-dark.png)                                                                                                                                                                                                                              |

最初 native MinHook 申请近地址失败，改用经过校验的共享虚表槽；没有尝试反作弊绕过。早期 CE 暂停/分配探针未确认安装，旧进程之后重启，恢复操作因 PID 已失效而未执行。后来每次注入均重新核对目标进程；测试副本停用回调但不热卸载。第一次宿主命令在 Windows PowerShell 5.1 下因 stderr 重定向报 NativeCommandError；改用 PowerShell 7。随后一轮 DX11 因前台焦点变化失败，保留原始记录；稳定前台后的 DX11/DX12 通过。

存档快照均位于 [baseline](../../output/pbz-dynamic-20261029/baseline/)。进度操作前补拍的 [快照清单](../../output/pbz-dynamic-20261029/baseline/pre-progress-20261029-133132/manifest.json) 校验复制前、复制后及副本哈希一致；这是运行中稳定文件副本，不声称事务一致性。没有自动回滚存档。

当前不能称为全部功能完成：材料、批量升级、天赋、图鉴、NPC 筛选、移动/跨图的业务结果及保存验证仍待完成。已拥有物品的测试不能代表完整物品目录。无限体力、金币、伤害/防御/姿态倍率、移速、无冷却保持禁用；状态标签读回不替代实战伤害、死亡及换图恢复测试。

## 2026-10-29 悟空同款开关表格与真实状态

按用户提供的悟空截图，将修改功能页改成“功能 / 快捷键 / 参数 / 状态”四列表格，采用 34×20 金色拨动开关、紧凑分隔行、分类页签和默认折叠的请求历史。无敌、免死和 God mode 各自合并开启/关闭动作；单次动作保留按钮。快捷键尚未接入绑定，因此显示“未绑定”。已去除功能表外层多余滚动条。

实现前使用 `custom-imagegen` 生成并查看双主题原型：provider `model_providers.custom`，model `gpt-image-2.5`；见[原型](../../output/pbz-dynamic-20261029/ui-toggle/toggle-table-prototype.png)、[最终完整提示词](../../output/pbz-dynamic-20261029/ui-toggle/toggle-table-prompt.txt)、[原型核对](../../output/pbz-dynamic-20261029/ui-toggle/prototype-review.md)。原型中的示例开关状态不是游戏证据。

开关位置只取 `Snapshot.toggleStates`，原生适配器在已核对的游戏线程读取状态。God mode 要求两个标签均可读且一致。未知状态显示居中灰色滑块；未知、能力未就绪或请求处理中禁用开关，提交不会乐观改变其位置。会话失效或切换会清除旧状态。

| 检查                       | 结果                                                                                                                               | 证据                                                                                                                                                                                                                                                                                                                                                              |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 最终 Debug / Release 构建  | 通过                                                                                                                               | [Debug](../../output/pbz-dynamic-20261029/ui-toggle/build-debug-final.log)、[Release](../../output/pbz-dynamic-20261029/ui-toggle/build-release-final.log)                                                                                                                                                                                                        |
| Gameplay 状态/队列测试     | 两配置均 0 failures                                                                                                                | [Debug](../../output/pbz-dynamic-20261029/ui-toggle/tests-debug-final.log)、[Release](../../output/pbz-dynamic-20261029/ui-toggle/tests-release.log)                                                                                                                                                                                                              |
| 最终 Release DX12 亮色宿主 | PASS；功能表外层多余滚动条已消除                                                                                                   | [结果](../../output/pbz-dynamic-20261029/ui-toggle/host-light-final/result.json)、[截图](../../output/pbz-dynamic-20261029/ui-toggle/actual-light-final.png)                                                                                                                                                                                                      |
| Debug DX11 暗色 0.8 倍     | 布局末次调整前自动化 PASS；最终两轮在菜单采样时失焦失败，恢复后截图已人工查看正常，不记为最终自动化通过                            | [先前 PASS](../../output/pbz-dynamic-20261029/ui-toggle/host-dark-08/result.json)、[最终失败](../../output/pbz-dynamic-20261029/ui-toggle/host-dark-final/result.json)、[重试日志](../../output/pbz-dynamic-20261029/ui-toggle/host-dark-final-retry/GlossTrainer.Host.log)、[最终恢复截图](../../output/pbz-dynamic-20261029/ui-toggle/actual-dark-restored.png) |
| 部署及启动                 | 用户退出后更新 ASI，SHA-256 校验一致；新 PID 10912 的适配器 ready=true、faulted=false，游戏线程为 44104                            | [安装回执](../../output/pbz-dynamic-20261029/ui-toggle/install-receipt.json)、[启动读回](../../output/pbz-dynamic-20261029/ui-toggle/in-game-startup-status.json)                                                                                                                                                                                                 |
| 游戏内 UI 和三组开关       | 用户确认新版显示正常；菜单提交无敌、免死和 God mode 的 on/off 共六个请求均回读验证通过，最终三个状态均为 known=true、enabled=false | [游戏内请求及状态](../../output/pbz-dynamic-20261029/ui-toggle/in-game-user-tests.json)                                                                                                                                                                                                                                                                           |

部署的 Release ASI SHA-256 为 `F2CBFF15D6DDB856718CD818D7852E521812496014069DC27FCE1BBDC757BF5E`。本次只更新 ASI，保留 loader 和用户设置，旧 ASI 保存在原始证据目录。当前验证覆盖界面、调度及状态标签开关，不扩大至实战伤害、死亡、换图或其余功能的业务正确性。

## 2026-10-29 全量物品与界面收敛（已部署，待实机验证）

本轮原始证据位于 [output/pbz-items-20261029](../../output/pbz-items-20261029/)。当前源码移除 God 模式及其不可达原生执行分支，隐藏传送页及无限体力、金币、伤害/防御/姿态倍率、移速、无冷却 7 个缺少可靠契约的功能，导航收敛为概览、修改功能、物品、设置、关于五页。前文的六页、禁用占位和 God 模式验证均是对应旧版的历史记录。

物品目录改为读取五张完整定义表，不局限于已拥有背包。既有进程只读捕获共 790 个有效条目，分类如下；煞兵表有 28 个槽位，1 个为空，27 个有效。

| 分类   | 有效条目 | 定义表             |
| ------ | -------- | ------------------ |
| 道具   | 640      | DT_InventoryData   |
| 药品   | 17       | DT_MedicineTable   |
| 饰品   | 73       | DT_Accessory       |
| 主武器 | 33       | DT_WeaponData      |
| 煞兵   | 27       | DT_ShaqiWeaponData |

来源：[五张完整定义表的 ID 与本地化键](../../output/pbz-items-20261029/runtime/raw-item-definitions-pid4880.json)。原先 FModel/CUE4Parse 的加密阻塞仍保留在 [资源调查](../../output/pbz-items-20261029/assets/findings.md)；本轮没有声称已解密资源包。7 项未适配功能的属性、玩家限定及恢复缺口见 [审查记录](../../output/pbz-items-20261029/unsupported/findings.md)。

当前新增实现：

- 在控制器 Tick 的游戏线程分批通过引擎本地化解析名称及可用描述，显示内容来自实际定义，不以猜测名称或 FName 输入框代替完整目录。
- 物品页提供分类、名称搜索、每页 20 项分页与数量确认，数量默认 1；药品、饰品、主武器和煞兵按唯一类限制为 1，已拥有时不重复发放；普通道具单次请求允许 1–999。此处是适配器请求限制，尚未证明所有道具的游戏内堆叠上限。
- 发放前后检查相同玩家会话、背包、定义行和函数契约，只调用一次；只有库存精确增加请求数量才返回 Verified 并显示完成。零增量、异常、会话变化、无法回读或增量不符不会伪报成功，也不自动重试。
- 设置页新增持久化 Debug 开关，新配置默认关闭；关闭时隐藏请求历史、实际读回和开发诊断。保留亮暗主题与 Insert 菜单键。

上述内容是源码实现和测试约束。**本轮新版已部署到游戏，尚未执行新版目录、本地化或发放实机验证。**历史进程中的定义提取与旧版已拥有物品测试，均不能充当新版 790 项目录及首次发放的验证。

| 检查                             | 本轮结果                                                                      | 证据                                                                                                                                                                                                                    |
| -------------------------------- | ----------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PBZ Debug / Release 最终源码构建 | God 原生分支清理后重新构建通过                                                | [Debug](../../output/pbz-items-20261029/build-debug-final.log)、[Release](../../output/pbz-items-20261029/build-release-final.log)                                                                                      |
| Gameplay 测试 Debug / Release    | 最终两配置均通过，0 failures                                                  | [Debug](../../output/pbz-items-20261029/tests-debug-final.log)、[Release](../../output/pbz-items-20261029/tests-release-final.log)                                                                                      |
| UI 原型                          | 已生成并查看双主题物品列表、数量确认与 Debug 设计；示例数据不是游戏证据       | [原型](../../output/pbz-items-20261029/ui/items-quantity-prototype.png)、[完整提示词](../../output/pbz-items-20261029/ui/items-quantity-prompt.txt)、[核对记录](../../output/pbz-items-20261029/ui/prototype-review.md) |
| Release DX12 亮色物品页宿主      | 本地图形宿主 exitCode=0，使用最终原生分支清理前的构建；不代表实机物品功能验证 | [结果](../../output/pbz-items-20261029/host-light-items-final/result.json)                                                                                                                                              |
| Release DX12 暗色设置页宿主      | 最终包构建 exitCode=0；已查看截图，Debug 默认关闭                             | [结果](../../output/pbz-items-20261029/host-dark-settings-controlled/result.json)、[截图](../../output/pbz-items-20261029/host-dark-settings-controlled/open.png)                                                       |
| 最终 Release 打包                | 已打包并记录哈希                                                              | [回执](../../output/pbz-items-20261029/package/receipt.json)、[PBZ.asi](../../output/pbz-items-20261029/package/PBZ.asi)                                                                                                |
| 新版部署                         | 14:44 用户退出游戏后备份旧版，仅替换 ASI；文件哈希一致                        | [部署回执](../../output/pbz-items-20261029/deploy-144404/receipt.json)                                                                                                                                                  |
| 新版实机功能                     | 待启动验证                                                                    | 不能沿用前文旧版游戏内请求作为本轮通过证据                                                                                                                                                                              |

两轮早期暗色宿主因前台焦点检查 `foreground-target=0` 失败，原始记录保留在 [host-dark-settings-final](../../output/pbz-items-20261029/host-dark-settings-final/result.json) 和 [host-dark-settings-dx12](../../output/pbz-items-20261029/host-dark-settings-dx12/result.json)，不改记为通过。稳定前台后的受控暗色设置页检查通过。亮色宿主记录的 ASI 哈希以其结果文件为准；最终打包产物 `package/PBZ.asi` 的 SHA-256 为 `3BF9E93B124B10722D5CBEE7E29570EC5EC98D11870C67F4574C54A63EA4F764`，与受控暗色宿主一致。

用户确认退出游戏后，已备份旧版并部署本轮产物，保留 loader 和用户设置。上述内容为 14:44 阶段记录；后续用户已经确认物品列表正常、功能可用，以下记录继续更新验证范围。

## 2026-10-29 动态图片、资源与战斗/连招修复

15:48 图标包已部署，游戏日志及用户截图确认主武器真实 RenderIcon 显示成功。物品图片从游戏运行时加载读取，不打包图集；图片桥、解码缓存和旧 GPU 资源退役的测试记录见 `../../output/pbz-icons-20261029/`。原图标包文件名仍是 PBZ.asi，不能与本轮改名混淆。

“意识粉尘”的实际本地化名称是“意识尘埃”。`AddDustNum(1)` 实测从 25,953,552 增至 25,953,553，同一会话精确增量为 1；见 [资源发放](../../output/pbz-icons-20261029/runtime/dust-grant-pid57944.json)。界面搜索兼容“意识粉尘”别名。默认列表隐藏无有效名称的条目，正常显示 418 项，Debug 可查看完整 791 项定义与资源。

本轮按用户确认将消耗条合并为一个“无限杀气”；新增完美格挡判定和自动完美闪避源码，删除附近目标清除及清除 NPC 的目录与原生契约。连招缺失来自此前直接删除 InputAction 等标签，现保留原始描述并动态关联游戏动作表和当前键鼠/手柄映射；五种武器动作的映射链见 [运行时读回](../../output/pbz-combat-20261029/input-richtext/action-binding-validation-pid57944.txt)。动态数值尚未完整解析，明确显示待读取。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Debug / Release 构建 | 通过，文件名 GlossTrainer.asi，目标名 PBZ | [Debug](../../output/pbz-combat-20261029/build-debug-final.log)、[Release](../../output/pbz-combat-20261029/build-release-final.log) |
| Gameplay 回归 | 两配置 0 failures，包含清除功能拒绝与三个开关定义 | [Debug](../../output/pbz-combat-20261029/gameplay-tests-debug.log)、[Release](../../output/pbz-combat-20261029/gameplay-tests-release.log) |
| 富文本回归 | 两配置 1095 checks / 0 failures；269 条真实文本、323 个按键标签 | [Debug](../../output/pbz-combat-20261029/richtext-tests-debug.log)、[Release](../../output/pbz-combat-20261029/richtext-tests-release.log) |
| 杀气/闪避规则测试 | 两配置 19 checks / 0 failures；数值边界、无效读数、重复来袭 | [Debug](../../output/pbz-combat-20261029/combat-tests-debug.log)、[Release](../../output/pbz-combat-20261029/combat-tests-release.log) |
| 暗色 DX12 / 亮色 DX11 悬停宿主 | 通过并查看截图；为布局样本，无游戏连接 | [暗色](../../output/pbz-combat-20261029/host-keycaps-dark/GlossTrainer.Host-open.png)、[亮色](../../output/pbz-combat-20261029/host-keycaps-light/GlossTrainer.Host-open.png) |
| 补充原型 | custom-imagegen 返回 429 usage_limit_reached；沿用已审阅网格布局 | [记录](../../output/pbz-combat-20261029/ui/prototype-status.txt) |
| 16:47 部署 | 用户退出后备份旧包，改装 GlossTrainer.asi，移除加载目录 PBZ.asi | [部署回执](../../output/pbz-combat-20261029/deploy-20261029-164714/receipt.json) |

16:47 部署 SHA-256 `9BF3CE923570C0545F5227298881A8C111FCE50F64E4126D8B0B5A752F6B42AB`。[包回执](../../output/pbz-combat-20261029/package/receipt.json)。随后 PID 57612 已确认启动、三战斗开关 on/off 回读及武器连招键鼠标签，见 [开关](../../output/pbz-combat-20261029/runtime/combat-on-pid57612.json)、[按键](../../output/pbz-combat-20261029/runtime/input-descriptions-pid57612.json)。这些记录没有证明真实消耗或来袭效果，后续验证如下。

## 2026-10-29 17:25 无限暗源、最大生命与快捷键

已移除免死模式与旧 `set_health` ID；新增 `set_max_health` 和 `infinite_dark_source`。快捷键由用户点击列录入，支持键盘 Ctrl/Shift/Alt 组合，冲突名称提示、Escape 取消、Delete 清除。游戏前台且菜单隐藏时执行，输入层只排队；实际执行使用当前能力、开关读回和参数验证，保存至 `GlossTrainer/PBZ/hotkeys.ini`。

暗源采用独立的 `BasicAttributeSet.SpiritShadow`（+0x8f8，current +0x904），上限为 `SpiritShadow_Max`（+0x908，current +0x914）。PID 57612 用户使用械兵前 4/4，使用后 0/4，杀气仍为 440；[读回](../../output/pbz-combat-20261029/runtime/dark-source-after-use-pid57612.json)。开启时通过引擎属性接口补满，再仅保护本地角色的暗源扣减，不修改上限；与杀气开关独立。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Debug/Release 构建与 Gameplay/Hotkey/MouseShortcut/CombatRules | 两配置通过；快捷键各 379 checks，战斗规则各 19 checks | [结果](../../output/pbz-combat-20261029/tests-final.json) |
| 暗色 DX12 / 亮色 DX11 功能页 | 通过并查看截图；包含新开关、最大生命和长快捷键省略；仅视觉样本 | [暗色](../../output/pbz-combat-20261029/host-new-dark-controlled/GlossTrainer.Host-open.png)、[亮色](../../output/pbz-combat-20261029/host-new-light/GlossTrainer.Host-open.png) |
| 17:25 部署 | 旧版备份，哈希一致，仅加载 GlossTrainer.asi | [回执](../../output/pbz-combat-20261029/deploy-20261029-172544/receipt.json) |
| 暗源实机开关 | PID 56444 捕获 5 次扣减保护，数值 5/5；用户确认开启不减、关闭正常扣减 | [状态及用户确认](../../output/pbz-combat-20261029/runtime/darksource-user-validated-pid56444.json) |
| 快捷键实机 | 用户确认切换正常；持久化为本地往返测试通过，未单独完成实机重启保留检查 | [快捷键测试](../../output/pbz-combat-20261029/hotkeys/verification.md) |
| 自动完美闪避 | 引擎接受的自动请求计数 1；仍不代表全部来袭/武器条件验证通过 | 同上实机状态 |
| 最大生命与回血 | 当前部署版输入基础上限 1000，装备修正后实际 1042.5/1042.5；回血有效，但最终上限未匹配请求，正确显示效果未验证 | 同上请求 #2 |

当前安装 SHA-256 为 `CE85CBC4E26F694B5E2ED7DE568F1EDD3F6E4DF05D3A857CEC379A3AA76D9D16`，对应 [当前包](../../output/pbz-combat-20261029/package-darksource-hotkeys/receipt.json)。早期 `host-new-dark` 由 Windows PowerShell 将宿主 stderr 日志当异常而退出，没有记为通过；随后受控宿主已通过。

针对最大生命加成偏差，已实现最多两次受界限控制的加法/乘法补偿，每次引擎调用后复核 pawn/ASC/属性对象指针与索引；零斜率、超范围和无法求解的效果不继续猜值。Debug/Release 构建通过，新增各 15 项数值边界检查通过，见 [测试](../../output/pbz-combat-20261029/health-tests.json)。修正版 [GlossTrainer.asi](../../output/pbz-combat-20261029/package-health-final/GlossTrainer.asi) SHA-256 `8C8997A7AE820D5EF0C019030B10961D58C2246A875752B611C1429D49343359` **仅准备，未部署、未实机验证**。用户明确选择“先保留当前版本”，未再次替换游戏文件。

仍待实机覆盖：最大生命补偿版、上下调上限、复活/换图与同 pawn 属性对象替换、设备切换及游戏改键、快捷键重启持久化、完美格挡真实接触条件。源码审查和本地测试不能替代这些验证。

## 2026-10-29 基础设置、版本更新与中英文界面

实现内容：

- 五个可逆功能开关以稳定 ID 保存到 `GlossTrainer/PBZ/features.ini`。经验证成功的启用才记入；用户关闭会取消自动启用。加载后等待能力和真实状态回读，再在游戏线程恢复；一次性发物品、恢复生命等动作不保存、不重放。失败恢复在同一会话不自动重试。关闭待办跨加载保留；玩家属性或 cheat 对象重绑时主动失效旧请求和回读。
- 启动界面异步检查更新；设置、关于页均提供手动重试。仅接受 `PhantomBladeZero/vX.Y.Z` 与对应 `GlossTrainer-PhantomBladeZero-vX.Y.Z-win64.zip`，忽略悟空和裸版本。检查结果只提示，不禁用功能，不自动安装。网络错误可重试，暂无正式发布则明确显示。首轮真实宿主请求得到“暂无正式发布版本”，见下列日志。
- 关于页同步版本、作者、GPL v3、源码、下载、赞助和制作申请入口；沿用双主题卡片布局。
- 设置页提供中文 / English，保存到 `GlossTrainer/PBZ/language.ini`，下次加载保留。468 项翻译覆盖 UI、目录、快捷键及常见状态；控件 ID 保持稳定。Toast 按当前语言格式化，物品名、描述和用户文本保持原文；未知的底层诊断不猜译。原子保存失败保留原语言，同帧翻译指针有效。

| 验证 | 结果与证据 |
| --- | --- |
| PBZ 与可视化 fixture 的 Debug / Release 构建 | 均通过：[Debug](../../output/pbz-basics-20261029/build-debug-final.log)、[Release](../../output/pbz-basics-20261029/build-release-final.log) |
| Gameplay / Update / Localization 三套回归 | 两配置全部 exitCode=0，共六次运行；[汇总](../../output/pbz-basics-20261029/tests-final.json)，[可重跑脚本](../../output/pbz-basics-20261029/validate.ps1) |
| 开关记忆 | 覆盖全部五个开关往返、固定参数、无读回/未就绪等待、失败不循环重试、未验证启用不保存、一次性动作排除、坏配置、原子写失败保留文件、关闭覆盖正在执行的恢复、加载后补发关闭 |
| 版本检查 | 覆盖游戏渠道隔离、版本排序、发布稳定性、安装包与链接来源、分页/体积限制、重复请求、失败后重试；[实际网络日志](../../output/pbz-basics-20261029/visual-about-dark-final/artifact/GlossTrainer/PBZ/logs/trainer.log) |
| 语言设置 | 覆盖中英文持久化、非法配置回退、写失败保留原选择、临时字符串及跨线程指针生命周期、参数错误/快捷键冲突翻译、库存诊断保留游戏物品名 |
| 双语关于页 | DX12 中文暗色、DX11 英文亮色可视化通过并查看：[中文](../../output/pbz-basics-20261029/final-zh-dark-about/open.png)、[English](../../output/pbz-basics-20261029/final-en-light-about/open.png) |
| 英文功能页 | DX12 暗色可视化通过并查看：[截图](../../output/pbz-basics-20261029/final-en-dark-features/open.png) |
| 设置页 | 中文暗色、英文亮色及英文暗色下半页通过并查看：[中文](../../output/pbz-basics-20261029/final-zh-dark-settings/open.png)、[English](../../output/pbz-basics-20261029/final-en-light-settings/open.png)、[记忆与更新](../../output/pbz-basics-20261029/final-en-dark-settings-bottom/open.png) |
| UI 原型 | 本轮 custom-imagegen 的 gpt-image-2.5 返回 HTTP 429 usage_limit_reached；已查看并复用既有关于页与语言设置双主题原型。[记录与原型链接](../../output/pbz-basics-20261029/ui/prototype-review.md)、[提示词](../../output/pbz-basics-20261029/ui/prompt.txt)、[失败原始日志](../../output/pbz-basics-20261029/ui/generation.log) |
| 最终产物 | [GlossTrainer.asi](../../output/pbz-basics-20261029/package/GlossTrainer.asi)、[哈希回执](../../output/pbz-basics-20261029/package/receipt.json)，尚未部署到游戏 |

最终 SHA-256：`B186CB0E3A117FE3D39D4923C047BAD3C9BD81001428CF6F0B2551265217558B`。六组可视化来自最终常见诊断词条补充前的相同布局构建，各自 ASI 哈希记录于截图同目录的 `result.json`；最终源码已另行完成两配置构建及六次回归。

首次生产 ASI 自动输入宿主因打开菜单时 `foreground-target=0` 未通过，[原始失败记录](../../output/pbz-basics-20261029/host-about-dark/result.json) 保留。后续六组使用明确打开菜单的 visual-only fixture，证明布局和渲染，不证明生产输入链或游戏效果。本轮没有安装或进行游戏内重启/换图测试，仍需用最终产物在真实游戏确认自动恢复；不能把本地测试或历史实机结果视为本轮实机验证。

## 2026-10-29 版本更新为 1.0.0

按用户要求将 PBZ 的统一版本常量从 `0.1.0` 更新为 `1.0.0`，同步模块 README。标题、设置、关于页、启动日志和更新比较均读取该常量；测试中的旧版本样本及历史证据保持原样。

PBZ 在 Debug / Release 均构建通过，`PBZUpdateTests` 两配置全部通过，包含使用真实编译版本进行当前版本判断的用例。[测试汇总](../../output/pbz-v1.0.0-20261029/tests.json)、[Debug 构建](../../output/pbz-v1.0.0-20261029/build-debug.log)、[Release 构建](../../output/pbz-v1.0.0-20261029/build-release.log)。此次仅更新版本与说明，没有新增游戏内验证。

新版产物：[GlossTrainer.asi](../../output/pbz-v1.0.0-20261029/package/GlossTrainer.asi)，SHA-256 `B327C0889EB01069FE7D2BAF412B879D68FD50B2E4D85E38645C1BEFA91B782B`；[回执](../../output/pbz-v1.0.0-20261029/package/receipt.json)。尚未部署或发布，前文 0.1.0 产物与截图仅作历史记录。

## 2026-10-29 v1.0.0 正式发布准备

PBZ 已加入独立发布注册表，标签 `PhantomBladeZero/v1.0.0`，目标 `PBZ`，产物通过 `artifactName` 显式映射为 `build/release/GlossTrainer.asi`。注册器拒绝非法文件名、路径不匹配和跨游戏复用；默认悟空产物路径保持兼容。CI 按游戏运行 11 个 PBZ 验证目标，打包包含固定 Loader、唯一 ASI 及双语玩家说明，最终发布 ZIP 与 SHA256SUMS。根安装脚本仍为悟空专用。

完整预检：[Debug / Release 22 次 C++ 回归](../../output/pbz-release-20261029/tests.json) 全部通过；[发布工具汇总](../../output/pbz-release-20261029/release-tools-summary.json) 共 206 项通过，覆盖 PS5/PS7 的 PBZ、悟空打包与通道隔离。PBZ 的便携包安装契约在 [PS5](../../output/pbz-release/installer/ps5-d9007011793b4fd0b4132e2005e02c79/results.json)、[PS7](../../output/pbz-release/installer/ps7-29f528120a474e9aad759990e491f955/results.json) 各通过 45 项，证明模拟复制升级保留配置和其他插件；不是游戏内验证。图标测试新增确定性 512×512 BC3 缩小验证，真实游戏图片改为可选，云端无需本机游戏样本。

中文/English 界面与原子保存源码已纳入本次提交；版本仍为 1.0.0。当前发布步骤会由标签触发 Actions，并在成功后回读附件、核对来源提交与哈希、备份本地安装并部署该发行包内的 ASI。最终发行与部署结果以后续记录为准。

## 2026-10-29 v1.0.0 正式发布

源码提交 `a09a4fe9fc3a0916ab3ee5eeb001ce43f7d6c962` 与标签 `PhantomBladeZero/v1.0.0` 已推送。[GitHub Actions](https://github.com/GlossMod/GlossTrainer/actions/runs/40859438625) 的 Debug、Release、打包及发布任务均成功；[正式版本](https://github.com/GlossMod/GlossTrainer/releases/tag/PhantomBladeZero/v1.0.0) 已发布，包含 Windows x64 ZIP 和 SHA256SUMS，非草稿、非预发布，保留仓库原有 Latest 标记。

已下载正式附件与 CI 候选包，核对候选 artifact digest、来源提交、ZIP 完整性、正式附件 API digest、校验文件及包内五个文件的大小和 SHA-256。正式 ZIP 与 CI 候选 ZIP 字节一致，见 [核验回执](../../output/pbz-release-20261029/published-verification.json)、[CI 结果](../../output/pbz-release-20261029/ci-result.json)、[候选清单](../../output/pbz-release-20261029/ci-candidate-verified/GlossTrainer-PhantomBladeZero-v1.0.0-win64.manifest.json)。

- 正式 ZIP SHA-256：`1554F6887545B1FBB1C7F0FB3AD458566603029573B4EC3D49D649D49687E93A`。
- 正式 `GlossTrainer.asi` SHA-256：`92DA3B8BF71F48246F54F3231B16DD3F50B5324BA6EA5D4B7A6509F27EBE71BB`。

本机编译与云端编译产物的哈希不同；此次部署以正式发行包为准。以上发布和文件核验不新增游戏内功能验证。

18:57 用户再次确认退出游戏后，已完成正式包在 `E:\steam\steamapps\common\PBZ\platform\steam` 的部署核验。执行时已有 ASI 与正式包哈希相同，因此保留 ASI、Loader 和 Loader 配置，补齐双语玩家说明；安装目录五个发行文件均与候选清单一致。现有安装文件与完整用户配置已备份，配置逐文件哈希保持一致，`scripts` 下仅有一个 `GlossTrainer.asi`。见 [部署与备份回执](../../output/pbz-release-20261029/deploy-20261029-185715/receipt.json)、[最终独立复核](../../output/pbz-release-20261029/release-deployment-verification.json)。此次没有启动游戏或新增实机功能验证。

## 2026-10-29 自定义界面开关按键

设置页“菜单与布局”新增界面开关按键录制与恢复默认，默认 Insert，支持 Ctrl / Shift / Alt 组合。Esc 取消；菜单键不能清空。当前菜单组合键与功能组合键双向检查冲突。录制优先于菜单切换和功能调用；legacy/raw 两路读取相同配置，沿用按住与抬起的消费状态，录下的新键不会在同次按住时再切换菜单。

菜单键保存到现有 `hotkeys.ini` 的 `[menu:toggle]` 节；完整文件原子替换成功后才启用新菜单键，保存失败保留旧键和旧文件，并允许重试。旧配置继续使用 Insert；非法菜单配置回退 Insert，再过滤与菜单冲突的功能键。底部提示和启动提示实时读取当前组合键，中英文文案与弹窗 ID 保持一致。版本仍为 1.0.0，本轮没有改动正式发布标签或附件。

| 检查 | 结果与证据 |
| --- | --- |
| PBZ / 图形 fixture Debug、Release 构建 | 均通过；[Debug 最终构建](../../output/pbz-menu-key-20261029/build-debug-final-pbz.log)、[Release 最终构建](../../output/pbz-menu-key-20261029/build-release-final-pbz.log) |
| Hotkey / MouseShortcut / IME / Localization 回归 | 两配置共 8 次全部通过，Hotkey 每配置 541 checks / 0 failures；[结果](../../output/pbz-menu-key-20261029/tests.json)、[复跑脚本](../../output/pbz-menu-key-20261029/validate-backend.ps1) |
| 保存与录制边界 | 覆盖全部修饰键组合、双向冲突及恢复默认冲突、录制取消、Delete 不可清空、失焦取消、旧配置、坏配置回退、文件锁定保存失败保留旧键/旧文件、重试和其他功能参数保留 |
| 长组合键与双语双主题界面 | 已查看中文暗色与英文亮色设置、录制弹窗；最终[英文亮色设置](../../output/pbz-menu-key-20261029/final-en-light-key/GlossTrainer.Host-open.png)、[中文暗色录制](../../output/pbz-menu-key-20261029/final-zh-dark-capture/GlossTrainer.Host-open.png)。显示 Ctrl+Shift+Alt+F10；fixture 不连接游戏且排除生产输入实现 |
| 自定义 F10 的生产 ASI 宿主 | Release DX12 自动开关、最小化/恢复、按住 W 的合成释放及后续抬起检查通过；[结果](../../output/pbz-menu-key-20261029/input-f10-dx12/result.json)、[日志](../../output/pbz-menu-key-20261029/input-f10-dx12/GlossTrainer.Host.log)。使用窗口消息，未验证真实物理组合键或 raw-only 键盘 |
| UI 原型 | custom-imagegen / model_providers.custom / gpt-image-2.5 返回 HTTP 429 usage_limit_reached；已查看并复用既有菜单按键双主题原型。[提示词](../../output/pbz-menu-key-20261029/ui/prompt.txt)、[日志](../../output/pbz-menu-key-20261029/ui/generation.log)、[审阅](../../output/pbz-menu-key-20261029/ui/prototype-review.txt) |
| 新版产物 | [GlossTrainer.asi](../../output/pbz-menu-key-20261029/package/GlossTrainer.asi)、[回执](../../output/pbz-menu-key-20261029/package/receipt.json)，SHA-256 `DF616E5837F797F62AF5B79D08CF54B9F23A2B167716ABB542F3DDBB91B1F8B0`；游戏仍在运行，本轮未替换游戏文件 |

默认 Insert 的两轮 DX11 自动宿主因消息到达时 `foreground-target=0` 未通过，见 [首次](../../output/pbz-menu-key-20261029/input-insert-dx11/result.json)、[第二次](../../output/pbz-menu-key-20261029/input-insert-dx11-controlled/result.json)，不改记为通过；首次恢复前台后日志有 Menu opened。最终构建前第一次命令未显式选择 `--game=PhantomBladeZero`，出现目标不存在，原始日志 `build-debug-final.log` 保留，随后显式选择游戏的两配置构建通过。真实游戏中的按键录制、组合键、raw-only 设备、重启保留仍需实机验证；本地回归和宿主结果不替代这些验证。

## 2026-10-29 物品输入装饰器发现扫描节流

代码检查发现：可选的 DT_RichTextInputAction 表未加载时，旧实现在每次 ReceiveTick 的 250 ms 节流之前扫描 4096 个 UObject，完整未命中后立即从头循环。该路径即使菜单关闭也会运行，可能造成持续卡顿。用户日志没有物品扫描细节，不能据此证明唯一根因。

发现扫描使用独立调度：50 ms 间隔、每批最多 512 个对象，完整未命中后等待 30 秒才重新扫描；输入标签刷新仍保持原有 250 ms 间隔。相比旧实现，60 FPS 时最大扫描吞吐从每秒 245760 个对象降为 10240；最大允许的 200 万对象首次遍历约需 195 秒，常见较小对象数组更快。表晚加载时仍可恢复发现，物品名称和发放不依赖该可选装饰器表。

PBZItemScanTests 在 Debug / Release 均为 0 failures，覆盖批次上限、扫描间隔、完整未命中冷却、冷却结束重新扫描及对象数组缩小。记录：[Debug](../../output/pbz-item-scan-20261029/test-debug.log)、[Release](../../output/pbz-item-scan-20261029/test-release.log)。PBZ Release 编译通过，原始记录：[build.log](../../output/pbz-item-scan-20261029/build.log)。未执行游戏内复现与帧耗时验证。

### 2026-10-29 物品磁盘缓存与单次发现

物品稳定符号键、名称、分类与原始描述保存为 `DataDirectory()/item_catalog.cache`，以受支持游戏构建 SHA-256 隔离。启动后优先展示缓存，当前会话目录验证完成前禁止获取；运行时指针、FName、库存数量和授权状态不持久化。当前游戏中新出现的符号键解析后合并，未出现的旧记录保留，内容无变化时不写盘。写入采用临时文件和原子替换；超过16 MiB、版本不符或截断文件回退游戏读取。

可选按键装饰表发现每次玩家会话/背包身份变化后只检查对象数组前缀一次，随后只检查新追加对象，50ms/512对象上限；不再完整循环扫描。同一会话已经检查过的槽位若复用为该表，需要下一次会话变化才会重新发现，物品描述可能显示“按键待读取”。缓存保存的是当前语言文本，切换游戏语言后需删除缓存再启动以重新解析。

验证：PBZ Debug/Release构建成功，扫描频率、完成后不重扫、尾部新增和数组缩小的Debug/Release测试均为0失败。原始证据见 [output/pbz-item-cache-20261029](../../output/pbz-item-cache-20261029/)。尚未在真实游戏内验证；缓存序列化的多行与转义往返、版本不符及截断失败保留旧内容，Debug/Release自动测试均0失败。

## 2026-10-29 v1.0.1 发布验证

按用户指定更新日志发布：“修复 获取物品导致卡顿的问题；实现可以自定义界面按键”。统一版本常量与模块说明已更新为 1.0.1，自定义菜单键沿用前一节实现。源码提交 `7765d10344de53678cd49ff6396f110089a6ddc3` 包含物品扫描调度、文本缓存和对应测试；在独立工作树固定该提交验证，未纳入其他会话尚在编辑的战斗功能文件。

Debug / Release 两配置的 PBZ 构建与 13 套测试共 26 次运行全部通过，见 [结果](../../output/pbz-release-v1.0.1-20261029/tests.json)、[Debug 构建](../../output/pbz-release-v1.0.1-20261029/build-debug.log)、[Release 构建](../../output/pbz-release-v1.0.1-20261029/build-release.log)。PowerShell 5/7 打包各通过 29 项，便携包升级契约各通过 45 项，共 148 项，见 [发行工具结果](../../output/pbz-release-v1.0.1-20261029/release-tools-results.json)。升级契约是模拟目录复制，不是游戏内验证。

代码和标签 `PhantomBladeZero/v1.0.1` 已推送，触发 [本次 CI](https://github.com/GlossMod/GlossTrainer/actions/runs/40890532412)。本轮没有新增帧耗时、真实物品发放或菜单组合键的游戏内验证。物品文本缓存首次写盘失败后，本进程不会自动重试；换游戏语言需清除缓存，已有扫描前缀中的对象槽位复用可能延迟发现可选装饰表。正式附件和本地部署以下文最终回执为准。

[v1.0.1 正式版本](https://github.com/GlossMod/GlossTrainer/releases/tag/PhantomBladeZero/v1.0.1) 已发布。云端 Debug、Release 及发布任务均成功；下载正式 ZIP、SHA256SUMS 与 CI 候选包，核对来源提交、artifact digest、附件 digest、ZIP 字节一致性及五个包内文件的哈希全部通过，见 [发行核验](../../output/pbz-release-v1.0.1-20261029/published-verification.json)、[CI API](../../output/pbz-release-v1.0.1-20261029/ci-result.json)、[源码审查](../../output/pbz-release-v1.0.1-20261029/source-review.json)。

- 正式 ZIP SHA-256：`8B2D345E4E687E07EEDEE0F171010055D08875EC2368ABF41AAE043F8771F66E`。
- 正式 ASI SHA-256：`5AA17A2B609E3414E7317537048A280DA1F98A303C12E16B2EBBA8BD6CB75CB9`。

22:27 确认无 PBZ 进程后，将正式包部署至 `E:\steam\steamapps\common\PBZ\platform\steam`，替换 ASI 和双语说明，Loader 与其配置原样保留。旧安装五个文件和完整配置树已备份，独立复核五个安装文件、五个备份及四个用户数据文件的哈希均一致；用户数据目录未改动，`scripts` 中仅加载 `GlossTrainer.asi`。见 [部署回执](../../output/pbz-release-v1.0.1-20261029/deploy-20261029-222738/receipt.json)、[最终复核](../../output/pbz-release-v1.0.1-20261029/release-deployment-verification.json)。本轮未启动游戏，不新增实机验证结论。

## 2026-10-30 三项高优先级性能修复

- 游戏 Tick 使用轻量 IsReady；物品目录改为不可变共享快照，渲染快照不再逐帧复制名称和描述，旧快照在新发布后仍安全可读。
- 隐藏菜单的普通鼠标/键盘消息跳过 ImGui 绘制锁，保留焦点通知及既有快捷键、原始输入释放和 IME 路径。
- 按键映射改为单遍索引，精确比较设备、映射与装饰字段；未改变时复用标签。仍每250ms线性读取以检测游戏内原地重映射。
- Debug/Release PBZ完整构建通过；PBZGameplayTests、PBZMouseShortcutTests、PBZImeTests、PBZItemActionIndexTests均最终通过。Gameplay Debug首次设置保存两条断言失败，随后重跑通过，原因未确定，保留原始失败日志。IME候选/组合窗口回读因本机IMM不支持而跳过，其余集成断言通过。
- 原始证据：[性能修复验证目录](../../output/pbz-performance-20261030/)。未进行游戏内帧时间采样，不能据此保证用户反馈的所有卡顿均消失。全库存周期回读、同步图片加载及隐藏覆盖层GPU提交不在本次三项修改范围。

## 2026-10-30 现场卡顿诊断与游戏语言缓存隔离

- 现场已加载模块与前次Release文件SHA256一致，部署hash、原始日志和开关快照见[现场证据](../../output/pbz-live-stutter-20261030/)。WPR因系统性能分析策略失败（0xc5585011），没有取得ETW样本，不能将源码热点认定为持续掉帧根因。
- 修复已确认的语言缓存错误：从游戏真实配置 `%LOCALAPPDATA%/ProjectPBZ/Saved/Config/Windows/GameUserSettings.ini` 的 `CultureName` 分区为 `item_catalog_<culture>.cache`，不读取旧的无语言缓存；实际语言未知时禁用缓存复用与写入。现有后台监测线程每秒读取配置，游戏线程只读取短字符串；切换语言清除当前目录并重新验证解析。游戏配置写入可能稍晚于语言切换，检测以落盘配置为准。
- 新增低频性能统计，每10秒记录Tick、labels、inventory、images、combat、items的calls/avg/max/over2ms；分区有包含关系，不能直接相加。日志写入仍在游戏线程，仅低频汇总，统计不包含其自身输出耗时。
- PBZItemCacheTests增加文化解析测试：en-us/zh-cn、大小写规范化、缺失/空值/非法路径字符。Debug/Release构建与测试证据位于上述现场目录。尚未加载诊断构建进行游戏内验证。

### 2026-10-30 复现后的实测热点修复

原始证据：../../output/pbz-live-stutter-20261030/reproduced-before-fix.log。00:32:07采样窗口Tick平均11.751ms/max102.218ms，labels平均3.353ms/max37.225ms，库存7次平均51.397ms，combat平均3.391ms；区段有嵌套不可相加。菜单隐藏仍有明显开销。

修复：物品页隐藏或离开时暂停周期物品维护和图片队列，保留发放force回读；FName解码使用校验块地址/header的线程局部缓存；缺失名称按名称池版本负缓存，池变化重新扫描，读取失败不缓存；自动战斗功能均关闭时跳过绑定检查。新增10秒SEH读取故障计数，以继续定位异常读取。Debug/Release完整构建和缓存、Gameplay、CombatRules、ActionIndex测试通过。日志为本目录根output中的fix-*.log。尚未完成修复后游戏内性能对比，不宣称帧率恢复。

### 2026-10-30 修复后性能观察（01:06—01:10）

证据：[after-hotspot-fix.log](../../output/pbz-live-stutter-20261030/after-hotspot-fix.log)。菜单关闭稳定窗口Tick平均5.884—7.161ms；01:10:40窗口6.621ms/max22.704ms，items约0.001ms，images约0.001ms且不再出现labels/inventory采样，证明按需暂停生效。与旧窗口11.751ms相比该窗口约下降44%，非严格同场景基准。

仍有问题：combat在01:10:40平均4.394ms，占Tick约66%，其余约2.225ms尚未分段；当前磁盘设置perfect_deflect/perfect_dodge/infinite_dark_source均为1，不等同于运行时状态证明。物品页打开后库存仍约50.584ms、labels max35.246ms；01:09:50 items max1545.898ms，目录重建/文本处理等内部阶段尚未细分不能认定具体函数。读取故障绝大多数窗口0，仅一次1，无证据支持持续SEH风暴。未采集实际呈现FPS，不能据Tick次数推算显示帧率。

## 2026-10-30 v1.0.2 发布验证

按用户指定更新日志“优化性能，降低对 FPS 的影响”更新为 1.0.2。发行源码 `30e13a08adb7ac58e841463f2861fbf2b73bdde5` 包含轻量就绪查询、不可变共享物品快照、隐藏菜单输入路径、映射索引和名称缓存、游戏语言缓存隔离，以及物品页按需维护和关闭自动战斗时的提前返回。已有性能观察及其限制见上文；本次没有新增严格同场景 FPS 基准。

固定源码工作树的 Debug / Release PBZ 构建和 14 套验证目标共 28 次测试全部通过，见 [测试结果](../../output/pbz-release-v1.0.2-20261030/tests.json)、[Debug 构建](../../output/pbz-release-v1.0.2-20261030/build-debug.log)、[Release 构建](../../output/pbz-release-v1.0.2-20261030/build-release.log)、[只读审查](../../output/pbz-release-v1.0.2-20261030/source-review.json)。PowerShell 5/7 打包各 29 项、模拟升级保留配置各 45 项全部通过，共 148 项，见 [发行工具证据](../../output/pbz-release-v1.0.2-20261030/release-tools-results.json)。

代码和标签 `PhantomBladeZero/v1.0.2` 已推送，触发 [发行 CI](https://github.com/GlossMod/GlossTrainer/actions/runs/40919910639)。后续正式包哈希与本地部署以下文最终回执为准。

[v1.0.2 正式版本](https://github.com/GlossMod/GlossTrainer/releases/tag/PhantomBladeZero/v1.0.2) 已发布，云端 Debug、Release 及发布任务全部成功。下载正式附件与 CI 候选包，核对来源提交、artifact/附件 digest、ZIP 字节一致性、SHA256SUMS 和五个包内文件全部通过，见 [发行核验](../../output/pbz-release-v1.0.2-20261030/published-verification.json)、[CI API](../../output/pbz-release-v1.0.2-20261030/ci-result.json)。

- 正式 ZIP SHA-256：`7A3BDD4E0DF65934FA6E9F279F3E8A14875567369FB09C575C14B1145DD0D039`。
- 正式 ASI SHA-256：`65B0C1F6B4E636260195C7D9EC6046F8F5F1C54E40F49649318F2F9983B03EE1`。

用户确认保存退出且部署前再次确认无 PBZ 进程后，01:23 将正式包部署至 `E:\steam\steamapps\common\PBZ\platform\steam`。替换 ASI 和双语说明，Loader 与其配置保留；备份旧安装五个文件与完整用户数据目录。独立复核五个安装文件、五个备份、七个用户数据文件均通过，`scripts` 下仅有 `GlossTrainer.asi`。见 [部署回执](../../output/pbz-release-v1.0.2-20261030/deploy-20261030-012309/receipt.json)、[最终复核](../../output/pbz-release-v1.0.2-20261030/release-deployment-verification.json)。部署后未启动游戏，本次发布未新增 FPS 基准或游戏内功能验证。
