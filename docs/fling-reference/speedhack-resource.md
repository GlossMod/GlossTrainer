# 内嵌全局游戏速度库：REMOTE/250

从 trainer 的 PE 资源树只读提取 `REMOTE/250/1033`，原始资源长度 142,848 字节，保存为 `data/flyy/analysis/resources/REMOTE-250.dll`。提取文件 SHA-256：`BE24440E4BD3C07729E931533B9B097DF945F57B876C28C0D8E0BBB743645324`。trainer 本身的 SHA-256 记录在 `speedhack-resource.json`。两个 EXE/DLL 均未运行。

`dumpbin /headers` 将资源识别为 x64 PE32+ DLL，Machine `8664`，7 个节，首选基址 `0x180000000`；PE 时间戳 `0x6608FE30`（dumpbin 显示 2024-03-31 14:09:52）。DLL 导出表内部名称是 `TrSpeedHack_x64.dll`，有 5 个仅按 ordinal 导出的函数（1 至 5），没有具名导出。

DLL 直接导入 `QueryPerformanceCounter`、`GetTickCount`、`timeGetTime`，并含 `GetTickCount64` 与 `RtlQueryPerformanceCounter` 字符串。它还导入 `VirtualAlloc`、`VirtualProtect`、`VirtualQuery`、线程枚举、`SuspendThread`/`ResumeThread`、线程上下文读写和 `FlushInstructionCache` 等 API。该组合与运行时计时钩子/速度控制实现相符。

trainer 中可见 UTF-16LE 文案 `Ctrl+Num 5 - Set Game Speed`，类名字符串 `TrRemote`、`TrSpeedHack`，以及 `Trainer Speedhack Library`、`TrSpeedHack.dll` 和“无法启动远程线程”的错误文本。由此推断 Ctrl+Num 5 的全局游戏速度由该内嵌 DLL 提供，而脚本中的 `speed_f` / `playerspeed_f` 属于角色移动速度变体。`SetSpeed` 没有在 trainer 或 DLL 的 ASCII/UTF-16LE 字符串中发现；5 个 ordinal 的具体职责、trainer 如何传 multiplier 仍未确认。

完整资源路径、导入/导出、字符串偏移和事实/推断界限见 [speedhack-resource.json](speedhack-resource.json)。
