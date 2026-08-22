# MotionPlayer `updateLayers` parameter-mode reset（四参考二进制，2026-08-14）

## 1. 结论

`Player_updateLayers_guess` 的末尾有两个相邻但不同的 record-range cleanup：

1. 清非 root `MotionNode` 的完整 flags byte 与 accumulated dirty byte；
2. 遍历 Player 的既有 `vector<MotionParameterEntry>`，只把每条记录的 `mode` 写成 0。

第二段不是 per-node evaluation scratch，也不会按 node count resize。它与 parameter parser、
ramp multimap、selected parameter pointer 和 frameProgress idle gate 使用的是同一个 parameter
vector。`mode` 由 `setVariable`/内部 binder 写入，type-3 child-motion pass 在本次
`updateLayers` 中消费，正常走到函数尾后归零；`value`、id、range、division 与 vector
capacity 全部保留。

本地此前把这段机器码误恢复为独立的 `_perNodeEvalData`：每次调用先按 node count resize、
把当前时间写进伪 `evalTime`，尾部再清伪 `dirtyFlag`。这会产生 native 不存在的 allocation/
异常点，同时让真正的 `MotionParameterEntry::mode` 永久粘滞，使 type-3 child 在后续帧重复
触发 replay/update gate。本轮删除了该伪容器并把尾部清零接回 `_parameterEntries`。

## 2. 四端容器与字段身份

| 目标 | `Player_updateLayers_guess` | parameter vector begin/end | entry size | `mode` offset |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B871C` | `Player+384/+392` | 56 B | `+48` |
| Android armv7 | `0x5856E0` | `Player+252/+256` | 48 B | `+40` |
| iOS arm64 | `0x10010E544` | `Player+296/+304` | 56 B | `+48` |
| iOS armv7 | `0x10BE5C` | `Player+204/+208` | 44 B | `+40` |

这些三元组与四个 `Player_appendParameterEntry_guess`、parameter idle gate、ramp-map
producer 和析构清理的既有映射逐端相同，不是“形状恰好相似”的另一个 vector。记录的
共同源级字段顺序仍为：

```cpp
ttstr id;
bool discretization;
double rangeBegin;
double rangeEnd;
double division;
double value;
int mode;
```

64 位 `ttstr`/对齐使 mode 位于 `+48`、记录为 56 B；两个 32 位目标的 mode 均在
`+40`，但 Android libstdc++ 记录步长为 48 B，iOS libc++/ARMv7 为 44 B。

## 3. fresh disassembly 的尾部证据

| 目标 | node flags/dirty clear | vector load | mode store | final state-byte stores |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6B910C/0x6B9110` | `0x6B9124` | vector pair `0x6B9194/0x6B9198`，scalar `0x6B91D0` | `0x6B91D8/0x6B91DC` |
| Android armv7 | `0x586074/0x586078` | `0x58607E` | `0x58609C` | `0x5860A6/0x5860AA` |
| iOS arm64 | `0x10010F004/0x10010F008` | `0x10010F01C` | `0x10010F050` | `0x10010F064/0x10010F068` |
| iOS armv7 | `0x10C87E/0x10C882` | `0x10C888` | `0x10C8AA` | `0x10C8B6/0x10C8BA` |

AArch64 Android 对偶数前缀使用一次循环同时清两条记录的 mode，再用 scalar tail 处理
奇数项；另外三端是普通逐项循环。该优化不改变共同伪代码：

```text
run all updateLayers phase-3 consumers, including MotionSub

for each non-root node:
    node.flags = 0
    node.accumulated.dirty = false

for each parameter entry:
    entry.mode = 0

clear noUpdateYet and queuing
return
```

Android armv7 对最后两个独立 byte store 的机器顺序与另外三端相反，这是无中间调用、
无别名的 compiler scheduling 差异；共同语义只要求两者都发生在 record-range cleanup 后。

四份 fresh decompile 都没有在 `updateLayers` 入口或主体对 parameter vector 做 resize、
append、clear，也没有把 `_clampedEvalTime` 写进 parameter record。空 vector 直接跳过 mode
loop；vector backing/capacity 不变。

## 4. 数据流和异常边界

`Player_bindParameterValue_guess` 对每个同名 ramp node 先写 `entry.mode=mode`，再写归一化
value。随后 type-3 `Player_updateMotionSubNodes_guess` 读取：

```text
mode = node.parameterEntry ? node.parameterEntry.mode : 0
if mode == 0 and !node.accumulated.dirty:
    skip child reconfiguration but still enter shared child step

if (mode & 5) != 0 or node.flags != 0:
    replay child motion using activeSlot.motionFlags | mode
```

所以 mode 是一次成功 update 的 trigger，不是持久属性。若 `updateLayers` 在到达尾部之前
因属性调用、child malformed state、allocation 或其他 phase 抛出，parameter mode 与尚未清理
的 node flags 会共同保留；下次调用可以再次消费。尾部的现有-record int stores 本身不分配、
不释放、不会形成新的 C++ 异常点。旧伪 vector 的 pre-phase resize 则可能在任何 native
phase 执行前抛 `bad_alloc`，属于明确的端口额外行为。

## 5. 落地与验证

- 删除 `detail::PerNodeEvalData` 与 `Player::_perNodeEvalData`；
- 删除 `updateLayers` 入口按 node count resize 和写伪 evalTime 的逻辑；
- 在 native phase-3/非 root node cleanup 之后遍历 `_parameterEntries`，只清 `mode`；
- 将 `_noUpdateYet`/`_queuing` 的清理放回两个 record range 之后；
- 新增回归断言：`mode=5` 经一次 `updateLayers` 变为 0，而 `value=3.25` 保持不变；
- 四个 recovery IDB 标注 parameter vector 身份、逐 ABI stride、one-update trigger 与
  pre-tail exception retention，并保存；
- 完整 motionplayer Catch2 测试翻译单元的 Emscripten `-fsyntax-only` 通过，仅有仓库既有
  `_tss` warning；
- `Web Debug Build` 重编 33 个受 Player 布局影响的步骤并成功链接最终 Wasm/HTML；
- `git diff --check` 通过，仅报告工作树既有 LF/CRLF conversion warning。
