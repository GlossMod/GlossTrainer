# 模块与生命周期

当前实现为 v1.0.0，目录包含原有 44 项和新增 20 项，并提供物品与传送工具。v0.3.3 修复中文输入法接入，保留按场景折叠的传送地标和界面留白；原有版本的游戏连接和用户反馈保留在 [验证记录](verification.md)，新增功能的合成回归与图形宿主结果单独记录。

`BlackMythWukong/src` 按职责拆分：

| 模块 | 职责 |
| --- | --- |
| `main.cpp` | 最小 DllMain、幂等 `InitializeASI`、启动失败回退 |
| `graphics.cpp` | MinHook 管理、DXGI 工厂和交换链发现、主窗口选择、后端生命周期 |
| `renderer.hpp` / `renderer_dx11.cpp` / `renderer_dx12.cpp` | 统一绘制和尺寸变化接口，以及各 API 的资源、状态和 GPU 同步 |
| `input.cpp` | HWND 消息转发、自定义菜单键、功能快捷键、raw input、光标与 GetRawInputData Hook |
| `ime.hpp/.cpp` | 文本焦点请求、窗口线程 IMM 上下文关联与恢复、候选位置、文本输入期间键盘 legacy 消息 |
| `overlay.cpp` | 64 项紧凑列表、物品与传送页、快捷键录制、中文字体、主题、DPI、外观配置 |
| `features.hpp/.cpp` | 功能定义、分组、参数暂存、自定义快捷键及持久化、状态及后端回调契约 |
| `gameplay/backend.cpp` | 特征解析、功能状态、原生功能 Hook、动作排队、QPC 速度控制 |
| `gameplay/midhook.hpp/.cpp` | x64 指令中间 Hook、寄存器与 CPU 状态保存、安全本进程读写、特征扫描 |
| `gameplay/resolver.hpp/.cpp` | 只读验证 Mono AOT 泛型链，定位真实 List getter 调用探针 |
| `gameplay/attributes.cpp` | 玩家代际识别、浮点 getter 覆盖、资源事件及 base 属性恢复队列 |
| `gameplay/extensions.cpp` | 游戏线程扩展请求、玩家／控制器／世界代际、状态与属性恢复、物品、书签和传送 |
| `gameplay/engine_bridge.cpp` | 验证 USharp 原生注册、反射参数、对象有效性、游戏线程及 UObject 调用 |
| `gameplay/aot.cpp` | 根据程序集 MVID、Mono AOT 表布局和方法指纹解析实际 native 入口 |
| `gameplay/additional_hooks.cpp` | 物品消耗、丹药／法宝计时与真实周目字段的局部 Hook |
| `gameplay/console_bridge.cpp` | 等待游戏自然注册白名单指令，并由真实原生控制台回调派发 |
| `runtime.cpp` | ASI 路径、模块信息、共享锁、文件日志及轮换 |

Loader 在 `LoadLibrary` 返回后调用导出入口。初始化同步安装图形导出 Hook，不创建临时窗口、工厂或设备。真实工厂返回前接入交换链创建方法，真实交换链返回前接入 Present / Present1 和 ResizeBuffers / ResizeBuffers1。图形资源仅在主窗口实际 Present 时创建。

DX12 仅使用创建该交换链时传入且设备身份一致的 direct command queue。ResizeBuffers1 改变队列后重新验证；无法证明关联时记录日志并跳过绘制。每个 backbuffer 有独立 allocator 和 fence 值，覆盖层使用自己的命令列表、RTV/SRV 描述符堆。尺寸变化前等待提交完成；超时保留仍可能被 GPU 使用的资源，并让宿主稍后重试。进程退出不在 DllMain/静态析构中等待 GPU。

DX11 使用 ImGui 后端恢复其管线状态，另外保存和恢复宿主的 render target、depth target 与输出 UAV。后端初始化失败不阻止原始 Present。

