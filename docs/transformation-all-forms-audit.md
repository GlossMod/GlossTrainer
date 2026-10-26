# 角色目录与能力适配静态审计

日期：2026-10-25。本文是实施决策与静态证据记录，不是游戏测试报告。调查没有运行游戏、生成角色或修改共享配置。本文所述候选方案不表示对应代码已经完成。

## 覆盖边界

`transformation-reference.csv` 保留参考 Mod 的 247 行，按完整资产路径不区分大小写去重为 246 个路径；087/088 指向同一雪僵尸资源。注释中的角色名称、阶段、飞行、巨型或友军描述只能提供调查线索，不能充当能力分类依据。

当前 `transformation.cpp::RefreshCatalog` 只枚举 `/Game/00Main/Design/Units` 下名为 Tamer 的蓝图。它既不能证明整个游戏资产目录完整，也没有列出同一资产的所有运行时阶段与 OverrideID 组合。完整路径、来源、ResID、OverrideID 和已证实的形态标识应分别保留；不得只按显示名称去重，不得从注释自动生成阶段或猜测 OverrideID。

移除 `InitialAdapter` 的三个路径判断只能消除入口限制，不能完成全角色适配。必须通过统一能力探测，得到具体的候选、拒绝或信息不足结果。全目录都能执行检查，与全目录都能变身，是不同的验收项。

## 已核对的静态证据

以下临时文件没有提交到仓库；构建身份与提取来源见 [transformation.md](transformation.md)。行号针对本次调查时的本地反编译。

| 证据 | 位置 | 对适配的影响 |
| --- | --- | --- |
| Tamer 配置的 ResID/OverrideID 和组件标记决定角色初始化 | `tmp/game-mapping/xxj/b1.BGUCharacterCS.transformation.cs:355–400` | 不能只复制模型或资源 ID；保留 `UnitCDesc`、`UnitPhysicsDesc`、`FlagConfig` 的深拷贝。 |
| 组件标记包含模板类型及 MonsterAlterTags/MonsterRejectTags | `b1.FUnitCompFlagConfig.transformation.cs:8–37`；`b1.BGUCharacterCS.transformation.cs:382–400` | 应读取真实配置；不得根据资源名决定双足、飞行或剧情形态。 |
| 飞行、样条飞行、物理变形、阶段切换等组件按配置注册 | `b1.BGU_CharacterAI.transformation.cs:236–276` | 所有 AI Character 具有同一父类，不等于使用同一种移动或阶段控制。 |
| 地面、游泳、飞行的速度和默认移动模式是反射属性 | `UnrealEngine.Engine.UCharacterMovementComponent.decompiled.cs:962,1074,1102,1186,1242,1270,2814,2842` | 可按实际 CMC 及有限数值分类，使用现有反射桥接，不猜内存偏移。 |
| 游戏另有 FlyControl、Cloud、SplineFly、Parkour、Glide、CarMontage、PhysicsSimulation、NpcGuideSpline、AISpider mover | `b1.BUS_MovementSystem.transformation.cs:1616–1745,3219–3300` | `MovementMode` 只是 CMC 模式；单看该字段不能证明游戏 ECS 移动控制兼容。 |
| 特殊 mover 的部分状态可以通过已有查询接口读取 | `b1.EBGUSimpleState.decompiled.cs:69,113,181,303`；`b1.BGUFunctionLibraryCS.decompiled.cs:4635` | `UseCarMontageMoveMode=27`、`FlyControlMode=54`、`PhysicsSimulationMoveMode=87`、`CloudMove=143` 可由现有 `BGUHasUnitSimpleState` 查询。 |
| 根运动跳跃有四足动画专用路径 | `b1.BUIAJump.decompiled.cs:37–46` | 对所有形态直接调用通用 Jump 或更改 Z 坐标，不能称为完成跳跃适配。现有输入桥接也没有转接 Jump。 |
| 技能目录接口可以接收真实代理实例 | `b1.AutoQA.AutoTestHelperLib.transformation.cs:2277–2279`；`b1.BGUFuncLibAICS.transformation.cs:771–816` | 初始化完成后可重新枚举实际代理技能，验证绑定，不必只相信 Tamer CDO 的列表。它仍只是技能描述存在性检查。 |
| 扩展配置会将状态图、阶段图与协同信息转入 ECS 数据 | `b1.BUS_ExtendConfigComp.transformation.cs:8835–8928` | 清空指针不等于保留阶段；只检查指针也可能遗漏 cooked 资源中的路径字符串。 |
| 当前原生隔离只认一个代理弱对象身份 | `BlackMythWukong/src/gameplay/transformation_native.cpp` 的 `Owned/OwnedComponent/ScopedCleanup` | 附属角色、召唤物或阶段新 Actor 不自动继承隔离；不能仅凭根 Actor 生成成功开放复合形态。 |

