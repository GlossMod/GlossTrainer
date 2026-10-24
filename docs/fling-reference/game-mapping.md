# 当前游戏构建的只读接口研究

调查日期：2026-10-24。本文记录实际磁盘文件、CLI 元数据和反编译结果；没有启动参考修改器，没有写游戏进程内存，也没有修改游戏目录。本文不是功能通过记录，方法 token、IL RVA 和文件偏移均不能直接作为原生 Hook 地址。

## 文件与构建身份

- Steam App ID `2358720`，`E:\steam\steamapps\appmanifest_2358720.acf` 的 `buildid=21393610`。
- 游戏主文件：`E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64\b1-Win64-Shipping.exe`，728,458,376 字节。
- 主文件 SHA-256：`cba6127df85fe6aedf5a71b398f268c8a963c7b19582c3cac087462518358e9a`。
- 本次调查时 Win64 目录没有 `dwmapi.dll`、`wukong_minimap.dll`、`Mods` 或 UE4SS 文件。旧验证报告记载的文件不代表当前磁盘状态；本次没有删除或恢复它们。
- 全游戏树未找到独立的 `.lua`、`.cs`、游戏托管程序集、SDK dump。`b1\Content\Paks` 保存 `.pak/.sig` 文件；未修改或解包这些包。

游戏主 EXE 中包含完整的 PE/CLI 程序集。通过嵌入 MZ/PE、CLI data directory、BSJB metadata signature 和节的文件范围校验后，得到下列切片。临时研究副本放在工作区 `tmp/game-mapping/`；这不是游戏安装内容。

| 嵌入模块 | EXE 文件偏移 | 长度 | MVID / 身份 |
|---|---:|---:|---|
| `b1.Managed.dll` | `0x16167240` | 25,600 | `f193b62b-4f10-4213-b70d-ae1c7c62ad3f` |
| `b1.Native.dll` | `0x1616D640` | 4,620,288 | `a56e8b20-e842-4dcb-a337-3200430cc0e8` |
| `B1UI_GSE.Script.dll` | `0x168B3640` | 4,228,096 | `64ac5451-3e83-486d-b18a-128163a8971c`；未完成成员解析 |
| `BtlSvr.Main.dll` | `0x16CBBA40` | 37,629,440 | `65a46ed7-6f85-40f5-ad32-079a507252b6` |
| `GSE.ProtobufDB.dll` | `0x19160660` | 3,836,416 | `807fe2db-ca58-45f5-83d9-bb07d5e2c0e3` |
| `UnrealEngine.Runtime.dll` | `0x1C30B0E0` | 1,564,160 | `70421ab0-86dd-4bd4-afd6-6c909e8b51a7`；未完成成员解析 |

`BtlSvr.Main.dll` SHA-256 为 `37d21f03714232499ee13f47ed294c9e36e5167b7e5db39e2ac116b14b8abdf5`，`GSE.ProtobufDB.dll` 为 `743d83a014b6d70e076f70cae84e5d9189d3f324fd43b36490f83ca64bb7e716`，`b1.Native.dll` 为 `6eb661e7c46e95c43ad366bb350e2492366685ca518ed1729e84e53ca1d54eba`。

## 运行时与解析边界

主 EXE 的静态字符串具有明确 Mono AOT 证据：

| EXE 文件偏移 | 字符串 |
|---|---|
| `0x11EFC280` | `d:\git\mono_develop\mono\mini\aot-runtime.c` |
| `0x11EFCA18` | `Found statically linked AOT module '%s'.` |
| `0x11EFCAE8` | `mono_aot_file_info` |
| `0x11EFD2C0` | `mono_aot_get_method` |
| `0x11EFD470` | `mono_aot_get_unbox_trampoline` |
| `0x11F2DF08` | `d:\git\mono_develop\mono\mini\aot-compiler.c` |

扫描未发现 `il2cpp_init`、`GameAssembly.dll` 或 `global-metadata.dat` 字符串，也没有独立 GameAssembly 文件。结合嵌入 CLI/CIL，当前证据支持 **UE + USharp/Mono、静态 AOT 组件**。未运行时确认是否所有方法仅走 AOT，不声称排除任何未观察到的执行路径。

