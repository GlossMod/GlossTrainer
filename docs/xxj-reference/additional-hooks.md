# 补充原生入口与实现边界

2026-10-24，Steam build `21393610`。本轮只读取静态游戏文件、运行隔离合成测试，没有启动参考修改器、操作游戏或写进程内存。EXE SHA-256：`cba6127df85fe6aedf5a71b398f268c8a963c7b19582c3cac087462518358e9a`。

托管 MethodDef RID 经已核对 MVID 的 Mono AOT v180 方法表解析；下列 RVA 仅是当前文件的证据，运行时不当作固定地址使用。完整记录在 `tmp/game-mapping/xxj/aot-method-map.json`；复查脚本与结果在 `tmp/xxj-extra/inspect-additional-hooks.py`、`verify-hook-sites.py`、`hook-sites-verified.json`。

| 功能 | 托管入口 / RID | 当前原生证据 | 实现方式 |
|---|---|---|---|
| 物品不减 | `CommB1.PlayerBag.CostItem(int,int)` / `0x374C`；`_CostStoreAndCarryItem(int,int,bool)` / `0x374B`，Script | 非货币 helper 返回点 `0x0187CAE0`；扣数减法 `0x0187CA1A` | 在物品存在、数量足够后将本次扣数设为 0。要求精确调用来源，排除货币、出售和仓库优先路径。 |
| 丹药 Buff 持续 | `b1.BUS_BuffComp.UpdateBuffTime(float)` / `0xFD61`，Main | 剩余时间减法 `0x02B1FD0C`，临近结束时间减法 `0x02B1FDDA` | 只冻结已识别丹药的这两个局部减法，不暂停周期触发、激活延迟、可靠性检查或清理。 |
| 丹药分类 | `BuffComp.BuffBegin(...)` / `0xFD66`；`BUS_PlayerItemSystem.OnTriggerItemEffectReal(int,OPReason)` / `0x17D53`，Main | `BuffBegin` `0x02B20350`；真实用药分支返回点 `0x03218024` | `PlayerItem` 来源还包括葫芦、酒与泡酒物，不能仅按枚举筛选。只接纳来自真实用物分支的玩家 Buff ID；其他 PlayerItem 来源会取消同 ID 分类。 |
| 法宝持续 | `b1.BUS_FaBaoManagerComp.OnTickWithGroup(float,int)` / `0xB14D`，Main | Cast 状态减法 `0x02750CB5`；RIP 常量 `0x02750E30` 为 `1000.0f` | 仅停止 Cast 状态的正数毫秒计时；保留 Carry、Cooldown、装备切换和负数重置。法宝 Buff 通常使用时长 `-1`，与这个计时器不同。 |
| 修改周目 | `CommB1.PlayerActor.GetNewGamePlusCount()` / `0x37C6`，Script | 实际字段读取 `0x0188442E` | 30 秒一次性请求，在自然 getter 中确认当前 live helper 后写字段。不是常驻覆盖，不主动调用托管方法，也不承诺已经落盘。 |

Script MVID：`64AC5451-3E83-486D-B18A-128163A8971C`；Main：`65A46ED7-6F85-40F5-AD32-079A507252B6`。对象字段还由 Runtime/Proto 各自的 MVID 与已映射 getter 指纹验证。

## 归属、输入与恢复

Buff/FaBao 组件的 `Owner` 为托管组件 `+0x10`，必须等于 `attributes::PlayerActor()`。Buff 当前版本的 `BuffID/+0xA8`、`Source/+0xB4`、`LeftTime/+0xC0` 与法宝数据的 `State/+0x38`、`CurrentCastDuration/+0x5C` 均有 getter 和实际消费代码双重证据。

物品回调使用纯字段链：`Bag+0x20 → GamePlayer+0xB8 → CSRpc+0x3D0 → Channel+0x10 → managed Controller`。要求 `GamePlayer+0x140` 为本地玩家；托管角色和控制器的 `UObject.Address/+0x58` 必须同时匹配主线程发布的原生角色/控制器。交易副本的 RoleBag 可以不同，不能误要求其等于 live 背包。

显式删除使用全局 mutex 保护的单次 `物品ID/数量/玩家/生命周期/30秒` 放行，支持异步 RPC，不使用仅当前线程有效的 TLS。关闭“不减”时经过的匹配删除也会消费放行，防止之后重开功能时残留。回调遇到锁竞争执行正常游戏扣数。

周目只接受 live `ActorHelper`，并校验 helper mode、主数据模式、完整原始 RoleActor 指针链和上述本地身份。`CsB1.RoleSvc` 初始检查会将 `0` 修正为 `1`（反编译文件 387–389 行），因此界面 1 对应字段 1，不能减一。成功只报告“已更新，随游戏保存”。

三个持续开关默认关闭，不保存启用状态。关闭后下一次自然计时/扣除恢复；未做常驻数值写入。原生玩家/控制器变化或清零时，生命周期序号令待处理周目、删除放行及丹药分类失效；同一有效 pair 连续发布会保留当前会话。游戏回调使用 `try_lock`，不等待 UI。

丹药分类从插件观察到的实际用药建立；启用前已存在且未被插件观察到来源的 Buff 无法可靠归类，界面开启结果明确说明“对此后使用的丹药生效”。药品 ID 分类容量 256；不能将合成策略测试当成游戏内效果确认。

## 固定视距

真正的相机距离是玩家 `USpringArmComponent.TargetArmLength`（float UPROPERTY，`/Script/Engine.SpringArmComponent:TargetArmLength`）。`BUS_PlayerCameraCompImpl.ApplyCameraControlData` / Main RID `0xFE01` 每帧由 `InControlData.ArmLength` 覆写它；单次设置会被后续帧覆盖。`ECameraParamType.ArmLength=6`（mask `0x40`）。

`ForceSetFreeCameraArmLength` / RID `0xFE40` 只在 Free arm mode 生效。控制台 `b.SetArmLen` 还会切换 arm mode，不能只恢复距离。BGUSpringArmComponent 有 `ResetArmLengthLerpCache` UFUNCTION，但未找到独立“固定距离”setter UFUNCTION。`FocalDistance` 是景深焦距，`FieldOfView` 是视野角度，都不能代替 arm length。该功能由主代理的属性基线与每帧重申路径处理。

## 已执行验证

`xmake build GlossTrainerAdditionalHookTests`（x64 Release）成功；测试程序输出 `PASS additional gameplay hook policy (0 failure(s))`。覆盖本地/外部对象、丹药来源、葫芦同 ID 排除、Cast 状态与关闭恢复、跨线程单次删除、过期/旧时戳/不匹配、交易副本排除、周目一次性 1-based 写入、场景变化与同地址重用。记录：`tmp/xxj-extra/additional-hooks-tests-release.txt`。

静态脚本验证 7 个 Hook/返回点均在各自方法范围唯一匹配，并验证法宝毫秒常量。以上不包含真实游戏效果测试、安装或图形宿主验证；这些由主代理单独记录。
