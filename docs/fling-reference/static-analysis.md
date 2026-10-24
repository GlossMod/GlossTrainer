# FLiNG/3DMGAME 修改器静态清单

本文只记录归档文件、随包说明和 PE 静态检查结果。参考 EXE 未启动，也未连接或读取游戏进程。快捷键/功能名称依据随包说明核对；截图数值记录的是截图当时显示的值，不能视作出厂默认值。逐项数据在 [features.json](features.json)。

## 文件清单与 SHA-256

归档：`data/flyy/3DMGAME-Black.Myth.Wukong.v1.0-v1.0.20.Plus.44.Trainer-FLiNG.rar`
SHA-256：`CDCFF7263E24F1EB9C83178F7806428734205FA828F969BDCE41B1F8DB7A03A1`
长度：801,436 字节。

使用 `C:\Program Files\7-Zip\7z.exe`（24.08）列出成员、逐项检查相对路径没有根路径或 `..` 逃逸后，解压到此前不存在的 `data/flyy/extracted`。7-Zip 报告 `Everything is Ok`，共 4 个文件；原 RAR 未改写。

| 文件 | 长度 | SHA-256 |
|---|---:|---|
| `Black Myth Wukong v1.0-v1.0.20 Plus 44 Trainer.exe` | 1,697,792 | `E81321810A9F5058ED73EDCE827453442D603CA89A2DE4939CE55B83E7D7FF63` |
| `说明.txt` | 3,068 | `44064BDB029B49B2F118487517DD2C89C660A4E4FD344E5B57924FD22BB03687` |
| `www.3dmgame.com.url` | 122 | `03DF27E82600098C34C413CC2E45B43638D3AC33666960CFBD913F1C3F9A0B11` |
| `2025年全部热门单机游戏及汉化下载.url` | 147 | `92929D971A13A5FA4650D272CCCE60E8D54DA2B1D5B4BAA8304A7BA6F8E97C1C` |

`说明.txt` 有 UTF-16LE BOM。它列出 44 个快捷键功能，和截图中的 44 项名称/快捷键逐项吻合；说明没有列出数值默认值或提示文案。两个 `.url` 文件是 3DMGAME 站点链接，不是功能资料。

## PE 静态检查

检查目标：`data/flyy/extracted/Black Myth Wukong v1.0-v1.0.20 Plus 44 Trainer.exe`。使用 Visual Studio 2022 `dumpbin /headers`、`dumpbin /imports` 和只读 ASCII/UTF-16LE strings 扫描。

**可直接确认的事实：**

- 文件是 Windows PE32+ x64 GUI EXE（Machine `8664`），6 个常规命名节：`.text`、`.rdata`、`.data`、`.pdata`、`.rsrc`、`.reloc`。COM Descriptor 目录为零；导入表包含 `mscoree.dll` 的 `CLRCreateInstance` 与 `CorBindToRuntime`，二进制字符串也提到 .NET Framework 3.5/4.*。因此能确认它带有 CLR 互操作/运行时相关组件，但这些证据不足以将整个程序断定为 .NET 托管程序集。
- 字符串含有 Cheat Engine Auto Assembler 风格的 `aobscanmodule`、`alloc`、`label`、`registersymbol`、`/\* Script Begins \*/` 等标识，并出现指向 `b1-Win64-Shipping.exe` 的扫描脚本文本。故静态证据支持其内置 CE 风格脚本/扫描引擎。
- 字符串还含 Unreal Engine FName/结构/函数解析相关标识和 Unreal Engine 版本提示；同时含目标进程名 `b1-Win64-Shipping.exe`。没有发现 Mono 相关字符串。
- 导入包含常见 Windows 系统库与线程、文件、映射、权限、窗口消息等 API；没有独立 `.ct` 文件，也没有随包 Lua/Python 脚本。
- 从 EXE 的 `.rdata` 只读摘录出 17 段完整 ASCII `[ENABLE]` 至对应 `[DISABLE]` Auto Assembler 脚本；每段原始文件偏移、映射 RVA/首选 VA 和 SHA-256 见 `data/flyy/analysis/scripts/offsets.json`。摘录内容在 `data/flyy/analysis/scripts/aa-scripts-all.txt`，每段也各自保存为单独文件。没有找到 UTF-16LE 编码的 AA 脚本块标记。此摘录用于静态映射，不代表执行或验证了脚本。

**推断与边界：**

- 节名、导入表和整体布局正常，未见常见 `UPX0`/`UPX1` 节名。这只能说明没有从这些静态特征识别出常见 UPX 壳，不能证明二进制未加壳或没有运行时解包。`.rsrc` 熵约 7.584 bits/byte，可能来自压缩资源，单凭熵不能判断打包状态。
- AOB 与 UE 解析相关标识说明修改器可能采用特征扫描并解析 Unreal 对象/属性，但静态字符串不证明每项功能实际可用、支持哪些游戏补丁、实现路径或运行效果。
- `.NET` 字符串和 CLR 导入说明存在相关组件/探测逻辑；COM Descriptor 为零，不能据此确定具体 CLR 组件的用途或托管代码比例。
- 截图里的感叹号图标未显示悬停提示正文；`features.json` 保留为未知，没有从名称臆测提示内容。

## 可复现检查

```powershell
& 'C:\Program Files\7-Zip\7z.exe' l -slt 'E:\GitHub\GlossMod\GlossTrainer\data\flyy\3DMGAME-Black.Myth.Wukong.v1.0-v1.0.20.Plus.44.Trainer-FLiNG.rar'
Get-FileHash 'E:\GitHub\GlossMod\GlossTrainer\data\flyy\3DMGAME-Black.Myth.Wukong.v1.0-v1.0.20.Plus.44.Trainer-FLiNG.rar' -Algorithm SHA256
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe' /headers 'E:\GitHub\GlossMod\GlossTrainer\data\flyy\extracted\Black Myth Wukong v1.0-v1.0.20 Plus 44 Trainer.exe'
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe' /imports 'E:\GitHub\GlossMod\GlossTrainer\data\flyy\extracted\Black Myth Wukong v1.0-v1.0.20 Plus 44 Trainer.exe'
```
