# `calcBounds` node-type mask 与越界 shift 四参考恢复（2026-08-14）

## 1. 合法域共同语义

在 type-4 粒子预处理、active-slot gate 和非 preview type-3 递归之后，普通 bounds admission
使用一个 node-type bit 与常量 mask 相交，再短路检查 `source.valid`：

```text
mask = preview ? 0x1449 : 0x1441
typeBit = 1 << node.nodeType
if ((mask & typeBit) == 0): continue
if (!node.source.valid): continue
```

合法 nodeType `0..12` 下：

- normal `0x1441` 接受 `{0,6,10,12}`；
- preview `0x1449` 额外接受 `3`，即 `{0,3,6,10,12}`；
- non-preview type 3/4 在到达该 mask 之前已有独立递归分支；type 3 完成后跳过普通路径，
  type 4 粒子递归结束后仍可能落到 mask，但 bit 4 不在 `0x1441` 中；
- mask 失败时不读取 `source.valid`；mask 成功后才读 node 的 byte field。

当前源码保留共同的 `1 << node.nodeType` 表达式和 short-circuit 顺序；新增合法域回归覆盖
normal/preview 的完整 `0..12` mask（normal 独立跳过已由专门回归覆盖的 type 3/4）。

## 2. 四端指令映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| 装载 normal/preview mask | `0x6C1478..0x6C1484` | `0x58BFFE..0x58C008` | `0x100115D80..0x100115D8C` | `0x11377E..0x113788` |
| variable shift | `0x6C1480: LSL W9,W22,W9` | `0x58C00C: LSL.W R0,R2,R0` | `0x100115D7C: LSL W9,W10,W9` | `0x11378C: LSL.W R0,R2,R0` |
| mask AND/TST 与跳过 | `0x6C1488..0x6C148C` | `0x58C010..0x58C012` | `0x100115D90..0x100115D94` | `0x113790..0x113792` |
| `source.valid` byte gate | `0x6C1490..0x6C1494` | `0x58C014..0x58C01A` | `0x100115D9C..0x100115DA0` | `0x113794..0x11379A` |

A64 两端使用 AArch64 `LSLV W` alias，A32 两端使用 Thumb-2 register `LSL.W`。四端都没有
范围检查、clamp、switch table 或 `nodeType < 32` guard。

## 3. malformed shift count 的平台差异

原始 C++17 形状中的左操作数是有符号 `int 1`。因此：

- `nodeType < 0`；
- `nodeType >= 32`；
- 以及把 `1` 移入不可表示的 signed bit 31

都落在 C++ abstract-machine 的未定义行为域。四个成品二进制已经选定各自硬件指令，因而
仍可记录实际机器边界，但不能反推一个跨编译器统一的标准 C++结果。

已生成指令的行为为：

| 目标 | 实际 shift amount | 机器结果 |
|---|---|---|
| Android/iOS ARM64 | `nodeType & 31` | `1u << (nodeType & 31)` |
| Android/iOS ARMv7 | 先取 `nodeType & 255` | amount=0 得 1；1..31 正常；32..255 得 0 |
| 当前 WebAssembly | wasm `i32.shl` 取低 5 位 | 与 AArch64 的 modulo-32 结果相同 |

因此 malformed 值存在真实跨参考差异，例如：

- `nodeType=32`：A64/Web 映射到 bit 0，可通过两种 mask；A32 shift 结果为 0，拒绝；
- `nodeType=38`：A64/Web 映射到 bit 6，可通过；A32 拒绝；
- `nodeType=256`：A64 低 5 位和 A32 低 8 位都为 0，均产生 bit 0；
- 某些负值同样会因低 5/8 位解释不同而分歧。

这不是两个源算法，而是同一 unchecked signed shift 在 AArch64/AArch32 codegen 中显现的
平台 UB。恢复源码不应擅自加范围 guard，也不应声称四端对 malformed nodeType 有统一结果。
Web 当前自然跟随 wasm/A64 modulo-32；合法 motion 数据只使用小的非负类型值，因此共同域
不受影响。

## 4. 回归、IDB 与验证

回归逐项验证：

- normal 合法域除独立 type 3/4 外，只有 `0,6,10,12` 产生七成员 bounds；
- preview 合法域只有 `0,3,6,10,12` 产生七成员 bounds；
- 被拒绝类型保留 `±DBL_MAX` parent sentinels，getter 只有 `isValid=false`。

测试不主动执行 C++ shift UB；malformed 跨平台结果由四端最终机器指令定证。

验证结果：

- 完整 motionplayer 单测 TU 使用真实 Emscripten response file 执行
  `-fsyntax-only`：通过；仅有仓库既有 `_tss` literal-operator 弃用警告；
- 当前生产源码状态此前已由 `cmake --build out/web/debug --parallel 8` 完整重建并成功链接
  `index.html`；本纵切面只增加合法域测试/文档，不修改该生产表达式；
- 对新增测试、bounds 主文档、纵切面文档和计划执行 `git diff --check`：通过；仅有工作树
  既有 LF/CRLF 转换提示；
- 四份 recovery IDB 的 shift 与 source-valid short-circuit 注释均已原位保存。
