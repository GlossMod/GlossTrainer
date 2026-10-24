# GlossTrainer

**By: 小莫**

GlossTrainer 是面向 Windows x64 单机游戏的游戏内工具项目。每个游戏的界面、功能适配与验证代码放在独立目录；公共构建、安装和发布工具保留在项目根目录。

使用 C++20、Dear ImGui 和 MinHook，提供支持亮色 / 暗色主题的中文界面、可自定义快捷键、配置持久化和日志。游戏接口与兼容范围以各模块文档为准。

## 游戏模块

| 模块 | 说明 |
| --- | --- |
| [BlackMythWukong](BlackMythWukong/readme.md) | 《黑神话：悟空》的功能、安装、使用及适配说明 |

## 项目结构

- 游戏目录：源码、游戏适配及回归测试。
- `Scripts/`：构建辅助、清单式安装 / 卸载、发行包生成和校验。
- `.github/workflows/`：Windows x64 构建、回归与 GitHub Releases 发布。
- `docs/`：架构、验证与维护文档。
- `licenses/`：第三方依赖许可。

## 构建与发布

需要 Windows、Visual Studio 2022 C++ 桌面工具、Windows SDK 及 [xmake](https://xmake.io/)。在仓库根目录配置，再构建相应模块：

```powershell
xmake f -y -p windows -a x64 -m release
xmake build <模块名>
```

输出位于 `build/release/`；Debug 使用 `-m debug`。安装方式与依赖见各模块文档。

正式 `vX.Y.Z` 标签触发 Debug / Release 构建、回归、打包和 Releases 发布；手动运行 workflow 仅构建验证。流程、版本规则和本地打包方式见 [发布说明](docs/releasing.md)。

更新检查只使用公开 GitHub Releases，不自动下载安装。成功确认有新版后要求玩家自行下载；检查失败不限制使用，仅显示“检查更新失败”。

[下载最新发行版](https://github.com/GlossMod/GlossTrainer/releases/latest) · [源码](https://github.com/GlossMod/GlossTrainer)

第三方许可位于 [licenses](licenses/)，分发时请保留。各模块的运行要求、使用说明和验证范围以模块 README 为准。
