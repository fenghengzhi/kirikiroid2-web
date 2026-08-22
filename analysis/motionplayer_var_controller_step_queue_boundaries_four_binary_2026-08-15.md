# MotionPlayer VarController step 队列与 20B 元素边界四参考纵切（2026-08-15）

## 裁决

本轮重新反编译四个当前 `reference/binaries/` 产物中的变量 ramp controller，确认本地
step 的主要数值公式正确，但旧实现用 `goto/LABEL_*` 和单一 Android ARM64 寄存器注释
描述控制流，掩盖了两个必须分别保留的队列节拍：

1. idle 状态启动一条 keyframe 后，会在同一次 step 中立即用完整 `dt` 做第一次 ramp
   更新；
2. ramp 在本次 step 完成时只提交 target 并回到 idle，即使队列还有下一条 keyframe，
   也不会在同一次调用启动它。

源码现已改写为两个顺序 `if`：第一个只负责 idle/setup，第二个负责 active/update。这既
表达 setup 后的同调用 fallthrough，又不会把 terminal state 0 错误地重新送回 setup。
新增回归以两条排队 keyframe 锁定这一不对称节拍。

## 四平台入口与关键 CFG 点

下表名称是对 stripped 入口的语义描述；新恢复的 helper 使用 `_guess` 后缀，不把推断名
当作原始符号。

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| VarController step | `0x663FD8` | `0x554014` | `0x1001A48C0` | `0x1A3E48` |
| 函数大小 | `0x438` | `0x16C` | `0x1B8` | `0x172` |
| setup 写 state 1 | `0x664144` | `0x554068` | `0x1001A4958` | `0x1A3EC6` |
| phase 清零并进入 update | `0x6641AC` | `0x5540BC` | `0x1001A4984` | `0x1A3EEC` |
| terminal：提交但不取下一项 | `0x6643A0` | `0x55415E` | `0x1001A4A3C` | `0x1A3F98` |
| iOS 20B `pop_front` helper | 内联 | 内联 | `0x1001A4A78` | `0x1A3FBA` |

iOS 两个 out-of-line helper 已保守命名为 `VarKeyframe20B_popFront_guess`。Android 两端
把等价 deque 前移/换块逻辑内联在 step 中。

## 数据布局与对象所有权

逻辑 keyframe 固定为五个连续 32-bit float word，共 20 B：

| word | 正常角色 | 构造顺序导致的别名 |
| ---: | --- | --- |
| 0..2 | channel 0..2 | 只复制 `i < count` 的输入 |
| 3 / `+12` | duration | `count >= 4` 时被 channel 3 覆盖 |
| 4 / `+16` | power exponent | `count >= 5` 时被 channel 4 覆盖 |

keyframe 在 deque 元素存储中直接构造：先写 word 3 的 duration、再写 word 4 的 power，
最后以有符号 `for (i=0; i<count; ++i)` 从 word 0 起复制 channels。元素没有整体
value-initialize，因此 `count < 3` 时未被 channel 覆盖的中间 word 保持未初始化；step
只读取 `[0,count)` channel 和固定 word 3/4，不会为了“整洁”补零。

controller 另拥有三段 `new float[count]` 数组：current、start snapshot 和 target。构造
时三段都清零；析构函数体按 current -> start -> target 删除，随后语言自动析构 deque。
step 只借用 queue front 到 `pop_front`，pop 后不再使用元素引用。它不拥有 `out`；正常
Engine caller 传入自己的标量/小数组存储。

四端 STL ABI 的块策略不同，但元素生命周期一致：

- Android libstdc++ deque 的 20B block 为 500 B，即 25 elements；跨块时释放旧 block，
  再推进 map node；
- iOS libc++ deque 的 block 为 4080 B，即 204 elements；helper 的内部比较常量
  `0x198` 对应双倍 cursor 单位，换块时按 204 elements 修正起点并释放旧 block；
- 空队列、同 block pop 和最后元素跨 block 的分支都只销毁/前移当前 front，不触碰后续
  keyframe 内容。

## step 的精确状态机

### state 0：最多启动一条 keyframe

若 queue 为空，直接进入输出。否则：

1. 对 `i in [0,count)`，复制 `current[i] -> start[i]`，复制 front word `i` 到
   `target[i]`；
2. 写 `state=1`；
3. 写 `invDuration = 1.0f / front.word3`；
4. 原样复制 front word 4 的 float bits 到 power；
5. `pop_front`；
6. 写 `phase=0`；
7. 不返回，立即进入下面的 state-1 更新，且使用调用者传入的完整 `dt`。

这里没有“剩余时间”计算。setup 也不会先检查 duration、power、channel 数或输出指针。

### state 1：推进或提交当前 ramp

```text
phase += invDuration * dt
if phase >= 1:
    phase = 1
    current[0..count) = target[0..count)
    state = 0
else:
    weight = powf(phase, power)
    current[i] = start[i] + weight * (target[i] - start[i])
```

