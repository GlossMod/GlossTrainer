# 本机验证记录

最近更新：2026-10-26。Windows 11 x64、Visual Studio 2022、RTX 5080，游戏 Steam build 21393610。

## 黑神话 v0.3.5 与多游戏发布隔离（2026-10-26）

用户确认 AE533E96 中红孩儿可以移动，仍没有移动动画，并接受当前效果、要求停止动画修复。本次保留该实现，将侧栏入口改为“七十二变（实验性）”，两行显示以适应原有宽度，英文同步更新。[亮暗导航原型](../output/imagegen/72-transformations-navigation-light-dark-20261026.png)使用 `custom-imagegen`、`model_providers.custom`、`gpt-image-2.5` 生成并查看；最终提示为精简导航、亮暗双主题、精确中文及选中项两行显示，不重做页面。

同仓库的发布通道改为 `<Game>/vX.Y.Z`，本次为 `BlackMythWukong/v0.3.5`。注册表绑定每个游戏的版本文件、构建目标、验证清单、安装回归、打包脚本与发布说明。构建只配置选定游戏；发行包、校验文件、审计清单、Actions 产物与输出目录包含游戏标识。GitHub 发布不争用仓库全局 Latest，客户端完整分页扫描后只比较本游戏正式版及配套 ZIP，跨游戏、错误版本与混配附件不触发升级。详细规则见 [发布流程](releasing.md)。旧 `v0.3.4` 保留，旧客户端需手动下载本游戏新版本完成一次通道迁移。

本轮构建和测试证据保存于 [原始命令结果](../output/game-release-20261026/build-tests.json)，脚本回归与发布回执也统一存放 `output/game-release-20261026/`。这里只记录可复核的自动验证与上述用户反馈，不将宿主通过或资源加载成功计为所有角色实测通过。

本地最终结果：Debug / Release 各 19 个验证程序全部通过，两配置 ASI 构建通过，共 80 条命令 exit 0。PowerShell 5.1 / 7 的便携包回归各 26 项通过（[PS5](../output/game-release-20261026/package-ps5-final.log)、[PS7](../output/game-release-20261026/package-ps7-final.log)）；隔离回归各 26 项通过（[PS5](../output/game-release-20261026/isolation-ps5-fixed-v2.log)、[PS7](../output/game-release-20261026/isolation-ps7-fixed-v2.log)），含两游戏同版本、共用输出目录不覆盖、跨游戏字段/目标冲突拒绝、仅导入选定模块及不改调用方 xmake 配置。安装器使用固定哈希的离线 Loader 归档，18 项夹具通过，[最终日志](../output/game-release-20261026/installer-tests-offline-final.log)。初次配置的 DSL 接口错误、夹具配置污染、联网安装回归中止及误选损坏缓存的失败记录均保留，最终验证分别修复配置/夹具或改用正确离线输入，不将失败算作通过。

已查看 v0.3.5 导航的真实宿主[暗色截图](../output/game-release-20261026/ui-dx12-dark-final/GlossTrainer.Host-restored.bmp)和[亮色截图](../output/game-release-20261026/ui-dx12-light-final/GlossTrainer.Host-restored.bmp)，两行文字无截断。DX12 亮色完整自动序列通过（3355 帧），[日志](../output/game-release-20261026/ui-dx12-light-final/GlossTrainer.Host.log)。先前 DX11 暗色及 DX12 暗色自动序列在首次按键时没有前台焦点，像素断言失败；恢复窗口后截图可见，不记为完整回归通过。此次未操控游戏或重新验收角色移动动画。

本地最终 Release ASI SHA-256：`8870FE619FBB540C53D7A0B00D9204D9917C6E6E0314B179A2C125E1524EF350`；[本地四文件包回执](../output/game-release-20261026/local-package-receipt.json)。正式附件以 GitHub 对已提交源码重新构建的结果为准。

