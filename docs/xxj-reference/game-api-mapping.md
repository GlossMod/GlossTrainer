# 当前构建的游戏 API 桥与新增功能映射

调查日期 2026-10-24。仅静态读取游戏 EXE 和其中的 CLI 程序集；没有执行参考修改器、输入游戏、写游戏内存、构建或安装。本文全部为静态证据，不代表游戏内通过。

## 推荐接入与版本身份

当前 Steam build `21393610`；`b1-Win64-Shipping.exe` SHA-256 `cba6127df85fe6aedf5a71b398f268c8a963c7b19582c3cac087462518358e9a`。完整程序集研究副本和反编译文件位于 `tmp/game-mapping/xxj`。**托管 IL RVA、文件偏移、metadata token 不能当成原生函数地址。**

推荐解析原生 USharp 的命名注册表，再经 `ProcessEvent` 调用经过参数反射验证的 UFunction。最小桥无需 Mono 导出，当前 EXE 也没有可用的 `mono_*` PE 导出。

`tmp/game-mapping/xxj/extract-native-bindings.py` 可复现扫描，结果 `native-bindings-all.json` 含 1,814 条已验证名字与原生代码映射。注册模板为：

```text
48 8D 15 rr rr rr rr 48 8D 0D ff ff ff ff FF D3
lea rdx,[rip+完整 Export_名字的 ASCII NUL 字符串]
lea rcx,[rip+原生实现]
call rbx
```

注册位置为 R 时，字符串目标 `R+7+signed32(R+3)`；函数目标 `R+14+signed32(R+10)`。只接受 PE 只读数据中的完整名字和可执行节中的唯一函数。JSON 中 RVA 仅是当前版本复查证据；运行时按模块基址重定位、字符串匹配，不硬编码 RVA。

## 最小原生 ABI

Win64 ABI；以下 `csbool` 均为 `int32_t`，不等于 C++ `bool`。`FScriptArray` 为 `{void* Data; int32 Num; int32 Max;}` 共 16 字节。

| 注册名称 | ABI |
|---|---|
| `Export_UObject_ProcessEvent` | `void(void* object, void* function, void* params)` |
| `Export_UObjectGlobals_StaticFindObject` / `StaticFindObjectSafe` | `void*(void* class, void* outer, FScriptArray* name, int32 exactClass)` |
| `Export_UClass_GetDefaultObject` | `void*(void* class, int32 createIfNeeded)` |
| `Export_UFunction_Get_ParmsSize` | `uint16(void* function)` |
| `Export_UObjectBase_IsValidLowLevel` | `int32(void* object)` |
| `Export_UObjectBase_IsValidLowLevelFast` | `int32(void* object, int32 recursive)` |
| `Export_UObjectBaseUtility_IsA` | `int32(void* object, void* baseClass)` |
| `Export_UObjectBaseUtility_IsPendingKillOrUnreachable` | `int32(void* object)` |
| `Export_UObject_GetWorld` | `void*(void* object)` |
| `Export_UObjectGlobals_IsGarbageCollecting` | `int32()` |
| `Export_FThreading_IsInGameThread` | `int32()` |
| `Export_FCoreDelegates_Reg_OnEndFrame` | `void(void* instance, void(*callback)(), uint64* handle, int32 enable)` |
| `Export_FString_GetCharSize` | `int32()` |
| `Export_FString_FromCharPtr` | `void(void* nulTerminatedChars, FScriptArray* result)` |
| `Export_FScriptArray_Destroy` | `void(FScriptArray*)` |
| `Export_UStruct_Get_ChildProperties` | `void*(void* structure)` |
| `Export_FField_Get_Next` | `void*(void* field)` |
| `Export_FField_GetName` | `void(void* field, FScriptArray* result)` |
| `Export_FField_HasAnyCastFlags` | `int32(void* field, uint64 flags)` |
| `Export_FProperty_GetOffset_ForUFunction` | `int32(void* property)` |
| `Export_FProperty_Get_ElementSize` / `Get_ArrayDim` | `int32(void* property)` |
| `Export_FProperty_Get_PropertyFlags` | `uint64(void* property)` |
| `Export_FBoolProperty_SetPropertyValue` | `void(void* property, void* valueAddress, int32 value)` |

证据是 `UnrealEngine.Runtime.Native.Native_*.decompiled.cs` 内实际委托签名，并与 `native-minimal-bodies.json` 反汇编交叉验证。`IsValidLowLevel` 自身不能防御任意无效地址；先校验可读内存、对象指针范围，并在游戏线程检查垃圾回收、PendingKill 和 `IsA`。

FString 通过游戏分配器创建并用游戏的 `FScriptArray_Destroy` 销毁，不能交给 C++ `free`。原生字符宽度应调用 `GetCharSize` 确认；当前 Runtime 默认 UTF-16。FString Num 包括末尾 NUL。

静态函数接收者使用类的 CDO：完整类路径查 UClass，再 `GetDefaultObject(class,1)`。函数使用相同类路径加冒号和函数名查找。每次按属性名字、类型、元素大小、ArrayDim、Parm 标志和 offset 范围构造参数。不能把字段顺序或 bool 大小当成固定布局。