terminal 分支写 state 0 后直接进入统一输出尾部，不回跳到 queue setup。因而当第一条在
`dt=2` 内完成、第二条仍排队时，本次输出第一条 target；下一次即使 `dt=0`，才会 snapshot
第一条 current、弹出第二条、把 phase 保持在 0 并输出第一条值。

state 既非 0 也非 1 时不读取 queue/curve 字段，只输出 current。

### 统一输出

最后以有符号 `i<count` 正向复制 `current[i] -> out[i]`。Android ARM64 在可证明
non-overlap 时使用向量化路径，重叠时回到正向 scalar；源码保留正向循环，因此诸如
`out == current + 1` 的覆盖顺序也与 scalar 边界一致。`count>0` 时四端都不检查
`out==nullptr`。

## 未防御的输入与越界行为

这些行为来自四端共同的直接算术/直接内存访问，移植层不能擅自增加 clamp 或验证：

- `count < 1` 时所有 channel 和 output 循环为空；若 state 1，仍会更新 phase，且未到
  terminal 时仍调用 `powf`；
- `count == 4` 时构造把 alpha 写到 word 3，因此 alpha 同时成为 duration；这是当前 color
  controller 的真实固定元素别名；
- `count == 5` 时 channel 4 进一步覆盖 word 4，power 变成 channel 4；
- `count > 5` 时构造从 20B 元素尾后继续写，step 也从 front 元素尾后继续读；没有长度
  字段、扩容元素或边界检查，结果是 deque 相邻存储破坏/越界访问；
- duration 为 `+0/-0`、负值、无穷或 NaN 时直接做 IEEE-754 除法；
- phase 只有 terminal 上界 clamp，没有下界 clamp；负 `dt` 可产生负 phase，NaN 比较失败
  后进入 `powf` 并传播；
- power 不做整数化或合法性检查，word 4 作为 float 原始 bits 直接送入 `powf`；
- terminal 的 `>= 1` 对 NaN 为假，因此 NaN phase 不会自动提交 target。

正常 motionplayer 只构造已知的 1/2/4-channel controller，但这个上层约束不改变底层
函数的边界事实。

## 提交、异常与调用链

```text
Engine progress / direct controller update
  -> VarController_step(controller, out, floatDt)
     -> 可选：snapshot + pop 一条 20B keyframe
     -> 可选：推进/提交一个 active ramp
     -> 正向写 count 个输出
  -> caller 将输出继续写入 transform/color/变量目标
```

step 本身的 setup 在元素已经存在后不做新的容器分配；20B 元素为 trivially destructible，
正常 `pop_front` 只有 deque block 到界时的释放。`powf` 不以 C++ exception 报告普通域错误。
因此没有事务回滚层：setup 的 snapshot/state/标量写入和 pop 按程序顺序提交；后续 caller
若失败，不会把已弹出的 keyframe 放回。

## 本地与 IDB 更新

- `EmoteVarController.cpp` 将 step 从 `goto/LABEL_*` 伪代码改写为 setup `if`、active
  `if` 和统一输出三段，语义名明确 start snapshot、target、weight；
- constructor 注释从旧单一产物的寄存器/偏移转为四参考所有权说明；header 明写
  setup-fallthrough 与 terminal-no-next 的队列节拍；
- 单元回归排入两个 2-channel keyframe，验证第一条完成时第二条仍在 queue，下一次
  `dt=0` 才弹出第二条并保持第一条输出；
- 四份 recovery IDB 在 step entry、setup、phase reset 和 terminal 处加入跨平台语义注释，
  terminal 处加入“does not start next queued keyframe”书签；
- iOS 两个 20B pop helper 命名并注释为 `VarKeyframe20B_popFront_guess`。

## 验证

- 完整 `motionplayer-dll.cpp` Emscripten 单翻译单元 `-fsyntax-only`：通过，仅有既有 `_tss`
  deprecated warning；新增双 keyframe 队列节拍回归已成功编译；
- `cmake --build --preset "Web Debug Build"`：通过；重新编译 `EmoteVarController.cpp` 并
  成功链接 motionplayer、最终 Wasm 与 `index.html`。输出只有既有 `_tss`、imagepacker
  attributes、pthread memory-growth、JSPI/internal-symbol 警告；
- 限定本纵切文件的 `git diff --check` 与行尾空白扫描：通过；VarController source/header
  的 `LABEL_*`、IDA `vNN`、`sub_*`、旧产物名、绝对地址和裸 `*((...))` 残留扫描为空；
- 四份 recovery IDB 已强制刷新 step 和 iOS helper 的 decompile cache、保存并回读。四个
  step 分别回读为 `0x438/0x16C/0x1B8/0x172`，iOS 两个
  `VarKeyframe20B_popFront_guess` 分别回读为 `0x58/0x30`，名称、注释与函数边界均可解析。
