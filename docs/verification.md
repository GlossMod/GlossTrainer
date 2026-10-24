# 本机验证记录

日期：2026-10-24。Windows 11 x64、Visual Studio 2022、RTX 5080，游戏 Steam build 21393610。

## 构建与宿主

最终 x64 Debug、Release 的 ASI 和宿主均构建成功。`dumpbin` 确认两份 ASI 为 x64、导出 `InitializeASI`，依赖为 Windows 与 MSVC 运行库，未动态依赖外部 ImGui/MinHook DLL。Debug 需要开发机的调试运行库，部署使用 Release。PE 检查见 [Debug](evidence/pe-debug.txt)、[Release](evidence/pe-release.txt)。

最终 Release ASI SHA-256：

```text
9EEA8C1D8C06DF987651D6B92D81681A11A5F243936E9D8A7C3898D09666A79B
```

宿主使用真实 DXGI / D3D 设备，通过同一个 ASI 的 `InitializeASI()` 和实际 MinHook 链路运行，不是独立 ImGui 演示程序。

| 最终 Release 宿主 | 结果 | 证据 |
| --- | --- | --- |
| DX11 | PASS，3383 帧；显示/隐藏、尺寸变化、最小化恢复、主链选择、legacy 按住键释放全部通过 | [日志](evidence/host-dx11-release.log) |
| DX12 | PASS，3375 帧；同上，并在无关队列/辅助链先创建时正确选择主链真实队列 | [日志](evidence/host-dx12-release.log) |
| DX12 late-init | PASS，1194 次成功 Present；队列关联未知时记录错误，Insert 前后主 backbuffer 差异为 0 | [日志](evidence/host-dx12-late-release.log) |

标准测试均重复调用初始化入口，主链开菜单时有 563591 个像素变化，隐藏后与基线差异为 0。截图来自 GPU backbuffer readback，保存在 `build/test-output/*-final`。Debug 的 DX11 与 late-init 也通过过对应自动序列；最终 Debug 完成重建，正式最终宿主结论以上表 Release 为准。

安装器的 18 个回归场景已分别在 Windows PowerShell 5.1、PowerShell 7 通过，涵盖首次安装、重复安装、损坏缓存、文件冲突、清单校验、运行进程拦截、预览、卸载与保留其他插件等行为。可用 `Scripts/test-install.ps1` 复现。

## 交互检查

在 DX11 宿主中通过 Computer Use 实际输入验证：

- 启动菜单隐藏，短暂显示欢迎提示；Insert 显示菜单。
- 简体中文、概览状态、修改功能占位页、设置页正常显示。
- 暗色 / 亮色切换，界面缩放到 1.07，拖动窗口后 `ui.ini` 保存主题、缩放和坐标。
- 退出后重启宿主，亮色、1.07 缩放及窗口坐标仍保留；[实际截图](../output/ui-light.png) 来自最终构建的 GPU 读回。
- 关闭按钮只隐藏菜单，再按 Insert 可以重开。
- Alt+Tab 失焦时菜单隐藏，正常关闭宿主退出。

这组检查不代表游戏中的输入验收。完整 raw-only 按住输入、GetRawInputBuffer、延迟读取 raw handle、DirectInput/XInput/异步轮询路径，以及跨显示器 DPI 切换尚未实测。当前 raw 处理覆盖同步 WM_INPUT / GetRawInputData 路径；已交给游戏的 raw 按下状态会在真实释放包到来时释放。

未强制触发设备移除、GPU fence 超时或后端对象创建失败；这些防御路径已实现，但未经过故障注入验证。缺少 DX12 队列关联的初始化回退由专门宿主模式验证。

## 真实游戏

已在 `E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64` 安装固定 Loader、`version.ini` 和 `scripts\GlossTrainer.asi`，保留原插件。`version.log` 及 trainer 日志确认 Loader 自动加载 ASI，并调用初始化入口。

| 启动模式 | 实际结果 | 验收结论 |
| --- | --- | --- |
| 安装前 `-dx12` | 游戏在 FAsyncLoadingThread 访问违规后退出 | 现有环境基线失败 |
| 安装后 `-dx12` | DX12 交换链、中文 ImGui、真实命令队列和后端初始化成功；随后游戏仍在同一资源加载线程崩溃 | 自动加载和后端初始化通过；游戏内菜单未验收 |
| 安装后 `-dx11` | 崩溃记录的 RHI 为 D3D11；捕获交换链前发生空地址访问违规 | 未进入可操作画面，不能标记游戏内通过，也没有证据将其称为明确拒绝 DX11 |

安装前后的 DX12 `PCallStackHash` 完全相同，均为 `F2A524F9359A1D73069B2179CE56656A912EF0C4`，顶部地址为 `b1-Win64-Shipping.exe + 0xCC63F52`。这证明同一崩溃在安装前已存在；尚未确定原始原因，也不证明全部插件的运行兼容性。经过提取的异常、崩溃线程和栈信息见 [基线证据](evidence/baseline-dx12-crash.json)、[安装后证据](evidence/installed-dx12-crash.json)。[Trainer 游戏日志](evidence/trainer-game-attempts.log) 记录了两次模式尝试。

18:54 更新并安装最终 Release 后再次尝试 DX12：ASI 哈希与上述构建一致，18:55:04 后端初始化成功，18:55:12 仍出现相同崩溃栈。[最终加载日志](evidence/trainer-game-final.log)、[最终崩溃记录](evidence/final-dx12-crash.json) 已保存。验证结束时游戏与测试宿主均已退出。

首轮安装后的早期初始化曾停滞。移除主动创建 DXGI 工厂、窗口及 D3D11 设备的启动探针后，游戏已能继续至真实 DX12 后端初始化。最终方案只同步安装发现 Hook，在捕获真实交换链后创建资源。

原文件 SHA-256 在安装后保持不变：

```text
dwmapi.dll         6AEA3C390442053FA64F306D7AC6ECDA6F82C8FF1B7AC909920F94636D96E1AB
wukong_minimap.dll 63903ED2CA7DACD9516090141F7C5015AFB46D5B9AE9FC3A8A738B769EE047B7
```

当前不能完成游戏里的反复开关菜单、按住输入切换、最小化恢复、退出和插件功能共存验收；需先恢复该游戏环境能正常启动，再继续这些项目。此次未改动原有插件配置或游戏存档。
