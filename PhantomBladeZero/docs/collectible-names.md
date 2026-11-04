# 收藏品名称与分类

`DT_InventoryData` 中的书籍、书页、记忆花和花瓣继续作为 `ItemKind::Prop` 保存、回读和发放。目录分类仅使用现有的 `ItemEntry.category`，由现有物品页动态生成分类，无新增页面或控件。

书页和花瓣的独立行 ID、FName 的 comparison/number、目录 `prop:` 键与库存键均保留。名称回退按完整父 FName 关联，不按前缀、字符串猜测或父项名称合并条目。发放仍由现有 `AddPropsToBackPack` 入口执行；游戏对花与花瓣的专用分流不被替换。此实现不手工写入进度、全局属性或存档。

## 字段与类型契约

仅在本版本 `PBZItemDataStruct` 大小 856 和以下反射字段通过核对后，启用收藏品分类和名称回退：

| 字段 | 类型 | 大小 | 偏移 |
| --- | --- | --- | --- |
| ItemName | FlowLocText | 48 | 0x28 |
| Subhead | FlowLocText | 48 | 0xe8 |
| SequentialIndex | IntProperty | 4 | 0x240 |
| IntegrationItemID | NameProperty | 8 | 0x244 |
| ItemType | EnumProperty / EItemTypes | 1 | 0x24c |

已确认枚举为 `Item=0, Book=1, BookPage=2, Letter=3, MemoryFlower=4, MemoryFlowerPetal=5`。对应目录分类为 `书籍`、`书页`、`记忆花`、`花瓣`。普通 Item、Letter、未知类型或未通过布局验证的条目维持原目录处理。

书页只有在自身 `ItemName.Key` 经合法 FString 读取确为空、父项类型为 Book、父项具有有效名称 Key 时，才使用父书的本地化名称。花瓣同理要求父项为 MemoryFlower。子项名称为“父项名称 · 本地化 Subhead”；合法空副标题使用“第N页”或“第N片花瓣”。SequentialIndex 已存储从 1 开始的序号，不额外加一。无法读取或仍返回本地化 Key 的文本视为解析失败，继续保持不可发放状态。

## 解析与缓存

父名称和子副标题分别在既有名称解析预算内完成，每轮最多开始一次本地化调用，下一次调用前重新检查既有 2ms/32 项预算。已经解析的父名称可以复用。目录按会话与语言变化重建，父项行和子项行在解析前均重新验证。缓存仍按版本 SHA256 与游戏语言区分；当前已验证的分类覆盖旧缓存分类，空名称条目只有通过该次父关联验证后才能接受缓存名称。

## 验证记录

原始只读证据位于 [inventory-name-audit.json](../../output/pbz-features-20261104/collectibles/inventory-name-audit.json)。640 行完整 ID 均唯一；218 张书页和 128 片花瓣均有类型正确、名称 Key 非空的父项，合计 346 条可应用名称回退。其中 73 条子项带 FName number 后缀，必须保留。完整关联核验见 [name-fallback-audit.json](../../output/pbz-features-20261104/collectibles/name-fallback-audit.json) 和 [核验日志](../../output/pbz-features-20261104/collectibles/name-fallback-validation.log)。

游戏原脚本 `recovered-module-14` 的 970–1005 行使用父书名与子副标题，1007–1038 行使用父花名与花瓣副标题；`recovered-module-07` 的 `initBookDisplay` / `initMemoryFlowerDisplay` 使用相同数据关系。原始恢复文件位于上述证据目录。

纯逻辑回归源码为 `validation/collectible_names_tests.cpp`，覆盖分类、布局拒绝、父类型错误、已有自身名称、空父名、本地化失败、页/瓣序号及边界。该子任务未进行构建、引擎调用或游戏内发放。实际名称显示、发放与收藏进度效果需要主代理另行验证；只读关联核验不能替代实机功能验证。

## 唯一收藏品发放约束

`AddPropsToBackPack` 的 exec 为 `0x6619b60`，调用原生主体 `0x697da70`。主体从 `Inventories @0x510` 查找完整 FName；已有项路径 `0x697dd63–0x697dd95` 把 ItemType 1/2/3/4/5 跳转到 `0x697ebc0`，绕过普通道具对 `CurrentAmount @0x380` / `HasMaxAmount @0x384` 的累加。这与脚本按 `Inventories.Get(page/petalID)` 判断已收藏的行为一致。目录回读仍使用同一 Inventories 容器和 CurrentAmount，未从进度 UI 估算库存。

首次创建的库存对象则经 `0x68f09a0` 直接把请求 Num 写入 CurrentAmount 和 HasMaxAmount，并没有在该初始化处把收藏品数量钳到 1。因此本修改器对经过完整 schema 核对的 Book/BookPage/MemoryFlower/MemoryFlowerPetal 设置 `maxQuantity=1`，并在目录可用状态及真正发放前分别检查已有数量，已有则拒绝。普通 Prop 和未验证类型保持原逻辑。仍调用一次原 AddProps，保留游戏正常的父项聚合与副作用；不会自动重试。

主体 `0x697ddda–0x697df62` 对书页/花瓣检查完整父 ID、对应子项列表及每个子项 CurrentAmount，所有子项拥有后递归调用 AddPropsToBackPack 发放父项。这能证明正常父项聚合路径存在，不足以证明手动发放整本书/整朵花会反向补齐子项。

整花另有 `0x697e277` 起的类型 4 特殊分支；结尾类型 4/5 调用 `0x697ca00`。后者明确读取 `DuplicatePetalDustLootID @0x34c`，属于重复花瓣尘埃奖励路径，不能把它当作“集齐花朵增加生命”的证明。本任务没有完整追踪或实测所有整花完成奖励、生命上限与存档链，也没有进行任何收藏品发放；实际生命/奖励效果保持未验证，不对其作保证。原始反汇编见 `../../output/pbz-features-20261104/collectibles/AddProps-exec.asm`、`AddProps-body.asm`、`InventoryInfo-init.asm` 与 `AddProps-collectible-helper.asm`。

补充新 PID 10020 的只读结构核对：ItemType 的 `FEnumProperty+0x78` 是 EItemTypes，`+0x70` 是大小 1 的 ByteProperty。UEnum 在 `+0x40/+0x48` 保存名称和值的两条带标记指针，清除低三位后各以 8 字节步长读取，`+0x50` 数量为 7；本实现精确比较七个枚举名称与 0–6 值。FlowLocText 内部 Namespace/Key/ValueCache 的字段类型、大小及偏移也逐项核对。证据脚本为 `../../output/pbz-features-20261104/collectibles/live_contract_readonly.js`，REA Evidence ID `ev_d00ebb0ea7ba544d049be864d6de5e7b859a8ee596cde4f4e3053f0b5c903a18`。新实例再次确认所有 346 子项的 Subhead.Key 均非空，57 组父子关系及从 1 开始的序列成立；空副标题序号回退目前属于防护路径。