`EClassCastFlags : uint64`：Int `0x80`、Float `0x100`、Class `0x400`、Str `0x4000`、Object `0x10000`、Bool `0x20000`、Struct `0x100000`、Array `0x200000`、ObjectBase `0x4000000`、Double `0x100000000`、Enum `0x1000000000000`。完整证据 `UnrealEngine.Runtime.EClassCastFlags.decompiled.cs`。

`FVector` 的 `mX/mY/mZ` 是 **double，合计 24 字节**，不是常见的三个 float；`UnrealEngine.Runtime.FVector.decompiled.cs:29/37/45`。`FName` 是两个 int32，共 8 字节。

游戏线程：在已确认 `IsInGameThread` 的现有原生玩家捕获回调里注册一次 `OnEndFrame`，由它执行排队操作。注册实现忽略 instance、保留无参数函数指针、返回 8 字节句柄。每帧重复检查线程、GC 与对象有效性并加 TLS 重入保护。**不要在尚未返回的属性访问 Hook 栈内切图或销毁 actor**。不使用尚未核实参数深拷贝/生命周期的 `ProcessEvent_GameThreadDefer`。

## 经过元数据验证的 UFunction

下表 `Object` 为对象指针，`Bool` 必须使用 bool property setter。参数布局全部运行时反射。静态类路径取冒号前部分。

| 反射路径 | 参数名字和类型 | 证据 |
|---|---|---|
| `/Script/b1-Managed.BGUFunctionLibraryManaged:RunScriptGM` | `GMCommand:Str, WorldContext:Object` | Managed.cs:389；反射调用 `GSGMSvc.RunGSScriptGM`，没有 HasGMFlag 检查 |
| `/Script/UnrealExtent.GSE_EngineFuncLib:GetFirstLocalPlayerController` | `WorldCtx:Object, ReturnValue:Object` | EngineFuncLib.cs:6256,8806 |
| `/Script/UnrealExtent.GSE_EngineFuncLib:GetWorldFromObj` | `Context:Object, ReturnValue:Object` | EngineFuncLib.cs:5620,8520 |
| `/Script/UnrealExtent.GSE_ActorFuncLib:GetActorLocationSimple` | `Actor:Object, OutPos:Struct(Vector24)` | ActorFuncLib.cs:1295,1863 |
| `/Script/UnrealExtent.GSE_ActorFuncLib:SetActorLocationSimple` | `Actor:Object, Pos:Struct(Vector24), bSweep:Bool, iTeleportType:Int` | ActorFuncLib.cs:973 |
| `/Script/b1-Managed.BGUFunctionLibraryCS:BGUHasUnitSimpleState` | `Unit:Object, SimpleState:Enum(byte), ReturnValue:Bool` | CS.cs:4634 |
| `/Script/b1-Managed.BGUFunctionLibraryCS:BGUSetUnitSimpleState` | `Unit:Object, SimpleState:Enum(byte), IsRemove:Bool` | CS.cs:4359,9173,10548 |
| `/Script/b1-Managed.BGUFunctionLibraryCS:ToggleCloudMove` | `Unit:Object` | CS.cs:7750,8768,10121 |
| `/Script/b1-Managed.BGUFunctionLibraryCS:BreakCloudMove` | `Unit:Object, BreakAnimId:Int` | CS.cs:7761,8620 |
| `/Script/b1-Managed.BGUFunctionLibraryCS:SwitchFreeCameraMode` | `InWorldContext:Object` | CS.cs:6946,9123；事件参数 None 会在 AutoTrail/G4Mode 间翻转（PlayerCameraCompImpl.cs:1434）；不是独立摄影相机 |
| `/Script/Engine.Controller:K2_GetPawn` | `ReturnValue:Object`，在 PlayerController 实例调用 | Engine.AController.cs:750；这是 GetControlledPawn 的真实反射名 |
| `/Script/Engine.Character:LaunchCharacter` | `LaunchVelocity:Vector24, bXYOverride:Bool, bZOverride:Bool` | Engine.ACharacter.cs:1226 |
| `/Script/Engine.Actor:GetComponentByClass` | `ComponentClass:Class, ReturnValue:Object` | Engine.AActor.cs:6369 |
| `/Script/Engine.Actor:SetActorHiddenInGame` | `bNewHidden:Bool` | Engine.AActor.cs:3957 |
| `/Script/Engine.Actor:SetActorEnableCollision` | `bNewActorEnableCollision:Bool` | Engine.AActor.cs:3976 |
| `/Script/Engine.CheatManager:EnableDebugCamera` / `DisableDebugCamera` | 无参数，在 CheatManager 实例调用 | Engine.UCheatManager.cs:421,439；未见 ToggleDebugCamera 或 GameplayStatics.SpawnObject 的托管绑定 |

`b1.EBGUSimpleState : byte` 路径 `/Script/b1-Managed.EBGUSimpleState`，`CantBeBaseTarget=38`、`Imperceptible=66`。敌人隐身的原始 console 实现分别添加/移除这两个状态。建议先 Has 读基线，关闭仅移除本项目添加的状态；还需考虑游戏在启用期间自身施加状态的所有权问题。

## GM 派发与物品事务

`RunScriptGM` -> `GSGMSvc.RunGSScriptGM` -> `RunGMCommand`。本地方法按不区分大小写名字反射，包含 private 及 GMNoExport；未找到才检查 `PlayerGmExecutor`，最后 fallback 控制台。不要允许自由命令输入覆盖此白名单。