主 EXE PE 导出表有 790 项，没有 `mono_get_root_domain`、`mono_assembly_foreach`、`mono_class_from_name`、`mono_class_get_method_from_name`、`mono_compile_method`、`mono_aot_get_method` 等 `mono_*` 导出。唯一名称含 `mono` 的项为 `png_set_invert_mono`。因此不能直接以 `GetProcAddress(mainModule, "mono_...")` 作为已验证的接入方式。

REA 对 `b1.Native.dll` 的静态检查完整：`.NETFramework,Version=v4.7.2`、AnyCPU CIL、611 个 TypeDef、7,021 个 MethodDef，证据父 ID `ev_fbeb445e830b447b34de01504f3a1836028b4321353cad4a6e32602688958d97`。REA 对大型 `BtlSvr.Main.dll` 因表示预算返回 partial，父 ID `ev_f31f57968c8c73434ac28c26d118d34a67bd0e9460d86f0f6c33ae04c3eba795`；它没有返回完整类型集合。

后续使用 `System.Reflection.Metadata` 从文件/内存流读取 CLI 表，并用 ILSpy CLI `9.1.0.7988` 静态反编译明确指定的类型；未加载或执行游戏程序集。缺少依赖时 ILSpy 显示 unresolved type 提示，因此常量语义以原始 metadata enum 为准。

## 属性访问链路

在 `BtlSvr.Main.dll` 中确认以下链路：

```text
b1.BUS_AttrComp.GetFloatValue(AttrID)
    -> b1.BUC_AttrContainer.GetFloatValue(EBGUAttrFloat)
    -> BUC_AttrContainer.GetValue<EBGUAttrFloat, float>(FloatAttrs, AttrID)
    -> b1.GSBindList<float>.this[int]
    -> System.Collections.Generic.List<float>.this[int]
```

- `BUC_AttrContainer.FloatAttrs` 是 `GSBindList<float>`。`Init()` 创建该容器并追加 255 个 `0f`。
- `BUC_AttrContainer.GetValue` 的方法体返回 `TargetAttr[(int)AttrID]`。
- `GSBindList<T>` 的 `ValueList` 字段是 `List<T>`，getter 返回 `ValueList[Index]`。
- `GSBindList<T>` setter 修改 `ValueList[Index]` 后会触发 `OnSetIdxValue(Index, old, new)`。直接修改底层数组会绕过通知；其效果不能由静态数据布局推断。
- `BUC_AttrContainer.SetFloatValue` 负责 min/max 限制、base/multiplier 计算，以及最大值变化后的当前值夹取。已见公式 `final = base * ((10000 + multiplier) * 0.0001)`。对最终属性直接赋值可能被之后的属性重算覆盖。
- `BUS_AttrComp` 另有私有 `SetFloatValue(AttrID, NewValue)`、`OnIncreaseFloatValue(AttrID, IncreaseValue)` 和延迟增量路径；`SetFloatValue` 方法体先检查 `GetOwner().BGUGetActorHasAuthority()`，再调用属性容器。
- `GetAttributeFloat` 未在主 EXE 明文字符串以及已检查程序集的方法名中找到。参考 AA 的同名 symbol 不能被当作已存在的游戏导出名。

以下坐标只用于当前嵌入程序集的静态复查：

| 声明类型 / 方法 | MethodDef token | 托管 IL RVA |
|---|---|---|
| `BUC_AttrContainer.SetFloatValue` | `0x06008CA1` | `0x222F28` |
| `BUC_AttrContainer.GetFloatValue` | `0x06008CA2` | `0x2230BA` |
| `BUC_AttrContainer.GetValue` | `0x06008CA7` | `0x22319B` |
| `BUC_AttrContainer.SetFloatValueDelayTick` | `0x06008CAC` | `0x223210` |
| `GSBindList<T>.get_Item` | `0x0600C3F1` | `0x2F482F` |
| `GSBindList<T>.set_Item` | `0x0600C3F2` | `0x2F4840` |

**这些 IL RVA 不是 EXE 原生机器码 RVA。** `float[]` 的 `+0x20` 等运行时对象布局需要以本构建的 Mono 和目标对象证据验证，本文未据此提供内存写入实现。

## 已核对的功能语义路径

`b1.BGWConsoleCommands` 含下列实现，可用作寻找游戏原有状态切换路径的依据。命令注册处受 `HasGMFlag(2)` 条件控制，尚未证明零售运行时会注册或允许执行。

