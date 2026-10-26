# 多语言实现跟进审计

日期：2026-10-25。只读核对源码、词典、测试源码及已有验证记录；本次未运行构建、自动测试或游戏。以下“当前发现”是在补充修复前读取到的状态；实施代理随后可能更新词典或测试，最终结果需由主代理重新检查并记录。

主代理复验：下列漏项已补齐，同时补充移动适配头文件、技能配置头文件和恢复原因组合文案；当前为 1897 个 key、16 个模板。Debug / Release 两项语言测试均通过，详见[后续验证](../../output/localization-followup-validation-20261025/results.json)与[验证说明](verification.md#中英文后续覆盖复验2026-10-25)。以下保留修复前证据。

## 当前发现：偏好与自动检测

- `localization_model.hpp` 的新配置默认值是 `FollowGame`，序列化为 `Language=auto`；`zh-CN` 和 `en` 分别固定中文和英文。损坏或未知值回到跟随游戏。
- `SetPreference` 先写临时文件、刷新并原子替换 `DataDirectory()/language.ini`，仅在成功后更新偏好和原子语言值。`DataDirectory()` 位于 ASI 文件同目录的 `GlossTrainer` 文件夹。设置页每帧读取偏好，成功选项会即时改变 UI。
- 启动时先以 Windows 用户界面语言作为临时回退，再读取 `LOCALAPPDATA/b1/Saved/Config` 中的 `GameUserSettings.ini` / `Engine.ini`（Windows、WindowsNoEditor 平台）；配置优先于系统回退。游戏运行后由游戏线程探测 `GetCurrentLanguage`，失败再探测 `GetCurrentCulture`，每 5 秒限频；运行时结果优先于配置。固定语言覆盖后续探测。
- `extensions.cpp::Frame` 调用游戏语言探测。以上均是源码路径核对；REA/托管程序集静态证据不能证明当前游戏里反射调用成功。

## 当前发现：界面与翻译覆盖

- 应用自身 UI 文案通过 `localization::Text`、`Translate` 或 `overlay.cpp::UiLabel` 输出。`UiLabel` 用语言和源文本做缓存键；语言切换时使用相应语言的新键。角色/技能/状态、物品、传送和设置页均使用这些路径。
- 用户填写的技能名、书签名等自定义文本原样显示；未收录的运行时角色名保留原文。文档还明确 428 条唯一物品长描述不做英语翻译。
- 对 `localization_strings.inc` 与 `localization_catalog.inc` 的词典行做唯一 key 计数，结果为 1,856；`localization_patterns.inc` 有 10 个动态模板。与 `overlay.cpp`、`features.cpp`、`transformation_hotkeys.cpp`、`extensions.cpp`、`game_language.cpp`、`localization.cpp` 的中文字符串字面量逐项比对时，前述六个源文件未发现不在词典或模板中的中文源字面量。
- **发现变身状态/原因翻译遗漏：**`overlay.cpp` 将 `transformation.cpp` 提供的 `status.message`、`catalogMessage`、`entry.reason`、技能 reason 和快捷键提示传给 `Translate`，但当前词典没有下列 26 个源码 occurrence（25 个不同原文）。因此英文界面的部分变身目录诊断、拒绝原因和恢复状态仍会显示中文。

### 固定字符串遗漏

| `transformation.cpp` 行 | 未映射源文本 |
| ---: | --- |
| 211 | `无法校验角色专用移动状态` |
| 212 | `该形态使用车载、飞行控制、物理或筋斗云专用驱动` |
| 218、224 | `无法读取角色输入限制状态`（两处） |
| 233 | `无法读取角色隔离配置基线` |
| 251 | `阶段图路径或协同战斗标签非空，或无法校验` |
| 265 | `唤醒配置包含关卡角色关联，或无法校验` |
| 483 | `无法读取角色完整移动组件配置` |
| 654 | `生成角色的移动配置无法校验` |
| 664 | `生成角色没有可验证的实际技能集合` |
| 725 | `临时附属角色清理尚未完成，正在恢复` |
| 738 | `角色移动组件在运行中失效` |
| 762 | `角色移动输入或视角无效` |
| 812 | `正在隔离临时角色；保留悟空保护状态` |
| 816 | `正在隔离临时附属角色；保留悟空保护状态` |
| 865 | `临时附属角色清理未完成；正在重试` |
| 866 | `正在清理新出现的临时附属角色` |
| 868 | `正在清理角色销毁时出现的附属角色` |
| 869 | `临时配置清理失败；正在重试` |

### 动态模板遗漏

这些源码使用 `std::format` 的 `{}`；运行结果会把字段名插入消息。模板词典需采用本地化解析器支持的编号占位符 `{0}`：

| `transformation.cpp` 行 | 动态源文本 |
| ---: | --- |
| 237 | `角色依赖字段 {} 无法校验` |
| 238 | `形态依赖 {}；需要专用阶段或场景驱动` |
| 243 | `角色控制字段 {} 无法校验` |
| 244 | `形态依赖 {}；缺少专用控制或关卡隔离` |
| 259 | `角色依赖 {} 无法与隔离基线核对` |
| 260 | `形态包含 {} 依赖或未知序列格式；需要专用适配` |

另有 `transformation.cpp:497` 会把 `out.adapter.reason` 与 `；独立代理/手动技能实验模式` 拼成一个 reason。只给整个结果添加捕获 `{0}` 的模板会把捕获的中文 base reason 原样保留，故应分段翻译后组合，或提供能覆盖全部已知组合的完整键。

## 现有检查与记录

- `localization_tests.cpp::TestApplicationTranslationKeyCoverage` 目前只检查 `overlay.cpp` 和 `features.cpp`；它没有检查变身后端生成的状态/原因字符串，所以未覆盖上述遗漏。补充测试宜把 `transformation.cpp` 纳入字符串键覆盖，并正确处理编号动态模板与格式化后缀。
- 仓库已有记录声称 Debug、Release 构建和 15 组自动测试各通过，且完成 8 个语言/亮暗设置页宿主场景及英文功能/变身页宿主截图；证据文件位于 `output/localization-validation-20261025/`，汇总在 `BlackMythWukong/docs/verification.md`。本审计只核对了文档、测试源码与产物文件存在性，没有重新执行这些验证。
- 游戏语言调用、游戏菜单内语言变化、真实目录名称和游戏行为仍需玩家实际游戏验证；已有宿主截图或 mock 结果不构成游戏内验证。