## 推荐接口与流程

建议新增不接触引擎的 `transformation_adapter.hpp`，使用三值探测结果而非布尔默认值：

```cpp
enum class Knowledge { Unknown, Absent, Present };
enum class MovementDriver { Ground, Swimming, Flying, Unsupported };
struct AdapterEvidence; // CDO/实例来源、字段读取结果、模式、速度、图/世界依赖
struct AdapterDecision; // 驱动、可否尝试、能力限制、具体拒绝代码和字段
AdapterDecision Evaluate(const AdapterEvidence& evidence);
```

`transformation.cpp` 负责在游戏线程构建证据，策略只解释证据。建议两阶段检查：

1. `Prepare`：验证 Tamer、资源 ID、目标类、弱身份、CDO 移动组件、源扩展依赖和技能目录。成功读取才有证据；字段不存在、导出失败和未知语法都记录为 `Unknown`。不修改原始 CDO。
2. `Initialize`：延迟生成隔离的代理，等待真实 BeginPlay 完成，再读取实例的移动模式、速度、生命、特殊状态和技能目录。实例结果覆盖静态候选；冲突时拒绝或恢复，不继续沿用旧候选。
3. `Update`：监测对象代次、模式变化、特殊 mover、玩家保护状态和恢复前提。模式进入未实现分支时停止派发并恢复；不能强行改回 Walking 以隐藏不兼容。
4. `Restore`：复用完整基线、弱身份、免伤层所有权和重试策略。不得以单次 Destroy 调用成功代替弱对象失效，不得为绕过失败清空恢复记录。

能力和验证状态分开。通过这些检查最多成为“通用独立代理实验模式”；`Verified` 只能来自目标构建上的实际行为证据。无技能的角色可单独评估“移动/外观代理”能力，不应伪造攻击技能，也不能把它描述成完整战斗形态。

## 移动分支

| 实际证据 | 推荐行为 | 限制或拒绝理由 |
| --- | --- | --- |
| CMC Walking/NavWalking/Falling，相关速度有限且有效 | 继续现有视角 yaw 水平 `AddMovementInput`，保留目标物理参数 | Falling 不是独立可选择形态；不能覆盖角色自身重力与动画。 |
| CMC Swimming，MaxSwimSpeed 有效，未进入专用 mover | 独立三维移动分支，使用有限的视角 pitch/yaw 和物理输入 | 不能把本来地面角色强制改成 Swimming；没有水体适配证据不能宣称任意场景游泳。 |
| CMC Flying，MaxFlySpeed 有效，未进入 FlyControl 等专用 mover | 独立三维移动分支，保留原模式与重力配置 | CMC Flying 不等于 ECS FlyControl。使用相机前向实现升降应明确记录，不能宣称沿用完整原生飞行按键。 |
| Custom mode、无 CMC、初始化后 None、非法模式或无效关键数值 | 拒绝并返回确切字段 | 当前没有该驱动的已证实桥接。不能靠某个正速度值反推角色必然可移动。 |
| `UseCarMontageMoveMode/FlyControlMode/PhysicsSimulationMoveMode/CloudMove` 为真 | 在相应 ECS 驱动桥接完成前拒绝/恢复 | 已有静态代码证明它们走专用 mover；调用通用 CMC 输入不足以替代。 |
| `CantMove=3`、`IgnoreAllInput=6`、`BanInputButCamera=12` 为真 | 暂停移动派发 | 定身/受控状态不是“不支持角色”；不要通过强制位置更新绕过游戏状态。 |
| 施法或根运动接管 | 暂停外部强制转向，避免把移动注入与根运动叠加 | 现有 CastingSkillID 可保守阻止施法期间注入；它不等于完整 RootMotion 检测。未核实的 RootMotion API 不能写成已接通。 |