窗口消息和渲染通过 `OverlayMutex` 串行访问 ImGui；输入跟踪状态另有每 HWND 锁。调用原游戏 WndProc 前释放这些锁。隐藏或失焦会恢复光标限位；raw 输入使用真实句柄转发先前已交给游戏的释放包，鼠标同包的移动、按下和滚轮在同步 GetRawInputData 路径中过滤。鼠标 raw device 注册保持原状。

ImGui 的 `Platform_SetImeDataFn` 仅复制文本焦点、客户区光标位置与行高并向 HWND 发消息。窗口线程创建菜单专用 HIMC，保存原关联（包括 NULL），结束文本输入、隐藏或失焦时取消未完成组合并恢复；只销毁自身创建且已脱离 HWND 的上下文。IMM 和默认 IME 消息过程均不持有上述锁。菜单接管期间，IME 组合 / 字符消息只走一次 `DefWindowProcW → WM_CHAR → ImGui` 路径，绕开游戏对 IME 的处理；未接管时交原窗口过程，避免 ImGui 1.92.9 后端的默认组合处理再执行一次。依据：[ImGui 平台实现](https://github.com/ocornut/imgui/blob/v1.92.9/imgui.cpp)、[Win32 后端](https://github.com/ocornut/imgui/blob/v1.92.9/backends/imgui_impl_win32.cpp)、[ImmAssociateContext](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immassociatecontext)。

若键盘注册使用 `RIDEV_NOLEGACY`，仅在文本输入期间临时解除该键盘记录的 NOLEGACY / APPKEYS，保留目标和其他标志，并同步路由防止 raw / legacy 重复投递。结束时仅当当前注册仍与临时记录相同才还原，保留游戏或其他插件后续作出的注册变化；不重写其他设备注册。

## 功能层

图形 Hook 安装后，功能层启动独立初始化线程。仅在 `b1-Win64-Shipping.exe` 中扫描可执行区域；静态特征必须唯一。原有 12 项独立处理器、32 项浮点属性处理器和新增 20 项扩展分别报告支持状态，未找到入口、玩家未就绪和执行失败均反馈到列表行。

`features` 的输入值只暂存于进程内。UI 或初次快捷键按下调用 `SetEnabled` / `Apply` 后，后端才更新原子开关、参数或动作队列；查询状态不修改游戏。回调在功能模型锁之外执行。进入文本输入框时屏蔽功能快捷键，当前菜单组合独立控制菜单，不受功能快捷键总开关影响。

功能快捷键默认为空，由玩家在列表中录制并保存到 `hotkeys.ini`。录制期间消费按键，Esc 取消、Backspace / Delete 清除；重复组合、纯修饰键和保留组合被拒绝。菜单隐藏或失焦时取消录制。快捷键文件写入与录制取消串行处理。

界面开关默认 Insert，保存于同文件的 `[Menu] Toggle=key,modifiers`；加载时先确定菜单组合，再加载功能绑定，空值或非法菜单配置回退 Insert，冲突的功能绑定跳过。菜单录制不能清空，与功能绑定双向检查精确组合冲突；输入按“录制、菜单、功能”顺序分发。legacy 与 raw-only 使用同一动作判断，分别跟踪按下与已消费状态，消费重复消息、释放以及相应扫描码派生的字符，防止录制当次关闭菜单或字母菜单键泄漏进文本框。`--menu-key` 宿主参数可核对自定义单键的实际图形 Hook 路径。

设置页按 [menu-key-v1 亮暗原型](../../output/imagegen/menu-key-v1.png) 拆分外观与快捷键卡片，保留内边距和录制模态框；原型来自 `custom-imagegen`、`model_providers.custom`、固定模型 `gpt-image-2.5`，完整提示词见 [prompt](../../output/imagegen/menu-key-v1.prompt.txt)。底部键帽和启动提示都从当前菜单绑定读取。

界面沿用 [compact-list-v1 原型](../../output/imagegen/compact-list-v1.png)（`custom-imagegen`、`model_providers.custom`、固定 `gpt-image-2.5`；[原提示词](../../output/imagegen/compact-list-v1.prompt.txt)）。基础窗口 960×680，表格开启 `PadOuterX`，单元格 14×8，clipper 与实际行共用动态行高，至少 44px。默认窗口内边距 14×10 供提示框 / 下拉框使用；手工定位的主窗口局部覆盖为 0，卡片与模态框继续显式设置内边距，避免相互影响。

传送页采用先生成并查看的 [travel-groups-v1 原型](../../output/imagegen/travel-groups-v1.png)（`model_providers.custom` / `gpt-image-2.5`；[提示词](../../output/imagegen/travel-groups-v1.prompt.txt)）。地标按 scene ID 分组并保持场景目录与组内原顺序，使用单一滚动区和可变高度内容；普通与搜索折叠状态分别保存在 UI 内存中。搜索文字变化时展开匹配组，清空时切回普通状态；全部展开 / 折叠只作用于当前筛选可见的组，不发出传送请求。

扩展请求由 `OnEndFrame` 在游戏线程执行，单次排队、30 秒过期，绑定玩家、控制器及世界代际。对象或世界变化取消旧请求并恢复旧状态；旧世界暂停或时间倍率恢复失败时逐帧重试。组件换代后关闭会恢复该功能拥有的所有仍有效旧组件；镜头距离每帧重设，并保存游戏自身最新基值用于关闭恢复。原生自由相机通过 debug 控制器所属的 CheatManager 关闭，验证原控制器恢复后清理本次创建的相机对象，关闭不依赖 debug 控制器拥有 Pawn。

隐藏游戏界面作用于顶层 UMG UserWidget，包括 HUD 和游戏菜单。使用引擎 FWeakObjectPtr 的 index/serial 验证同一对象，定期发现新根控件，每帧隐藏已跟踪控件；不覆盖本来 Collapsed/Hidden 的对象。关闭仅恢复仍被隐藏的同一控件；游戏改为其他状态时采用最新观察值。轮询无法区分游戏在已经 Hidden 的控件上再次主动写 Hidden，不能视为捕获了每次显示状态写入。恢复失败的有效控件继续重试，弱引用失效则丢弃。

四项动作（灵蕴、点击物品数量、增加经验、灵光点）只提交请求，由对应游戏数据更新 Hook 消费。请求的时间、数值和状态由同一互斥事务保护，消费与超时清理通过 `try_lock` 获取；锁忙时跳过该次尝试，不阻塞游戏回调。超时判断还检查采样时间不早于入队时间，防止旧时间下溢，以及旧请求的清理覆盖刚提交的新请求。请求 30 秒未消费即取消；“已排队”不代表已写入。修改数量、货币、经验和点数可能随后由游戏保存，关闭菜单不会撤销这些数据。

全局游戏速度单独 Hook `QueryPerformanceCounter`，在连续计数基础上缩放新增计时量；关闭恢复 1 倍增长率，不倒退计数。作用域是整个游戏进程中调用该时钟的组件，不能等同于只修改 `World Time Dilation`。菜单、其他插件及依赖该时钟的系统也可能受到影响。

## Mono AOT 属性路径

从真实玩家查询的虚表槽获取包装方法，验证 `GetFloatValue -> GetValue -> generic context[0x18] -> GSBindList<float>.get_Item` 的完整机器码及寄存器转发，PLT 跳转最多四级，所有代码目标必须位于可执行页。泛型槽尚未初始化时返回失败并稍后重试；安装线程不会主动调用托管方法。实际只读调用图见 [现场包装链](../../output/fling-reference/live-attribute-callgraph.json) 与 [泛型 getter](../../output/fling-reference/live-attribute-getter.json)。

GSBindList 的调用探针采集真正的 `List<float>.get_Item`。随后只在该方法内寻找唯一浮点数组 getter 特征，避免全局 17 个相似 getter 的歧义。玩家身份需线程局部调用标记、原始 trampoline 返回地址、动态栈范围及匹配的属性枚举共同确认。本轮运行时已连接该路径，证据见 [实际 List reader](../../output/fling-reference/live-list-reader.json)、[v0.2.0 运行日志](../../output/fling-reference/runtime-v0.2.0.log) 与 [v0.2.1 运行日志](../../output/fling-reference/runtime-v0.2.1.log)；这里的现场地址只用于证据定位，安装不硬编码这些地址。

属性回调使用独立的有界同步，不获取 ImGui 锁，也不分配内存。getter 在保留原指令语义后覆盖返回值；关闭战斗类覆盖后立即返回原始属性。生命、法力、气力等资源还在真实变更事件中保护；不是由 UI 线程直接写目标内存。

生命 / 法力 / 气力上限、四种抗性 base、经验 / 灵蕴加成共九项使用固定事件队列。启用时保存该玩家的底层基值，在真实气力事件中分发更新；启用期间若收到游戏自身的基值变更事件，会把该值更新为恢复基准，避免关闭时回滚正常升级、装备或增益带来的变化。关闭则排队恢复最新真实基值，等待游戏处理和回读确认。借用气力事件时保存其原目标，在同代际的下一次 getter 中补回，避免吞掉原本的气力更新。

玩家身份包含 actor、vtable 和属性数组，且要求最近三秒内真实捕获。切换角色或更换数组后清空旧快照和旧请求，避免向失效对象写入；原对象已销毁时不尝试恢复其地址。开关不跨进程保存，进程退出不在 DllMain 中拆除运行中的游戏 Hook。具体操作及局限见 [功能说明](gameplay.md)。

## 正式版检查与发布

`update.hpp/.cpp` 使用 WinHTTP 异步分页读取固定仓库的 GitHub Releases 列表，初始化图形界面后检查一次，可在设置或关于页手动重试。完整扫描最多 10 页、每页 20 项，同时限制阶段超时、总预算、响应大小和 JSON 深度，禁用重定向与认证。仅接受 `BlackMythWukong/vX.Y.Z` 通道的正式版本，并核对本仓库精确的 Release 页面、安装包名称和下载地址；不使用仓库全局 Latest。网络线程不访问 ImGui 或游戏数据，进程退出不等待网络；不在 DllMain 中联网。

只有成功确认版本更高时才要求更新。失败状态的 UI 文案固定为“检查更新失败”，不保留上次锁定状态，详细原因只写日志；完整扫描后没有本游戏正式版本时显示“暂无正式发布版本”。下载按钮仅通过系统浏览器打开已验证的 Release 页面，不下载或执行更新程序。顶栏显示 `By: 小莫`，确认新版时功能页改为有内边距的更新卡片，关于页仍可访问，也可关闭菜单返回游戏。

功能模型将强制更新状态与后端写事务串行处理，阻止新启用与应用；已启用功能请求关闭，恢复仍由原后端完成。异步游戏线程请求也须核对门控，避免已排队启用在确认新版后生效。解除更新限制不自动重启旧功能。一次性已完成的数据修改不回滚。

v0.3.4 按稳定场景 ID 与精确地标键清理开发、测试和占位目录，保留32场景、284地标和17个有地标的场景分组；不对“正常位置”等通用名称进行全局删除。原始统计、推断边界及清理规则见 [目录记录](../../output/xxj-reference/catalog-data.md)。

界面先生成并查看 [release-ui-v1 原型](../../output/imagegen/release-ui-v1.png)，沿用现有金色强调、亮暗主题和卡片留白；来源为 `custom-imagegen`、`model_providers.custom` / `gpt-image-2.5`，[完整提示词](../../output/imagegen/release-ui-v1.prompt.txt)。原型仅用于设计，所示版本数据不是在线 Release 验证。

正式标签触发 GitHub Actions 的两种配置构建和回归，全部通过后由最小写权限作业发布 ZIP 与 SHA256SUMS。ZIP 使用明确文件清单；默认中文 `README.md` 和英文 `readme_en.md` 分别来自注册表指定的游戏发行模板，两份均内嵌完整项目及依赖许可。根 README 描述整个项目。生成时检查 x64 PE、InitializeASI 导出、许可和版本，记录有效载荷哈希。流程见 [发布说明](../../docs/releasing.md)。
