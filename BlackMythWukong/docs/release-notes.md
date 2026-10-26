## 黑神话：悟空 v1.0.0

- 增加“关于”页，显示当前和最新版本、作者、主许可证、开源地址、赞助及委托制作入口。
- 发行包增加完整英文 `readme_en.md`，默认中文仍为 `README.md`；两份均含安装、功能、关于信息及完整许可证。
- 继续提供 64 项分类修改功能、物品与传送工具、实验性七十二变，以及亮暗主题、中英界面和自定义快捷键。作者：**小莫**。

下载本页 Windows x64 ZIP，使用同名 `.SHA256SUMS.txt` 核对文件。正常退出游戏后，将 `version.dll`、`version.ini` 和 `scripts` 文件夹一起复制到 `b1/Binaries/Win64`（与 `b1-Win64-Shipping.exe` 同级）。首次启动按 **Insert** 显示菜单，菜单键可在设置中更改；原有配置会保留，功能快捷键默认未绑定。

ZIP 内为 `version.dll`、`version.ini`、`README.md`、`readme_en.md` 与唯一的 `scripts/GlossTrainer.asi`。已包含 Loader，无需安装时另行下载。不要覆盖归属其他插件的同名代理 DLL。

更新检查仅比较 `BlackMythWukong/v` 通道的正式版本。确认新版后引导手动下载；检查失败仅显示“检查更新失败”，不限制使用。修改器不会自动下载或安装。七十二变仍为实验性，游戏适配与逐项验证范围以包内说明及[验证记录](https://github.com/GlossMod/GlossTrainer/blob/master/BlackMythWukong/docs/verification.md)为准。

源码：[GlossMod / GlossTrainer](https://github.com/GlossMod/GlossTrainer) · 赞助：[sponsor.aoe.top](https://sponsor.aoe.top/) · [申请制作游戏修改器](https://www.glossmod.com/commissions) · [主 LICENSE](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE)

## 黑神话：悟空 v0.3.5

- 将角色变身入口命名为“七十二变（实验性）”。支持搜索角色、查看角色技能列表、自定义技能名称、键鼠组合键及每角色视距，配置可保存。
- 完善临时角色、武器与特殊形态初始化，以及过场结束后的角色恢复。缺少可用走跑动画时允许原生移动；红孩儿已由用户确认可以移动。
- 按游戏隔离版本标签、构建验证、发行包、校验文件和更新检查，使用 `BlackMythWukong/v0.3.5` 标签。后续同仓库其他游戏的发布不会触发本游戏升级。

七十二变仍为实验功能。部分角色移动时会滑行，特殊角色的移动、武器或技能可能不完整；“未验证”角色可以尝试，但不代表已通过逐角色实测。当前保留用户接受的移动效果。

从 v0.3.4 升级时，请手动下载本页安装包；旧版使用仓库通用更新通道，无法识别新的游戏专属标签。从本版本开始仅检查本游戏正式发布。退出游戏后复制包内文件，原有技能名称、按键和视距配置保留。

附件包含 Windows x64 安装 ZIP 及同名 `.SHA256SUMS.txt` 校验文件；ZIP 包内为 `version.dll`、`version.ini`、`README.md` 与 `scripts/GlossTrainer.asi`。