`RunServerGM` 的名字不代表外部服务器：`CsB1.CSRpc.RequestMiscExecGM` 标记 `Endpoint=Client`，经 `UnrealNetChannel.SendMessageToClient` 派发；同程序集 `HandleMiscExecGMReq` 调用 `Player.CreateTransaction -> PlayerGmExecutor.TryInvokeGmCmd -> Commit`。证据 `CsB1.CSRpc.decompiled.cs:17100–17122,26884–26940`。这确认本地事务实现存在，但不确认当前 GamePlayer/Rpc 已就绪。RunScriptGM 返回 void，不能将“成功派发”显示为“物品已添加”。

`BGWConsoleCommands` 的 `HasGMFlag` 先拒绝 `PlayerGMLevelFlags<=0`；`b.WTF 1` 仅改变另一层输入门控，不能保证 `b.*` 注册。下表直接走 RunScriptGM 的命令无需先开启 b.WTF。

| 功能 | 命令 | 已确认行为 / 限制 |
|---|---|---|
| 添加物品 | `AddItem Id [Num=1]` | `Bag.GainItemOne`，执行实际背包事务 |
| 减少物品 | `CostItem Id [Num=1]` | `Bag.CostItem`，不可用负数 AddItem 替代 |
| 删除该种物品 | `ClearItem Id` | `Bag.GMClearItemOne`；方法帮助漏写参数，实际必须有 Id |
| 材料/消耗品批量 | `AllItem [Num=1]` | 仅 ItemType 3/4，且排除酒；不是全部物品 |
| 任务道具批量 | `AllTaskItem [Num=1]` | 仅 ItemType 5 |
| 各分类获取 | `AllWine`, `AllSeeds`, `AllEquip`, `AllHulu`, `AllWeapon`, `AllSpell`, `AllAttrItem`, `AllRecipe`, `AllLegacy`, `AllSoulSkill` | 以 PlayerGmExecutor 具体分类筛选为准；装备按系列取值，不能冒称逐个所有 ID |
| 影神图 | `AllCard` / `ClearCard` | Collection 路径；ClearCard 还清红点 |
| 影神图单项 | `Card CardId ECollectionStage ECollectionPortraitStage` | 三个参数必须齐全；帮助中 RedPoint 命名与实际第三参类型不一致，以方法体为准 |
| 挑战敌人解锁 | `AllBossRush` | `BossRush.GMUnlockAllBossRush` |
| 地图图册解锁 | `AllMap` / `Map MapId` | **当前方法体只有 return 0，实际无效果，禁止用作实现** |
| 打坐点 | `AllMedition Status` | Status 0/1；原拼写确为 Medition |
| 全部护甲满级 | `AllArmorToTop` | 存档修改，不是临时开关 |
| 法术/技能 | `AddSpell Id`, `SetSpell Id`, `SoulSkill Id`, `SetSoulSkill Id` | 分别获得/装备，不能互相替代 |
| 土地庙对应地图区域解锁 | `UnLockRebirthPointArea RebirthPointId` | handler 的参数实际是土地庙 ID，遍历地图区域配置；不是直接激活土地庙功能 |
| 全土地庙 | `b.ActiveAllRebirthPoint` | 注册在 HasGMFlag(4) 块；handler 遍历全部 RebirthPointDesc、只选 Level.RebirthSortId>=1，Evt_ActiveRebirthPoint(id,true)，还触发当前世界庙交互 |
| 下一周目 | `ToNextGame` | `RequestActorOnNewGamePlus`；handler 要求 IsGameCompleted，否则10005；事务清理→count+1→NewGamePlusAdd→Commit，不是任意数值设置 |
| 调试周目难度 | `SwitchGameCount count` | 写 `DebugConfig.DebugGameCount` 并重载 tamer；不是持久化周目 |
| 任务阶段 | `ActiveTask QuestId` / `FinishTask QuestId` | `RequestTaskChangeQuestStage`，阶段 1/2 |

**隐藏结局**尚未找到充分的任务/条件 ID 组合。`FinalGameLevelPass` 帮助为“打过杨戬回到主界面”，调用全局 FSM 事件 5，不能等同于解锁隐藏结局。不要把通用 FinishTask 暴露为已验证隐藏结局按钮。

**筋斗云**：`ToggleCloudMove` 是完整骑乘/下云技能流程，受冷却、区域禁止启用/退出等状态限制。`BUIAToggleCloudMove` 输入流程另外检查 `CloudMoveConfig.CloudMoveItemId` 是否持有。真实资产由 `BGW_PreloadAssetMgr.CloudMoveConfig` 读取：`/Game/00Main/DataAsset/DA_CloudMoveConfig.DA_CloudMoveConfig`，类 `/Script/b1-Managed.BGWDataAsset_CloudMoveConfig`，属性 `CloudMoveItemId:Int`。可在游戏线程查找/加载该真实资产、反射读正数 ID 后 `AddItem ID 1`；不要猜 ID。`CloudInit` 是在线云存档初始化，与筋斗云无关。

## 不宜直接包装为可恢复开关的现有命令