大型体型由实际 Bounds 决定镜头距离、出生位置/碰撞检查策略；四足与双足不能由名称推断。只有尺寸比并不能证明出生空间足够或相机始终正确。现有相机倍率上限、对齐原悟空位置的生成方式，需要独立测试巨型角色。

## 图与关联依赖：需要检查的确切字段

字段均属于 `BUS_ExtendConfigComp`，除另有说明。对象字段和它对应的 Path 必须一起检查。`SyncCalliopeAssetPath` 在反编译 8770–8830 行将编辑期对象引用转成运行时路径。

| 字段 | 源码证据 | 处置建议 |
| --- | --- | --- |
| `PhaseStateGraphTemplate`、`PhaseStateGraphTemplatePath` | 声明 1438/1466；转换 8909–8912 | 任一非空：阶段驱动未适配，拒绝完整形态。不能清空后声称保持了原阶段。 |
| `OtherFsmTemplates`、`OtherFsmTemplatesPath` | 1352/1372；转换 8883–8897 | 任一非空：额外状态机依赖未审计；不能因主图为空就放行。 |
| `FsmSubMachineOverride`、`FsmSubMachinePathOverride` | 1395/1415；转换 8898–8908 | 任一非空：子状态机覆盖未审计。 |
| `BattleSC`、`SpecificEventListen_BattleSC`、`BattleSC_GUID` | 1521/1553/1577；转换 8915 起 | 关联协同战斗/事件依赖未适配；任一有效依赖应拒绝。 |
| `StateGraphTemplate`、`StateGraphTemplatePath` | 1293/1321；转换 8876–8882 | 普通状态图非空不自动证明危险，也不证明可丢弃。若保持现有隔离策略不复制图，需明确只提供手动独立代理，不保证原始 AI/阶段行为。 |
| `FloatRelativeSpotActor`、`SplineList`、`BeginTriggerBox`、`EndTriggerBox` | 3814/3879/3902/3937；转换 9093 起 | 存在实际世界引用：依赖场景对象的移动，不是独立 CMC 驱动。拒绝，不能复制原关卡引用。 |
| `BossBarObserveTamerList` | 8571 | 与其他 Tamer 的关联证据；当前单代理模型不能承担该关联语义。 |
| `WakeUpConfig.WakeUpLinkEnable`、`CanTriggerWakeUpLinkWhenDead`、`TargetTamerList` | 转换 8980–8990 | 关联唤醒/死亡触发和目标 Tamer 会转为 Guid；需独立隔离，不复制到临时代理。 |
| `bEnableFlyCtrl`、`bEnableTurretControl` | 7232/7649；转换 9393–9425 | 需要飞行控制/炮台专用分支，不能只复制布尔值开启。 |
| `bEnableMovePhysicsTransform`、`bEnablePhysicalMove`、`bSimulatePhysicsWhenAttacking` | 3032/3362/3634；移动转换 9010 起 | 标记物理移动/攻击物理依赖，需对应驱动及回收检查。 |

`GroupAIBPConfig`、`HatchConfigInfo`、`ComboGraphTemplates/ComboGraphTemplatesPath`、`SkillMappingGraph/SkillMappingGraphPath` 等还影响协作、孵化、连招和技能映射。没有审计其结构字段或运行时语义时，不能把空探测结果包装成“安全单体证明”。尤其技能可能在执行时生成新 Actor，单纯静态 CDO 字段无法完整证明所有技能的副作用。

## ExportPropertyText 与空值