已推送发布源码 `7088e1ef5d9fde3f8ad032637d422c5eec74f369` 并发布 [BlackMythWukong/v0.3.5](https://github.com/GlossMod/GlossTrainer/releases/tag/BlackMythWukong/v0.3.5)。[手动构建 40335401496](https://github.com/GlossMod/GlossTrainer/actions/runs/40335401496) 与[正式发布 40336895929](https://github.com/GlossMod/GlossTrainer/actions/runs/40336895929) 均 success：两配置验证通过，正式任务的发布作业通过，手动任务的发布作业按设计 skipped。[正式运行结果](../output/game-release-20261026/cloud-release-final.json)、[完整日志](../output/game-release-20261026/cloud-release.log)。原 `v0.3.4` 与全局 Latest 保持原状，新的游戏通道没有覆盖它。

正式包 ZIP SHA-256 为 `adff700a479373048aa316eb003c5db974338e377a299bed165f319c97239d5e`，ASI 为 `33cc72a4086e706a1926bf7637a871a32f8d47064f1f938102b0b6e6e5cfbc55`。完整下载正式 Actions 候选包，核对 game/tag/version/sourceRevision、ZIP 哈希及四个有效载荷的大小和 SHA-256；随后将该 ZIP 哈希与实际下载的 Release 校验文件、公开 Release API 的资产 digest 比对一致。[候选逐文件校验](../output/game-release-20261026/formal-candidate-verification.json)、[正式发布回执](../output/game-release-20261026/published-verification.json)、[公开发布元数据](../output/game-release-20261026/published-release-final.json)。本机直连 Release ZIP 多次连接重置，未完成该下载链路的独立整包回读；局部文件保留但不计为通过，以正式 CI 完整包及服务端资产摘要的一致性作为此次发布校验依据。本轮没有将正式包安装到正在使用的游戏目录。

## 动画资源缺失不再阻止移动（2026-10-26，AE533E96 已安装）

用户完成 `80A1112D` 测试后确认：一些角色已经能移动并播放动画，过场恢复正常；缺少移动动画的角色反而无法移动，并明确举例红孩儿。本轮把过场结论记为用户实测反馈，不扩大为所有过场或全部角色验收。[用户测试日志摘要](../output/transformation-live-resume-20261026/mm-user-test-summary.md)、[原始快照](../output/transformation-live-resume-20261026/trainer-cinematic-mm-user-test.log)。

红孩儿 `TAMER_hys_honghaier_01a` 的后续复现中，session `269695281` 连续 14 次选择结果均为 `available/current/required=true / valid=false / state=-1`，该会话没有发出可采样的移动请求。结合上一版 `animationReady=false` 时主动停止且不调用 MoveTo 的代码，确定了本功能的额外拦截；它不能证明游戏资产完全没有任何动画。[红孩儿复现快照](../output/transformation-live-resume-20261026/trainer-red-boy-before-fallback.log)、[逐会话数据](../output/transformation-live-resume-20261026/mm-user-test-summary.json)。

当前修复把动画检查限定为选择请求类型：已有合法 MM 状态时继续 WithMM；非 MM、缺少当前步态、缓存暂不可读或可选选择器不可用时，均回退普通原生 MoveTo，并保留键鼠/手柄输入、慢走/跑步、目标和移动请求索引。不会猜测 MM 状态、发送 None 或借用悟空动画；缺少动画时可能滑行。250ms 动画探测限频仍保留，重新获得有效动画后会自动恢复对应请求并清除本功能的旧动画提示。菜单、焦点、施法、状态限制、过场、死亡、切图和恢复检查照常生效；动画缺失不能豁免这些条件。日志新增 `moveAPI=native/with-mm`，区别普通回退与实际发送 MM 参数。

Debug / Release 的 Runtime 与 Localization 共 4 次测试及两配置 ASI 构建全部通过，[12 条最终命令](../output/transformation-live-resume-20261026/animation-fallback-results.json)。新增回归覆盖 available/current/required/valid 矩阵、MM→缺资源→未知→MM、JOG/RUN 与锁定朝向、缓存限频及请求连续性、暂挂和技能替代请求的索引取消、动画恢复后提示清除。Native 选择器和桥接代码本轮未改动，沿用上一节已通过的两配置测试，不把旧结果算作本轮重新执行。

检测到游戏退出后安装 Release `AE533E9656C36B61008614A8D35EB346439667B1002A6F980D33D44E17960611`，构建、快照、安装文件哈希一致。[安装日志](../output/transformation-live-resume-20261026/install-animation-fallback.log)、[安装清单](../output/transformation-live-resume-20261026/install-animation-fallback-manifest.json)、[文件哈希](../output/transformation-live-resume-20261026/installed-animation-fallback-hashes.json)、[源码哈希](../output/transformation-live-resume-20261026/animation-fallback-source-hashes.json)。未操作游戏、未修改用户技能名称、按键或视距配置；已请用户复测红孩儿移动、技能和恢复，以及原本有动画的角色。用户随后确认红孩儿已可以移动，仍无移动动画，并接受当前效果、要求停止动画修复。该反馈仅确认上述移动结果，不将任何角色自动升为“已验证”。

## 过场后恢复变身与 Motion Matching 请求（2026-10-26，已安装，待用户实测）

用户已完成 `77268924` 定位版测试，翠堤武师、四大天王（剑）仍站姿滑行。[本轮原始日志](../output/transformation-live-resume-20261026/trainer-mode-none-user-test.log)确认二者源配置及受管缓存都是 `ABPMoveMode=4 / SpareMoveMode=0 / CommonFinalMode=0`，对象和缓存均存在且身份有效。静态核对发现，现有普通 AI MoveTo 没有发送 MM 状态事件，AIController 代理也不会执行玩家专用状态派生。[完整调用链及 UFunction 参数](../output/transformation-live-resume-20261026/mm-state-driver-review.md)来自本机构建同 SHA/MVID 的程序集与离线原生函数，不宣称新增动态 REA 调试或游戏验证。

本版加入两项行为修复：

- 同一玩家、控制器和世界内的过场暂挂变身，不再主动销毁角色。停止输入、移动请求和待发技能，隐藏本功能的代理及已认领子对象，撤销自有四项玩家属性句柄、免伤层和相机改动；过场、序列状态和退场标记连续结束 250ms 后，把同一代理放到演出终点，重新读取相机基线、验证双方生命及特殊状态机，再重获玩家隔离并恢复角色。武器不重复初始化，技能配置不改写。过场中手动恢复、跨图、对象失效、死亡及恢复失败仍统一清理，且不把旧玩家传回旧代理坐标。
- MM 角色使用正常 `BGURequestAIMoveToLocationWithMM` 显式请求自身步态；独立只读选择器以游戏 `IsMMStateValid` 检查角色配置的状态键，锁定步态缺失时降为自身自由步态及速度朝向。非 MM 角色保留普通 MoveTo。未知缓存或缺少步态时保留变身并说明原因，250ms 限频复查，不借悟空动画、不强写 Final 模式、不重启自主 BT/FSM。状态键通过不保证资源值非空或最终动画成功；验收仍需实机画面。[选择器依据与完整指纹](../output/transformation-live-resume-20261026/mm-selector-implementation-review.md)。

玩家隔离刷新改为移动、碰撞、隐藏的顺序；发生任何重算时最后必算隐藏，覆盖同步移动回调显露悟空的窗口。剧情外部请求保留原栈优先级，不通过新分配句柄压过演出。失败诊断按租约及代际去重，区分退休、不确定、读取失败与外部栈顶仍获胜。[隔离审查](../output/transformation-live-resume-20261026/cinematic-isolation-review.md)、[过场生命周期及技能边界](../output/transformation-live-resume-20261026/cinematic-runtime-review.md)。本版暂挂不等同于完整冻结所有技能蒙太奇、notify 或持续生成器；子对象同值属性修改的所有权仍不能仅靠值比较判定。相机、输入和位置恢复的 mock 通过不替代真实过场验收。

Debug / Release 的 Native、Runtime、Adapter、Localization 四组测试及 ASI 构建全部 exit 0，共 20 条配置、构建和执行命令。[最终结果](../output/transformation-live-resume-20261026/cinematic-mm-final-results.json)。覆盖过场三种信号、同一角色恢复、连续退场观察、子对象原显隐、手动退出、切图取消、恢复部分失败、双方死亡、特殊状态机失效、MM 精确 Enum 参数、无资源与 unknown 区分、限频和拒绝重试。初次 Native Debug 因新增测试对象占用了旧归属链夹具索引而失败，修正为独立索引后复验通过，未放宽生产断言；[原失败](../output/transformation-live-resume-20261026/cinematic-mm-debug-GlossTrainerTransformationNativeTests-run.log)和[修复后复验](../output/transformation-live-resume-20261026/cinematic-mm-debug-native-recheck-run.log)均保留。

确认游戏进程退出后已安装本版 Release；[安装日志](../output/transformation-live-resume-20261026/install-cinematic-mm.log)、[安装清单](../output/transformation-live-resume-20261026/install-cinematic-mm-manifest.json)、[构建/快照/安装文件哈希](../output/transformation-live-resume-20261026/installed-cinematic-mm-hashes.json)、[源码哈希](../output/transformation-live-resume-20261026/cinematic-mm-source-hashes.json)可复核。安装当时尚无用户实测结果；后续用户已反馈过场恢复正常、部分动画可用，另有缺动画角色移动被拦截，详见本文开头的新回退修复记录。不据此提升全部角色为“已验证”，整体变身目标仍未完成。

## 移动回滚与武器初始化：实时反射核对（2026-10-26，修复验证中）

用户进入游戏后，代理通过 REA MCP 被动观测 PID 71880 中修改器的反射字段读取，复现四大天王（剑）移动后恢复悟空、翠堤武师武器初始化失败。两者具有同一根因：桥接层把 FName 的导出显示字符串当作大小写敏感键。实际移动接口字段是 `position`，调用使用 `Position`；武器接口实际字段是 `Index`，调用使用 `index`。参数名称检查拒绝执行，随后变身生命周期回滚。REA 证据分别为 `ev_25710f59bbcafee3471c0fc4ce76fff58d1096c7252b2b59b98fce405577dceb` 和 `ev_ae77817318648b08bf3a469048e2d4bda9cd28c971ac7feeb7c937ddbfeb20c7`。[完整移动字段快照](../output/transformation-live-resume-20261026/live-reflection.json)、[武器字段快照](../output/transformation-live-resume-20261026/live-weapons-reflection.json)、[修复前日志](../output/transformation-live-resume-20261026/trainer-before-name-fix.log)、[签名分析](../output/transformation-live-resume-20261026/movement-signature-audit.md)。

桥接查找已统一折叠 ASCII 大小写，覆盖参数验证、写入、回读及嵌套属性；保留重复名称、数量、类型、布局与方向检查，不改 FName 载荷或显示名。控制器诊断同时修正为原生 `K2_GetPawn`，旧 `controlsProxy=false` 不能作为控制器未绑定的证据。本节仍在验证中，不能据此宣布移动、武器或四大天王已经可玩；下方启动阻碍及旧安装哈希为此前记录。

EngineBridge、TransformationRuntime、TransformationNative、TransformationSpecial、TransformationAdapter、Extension 六目标在 Debug / Release 构建、运行均 exit 0，[结果](../output/transformation-live-resume-20261026/results.json)记录 27 条命令且没有失败或缺失日志。Release ASI 构建与安装成功，[安装回执](../output/transformation-live-resume-20261026/install-name-fix.log)与[两份文件哈希](../output/transformation-live-resume-20261026/installed-name-fix-hashes.json)已保留。准备重启游戏时 Computer Use 报告用户按下物理 Escape，按要求停止本轮桌面输入。新包尚未完成修复后实测；目标仍未完成，角色状态不提升为已验证。

后续在用户继续任务后成功启动新进程 PID 36760，插件磁盘 SHA-256 为 `1B53E92C27FE4694AEFF0CA8C61892B078D94C4E89185611F5D75A7E5FEB08EE`。用户明确改为自己操作游戏，代理只分析日志，随后未再发送游戏输入。新日志多次确认 `pawnRead=true / controlsProxy=true / dataCacheMatches=true`；此前移动名称错误不再出现，部分武器完成初始化。但用户反馈基本所有角色始终站姿滑行，因此移动动画验收明确未通过。日志还发现三武器角色在默认 inactive 的第 1 项后回滚，不能把所有武器标为通过。[用户测试日志快照](../output/transformation-live-resume-20261026/trainer-user-sliding.log)。后续只读诊断会分别记录速度、实际加速度、导航加速度开关及武器失败阶段；未经运行值证实不修改加速度属性，不强播动画。

只读诊断版在游戏退出后安装：SHA-256 `64E0CB6D4A1154FB0ED519C2E03E92558729AAB71F4574559904904046538420`，构建与安装文件一致，[回执](../output/transformation-live-resume-20261026/install-movement-diagnostics.log)、[哈希](../output/transformation-live-resume-20261026/installed-diagnostics-hashes.json)。移动请求后的后续帧至少 50ms 开始采样，之后每秒至多一次，每会话最多 24 组运动/动画样本及 8 条停止事件；记录独立 read-success/unknown、角色 stable ID、会话、真实 class、Velocity/Acceleration/MaxAcceleration、导航开关、动画实例与适用的 ABP 设置。读取失败不影响会话，不写动画或加速度属性。武器诊断保留所有 gate 和事件顺序，仅补 `stage/reason` 与显隐前后值。最新 [Runtime Debug/Release](../output/transformation-live-resume-20261026/diagnostics-animation-results.json) 和 [Native Debug/Release](../output/transformation-live-resume-20261026/diagnostics-native-results.json) 均通过，含可选探针缺失/错误类型/空对象/异常、采样延迟/上限/复位及零诊断写入。静态依据见[动画驱动](../output/transformation-live-resume-20261026/animation-driver-review.md)、[移动诊断](../output/transformation-live-resume-20261026/movement-animation-runtime-review.md)、[武器后置检查](../output/transformation-live-resume-20261026/weapon-postevent-review.md)。

用户已完成本包实测：[日志快照](../output/transformation-live-resume-20261026/trainer-diagnostics-user-test.log)。四大天王（剑）resource 1990 的多个移动样本 Velocity 约 250、Acceleration 约 2048、MaxAcceleration 2048，两个导航加速度开关均为 true；Mesh/AnimInstance/ABPSetting 存在，动画未暂停、速率 1、bDisableABPMove=false。用户确认仍始终站姿滑行，因此排除“实际加速度为零”，转查动画数据初始化与 Tick；不能继续盲改加速度开关。翠堤武师 resource 2017 连续三次均在 index1 默认 inactive 武器处 `post-hidden-mismatch`（beforeHidden=0、afterHidden=0），明确失败点；尚不能仅凭此证明事件无订阅或武器自身 LateBeginPlay 未完成。

后续版本 `CEB2AD31BEAF4EDFBF182B42629649D885F8AEC94EB2928938DD9E47219E2363` 已在再次确认无游戏进程后安装，[收据](../output/transformation-live-resume-20261026/install-graph-ready.log)、[构建/快照/安装哈希一致](../output/transformation-live-resume-20261026/installed-graph-ready-hashes.json)、[源码哈希](../output/transformation-live-resume-20261026/graph-ready-source-hashes.json)。此版本加入：

- 激活前检查全部已生成武器自己的 `BGUSimpleActorBaseCS.IsBeginPlayFinished`（RID 95B6，完整本构建指纹）和两个事件的实际订阅；Pending 不发送事件、不保存玩家恢复基线、不改相机/免伤/隔离租约，沿用会话总计 15 秒超时。已完成 ECS 但缺失订阅明确失败。真正派发前再次检查身份和就绪状态；显隐后置校验通过后才记 Complete，调用不确定或后置失败不重放。
- 在同一 24 次/每秒一次预算内只读采样 Top.MappedSpeed、Move linked graph 的 FinalABPMoveMode / IsMoveAccelerating、实际 owning actor，以及 Monster 的起步/停止/加速混合镜像或 Advanced 的 mapped speed / gait。精确 FName 标签和反射类型来自本机构建，字段缺失/错误类型/图缺失均独立记录，不裸读 ECS 私有字段、不写动画状态。

[Native Debug/Release](../output/transformation-live-resume-20261026/weapons-ready-native-results.json)、[Runtime/Localization Debug/Release](../output/transformation-live-resume-20261026/graph-ready-runtime-localization-results.json)、[ASI 两配置构建](../output/transformation-live-resume-20261026/graph-ready-asi-results.json)均 exit 0。新增回归覆盖多武器等待零部分事件、空/不可读订阅、对象复用/失效、后置失败不重放；生产后端等待转就绪、超时/切图/恢复/失败清理；动画图精确 Name/Object/Enum 参数、owner/class/null/type/NaN gate、采样上限与复位。首次 Runtime 回归发现测试 mock 错将旧会话累计 mutation 计数用于新会话零写入判断，已改为独立的本会话等待断言；原失败日志保留，随后两配置回归通过。

用户已完成 `CEB2AD31...` 实测，确认翠堤武师现在可正常变身，移动仍无动作。[新 trainer 快照](../output/transformation-live-resume-20261026/trainer-graph-ready-user-test.log)显示 13:47:46 两个武器先 `initialized=0 / pending`，下一帧均 `initialized=1 / masterSubscriber=1 / activeSubscriber=1`，默认 active 武器由 hidden=1 转为 0，默认 inactive 武器保留 hidden=1，后置校验均成功。这证实此前角色已 LateBeginPlay、武器尚未完成自身 ECS 的时序缺口，以及本次等待实际覆盖了该分支；不扩大为全部角色武器验收。

翠堤、四剑、杨戬的 Top.MappedSpeed 均随移动变化，Move 图存在、Owner 与 proxy 相符、IsMoveAccelerating 随移动更新，但所有采样的 FinalABPMoveMode 都是 0（None）。Enum 宽度/反射读取已重新静态核对，不能再把它解释为读失败默认值；下一步区分真实配置模式、受管 CommonData 引用/缓存和 Common.Update 路径。见[枚举审查](../output/transformation-live-resume-20261026/locomotion-enum-review.md)与[动画驱动第 9 节](../output/transformation-live-resume-20261026/animation-driver-review.md)。游戏自有日志已另存 [b1 快照](../output/transformation-live-resume-20261026/b1-graph-ready-user-test.log)，本次日志未记录能归因于 Common.Update 的异常；日志阴性不证明调用成功。

本版动画实测明确仍未通过，不提升角色为“已验证”。用户继续操作游戏，代理仅分析日志。补充 REA 成员分析 Evidence `ev_f462d889b01a1627b65eee9b692c74d2505b18f86bcec22e23ba67eefb5c7b1f` 仍受 64 MiB 元数据表示预算限制，返回 unavailable；[原始结果](../output/transformation-live-resume-20261026/mode-none-rea-members.json)。具体方法体与类型由本机同 SHA/MVID 的 ILSpy 补充，不宣称 REA 已解出这些方法。

为定位模式归零，新增只读缓存采样版 `772689243755BA86E77513E6DD2E939715D3556855679FC92FBE9299A7CDE5E6`，在确认游戏进程已退出后安装，[安装日志](../output/transformation-live-resume-20261026/install-mode-none.log)、[构建/快照/安装哈希](../output/transformation-live-resume-20261026/installed-mode-none-hashes.json)、[源码哈希](../output/transformation-live-resume-20261026/mode-none-source-hashes.json)。它保留已实测的武器等待修复；新增 Top 资产 CommonSetting.ABPMoveMode / MotionMatchingSetting.SpareMoveMode 的严格嵌套反射采样，以及独立能力的 `InspectAnimationState`：在一次连续 GC unsafe scope 内读取 Top 已有 BUABPSettingData、两个已有配置缓存与 Move.CommonData，通过四个已核对非泛型纯 getter 读取模式，再复核引用、weak identity、OwningActor 和会话代际。全部指纹由精确 Main MVID/RID call table 定位；证据见[原生探针报告](../output/transformation-live-resume-20261026/animation-aot-probe-review.md)。不存在的缓存报告 missing，不可读取报告 unknown，真实 None 保留 0；不会调用有懒初始化副作用的 getter，不创建缓存、不改模式、不重新执行生命周期。指纹失败仅禁用该诊断，不阻断变身。

本轮 [Debug/Release Native、Runtime 回归与 ASI 构建](../output/transformation-live-resume-20261026/mode-none-results.json)全部 exit 0。Native 新测试核对所有诊断指纹的独立失败、纯 getter ABI、缺失与错误枚举、SEH、读取失败、GC 进入/退出配对、引用变化、weak 地址复用、owner/会话变化、零写入及不调用懒 getter；Runtime 覆盖资产嵌套 Enum 类型错误、合法 0、class/interface 缺失、采样限频上限和 optional native observation 不可用时仍 Active。本版仍是定位动画模式的诊断更新，尚无真实游戏读数，不能称动画已修复。

## 追加修复验证（2026-10-26，已安装，实机待继续）

用户已确认旧修复版能够完成变身；连续移动、技能、恢复及新反馈仍需实机复核。鼠标、绑定持久化、快捷键、相机配置和 Gameplay 测试已在 Debug/Release 通过。最终 Native、Runtime、Special、Adapter、AOT、Localization 测试也在两种配置通过，新增覆盖四项原玩家状态句柄、过场直接显隐、同步 EndPlay、重复回调、特殊状态机启动顺序及 Engine 程序集边界。Debug Localization 首次因缺少租约错误文案翻译失败，补齐后 Debug 复测和 Release 通过，失败日志保留。安装器临时游戏回归 18 项通过。详见[本轮原始日志](../output/transformation-followup-20261026/)和[较早的中间测试](../output/transformation-live-fix-20261025/followup-tests.md)。这些测试使用合成对象，不等同于真实游戏行为。

主程序、宿主及变身 UI fixture 最终 Release 构建通过。最终 ASI SHA-256 `F444064D8C19A737A92680457247C44B84E8CED4E4AF92B99D88AFD75EF15385` 已安装并回读一致：[构建哈希](../output/transformation-followup-20261026/final-asi-sha256.txt)、[安装回执](../output/transformation-followup-20261026/install-final.log)、[安装哈希](../output/transformation-followup-20261026/installed-final-sha256.txt)。启动 b1.exe 和 Shipping.exe 均未获得可操作游戏窗口；Steam 窗口恢复也失败，已请用户手动进场。这些尝试没有运行到角色变身，尚无本轮新代码的游戏验收，四大天王琴/伞/龙/剑均保留未验证；静态依据见[专用状态图分析](../output/transformation-special-20261026/analysis.md)。旧 PID 70188 已退出，下文 PID 和安装状态均属历史记录。

UI 原型已按项目要求使用 `custom-imagegen` 的 `model_providers.custom` / `gpt-image-2.5` 生成并查看，见[亮暗原型和提示词](../output/transformation-live-fix-20261025/camera-prototype.md)。旧宿主的 DX11 亮色重复出现 `hideVerified=0`：日志记录隐藏，GPU 截图却仍显示菜单。确认旧宿主在 `Present` 后读取 `FLIP_DISCARD` 缓冲区，内容不保证保留；仅测试夹具改为 Draw 完成、Present 之前复制到独立快照，生产渲染代码不变。修正后 DX11／DX12 亮暗四组均 exit 0，检查显示、隐藏、缩放、最小化恢复及按键释放；旧失败日志全部保留。[DX11 亮色日志](../output/transformation-followup-20261026/ui-prepresent-dx11-light/GlossTrainer.Host.log) / [DX11 暗色](../output/transformation-followup-20261026/ui-prepresent-dx11-dark/GlossTrainer.Host.log) / [DX12 亮色](../output/transformation-followup-20261026/ui-prepresent-dx12-light/GlossTrainer.Host.log) / [DX12 暗色](../output/transformation-followup-20261026/ui-prepresent-dx12-dark/GlossTrainer.Host.log)。真实 fixture 页面截图已查看：[暗色](../output/transformation-followup-20261026/ui-prepresent-dx11-dark/GlossTrainer.Host-restored.bmp)、[亮色](../output/transformation-followup-20261026/ui-prepresent-dx11-light/GlossTrainer.Host-restored.bmp)。fixture、原型和宿主通过均不替代角色实测。

续轮复核仍没有游戏进程；资源管理器窗口也在重新枚举、重新绑定后激活失败，错误为 `failed to activate captured window`，因此尚不能把启动阻碍归因于游戏代码。未向游戏发送输入，等待可操作桌面及游戏场景。[续轮窗口审计](../output/transformation-followup-20261026/continuation-window-audit-2.json)。

## 代理实机修复进度（2026-10-25，已安装，验证进行中）

用户已授权代理接管游戏测试。实机复现翠堤武师生成后因读取纯托管 `RPCEventCollection` 失败回滚，修复为 D9C1 getter 后又捕获真实 `OnClearTargetInfo` 内 Mono 协作 GC 状态断言。新增操作级 balanced GC unsafe 作用域，保留完整清目标调用和所有原有隔离检查。原始证据、失败观测范围和停机经过见[实机记录](../output/transformation-live-fix-20261025/live-findings.md)，入口 ABI 与指纹见[GC 修复依据](../output/transformation-live-fix-20261025/gc-transition.md)。

Release ASI `3B947AF31562758589D994C9E1D501BEB63074BAE4492EA396E1C42DFBC8D98F` 已由安装脚本安装，磁盘哈希已核对；重启进程 PID 70188。受影响 Native/Runtime 两目标在 Debug、Release 下均通过，含清目标失败后清理、恢复和同角色重试，见[测试结果](../output/transformation-live-fix-20261025/test-results.md)。本节为进行中记录：尚未由代理完成修复版移动、技能与恢复的连续实测，未新增“已验证”角色。后文“未安装/用户自行测试”属于各次历史交付边界，不是当前授权或安装状态。

## REA 运行时观测与变身生成/协同修复（2026-10-25，新包未安装）

用户分别尝试翠堤武师和四大天王（伞）后，通过 REA MCP / Frida 被动捕获修改器的反射读取：阶段路径为空，协同标签实际为 `UGuid.BattleSC.Huan` / `UGuid.BattleSC.f4`，并非读取失败。反射元数据快照确认 deferred spawn 参数区为 136 字节，其中 FTransform 为 96 字节、碰撞参数是 1 字节 FEnumProperty。结合源码和旧游戏日志定位 `Arg::Byte` 错传，已改为 `Arg::Enum(..., 1)`；捕获未包含成功执行的 deferred spawn 或其实际参数值。[完整分析与 REA 证据](../output/transformation-runtime-observation-20261025/analysis.md)。

新增临时角色协同隔离：AFE6–AFEC 七个实例 void 入口拦截注册、注销及消息；保留 AFE5 本地绑定，仅按当前会话的根/子角色弱身份生效。全部 18 处原生指纹通过且 hook 安装完整后才发布就绪。允许可读的非空协同标签与事件列表，保留其它阶段/场景依赖检查；不改 CDO、原始关卡角色或全局协同中心。协同 FSM 消息也被停用，相关招式完整性仍待实测。[静态审计与初始化时序边界](../output/transformation-runtime-observation-20261025/cooperative-audit.md)。

- 最终 Debug / Release 全构建及 16 个测试目标分别通过，共 32 次 exit 0：[汇总](../output/transformation-runtime-observation-20261025/results.json)、[Debug](../output/transformation-runtime-observation-20261025/build-debug.txt)、[Release](../output/transformation-runtime-observation-20261025/build-release.txt)。IME 环境回读 SKIP 保留在日志中。
- [桥接回归](../output/transformation-runtime-observation-20261025/GlossTrainerEngineBridgeTests-release.txt)验证 Enum/Byte/Int 互不混用及错误参数诊断。[原生回归](../output/transformation-runtime-observation-20261025/GlossTrainerTransformationNativeTests-release.txt)覆盖各处指纹失败、安装失败回滚与七入口的身份边界；[生产后端回归](../output/transformation-runtime-observation-20261025/GlossTrainerTransformationRuntimeTests-release.txt)覆盖三类协同标签、非空事件列表、适配未就绪、CDO 不变、阶段/字段失败及恢复。
- 精确 EXE SHA-256、程序集 MVID 和 18 个入口的离线字节匹配，其中新增协同入口 7 个：[离线回执](../output/transformation-runtime-observation-20261025/native-cooperative-fingerprints.json)。这是磁盘证据，不能证明新 hook 已在游戏内安装。
- 最终 ASI 均为 AMD64 / PE32+ DLL 并导出 `InitializeASI`：[大小与 SHA-256](../output/transformation-runtime-observation-20261025/artifacts.json)。Release SHA-256 `757B7E12CC14C7AC364E10F4455165616E39573181BE0FCEE7A24D3D3285965A`。

最终测试包 `output/GlossTrainer-transformation-runtime-fix-20261025-r2.zip` 保留旧包，逐项回读 manifest 大小与哈希：[打包回执](../output/transformation-runtime-observation-20261025/package-receipt.json)。r2 只完善文档措辞和交付证据，ASI 与本轮初包相同。本轮没有安装、重启或控制游戏。观测会话已调用关闭，响应原文保留；关闭后同 PID 仍运行。磁盘安装文件仍为旧 ASI `1BE0DE1D3F58FF9DC0F4A47F2C8C4934BA17F4868C82DBA679DDE74D74E48443`。

没有修改 UI 布局或重跑图形宿主，没有新版本的角色游戏行为验收，不新增“已验证”角色。游戏由用户自行复测，先检查翠堤武师、四大天王（伞）与普通小怪的生成、移动、技能和恢复，再记录协同伙伴、死亡、召唤物与切图影响。

## 空引用完整导出与唤醒字段路径修复（2026-10-25，未安装）

用户更新前轮包后仍遇到 `BeginTriggerBox 无法校验`。确认 `ExportText_Direct` 会对与空默认值相同的属性返回 false 并省略输出；此前将其误当作读取失败。三个只读导出入口统一改用真实 void ABI 的无条件 `ExportTextItem`，覆盖软对象、软类、直接容器和嵌套容器。独立字段审计又发现 `WakeUpLinkEnable` / `CanTriggerWakeUpLinkWhenDead` 属于 `WakeUpConfig`，已从错误的顶层查询改为结构体路径读取。依据见[分析及 REA MCP 记录](../output/transformation-empty-export-validation-20261025/analysis.md)、[字段审计和安装文件只读核对](../output/transformation-empty-export-validation-20261025/field-audit.md)。

- 最终代码在 Debug / Release 均构建主程序及全部 16 个测试目标，32 次执行 exit 0：[汇总](../output/transformation-empty-export-validation-20261025/results.json)、[Debug 构建](../output/transformation-empty-export-validation-20261025/build-debug.txt)、[Release 构建](../output/transformation-empty-export-validation-20261025/build-release.txt)。IME 每配置的候选窗/组合窗环境回读两项 SKIP 保留。
- 桥接回归改用严格 void ABI，增加差异导出省略空值的合成复现及空软对象、空软类、空容器、嵌套唤醒列表的完整导出；读取失败、异常、未知格式及内存释放仍验证：[Debug](../output/transformation-empty-export-validation-20261025/GlossTrainerEngineBridgeTests-debug.txt)、[Release](../output/transformation-empty-export-validation-20261025/GlossTrainerEngineBridgeTests-release.txt)。模拟复现与原生字节证据分别记录，未执行游戏原生函数。
- 生产后端夹具只在真实嵌套位置提供两个唤醒 bool，移除顶层兜底；覆盖 false 正常开始/恢复、两个开关分别为 true 时拒绝、字段缺失报错，以及实例初始化后产生依赖时清理/恢复：[Debug](../output/transformation-empty-export-validation-20261025/GlossTrainerTransformationRuntimeTests-debug.txt)、[Release](../output/transformation-empty-export-validation-20261025/GlossTrainerTransformationRuntimeTests-release.txt)。
- x64 / PE32+ DLL、`InitializeASI` 导出及哈希已核对：[构建回执](../output/transformation-empty-export-validation-20261025/artifacts.json)。`git diff --check` 通过。没有更改 UI 布局、重新执行图形宿主或新增已验证角色。

新测试包：`output/GlossTrainer-transformation-empty-export-20261025.zip`，保留前轮包。Release ASI SHA-256 `1BE0DE1D3F58FF9DC0F4A47F2C8C4934BA17F4868C82DBA679DDE74D74E48443`，打包后逐一回读 manifest 有效载荷大小和哈希，见[打包回执](../output/transformation-empty-export-validation-20261025/package-receipt.json)。版本显示仍为 0.3.4，以哈希区分构建。

未安装 ASI、未操作游戏。日志仅确认了用户遇到 BeginTriggerBox 错误，没有捕获该字段当时的真实内容或原生返回值；空容器具体序列化字符串仍待游戏验证，不能将合成测试视为角色实测通过。

## 角色依赖字段引用类型修复（2026-10-25，未安装）

修正 `FloatRelativeSpotActor` 被当作硬对象指针读取的问题，并同时修正 `BeginTriggerBox` / `EndTriggerBox` 的软对象读取。新增只读 `ObjectReferenceAssigned`：空引用可以继续初始化；非空/陈旧弱引用、未加载软路径仍为依赖，类型/布局/读取失败不放行。保留全部验证状态的开始入口、技能名称和按键持久化，以及生成前/后的隔离复检。详细字段表、REA 成功/预算限制和本地补证见[分析](../output/transformation-reference-types-validation-20261025/analysis.md)。

- Debug / Release x64 主程序和 16 个测试目标构建成功，32 次测试执行 exit 0：[汇总](../output/transformation-reference-types-validation-20261025/results.json)、[Debug 构建](../output/transformation-reference-types-validation-20261025/build-debug.txt)、[Release 构建](../output/transformation-reference-types-validation-20261025/build-release.txt)。IME 各配置的候选窗和组合窗环境回读仍为 SKIP。
- 引擎桥接测试覆盖硬/弱/软反射类型、8 字节弱引用布局、原生/零初始化空值、非空/陈旧 token、类型/尺寸/数组维数/边界异常、读取/导出失败、未知文本与临时字符串释放：[Debug](../output/transformation-reference-types-validation-20261025/GlossTrainerEngineBridgeTests-debug.txt)、[Release](../output/transformation-reference-types-validation-20261025/GlossTrainerEngineBridgeTests-release.txt)。
- 生产后端合成测试覆盖空引用正常开始与恢复、CDO 依赖/字段错误拒绝、真实实例初始化后才产生弱引用依赖时清理临时 Tamer / proxy 并恢复悟空：[Debug](../output/transformation-reference-types-validation-20261025/GlossTrainerTransformationRuntimeTests-debug.txt)、[Release](../output/transformation-reference-types-validation-20261025/GlossTrainerTransformationRuntimeTests-release.txt)。首轮夹具把硬引用移入独立映射后漏掉属性写入接口，造成激活失败和后续断言终止；[失败日志](../output/transformation-reference-types-validation-20261025/initial-runtime-debug-failure.txt)保留，补齐硬引用读写同步后两种配置重跑全部通过。
- 两个 ASI 的 AMD64 / PE32+ DLL 格式、`InitializeASI` 导出和 SHA-256 均已核对：[构建回执](../output/transformation-reference-types-validation-20261025/artifacts.json)。`git diff --check` 通过。本轮无 UI 布局修改，没有重新执行图形宿主。

测试包：`output/GlossTrainer-transformation-reference-types-20261025.zip`，保留此前测试包。Release ASI SHA-256：`1F11A9F8597944C8AEFF0DD9717B69F89920883C64673B57C2DC09E51336909E`。打包后回读 manifest 并逐一核对 ZIP 有效载荷，见[回执](../output/transformation-reference-types-validation-20261025/package-receipt.json)。界面版本仍为 0.3.4，以构建哈希区分。

未安装或操作游戏；没有新增已验证角色。上述结果不能确认截图中二王子或其它角色的实际移动、技能和剧情隔离行为，游戏实测由用户自行进行。

## 目录角色不受验证状态限制（2026-10-25，未安装）

移除变身页“开始变身”对 `Support` 状态的限制。未验证、不可用等条目也可直接提交 Begin，无需先检查技能；保留接口就绪、空闲会话与待处理请求的门槛。每次尝试仍检查真实资源、驱动、隔离和初始化；失败恢复悟空，状态标签不表示稳定可玩。

- Debug / Release 主程序及全部 16 个测试目标构建成功；32 次执行全部 exit 0，见[汇总](../output/transformation-open-status-validation-20261025/results.json)、[Debug 构建](../output/transformation-open-status-validation-20261025/build-debug.txt)、[Release 构建](../output/transformation-open-status-validation-20261025/build-release.txt)。IME 两项环境回读 SKIP 仍保留。
- 新增生产后端合成回归：未经 Inspect 的未验证条目可开始尝试；无效资源进入恢复并标为不可用，资源恢复后仍可不经 Inspect 直接 Begin、完成激活和恢复。见[Debug](../output/transformation-open-status-validation-20261025/GlossTrainerTransformationRuntimeTests-debug.txt)、[Release](../output/transformation-open-status-validation-20261025/GlossTrainerTransformationRuntimeTests-release.txt)。
- 按项目要求先生成并查看 `custom-imagegen` / `model_providers.custom` / `gpt-image-2.5` [亮暗原型及提示词](../output/transformation-open-status-validation-20261025/prototype.md)，保留现有布局。
- 查看恢复窗口后的实际 UI fixture 截图：[DX11 暗色](../output/transformation-open-status-validation-20261025/ui-dx11-dark/GlossTrainer.Host-restored.png)、[DX12 亮色](../output/transformation-open-status-validation-20261025/ui-dx12-light/GlossTrainer.Host-restored.png)。未验证角色的开始按钮可用。两次完整宿主自动序列均因首次菜单键时 `foreground-target=0`、无法完成菜单开关像素断言而 exit 1，不能记为宿主回归通过；最小化/恢复后前台为 1，页面可以显示。模拟鼠标未取得 Begin 回调证据，入口可提交性由生产后端回归覆盖。原始 GPU BMP 已转换为 PNG。

本轮没有安装或操作游戏，没有新增已验证角色。ASI 的 x64 格式、InitializeASI 导出与 SHA-256 见[构建回执](../output/transformation-open-status-validation-20261025/artifacts.json)。

本轮测试包：`output/GlossTrainer-transformation-open-status-20261025.zip`。Release ASI SHA-256 为 `22A9436A420F240F602CA4E34B45C92405E16F8BA5193D762A2E2C22253EE7EC`；ZIP 中 64 个有效载荷均已核对大小和 SHA-256，视觉夹具未进入 ASI，见[打包回执](../output/transformation-open-status-validation-20261025/package-receipt.json)。

## 通用代理能力与清理回归（2026-10-25，未安装）

本轮以移动组件、初始化后真实技能和隔离依赖检查替代历史三角色名单；没有把目录条目直接升级为已验证。实现及未完成的专用形态见[当前范围](transformation.md)和[全形态审计](transformation-all-forms-audit.md)。没有更改 UI 布局，没有进行游戏内输入、安装或逐角色游戏验收。

| 验证 | 结果与证据 |
| --- | --- |
| x64 Debug / Release 主程序与全部 16 个测试目标构建 | 均通过；[Debug 构建](../output/transformation-adapter-validation-20261025/debug-build.log) / [Release 构建](../output/transformation-adapter-validation-20261025/release-build.log) |
| 全部 16 组自动测试，两种配置 | 32 次执行 exit 0；[汇总](../output/transformation-adapter-validation-20261025/results.json)、[Debug 输出](../output/transformation-adapter-validation-20261025/debug-tests.log)、[Release 输出](../output/transformation-adapter-validation-20261025/release-tests.log) |
| 真实后端、引擎边界合成测试 | 非旧名单路径、地面/三维移动策略、输入与施法时序、CDO/实例技能差异、特殊状态、场景/控制依赖拒绝、弱身份复用、子对象死亡与延迟销毁、销毁中新子对象重查均通过。夹具不代表该路径真实可玩 |
| 更新检查失败策略 | 两种配置的 Update 测试通过：先确认新版，再收到失败结果时解除限制；UI 消息为“检查更新失败”。未伪造真实 GitHub 发布或实施网络断连 |
| IME 环境边界 | 两种配置执行成功；各有候选窗和组合窗 readback 两项环境 SKIP，不能据此宣称输入法所有行为通过 |
| 测试目录隔离 | 全部测试从独立临时工作目录运行；Localization 测试目标新增 MSVC `/FC`，使源码覆盖检查可由绝对 `__FILE__` 定位源码，修复其既有 cwd 依赖 |

没有执行本轮图形宿主或游戏实测。普通 CMC 分流和数据规则的合成通过，不能证明真实动画、根运动、派生死亡实现、异步掉落/任务隔离和全部形态正常。阶段图、专用 ECS mover 和无 Owner 关联的复合形态仍有未完成分支。

## 首批变身实验构建（历史记录，2026-10-25，未安装）

新增独立变身模块、游戏动作转接、角色目录和技能槽页面。当前只开放杨戬、大圣、狼侍卫三个实验性适配；没有任何角色被标记为已验证。参考表的 247 行合并为 246 条路径线索，运行时尝试合并 Asset Registry 资源；同资产的运行阶段/覆盖配置尚未完整枚举，目录始终提示覆盖不足。实现分析、REA 成员、程序集指纹和用户手测步骤见 [变身文档](transformation.md)，来源逐项清单见 [CSV](transformation-reference.csv)。

UI 实现前使用 `custom-imagegen` / `gpt-image-2.5` 生成并查看了[亮暗原型](transformation-validation-20261025/prototype.png)。页面沿用现有主题，包含搜索、分类、支持状态、技能序列编辑和恢复悟空入口。本轮未运行新页面的 DX11/DX12 宿主及亮暗交互回归，原型图不是实际界面截图。

| 验证 | 结果与证据 |
| --- | --- |
| x64 Debug / Release 全构建 | 均通过，无编译警告；[Debug](transformation-validation-20261025/build-debug.txt) / [Release](transformation-validation-20261025/build-release.txt) |
| 11 组自动测试，两种配置 | 22 次执行全部 exit 0：[汇总](transformation-validation-20261025/results.json)。包含 Gameplay、Extension、AdditionalHook、AOT、ExtensionLifecycle、EngineBridge、Transformation、TransformationNative、TransformationRuntime、IME、Update |
| 反射桥接与原生适配 mock | 类型/方向/布局、动态内存释放、结构体深拷、调用完成状态、作用域销毁隔离、输入释放和地址复用均通过；[桥接 Debug](transformation-validation-20261025/GlossTrainerEngineBridgeTests-debug.txt) / [Release](transformation-validation-20261025/GlossTrainerEngineBridgeTests-release.txt)，[Native Debug](transformation-validation-20261025/GlossTrainerTransformationNativeTests-debug.txt) / [Release](transformation-validation-20261025/GlossTrainerTransformationNativeTests-release.txt) |
| 变身模型及实际后端 mock | 目录去重、实际技能 ID、预设缺项、连招末项、重复请求、加载失败/超时、部分激活回滚、恢复重试、免伤层计数与结果不明时停止重放、对象复用、菜单/失焦排队和切图取消通过；[模型 Debug](transformation-validation-20261025/GlossTrainerTransformationTests-debug.txt) / [Release](transformation-validation-20261025/GlossTrainerTransformationTests-release.txt)，[后端 Debug](transformation-validation-20261025/GlossTrainerTransformationRuntimeTests-debug.txt) / [Release](transformation-validation-20261025/GlossTrainerTransformationRuntimeTests-release.txt) |
| 现有功能冲突恢复 | 隐藏玩家、无重力、世界冻结、后台暂停暂挂/恢复及已排队冲突取消通过；[生命周期 Debug](transformation-validation-20261025/GlossTrainerExtensionLifecycleTests-debug.txt) / [Release](transformation-validation-20261025/GlossTrainerExtensionLifecycleTests-release.txt) |
| 目标 EXE 静态指纹 | 34/34 匹配；另核对 PendingKill 期间序列号校验路径，见[离线报告](transformation-validation-20261025/native-fingerprints.json)。这不证明钩子已在游戏运行或隔离完整 |
| ASI 文件结构 | 两份均为 AMD64 PE32+ DLL，导出 InitializeASI；文件大小与 SHA-256 见[构建回执](transformation-validation-20261025/artifacts.json) |

IME 候选窗、组合窗位置回读在两种配置下仍各有一项环境 SKIP；其余断言通过，不将这些跳过项视为真实输入法验收。原生适配及后端测试使用 mock，不等同于游戏角色测试。

Release SHA-256：`0FC0A55D0C953CC9C0B2B22C3963065B050D6B16A7A1CF8F087A4EE6D406F1F0`。Debug SHA-256：`32E1CC5274637BAE37E3DEB1EDC87B1F89C8543DC4841F61CCDA6714BBCBA43B`。

**本次实验 ASI 未安装，游戏实测由用户自行进行。** 本轮未重跑安装器回归。首批三角色的生成、移动、技能、受伤、死亡、恢复、改键/手柄、剧情隔离和异步清理均待实测；其他角色暂只开放目录/技能检查。这是首批通用链路实验构建，尚未完成完整角色目录及全部角色稳定可玩的验收。

本地交付包为 `output/GlossTrainer-transformation-experimental-20261025.zip`，内含 Release ASI、安装脚本、许可、实现分析和测试说明；已逐一校验 ZIP 中 40 个有效载荷的大小与 SHA-256，见[打包回执](transformation-validation-20261025/package-receipt.json)。这是未提交工作区的实验包，源码显示版本仍为 0.3.4，不作为同名正式 Release 发布。

## 逐技能名称与按键绑定扩展（2026-10-25，未安装）

变身页按当前角色枚举结果逐技能列出实际技能 ID；每项支持可选 UTF-8 名称和单键/修饰键组合键，绑定按角色稳定 ID 与实际技能 ID 保存。配置检查拒绝未知新技能 ID、同角色重复组合键、无效按键和全局菜单/功能快捷键冲突；同角色已存但当前缺失的技能记录可原样保留。旧 8 槽继续服务于游戏原生动作映射，不再限制逐技能 UI。

新增自动用例覆盖超过 8 个技能、按角色独立的技能快照与配置、真实 ID 校验、中文名与空名序列化、损坏配置解析不污染旧数据、精确修饰键匹配、菜单/功能双向冲突、按键录制、菜单/失焦取消、重复请求、缺失技能记录保留/清键和配置失败回滚。异步检查使用 `SkillsFor(id)` 按角色读取快照，切换角色保留各自草稿；失效按键可清除，不永久占用组合键。

| 验证 | 结果与证据 |
| --- | --- |
| x64 Debug / Release 全构建 | 均通过，无编译警告；[Debug](transformation-skill-bindings-validation-20261025/build-debug.txt) / [Release](transformation-skill-bindings-validation-20261025/build-release.txt) |
| 13 组自动测试，两种配置 | 26 次执行全部 exit 0，含新增 Binding / Hotkey 及实际后端 Runtime 测试；[汇总](transformation-skill-bindings-validation-20261025/results.json) |
| 技能配置与录制 | 真实 ID、UTF-8、精确组合键、同角色冲突、跨角色隔离及损坏配置通过；[Binding](transformation-skill-bindings-validation-20261025/GlossTrainerTransformationBindingTests-release.txt) / [Hotkey](transformation-skill-bindings-validation-20261025/GlossTrainerTransformationHotkeyTests-release.txt) |
| 生产后端 mock | 角色技能快照归属、>8 技能、失效记录清键、队列门控与保存失败回滚通过；[Runtime](transformation-skill-bindings-validation-20261025/GlossTrainerTransformationRuntimeTests-release.txt) |
| DX11 亮暗 UI fixture | 均 PASS，3377 / 3380 帧；菜单开关、主交换链、按住键释放、尺寸与最小化恢复；[暗色日志](transformation-skill-bindings-validation-20261025/ui-dx11-dark-final/GlossTrainer.Host.log) / [亮色日志](transformation-skill-bindings-validation-20261025/ui-dx11-light/GlossTrainer.Host.log) |
| DX12 亮暗 UI fixture | 均 PASS，3363 / 3364 帧，先创建无关队列/辅助链；[暗色日志](transformation-skill-bindings-validation-20261025/ui-dx12-dark/GlossTrainer.Host.log) / [亮色日志](transformation-skill-bindings-validation-20261025/ui-dx12-light/GlossTrainer.Host.log) |
| 实际布局检查 | 查看了四组宿主 GPU 截图，完整列表使用 96 项合成技能，显示名称、组合键、滚动条、保存与恢复；[暗色](transformation-skill-bindings-validation-20261025/ui-dx12-dark/GlossTrainer.Host-open.png) / [亮色](transformation-skill-bindings-validation-20261025/ui-dx12-light/GlossTrainer.Host-open.png) |
| UI 原型 | 实现前使用 custom-imagegen / model_providers.custom / gpt-image-2.5 生成并查看；[原型与完整提示词](transformation-skill-bindings-validation-20261025/prototype.md) |
| ASI 文件检查 | Debug / Release 均为 AMD64 PE32+ DLL，导出 InitializeASI；大小、SHA-256 见[回执](transformation-skill-bindings-validation-20261025/artifacts.json) |

IME 的候选窗/组合窗位置回读在两种配置下仍各有一项环境 SKIP，不作为真实输入法验收。首次运行 Runtime 测试时，测试读取配置的流未关闭，导致 Windows 文件句柄阻止重命名和替换；关闭测试流后重新执行全部测试通过。初次 DX11 暗色宿主因前台未满足而失败，记录保留在 `ui-dx11-dark`；随后仅在本地宿主夹具启动时先最小化/恢复自己的窗口，四组最终运行均通过。GPU 原始 BMP 已转为同名 PNG 保存。

fixture 只替换游戏数据并选择变身页，沿用实际页面、输入路由和渲染器；不进入交付 ASI。录制/组合键由自动单元测试覆盖，宿主序列只验证窗口消息和图形路径，不证明真实 Raw/Legacy 双通道、游戏内按键、手柄或角色效果。**游戏实测由用户自行进行，本轮未安装或操控游戏；三个角色仍为实验性，已验证角色数仍为 0。**

新测试包：`output/GlossTrainer-transformation-skill-bindings-20261025.zip`，保留此前八槽实验包。Release ASI SHA-256：`F80EE1653BB9A8E7D49EF2F44D6A343BD6A509E66FBC73411ED8D7096DD353D8`。已校验 ZIP 中的 manifest 有效载荷大小与 SHA-256，见[打包回执](transformation-skill-bindings-validation-20261025/package-receipt.json)。显示版本仍为 0.3.4，本包不作为正式 Release。

## VS Code C/C++ 头文件路径修复（2026-10-25）

截图中的 `C/C++(1696)` 来自编辑器未配置读取 xmake 编译数据库。已核对 MinHook 1.3.4 的 `MinHook.h` 和数据库内的包含目录均存在，补充 `.vscode/c_cpp_properties.json`，配置 Windows x64 / C++20 并引用 `${workspaceFolder}/.vscode/compile_commands.json`；Git 只保留通用配置，继续忽略含本机绝对路径的数据库。

`xmake project -k compile_commands .vscode` 成功，数据库包含 55 条记录。VS Code 1.143.1 / C/C++ 1.35.4 重新打开 `midhook.cpp` 后，`#include <MinHook.h>` 无红线，状态栏显示“没有问题”和“IntelliSense: 就绪”，当前配置为 `Windows x64`。数据库重新生成期间输出过一次解析警告，生成完成后编辑器已恢复正常。此轮仅改编辑器配置和文档，未改 C++ 源码，未执行运行时回归。

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

## 中英文界面：2026-10-25

新增简体中文 / English，默认跟随游戏文本语言；设置可改为固定语言并立即保存。1856 个精确翻译 key、10 个动态模板覆盖界面操作、功能说明、状态及目录短名；428 条唯一物品长描述仍保留原文。自定义技能名和书签名直接显示。运行时语言只从游戏线程获取，未检测到时设置页明确展示配置或系统临时回退来源。[实现与手测步骤](localization.md)。

| 验证 | 结果与证据 |
| --- | --- |
| Debug / Release 全构建 | 均通过；[Debug](../output/localization-validation-20261025/build-debug.txt) / [Release](../output/localization-validation-20261025/build-release.txt) |
| 15 组测试 × 两种配置 | 30 次执行 exit 0，[汇总](../output/localization-validation-20261025/results.json)。含此前全部 13 组与 Localization / GameLanguage 两组 |
| 语言保存及词典 | 默认跟随、文化优先级、手动覆盖、立即切换、保存后重读、失败回滚、坏配置、重复 key、printf/模板占位符、UI/feature 静态 key 覆盖；[Release](../output/localization-validation-20261025/GlossTrainerLocalizationTests-release.txt) |
| 游戏线程语言探测 mock | Language 优先、Culture 后备、非 ASCII 拒绝、5 秒限频、失败重试及非游戏线程门控；[Release](../output/localization-validation-20261025/GlossTrainerGameLanguageTests-release.txt) |
| DX11 / DX12 设置页 | 中文/英文 × 亮/暗共 8 场景通过；菜单显示/隐藏、按住键释放、主链选择、尺寸变化及最小化恢复。[汇总](../output/localization-validation-20261025/ui-results.json) |
| 英文功能及变身页 | 初次宿主运行通过；查看截图发现分类挤压和试招按钮截断，修正后两页另做最终宿主复测。[结果](../output/localization-validation-20261025/ui-layout-final-results.json)，[功能](../output/localization-validation-20261025/dx11-dark-en-features-final/GlossTrainer.Host-open.png) / [变身](../output/localization-validation-20261025/dx12-light-en-transform-final/GlossTrainer.Host-open.png) |
| 布局调整后复验 | Debug / Release ASI 重新构建，语言测试各再通过一次；[记录](../output/localization-validation-20261025/post-layout-validation.json)。其他测试的代码和依赖未变 |
| 静态接口证据 | REA 受程序集 64 MiB 表示预算限制；[REA 原始记录](../output/localization-validation-20261025/rea-language-members.json)与[补充元数据/IL](../output/localization-validation-20261025/game-language-static-evidence.json)明确身份、路径及 FString 输出，不能替代游戏调用验证 |
| 原型 | UI 实现前使用 custom-imagegen / model_providers.custom / gpt-image-2.5 生成并查看；[图与完整提示词](../output/localization-validation-20261025/prototype.md) |
| 最终 ASI | Debug / Release 均为 AMD64 PE32+ DLL，导出 InitializeASI；[大小与 SHA-256](../output/localization-validation-20261025/artifacts.json) |

首轮测试曾发现一项测试负向期望写反、词典重复和 printf 检查差异，修正后完成上述全量及最终复测。IME 候选窗和组合窗位置回读在两种配置中仍各有一个环境 SKIP，其余断言通过，不将 SKIP 视为输入法验收。

设置页实际截图：[中文暗色](../output/localization-validation-20261025/dx11-dark-zh-CN-settings/GlossTrainer.Host-open.png)、[中文亮色](../output/localization-validation-20261025/dx11-light-zh-CN-settings/GlossTrainer.Host-open.png)、[英文暗色](../output/localization-validation-20261025/dx12-dark-en-settings/GlossTrainer.Host-open.png)、[英文亮色](../output/localization-validation-20261025/dx12-light-en-settings/GlossTrainer.Host-open.png)。截图来自 GPU backbuffer 读回，BMP 转换为同名 PNG 保存。长名称列表项按列裁剪并可查看提示。视觉夹具使用合成角色/技能并模拟语言来源，不编入生产 ASI；宿主自己的启动最小化/恢复只用于取得前台，不操作游戏。

本轮未安装到游戏，未运行游戏内测试；游戏实测由用户完成。测试不能证明真实游戏语言反射或角色可玩性。新包为 `output/GlossTrainer-localization-zh-en-20261025.zip`，保留此前两份变身测试包。界面版本仍为 0.3.4，以构建哈希区分；无提交、发布或安装。

## 中英文后续覆盖复验：2026-10-25

核对已完成的语言功能时，发现近期变身适配新增状态未进入原词典。[修复前审计](localization-followup-audit-20261025.md)记录具体遗漏；现已新增 41 个精确 key 和 6 个锚定模板，当前共 1897 个 key、16 个模板。恢复悟空的退出原因只翻译应用自有原因一层；快捷键冲突消息中的玩家名称始终保留原文。技能名称与按键配置在切换语言前后保持相同序列化内容。

- Debug / Release 均重新构建 ASI、LocalizationTests、GameLanguageTests；四次测试全部 exit 0。[执行记录](../output/localization-followup-validation-20261025/results.json)、[构建产物](../output/localization-followup-validation-20261025/artifacts.json)。
- 扩展源覆盖至全部 gameplay `.cpp/.hpp` 和 localization.cpp；新增实际移动适配结果、技能配置失败、格式化后的运行时错误、恢复原因及中文玩家名称回归。
- 游戏语言 mock 新增无效 ASCII 标签、超长值、空回退值及离开游戏线程后恢复探测；原有偏好保存、重读、失败回滚等检查仍通过。
- 本次仅修改翻译与测试，沿用上一节已查看的 DX11/DX12、亮暗、中英文布局截图；未再次运行宿主，也未把历史宿主记录记作本次执行。

后续构建包为 `output/GlossTrainer-localization-zh-en-20261025-r2.zip`，包含当前工作区构建和对应源码，保留旧包。默认与手动语言行为见[语言说明](localization.md)。仍未安装或操作游戏，真实游戏语言与输入法效果由用户实测。

## GitHub 复制安装包：2026-10-26

发行打包器改为精确四文件清单：根目录 `version.dll`、`version.ini`、`README.md`，以及唯一的 `scripts/GlossTrainer.asi`。包内不含安装脚本、审计清单或用户配置；退出游戏后复制到游戏 Win64 目录即可安装。独立的玩家版 README 模板包含“黑神话：悟空内置修改器”、安装方法、功能、署名 `By: 小莫` 和 licenses；根 GPL v3 及四份第三方许可全文内嵌，外部审计清单只供本地和 CI 候选包使用。

- Loader 取自固定官方 v10.0.0 x64 ZIP，固定 SHA-256 验证成功。归档实际仅含 x64 `dinput8.dll`，原字节重命名为 `version.dll`；DLL SHA-256 为 `900fa114af8317bfe22015ca77becc1afa9853831213ff17a5e03e0f45d58937`。[来源、PE 与许可审计](../output/release-package-20261026/loader-audit.json)。
- PowerShell 7.6.5 与 Windows PowerShell 5.1 各通过 22 项最终回归，覆盖精确文件结构、真实 Loader、INI、完整许可、导出和架构拒绝路径、损坏归档与缓存、重复打包字节一致、解压后自动加载路径及配置保留。[汇总与精确脚本哈希](../output/release-package-20261026/results.json)。测试启动时复制 ASI 快照，防止其他任务重建影响结果。
- Windows PowerShell 5.1 默认参数回归也通过 22 项；同时修复参数默认表达式中路径上下文缺失的问题。[默认参数回归](../output/release-package-20261026/defaults-smoke/tests-ps5-f071f1cf16f14e1abb1034b4d697088f/results.json)。
- 首次联网下载 Loader、打包、逐项读取 ZIP 和五份完整许可检查均通过。[下载与打包日志](../output/release-package-20261026/download-smoke/package.log)、[独立结构检查](../output/release-package-20261026/download-smoke/download-check.json)。此包使用本机现有 ASI 验证打包，不作为正式游戏版本发布；清单 `sourceRevision` 为 null。

GitHub 工作流使用根 `output/` 保存候选包与验证夹具，先打包下载固定 Loader，再在 PowerShell 5.1 / 7 中离线运行回归；正式 Release 仅上传 ZIP 与 `SHA256SUMS.txt`。本轮只更新发行流程和说明，不改动游戏功能、已发布标签或现有正式 Release，也未执行游戏内验证。

GitHub 干净 checkout 的 [build-only 运行 40271944811](https://github.com/GlossMod/GlossTrainer/actions/runs/40271944811) 已成功，源提交为 `fb60aef81feadf5778b9790ee98b796531c60ec3`。Debug / Release 构建及全部验证程序、安装器、PowerShell 5.1 / 7 打包回归均通过。下载 CI 候选包后重新检查了精确四文件结构、逐项 SHA-256 / 大小、Loader 固定哈希和源码提交；ZIP SHA-256 为 `9a67d976f462b7c1e327e82f1a05f92b6195ae8996021a95402c0eb92f621633`。[CI 与候选包核验](../output/release-package-20261026/github-validation.json)、[最终作业状态](../output/release-package-20261026/github-jobs-final.json)。该运行未创建或覆盖正式 Release。
