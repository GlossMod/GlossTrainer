# DnDG 原生 ASI 构建与验证

适配 Windows Steam x64 版 **Dungeons & Degenerate Gamblers 2.0.8.1**。原生版使用 C++20、Dear ImGui、MinHook 和 Ultimate ASI Loader v10.0.0，产物为 `GlossTrainer.asi`。Ultimate ASI Loader 通过隔离包中的 `dinput8.dll` 加载 ASI，派生 PCK 通过 GDExtension 启动同一个 ASI 的 Godot 游戏线程桥接。界面走 D3D12；这里的 GDScript 仅用于启动桥接或运行 smoke，不是 [历史 GDScript 原型](../README.md#历史v014-godotgdscript-资源包原型)的界面实现。

Steam 直装使用[发行包与安装脚本](release-readme.md)。本页也说明如何构建仅供本机验证的隔离包。真实普通对局逐项验收状态见[验证记录](verification.md)。

## 构建 Release

需要 Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、**MSVC x64 14.38**（证据使用 `14.38.33130`）、xmake、Git、Python 3.11+ 和 SCons。SDK 使用官方 `godotengine/godot-cpp` 的 `10.0.0-stable`，提交固定为 `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`；绑定按 **Godot API 4.6** 生成，而非按 godot-cpp 的标签版本选择 API。

**SDK 和 ASI 必须由同一套 14.38 工具链编译。** 仅为 xmake 传 `--vs_toolset=14.38` 不会更改此前 SCons 生成的 SDK。已观察到用 14.44 编译的 ASI 与 14.38 SDK 混用时，Godot 对象构造阶段的 TLS 钩子失败，日志报 `Godot Object created without binding callbacks`，smoke 异常退出。切换工具链时使用干净的 SDK 构建目录，不复用另一套工具链的 `.obj` 或 `.lib`；也要核对 SCons 的实际编译环境，不能只看 xmake 配置。

在新的 **cmd.exe** 窗口中进入仓库根目录后执行以下示例，按安装位置调整 `vcvars64.bat`。该命令须在运行 SCons 的同一窗口生效；确认 `VCToolsVersion` 和 `where cl` 首项均指向 `14.38`。如果 SCons 自动探测仍选择 14.44，停止构建，改用默认工具集为 14.38 的 VS 构建环境并重新确认。示例使用新的输出位置，不覆盖已有证据。

```bat
set "ROOT=%CD%"
set "SDK=%ROOT%\output\dndg-asi-local\godot-cpp"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.38
echo %VCToolsVersion%
where cl
if not exist "%ROOT%\output\dndg-asi-local" mkdir "%ROOT%\output\dndg-asi-local"
git clone --branch 10.0.0-stable --single-branch https://github.com/godotengine/godot-cpp.git "%SDK%"
git -C "%SDK%" rev-parse HEAD
```

确认提交 SHA 后继续。编译 SDK 时使用仓库的 `build_profile`、`template_release` 与动态 C++ 运行库（`use_static_cpp=no`，对应 `/MD`）；配置 xmake 时从 `DnDG` 子项目进入，不以根目录的发布注册表作为入口。

```bat
scons -C "%SDK%" platform=windows arch=x86_64 api_version=4.6 target=template_release use_static_cpp=no build_profile="%ROOT%\DnDG\validation\godot_build_profile.json"
xmake f -P "%ROOT%\DnDG" -c -p windows -a x64 -m release --vs_toolset=14.38 --godot_cpp="%SDK%"
xmake build -P "%ROOT%\DnDG" -v DnDG
```

SDK 应生成 `gen/include/godot_cpp/classes/scene_tree.hpp` 和 `bin/libgodot-cpp.windows.template_release.x86_64.lib`。产物在 `build/DnDG/release/GlossTrainer.asi`。不提供 `--godot_cpp` 时，原生目标会明确拒绝构建，避免误交付缺少 GDExtension 入口的 ASI。用 `-m debug` 可生成 `build/DnDG/debug/GlossTrainer.asi`；两种配置均使用模板 SDK 对应的 `/MD`。

## 打包 Steam 安装版

先按上文构建 Release，再使用已校验的 Ultimate ASI Loader 10.0.0 x64 ZIP，并指定新的输出文件。DnDG 标签走独立打包流程。

```powershell
$loader = Read-Host '请输入 Ultimate-ASI-Loader_x64.zip 的路径'
$zip = 'output/releases/DnDG/v1.0.0/GlossTrainer-DnDG-v1.0.0-win64.zip'
py -3 -B DnDG/tools/package_steam_release.py --asi build/DnDG/release/GlossTrainer.asi --loader $loader --output $zip
if ($LASTEXITCODE) { throw 'DnDG packaging failed' }
$sha = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ([IO.Path]::ChangeExtension($zip, '.SHA256SUMS.txt')) -Encoding ascii -NoNewline -Value "$sha  $(Split-Path $zip -Leaf)`n"
```

公开发行附件为安装 ZIP 和它的 SHA-256 校验文件。安装 ZIP 仅含修改器、加载器、安装脚本和许可。

## 构建并启动隔离包

需要本机拥有并安装的 Steam 游戏，以及官方 Ultimate ASI Loader **v10.0.0** 的 `Ultimate-ASI-Loader_x64.zip`。把未解压的 ZIP 放在下例位置或调整变量。脚本要求 ZIP 内仅有 `dinput8.dll`，并核对 SHA-256：`3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7`。

以下命令在 PowerShell 中执行。实际启动前退出所有 `DnDG_64.exe` 进程，保留 Steam 登录状态。先预检，再运行 smoke；有错误就停，不要跳过校验直接执行副本。

```powershell
$root = (Get-Location).Path
$game = Read-Host '请输入 Steam 游戏目录'
$loader = Join-Path $root 'output\dndg-asi-local\Ultimate-ASI-Loader_x64.zip'
$dest = Join-Path $root 'output\dndg-asi-local\package'
$tool = Join-Path $root 'DnDG\tools\native_bundle.py'
$asi = Join-Path $root 'build\DnDG\release\GlossTrainer.asi'
$bundle = Join-Path $dest 'bundle'

py -3 -B $tool build --game-dir $game --output $dest --asi $asi --loader $loader
py -3 -B $tool launch --bundle $bundle --smoke --dry-run
py -3 -B $tool launch --bundle $bundle --smoke
```

`--dry-run` 校验并打印命令和用户目录，但不启动进程。 如果只检查包内原版引擎能否启动 C++ 桥接，运行 `launch --bundle $bundle --smoke --headless`；它不创建游戏窗口、不载入普通对局，不验证 D3D12 或 DXGI Hook。Smoke 使用 D3D12 Forward Mobile、1280×720 窗口和最小场景，不载入原版场景、原版资源或原版 autoload。启动器有 20 秒超时，要求进程退出码为 0，并分别核对 D3D12 后端、C++ 桥接就绪、独立用户目录和完成标记；含 `ERROR:` 时不通过。

要另行验证普通模式，先预检 `normal` 包，再去掉 `--dry-run`；不传 `--smoke` 会使用 D3D12 Forward+ 启动原版场景的隔离副本，并保留原版分辨率与缩放设置。

```powershell
py -3 -B $tool launch --bundle $bundle --dry-run
py -3 -B $tool launch --bundle $bundle
```

### 安全边界

- 构建在 Steam 安装以外生成 `bundle/normal` 和 `bundle/smoke`，不改写安装中的 EXE、PCK、Steam DLL 或原版存档。两个包均包含原版 EXE 和 Steam DLL 的副本，普通包还使用含原版资源的派生 PCK。**不要上传、发布或转发生成包。** Steam 直装请使用发行包脚本，派生 PCK 只用于隔离验证。
- 启动前复核原版文件和副本的哈希、目录结构、隔离设置及 GDExtension 模块路径。包内描述文件记录 ASI 的绝对路径，不要移动包；请换一个 `--output` 重建，脚本不会覆盖已有 `bundle/`。
- 普通包的 Godot 用户目录为 `%APPDATA%\GlossTrainer\DnDG\native\<bundle-id>`，smoke 另用其下的 `smoke` 子目录。ID 取决于输出绝对路径，不读取原版存档。修改器自己的主题、语言、快捷键和下一张牌显示设置另存于 `%LOCALAPPDATA%\GlossTrainer\DnDG\native.ini`，不是游戏存档，也不按包区分。
- Steam 校验仍适用；遇到所有权校验不通过、启动失败或跳回原版时，停止验证，不替换 Steam 文件、不尝试绕过。仅使用隔离包时，退出隔离副本后可从 Steam 启动原版。启用过 Steam 直装的用户按安装指南卸载。

## 普通模式操作（实现入口，非实战验收）

在隔离副本进入普通对局，等到轮到玩家操作后按 **Insert** 打开或收起面板。原生桥接会在面板打开时暂停场景树，并在收起时恢复由修改器施加的暂停；不会主动解除原有暂停。操作不可用时等待对应场景和回合就绪，不要把请求提交当成已经生效。

| 区域 | 操作 |
| --- | --- |
| 玩家 | 调整当前生命、护盾和筹码，点「应用」提交；也可一键回满生命。 |
| 卡牌 | 查看袖中牌和桌面牌，在可编辑时添加、替换或丢弃。锁定或遮蔽条件会限制编辑。 |
| 物品 | 分别查看幸运护符与独特筹码，在可编辑时添加、替换或移除；独特筹码不是货币余额。 |
| 下一张牌 | 打开「显示下一张牌」后收起面板；有可读牌时，左上角显示下一张牌名；鼠标悬停可看卡牌效果。不执行抽牌，不触发原版“预见”。 |
| 设置 / 关于 | 切换亮色或暗色、跟随游戏 / English / 简体中文，修改显示快捷键，或手动检查 GitHub 的 DnDG 更新。更新检查使用 DnDG 专属版本标签。 |

## 开发回归

在配置好 Debug 或 Release 后，从仓库根目录运行同配置的测试。例如 Release：

```powershell
$mode = 'release'
$evidence = Join-Path $PWD 'output\dndg-native-local-tests'
New-Item -Type Directory -Force $evidence | Out-Null
"[UI]`nDark=0`nShowNext=1`nLanguage=0`nKey=36" | Set-Content -Encoding ascii "$evidence\light.ini"
"[UI]`nShowNext=1" | Set-Content -Encoding ascii "$evidence\dark.ini"
foreach ($target in 'GlossTrainerDnDGModelTests', 'GlossTrainerDnDGUpdateTests', 'GlossTrainerDnDGOverlayTests') {
    xmake build -P DnDG $target
    if ($LASTEXITCODE) { throw "$target build failed" }
}
& ".\build\DnDG\$mode\GlossTrainerDnDGModelTests.exe"
& ".\build\DnDG\$mode\GlossTrainerDnDGUpdateTests.exe"
& ".\build\DnDG\$mode\GlossTrainerDnDGOverlayTests.exe" "$evidence\light.ini" light
& ".\build\DnDG\$mode\GlossTrainerDnDGOverlayTests.exe" "$evidence\dark.ini" dark
```

另可使用官方 Godot 4.6.3 Windows x64 引擎对原版 PCK 只读取样，验证游戏图标的 RGBA 输出。`--output` 要求选择尚不存在的目录；不会启动或操作游戏窗口。更新检查测试默认离线，加 `--live` 才调用生产 WinHTTP 路径。

```powershell
xmake build -P DnDG GlossTrainerDnDGIconProbe
$engine = 'C:\tools\Godot_v4.6.3-stable_win64.exe' # 指向本机官方引擎
py -3 -B DnDG/tools/validate_native_icons.py --engine $engine --mode $mode --output "$evidence\icons-$mode"
& ".\build\DnDG\$mode\GlossTrainerDnDGUpdateTests.exe" --live
```

如需检查真正的 D3D12 字体像素和图标图集上传，在所需配置中构建下列目标。离屏测试不创建窗口、不启动游戏，也不读取真实修改器 INI。重复目标测试时选择新的证据目录；不传 `--mode` 时需要预先构建 Debug 和 Release。

```powershell
xmake build -P DnDG GlossTrainerDnDGDx12CaptureTests
py -3 -B DnDG/tools/validate_native_dx12.py --mode $mode --output "$evidence\dx12-$mode"
```

ImGui 夹具直接提供模拟快照和指针事件，不接管系统输入，也不读取或写入用户自己的 INI。它验证控件和提交给 ImGui 的文本，不证明真实对局写入、D3D12 提交或高分辨率光栅质量。按层次验证的结果和实战核对表见[原生验证记录](verification.md#02-原生-asi开发验证)。
