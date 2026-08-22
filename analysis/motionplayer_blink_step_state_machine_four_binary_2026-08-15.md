# MotionPlayer Eye/Blink step 状态机四参考纵切（2026-08-15）

> **2026-08-16 unordered overshoot 勘误：** 本文旧伪码把 overshoot 简写成两个
> ordered conjunction，遗漏了负向 `GE/LT` 补集对 unordered 的接纳。Eye 正向
> target unordered 不 overshoot，负向或 direction unordered 会 overshoot。完整四端
> 条件链见
> `analysis/motionplayer_eye_eyebrow_overshoot_unordered_four_binary_2026-08-16.md`。

## 裁决

本轮从四个当前 `reference/binaries/` 产物重新反编译 Eye/Blink 的 per-slice step，发现
本地实现有一处真实的同帧队列提交错误：活动 8B segment overshoot 后，如果次级轨道已空，
旧源码把 state 1 清成 state 0 后直接进入 blink 输出；四端都会继续停留在同一个外层循环，
立即从 12B 主轨道弹出下一条命令、调用 mesh resolver，并继续消费新生成的次级轨道。

修正后的源码恢复为一个外层循环。它会在同一次调用中连续切换 segment/command，且每次都
复用完整的原始 `dt`，不计算或传递“剩余时间”。这既解释了四端 CFG，也去掉了旧源码中
`LABEL_19/LABEL_24/LABEL_28` 和 `vNN/+offset` 注释造成的错误结构暗示。

## 四平台入口

所有名称都带 `_guess`，表示它们是 stripped 产物上的保守语义名，不声称恢复了原始符号。

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteBlinkController_step_guess` | `0x660FBC` | `0x552472` | `0x1001A27A0` | `0x1A19D8` |
| 函数大小 | `0x3EC` | `0x34C` | `0x388` | `0x378` |
| `EmoteEngine_stepEyeControls_guess` | `0x673858` | `0x55BDF4` | `0x1001AFA8C` | `0x1AF2A4` |
| 12B track `pop_front` | 内联 | `0x5527BE` | `0x1001A2B28` | `0x1A1D50` |
| 8B track `pop_front` | 内联 | `0x5527EE` | `0x1001A2B80` | `0x1A1D84` |

Android ARM64 还在 `EmoteEngine_progressCore_guess` 的 `0x67A4C0` 与
`EmoteEngine_serializeState_guess` 的 `0x673298` 直接调用 step；后者传 `dt=0`。其独立
eye-walk helper 的调用点在 `0x6738BC`。另外三端的对应 walk helper 分别在
`0x55BE32`、`0x1001AFB0C`、`0x1AF2FA` 调 step。

## 调用链和输出所有权

```text
Engine progress / dt=0 state refresh
  -> 顺序遍历 Eye deque entry {unique_ptr<controller>, label}
     -> EmoteBlinkController_step(controller, &floatOut, sameFloatSlice)
        -> 主/次 value-track 状态机
        -> 独立 blink phase 状态机
        -> inclusive frame-window remap
     -> floatOut 提升为 double
     -> label-value map get-or-insert(label)
     -> 覆盖 mapped double
```

step 先完整提交 controller 的内部状态，caller 才查找/插入 label map。因而后续 map 分配或
插入异常不会回滚 controller。`out` 是调用者栈上的裸 `float*`，四端都不检查 null；正常
Engine 调用总是传有效地址。

## 主/次轨道状态机

`trackState` 的观察语义是：

- `0`：可从 12B 主命令轨道取新命令；
- `1`：可从 resolver 生成的 8B `{start,end}` 次级轨道取新 segment；
- `2`：当前 segment 正在做 power-curve 推进；
- 其他值：不碰两条轨道，直接进入 blink phase。

四端共同的源码级 CFG 可写成：

```text
state = trackState
repeat:
  while state != 2:
    if state == 1:
      if secondary empty:
        state = trackState = 0
      else:
        segment = secondary.front
        secondary.pop_front
        trackValue = segment.start
        if start == end:
          goto blink                       // state 仍为 1
        trackTarget = end
        trackDir = ((end-start) < 0) ? -1 : +1
        state = trackState = 2
    else:
      if state != 0 or primary empty:
        goto blink
      command = primary.front
      primary.pop_front
      resolve(mesh, trackValue, command.end, secondary)
      accum = 0
      span = mesh.resolvedSpan
      invDuration = 1 / command.duration
      power = command.power 的原始 float bits
      state = trackState + 1
      trackState = state

  eased = pow(accum/span, 1/power) + invDuration*dt
  delta = pow(eased,power)*span - accum
  next = trackValue + trackDir*delta
  trackValue = next
  if blinkTrackOvershootsByNativeConditions(dir, target, next):
    state = trackState = 1
    trackValue = target
    goto repeat
  accum += delta

