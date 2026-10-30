# 影之刃零内置修改器

GlossTrainer v{{VERSION}} · Windows x64

[简体中文](README.md) · [English](readme_en.md)

## 安装方法

1. 保存并正常退出游戏，解压发行 ZIP。
2. 将 `version.dll`、`version.ini` 和 `scripts` 文件夹一起复制到游戏的 `platform/steam`，与该目录的 `PBZ.exe` 同级。
3. 启动游戏，默认按 **Insert** 打开或隐藏菜单。可在设置页“菜单与布局”点击按键框自定义组合键或恢复默认，支持 Ctrl / Shift / Alt，Esc 取消，设置会自动保存。飞行升降默认使用 **Page Up / Page Down**，每次移动 100；其他新增功能快捷键默认未绑定，均可在修改功能页设置。已有绑定和手动清除的绑定会保留。

```text
PBZ/platform/steam/
  PBZ.exe
  version.dll
  version.ini
  scripts/
    GlossTrainer.asi
```

ZIP 已包含 Ultimate ASI Loader v10.0.0 x64，无需另行下载。已有其他插件的同名 Loader 时先核对归属。升级前退出游戏并备份旧文件，移出旧的 `scripts/PBZ.asi`，避免与 `GlossTrainer.asi` 重复加载。不要删除其他插件。

配置与日志在 `scripts/GlossTrainer/PBZ/`。发行包不含默认配置，保留原有 `ui.ini`、`hotkeys.ini`、`features.ini` 和 `language.ini`。卸载时退出游戏后移除 `scripts/GlossTrainer.asi`；只有确认 Loader 无其他使用者时才移除 `version.dll` 与 `version.ini`。

## 包含功能

- 无限生命、杀气、纯杀气段数、暗源，自动完美格挡、自动完美闪避及无敌模式。
- 伤害、防御、破防和纯杀气获取倍率，一击必杀、一击破防、隐身及武器切换冷却。
- 主动技能蓝图杀气消耗倍率：覆盖重击和煞兵蓝图费用，不改变受击削杀气；原生风雷转换未覆盖。
- 药瓶、物品、商店库存不减。
- 移动、玩家、AI 和全局游戏速度，跳跃高度、重力、FOV；飞行穿墙，Page Up / Page Down 调整高度（可改键），默认步长 100。关闭时检查胶囊空间，阻挡中保持飞行。
- 编辑剩余天数、解锁六十六天难度；一次性进度操作由游戏保存。
- 读取生命、设置最大生命并回血、恢复生命、药品及终结技补充。部分操作可能修改存档。
- 游戏内动态物品目录和图标、分类搜索、数量确认与库存增量核对。唯一类物品限制单次 1 件，普通物品单次最多 999。
- 获取意识尘埃取消原先单次 999 的人为限制，可直接输入数量；获取后的总量须在游戏 32 位有符号整数范围内（最多 2,147,483,647）。
- 物品页先显示缓存与已解析名称，说明分批补齐；当前场景目录验证完成后才允许获取。
- 自动保存已验证的开关与倍率参数，下次进入游戏及对应功能就绪后恢复；一次性操作不会重放。参数编辑失败保留上次成功值。
- 自定义键盘快捷键、中文 / English 界面、亮暗主题、缩放和布局记忆。
- 独立 `PhantomBladeZero/v` 更新通道，在设置与关于页检查更新，手动前往发布页下载。

支持 `CL-301524_JKS-17219` Windows x64 主程序，SHA-256：`7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4`。其他构建将禁用原生调用。新战斗行为与跨场景效果仍需逐项实机验证，以[验证记录](https://github.com/GlossMod/GlossTrainer/blob/master/PhantomBladeZero/docs/verification.md)为准。物品名称和描述使用游戏语言。

## 关于

当前版本：v{{VERSION}}。最新版本在关于页查询；无发行或网络失败会明确提示，不影响现有功能，修改器不自动安装更新。

## 署名

By: 小莫

- [主 LICENSE · GNU GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE)
- [源码](https://github.com/GlossMod/GlossTrainer)
- [影之刃零下载](https://github.com/GlossMod/GlossTrainer/releases?q=PhantomBladeZero%2Fv&expanded=true)
- [赞助作者](https://sponsor.aoe.top/)
- [申请制作游戏修改器](https://www.glossmod.com/commissions)

## licenses

{{LICENSES}}
