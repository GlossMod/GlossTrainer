# PBZ 原生适配

适配目标：CL-302256_JKS-17280，Windows x64 Shipping，主程序 SHA-256 `4415933ce33476f14c837abb61a1b60af8b78cd81783896501913a7aac69e423`。

当前源码迁移原生接口、战斗/移动钩子与物品目录地址，并保留精确版本与反射检查。构建、实机结果及边界见 [验证记录](verification.md) 最后一节；历史版本的实测结果仅适用于对应旧版本。

## 分层

- `catalog` 定义功能、类型、用户参数、固定参数、输入范围及进度影响。
- `backend` 管理有界队列、有效期、会话代次、互斥与结果历史。回调锁外执行，同一时间只有一个 Pump；旧会话、过期或撤销 capability 的请求不执行。异常与会话中断标记结果未知，不自动重试。
- `native_adapter` 检查磁盘哈希和入口字节，跟踪引擎、世界、本地玩家和 Pawn。监测线程只检查/安装虚表回调，调用 ProcessEvent 和读回均在当前控制器 ReceiveTick 的游戏线程中执行。
- `native_items.inc` 定义本版本五张物品表、行布局、文本解析和背包发放契约，在游戏线程生成完整目录、解析本地化并回读库存。
- `overlay` 从同一帧 Snapshot 显示就绪原因、实际统计和结果，仅提交用户参数；固定参数由 Backend 添加。当前保留五页导航和亮暗主题，传送页隐藏；Debug 新配置默认关闭并持久化，关闭时隐藏请求历史、实际读回和开发诊断。

无敌及战斗开关通过 `ToggleCatalog` 合并开启/关闭动作；God、免死与目标/NPC 清除功能已从当前目录移除。Snapshot 的 `toggleStates` 每 250 ms 在游戏线程读取；开关不使用最近一次点击推测状态。会话失效清除读回，处理中的开关暂时禁用。

## 调度及生命周期

控制器 ProcessEvent 虚表槽 `+0x260` 只有在原值匹配本版本 Actor ProcessEvent 时才原子替换。回调先执行原函数，再核对当前控制器、ReceiveTick、GGameThreadId。启动时尚无角色会等待；加载/暂停超过两秒无 Tick 会停止接收请求。世界、控制器或 Pawn 变化会清除旧请求，重新验证运行时。

本版本原生功能不依赖 CheatManager，也不创建 CheatManager。旧 GM 函数即使反射签名仍存在，其业务实现已经成为空操作；Contract() 拒绝全部历史 GM 契约，能力发布和执行均显示明确的不可用原因。历史契约表仅保留作审计对照。

模块在发布回调地址前固定至进程退出。停用恢复仍归本模块所有的虚表槽，等待监测线程/回调；不能据此调用 FreeLibrary。开发调试副本使用 GameplayOnly 入口，正式版本由 Loader 调用 InitializeASI，一次性安装原生层和输入/图形层。

## 契约及读回

旧 GM exec 编译表不参与本版本执行；物品发放使用独立的背包函数契约。参数数量、偏移、类型、尺寸、flags、native RVA 在调用前重新检查，FName 参数只能引用本进程已有名称。比较名称按 Unreal 大小写不敏感语义处理（MedicineId 可读成 MedicineID）。不把 FName ID 当成跨进程稳定物品 ID。

生命从 BasicAttributeSet.Health/Health_Max 当前值读取；GM_GetHealth 是 void 日志函数，不被误作 float 返回。`Native_SetMaxHealth` 使用引擎构造的 GameplayAttribute 描述符设置 Health_Max base，再读取实际上限并设置 Health；两次调用后都检查 pawn/ASC/AttributeSet 指针与 UObject index。描述符按原生 GM 实现用引擎分配器释放，生命及上限同时匹配才标 Verified。恢复生命使用同一原生属性设置器并读回当前值及上限。无敌通过已验证的 Add/RemoveLooseGameplayTag 添加一个标签贡献；重复开启不会重复计数，关闭只撤销本修改器贡献并保留游戏原有计数。会话切换、停用或异常时保留尚未确认的清理记录；SEH 后停止引擎调用。状态通过 AbilitySystemComponent.GetGameplayTagCount 回读，标签存在不替代实战验证。

时间倍率用 GameplayStatics.Set/GetGlobalTimeDilation，限制 0.1–3.0，设为 1 恢复；坐标用 K2_GetActorLocation 返回 24 字节 double Vector。背包从当前 WorldSubsystem 稀疏映射核对类和 Outer 后读取，补充真实终结技资源及当前药品名称/本次 FName ID。