blink:
  ...
```

关键边界：

- overshoot 后不是下一帧再取轨道，而是在当前调用立刻回到 state 1；
- state 1 发现次级轨道空时会继续进入 state 0，因此可在同一调用弹出下一条主命令；
- 每个新 segment 仍使用完整 `dt`，没有按 overshoot 比例扣除已用时间；大 `dt` 可以在一次
  step 内连续完成多条非零 segment，甚至连续消费多条主命令；
- `{x,x}` segment 被弹出并把 `trackValue` 写为 `x`，但保持 state 1，立即停止本轮轨道
  消费；它后面的次级 segment 留到下一次 step；
- `end-start` 为 NaN 时 `<0` 为假，方向取 `+1`；正向 target compare 使用 `LS`，
  unordered 不 overshoot，所以该正常解析路径仍把 NaN accum 留在 state 2；由 restore
  注入负向/NaN direction 时则遵循 2026-08-16 补充的不同边界；
- 没有对零/负 duration、零 span、零/负/NaN power 或负 `dt` 做保护。正常 enqueue 只允许
  ordered-positive duration 进入主轨道，NaN 也会 immediate；恢复状态或内部直接写入
  仍可触达其余边界。

## pop_front 的内部容器行为

两条轨道都是 `std::deque`，但四参考产物展示了两套 STL ABI：

| ABI | 12B command block | 8B segment block | 块切换 |
| --- | ---: | ---: | --- |
| Android libstdc++ | 504 B / 42 elements | 512 B / 64 elements | front 到块尾时先 delete 当前块，再从 map 下一项装入 begin/end |
| iOS libc++ | 341 elements / 4092 B | 512 elements / 4096 B | size--、startIndex++；跨块时 delete map 首块、map start 前移并把 index 减一个 block capacity |

iOS helper 的比较常量是两倍 capacity（12B 为 `0x2AA`，8B 为 `0x400`），跨块后分别减
`341`/`512`；这是 libc++ deque 的 start-index 表示，不是每块有 682/1024 个元素。
Android ARM64 将两个 pop 路径内联，ARMv7 保留两个共享 out-of-line helper；iOS 两端也
保留 out-of-line helper。便携源码使用 `std::deque`，保留元素顺序、所有权和 block-boundary
释放时机的源码语义，不伪造特定 STL header 字节布局。

两个元素类型都是 trivial。step 先复制 front、再 `pop_front`，然后才写新的活动字段；主
命令同样先从 deque 移除，再调用可能分配内存的 mesh resolver。因此 resolver 抛异常时，
已弹出的主命令不会恢复，resolver 已清理/部分构建的输出容器也按其自身异常边界保留；本轮
没有引入事务式回滚。

## Blink phase 状态机

轨道阶段结束后，每次 step 最多执行一个 `blinkPhase` case；本次 transition 不会在同一
调用继续执行目标 phase：

- `0` wait：仅当 `blinkEnabled != 0` 且 `beginFrame == int(blinkPos)` 时减 timer；
  timer `<=0` 只把 phase 改为 `10`；
- `10` closing：位置增加
  `(dt*2.5/blinkFrameCount)*(endFrame-beginFrame)`；达到/越过 end 后 clamp，改为 `11`，
  timer 写 `blinkFrameCount/5`；
- `11` hold：timer 减 `dt`；到期后先把 phase 写 `12`，再从共享 RNG 取一次数并写下一次
  wait interval；
- `12` opening：位置增加同式但系数为 `-2.5`；达到/越过 begin 后 clamp 并写 phase `0`；
- `1..9`（除 0）、`>12` 和负值：不修改 blink 字段，直接做最终 remap。

这里的 `int(blinkPos)` 已于 2026-08-16 重新逐端闭合：四端分别使用
`FCVTZS W,S` / `VCVT.S32.F32`，即 signed-int32 toward-zero saturation，而不是可对 NaN/
越界产生 C++ UB 的裸 `static_cast<int>`。NaN 得 0、正溢出得 `INT32_MAX`、负溢出得
`INT32_MIN`；完整指令和回归见
`motionplayer_blink_wait_position_conversion_four_binary_2026-08-16.md`。

phase 11 在调用共享 RNG 前已经提交 phase 12。首次惰性创建全局 RNG 或取数路径若抛异常，
phase 不回滚。负 `dt`、零 `blinkFrameCount`、反向 begin/end、NaN/Inf 均无防御性 clamp；
只有各 case 明写的端点比较会 clamp。

## 最终 remap 边界

四端只在 `trackValue >= beginFrame && trackValue <= endFrame` 时应用：

```text
trackValue
  + ((endFrame-trackValue) * (blinkPos-beginFrame))
    / (endFrame-beginFrame)