| 方法 | 实际行为 | 结论 |
|---|---|---|
| `PlayerHidden 0/1` | 0 分离镜头并缩放角色为 0.0001；1 设缩放 1 并重新附着，必要时 Z+100 | 不是单纯 SetActorHiddenInGame，也不是敌人隐身；不用于纯隐藏玩家开关 |
| `ChrisSkipPawnCollision` | 对通道 1/2 添加 Ignore 的两类碰撞属性事件 | 未见配套移除/句柄恢复，不作为安全可恢复无碰撞开关 |
| `GMGamePause` | 共享 IsGamePause bool 翻转；GlobalTimeDilation 在 0.001 和 1 之间切换 | 非幂等，恢复固定 1，可能覆盖已有速度 |
| `GMGamePauseV2` | 同一个 IsGamePause bool；翻转 pause event 7 | 与上项混用状态冲突，优先原生 Get/SetGamePaused |
| `SetCanCameraRoam 0/1` | 先 SwitchChapterViewCamera、显示页面 53，再 SetCanCameraRoam | 地图/章节相机流程，有额外 UI 副作用，未证实是自由摄影模式 |
| `SwitchFreeCameraMode` | 固定向事件传 `EPlayerFreeCameraType.None=0`，接收端将其翻转为 AutoTrail/G4Mode | 切换相机跟随模式，不是离开角色的摄影相机 |

引擎属性新增证据来自 EXE `0x1b2792e0` 的完整引擎 CLI 副本 `engine-0x1b2792e0.dll`：`Character.CharacterMovement:Object`、`CharacterMovementComponent.GravityScale:Float`、`JumpZVelocity:Float`、`Character.JumpMaxCount:Int`、`SpringArmComponent.TargetArmLength:Float`、`PlayerController.CheatManager:Object`。**只改 JumpMaxCount 不能实现连续跳**：游戏 `BUIAJump` 在 JumpMoving 时拒绝，`BUS_JumpComp.UnitCanStrideJump` 在 IsFalling 时拒绝。游戏相机也可能每帧重写 SpringArm 距离，须走实际更新点或保存/恢复基线。

创建 UObject 的已验证 native ABI：`Export_UObjectGlobals_StaticConstructObject_Internal -> void*(class, outer, FName* name, uint32 EObjectFlags, int32 EInternalObjectFlags, template, int32 copyTransients, instanceGraph)`。加载资产为 `Export_UObjectGlobals_StaticLoadObject -> void*(class, outer, FScriptArray* name, FScriptArray* filename, uint32 ELoadFlags, sandbox, int32 allowObjectReconciliation)`。两者均来自 `Native_UObjectGlobals` 委托；必须游戏线程、GC 未运行，并保留有效 GC 引用。

## Mono AOT 方法表：已与现有属性解析器交叉核对

