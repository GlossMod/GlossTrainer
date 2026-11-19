# GlossTrainer DnDG v1.0.0

适配 **Windows Steam x64 / Dungeons & Degenerate Gamblers 2.0.8.1**。安装脚本需要 Python 3.11 或更新版本。请先在 Steam 中找到游戏安装目录并退出游戏。

## 安装

1. 解压 `GlossTrainer-DnDG-v1.0.0-win64.zip` 至游戏目录之外。
2. 在解压目录打开 PowerShell。运行下方命令，在提示时粘贴 Steam 中「管理 → 浏览本地文件」打开的目录。

```powershell
$game = Read-Host '请输入 Steam 游戏目录 / Enter Steam game directory'
py -3 -B .\tools\install_steam_bridge.py --game-dir $game --package-dir .
```

加上 `--dry-run` 可先检查游戏版本、预览安装文件及检查冲突。如果已安装过开发版且提示 `scripts/GlossTrainer.asi` 冲突，请在游戏退出后将旧 ASI 备份到游戏目录之外，再执行同一命令。安装器会保留其他冲突文件。安装完成后从 Steam 正常启动。

游戏存档使用 `%APPDATA%\GlossTrainer\DnDG\native\steam`，首次启动通常显示「新游戏」。修改器设置保存在 `%LOCALAPPDATA%\GlossTrainer\DnDG\native.ini`。

## 使用

进入普通对局，等到可操作时按 **Insert** 开关面板。拖动标题栏可移动，拖动右下角可调整尺寸。

- **玩家**可编辑生命、护盾和普通筹码，也可一键回满生命。
- **卡牌**可悬停看详情，右键丢弃、替换或添加。牌库支持按名字或 ID 搜索。
- **物品**可点选网格内的幸运护符或独特筹码图标添加，右键已获得的物品可替换或移除。
- **显示下一张牌**开启后收起面板，左上角出现可预览的牌名。悬停牌名查看卡牌效果。
- **设置**提供亮色和暗色、中英文切换，以及自定义面板快捷键。

首次发布已完成安装文件和自动化回归验证。Steam 普通对局内的读写仍待逐项实战确认。

## 卸载

退出游戏。若这些文件由本安装包创建，将游戏目录下的 `override.cfg`、`.godot/extension_list.cfg`、`gloss_trainer_native/bridge.gd`、`gloss_trainer_native/bridge.gdextension`、`scripts/GlossTrainer.asi`、`dinput8.ini` 和 `dinput8.dll` 移至游戏目录之外。此后从 Steam 启动会恢复使用原版设置和原存档目录。独立存档可保留。

## English quick start

Windows Steam x64, game version **2.0.8.1**, Python 3.11 or later. Close the game, extract the ZIP outside the game folder, and run the PowerShell commands above from the extracted directory. When prompted, enter the folder shown by Steam’s **Manage → Browse local files**. Start the game through Steam and press **Insert** during a normal encounter. Trainer-enabled sessions use a separate local save directory. Installation and automated regression checks passed. Live gameplay reads and writes are pending individual verification.
