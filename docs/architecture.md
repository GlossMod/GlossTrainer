# 框架结构

`BlackMythWukong/src` 按职责拆分：

| 模块 | 职责 |
| --- | --- |
| `main.cpp` | 最小 DllMain、幂等 `InitializeASI`、启动失败回退 |
| `graphics.cpp` | MinHook 管理、DXGI 工厂和交换链发现、主窗口选择、后端生命周期 |
| `renderer.hpp` / `renderer_dx11.cpp` / `renderer_dx12.cpp` | 统一绘制和尺寸变化接口，以及各 API 的资源、状态和 GPU 同步 |
| `input.cpp` | HWND 消息转发、Insert、raw input、ClipCursor / SetCursorPos / GetRawInputData Hook |
| `overlay.cpp` | ImGui 界面、中文字体、主题、DPI、配置读写 |
| `runtime.cpp` | ASI 路径、模块信息、共享锁、文件日志及轮换 |

Loader 在 `LoadLibrary` 返回后调用导出入口。初始化同步安装图形导出 Hook，不创建临时窗口、工厂或设备。真实工厂返回前接入交换链创建方法，真实交换链返回前接入 Present / Present1 和 ResizeBuffers / ResizeBuffers1。图形资源仅在主窗口实际 Present 时创建。

DX12 仅使用创建该交换链时传入且设备身份一致的 direct command queue。ResizeBuffers1 改变队列后重新验证；无法证明关联时记录日志并跳过绘制。每个 backbuffer 有独立 allocator 和 fence 值，覆盖层使用自己的命令列表、RTV/SRV 描述符堆。尺寸变化前等待提交完成；超时保留仍可能被 GPU 使用的资源，并让宿主稍后重试。进程退出不在 DllMain/静态析构中等待 GPU。

DX11 使用 ImGui 后端恢复其管线状态，另外保存和恢复宿主的 render target、depth target 与输出 UAV。后端初始化失败不阻止原始 Present。

窗口消息和渲染通过 `OverlayMutex` 串行访问 ImGui；输入跟踪状态另有每 HWND 锁。调用原游戏 WndProc 前释放这些锁。隐藏或失焦会恢复光标限位；raw 输入使用真实句柄转发先前已交给游戏的释放包，鼠标同包的移动、按下和滚轮在同步 GetRawInputData 路径中过滤。不会接管游戏的 raw device 注册。

目前不实现数值扫描、游戏地址、内存写入或运行中卸载。后续功能可从独立功能模块接入 UI；应保持游戏相关逻辑与图形后端分离。