GM_InfiniteUltimateShachi 的旧版实测修改背包 CurrentUltimateCharge、MaxUltimateCharge 至 2147083648，充能 100%；不能称为普通补满或可逆开关。本版本此命令与其他依赖旧 GM 的批量进度入口均已禁用，不再报告空调用成功。

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

目录建立时检查表身份、行类型、尺寸、分配位及预期槽位数，保留当前会话的真实 FName 和定义行。每种行类型的图标和描述布局复用一次反射结果。名称通过引擎本地化入口在游戏线程按最多 32 行／约 2 ms 预算分批解析，详细说明随后按最多 8 字段／约 1 ms 预算补齐；单次引擎调用不能被预算抢占。未解析出有效显示名的条目默认隐藏。缓存及已解析名称可以预览，目录和库存验证后才开放获取。换玩家、世界、背包或表身份变化会使目录失效并重建，渲染线程只消费 Snapshot。

物品页支持分类、名称搜索、每页 20 项分页；数量确认默认 1。普通道具调用 AddPropsToBackPack，单次请求限制 1–999。意识尘埃使用 AddDustNum，移除单次 999 的人为限制，允许自由输入正整数；游戏参数及库存仍使用 int32，当前持有量加请求数量不能超过 2,147,483,647，溢出时拒绝执行。药品、饰品、主武器、煞兵按唯一类处理，单次上限 1，已拥有时拒绝重复发放，分别使用已核对的 PushMedicineToInventory、PushAccessoryToInventory、AddWeaponItem 契约。普通道具的 999 是适配器请求上限，不是对所有游戏道具堆叠上限的结论。发放不自动装备，不启用读档旁路或拾取 UI 副作用。

Backend 校验目录键、整数数量及分类上限；原生层再次校验本地玩家会话、背包归属、定义行与函数布局。一次请求最多调用一次，不自动重试。先读取库存，再调用，再核对同一会话中的库存：只有 `after - before == requestedQuantity` 才返回 Verified；零增量为 Failed，增量不一致、调用异常、会话变化或无法回读为结果不确定。排队、Dispatched 或原物品仍存在均不触发成功提示。这一条件仍需新版实机验证，包括未拥有物品的首次发放及保存/重载结果。

## 动态图片、富文本与战斗（2026-10-29 更新）

`native_items.inc` 在游戏线程使用游戏的 LoadAsset_Blocking 加载物品软纹理引用，延迟至少一帧再读取已核对的 TexturePlatformData/Mip/BulkData。目录建立和输入对象发现按时间/条目预算分批；每次最多处理一个图片请求。具有有效弱身份的稳定 Texture2D 会跨等待复用，只有对象、路径、会话或目录代次变化才重新加载；无 serial 的对象保留完整重验证路径。图片桥按会话 epoch 丢弃过期结果；`item_icons.cpp` 在工作线程解码 BC1/2/3/7、RGBA/BGRA，缩小至最长边 256，再每帧最多上传一个 GPU 纹理。最多 64 个 GPU 图片；DX11/DX12 的淘汰分别等待事件查询/实际完成的 fence。解码器许可证为仓库 `licenses/bcdec.txt`。不写图集、不打包游戏图片。

`native_item_input.inc` 按路径找到游戏 DT_RichTextInputAction，校验 RichTextInputActionMapping 结构，用其中软 InputAction 的包名和对象名匹配当前 EnhancedActionMappings。CommonInputSubsystem 的 CurrentInputType 与 GamepadInputType 决定当前设备和按键名称；250ms 检查一次。无设备覆盖的实际武器五种动作均已只读核对。保留原始描述，映射变化后重新格式化；`item_richtext.cpp` 保留相邻重复按键、样式内文字、换行与未解析标签占位。带特殊覆盖的动作暂不猜测默认映射。动态数值标签尚未完整适配，会明确标注待读取。

`native_combat.inc` 的无限杀气使用 BasicAttributeSet 的 PreAttributeChange/PreAttributeBaseChange 两个 vtable 入口。在原逻辑返回后仅调整即将写入的 float 参数；要求游戏线程、当前本地 pawn/属性对象/索引、Shachi FProperty 全匹配。保留当前值，允许自然恢复，遵守当前上限，不改 Min/Max；关闭只停止干预。槽位恢复采用拥有者比较，DLL 继续固定到进程退出。