| 功能 | 实际观察到的方法体 / 数据 |
|---|---|
| 免伤 | `OnGSLockHP` 调用 `BGUFunctionLibraryCS.BGUSetUnitSimpleState(actor, EBGUSimpleState.ImmueDamage, false)`；解锁对应移除状态 |
| 无限法力 | `OnGSLockMP` 切换 `EBGUSimpleState.LockMP` |
| 无限气力 | `OnGSLockStamina` 切换 `EBGUSimpleState.StaminaLock` |
| 无限棍势候选路径 | `OnGSLockPE` 切换 `EBGUSimpleState.PELock`；具体与参考修改器 PE/Pevalue/Pelevel 的对应需动态比较 |
| 变身能量 | `OnGSLockTransEnergy` 切换 `EBGUSimpleState.TransEnergyLock` |
| 法宝元气 | `OnGSLockFaBaoEnergy` 切换 `EBGUSimpleState.FaBaoEnergyLock` |
| 化身技元气 | `OnGSLockVigorEnergy` 切换 `EBGUSimpleState.VigorEnergyLock` |
| 技能冷却 | `OnGSCancelSkillCD` 切换 `EBGUSimpleState.CancelSkillCD`；各类法术/变身是否共用仍需分别验证 |
| 全局游戏速度 | `OnGSChangeGlobalSpd` 调用 `UGameplayStatics.SetGlobalTimeDilation(player, value)` |
| 玩家速度 | `OnGSChangeCharacterSpd` 调用 `BUS_EventCollectionCS.Get(actor).Evt_SetGMCustomTimeDilation.Invoke(value)` |
| 生命上限 | `OnGSSetMaxHP` 通过 `Evt_SetAttrFloat` 写 `HpMaxBase(101)` 和 `Hp(151)` |
| 葫芦次数上限 | `OnGSSetBloodBottle` 通过 `Evt_SetAttrFloat` 写 `BloodBottomNumMaxBase(136)` |
| 属性修改 | `BUC_AttrContainer.SetFloatValue`、`BUS_AttrComp.SetFloatValue`，属性 ID 见下表 |

还确认这些元数据成员存在，但尚未完成具体副作用与调用前提分析：

- `b1.RoleDataHelper.GetBagItemNum`、`FindBagItem`、`GetBagMoneyNum`、`PlayerInfoRoleAddItem`。最后一项名称本身不证明修改真实背包，不能凭名称调用。
- `b1.BPC_PlayerRoleData.get_RoleData()`、`get_RoleData_Client()`、`get_AttrFloatList()`。
- `b1.BPC_PlayerAttrData.PlayerPersistentAttr`、`PlayerMagicSkillCd`、`PlayerLifeSavingCoolDownRemainTime`。
- `b1.BUC_BuffData.AddBuff`、`RemoveBuff`、`bCanAddBuff`、`bShouldUpdateBuffTime`。
- `b1.BUS_StaminaComp`、`BUS_TransEnergyComp`、`BUS_SpeedCtrlComp`、`BPS_PlayerAttrSystem`。
- 灵蕴/经验/掉落属性分别存在 `SpiritDropAddition`、`ExpDropAddition`、`CommDropAddition`；倍率的单位、叠加规则与触发时间仍需核对相关计算方法。
- 跳跃高度、编辑选中物品数量、增加经验、编辑灵光点/根器点和独立伤害倍率尚无已验证的可调用入口。不能把 UI 控件存在当作功能实现完成。

## 版本适配与验收建议

1. 固定上述 EXE SHA-256 与程序集 MVID；新构建必须重新核对类型、enum 和原生 AOT 映射。
2. 先将参考 AA 模式定位到当前 EXE 的唯一匹配，再核实命中处实际调用约定、原始指令、玩家/非玩家区分；失败时禁用对应功能。
3. 读取属性的验证至少覆盖玩家当前值、上限、非玩家对象以及切图/变身后的对象替换。不要长期缓存未验证的托管裸指针。
4. 属性写入还要验证通知、重算和恢复行为。菜单关闭不等于停止修改；功能关闭需恢复自己的状态且避免覆盖游戏之后的正常属性变化。
5. 可用时优先复用游戏现有的属性事件/状态机制，但必须先证明从 ASI 到这些机制的接入成立；当前尚未建立 Mono API 或 UE 反射入口。
6. 修改货币、经验、背包、灵光点之前备份当前存档，再使用可回读的具体预期值验证。未完成游戏内验证的条目保持未验证状态。

