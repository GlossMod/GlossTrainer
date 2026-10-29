# PBZ 原生适配

适配目标：CL-301524_JKS-17219，UE 5.7.4 Shipping，主程序 SHA-256 `7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4`。

当前源码已实现全量物品目录、数量发放与结果读回，并收敛用户界面；本轮 Debug / Release 构建和 Gameplay 测试通过。**本轮新版已部署，尚未完成实机验证，以下新增物品行为描述的是实现与验证条件。**旧版部署及实机结果保留在 [验证记录](verification.md)。

## 分层

- `catalog` 定义功能、类型、用户参数、固定参数、输入范围及进度影响。
- `backend` 管理有界队列、有效期、会话代次、互斥与结果历史。回调锁外执行，同一时间只有一个 Pump；旧会话、过期或撤销 capability 的请求不执行。异常与会话中断标记结果未知，不自动重试。
- `native_adapter` 检查磁盘哈希和入口字节，跟踪引擎、世界、本地玩家和 Pawn。监测线程只检查/安装虚表回调，构建 CheatManager、调用 ProcessEvent 和读回均在当前控制器 ReceiveTick 的游戏线程中执行。
- `native_items.inc` 定义本版本五张物品表、行布局、文本解析和背包发放契约，在游戏线程生成完整目录、解析本地化并回读库存。
- `overlay` 从同一帧 Snapshot 显示就绪原因、实际统计和结果，仅提交用户参数；固定参数由 Backend 添加。当前保留五页导航和亮暗主题，传送页隐藏；Debug 新配置默认关闭并持久化，关闭时隐藏请求历史、实际读回和开发诊断。

无敌及战斗开关通过 `ToggleCatalog` 合并开启/关闭动作；God、免死与目标/NPC 清除功能已从当前目录移除。Snapshot 的 `toggleStates` 每 250 ms 在游戏线程读取；开关不使用最近一次点击推测状态。会话失效清除读回，处理中的开关暂时禁用。

## 调度及生命周期

控制器 ProcessEvent 虚表槽 `+0x260` 只有在原值匹配本版本 Actor ProcessEvent 时才原子替换。回调先执行原函数，再核对当前控制器、ReceiveTick、GGameThreadId。启动时尚无角色会等待；加载/暂停超过两秒无 Tick 会停止接收请求。世界、控制器或 Pawn 变化会清除旧请求，重新验证运行时。

Shipping 的 EnableCheats 路径为空，适配器通过已核对的 GameplayStatics.SpawnObject 在游戏线程创建控制器指定的 CheatClass；验证 ClassWithin、Outer 和派生类，将引用保存在控制器 CheatManager UPROPERTY。每个控制器上下文仅尝试一次，失败不循环创建。

模块在发布回调地址前固定至进程退出。停用恢复仍归本模块所有的虚表槽，等待监测线程/回调；不能据此调用 FreeLibrary。开发调试副本使用 GameplayOnly 入口，正式版本由 Loader 调用 InitializeASI，一次性安装原生层和输入/图形层。

## 契约及读回

GM exec 编译表沿用已核对的静态与运行时契约，目录只启用其中所选玩法动作；当前物品发放使用独立的背包函数契约。参数数量、偏移、类型、尺寸、flags、native RVA 在调用前重新检查，FName 参数只能引用本进程已有名称。比较名称按 Unreal 大小写不敏感语义处理（MedicineId 可读成 MedicineID）。不把 FName ID 当成跨进程稳定物品 ID。

生命从 BasicAttributeSet.Health/Health_Max 当前值读取；GM_GetHealth 是 void 日志函数，不被误作 float 返回。`Native_SetMaxHealth` 使用引擎构造的 GameplayAttribute 描述符设置 Health_Max base，再读取实际上限并设置 Health；两次调用后都检查 pawn/ASC/AttributeSet 指针与 UObject index。描述符按原生 GM 实现用引擎分配器释放，生命及上限同时匹配才标 Verified。无敌读 AbilitySystemComponent.GetGameplayTagCount；标签存在不替代实战验证。

时间倍率用 GameplayStatics.Set/GetGlobalTimeDilation，限制 0.1–3.0，设为 1 恢复；坐标用 K2_GetActorLocation 返回 24 字节 double Vector。背包从当前 WorldSubsystem 稀疏映射核对类和 Outer 后读取，补充真实终结技资源及当前药品名称/本次 FName ID。

GM_InfiniteUltimateShachi 的旧版实测修改背包 CurrentUltimateCharge、MaxUltimateCharge 至 2147083648，充能 100%；不能称为普通补满或可逆开关。批量进度等尚无专属业务读回的调用只报告 Dispatched，意味着 native 调用返回，不意味着业务成功或存盘成功。

## 完整物品目录与数量发放

本轮既有进程只读捕获来自五张完整定义表的单例映射，不是已拥有背包实例。有效条目共 790 项；煞兵表 28 个槽位中 1 个为空，按稀疏映射分配位过滤后为 27 项。

| 分类 | 定义表 | 有效条目 |
| --- | --- | --- |
| 道具 | `/Game/PBZContent/DataTable/Item/DT_InventoryData` | 640 |
| 药品 | `/Game/PBZContent/DataTable/UI/DT_MedicineTable` | 17 |
| 饰品 | `/Game/PBZContent/DataTable/UI/DT_Accessory` | 73 |
| 主武器 | `/Game/PBZContent/DataTable/UI/DT_WeaponData` | 33 |
| 煞兵 | `/Game/PBZContent/DataTable/UI/DT_ShaqiWeaponData` | 27 |

