## 黑神话：悟空 v1.1.0

- 优化 七十二变，现在角色能进行基础的操作了；
- 实现 记住修改项，下次启动游戏时自动加载上一次操作；
- 新增 更多可变身角色，以及补充角色翻译；
- 优化 角色技能快捷键设置窗口；

下载本页 Windows x64 ZIP，使用同名 `.SHA256SUMS.txt` 核对文件。正常退出游戏后，将 `version.dll`、`version.ini` 和 `scripts` 文件夹一起复制到 `b1/Binaries/Win64`（与 `b1-Win64-Shipping.exe` 同级）。首次启动按 **Insert** 显示菜单，菜单键可在设置中更改；原有配置会保留，功能快捷键默认未绑定。

ZIP 内为 `version.dll`、`version.ini`、`README.md`、`readme_en.md` 与唯一的 `scripts/GlossTrainer.asi`。已包含 Loader，无需安装时另行下载。不要覆盖归属其他插件的同名代理 DLL。

更新检查仅比较 `BlackMythWukong/v` 通道的正式版本。确认新版后引导手动下载；检查失败仅显示“检查更新失败”，不限制使用。修改器不会自动下载或安装。七十二变仍为实验性，游戏适配与逐项验证范围以包内说明及[验证记录](https://github.com/GlossMod/GlossTrainer/blob/master/BlackMythWukong/docs/verification.md)为准。

源码：[GlossMod / GlossTrainer](https://github.com/GlossMod/GlossTrainer) · 赞助：[sponsor.aoe.top](https://sponsor.aoe.top/) · [申请制作游戏修改器](https://www.glossmod.com/commissions) · [主 LICENSE](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE)
