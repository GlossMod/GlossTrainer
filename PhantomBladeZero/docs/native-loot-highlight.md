# 原生战利品提示

2026-11-05，适用 `CL-302256_JKS-17280`。用户要求用按 V 时的游戏原生效果替换修改器绘制的黄色圆点和文字。

## 原生资源与实现

运行时只读回收的 `PlayerSenseMixinInterface` 和实际控件 brush 指向证明：游戏使用 `W_Sense_Quest`，收藏图标为 `T_Track_Collection`，绿色光晕材质为 `MI_UI_ChengXinDongZhao_001_BG_Green`。默认类模板是另一套蓝色图标，因此独立实例必须显式设置收藏图标和绿色光晕。[原生素材回读](../../output/pbz-native-highlight-20261105/native-art.json)、[调查及审查](../../output/pbz-native-highlight-20261105/native-vision-review.md)。

每个受支持的战利品持有独立 `W_Sense_Quest` 叶子控件及屏幕空间 `WidgetComponent`。组件在延迟注册期间切换为 Screen，避免先分配世界空间渲染目标；组件附着目标，位置由 Unreal 每帧投影，墙壁不遮挡屏幕空间提示。距离使用原生 `DistanceText.SetPBZFormatText`，仅在整数米变化时更新。原生图标、光晕和距离栏均沿用游戏资源，无修改器 ImGui 世界标记。

后续版本改为在绿色光晕中显示真实物品小图。原生 V 本身按追踪任务类型选图；逐物品区分是修改器新增的选择逻辑。原生拾取物使用 `ItemId`，场景物件使用已经存在的 `PickupInfo` 奖励数组完整反射字段，不调用生成奖励接口，也不把场景键当库存 ID。最多读取 16 项，只有奖励都指向同一 ID 且数量有效才显示该物品图；未知或多种奖励保留通用收藏标记。[图标契约](../../output/pbz-native-icons-20261105/resolver-contract-final.json)。

仅在创建提示时按道具、饰品、药品、主武器、煞兵的顺序读取最多五个原生数据行，要求继承的 `ItemId` 精确匹配。使用原生小图字段及 `SetBrushFromSoftTexture(texture,false)` 异步加载，保持原有尺寸；解析/绑定失败局部回退。每个提示保留所属行及软引用，缓存随至多 32 个提示的生命周期回收；稳定维护不重新查表或改 brush。同一 actor 原地替换奖励尚未增加单独监听，关闭重开、超距或对象/会话更换会重新解析。

宝箱是否完成使用已经通过当前构建蓝图字节码确认的 `CurSwitchState`，严格为 `false` 才能成为高亮或自动拾取候选。`bInteracted` 实为布尔数组，现场已开、未开箱都可为 `[false,true]`，不能用于完成判断。场景掉落仍遵循自身 `Not Interact === false` 谓词。[完成状态证据](../../output/pbz-native-icons-20261105/chest-state-confirmed-receipt.json)。

独立实例不创建 `W_Player_Sense` 宿主，不激活 Sense 技能，也不修改共享 GameplayTag、UI Extension 或任务追踪；该宿主的销毁逻辑会清除真实 V 技能状态，不能作为修改器的宿主。屏幕空间组件不提供 V 界面原本的屏外安全区箭头，背后目标不会贴边显示。

## 调度和生命周期

高亮默认 50 米，滑块 1–100 米；自动拾取默认 10 米，滑块 1–50 米。保留现有保存、恢复、目标分类、原生范围查询和有界候选扫描。最多持有 32 个最近候选，每 250ms 最多新建两个控件。稳定控件不重复加载素材、设置 brush 或播放入场动画。首次启用可能同步加载一次类和素材，具体冷启动耗时尚需实测。

关闭、超距、对象失效、真实拾取/开箱状态、换玩家/交互组件、异常或维护租约过期会清理。只销毁所有者、组件路径、绑定控件仍匹配的实例；隐藏优先，销毁失败保留回收账目重试。500ms Puerts 定时守卫在游戏线程检查维护租约，不从工作线程调用引擎。`DoInteract=true` 仅表示受理，不作为隐藏物品的完成证据。