新发现的 **实际 MonoAotFileInfo v180 方法地址表**允许按程序集身份与 MethodDef RID 定位原生方法。它不使用 IL RVA。参考结构布局来自 [Mono 官方 aot-runtime.h](https://github.com/mono/mono/blob/main/mono/mini/aot-runtime.h)，本机构建的字段指针、MVID、代码边界、标量及 call 表已逐项验证，不能盲用官方最新版布局到别的版本。

| 字段 | v180 结构相对偏移 |
|---|---:|
| version / dummy | 0 / 4，各 uint32，当前180/0 |
| jit_code_start / end | 0x30 / 0x38，指针 |
| method_addresses | 0x40，指针 |
| assembly_guid | 0xC0，ASCII GUID 字符串指针 |
| assembly_name | 0x108，ASCII 程序集名指针，无.dll |
| nmethods | 0x154，uint32，含额外实例化方法，不等于CLI总数 |
| call_table_entry_size | 0x188，uint32，当前5 |

`selected-tokens.ps1` 通过 .NET PEReader 静态读程序集名、MVID、MethodDef RID、签名 blob、参数和总数；不执行程序集。`extract-aot-map.py` 找完整程序集名的指针引用，从 name slot 反推 header，校验唯一结构及所有边界，生成 `aot-method-map.json`。

地址规则：`entry = method_addresses + (RID-1)*5`，要求 `entry[0]==0xE8`，`native = entry+5+signed32(entry+1)`，且在该模块 `[jit_code_start,jit_code_end)` 和可执行节内。代码版本不匹配、MVID不同、出现多个候选、表项非 E8 或边界不成立时全部拒绝。

| 模块 | MVID | 当前 header RVA / code RVA / table RVA |
|---|---|---|
| BtlSvr.Main | 65A46ED7-6F85-40F5-AD32-079A507252B6 | header 1D0E78C0；code 01E83010..047E8BB0；table047E8BB8；nmethods220718，CLI MethodDef106230 |
| B1UI_GSE.Script | 64AC5451-3E83-486D-B18A-128163A8971C | header1D0E7658；code0141B7F0..01E0BFB8；table01E0BFC0；nmethods43740 |
| UnrealEngine.Runtime | 70421AB0-86DD-4BD4-AFD6-6C909E8B51A7 | header1D0EAB18；code071EB340..076122B8；table076122C0；nmethods28155 |
| Protobuf.RunTime | 3CED0FE6-3455-4ADB-9501-636B0DB4FC87 | header1D0E9A60；code0521DEC0..05700598；table057005A0；nmethods40388 |

强交叉证据：主程序集 RID `0x8CA2` 是 `BUC_AttrContainer.GetFloatValue`，call 表解析为 native RVA `0x255CAF0`，机器码完整匹配现有 `resolver.cpp::ResolveListProbe` 浮点 getter 模板；RID `0x8CA1` 的 SetFloatValue 指向 `0x255C730`（XMM2 浮点参数），RID `0xC3F1` 的 GSBindList<T>.get_Item 指向 `0x281A5D0`（读取 this+0x30 的 ValueList）。这是独立于 IL RVA 的实际编译代码关联。

**调用边界**：MinHook 拦截自然发生的 managed 调用可以保持原来线程/运行时上下文。普通 AOT 方法入口不是自动验证过的 reverse-P/Invoke thunk；不能仅因知道地址就从任意原生线程调用，亦不能把 native UObject* 当托管包装对象 this。泛型方法还可能依赖 R10 context。具体 Hook 必须逐个核对机器码、寄存器、返回类型和玩家归属。

GM 注册最小策略：AOT RID `0x1399F` 的 `BGWConsoleCommands.HasGMFlag(int Flag)` 原生 ABI `int32(int32)`，RCX/EAX，无 R10 入参；RID `0x139A0` 的 `get_IsPlayerGMInputEnabled()` 为 `int32()`。只放开 Flag4 及输入判定，让游戏自然 `OnTick -> CheckRegisterAllCMD -> InitDebugGM` 注册传送和全庙命令。实际调用前必须 `FindConsoleObject` 成功，再 `ProcessUserConsoleInput` 检查返回值。若 `ResisterAllCMDFinish` 已被旧流程置 true 而命令缺失，须报告失败，不能声称已注册。

## 本地玩家归属与真实周目字段

以下是当前 MVID 对应的编译代码证据。地址均为本节 AOT 方法表解析结果，只供交叉验证；实现必须先验证模块身份和方法字节，不能独立沿用裸偏移。

| 对象成员 | 实际偏移/类型 | 证明方法、token、native RVA |
|---|---|---|
| PlayerBag.mPlayer | +0x20 managed reference | InitForHelper/InitForDataMgr，见 aot-method-map.json |
| PlayerActor.mData / mDataMgr / mPlayer | +0x10 / +0x18 / +0x20 managed reference | InitForHelper 06003786/01880E80；InitForDataMgr 06003787/01880EF0 |
| PlayerActor.IsDataMgrMode | +0x30 byte | 上两方法分别写0/1 |
| GamePlayer._ActorHelper | +0x20 managed reference | get_ActorHelper 06003AAF/018AEE00 |
| GamePlayer.WorldContext | +0x88 managed UObject reference | Init 06003ABD/018AF5C0 |
| GamePlayer.RootData | +0x90 managed DSRoleData reference | Init、get_RoleDataCS 06003AEF/018B0840 |
| DSRoleData.Protobuf | +0x10 managed RoleData reference | get_Protobuf 06002B33/017D0AC0；GamePlayer.get_RoleData 06003AEE/018B0810内联相同读取 |
| RoleData.RoleCs | +0x18 managed RoleDataCS reference | Protobuf.RunTime get_RoleCs 060073D4/05530B50 |
| RoleDataCS.Bag / Actor | +0x20 / +0x28 managed reference | Protobuf.RunTime get_Bag 06007413/055328F0；get_Actor 06007415/05532950 |
| GamePlayer.Rpc | +0xB8 managed CSRpc reference | InitRpc 06003AE5/018B0130 |
| GamePlayer.DataMode | +0x130 int32 | IsMasterMode 06003ACE/018AFCD0比较0；IsDataReadOnly 06003AD0/018AFD30比较1 |
| GamePlayer._IsLocalPlayer | +0x140 byte | IsLocalPlayer 06003AC5/018AFA00 |
| CSRpc.UnrealNetChannel | +0x3D0 managed reference | DeAttachPlayerController 0600274A/01786030 |
| GSRpcUnrealChannel.PlayerController | +0x10 managed IGSRpcController reference | AttachPlayerController 06003C83/018C11E0；IsPlayerControllerReady 06003C86/018C1440 |
| UObject.Address | +0x58 native pointer | UnrealEngine.Runtime get_Address 0600145F/072F5EB0，直接 mov rax,[rcx+58] |
| RoleActor.newGamePlusCount_ | +0x28 int32 | Protobuf.RunTime get_NewGamePlusCount 0600778C/0554DFB0；set_NewGamePlusCount 0600778D/0554DFD0 |

本地 Bag 校验可在自然 CostItem 入口同步遍历 `bag.mPlayer -> Rpc -> UnrealNetChannel -> PlayerController -> Address`，要求其 native controller 等于当帧已通过 Unreal 反射有效性/IsA 校验的本地 controller，同时要求 IsLocalPlayer。WorldContext 通常是 GameInstance，不能当 Pawn。IGSRpcController 这一接口字段应只有匹配 canonical controller 的对象才能通过，不能对任意对象+0x58 就视为可信包装。不要缓存托管指针到之后的异步线程；每次回调重新检查。

`CommB1.PlayerActor.GetNewGamePlusCount`（B1UI_GSE.Script，060037C6，018843B0）先检查 DebugConfig.DebugGameCount>0，再读取 `[[this+0x10]+0x28]`。`SetNewGamePlusCount`（060037FC，018870A0）只把 int 参数写入同一字段，没有清关、发奖励、改任务或自动保存逻辑。`ArchiveB1.RoleActor.WriteTo` 会把非零 NewGamePlusCount 写为 protobuf tag24/int32，因此这是实际存档字段，不是调试难度覆盖。

**周目数是从1开始，不能把 UI 值减1。** `CsB1.RoleSvc.LoginCheckFixRoleData` 在 `GetNewGamePlusCount()==0` 时显式 `SetNewGamePlusCount(1)`（`CsB1.RoleSvc.decompiled.cs:331,387–389`）。首次生成 `B1UI.RoleLoginSvc.GenNewRoleDataByConfig` 在初始化配置前调用该检查（同名解编文件:25,62），登录也再调用（:161）。一周目字段值为1，二周目为2；protobuf默认0只代表尚未初始化/缺省。`HandleChapterGameCompleteReq` 在count==1时调用BossRush.RefreshBossRushData（CSRpc:27265），与首轮通关语义一致。

一次性编辑建议放在自然 `PlayerActor.GetNewGamePlusCount` 的入口回调：仅接受 IsDataMgrMode==0、`mPlayer._ActorHelper==this`、本地 controller 身份匹配和当前 live RoleActor 一致的实例。先对请求做事务锁/过期验证，再写 int32 并立即读回；成功只表示当前真实角色数据已更新。若 DebugGameCount 已被其它工具设为正，getter 返回值会继续被覆盖，不能宣称可见难度已同步改变。

live RoleActor 身份可重新从 `GamePlayer+0x90 -> DSRoleData+0x10 -> RoleData+0x18 -> RoleDataCS+0x28` 获取，再与 helper.mData 比较。要求 DataMode==0（Master），避开Slave只读角色。不要依赖 Proto getter Hook 一定命中：已见 PlayerActor getter 将 Proto getter 内联为直接字段读取；只有方法地址存在不能证明运行时会独立调用。备选自然 GamePlayer.get_RoleDataCS 入口可用同一live链消费请求，不需要原生调用托管setter，但实际命中频率仍需用户日志验证。

必须拒绝事务副本：`CommB1.PlayerDataMgr.Actor` 在首次访问时给 `_Actor.InitForDataMgr(Player,this,mData.Actor.Clone())`，`Bag` 同理；其值只有 `Commit -> Compare -> MergeRoleData` 才合入 live root。`Commit` 还调用 `BGW_GameArchiveMgr.MarkDelaySaveArchive` 和 `MarkSaveArchive`。直接写 live 字段不会自动调用这些保存标记，落盘必须经过随后真实游戏保存并重载验证；未做运行验证时只标“已更新当前角色数据，等待游戏保存”，不能标“存档已保存”。证据见 `CommB1.PlayerDataMgr.decompiled.cs:91–109,909–960`。

## 原生 DebugCamera 注册的补充证据

托管 UnrealEngine 程序集没有 ToggleDebugCamera/GameplayStatics.SpawnObject 绑定，不等于原生函数不存在。`debugcamera-native-registration.json` 已恢复原生注册：EnableDebugCamera exec RVA0EB02950（无参数后调用 vtable+0x440），DisableDebugCamera RVA0EB028B0（vtable+0x448），ToggleDebugCamera RVA0EB02ED0（vtable+0x360）；GameplayStatics.SpawnObject exec RVA0EB348A0 解析两个对象参数。

进一步静态跟踪已确认 **Shipping 中真实虚函数不是空返回**。类名 UTF16 `CheatManager` 的 RIP 引用定位 StaticClass RVA0EB00B30；其传入的构造 wrapper0EB00DF0经过直接E9跳转到2940FFB0，再调用0E345630（直接E9到282113A0）。真正构造函数写vtable RVA11A9F4F8，并设置DebugCameraControllerClass。该vtable的slot0x440指向Enable0E351520、slot0x448指向Disable0E350A40、slot0x360指向Toggle0E35DFD0。证据脚本/输出：`find-cheatmanager-native.py`、`cheatmanager-native-xrefs.json`、`inspect-debugcamera-vtable.py`、`debugcamera-native-vtable.json`。这些是静态交叉证据，功能层应仍通过反射调用，而不直接调用固定RVA。

Enable完整逻辑检查非CDO、Outer有Player且本地，生成DebugCameraController，写自身DebugCameraControllerRef，执行OnActivate(originalPC)，然后UPlayer.SwitchController(debugPC)。Disable读取自身Outer并要求它是DebugCameraController，再用其OriginalPlayer/OriginalControllerRef恢复控制器、调用OnDeactivate。**所以对仍Outer=原PC的同一个CheatManager调用Disable没有效果。** OnActivate完整函数0E467410..0E4678DE没有迁移原CheatManager的Outer；它记录原Player/Controller，初始化摄影相机并停止原PC/相机tick。

字段有真实原生反射记录，可用现有FObjectProperty类型校验读取，不需要裸偏移：

| 反射属性路径 | native属性参数记录RVA | 记录offset与机器码交叉证据 |
|---|---|---|
| `/Script/Engine.CheatManager:DebugCameraControllerRef` | 11C78000 | +0x28；Enable0E35161B写；type构造fn0EB15370 |
| `/Script/Engine.CheatManager:DebugCameraControllerClass` | 11C78030 | +0x30；ClassProperty，构造默认ADebugCameraController |
| `/Script/Engine.DebugCameraController:OriginalControllerRef` | 11C8D140 | +0x8E0；OnActivate0E467452写originalPC，OnDeactivate0E467B31清零 |
| `/Script/Engine.DebugCameraController:OriginalPlayer` | 11C8D170 | +0x8E8；OnActivate0E467448写originalPC.Player，OnDeactivate0E467B3C清零 |

属性名字符串及唯一指针引用保存在`debugcamera-native-properties.json`。Native结构里ArrayDim=1和offset已可与Enable/OnActivate双向核对，尽管托管公开绑定未导出这些private字段。运行时仍需反射验证ObjectProperty、UObject有效性与IsA(DebugCameraController/PlayerController/Player)。

建议开启/关闭流程：

1. 保存原PC和其CheatManager属性；在原PC下创建本项目cm0并暂设为PC.CheatManager。启用前记录cm0.DebugCameraControllerRef（全新对象应为空）。
2. 调用cm0.EnableDebugCamera。成功条件包括Ref为有效DebugCameraController、其OriginalControllerRef等于原PC、OriginalPlayer有效、GameplayStatics.GetPlayerController(0)已成为该debugPC；仅ProcessEvent成功不足以确认。
3. 关闭时取debugPC.CheatManager。若为空，在debugPC下创建cm1并设置该属性；确保cm1的真实Outer为debugPC，再调用cm1.DisableDebugCamera。关闭流程不得依赖当前Pawn存在，因为debugPC可能无Pawn。
4. 验证当前控制器已回原PC、debugPC的Original字段清空后，再恢复两套被替换CheatManager属性。不要先失去原PC/debugPC/cm0的恢复句柄。
5. Disable不销毁debugPC。若采用每次新建而非持有GC强引用复用，则只对本项目确认新生成、已完全停用的debugPC调用Actor.K2_DestroyActor，防止反复开关积累world actor。不要销毁启用前已有对象；恢复失败时保留恢复状态。

`DebugCameraController.ReceiveOnDeactivate(RestoredPC)`只是真正OnDeactivate清理末尾触发的Blueprint事件，不能替代步骤3；其反射存在不代表它执行控制器恢复。最终实际输入、摄影视角、Alt+Tab和正常返回仍须用户手工验收，静态代码完整不等于游戏内通过。

## 单项图鉴、精魄与删除操作的准确边界

`game-enums.json` 直接从内嵌 CLI 元数据读取常量：`BtlShare.ECollectionStage : byte` 是 FirstSight=0、Story1..Story5=1..5、EnumMax=6；`ECollectionPortraitStage : byte` 是 FirstSight=0、Completed=1、EnumMax=2。因此 `Card ID 2 1` 只表示故事第二阶段和完整画像，不能称为单项全解锁。

`PlayerCollection.CalCardCollectionStage(int StoryNum)` 将 CardDesc.CardStory.Count 限制到0..5，AllCard逐项使用它。`PushCardStoryStage` 仅在stage==CardStory.Count时推动对应BossRush解锁，全图鉴检查也要求每张卡达到其精确最终阶段；盲设5同样不可靠。`GMSetCard` 先RemoveCard，再FindOrInsertCard，再分别推动画像与故事，所以 `Card ID 0 0` 并非删除。当前四程序集（Main、B1UI、b1.Native、b1.NativePlugins）的UFUNCTION元数据扫描未发现可读CardStory.Count的桥，结果记录在`card-ufunction-candidates.json`；已解编两套GM也未发现自动完成单卡的命令。应提供“设置图鉴阶段”及明确0..5/0..1选择，或将来实现身份受控的自然managed调用解析；不能固定阶段冒充“全解锁”。

参考INI list13是精魄条目，例8011/8111等；无需进行ID转换。`SoulSkill ID`（**不是AddSoulSkill**）先验证GetItemDesc存在，再调用`PlayerBag.GainSoulSkill(PlayerMgr,ID)`。普通`AddItem ID 1`也有效：`GainItemByType`对ItemType==18明确分流到相同GainSoulSkill（PlayerBag解编:1883–1924），之后继续OnGainItemSuccess。因此统一添加路径可以保留该分类事件处理；若支持读当前ItemDesc，仍应验证type18，不要按ID位数猜类型。重复已有精魄树走替代掉落，不能承诺增加第二份或直接升级至所选等级。

| 删除对象 | 真实实现与副作用 | 现有GM可达性 |
|---|---|---|
| 普通背包ItemList条目 | PlayerBag.RemoveItem(int)：取消快捷栏、删除ItemList、清相关红点 | `ClearItem ID` ->GMClearItemOne->RemoveItem |
| 单项法术 | PlayerActor.RemoveSpell(int)：若已装备先UnSetSpell，删除Progress.SpellList，清类型3/20红点 | 已解编GM中无RemoveSpell；不能用ClearItem替代 |
| 单项精魄 | PlayerBag.GMRemoveSoulSkillByUid(ulong)：已装备则ResetSoulSkill，按真实UID删除SoulSkillList并清红点 | private，仅见GMClearBag批量调用；输入是UID，不是list13物品ID；ClearItem不处理此列表 |
| 单项影神图 | PlayerCollection.RemoveCard(int)：删除卡并清故事/图鉴红点 | private，仅见GMSetCard先删除再插入；ClearCard是全量清理 |

这些托管删除方法存在不等于可以从原生OnEndFrame直接调用；仍缺安全的事务/运行时调用桥时，UI应禁止对应单项删除并给出具体原因，不能派发无效果的ClearItem显示完成。范围广泛的ClearBag/AllSoulSkill也不能用来模拟单项删除。

## 隐藏游戏 UMG 界面

仅改AHUD.bShowHUD不足以覆盖本作UMG。`B1UI.GSUI.UIBattleMainCon`直接操作RootUserWidget/子UWidget的Visibility与RenderOpacity（:604–611,995–1053），并绑定DSBattleMain.ShowState；这些路径未查询AHUD.bShowHUD。

已查候选中，`SetBattleUIShowStat n`是可派发GM，调用GenABattleMain.SetBattleMainShow，但其CanShow/CanHide受HUD设置、战斗/过场等条件控制，Tick会再次改变状态；固定写3、关闭写2既可能无效，也不能恢复原状态。`BGUHideLevelUI(Unit)`虽有UFUNCTION，却仅触发单向HideLevelUI事件，未找到对应读基线/显示接口，不推荐作为可恢复开关。`BGWConsoleCommands.OnGSShowBattleUI`当前方法体只有参数解析，没有实际显隐操作。

现有引擎桥可直接作用于顶层UMG根控件，适合明确命名为“隐藏游戏界面”（含UMG菜单，范围大于仅战斗HUD）：

| 反射路径 | 准确参数类型 |
|---|---|
| `/Script/UMG.WidgetBlueprintLibrary:GetAllWidgetsOfClass` | WorldContextObject:Object；FoundWidgets:Array<Object> out；WidgetClass:Class（`/Script/UMG.UserWidget`）；TopLevelOnly:Bool=true |
| `/Script/UMG.Widget:GetVisibility` | ReturnValue:Enum（byte underlying） |
| `/Script/UMG.Widget:SetVisibility` | InVisibility:Enum（byte underlying） |

ESlateVisibility真实枚举：Visible=0、Collapsed=1、Hidden=2、HitTestInvisible=3、SelfHitTestInvisible=4。参数与验证来自 `UnrealEngine.UMG.UWidgetLibrary.decompiled.cs:1897–1925,2610–2619` 和 `UnrealEngine.UMG.UWidget.decompiled.cs:1955,2635,3030,3293`，枚举独立解编同目录。FoundWidgets读取后须DestroyValue释放返回数组，现有桥的ObjectArray RAII应覆盖。

建议仅对当前可绘制的0/3/4保存基线并设Hidden2，原Collapsed/Hidden不作为本项目修改；启用期间周期枚举新根控件，并对已跟踪控件按帧维持Hidden。如果游戏把控件改为其它状态，保存新观察状态作为恢复基线；关闭时仅恢复仍是本项目Hidden且同一对象身份的条目。不能统一设Visible，也不要改RenderOpacity以免覆盖游戏自身淡入淡出。ImGui绘制不属于UMG枚举。

对象跨GC/切页必须以弱句柄跟踪，避免native地址被新对象复用。真实USharp注册提供：

```text
struct FWeakObjectPtr { int32 ObjectIndex; int32 ObjectSerialNumber; }; // 8 bytes
Export_FWeakObjectPtr_SetUObject : void(FWeakObjectPtr* instance, void* value)
Export_FWeakObjectPtr_Get        : void*(FWeakObjectPtr* instance)
Export_FWeakObjectPtr_IsValid    : int32(FWeakObjectPtr* instance, int32 evenIfPendingKill, int32 threadsafeTest)
```

初始化为{-1,0}后SetUObject；游戏线程IsValid传0,0，Get失效返回空则丢弃，不能GetEvenIfUnreachable继续恢复。当前静态注册xref/native RVA分别是Get0AE64E3E/0AE51050、SetUObject0AE64E5E/0AE51180、IsValid0AE64E7E/0AE510E0；按注册名解析，不硬编码RVA。证据是native-bindings-all.json及新解编Native_FWeakObjectPtr/FWeakObjectPtr。

轮询恢复存在明确边界：当本项目已设置Hidden2时，游戏自身再次请求Hidden2无法仅靠字段读数区分，因此“最新基线”只是可观察到的状态变化；不能声称捕获所有真实写入。精确区分需要额外拦截SetVisibility自然调用。验收应覆盖启用期间打开/关闭页面、切关卡及GC、解除隐藏后原本折叠页面仍不出现，并确认本项目ImGui仍可操作。

## 执行顺序与验收边界

1. 主代理拥有引擎桥：命名注册解析、对象有效性、CDO、参数反射、FString RAII、OnEndFrame 队列；首先只读获取坐标。
2. 功能层使用白名单 UFunction / GM 模板，先添加/减少单物品与同地图坐标保存/回退，再添加状态开关和存档操作。参考 ID 数据与调用层分开；不让配置文件当任意指令执行。
3. 用户手工验证：同一 ASI 已派发、游戏状态确实改变、失败原因可见、关卡重载后对象更新、关闭恢复属于本项目的修改。未经测试一律标静态映射/待验证。
4. 全部新增截图功能的“完成”不能由命令存在推导；隐藏结局、全庙 ID、云解锁 ID、无限物品/丹药 buff/法宝持续时间仍需额外证据。

本调查不改游戏文件，不触碰现有插件，不操作游戏。后续研究会补充本文件和 `tmp/game-mapping/xxj/feature-api-map.json`。
