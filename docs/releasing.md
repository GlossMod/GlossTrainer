# GitHub 发布流程

仓库为 [GlossMod/GlossTrainer](https://github.com/GlossMod/GlossTrainer)。工作流只在正式版本标签 `vX.Y.Z` 推送时创建 Release；Actions 页面手动执行 `Build, validate and release` 只构建、验证并保留下载产物，不创建 Release。

## 发布前

1. 更新 `BlackMythWukong/src/runtime.hpp` 的 `Version`、用户说明和验证记录。
2. 提交所有构建所需源码、目录数据、验证程序和许可证。`build/`、`data/`、`output/`、本机配置以及 Git 忽略的 `Tests/` 图形宿主不属于发布源码；新 checkout 无需该宿主也能构建。
3. 在 Actions 页面选择准备发布的分支，先手动运行工作流。核对 Windows x64 Debug / Release、全部 `validation/xmake.lua` 中的 `*Tests` 程序、安装器和打包回归均通过。
4. 审查 `release-candidate` 产物后，由有推送权限的维护者创建与源码版本完全一致的标签并推送，例如：

```powershell
git tag -a v0.3.4 -m "Release v0.3.4"
git push origin v0.3.4
```

以上命令是维护者的发布操作，不会由本地打包脚本自动执行。不要移动已发布标签；修复应使用下一个版本。重新运行已成功发布的标签不会覆盖原 Release，创建命令会明确失败。

## CI 和产物

工作流固定 Windows Server 2022、xmake 3.1.2、项目声明的依赖版本，并固定官方 Actions 的提交 SHA。Debug 和 Release 在独立作业中构建及执行全部验证；验证程序从独立临时工作目录启动，不能依赖 Git 忽略的本机样本或仓库当前目录。AOT 测试使用内存中构造的 PE/元数据，不读取游戏或参考 DLL。两种构建都成功后才允许发布。

Release 附件为：

- `GlossTrainer-BlackMythWukong-vX.Y.Z-win64.zip`
- `SHA256SUMS.txt`，记录该 ZIP 的 SHA-256。

ZIP 使用明确文件清单，包含 `scripts/GlossTrainer.asi`、`Scripts/install.ps1`、游戏 README、安装说明、依赖许可和 `release-manifest.json`。包内 `README.md` 来自 `BlackMythWukong/readme.md`；打包时将已包含文件的链接改为包内路径，其余源码文档链接指向本次标签的 GitHub 页面。根 README 仅描述整个项目，不进入游戏发布包。清单记录版本、源码提交、架构以及每个有效载荷的路径、字节数和 SHA-256；清单自身由 ZIP 的外部校验和保护。打包前验证 PE32+ x64 DLL 和本地 `InitializeASI` 导出。文件顺序及 ZIP 时间固定，相同输入重复打包得到相同字节内容。

包中不放默认 `ui.ini` / `hotkeys.ini`、日志、存档、开发机安装清单、参考 EXE / RAR 或 Loader DLL。配置由运行时创建，解压包不会覆盖已有配置；安装器也保留配置。Loader 由现有安装器从固定官方 Release 下载并验证。安装和更新必须先正常退出游戏，解压包后按包内安装说明使用 `Scripts/install.ps1`。

本地构建完成后，可单独验证及打包；两个命令均不会访问游戏目录、推送 Git 或创建 GitHub Release：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-package-release.ps1 `
  -Artifact .\build\release\BlackMythWukong.asi

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\package-release.ps1 `
  -Artifact .\build\release\BlackMythWukong.asi -OutputDirectory .\dist -Tag v0.3.4
```

CI 还传入完整 `-SourceRevision`；未提供时本地包的该字段为 `null`，不推断未提交源码来自某个提交。版本取自源码；维护者应始终对本次构建的 ASI 打包，不能拿旧二进制搭配新源码发布。

## 仓库权限与设置

需要启用 GitHub Actions，并允许工作流使用官方 `actions/checkout`、`actions/upload-artifact`、`actions/download-artifact` 和 xmake 项目的官方 `xmake-io/github-action-setup-xmake`。默认作业仅有 `contents: read`；仅发布作业申请 `contents: write`，使用内置 `GITHUB_TOKEN` 创建 Release，无需额外 PAT。

仓库或组织若限制 Actions、令牌写权限、标签创建或 Release 发布，维护者须在相应仓库设置或规则中授权。Windows 托管 runner 需可访问 GitHub、xmake 项目和依赖的官方下载来源。Actions 产物保留 14 天；Release 附件按 GitHub Release 保留。

应用的更新检查只读取 GitHub 最新正式 Release，提示玩家前往该 Release 手工下载，不自动下载、执行安装器或替换正在运行的 ASI。发布操作与客户端更新安装是分开的步骤。

官方参考：[GitHub 的工作流事件](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)、[GitHub CLI release create](https://cli.github.com/manual/gh_release_create)、[xmake 官方 setup action](https://github.com/xmake-io/github-action-setup-xmake)。
