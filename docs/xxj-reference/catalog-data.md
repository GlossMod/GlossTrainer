# XiaoXing 静态 catalog 数据来源

该说明对应 `BlackMythWukong/src/gameplay/catalog_data.inc`。catalog 由本地样本数据离线生成；未执行 trainer、游戏或安装程序，也未将样本代码纳入产品源代码。

| 输入 | SHA-256 | 用途 |
|---|---|---|
| `data/xxj/Black.Myth.Wukong.Trainer.V1.7.0-XiaoXing.zip` | `62AAAFBFB47CAC64BF13ED287B71FA0AB5977E15E709E98B3803792E133FDF6B` | 原始 ZIP 样本 |
| ZIP 内 `b1Wukong.ini` | `5D6701A93C5FE602E9CA0F8F21641DD54961ED72778A33922EB8CD7C1DBC7196` | 原始物品、场景、坐标条目 |
| 当前解压 `data/xxj/Black.Myth.Wukong.Trainer.V1.7.0-XiaoXing/b1Wukong.ini` | `663379B56C0AD275D7FF6267B3D5D6813C40104DC58ADAFCA176F744A64F4910` | catalog 实际解析输入 |
| ZIP 内 `黑神话悟空文本.rar` | `42CB5196AA76D12CDDDDFF7D77538F172780161886AB268F81278A2FBE41400E` | 内嵌文本 RAR |
| `data/xxj/analysis/embedded_text.bin` | `61AE29B2E72E074AD818176635F8E759E9871DD345AC9E0AAF56BB9242668BC3` | 从文本 RAR 静态提取的本地化数据 |

ZIP 内 INI 为 133,654 字节，当前解压版为 133,656 字节；唯一不同的文本行是 `[config]` 中 `check=` 值（ZIP 为 `8/28`，解压文件为 `10/24`，后者多一个 UTF-16LE 字符）。其余内容与条目相同。catalog 使用当前 INI 的记录，配置日期差异不影响生成的数据。

## 生成与校验

- 物品：1,160 条，1,160 个唯一 ID，17 个分类；保留 INI 列表顺序。名称优先取本地化 `Name`，缺失时回退 INI 标签。描述按 `EffectDesc`、`BriefDesc`、`Desc`、`HudEffectDesc` 顺序取首个非空值；208 个 ID 缺少本地化名称记录并回退到 INI 标签，240 条没有可用描述。
- 场景：初始解析 79 条，ID 唯一；所有非负坐标记录的场景 ID 都能在场景表中找到。2026-10-25 清理后的当前 catalog 数量见下节。
- 地标：源文件可解析 304 条坐标记录；初始版本排除 6 条 `scene=-1` 且坐标全为零的 UI 说明/自定义位置占位记录后，曾输出 298 条。2026-10-25 清理后的当前 catalog 数量见下节。所有保留坐标均为有限数值，没有补造记录。
- 所有物品 ID、场景 ID、坐标均按输入原值生成；UTF-8 C++ 字符串字面量，类别序号来自 INI 的 `list0` 至 `list16`。

| INI 分类 | 物品数 |
|---|---:|
| `特殊` | 39 |
| `用品` | 22 |
| `材料` | 46 |
| `酒食` | 47 |
| `药材` | 60 |
| `物事` | 30 |
| `兵器` | 26 |
| `头冠` | 48 |
| `衣甲` | 58 |
| `臂甲` | 60 |
| `腿甲` | 57 |
| `珍玩` | 40 |
| `法宝` | 4 |
| `精魄` | 329 |
| `葫芦` | 17 |
| `法术` | 74 |
| `影神图` | 203 |

## 2026-10-25 场景与地标清理

上面的 79 个场景、298 个地标是清理前的历史统计，不代表当前数组。当前 [`catalog_data.inc`](../../BlackMythWukong/src/gameplay/catalog_data.inc) 保留 **32 个场景、284 个地标**；其中有地标的场景分成 **17 组**。本次从原始场景表移除 47 条、从原始地标表移除 14 条。计数来自当前静态数组和校验规则，没有改写原始 ZIP/INI 的历史统计。

