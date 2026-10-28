# 黑神话：悟空内置修改器

GlossTrainer v{{VERSION}} · Windows x64

[简体中文（默认）](README.md) · [English](readme_en.md)

## 安装方法

1. 正常退出游戏，解压发行包。
2. 将 `version.dll`、`version.ini` 和 `scripts` 文件夹一起复制到游戏的 `b1\Binaries\Win64` 目录，与 `b1-Win64-Shipping.exe` 放在同一级。
3. 启动游戏，按 **Insert** 显示或隐藏菜单。可以在“设置 → 快捷键”自定义界面开关按键；功能快捷键默认全部未绑定，按需自行设置。

安装后的文件结构：

```text
BlackMythWukong/
└─ b1/Binaries/Win64/
   ├─ b1-Win64-Shipping.exe
   ├─ version.dll
   ├─ version.ini
   └─ scripts/
      └─ GlossTrainer.asi
```

发行包已包含 Ultimate ASI Loader v10.0.0 x64，安装时无需联网下载 Loader。同名 `version.dll` 或 `version.ini` 若属于其他插件，请先核对归属；保留其他插件及其文件。

更新前退出游戏，再复制新版文件。配置和日志由修改器自动生成在 `scripts\GlossTrainer` 中，发行包不包含默认配置，更新不会覆盖已有的主题、缩放或快捷键设置。

卸载前退出游戏，移除 `scripts\GlossTrainer.asi`；确认 `version.dll`、`version.ini` 仅供本修改器使用后再移除。`scripts\GlossTrainer` 中的配置和日志可按需保留，不要删除整个 `scripts` 文件夹。

可从 [黑神话：悟空发行版](https://github.com/GlossMod/GlossTrainer/releases?q=BlackMythWukong%2Fv&expanded=true) 获取新版，使用同页对应 ZIP 名称的 `.SHA256SUMS.txt` 核对文件。版本标签以 `BlackMythWukong/v` 开头；更新检查只比较本游戏正式版本。成功检查到新版时，需自行下载更新后继续使用修改功能；网络检查失败仅显示“检查更新失败”，不限制使用。修改器不会自动下载安装。

## 包含功能

- **生存与法术**：生命、法力、气力、棍势、葫芦使用次数，以及法术、变身、化身和法宝相关功能。
- **战斗与属性**：伤害、防御、暴击、韧性、异常状态、属性上限和四种抗性等，共 64 项可搜索、分类的修改功能。
- **资源与移动**：灵蕴、经验、灵光点、根器点数、掉落、移动速度、跳跃和游戏速度等。
- **物品工具**：1,160 条物品目录，支持搜索、数量设置及对应类别的添加、删除；精魄、法术和影神图的单项删除暂未实现。
- **传送工具**：32 个场景、284 个地标；地标按场景折叠分类，支持搜索、坐标移动、位置书签及场景对象操作。
- **七十二变（实验性）**：扩展角色目录并补充翻译，改善角色基础操作；支持搜索角色、在独立窗口设置技能名称及键盘或鼠标组合键、按角色保存视距与移动速度，以及恢复悟空。未验证角色也可尝试；部分角色缺少移动动画，会以滑行方式移动，特殊角色的能力可能不完整。
- **记住修改项**：自动保存开关和数值，下次启动并进入存档、对应功能就绪后恢复上次启用项。尚未应用的数值只恢复输入；增加经验、解锁等一次性操作不会自动重放。
- **界面与设置**：亮暗主题、界面缩放、中文输入、自定义菜单键和功能快捷键、配置保存，以及简体中文 / English 界面。

功能适配随游戏版本而异，以当前菜单中的可用状态为准。物品、剧情和跨场景操作可能改变存档进度，操作前请保留存档备份。

## 署名

**By: 小莫**

## 关于

- **当前版本号**：v{{VERSION}}。
- **最新版本号**：打开修改器“关于”页查看检查结果，或访问[本游戏 GitHub Releases](https://github.com/GlossMod/GlossTrainer/releases?q=BlackMythWukong%2Fv&expanded=true)。本文记录随包版本，不将它标记为在线最新版本。
- **作者：小莫**。
- **主 LICENSE**：[GNU GPL v3](https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE)。
- **开源地址**：[GlossMod / GlossTrainer](https://github.com/GlossMod/GlossTrainer)。
- **赞助作者**：[sponsor.aoe.top](https://sponsor.aoe.top/)。
- **申请制作游戏修改器**：[GlossMod 委托制作](https://www.glossmod.com/commissions)。

## licenses

GlossTrainer 使用 GNU GPL v3 许可；[本版本源码](https://github.com/GlossMod/GlossTrainer/tree/BlackMythWukong/v{{VERSION}})。以下保留项目及随包组件的完整许可证和版权声明。

{{LICENSES}}