完美格挡使用游戏 GE_PerfectDeflect 实际授予的 CharacterState.EditorDebug.Deflect.AlwaysPerfect 标签；通过 PBZAbilityBlueprintLibrary 添加一份 loose tag 并核对计数增加，关闭仅撤销自己的贡献。保留正常可格挡动作条件。自动完美闪避使用 DeflectComponent.GetNearestValidReceivedFrame 选出的真实可闪避来袭帧，并复用 BP_PerfectDodgePassive 的 0.04 秒触发门槛，调用 AbilityInputComponent 的正式动作激活接口，保留目标、时序、动作优先级等检查；同一 handle/命中时间只尝试一次。新战斗实现仍需实机 on/off、真实消耗/来袭、换图验证。

## 当前未完成（旧阶段记录，最新范围见验证记录）

无限体力、金币、伤害/防御/姿态倍率、移速和无冷却缺少可靠的玩家限定、玩法效果和恢复契约，当前已隐藏，候选证据见 [未适配项审查](../../output/pbz-items-20261029/unsupported/findings.md)。God 模式及其不可达原生执行分支已删除，传送页隐藏，地图目录及跨图业务边界没有因 UI 隐藏而变成已验证。

本轮已完成最终源码 Debug / Release 构建及 Gameplay 测试，均为 0 failures；最终包及其哈希见 [打包回执](../../output/pbz-items-20261029/package/receipt.json)。用户退出游戏后已备份旧 ASI、部署并核对新版哈希，见 [部署回执](../../output/pbz-items-20261029/deploy-144404/receipt.json)。790 项显示名解析、分页/搜索、各类首次发放、数量增量、Debug 持久化和存档重载均待实机验证。既有材料、升级、天赋、图鉴、NPC 等功能也未完成全部业务及保存边界验证，不能称为“全部功能完成”。

原始证据、测试及部署结果见 [验证记录](verification.md)。

## v1.1.0 功能扩展

新增功能按运行时责任拆为 `native_movement.inc`、`native_damage.inc`、`native_resources.inc`、`native_shachi_cost.inc`、`native_cooldown.inc`、`native_stealth.inc`、`native_progress.inc` 和 `native_extended.inc`。均沿用精确主程序 SHA-256、反射参数布局、本地对象身份、游戏线程与回读校验；源码中存在功能不代表全部游戏场景已验证。

- 移动、跳跃、重力、FOV 保存启用前值；玩家时间倍率仅在已成功启用、同一对象且回读正常时维持。AI 只选取 AIController 双向归属的 Pawn。对象切换时恢复仍存活的旧对象，恢复失败保留记录，不把旧值写入新对象。
- 飞行使用游戏移动模式与碰撞接口，保留原移动模式、自定义模式、飞行速度及碰撞开关。关闭前以原胶囊尺寸和碰撞配置执行空间检查；阻挡时拒绝关闭并保留飞行。升降调用游戏位移接口并核对坐标变化。FArrayProperty.Inner 已按当前构建核对为 `+0x78`。
- 伤害与破防在六类伤害计算器的临时属性差值中处理，仅覆盖本地玩家攻击或受到的结算，不修改持久战斗属性；治疗、自伤和 NPC 对 NPC 不参与。生命保护不抬高上限；纯杀气只保护段数，保留满充能转为段数的正常过程。
- 杀气消耗倍率限定重击最终消耗 getter 和煞兵蓝图初始/持续费用的临时输出。通过本地能力列表、ActorInfo 和属性白名单确认身份，不缩放受击削杀气、不改能力原始字段。原生风雷转换路径未覆盖。
- 药瓶、道具和商店库存 hook 仅修改已验证本地子系统的扣减参数；冷却 hook 只覆盖本地武器合法槽位的查询结果。钩子在初始化通过版本与模块固定校验后预先创建，开关开启时才启用，避免场景加载后邻近地址空间被 PSO 缓存映射占满导致 `MH_ERROR_MEMORY_ALLOC`。
- 六十六天难度解锁调用 `PBZGameSystem.MarkSelectNightMare`，其实际逻辑写入高级存档 JSON 的 `bCanSelectNightMare` 并标记 dirty；以 `CanSelectNightMare` 回读，不改当前难度枚举。剩余天数写入玩家 `RemainingDays`，两项都是不自动重放的一次性进度操作。

