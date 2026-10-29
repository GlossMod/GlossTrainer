## 影之刃零 v1.0.1

- 修复 获取物品导致卡顿的问题；
- 实现可以自定义界面按键

下载 Windows x64 ZIP，并用同名 `.SHA256SUMS.txt` 核对。保存并退出游戏后，将 `version.dll`、`version.ini` 和 `scripts` 文件夹一起复制到游戏的 **`platform/steam`（与其中的 `PBZ.exe` 同级）**。默认按 **Insert** 打开菜单，在设置页“菜单与布局”中点击按键框自定义组合键；支持 Ctrl / Shift / Alt，自动保存，可恢复默认。旧 `scripts/PBZ.asi` 需移出加载目录；保留用户设置与其他插件。

ZIP 已包含 Loader、中文及英文说明和唯一的 `scripts/GlossTrainer.asi`。支持主程序 `CL-301524_JKS-17219`，其他版本会停止原生调用。游戏内物品名称和描述保持游戏语言。战斗与跨场景功能的实机验证范围见[验证记录](https://github.com/GlossMod/GlossTrainer/blob/master/PhantomBladeZero/docs/verification.md)。

更新仅识别 `PhantomBladeZero/v` 通道，不自动下载或覆盖文件。网络检查失败不影响功能使用。

Phantom Blade Zero v1.0.1 fixes stuttering when acquiring items and adds a customizable menu toggle key. Copy the included Loader and `scripts` to `platform/steam` beside `PBZ.exe`, after exiting the game. See `readme_en.md` in the ZIP for installation, supported build and validation limits.

[源码](https://github.com/GlossMod/GlossTrainer) · [GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE) · [赞助](https://sponsor.aoe.top/) · [申请制作修改器](https://www.glossmod.com/commissions)