仓库中的 `tools/inspect_xxj.py` 只检查样本文件、INI、PE 和来源哈希，不生成 C++ catalog；本次过滤因此直接体现在已审阅的 catalog 数据及 `extension_tests.cpp` 完整性断言中，没有把静态检查脚本误称为生成器。

场景过滤按稳定 ID 执行，而非对 `***` 或“测试”字样做全局替换：

- 移除明确的界面/设置/演示/联机或测试场景 ID：`-1, 0, 1, 4–7, 90, 91, 99–113, 1000, 4444, 4445`。对应源标签包括“不在场景中”“开始界面”“derek_test”“设置关卡”“浮屠塔_联机”“黄眉测试”“复活点测试”“联机测试神之猎场”“厕所”“Demo_Battle_scene00”“除错房间”“QA空厕所”“天赋录制环境”等。
- 移除内部测试关卡 ID：`3615–3627` 与 `3630–3636`。原标签包含测试地图、除错房间、厕所、采集刷新、性能监控及内部场景名；`3631`、`3636` 虽是内部代号，源表所在的 `3630–3636` 段也是同一组调试/性能场景。
- 保留章节和隐藏/剧情地图，包括带 `***` 前缀但名称对应实际地图的 ID `9, 21, 22, 23, 51, 60, 97`，以及隐藏地图 ID `14, 26, 27, 32, 33, 52, 63`。因此 `***` 本身不是删除条件；例如 ID `3` 的“***人种袋”也保留。

移除的 14 条地标由两部分组成：被删除场景 ID `103`、`4444`、`4445` 下的 8 条记录，以及保留场景中的 6 条明确测试/占位记录：`(10, “测试拾取”)`、`(10, “测试菜单”)`、`(30, “测试拾取种子”)`、`(62, “触发通关（测试）”)`、`(62, “123”)`、`(52, “此处输入位置描述52”)`。过滤用场景 ID 和精确地标名配对；其他章节中同名的正常地标（如 ID `13` 的“正常位置”）不受影响，也不因名称含“测试”而统一删除。

此处的“开发/测试”判定来自原始 `b1Wukong.ini` 中的场景与地标名称及上述 ID 分组；这是静态来源证据，不代表对每条传送坐标做过游戏内可达性验证。

## 使用约定

`catalog_data.inc` 不声明命名空间、include guard 或额外类型，须在 `ItemEntry`、`SceneEntry`、`LandmarkEntry` 和 `Vec3` 类型可见的实现文件作用域中包含。生成数组分别名为 `itemEntries`、`sceneEntries`、`landmarkEntries`。

## 精魄、法术与影神图的调用证据

以下把可复核的静态事实、当前代码路径和未确认的游戏行为分开记录。数据计数来自上表对应的 INI；命令与反射行为见[游戏 API 映射](game-api-mapping.md)的“GM 派发与物品事务”段落。代码位置以 `BlackMythWukong/src/gameplay/extensions.cpp` 的 `ChangeItem` 为准。