设置升级为兼容旧格式的 v2，按功能保存最后成功的倍率值；失败请求不覆盖上次有效参数，关闭不依赖编辑框中尚未提交的内容。飞行升降默认 Page Up / Page Down，保留用户改键、显式解绑与已有冲突。新增中英文状态、亮暗四列界面；界面宿主验证与游戏内验证分别记录。

“忽略升级条件”在两轮实机测试中仍需材料，按用户反馈从 v1.1.0 撤下。已移除功能目录、专用原生钩子及其启动和清理路径；旧设置中的该功能 ID 不再识别。失败调查仅保留在验证记录和根 output 中。

## v1.2.0 脚本功能与相机、饰品

`native_scripts.inc` 只在已核对的本地游戏线程同步执行固定私有模块。每个 Puerts 环境先执行最小 ACK 探针，核对实例索引、环境、loader/logger 和本机构造/加载/验证入口契约；六个临时 loader/logger 槽每次调用后恢复。新版模块完整性验证入口只准入当前调用精确私有路径及 Load 返回的同一描述符、缓冲区、长度和字节内容，其他请求调用原验证函数。

自动收集、高亮和传送共享 250ms 维护。成功启用脚本组后持有属于本模块的窄验证钩子，稳定维护不再反复执行 MinHook Enable/Disable；空闲 TLS 门关闭，普通模块仍走原验证。最后一个开关关闭、探针、清理、失败或环境失配会恢复验证入口；恢复失败保留所有权记录并停止适配器。完整入口字节或本模块补丁收据始终重新核对，不覆盖第三方改动。首次开启和末次关闭仍可能产生一次性的钩子管理开销。

附近交互仅来自本地 InteractComponent，支持原生 Pickup、精确宝箱类和精确 BP_Rubbish 场景掉落类。后者要求 `Not Interact` 明确为 false、场景键有效；218 个 BookPage 后缀用于纸条标记，其余有效场景掉落标记为 Loot，不把场景键当作背包物品 ID。NPC、梯子及其他开关不在白名单。每次维护最多轮转检查 128 个稀疏槽、分类 32 个新对象，并优先检查游戏当前交互目标；最多保留 32 个候选，每轮重新验证成员关系、对象、类别和归属。4096 槽极端密集表完整轮转最长约 32 秒，当前交互目标不等待轮转。自动模式忙碌、空中或战斗时提前返回；两项同时开启共享一次扫描。

交互范围使用原生 `GetValidDistance(actor, pawn)`，不以 DistanceA/B/C 搜索层级或 Actor 原点距离推断可交互范围；高亮和自动收集分别使用用户保存的米制距离（默认 50 米和 10 米）。只对原生 Pickup 调用 `IsValidPickup`。交互前保留正常状态和 CanTriggerInteract，最后一个 Blueprint predicate 返回后再次核对本地 pawn/component/owner、actor 身份、类别、完整路径和重新读取的当前目标/候选表，每轮最多尝试一次。DoInteract 非 true 结果间隔 1.5 秒重试；true 仅记录受理并防重放，不视为背包变化或拾取完成，因为游戏的失败与长按分支也可能返回 true。受理记录只在同一查询代次确认两套来源都已完整访问后清理，范围刷新或稀疏表变化会使旧完整性证明失效。关闭、会话变化和清理释放候选包装引用，跨轮持有的 Puerts 包装对象对 GC 的具体影响尚未实机证明。现场证据和返回值语义见[拾取审查](../../output/pbz-features-20261104/performance/pickup-audit.md)。

高亮经 ProjectWorldToScreen 返回最多 32 个屏幕标记，严格 JSON 校验后发布，750ms 未更新即过期。渲染线程只读取标记，不访问引擎对象。免费传送沿用原始传送业务并跳过道具数量/扣减；限制开关与免费开关独立，目标仍需有效且已激活。游戏禁止字符串动态编译，因此传送方法使用经原方法源码核对的静态函数。

`native_camera.inc` 保存实际相机模式的距离、垂直偏移原值，关闭或会话切换时恢复；模式自身范围可能比 UI 目录范围更窄。`native_accessories.inc` 按已拥有未装备饰品调用本地 GE 辅助函数，以精确 handle 记录和撤销本模块贡献，不修改装备槽；次级效果与所有饰品组合尚未取得完整实机 on/off 证明。数量编辑、收藏品名称与分类见 [收藏品说明](collectible-names.md)。本版验证范围、性能口径和安装产物以 [验证记录](verification.md) 为准。
