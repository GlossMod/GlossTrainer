# PR #1：PBZ 每帧开销优化案例

在复用本项目已知热点、评审同类改动或需要解释性能数字时读取。这里记录固定提交的机制与证据边界，不代表其他游戏或当前所有构建已达到同样结果。

## 来源与复查入口

- [PR #1：优化影之刃零运行性能并修正药品补满](https://github.com/GlossMod/GlossTrainer/pull/1)
- Base：`c50851bbf3699ce8f3c26ef18a761851077d6773`
- Head：`14f71abf9ce29ba6aa6391d02bb916404e1899d4`
- [Head 核心实现](https://github.com/GlossMod/GlossTrainer/blob/14f71abf9ce29ba6aa6391d02bb916404e1899d4/PhantomBladeZero/src/gameplay/native_adapter.cpp)
- [Head 验证记录](https://github.com/GlossMod/GlossTrainer/blob/14f71abf9ce29ba6aa6391d02bb916404e1899d4/PhantomBladeZero/docs/verification.md)
- [VirtualQuery 官方语义](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery)

在仓库根目录可只读复查：

```powershell
git diff c50851bbf3699ce8f3c26ef18a761851077d6773 14f71abf9ce29ba6aa6391d02bb916404e1899d4 -- PhantomBladeZero/src
git show 14f71abf9ce29ba6aa6391d02bb916404e1899d4:PhantomBladeZero/src/gameplay/native_adapter.cpp
git show 14f71abf9ce29ba6aa6391d02bb916404e1899d4:PhantomBladeZero/validation/performance_tests.cpp
```

行号仅指上述 head；当前源码以函数名检索为准。案例没有要求切换分支或还原工作区。

## 为什么全部功能关闭仍然慢

旧版的必经调用链：

```text
HookActorProcessEvent
  → 原游戏 ProcessEvent
  → 判断 ReceiveTick
  → CurrentContext
      → 验证 world、instance、player、controller、pawn
      → ObjectValid
          → Executable(虚表中的函数地址)
              → VirtualQuery
  → 若为本地 controller 则 Tick
      → EnsureCheat 等进一步对象验证
```

`ObjectValid`（head `native_adapter.cpp:226`）除了标志与名称，也验证虚表函数地址是否可执行。旧页缓存只在一个同步 Tick 内有效，帧间清空；Tick 前的 `CurrentContext` 不受该缓存覆盖。因此，关闭功能没有关闭基础对象检查。

`VirtualQuery` 返回从查询位置起属性相同的连续页区域。官方文档描述它检查后续页直到区域结束或属性不同，不能把它当成永远廉价的单地址布尔判断。PR 的本机合成基准记录：256 MiB 约 1.2 ms，512 MiB 约 3 ms；这些数字没有直接测量 PBZ.exe，也不是所有系统的通用耗时。

新版 `Executable`（`:154`）先查 `ExecutableImageRanges`：初始化读取主模块 PE 可执行节，只比较地址是否落入已捕获范围，命中便不调用 `VirtualQuery`。模块外地址仍保留原查询与单 Tick 页缓存。关键实现位于 `src/gameplay/image_ranges.hpp`，初始化在 `native_adapter.cpp:1182`。

这个快路径依赖主模块代码节及其保护稳定；节头的可执行标记不代表实时页保护。动态卸载模块、保护变化或未知地址不能直接套用。基于调用频率与开销，这最可能是数量级提升的主因，但缺少逐项关闭优化的对照实验，无法给出贡献率。

## 其余改动及适用边界

| 改动 | 源码入口 | 减少的工作及条件 |
| --- | --- | --- |
| 状态回读分帧与降频 | `native_adapter.cpp:644` 的 `StatusTick`；`status_schedule.hpp` | 原每 250 ms 集中回读，改为每帧一片；菜单打开或有待恢复开关时连续执行，空闲在整轮结束后等 2 秒 |
| 反射查询缓存 | `FindProperty`、`FindFunction`；`reflection_cache.hpp` | 命中时避免再次遍历成员链与继承链；检查 owner index 与成员名称，未命中一秒后过期，会话重绑定清空 |
| 缺失名称增量扫描 | `native_adapter.cpp:496` 的 `FindExistingName`；`missing_name_cache.hpp` | 名称池增长后只搜索新增 block/cursor 数据，复用线程局部缓冲；读取失败不记作完整扫描，依赖名称池追加语义 |
| 减少字符串生成 | `NameText`、`NameIs`、`ObjectName`、`Context::SameKey` | 名称比较复用缓存文本，返回非拥有视图；会话比较直接比原字段，仅需要发布会话键时格式化字符串 |
| 空闲路径避锁 | `backend.cpp` 的 `IsReady`、`ModelChanged`、`Pump` | 就绪查询读原子提示，无请求且无恢复时跳过锁；实际模型仍受 mutex 保护 |
| 按模型变化取快照 | `overlay.cpp:2145`；`backend.hpp` 的 `Revision` | 版本变化才取带锁快照；不可变物品集合共享持有，派生分类和筛选只在发布或条件变化后重建 |
| 后台缓存写入 | `native_items.inc:140` | 复制拥有所有权的文本行后后台序列化写盘；串行保存并跳过尚未执行的旧任务，模块存活是线程安全前提 |
| 描述重格式化分批 | `native_items.inc:754` | 输入标签变化后，每帧约 1 ms、最多 64 行，完成再发布；该预算不能抢占单行处理 |
| 关闭移动维护时提前返回 | `native_movement.inc` 的 `MovementTick` | 无需维护玩家时间倍率时，跳过额外上下文检查；其余功能维护仍保留 |
| 完美闪避契约分层 | `native_combat.inc` 的 `DodgeContractCurrent` | 一秒内轻量核对对象身份，变化或到期时完整复验；仅影响启用该功能的场景 |

注意两个容易误传的细节：

- PR 描述写 18 个切片，head 实际 `ToggleSliceCount = 1 + 8 + 4 + 3 = 16`，加统计切片共 **17**。应从代码计算，不能照抄描述。
- 空闲等待发生在整轮完成后，状态更新延迟是“两秒量级加分片时间”，不是严格最多两秒。执行请求后的 `PublishToggles` 立即完整回读，并丢弃执行前收集的半轮结果。

## 数字与计时边界

PR 和验证文档转述了菜单关闭为主的游戏日志：

| 指标 | 优化前 | 优化后 |
| --- | --- | --- |
| Tick 平均 | 7–9 ms | 约 0.02 ms |
| Tick 大于 2 ms | 每次 | 每 10 秒约 0–1 次 |
| Tick 峰值 | 34–91 ms | 约 1.5–2.7 ms |
| 新增 `frame` 平均 | 未测 | 约 0.05 ms |

上述均是报告中的观察值。不同窗口峰值存在差异，不能拼成严格同场景基准。按平均值计算约快 350–450 倍，耗时下降约 99.7%–99.8%；这不是实际呈现 FPS 的倍数。

`performance_scope.hpp` 使用 `steady_clock` 聚合墙钟耗时，每 10 秒输出统计。重要边界：

- `tick` 在 `Tick` 开头开始，不包含入口 `CurrentContext`。
- `frame` 从确认事件名为 `ReceiveTick` 后开始，包含上下文读取及随后本地 Tick；原游戏 `ProcessEvent`、事件名称判断和覆盖层渲染不在其中。它不是完整游戏帧，调用次数也不能当 FPS。
- 部分性能日志在 scope 析构统计后同步写出；其自身输出耗时可落入外层区段。报告里每约 10 秒的 `frame` 约 10 ms 尖峰被怀疑与日志写盘有关，未单独测量，不应当作已证实根因。

## 验证的实际范围

文档记录 Debug/Release 构建与各 21 套测试通过。`PBZPerformanceTests` 验证缓存失效、调度与模型版本等行为，不测游戏耗时、CPU 占用或 FPS。未运行图形宿主，未测物品页打开时性能，也未在游戏里穷举回归所有功能。

文档引用原始目录 `output/pbz-performance-20261031/`；2026-11-04 整理本技能时，该目录在当前工作区不存在，原始日志与合成基准不能独立复核。未来工作若取得原始文件，应重新检查样本与场景；若仍不可得，只引用为 PR/文档记录，不宣称本次已复现。

同一 PR 的药品补满修复属于功能正确性，不用于解释性能倍数。复用优化时无需捆绑修改其他功能。