```

区间是双端 inclusive。`beginFrame == endFrame` 且 value 恰好等于该值时仍进入计算，分母
为零，通常形成 `0/0 -> NaN`；没有特殊处理。`beginFrame > endFrame` 时普通有限 value
无法同时通过两个条件。最后只写 caller 的 `float` 输出，不把 remap 结果回写 `trackValue`。

## 源码、测试和 IDB 更新

- `EmoteBlinkController_step` 改写为四端共同的单一外层轨道循环，修复 overshoot 后错误地
  延迟下一条主命令的问题；
- 删除该函数旧 `LABEL_*`、IDA `vNN` 和单端字段偏移伪代码，保留语义注释；
- 新鲜回读四端 constructor 后，一并把同文件构造区的 `v10/v11/v12` 与裸指针偏移注释
  改为 `intervalMin/intervalMax/initialPosition` 等语义表达；不改变此前已核对的构造写集合；
- 单元回归覆盖 overshoot 后同帧消费下一主命令、`{x,x}` segment 停止继续 drain，以及
  phase 0/10/11/12 每次调用只执行一个 case；
- 后续 conversion 纵切又补充 phase-0 NaN/Inf/阈值/分数输入，锁定 blinkPos 的
  signed-int32 饱和转换和比较不等时 timer 不写；
- 四份 recovery IDB 的 step 入口、equal-segment、overshoot re-entry 和 phase dispatch 已
  添加注释与书签；六个 out-of-line deque helper 分别命名为
  `ValueTrack12B_popFront_guess` / `ValueTrack8B_popFront_guess`；四个平台的 eye-walk helper
  命名为 `EmoteEngine_stepEyeControls_guess`。

## 验证

- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过，仅有仓库既有 `_tss`
  deprecated warning。
- `cmake --build --preset "Web Debug Build"`：通过；在最后一次构造区清理后重新编译
  `EmoteBlinkController.cpp` 并成功链接 motionplayer 与最终 Wasm/`index.html`。输出只有
  既有 `_tss`、pthread memory-growth、JSPI/internal-symbol 警告。
- `cmake --build --preset "Wasmtime Headless Debug Build"`：该独立预设重新配置后编译到
  `145/186`；本纵切的 `EmoteBlinkController.cpp.o` 在 `144/186` 成功。随后
  `EmoteEngine.cpp` 因 headless include path 找不到既有 `math/Mat4.h` 而停止；失败点不在本轮
  文件或状态机代码，因此不把它记为本纵切通过，也不为此扩大修改范围。
- `git diff --check`（限定本纵切文件）与行尾空白扫描：通过；
  `EmoteBlinkController.cpp` 的 `LABEL_*`、IDA `vNN`、`sub_*`、旧产物名、绝对地址和裸
  `*((...))` 注释扫描均为空。
- 四份 recovery IDB 均已保存并回读：四个 step/eye-walk 名称、六个 out-of-line deque
  helper 名称、equal-segment/overshoot/phase 注释及 overshoot 书签全部可解析。
