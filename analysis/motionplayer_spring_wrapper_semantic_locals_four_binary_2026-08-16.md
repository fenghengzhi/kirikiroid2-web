# MotionPlayer spring Engine wrapper 语义局部名与注释迁移（四参考，2026-08-16）

## 结论

`EmoteSpring` 的 simple/chain solver 已在 8 月 15 日专项中闭环，但调用它们的
`EmoteEngine.cpp` wrapper 仍遗留 `v13/v49/v50` 一类反编译临时名。这些名字不表达
reference source 的数据流，也容易让后来维护者把同一阈值或 scale 窄化误认成未知字段。

本轮重新反编译四个 simple wrapper 和四个 chain wrapper，确认源码现有运算、顺序和
边界已经与四参考一致；只迁移局部变量/注释，不改变控制流或初始化状态。尤其没有给
reference 故意未初始化的 current-force/output 栈槽补零。

## fresh 四参考映射

| wrapper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| simple hair/parts | `0x678B28` | `0x55EE98` | `0x1001B29D0` | `0x1B24D8` |
| two-segment bust chain | `0x6790C8` | `0x55F2F4` | `0x1001B2F2C` | `0x1B2ABC` |

八份 fresh decompile 共同重现：

1. `float currentForce[2]` 先以未初始化状态存在；controller count 至少为 1 时执行
   `memcpy(currentForce, currentValue, count * 4)`，没有 count 上限或 null 检查。
2. `stepThreshold = dt - 0.0001f` 在 deque 循环前只计算一次。
3. init node 无视 threshold，以完整 `dt` 求解一次；非 init 仅在
   `stepThreshold > 0.0f` 时子步。
4. 子步是 `min(dt - elapsed, 1.1f)`；先累加 elapsed，再以
   `elapsed / dt` 和 `1 - elapsed / dt` 插值旧/新 anchor。
5. angle 在每次实际 solver call 前从 Player 重新读取并窄化为 float。
6. chain wrapper 在循环前把传入 double scale 窄化成一个 float，并在所有 solver call
   中复用；每次 chain solver 之后立即运行 post-bend。
7. 非 init 且未通过 threshold 时，simple 两个输出或 chain 三个输出仍未初始化，但仍按
   X/Y 或 segment1/segment0/selectedY 的既定顺序写入 HM7。

逐端可见的关键位置包括：simple count-copy 为 `0x678B84/0x55EEC8/
0x1001B2A2C/0x1B251A`，simple threshold 为 `0x678BB0/0x55EEDA/
0x1001B2AAC`（iOS armv7 同一 loop 前），chain count-copy 为
`0x67912C/0x55F326/0x1001B2F90/0x1B2AFC`，chain threshold 为
`0x679190/0x55F350/0x1001B301C`（iOS armv7 同一 loop 前）。精确地址只留在本文和
recovery IDB，不进入编译源码注释。

## 源码局部名恢复

| 旧临时名 | 恢复后的语义名 |
| --- | --- |
| `cur` | `currentForce` |
| `v13`, `v49` | `stepThreshold` |
| `v50` | `outputScale` |
| `oX`, `oY` | `outX`, `outY` |
| `oSeg0`, `oSeg1`, `oLastY` | `outSegment0`, `outSegment1`, `outSelectedY` |
| `acc`, `st` | `elapsed`, `substep` |
| `f`, `w` | `currentWeight`, `previousWeight` |
| `ax`, `ay`, `ang` | `interpolatedAnchorX/Y`, `angleRad` |

同文件 variable-list 两层枚举中无语义歧义的 `v11/v55` 也迁移为
`variableIndex/frameIndex`。这只恢复循环索引角色，访问和 owner lifetime 不变。

旧注释还把 controller queue 写死成“某 binary 的 libstdc++ deque 与固定成员偏移”。四个
参考包含 Android libstdc++ 与 iOS libc++ 两套 deque 投影；跨平台源结构只能表达为
`std::deque`。编译源码现改为说明 ABI map/block 差异记录在分析文档，不再把单目标偏移
冒充共同源码字段。

## 边界保持

- 未初始化数组/输出保持未初始化；
- `count * sizeof(float)` 的潜在越界保持；
- ordered `stepThreshold > 0`、NaN skip 和首次节点无条件 step 保持；
- `std::fmin`、先 elapsed commit 后插值、每子步重读 angle 保持；
- chain solver/post 的紧邻调用和 output publication crossover 保持；
- stripped wrapper/solver 名继续保留 `_guess`，没有凭局部语义猜原始 C++ 拼写。

四个 recovery IDB 已在两个 wrapper 入口补充语义局部映射/边界注释并保存。
