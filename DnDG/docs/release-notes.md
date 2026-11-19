## Dungeons & Degenerate Gamblers · v1.0.0

首个原生版本。适配 **Windows Steam x64 版 Dungeons & Degenerate Gamblers 2.0.8.1**。使用 C++20、Dear ImGui、MinHook 和 Ultimate ASI Loader。

- 编辑生命、护盾和普通筹码，支持一键回满生命。
- 袖中牌和桌面牌以卡面显示。悬停查看详情，右键操作卡牌。
- 以图标网格添加幸运护符和独特筹码，并可替换或移除已拥有的物品。
- 开启下一张牌预览后，悬停左上角牌名即可看效果。
- 提供可拖动、可缩放的原生 D3D12 界面，支持亮暗主题、中英文和自定义快捷键。默认按 **Insert** 打开。

### 安装

下载并解压 `GlossTrainer-DnDG-v1.0.0-win64.zip`。退出游戏后按包内 `README.md` 运行安装脚本。脚本会同时配置 ASI 加载与 Godot 游戏线程桥接。随后从 Steam 启动。修改器对局使用独立的本地存档目录。

已通过安装文件检查、Debug 和 Release 自动化回归。Steam 普通对局的数据读取与修改仍待逐项实战确认。
