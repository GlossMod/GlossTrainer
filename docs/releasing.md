# 按游戏隔离的 GitHub 发布

仓库为 [GlossMod/GlossTrainer](https://github.com/GlossMod/GlossTrainer)。每个游戏有独立版本、标签、构建验证、发行包和更新通道。`v0.3.4` 等历史标签保留，新发布只接受 `<Game>/vX.Y.Z`，例如 `BlackMythWukong/v0.3.5`。其他游戏可以同时使用 `v0.3.5`，互不占用版本号。

## 注册游戏

[Scripts/release-games.json](../Scripts/release-games.json) 是构建与打包入口注册表；[resolve-release.ps1](../Scripts/resolve-release.ps1) 在本地及 CI 中使用同一套解析与校验规则。游戏 ID 区分大小写，只能含字母和数字，以字母开头。每项指定本游戏的 target、版本源码、验证 target 列表所在文件、专用 README、releaseNotes、打包脚本、打包测试、安装测试和 Release ASI 路径。当前仅注册真实存在的 `BlackMythWukong`；第二游戏只在自动测试夹具内出现。

版本的唯一来源仍是各游戏自己的 `src/runtime.hpp` 中的 `Version`。注册表不再存一份版本，修改一个游戏的版本不会改变另一个游戏。解析器拒绝未知或重复游戏、裸标签、跨游戏标签、与源码不一致的版本、非法路径和不匹配的 ASI target。

新增游戏时：

1. 在与游戏 ID 同名的独立目录添加其源码、构建 target、版本、验证和发行说明；根 xmake 根据注册游戏自动引入模块。验证 target 名也应加游戏前缀；解析器强制所有游戏的 build / test target 全局唯一，冲突将直接拒绝发布。版本、验证、README 和 releaseNotes 源文件强制放在对应 ID 的游戏目录内。
2. 在注册表添加唯一游戏 ID 及对应入口，确认该游戏的构建输出为 `build/release/<target>.asi`。当前内置打包器支持 `asi-win64`；不同分发格式需要单独实现对应打包与验证流程，不能直接套用 ASI 包。
3. 客户端更新检查必须按游戏标签前缀和本游戏附件筛选，不能依赖仓库全局 `/releases/latest`。给新游戏配置自己的更新通道和安装目录。
4. 手动执行 Actions，指定该游戏 ID，核对其 Debug / Release 和包回归，再推送自己的标签。

## 发布步骤

1. 修改本游戏 `Version`、releaseNotes、玩家说明和验证记录，提交所有构建所需源码、目录数据、测试和许可。`build/`、`data/`、`output/` 和 Git 忽略的 `Tests/` 不属于发布源码；新 checkout 无需本地图形宿主。
2. 在 Actions 手动运行 `Build, validate and release`，输入注册游戏 ID。手动运行仅构建、验证并保留候选包，不创建 Release。
3. 审查候选包后，对同一提交推送与源码版本匹配的游戏标签：

```powershell
git tag -a BlackMythWukong/v0.3.5 -m "Release BlackMythWukong v0.3.5"
git push origin BlackMythWukong/v0.3.5
```

不要移动已经发布的标签；修复使用该游戏下一个版本。发布命令不使用覆盖上传，重跑已经成功发布的标签会明确失败。

## CI 与产物隔离

工作流先解析标签或手动输入的游戏，再仅构建该游戏 target 及其 `validationFile` 中的 `*Tests`，配置时显式传入 `--game=<Game>`，根 xmake 仅导入对应游戏模块及其依赖；不执行全仓库 `xmake build`。现有不传 `--game` 的命令默认 BlackMythWukong。Windows 2022、xmake 3.1.2、依赖版本和官方 Actions 提交 SHA 固定。Debug / Release 分属独立作业；测试从本游戏专属的 `output/validation/<Game>/v<version>/<mode>` 运行，不能依赖本机游戏或被忽略的样本。

默认打包输出为 `output/releases/<Game>/v<version>/`，候选 artifact 为 `release-candidate-<Game>-v<version>`。校验文件与清单也带游戏和版本；即使维护者显式让两个游戏写入同一输出目录，也不会覆盖彼此的包、校验或清单。例如：

- `GlossTrainer-BlackMythWukong-v0.3.5-win64.zip`
- `GlossTrainer-BlackMythWukong-v0.3.5-win64.SHA256SUMS.txt`
- `GlossTrainer-BlackMythWukong-v0.3.5-win64.manifest.json`（仅候选 artifact / 本地审计）

正式 Release 仅上传 ZIP 和对应 SHA256SUMS。发布说明从注册表中该游戏的 releaseNotes 文件读取；每次随本游戏版本维护，不使用跨游戏自动汇总提交的 generate-notes。发布前再次校验 game、tag、version、sourceRevision、ZIP 文件名与哈希，两个配置构建验证都通过才发布。Release 标题包含游戏；使用 `--latest=false`，不争用仓库全局 Latest。下载时按游戏名称和标签选择；GitHub 的全局 Latest 不代表任意游戏的最新版本。旧版修改器仍使用全局 Latest 时，应手工下载升级到支持游戏通道的版本。

## ASI 复制安装包

ZIP 使用明确的四文件清单：

```text
version.dll
version.ini
scripts/
  GlossTrainer.asi
README.md
```

ASI 仅放一份，位于 `scripts/GlossTrainer.asi`。`version.ini` 固定 `DontLoadFromDllMain=1`、`LoadPlugins=1`、`LoadFromScriptsOnly=1` 和 `DebugLog=1`。当前黑神话包复制到实际 `b1/Binaries/Win64`；其他游戏的真实安装目录由对应游戏说明确定，不能因同为 ASI 就混用游戏适配文件。

README 来自注册表指定的游戏模板。打包器替换 `{{VERSION}}`，将根 `LICENSE` 与 `licenses/*.txt` 的完整内容写入 `{{LICENSES}}`。不打包根开发 README、默认用户配置、日志、存档、安装清单、参考样本、安装脚本或额外许可目录。用户配置在运行时创建，覆盖更新 ZIP 不改已有配置。

打包器固定 [Ultimate ASI Loader v10.0.0 x64 官方归档](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.0/Ultimate-ASI-Loader_x64.zip)，SHA-256 为 `3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7`。归档只有 `dinput8.dll`，验证后改名 `version.dll`，不从开发机游戏目录读取 Loader。`-LoaderArchive` 允许离线指定同一官方归档，不能绕过固定哈希。Loader 和 ASI 均校验 PE32+ x64 DLL，ASI 须有本地 `InitializeASI` 导出。

清单 schema 3 记录游戏、独立版本和标签、源码提交、架构、Loader 来源、ZIP 及全部有效载荷的字节数与 SHA-256，不进入 ZIP。文件顺序和时间固定；相同输入重复打包结果相同。

```powershell
# 不传 OutputDirectory 时自动使用游戏和版本目录。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\package-release.ps1 `
  -Game BlackMythWukong -Artifact .\build\release\BlackMythWukong.asi `
  -Tag BlackMythWukong/v0.3.5

# 打包回归离线运行，不访问游戏。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-package-release.ps1 `
  -Artifact .\build\release\BlackMythWukong.asi `
  -LoaderArchive .\output\releases\BlackMythWukong\v0.3.5\cache\Ultimate-ASI-Loader\v10.0.0\Ultimate-ASI-Loader_x64.zip `
  -OutputDirectory .\output\game-release-validation

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Scripts\test-release-isolation.ps1 `
  -OutputDirectory .\output\game-release-validation
```

上述本地脚本不推送 Git 或创建 Release。CI 传入完整 `-SourceRevision`；本地未提供时清单字段为 `null`，不会把未提交源码冒充某个提交。源码版本校验不代表二进制由该源码构建，维护者必须使用本次构建的 ASI。

## 权限与验证

默认工作流只有 `contents: read`，仅发布作业申请 `contents: write`，使用内置 `GITHUB_TOKEN`，无需额外 PAT。官方 Actions、xmake、GitHub 下载来源需可访问；候选 artifact 保留 14 天。发布工作流并发组按 tag / 手动游戏区分，不互相取消。

每次发布在 PowerShell 5.1 / 7 运行便携包和游戏隔离测试。隔离夹具覆盖两游戏相同版本、输出目录共用、独立测试列表、跨游戏 / 版本混配拒绝、未知游戏、重复 ID 和路径越界拒绝；它不宣称验证了尚未实现的第二款游戏。

应用更新检查仅提示手工下载，不自动下载、运行安装器或替换正在使用的 ASI。游戏内特性与发布包验证记录在各游戏自己的 `docs/verification.md`，例如[黑神话验证记录](../BlackMythWukong/docs/verification.md)。
