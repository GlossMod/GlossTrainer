# 静态 AOB 匹配结果

目标是本机磁盘上的游戏 EXE；只读扫描，没有启动游戏或修改该文件。

- 文件：`E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64\b1-Win64-Shipping.exe`
- SHA-256：`cba6127df85fe6aedf5a71b398f268c8a963c7b19582c3cac087462518358e9a`
- PE：Machine `0x8664`，首选基址 `0x0000000140000000`，时间戳 `2025-12-30T10:01:08+00:00`
- 只扫描 PE execute 标志节：.shared, .xtls。`.code` 节没有 execute 标志，因此未纳入。
- 从 17 段 AA 脚本找到 16 个 AOB 调用；aa-01 没有 aobscan 调用。每个命中的 RVA、文件偏移、首选 VA 与周围 64 字节见 `signature-matches.json`。通配语法展开规则也记录在 JSON。

| AA 符号 | 模式长度 | 命中数 | RVA |
|---|---:|---:|---|
| `aobgodmode` | 27 bytes | 1 | 0x02AEF8F0 |
| `aobgetplayerpevalue` | 38 bytes | 1 | 0x028C4777 |
| `aobgetfloatattribute` | 21 bytes | 17 | 0x00B5AEC2, 0x010EDBB2, 0x01A23162, 0x01A9683D, 0x034B589D, 0x0357CE32, 0x0501A26D, 0x05610F2D, 0x05E0864D, 0x06AEA0F2, 0x07F2050D, 0x08BE378D, 0x08C17022, 0x094F356D, 0x0983CB8B, 0x09863C7D, 0x0A30D3FD |
| `aobgodmode` | 39 bytes | 0 | — |
| `aobskillcd` | 38 bytes | 1 | 0x02BEEC38 |
| `aobattributefloatchanges` | 32 bytes | 1 | 0x02AEA27F |
| `aobclickeditem` | 17 bytes | 1 | 0x01651383 |
| `aobsoul` | 21 bytes | 1 | 0x02E6EE3F |
| `aobplayerpawn` | 29 bytes | 1 | 0x0E7D7976 |
| `aobskillpoints` | 25 bytes | 1 | 0x0555F43B |
| `aobspeed` | 23 bytes | 1 | 0x0E36EBF0 |
| `aobplayerspeed` | 37 bytes | 1 | 0x0E378B55 |
| `aobjumpheight` | 25 bytes | 1 | 0x0E378411 |
| `aobspeed` | 19 bytes | 0 | — |
| `aobdropcheck` | 39 bytes | 1 | 0x02E70D70 |
| `aobrelics` | 22 bytes | 1 | 0x01584FC9 |

## aa06 的 s2 捕获范围

`s2.1.*.0x30.0x40` 按“一字节捕获，值在 0x30..0x40”解释后，完整 38 字节模式唯一命中 RVA `0x02BEEC38`，捕获 `s2=0x30`、`s1=0xA8`。`aobskillcd+0x1D` 为字节偏移 29，正好指向匹配尾部 `F3 0F 10 4D A8`，与新内存代码先恢复该指令一致。另一个未加范围约束的候选在 `0x02BF7318`，捕获字节为 `0x10`，超出推断范围。仅用尾部 `F3 0F 10 4D ?? F3 0F 5C C1` 会命中 575 次，区分力不足。范围语义根据字符串形式和命中/补丁偏移对齐作出推断；未运行 trainer 的解析器。

## GetAttributeFloat

trainer 中该名称有 3 个 ASCII 命中（aa-04 区域表达式两处、独立字符串一处），没有 UTF-16LE 命中；游戏 EXE 中 ASCII/UTF-16LE 均为 0 个直接命中。aa-04 的 21 字节 AOB 在全体可执行节找到 17 个匹配，静态扫描无法选择哪一个落在运行时解析出的 `GetAttributeFloat .. +0x100` 范围内。邻近 +/-4 KiB 字符串和偏移、以及 FName/函数处理标识见 `trainer-symbol-windows.json`。这支持“运行时根据 Unreal 名称/反射信息解析函数”的推断，但不能证明具体实现。