脚本维护回执固定 `ok|[]`。C++ 不再解析坐标 JSON，覆盖层不再每帧加锁复制并绘制标记；历史调试字段 `lootMarkers` 保留为空以兼容已有工具。原生控件及游戏自身的 Slate/渲染开销不包含在修改器脚本桥计时中。

## 验证

修正版的 Debug、Release PBZ 本体均编译成功，Gameplay、Localization 和原生 Hook 生命周期测试均通过，生命周期每配置 920 项。六套源码 JS 回归共 486 项，历史完整功能测试副本另有 74 项，合计 560 项、0 失败；其中原生高亮套件 193 项，包含已销毁代理的严格回归。[Debug 结果](../../output/pbz-native-highlight-fix-20261105/validation/tests-debug.json)、[Release 结果](../../output/pbz-native-highlight-fix-20261105/validation/tests-release.json)、[JS 汇总](../../output/pbz-native-highlight-fix-20261105/js-validation-summary.json)、[原生高亮最终日志](../../output/pbz-native-highlight-fix-20261105/validation/script_native_highlight_tests.log)。12 段、419 字节的既有精确构建桥接契约仍与游戏文件一致；嵌入脚本为 58,572 字节，各 raw literal 均小于 12,000 字节。[脚本及桥接契约](../../output/pbz-native-highlight-fix-20261105/script-receipt.json)。

## 首版实机故障与修正部署

首版 `77CA0FDB...` 通过离线测试后部署，但在实际游戏 PID 37356 中，04:05:30、04:06:44 和 04:06:50 的维护调用均报 `passing a invalid object`，自动拾取与高亮状态随后变为未知；桥接校验和回调恢复仍成功。因此首版离线结果不能作为实机功能通过的依据。[修复前日志](../../output/pbz-native-highlight-fix-20261105/trainer-before-fix.log)、[故障分析](../../output/pbz-native-highlight-fix-20261105/fix-report.md)。只读快照发现两个独立 `W_Sense_Quest` 叶子控件，但未发现当时仍绑定它们的组件；这与故障后的清理相符，不能据此确定第一次失败的调用。[对象快照](../../output/pbz-native-highlight-fix-20261105/widgets-before-fix.json)。

同构建游戏源码明确指出，PuerTs 将已销毁的 JS UObject 代理传给 `KismetSystemLibrary.IsValid` 时，该调用自身会抛上述异常。修正版只对这条精确错误返回失效，其余异常继续上报；组件销毁后的验证因此能够正常完成。创建和维护失败时，独立清理不再覆盖原始错误，并保留调用阶段与有界堆栈。入场动画改用原生 UI 同款的 `PlayAnimationForward(animation,1,false)` 契约。严格代理回归在旧实现上失败、在修复后通过；尚不能仅凭这些修复确定原现场首个失败点。[故障与修复证据](../../output/pbz-native-highlight-fix-20261105/fix-report.md)。

游戏退出后，于 04:24:04 部署修正版 `GlossTrainer.asi`，SHA-256 为 `F1344BE76D6EA5806D0487814C3930DC84E467B790F561D7517C70E66F3760B3`，安装文件与产物一致。部署前游戏进程数为 0，原 ASI、最新配置及两处存档均已备份；`features.ini` 前后保持 `6DE7645FA93323910EAFFF69A575B7744A71DB292773EA689B18394F6126C72E`。[部署回执](../../output/pbz-native-highlight-fix-20261105/deploy-receipt.json)。

随后经 Steam 启动，用户回传画面已确认多个目标的原生绿色图标、光晕和距离实际可见。PID 41424 的只读快照包含 20 个 `W_Sense_Quest` 叶子和 154 个 `WidgetComponent`，读取失败数为 0；这些是本次快照的类型总数，并非全部由修改器创建。[修复后对象快照](../../output/pbz-native-highlight-fix-20261105/widgets-after-fix.json)。用户同时指出物品仍统一显示收藏图标，要求按目标原生图标区分，这项尚待修复。稳定维护、关闭清理、DPI、菜单/载入及密集场景性能仍未完成验收。

构建/安装及最终实机状态见[验证记录](verification.md)。离线测试不证明 DPI 大小、动画可见性、菜单/载入表现、流式关卡清理或密集场景 CPU/GPU 性能；没有可比的实机帧率结论。
