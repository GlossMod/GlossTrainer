# PBZ 修改项可行性分析

日期：2026-10-29。本报告保留早期静态调查的证据分级；后续已经开展游戏内动态调用与实现，最新状态见 [验证记录](verification.md) 和 [原生适配说明](native-adapter.md)。以下“未验证”描述对应早期调查阶段。

## 分析对象与证据边界

- 主程序：`E:\steam\steamapps\common\PBZ\platform\steam\PBZ.exe`，Windows x64，511,072,344 字节。
- 版本：`CL-301524_JKS-17219`；SHA-256：`7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4`。
- 同 build 的历史崩溃上下文标识 UE `5.7.4-301524+42ecbfa37f78`、Shipping。它支持引擎版本判断，不代表本次游戏运行状态。
- REA MCP 已打开目标并完成 PE 资源检查，证据 ID：`ev_3942aa6be2d47a304154d5c17094b60f01d16cbb5ad4fc3151852a095c69b9e7`。该证据只支持目标身份和 PE 资源结构。
- REA 原生反编译后端未配置可用，Rizin 也不可用；未取得 REA 伪代码。以下玩法结论由本地只读字符串、指针表扫描和现有 LLVM 反汇编补充，不应称为 REA 已完整反编译。
- CUE4Parse 未挂载加密的核心 PAK/IoStore，未取得玩法资产、蓝图或物品数据表。当前文件索引为 0 不表示游戏没有这些系统。
- 运行侧只枚举了进程与模块；未注入、调用 GM、读写玩法内存或修改游戏文件、存档。REA 活动目标已关闭。

原始证据：[REA 导出](../../output/pbz-rea-20261029/rea-evidence.json)、[后端状态](../../output/pbz-rea-20261029/rea-readiness.json)、[文件版本](../../output/pbz-rea-20261029/file-version.json)、[模块清单](../../output/pbz-rea-20261029/live-modules.json)、[资源侧调查](../../output/pbz-rea-20261029/assets/findings.md)。

## 功能清单

证据等级：**A** 为已追到相应入口的实际处理代码；**B** 为名称与可执行指针表、帮助或相关日志支持的候选；**C** 为字段或通用接口线索，所属对象与完整语义待确认。A 也不表示运行时可用。