## `BtlShare.EBGUAttrFloat` 常量表

下表直接读取 `GSE.ProtobufDB.dll` TypeDef 的 Constant 表。底层类型为 `System.Byte`，数值属于当前构建，不是内存偏移。

| 名称 | 十进制 | 十六进制 |
|---|---:|---:|
| None | 0 | 0x00 |
| HpMax | 1 | 0x01 |
| MpMax | 2 | 0x02 |
| B1StunMax | 6 | 0x06 |
| StaminaDepletedLimit | 7 | 0x07 |
| StaminaMax | 8 | 0x08 |
| SkillSuperArmorMax | 10 | 0x0A |
| TransEnergyMax | 11 | 0x0B |
| EnergyMinConsume | 12 | 0x0C |
| EnergyConsumeSpeed | 13 | 0x0D |
| EnergyIncreaseSpeed | 14 | 0x0E |
| SpecialEnergyMax | 15 | 0x0F |
| FabaoEnergyMax | 16 | 0x10 |
| VigorEnergyMax | 17 | 0x11 |
| BlockCollapseArmorMax | 21 | 0x15 |
| FreezeAbnormalAccMax | 23 | 0x17 |
| BurnAbnormalAccMax | 24 | 0x18 |
| PoisonAbnormalAccMax | 25 | 0x19 |
| ThunderAbnormalAccMax | 26 | 0x1A |
| BlindSlotMax | 27 | 0x1B |
| BloodBottomNumMax | 36 | 0x24 |
| PelevelMax | 37 | 0x25 |
| ShieldMax | 38 | 0x26 |
| PevalueMax | 39 | 0x27 |
| YinAbnormalAccMax | 40 | 0x28 |
| YangAbnormalAccMax | 41 | 0x29 |
| HpMaxMul | 51 | 0x33 |
| MpMaxMul | 52 | 0x34 |
| AtkMul | 53 | 0x35 |
| DefMul | 54 | 0x36 |
| B1StunMaxMul | 56 | 0x38 |
| StaminaDepletedLimitMul | 57 | 0x39 |
| StaminaMaxMul | 58 | 0x3A |
| StaminaRecoverMul | 59 | 0x3B |
| KptturnSpeedMul | 65 | 0x41 |
| FreezeAbnormalAccMaxMul | 73 | 0x49 |
| BurnAbnormalAccMaxMul | 74 | 0x4A |
| PoisonAbnormalAccMaxMul | 75 | 0x4B |
| ThunderAbnormalAccMaxMul | 76 | 0x4C |
| YinAbnormalAccMaxMul | 77 | 0x4D |
| YangAbnormalAccMaxMul | 78 | 0x4E |
| StaminaCostMultiperMul | 79 | 0x4F |
| TransEnergyMaxMul | 81 | 0x51 |
| EnergyMinConsumeMul | 82 | 0x52 |
| EnergyConsumeSpeedMul | 83 | 0x53 |
| EnergyIncreaseSpeedMul | 84 | 0x54 |
| HpMaxBase | 101 | 0x65 |
| MpMaxBase | 102 | 0x66 |
| AtkBase | 103 | 0x67 |
| DefBase | 104 | 0x68 |
| B1StunMaxBase | 106 | 0x6A |
| StaminaDepletedLimitBase | 107 | 0x6B |
| StaminaMaxBase | 108 | 0x6C |
| StaminaRecoverBase | 109 | 0x6D |
| SkillSuperArmorMaxBase | 110 | 0x6E |
| CritRateBase | 111 | 0x6F |
| CritMultiplierBase | 112 | 0x70 |
| TenacityBase | 114 | 0x72 |
| KptturnSpeedBase | 115 | 0x73 |
| EarPlugBase | 116 | 0x74 |
| CritRateDefBase | 117 | 0x75 |
| CritDmgMulDefBase | 118 | 0x76 |
| DmgAdditionBase | 119 | 0x77 |
| DmgDefBase | 120 | 0x78 |
| BlockCollapseArmorMaxBase | 121 | 0x79 |
| DingshenDefAdditionBase | 122 | 0x7A |
| FreezeAbnormalAccMaxBase | 123 | 0x7B |
| BurnAbnormalAccMaxBase | 124 | 0x7C |
| PoisonAbnormalAccMaxBase | 125 | 0x7D |
| ThunderAbnormalAccMaxBase | 126 | 0x7E |
| FreezeAtkBase | 128 | 0x80 |
| BurnAtkBase | 129 | 0x81 |
| PoisonAtkBase | 130 | 0x82 |
| ThunderAtkBase | 131 | 0x83 |
| FreezeDefBase | 132 | 0x84 |
| BurnDefBase | 133 | 0x85 |
| PoisonDefBase | 134 | 0x86 |
| ThunderDefBase | 135 | 0x87 |
| BloodBottomNumMaxBase | 136 | 0x88 |
| PelevelMaxBase | 137 | 0x89 |
| ShieldMaxBase | 138 | 0x8A |
| PevalueMaxBase | 139 | 0x8B |
| YinAbnormalAccMaxBase | 140 | 0x8C |
| YangAbnormalAccMaxBase | 141 | 0x8D |
| YinAtkBase | 142 | 0x8E |
| YangAtkBase | 143 | 0x8F |
| YinDefBase | 144 | 0x90 |
| YangDefBase | 145 | 0x91 |
| StaminaCostMultiperBase | 146 | 0x92 |
| TransEnergyMaxBase | 147 | 0x93 |
| EnergyMinConsumeBase | 148 | 0x94 |
| EnergyConsumeSpeedBase | 149 | 0x95 |
| EnergyIncreaseSpeedBase | 150 | 0x96 |
| Hp | 151 | 0x97 |
| Mp | 152 | 0x98 |
| Atk | 153 | 0x99 |
| Def | 154 | 0x9A |
| B1Stun | 156 | 0x9C |
| Stamina | 158 | 0x9E |
| StaminaRecover | 159 | 0x9F |
| SkillSuperArmor | 160 | 0xA0 |
| CritRate | 161 | 0xA1 |
| CritMultiplier | 162 | 0xA2 |
| Tenacity | 164 | 0xA4 |
| KptturnSpeed | 165 | 0xA5 |
| EarPlug | 166 | 0xA6 |
| CritRateDef | 167 | 0xA7 |
| CritDmgMulDef | 168 | 0xA8 |
| DmgAddition | 169 | 0xA9 |
| DmgDef | 170 | 0xAA |
| BlockCollapseArmor | 171 | 0xAB |
| DingshenDefAddition | 172 | 0xAC |
| FreezeAbnormalAcc | 173 | 0xAD |
| BurnAbnormalAcc | 174 | 0xAE |
| PoisonAbnormalAcc | 175 | 0xAF |
| ThunderAbnormalAcc | 176 | 0xB0 |
| BlindSlot | 177 | 0xB1 |
| FreezeAtk | 178 | 0xB2 |
| BurnAtk | 179 | 0xB3 |
| PoisonAtk | 180 | 0xB4 |
| ThunderAtk | 181 | 0xB5 |
| FreezeDef | 182 | 0xB6 |
| BurnDef | 183 | 0xB7 |
| PoisonDef | 184 | 0xB8 |
| ThunderDef | 185 | 0xB9 |
| BloodBottomNum | 186 | 0xBA |
| Pelevel | 187 | 0xBB |
| CurEnergy | 188 | 0xBC |
| SpecialEnergy | 189 | 0xBD |
| Shield | 190 | 0xBE |
| Pevalue | 191 | 0xBF |
| YinAbnormalAcc | 192 | 0xC0 |
| YangAbnormalAcc | 193 | 0xC1 |
| YinAtk | 194 | 0xC2 |
| YangAtk | 195 | 0xC3 |
| YinDef | 196 | 0xC4 |
| YangDef | 197 | 0xC5 |
| ExpDropAddition | 198 | 0xC6 |
| SpiritDropAddition | 199 | 0xC7 |
| StaminaCostMultiper | 200 | 0xC8 |
| FabaoEnergy | 201 | 0xC9 |
| VigorEnergy | 202 | 0xCA |
| CommDropAddition | 203 | 0xCB |
| AttrFloatMax | 204 | 0xCC |
| EnumMax | 254 | 0xFE |