原始定义 ID、本地化命名空间和键见 [完整定义捕获](../../output/pbz-items-20261029/runtime/raw-item-definitions-pid4880.json)。此前封装资源仍受加密限制，见 [资源侧调查](../../output/pbz-items-20261029/assets/findings.md)；本轮通过运行时定义映射补充目录，不声称已经解密资源包。定义捕获不等于新版原生目录已在游戏中运行通过。

目录建立时检查表身份、行类型、尺寸、分配位及预期槽位数，保留当前会话的真实 FName 和定义行。名称及描述通过引擎本地化入口在游戏线程分批解析，每 Tick 最多处理 1 项；未解析出有效显示名的条目默认隐藏。换玩家、世界、背包或表身份变化会使目录失效并重建，渲染线程只消费 Snapshot。

物品页支持分类、名称搜索、每页 20 项分页；数量确认默认 1。普通道具调用 AddPropsToBackPack，单次请求限制 1–999。药品、饰品、主武器、煞兵按唯一类处理，单次上限 1，已拥有时拒绝重复发放，分别使用已核对的 PushMedicineToInventory、PushAccessoryToInventory、AddWeaponItem 契约。999 是适配器请求上限，不是对所有游戏道具堆叠上限的结论。发放不自动装备，不启用读档旁路或拾取 UI 副作用。

Backend 校验目录键、整数数量及分类上限；原生层再次校验本地玩家会话、背包归属、定义行与函数布局。一次请求最多调用一次，不自动重试。先读取库存，再调用，再核对同一会话中的库存：只有 `after - before == requestedQuantity` 才返回 Verified；零增量为 Failed，增量不一致、调用异常、会话变化或无法回读为结果不确定。排队、Dispatched 或原物品仍存在均不触发成功提示。这一条件仍需新版实机验证，包括未拥有物品的首次发放及保存/重载结果。

## 动态图片、富文本与战斗（2026-10-29 更新）

`native_items.inc` 在游戏线程使用游戏的 LoadAsset_Blocking 加载物品软纹理引用，延迟至少一帧再读取已核对的 TexturePlatformData/Mip/BulkData。每次最多处理一个请求，常规失败下帧才尝试备用纹理。图片桥按会话 epoch 丢弃过期结果；`item_icons.cpp` 在工作线程解码 BC1/2/3/7、RGBA/BGRA，缩小至最长边 256，再每帧最多上传一个 GPU 纹理。最多 64 个 GPU 图片；DX11/DX12 的淘汰分别等待事件查询/实际完成的 fence。解码器许可证为仓库 `licenses/bcdec.txt`。不写图集、不打包游戏图片。

`native_item_input.inc` 按路径找到游戏 DT_RichTextInputAction，校验 RichTextInputActionMapping 结构，用其中软 InputAction 的包名和对象名匹配当前 EnhancedActionMappings。CommonInputSubsystem 的 CurrentInputType 与 GamepadInputType 决定当前设备和按键名称；250ms 检查一次。无设备覆盖的实际武器五种动作均已只读核对。保留原始描述，映射变化后重新格式化；`item_richtext.cpp` 保留相邻重复按键、样式内文字、换行与未解析标签占位。带特殊覆盖的动作暂不猜测默认映射。动态数值标签尚未完整适配，会明确标注待读取。

`native_combat.inc` 的无限杀气使用 BasicAttributeSet 的 PreAttributeChange/PreAttributeBaseChange 两个 vtable 入口。在原逻辑返回后仅调整即将写入的 float 参数；要求游戏线程、当前本地 pawn/属性对象/索引、Shachi FProperty 全匹配。保留当前值，允许自然恢复，遵守当前上限，不改 Min/Max；关闭只停止干预。槽位恢复采用拥有者比较，DLL 继续固定到进程退出。

完美格挡使用游戏 GE_PerfectDeflect 实际授予的 CharacterState.EditorDebug.Deflect.AlwaysPerfect 标签；通过 PBZAbilityBlueprintLibrary 添加一份 loose tag 并核对计数增加，关闭仅撤销自己的贡献。保留正常可格挡动作条件。自动完美闪避使用 DeflectComponent.GetNearestValidReceivedFrame 选出的真实可闪避来袭帧，并复用 BP_PerfectDodgePassive 的 0.04 秒触发门槛，调用 AbilityInputComponent 的正式动作激活接口，保留目标、时序、动作优先级等检查；同一 handle/命中时间只尝试一次。新战斗实现仍需实机 on/off、真实消耗/来袭、换图验证。

## 当前未完成（旧阶段记录，最新范围见验证记录）

无限体力、金币、伤害/防御/姿态倍率、移速和无冷却缺少可靠的玩家限定、玩法效果和恢复契约，当前已隐藏，候选证据见 [未适配项审查](../../output/pbz-items-20261029/unsupported/findings.md)。God 模式及其不可达原生执行分支已删除，传送页隐藏，地图目录及跨图业务边界没有因 UI 隐藏而变成已验证。

本轮已完成最终源码 Debug / Release 构建及 Gameplay 测试，均为 0 failures；最终包及其哈希见 [打包回执](../../output/pbz-items-20261029/package/receipt.json)。用户退出游戏后已备份旧 ASI、部署并核对新版哈希，见 [部署回执](../../output/pbz-items-20261029/deploy-144404/receipt.json)。790 项显示名解析、分页/搜索、各类首次发放、数量增量、Debug 持久化和存档重载均待实机验证。既有材料、升级、天赋、图鉴、NPC 等功能也未完成全部业务及保存边界验证，不能称为“全部功能完成”。

原始证据、测试及部署结果见 [验证记录](verification.md)。
