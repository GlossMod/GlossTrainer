## 影之刃零 v1.1.0

- 新增功能：
  - 战斗与生存：无限生命、无限纯杀气、隐身、武器切换无冷却、瞬间破防、一击必杀，以及伤害、防御、姿态伤害（破防）、杀气消耗、纯杀气获取倍率。
  - 物品与资源：药瓶不减、物品不减、商店库存不减。
  - 移动与视角：移动速度、玩家速度、AI 速度、跳跃高度、重力倍率及视野范围（FOV）；游戏速度改为开关并支持参数恢复。
  - 飞行穿墙：支持飞行速度调整和上升、下降，默认按键为 Page Up / Page Down；关闭时若当前位置有阻挡，会提示并保持飞行。
  - 游戏进度：编辑剩余天数、解锁六十六天难度。
  - 功能设置：倍率参数编辑、保存与恢复；新增功能支持自定义快捷键及冲突检查，保留已有绑定和手动解绑状态。
- 优化物品列表加载速度：优先显示缓存与物品名称，分批补齐详细说明，目录验证完成后即可获取物品。
- 移除获取意识尘埃的数量上限：取消原先单次 999 的人为限制，支持直接输入数量；保留游戏 32 位整数范围和库存溢出检查。

杀气消耗倍率覆盖主动技能蓝图消耗，原生风雷转换路径未覆盖。各项功能的实机验证范围见下方验证记录。

下载 Windows x64 ZIP，并用同名 `.SHA256SUMS.txt` 核对。保存并退出游戏后，将 `version.dll`、`version.ini` 和 `scripts` 文件夹一起复制到游戏的 **`platform/steam`（与其中的 `PBZ.exe` 同级）**。默认按 **Insert** 打开菜单，在设置页“菜单与布局”中点击按键框自定义组合键；支持 Ctrl / Shift / Alt，自动保存，可恢复默认。旧 `scripts/PBZ.asi` 需移出加载目录；保留用户设置与其他插件。

ZIP 已包含 Loader、中文及英文说明和唯一的 `scripts/GlossTrainer.asi`。支持主程序 `CL-301524_JKS-17219`，其他版本会停止原生调用。游戏内物品名称和描述保持游戏语言。战斗与跨场景功能的实机验证范围见[验证记录](https://github.com/GlossMod/GlossTrainer/blob/master/PhantomBladeZero/docs/verification.md)。

更新仅识别 `PhantomBladeZero/v` 通道，不自动下载或覆盖文件。网络检查失败不影响功能使用。

Phantom Blade Zero v1.1.0 expands combat, resources, movement, flight and progression features, with saved parameters and configurable hotkeys. It also speeds up item-list loading and removes the trainer's 999-per-request limit for Consciousness Dust, while retaining integer overflow checks. Copy the included Loader and `scripts` to `platform/steam` beside `PBZ.exe`, after exiting the game. See `readme_en.md` in the ZIP for installation, supported build and validation limits.

[源码](https://github.com/GlossMod/GlossTrainer) · [GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE) · [赞助](https://sponsor.aoe.top/) · [申请制作修改器](https://www.glossmod.com/commissions)
