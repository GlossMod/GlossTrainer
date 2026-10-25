# 界面语言

GlossTrainer 初版提供简体中文和英文。设置项为“跟随游戏 / 简体中文 / English”；新配置默认为“跟随游戏”。选择立即生效，并保存到 ASI 同目录下的 `GlossTrainer/language.ini`：

```ini
[Interface]
Language=auto
```

`auto` 表示跟随已检测到的游戏语言，`zh-CN` 固定为中文，`en` 固定为英文。用户选择的固定语言优先于后续探测。缺失或损坏配置时默认“跟随游戏”；保存新偏好失败时保留之前的选择和当前有效语言。成功写入临时文件并替换配置后才提交新选择。

自动检测按可信来源保留较高优先级：游戏运行时反射结果高于游戏配置，高于 Windows 用户界面语言。启动时读取 `LOCALAPPDATA/b1/Saved/Config` 下 Windows 或 WindowsNoEditor 平台的 `GameUserSettings.ini`、`Engine.ini`；运行后只在游戏线程调用 Unreal 的 `GetCurrentLanguage`，失败时再试 `GetCurrentCulture`，探测间隔为 5 秒。有效 `zh-*` 文化映射为中文；其他有效文化暂回退为英文。无法解析的文化不覆盖现有检测结果。

静态 UI 文案和目录短名称位于 `BlackMythWukong/src/localization_strings.inc` 与 `localization_catalog.inc`。当前这两个词典共有 1,856 个唯一源 key；动态状态文案有 10 个编号模板，模板捕获会保留原值。词典覆盖全部 32 个场景、284 个地标、247 条变身来源角色名，以及 1,159 条中文物品短名；另一个物品 AK-47 保留原名。参考和调试标签采用语义翻译，不宣称全部为游戏官方英语。运行时新发现且尚未收录的名称保留原文。当前仍有 428 条唯一物品长描述保留游戏原文；它们不是遗漏的短名称或 UI 操作文案。应用自有文案使用 `localization::Text` / `Translate`；玩家填写的技能名、书签名和其他自定义文字应直接显示，不能送入翻译函数。

游戏语言来源通过本机游戏包中的 Unreal Engine 托管程序集进行只读静态检查：两个 getter 均为无参数、返回字符串的静态方法，并确认反射函数路径与 `FString` 返回字段。[提取记录](localization-validation-20261025/game-language-static-evidence.json)同时注明 REA 解析预算限制、程序集身份和静态检查不能证明游戏内调用一定成功。实现选择优先使用 `GetCurrentLanguage` 再回退到 `GetCurrentCulture`，仍需用户在游戏中确认实际语言结果。

## 自动验证

`GlossTrainerLocalizationTests` 覆盖文化标签及偏好解析、INI 的 BOM/CRLF 与 Language/Culture 优先级、默认跟随、固定语言覆盖探测、即时切换、持久化重读、坏配置、写失败回滚、翻译回退、动态模板和字典键去重。测试还逐项比较中英文的 printf 转换序列与 `{}` / `{N}` 占位符。

`GlossTrainerGameLanguageTests` mock 游戏线程和引擎反射边界，覆盖非游戏线程不探测、Language 优先、Culture 回退、非 ASCII 返回值拒绝、5 秒限频及失败后的再次探测。此测试不调用游戏，也不能替代游戏内验证。

按仓库约定，在 Debug 和 Release 下分别构建并运行：

```powershell
xmake build GlossTrainerLocalizationTests GlossTrainerGameLanguageTests
.\build\release\GlossTrainerLocalizationTests.exe
.\build\release\GlossTrainerGameLanguageTests.exe
```

Debug 执行时将 `build\release` 替换成 `build\debug`。自动测试和宿主 UI 检查不能证明游戏实际语言探测或游戏内菜单行为；游戏实测由用户完成。


## 游戏内手测

1. 删除或备份既有 `language.ini` 后启动游戏；分别使用中文、English 游戏文本语言，确认修改器默认跟随。只读设置页的来源应从配置或临时回退更新为游戏运行时。
2. 手动选择 English 或简体中文，确认菜单和缓存状态立即切换；重启确认语言偏好保留。改变游戏语言时，手动选择应保持。
3. 切回跟随游戏；检查约 5 秒内更新。繁体中文文化暂显示简体中文；其余有效文化暂显示英文。
4. 检查功能、物品、地标、变身页及亮暗主题，确认搜索和状态提示；技能自定义名称、组合键和书签名称应保留原样。
5. 单独确认输入法、游戏内改键、手柄及真实游戏语言反射结果。以上仍待用户实测。

实现前通过 `custom-imagegen`（`model_providers.custom` / `gpt-image-2.5`）生成并查看了[亮暗设计原型与完整提示词](localization-validation-20261025/prototype.md)。最终测试与实际截图见[验证记录](verification.md#中英文界面2026-10-25)。