| 问题 | 已确认事实 | 结论边界 |
|---|---|---|
| 精魄能否走 `AddItem` | catalog 中 list 13 有 329 个“精魄”记录；`ChangeItem` 对 list 15、16 分支处理，其余 list（含 list 13）构造 `AddItem <id> <count>`。这 329 个 ID 与 `embedded_text.bin` 中全部 329 个 `SoulSkillDesc.<id>.SkillName` 键精确相同。游戏 `AddItem` 命令调用 `Bag.GainItemOne`；`GainItemByType` 对 ItemType 18 调 `GainSoulSkill`，而 `AllSoulSkill` 遍历 ItemType 18 并按 ID 调 `GainSoulSkill`。 | 静态交叉证据**强烈支持**这些精魄使用 AddItem 的魂技专用分支。仍未直接逐条核对当前游戏 `ItemDesc.ItemType==18`，也未测试每条 ID 的所有权去重、重复获取转化、UI 显示或事务提交。因此不把 329 条标成逐 ID 游戏验证；“已发送到游戏接口”也不等于存档已保存。 |
| 法术添加与删除 | list 15 有 74 条；当前代码对其使用 `AddSpell <id>`，不会调用 `AddItem`。当前 UI/后端对 list 15 删除直接拒绝。静态映射还列出独立 `SetSpell`、`SoulSkill`、`SetSoulSkill` 命令。 | 法术获得/装备是不同操作；本项目当前仅有添加路径，未实现对应单条删除。静态映射没有证明游戏机制绝不允许删除。`AddSpell` 对当前全部 74 个 ID 的实机结果待验证。 |
| 影神图阶段参数 | list 16 有 203 条；当前 UI 在应用前让用户选择故事阶段 0–5 和画像阶段 0–1，`SetCard` 按所选值生成 `Card <id> <storyStage> <portraitStage>`。`CsB1.PlayerGmExecutor.Card` 将参数转换为 `ECollectionStage` / `ECollectionPortraitStage`，调用 `PlayerCollection.GMSetCard`；后者把值写入故事 / 画像状态。`CalCardCollectionStage(CardStory.Count)` 按故事条目数返回 1 到 5 的阶段值（最多5）。 | 两个阶段是显式输入，不是固定 `2 1`，也不能泛称“完整解锁”。故事选择范围为初见及故事一至五，画像为初见或完整；条目实际故事数可能少于所选值。命令还要求 `GetCardDesc` 成功且 `IsVersionCard` 为真。仍未确认具体样本中的全部 ID 在当前游戏版本都可用，也没有实机验证 UI、红点或存档结果。 |
| 精魄、法术与影神图删除 | 当前 `ChangeItem` 对 list 13、15、16 删除直接拒绝；游戏内部有 `PlayerActor.RemoveSpell(SpellId)`、单卡 `RemoveCard(CardId)`（private）、`GMRemoveSoulSkillByUid`（private）等实现线索。`CostItem` 的静态实现路径是背包 `Bag.CostItem`。 | **本项目当前在界面和后端禁用精魄、法术、影神图删除。** 游戏内部存在移除方法的静态证据，但没有当前可用的安全单条调用路径；private 方法不能直接等同于当前可用 GM 命令。还需正确角色事务、SoulSkill UID、红点同步 / 保存语义并经实机验证。不对这些类型使用未经验证的 `CostItem` 或负数量 `AddItem`。 |

反编译证据文件：[`CsB1.PlayerGmExecutor.decompiled.cs`](../../tmp/game-mapping/xxj/CsB1.PlayerGmExecutor.decompiled.cs) 中的 `AddItem`、`AllSoulSkill`、`Card` 参数转换和版本检查；[`CommB1.PlayerBag.decompiled.cs`](../../tmp/game-mapping/xxj/CommB1.PlayerBag.decompiled.cs) 中 `GainItemOne` / ItemType 18 分支、`GainSoulSkill`、魂技移除和背包实现；[`CommB1.PlayerCollection.decompiled.cs`](../../tmp/game-mapping/xxj/CommB1.PlayerCollection.decompiled.cs) 中的 `GMSetCard`、`PushCardStoryStage`、`CalCardCollectionStage`、private `RemoveCard`；[`CommB1.PlayerActor.decompiled.cs`](../../tmp/game-mapping/xxj/CommB1.PlayerActor.decompiled.cs) 中的 `RemoveSpell`。项目分支和命令构造见 [`extensions.cpp`](../../BlackMythWukong/src/gameplay/extensions.cpp)。精魄 ID 重合核验使用上表哈希标注的 UTF-16LE `embedded_text.bin` 静态键名，没有解析或运行样本程序。

### 复核说明

- 以上是对本地生成 catalog、当前 `ChangeItem` 分支和静态 API 映射的交叉核对，不执行参考 trainer，也没有在游戏里修改存档。
- `Card` 的两个阶段值由 UI 显式选择（故事 0–5、人像 0–1），并被直接转换为枚举写入 `StoryStatus.CollectionStage` / `PortraitStatus.PortraitStage`；完整故事阶段按该 `CardDesc.CardStory.Count` 计算，取值不超过5。该静态语义仍不等同于游戏中展示状态的实测。
- `AddItem -> GainItemOne` 是通用入口关联。若要把“精魄可添加”升级为已确认行为，还需要对单个已知精魄 ID 发起游戏内测试并检查背包 / 精魄列表的实际变化；批量 329 条不应自动视为已测。
