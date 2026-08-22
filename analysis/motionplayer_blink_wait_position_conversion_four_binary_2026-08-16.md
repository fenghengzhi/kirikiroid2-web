# MotionPlayer Blink wait-position 有符号转换四端复原（2026-08-16）

## 1. 结论

本轮只依据 `reference/binaries/` 的四个当前参考目标，重新核对
`EmoteBlinkController_step_guess` 中 `blinkPhase == 0` 的等待门。四端都不是在浮点域比较
`beginFrame` 与 `blinkPos`，也没有先把 `beginFrame` 转成 float；真实顺序为：

```text
blinkEnabled byte != 0
  -> load blinkPos as float
  -> signed-int32 toward-zero saturating conversion
  -> compare the resulting 32-bit integer with beginFrame
  -> equal only: blinkTimer -= dt
  -> ordered blinkTimer <= 0: blinkPhase = 10
```

转换的共同边界是：

| `blinkPos` | 比较所用的 signed int32 |
|---|---:|
| NaN | `0` |
| `+Inf` 或 `>= 2^31` | `INT32_MAX` |
| `-Inf` 或 `<= -2^31` | `INT32_MIN` |
| 其余有限值 | 向零截断 |

原 portable 源码直接执行 `static_cast<int>(blinkPos)`。普通有限可表示值上结果相同，但 NaN
和越界输入落入 C++ 浮点转整数未定义域；WebAssembly 编译器不需要保持参考 ARM 指令的确定
结果。现在改由局部 `_guess` helper 显式表达机器边界。

## 2. 四端 fresh 指令

| 目标 | step 入口 | enable/load/convert/compare |
|---|---:|---|
| Android arm64-v8a | `0x660FBC` | `0x661218 LDRB`、`0x661220 LDR S0`、`0x661228 FCVTZS W9,S0`、`0x66122C CMP W8,W9` |
| Android armeabi-v7a | `0x552472` | `0x5526D4 LDRB`、`0x5526DA VLDR S0`、`0x5526E2 VCVT.S32.F32 S0,S0`、`0x5526EA CMP R0,R1` |
| iOS arm64 | `0x1001A27A0` | `0x1001A2A38 LDRB`、`0x1001A2A40 LDR S0`、`0x1001A2A44 FCVTZS W8,S0`、`0x1001A2A4C CMP W8,W9` |
| iOS armv7 | `0x1A19D8` | `0x1A1C56 LDRB`、`0x1A1C5C VLDR S0`、`0x1A1C64 VCVT.S32.F32`、`0x1A1C6C CMP R1,R0` |

两份 AArch64 直接在 W 寄存器得到结果；两份 ARMv7 先在 VFP S lane 得到 signed word，再
`VMOV` 到通用寄存器。四端的 compare 都紧跟 conversion，没有额外范围分支、double 中转、
round-to-nearest 或 0..frame-range clamp。

## 3. 可达性和状态提交

构造时 `blinkPos` 由整数 `beginFrame` 转成 float，正常初值必然有限；但 step 的后续 phase
会用未夹取的 `dt`、`blinkFrameCount` 与 frame delta 更新它：

```text
blinkPos += (dt * +/-2.5 / blinkFrameCount) * (endFrame - beginFrame)
```

零 frame count、非有限 metadata 或非有限 `dt` 都能把 NaN/Inf 写回持久 `blinkPos`。因此
conversion 边界并非只能由内存破坏到达。phase 0 还会跨帧反复读取该值，且 public state/
controller 路径没有 finite clamp。

conversion 与 timer 的提交顺序也不可交换：只有整数比较相等才读减 timer。比较不等时 timer
保持原值；比较相等后先写减法结果，再做 ordered `<= 0` gate。若 timer 或 `dt` 令结果为 NaN，
写回仍发生，但 phase 不进入 10。

## 4. 源码、测试和 IDB

`cpp/plugins/motionplayer/EmoteBlinkController.cpp` 新增
`blinkPositionToSignedInt32_guess(float)`：先处理 NaN 和 `[-2^31,2^31)` 外的值，只有已证明
可表示的有限值才执行 C++ cast。step 的 compare 顺序和 timer commit 不变。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 通过完整 phase-0 step 观察 timer 是否从 5 减到
4，覆盖 NaN、正负无穷、正负 `2^31` 阈值、正负分数和饱和结果不匹配时 timer 不变；没有把
file-private helper 暴露成额外生产 API。

四份 recovery IDB 均在 conversion 指令写入语义注释，并加入
`blink wait float-to-signed-int32 saturation (2026-08-16)` 书签；四次原位保存均成功。

## 5. 验证范围

本纵切面只修复 phase-0 的 float→signed-int32 边界，不改变已经四端闭合的 value-track 外层
循环、每次最多一个 blink phase、RNG 消费、frame-window remap 或构造写集合。已完成：

- 普通与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer Catch2 TU syntax-only；
- `Web Debug Build` 完整最终链接；
- `Wasmtime Headless Debug Build` 完整 75-step 构建和 wasm 后处理；
- 本纵切面文件的 `git diff --check`。

第一次 Wasmtime preset 在独立 `krkr2_wasmtime_guest_objects` 编译处暴露了已有构建接线缺口：
`DrawDeviceD3DIntf.h` 需要完整 `cocos2d::Mat4`，但该对象库没有继承
`cocos2dx::cocos2d` exported include usage requirements。最终 guest 目标原本已链接同一个 imported
target；现在对象库也显式私有链接它，随后全量构建通过。此 CMake 修复只补齐编译依赖，不改变
Blink 或参考插件运行语义。

当前仓库仍没有由这些 preset 生成的可直接运行 Catch2 motionplayer 可执行文件，因此不会把
syntax-only 冒充运行测试。
