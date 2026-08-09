# `sub_6DA454@0x6DA454` — `PSBRawNode_GetResource` caller follow-up

本文件记录 114-address psbfile 审计二次复核中，沿
`PSBRawNode_GetResource_guess@0x5996E4` 的真实 caller 发现并闭合的确定差异。
`0x6DA454` 本身不属于 MANIFEST，因此不改变 114 份逐函数报告的地址集合或统计分母。
唯一权威来源仍是 Android kirikiroid2 `libkrkr2.so`；二进制没有保存该函数的精确源码名。

## Fresh IDA 正证据

- 主线 fresh `decompile(addr="0x6DA454")` 与完整
  `disasm(addr="0x6DA454", include_total=true)` 覆盖
  `[0x6DA454,0x6DAA5C)` 的 378 条指令。
- IDA 栈帧只恢复一个 32-bit `size` 槽：`[SP+0x24]`。函数序言到首次资源调用前没有
  对该槽的 store，因此它是未初始化局部，而不是默认值 0。
- 三个资源调用都把同一地址放入 X1：
  - `0x6DA5A0 ADD X1,SP,#0x24` → `0x6DA5A4 BL 0x5996E4`：非 RL pixel；
  - `0x6DA604 ADD X1,SP,#0x24` → `0x6DA608 BL 0x5996E4`：palette；
  - `0x6DA720 ADD X1,SP,#0x24` → `0x6DA724 BL 0x5996E4`：RL pixel。
- RL 解码在 `0x6DA778` 以 `LDRSW X8,[SP,#size]` 把同一槽符号扩展到 64 位，
  `0x6DA780 ADD X28,X23,X8` **先**形成 `sourceEnd = pixel + signedSize`，
  `0x6DA784 CMP W8,#1` 才建立有符号 gate。RL8 路径在 `0x6DA78C B.LT`、RL32
  路径在 `0x6DA90C B.LT` 跳过各自 decoder，因此两条路径都严格是
  `signedSize < 1`，而不是 unsigned `source < sourceEnd` 的等价改写。
- palette 路径在 `0x6DA648` 以 `LDR W8` 读取同一 32-bit 位型；
  `0x6DA658..0x6DA66C` 的负数加 3、条件选择、arithmetic shift right 2 序列按
  C/C++ 有符号除法向零截断语义得到 `(int32_t)size / 4`，再作为
  `std::vector<uint32_t>` 数量。`0x6DA67C..0x6DA6B4` 对 `TVPReverseRGB` 的 count
  重复相同的 signed `/4`。没有第二个 palette-size 槽。
- `PSBRawNode_GetResource_guess@0x5996E4` 在 `chunkData==null` 时返回 null 且不写
  size；所以“未初始化 + 同一槽复用”也是损坏/非法前置状态下的真实边界，不得用两个
  `{}` 初始化变量安全化。

## Android 关键伪代码（10 行）

```text
if texture exists: return
width/height = strict node reads; compressed = false
if "compress" exists: compressed = (strict string == "RL")
uint32 resourceSize                              // 不初始化
if compressed: pixel = strict("pixel").GetResource(resourceSize); signedSize=(int32)resourceSize
sourceEnd = pixel + signedSize; if signedSize >= 1: decode RL8/RL32 until pixel >= sourceEnd
else: pixel = strict("pixel").GetResource(resourceSize)
hasPalette = contains("pal")
if hasPalette: palette=strict("pal").GetResource(resourceSize); count=(int32)resourceSize/4; expand
else choose decoded or allocated/reversed pixel; copy rows; create texture; release buffers
```

## 本地闭环

修复前 `cpp/plugins/motionplayer/SourceCache.cpp` 有两个独立且零初始化的局部：
`pixelSize{}` 与 palette 分支内的 `paletteSize{}`。这同时偏离 Android 的初始化状态、
栈槽身份和跨调用数据流。

当前 `SourceCache.cpp:253-305,381-490` 精确恢复为：

- `std::uint32_t resourceSize;`，不初始化；
- compressed/uncompressed pixel 两条路径都把它传给 `GetResource`，并把同一值传给
  RL8/RL32 decoder；
- `SourceCache.cpp:253-305` 的 RL8/RL32 helper 都先以 `signedW32(sourceSize)` 恢复
  32-bit signed value，再先形成 `sourceEnd`，后执行 `< 1` return gate，最后用
  `do ... while(source < sourceEnd)` 解码；
- palette 路径再次把同一变量传给 `GetResource`，随后在 `SourceCache.cpp:438-451`
  以 `signedW32(resourceSize) / 4` 得到 `tjs_int paletteCount`；vector 只在构造边界
  转为 `size_t`，`TVPReverseRGB` 仍直接收到 signed count；
- 其余条件、指针默认值、分配/释放和渲染数据流不变。

## 结论

该 caller 的“未初始化状态 + 单槽三次复用 + RL signed-size gate/source-end 顺序 +
palette signed `/4`”确定 GAP 已闭合。它不是为了改变现有 fixture 的可观察输出，也不是
安全修复；它恢复的是 Android 已由栈地址、load/store 与 branch 集合直接证明的源码
token、数据流与损坏输入边界。运行时覆盖不足时只以现有测试做非回归守护，不从零制造
motion/PSB 物料。

## 已完成的原生非回归验证

- `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`；
- `motionplayer-dll`：`1386 assertions in 21 test cases`；
- `psbfile-dll`：`577 assertions in 10 test cases`。

健康资源路径会由 `GetResource` 覆写 size，所以上述原生测试主要承担非回归守护；
损坏输入下的未初始化槽、signed gate 和 palette signed count 仍以本轮 fresh IDA 的
load/store、`LDRSW` 与 branch 序列为权威证据。

最终当前源码的 Web Debug 与 Wasmtime guest 也已重编、链接通过；m2logo/yuzulogo
完整捕获 25/63 帧，trace hash 与汇总记录一致，structural comparator 仍为既有的
31/21 mismatch。健康 fixture 无法观察损坏 size 的 signed 边界，故这些结果只作非回归
守护；完整结果见 [SUMMARY.md](SUMMARY.md)。