| 修改项                              | 等级与现有依据                                                                                                         | 实现边界和待验证点                                                                                              |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| 设置生命值、恢复生命                | **A/B**：`GM_SetHealth` 的浮点参数进入处理函数，并引用生命设置结果和 ASC 缺失日志；另有 `GM_GetHealth`、`GM_FullHeal`  | 设置生命优先；读取/满血入口还需逐项核对。确认上下限、死亡状态、玩家对象重建行为                                 |
| 免死、无敌、God mode                | **A**：`GM_State` 代码识别 `immortal`、`invulnerable`、`godmode`；帮助提供 `on/off`；引用实际状态标签                  | 三个预设的伤害、硬直和死亡差异未实测，关闭及换图恢复需要单独验证                                                |
| 药品补给／高使用次数                | **A**：`GM_InfiniteMedicine` 处理代码引用 `MedicineInfos` 并写入数量类字段                                             | 目前可能是巨大数量或补充操作，不能直接宣称持续不消耗；不要默认作为可逆开关                                      |
| 终结技资源补充候选                  | **A（语义未完结）**：`GM_InfiniteUltimateShachi` 有 native 入口、处理函数和相关数值调用                                | 确认实际影响的 ShaChi 资源及持续时间；不能扩展为所有技能无限或无冷却                                            |
| 材料补满                            | **B**：`GM_FillAllMaterilas` 注册候选，保留原始拼写                                                                    | 确认材料范围、数量上限、前置条件及保存行为                                                                      |
| 指定药品／饰品发放                  | **B**：`GM_GiveMedicine`、`GM_GiveAccessory`，另有背包系统和库存变更线索                                               | 先解析参数与物品 ID，再做单项发放；`GetAllItems/GetAllWeapons` 名称不足以证明发放全部物品，更不证明全部内容解锁 |
| 主武器、ShaChi 武器、药品和饰品升级 | **B**：`UpgradeMainWeapon/Max`、`UpgradeShaChiWeapon/Max`、`UpgradeMedicine/Max`、`UpgradeAccessory`，以及批量升级入口 | 先支持单项，确认等级上限、材料扣除、拥有条件和存档持久化，再扩展批量                                            |
| 激活天赋                            | **B**：`GM_ActivateTalent`、`GM_ActivateAllTalent`                                                                     | 验证前置节点、点数扣除、重复激活及重载后结果；不能预先承诺可撤销                                                |
| 敌人图鉴解锁                        | **B**：`GM_DiscoverAllEnemies`，相关日志明确提到 `PBZCodexSubsystem` 和新增发现数量                                    | 指向图鉴发现，不是地图显怪；覆盖范围与存档行为待验证                                                            |
| 坐标查看、位置记录、移动到坐标      | **B**：`GM_GetPlayerLocation`、`GM_SavePosition`、`GM_MoveTo`；后者有 `X,Y,Z` 格式及任务被替换日志                     | 先做只读坐标；移动可能为异步任务，不能据此确定瞬移或任意传送                                                    |
| 附近敌人处理／清除 NPC              | **B**：`GM_AutoKillNearby`、`GM_KillAllNPC`                                                                            | 未追到处理函数。目标可能包含任务 NPC，须确认筛选范围；不等同于玩家一击必杀                                      |
| 攻击、防御、姿态伤害倍率            | **C**：PBZ DamageCalculator、`PoiseDamage`、倍率字段与 Posture 遥测线索                                                | 需要确定玩家／敌人归属及计算链；不能将 Poise 与 Posture 的关系视为已证实                                        |
| 移速、游戏速度                      | **C**：`Get/SetMoveSpeed`、`CharacterMovementSpeed` 和通用 `SetGlobalTimeDilation`                                     | 所属对象及实际游戏路径未确认；全局时间缩放可能同时影响 AI、动画和任务                                           |
| 冷却与特定武器能量                  | **C**：`ShachiRegenCooldown`、Wind/Thunder 消耗与恢复、`MaxEnergy` 等                                                  | 字段涉及不同武器和技能，尚不能归纳成全技能无冷却或通用无限能量                                                  |
| 金币、Anger 数值                    | **C**：同组存在 `GetCoin/AddCoin/ConsumeCoin`、`GetAnger/GetMaxAnger` 等                                               | 尚未证明该组属于主流程玩家货币；Anger 的攻防消耗／恢复线索不能证明其等于体力                                    |
| 跨地图移动、周目设置                | **B/C**：`GM_Travel`、`GM_ReloadMapAtPosition`、`GM_SetCycleCount`                                                     | 参数及行为未确认；另有跨地图加载守卫和阻止传送标签。应在关卡、任务与保存机制明确后处理                          |

表中 B 级主要证据见 [名称与指针表候选](../../output/pbz-rea-20261029/registration-candidates.json) 和 [玩法字符串](../../output/pbz-rea-20261029/binary/focused_gameplay.json)；C 级补充见 [区域扫描评估](../../output/pbz-rea-20261029/binary/gameplay_region_assessment.json)。邻近类名只是关联候选，不能替代正式的类归属验证。

## 已追踪的代码位置

以下 RVA 仅适用于上述 SHA-256；静态 VA 基址为 `0x140000000`，运行时不能直接把静态 VA 当作有效指针。