本次没有采集目标运行时的 ExportText 样本，不能声称已核实本构建的空数组、空 Map、软引用或结构体默认序列化语法。常见的 `None`、`()`、`""` 仅是待测试候选，不应使用通用字符串包含匹配将它们全部当空。

本轮实现对有明确字段类型的数组/Map 仅接受完整空导出或完整 `()`，同时要求与基础 CDO 相等；未知格式和非空内容均拒绝。这是实验准入规则，仍未采集本构建的真实空容器样本。阶段路径使用强类型 FString，战斗标签使用嵌套 NameValue，唤醒配置只检查嵌套目标列表，避免从整段结构体文本推断关联为空。

- 对对象、bool、int、Name，优先现有强类型 `Property/PropertyPath`。导出成功和导出文本为空是两个独立结果。
- `BattleSC_GUID` 的实际类型是 **FGameplayTag**，不是 FGuid。转换代码读取 `BattleSC_GUID.TagName.PlainName`。应使用反射的 `TagName` 字段及 Name 的空值语义，不使用 `00000000-...` 规则。
- 集合可先将源字段与同构建、同类型的 `BUTamerActor` 基类 CDO 对应字段成功导出的文本比较，作为“等于已读取基线”的证据；基线本身还需确认为无依赖。不能把“等于基类默认值”概括成任意属性都安全。
- 解析器必须匹配完整、类型明确的语法。读取失败、截断、未知结构和内容非空都不能判为 `Absent`。保留字段名与原始结果供诊断，但不要从未识别的字符串内猜测地址或 Guid。
- Source/CDO 信息与新生成实例不同，应分别记录；原对象不可写。

## 可复用的默认 ECS 初始化范围

已证实的最小链路仍是现有实现：创建未完成 Spawn 的新鲜基础 Tamer → 深拷贝 `UnitCDesc/UnitPhysicsDesc/FlagConfig` → 写入 MonsterClass/Owner/TamerType → 注册代理隔离身份 → FinishSpawning Character → 等待初始化完成 → 校验生命、阵营、目标、移动 → 捕获悟空恢复基线 → 激活。

没有证据支持整体复制源 `ExtendConfigComp` 后只清空几个图指针。新鲜基础配置的默认值也不全是零：`BUS_ExtendConfigComp.Initialize:8681–8760` 设置 `EnablePatrolOnStart=true`、`bCanAsPerformer=true`、`DefalutInteractiveGroupID=-1`、`ActorResetType=5` 等。因此需要继续显式禁用剧情/交互/跟随标记，保留不注册 Tamer 的隔离链路；不能把“默认配置”称作绝无行为或副作用。

没有额外证据证明设置某个非零 TamerType、关闭行为树、成功读到生命就能完成所有角色的阶段与输入初始化。新 API 和状态值必须来自对应构建的反射/映射证据。

## 实施与验收顺序

1. 主代理负责 `transformation.cpp` 与纯策略 `transformation_adapter.hpp`：两阶段探测、实际驱动分流、拒绝原因、动态恢复；保留逐技能命名/快捷键和现有恢复所有权。
2. 引擎/native 实现代理负责反射导出边界：类型校验、成功状态、生命周期和失败返回；不在该模块混入具体角色规则。
3. 测试代理覆盖策略与真实后端 mock：非名单普通角色、三种 CMC 分流、CDO/实例冲突、特殊状态、非空 Path 但空对象、空值与未知导出区别、无技能、模式中途变化、恢复失败和对象地址复用。已有三个角色必须走同一规则，不能保留豁免。
4. 记录每个已检查路径的真实证据和拒绝字段，而非批量将 246 路径标“已支持”。目录扫描完成与候选通过分开统计。
5. 用户实际游戏验证必须覆盖生成、移动、技能、受伤/死亡、恢复、剧情/掉落、切图，以及巨型/飞行/多阶段/关联形态。自动测试和反编译都不能替代这项证据。

截至本次静态审计，没有新增游戏内验证。专用 ECS mover、阶段图和多 Actor 隔离仍存在未完成分支；不能将这份方案或全目录能力检查称为“全角色完整适配已经完成”。