| 名称                        | native 入口 RVA | 处理函数 RVA | 关键证据                                                                                           |
| --------------------------- | --------------- | ------------ | -------------------------------------------------------------------------------------------------- |
| `GM_SetHealth`              | `0x6520c50`     | `0x6629d00`  | 读取 float 参数并调用处理代码；引用 `Health set to: %.1f`、`AbilitySystemComponent not found`      |
| `GM_State`                  | `0x6520db0`     | `0x662a1a0`  | 预设分支；`CharacterState.Immortal`、`CharacterState.Stand.Invulnerable.Debug` 和 godmode 结果文本 |
| `GM_InfiniteMedicine`       | `0x6520430`     | `0x66274b0`  | 跳转到处理代码，引用 `MedicineInfos`，写入数量类字段                                               |
| `GM_InfiniteUltimateShachi` | `0x6520450`     | `0x66277b0`  | 跳转到处理代码，迭代相关项目并传入浮点值；具体资源语义待确认                                       |

证据：[生命入口](../../output/pbz-rea-20261029/gm-sethealth-native-disasm.txt)、[生命处理](../../output/pbz-rea-20261029/gm-sethealth-body.txt)、[状态完整入口](../../output/pbz-rea-20261029/gm-state-native-complete.txt)、[状态处理](../../output/pbz-rea-20261029/gm-state-body.txt)、[补给入口](../../output/pbz-rea-20261029/gm-infinite-native-disasm.txt)、[补给处理](../../output/pbz-rea-20261029/gm-infinite-body.txt)、[指令关联字符串](../../output/pbz-rea-20261029/body-linked-strings.json)。

识别到两类相似的名称／指针表：实际 getter 表为 `{getter,name}`，native 表为 `{name,exec}`。早期把 getter 与下一个名称错配，`GM_SetHealth` 的正确 getter RVA 为 `0x651f080`，UFunction 全局 RVA 为 `0xe0124e8`；旧的 `0x651f0b0` / `0xe0124f0` 对应 `GM_SetNPCMovementMode`。native 入口 `0x6520c50` 和处理函数 `0x6629d00` 未变。修正证据见 [完整契约](../../output/pbz-dynamic-20261029/static-contracts/contracts.json)。90 项候选包含音频、UI 和调试方法，不能当作 90 项作弊功能。

LLVM 输出中的 `pesapi_load_addon+...` 或音频导出名是临近符号标注，不是恢复出来的 GM 函数名。旧的 `body-string-links.json` 混淆了 ASCII/UTF-16，已由 `body-linked-strings.json` 取代；原始捕获保留。状态入口以完整文件为准，早期拆分文件的中途解码不用于结论。

## 体力与资源命名

完整 EXE 的 ASCII 和 UTF-16LE 检索未发现 `Stamina`、`Endurance`。这不证明游戏不存在体力系统，只说明本次没有取得明确的对应属性。初筛中 `Timestamp` 的子串误命中已纠正，以区域评估的完整字面扫描结果为准。

`Posture` 有独立玩家属性遥测，`Anger` 有消耗、伤害和恢复线索，Wind/Thunder Energy 又与特定武器能力关联；不能把其中任一项未经验证直接标为“无限体力”。现有 UI 的“无限体力”仍是待适配占位，不应因为本报告而启用。

## 建议接入顺序

1. 建立当前 build 的对象／反射入口定位、游戏线程调度和失效对象处理；先只读核对玩家生命及状态。目前未确定稳定调用上下文、参数完整布局或跨版本定位方案。
2. 验证生命设置、免死／无敌、药品补给、终结技资源四项。逐项覆盖实际效果、重复操作、适用项的关闭恢复、死亡和换图后的行为；有名称不代表控制台或外部调用通路已开放。
3. 再接材料、单项升级、天赋和图鉴。涉及进度的功能需独立验证保存、退出和重载后的结果，再考虑批量操作。
4. 最后推进物品目录、坐标移动、战斗倍率和资源／冷却；跨地图及周目设置应等待任务与存档约束明确。

本轮没有变更 UI 或功能代码，也没有把任何候选标记为已支持。
