# MotionPlayer Blink/Wind 共享 RNG 四二进制审计（2026-08-11）

## 结论

四个 1.3.9 参考二进制的眨眼控制器与风粒子发射器并不是各自维护随机源，而是共同调用一个进程级、惰性分配的 MT19937 对象。它没有按控制器、Player 或 Engine 隔离：先构造哪个眼睛控制器、某一帧 Wind 消耗了多少次随机数，都会改变后续 Blink 的随机序列，反之亦然。

本轮以四个当前参考二进制重新定位后，确认旧 `libkrkr2.so` 注释中的 Android ARM64 地址已经失效，并修正了本地 RNG 的三个可观察语义错误：

1. 时钟 tick 必须先做完整宽度的 `/ 1000000`，再截取低 32 位作为 seed；
2. twist 合成字虽然使用 `next & 0x7ffffffe`，但 `MATRIX_A` 的选择测试的是未掩码 `next` 的 bit 0；
3. 第二个 MT word 已经完整 temper 一次，canonical double 直接取它的低 20 位，不能再次执行 `high ^= high >> 18`。

四份 IDB 已补上函数名、原型并保存。本文件中的名称带 `_guess`，表示它们是根据行为恢复的语义名，不声称拿到了原始符号。

> 2026-08-15 续审：重新恢复了 RNG 对象本身的虚析构/vtable、pointer-width 状态槽和真实 cursor，并发现旧 Web `get()` 的内联 seed recurrence 把首个 `+1` 错写成了 `+2`。源码现已改为带 seed 参数的真实构造函数；详细证据见 `motionplayer_blink_rng_object_lifecycle_four_binary_2026-08-15.md`。下文算法结论仍有效，涉及“Web 不复原 vtable/原生布局”的旧描述以续审文件为准。

## 参考二进制与入口映射

### Eye/Blink/Wind 调用者

| 参考二进制 | metadata 主构建 | `buildEyeControl` | Blink ctor | Blink step | WindEmitter step |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `0x67A8B0` | `0x669B5C` | `0x65FD48` | `0x660FBC` | `0x665BC8` |
| Android ARMv7 | `0x560020` | `0x55739C` | `0x551B34` | `0x552472` | `0x554E4C` |
| iOS ARM64 | `0x1001B4468` | `0x1001A91F4` | `0x1001A1C8C` | `0x1001A27A0` | `0x1001A5A24` |
| iOS ARMv7 | `0x1B3F58` | `0x1A8800` | `0x1A0E50` | `0x1A19D8` | `0x1A4FEC` |

`eyeControl` 宽字符串的引用先落在 metadata 主构建函数，再进入上表的 Eye builder。Blink ctor 在启用的 eye 条目被分配时立即消耗一次随机数，用于初始化下一次眨眼倒计时。

IDB 中使用的调用者名称：

- `EmoteEngine_buildEyeControl_guess`
- `EmoteBlinkController_ctor_guess`
- `EmoteBlinkController_step_guess`
- `EmoteWindEmitter_step_guess`

Blink step 原型统一恢复为：

```cpp
void EmoteBlinkController_step_guess(void *self, float *out, float dt);
```

Wind step 原型统一恢复为：

```cpp
void EmoteWindEmitter_step_guess(void *self, float dt);
```

### RNG 函数

| 参考二进制 | get global | next canonical | seed helper | regenerate |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x9F0308` | `0x9F00D0` | `0xA2A308` | 内联在 `nextCanonical` 的两个取 word 路径中 |
| Android ARMv7 | `0x7508D4` | `0x750838` | `0x770E54` | `0x750578` |
| iOS ARM64 | `0x1002C24B0` | `0x1002C23E0` | `0x1002964AC` | `0x1002C223C` |
| iOS ARMv7 | `0x2C7878` | `0x2C77DC` | `0x29AAAC` | `0x2C7692` |

IDB 中使用的 RNG 名称：

- `EmoteBlinkRng_getGlobal_guess`
- `EmoteBlinkRng_nextCanonical_guess`
- `EmoteBlinkRng_seedFromSteadyClock_guess`
- `EmoteBlinkMt19937_regenerate_guess`（Android ARM64 没有独立函数）

## 数据流与调用链

```text
metadata/application
  -> 读取 "eyeControl"
  -> EmoteEngine_buildEyeControl_guess
       -> enabled gate
       -> operator new(平台对应的 BlinkController 大小)
       -> EmoteBlinkController_ctor_guess
            -> EmoteBlinkRng_getGlobal_guess
            -> EmoteBlinkRng_nextCanonical_guess
            -> blinkTimer = min + (max - min) * float(rng)

EmoteEngine progress 的每个 clamped slice
  -> 遍历 Eye deque {controller, label}
       -> EmoteBlinkController_step_guess
            -> 仅在 phase 11 的 hold timer 到期时消耗一次 RNG
            -> 重设下一次 blinkTimer
       -> HM#7[label] = blink 输出

D3D/Primary startWind
  -> EmoteEngine::setWind_guess
       -> 惰性创建或重建 EmoteWindEmitter
       -> 设置 gate、速度、范围与累计器

EmoteEngine progress 的同一 clamped slice
  -> wind != null && wind.gate != 0
       -> EmoteWindEmitter_step_guess
            -> 每个 emission attempt 消耗一次 RNG
            -> 若 float(rng) < 0.0625 且有空槽，再消耗一次 RNG 生成 yPos
```

这条共享流带来一个重要的外部可见边界：Blink 与 Wind 的随机序列会互相扰动。不能为了“可复现”而给每个对象单独 seed，也不能把 Wind 改用另一个标准库 generator；那会改变参考实现中后续所有随机事件的相位。

## 进程级对象的原生布局与生命周期

`getGlobal` 在四个平台都是普通的 raw-global-null-check：没有观察到函数局部静态变量的 `__cxa_guard`、锁或一次性初始化原语。

```cpp
if (global == nullptr) {
    p = operator new(native_size);
    seed = low32(full_width_clock_ticks / 1000000);
    p->vptr = platform_vtable;
    p->left = 1;
    p->mt[0] = seed;
    for (i = 1; i < 624; ++i)
        p->mt[i] = 1812433253u *
                   (p->mt[i - 1] ^ (p->mt[i - 1] >> 30)) + i;
    p->cursor = &p->mt[0];
    p->left = 1;
    global = p;
}
return global;
```

| ABI | allocation | vptr | `left` | cursor | 624 个状态槽 |
|---|---:|---:|---:|---:|---:|
| Android/iOS ARM64 | `0x1398` | `+0`，8 B | `+8`，DWORD | `+16`，8 B 指针 | `+24` 起，QWORD/槽 |
| Android/iOS ARMv7 | `0x9CC` | `+0`，4 B | `+4`，DWORD | `+8`，4 B 指针 | `+12` 起，DWORD/槽 |

64 位实现使用 QWORD 槽是该原生 C++ 类型/ABI 的布局选择；参与 recurrence、twist 和 temper 的仍是每槽低 32 位。2026-08-15 续审后，Web 端改用 `uintptr_t mt[624]`、真实 cursor 指针和虚析构，因而在 64/32 位 ABI 上分别恢复 `0x1398/0x9CC` 对象大小；算法仍显式只消费每槽低 32 位。

全局指针没有在四个参考二进制的正常对象析构链中释放；BlinkController 和 WindEmitter 都只借用它。因此其生命周期是从第一次调用持续到进程结束。当前本地实现同样保留 raw owning global，且删除了并不存在于原生对象中的 `initialized` 字段。续审还恢复了原生确实存在、但旧 Web 结构曾省略的虚析构 vtable。

这也意味着原版第一次并发初始化和并发取数都没有此处可见的同步保证。为了“一比一”不能擅自引入 per-object 锁或改变消费顺序；若将来因 Web 线程模型必须加同步，应作为明确的平台边界单独记录。

## Seed 的精确截断顺序

iOS 反编译直接显示 `steady_clock` 路径；Android 经由对应 chrono helper 获得同类完整宽度 tick。四者的共同可观察顺序是：

```cpp
full_width_ticks = steady_tick_representation();
quotient = full_width_ticks / 1000000;
seed = uint32_t(quotient);
```

旧本地实现先执行近似于 `int32_t(full_width_ticks)`，再除以 `1000000`。这会丢掉商所需的高位，并且几乎总是生成完全不同的 seed。修复后的实现是：

```cpp
return static_cast<uint32_t>(now / 1000000);
```

32 位 helper 的返回 ABI 是双寄存器宽值，64 位 helper 的最终消费只需要低 32 位；不能因为最终 seed 是 32 位，就把除法本身提前降成 32 位。

## MT19937 twist 的精确 bit 语义

四个参考的数学等价形式如下：

```cpp
for (int i = 0; i < 624; ++i) {
    uint32_t next = mt[(i + 1) % 624];
    uint32_t y = (mt[i] & 0x80000000u) |
                 (next & 0x7ffffffeu);
    uint32_t mag = (next & 1u) ? 0x9908b0dfu : 0u;
    mt[i] = mt[(i + 397) % 624] ^ (y >> 1) ^ mag;
}
cursor = &mt[0];
left = 624;
```

`0x7ffffffe` 不是误反编译，也不能机械改成“看起来更标准”的 `0x7fffffff`。native 把 bit 0 从待右移的 `y` 中清掉，同时单独用原始 `next & 1` 选择 `MATRIX_A`；它与标准 MT twist 等价。

旧本地实现错误地测试 `(y & 1)`。由于 `y` 已经与 `0x7ffffffe` 做过 AND，这个条件恒为 0，导致所有应异或 `0x9908b0df` 的状态字都走错分支。

原生代码按 wrap 边界拆成多段循环，Android ARM64 又把 regeneration 内联到 canonical 的两个 exhaustion 路径。当前 portable modulo 循环依赖原地更新顺序，和原生三段循环等价：前 227 项读取旧的远端状态，后半段有意读取本轮已经更新的低索引状态，最后一项也有意使用本轮的新 `mt[0]`。

## Temper 与 canonical double

每个 raw MT word 恰好执行一次完整 temper：

```cpp
y ^= y >> 11;
y ^= (y << 7)  & 0x9d2c5680u;
y ^= (y << 15) & 0xefc60000u;
y ^= y >> 18;
```

`nextCanonical` 连续消费两个已经 temper 完成的 word：

```cpp
uint32_t low  = nextTemperedWord();
uint32_t high = nextTemperedWord();

uint64_t bits = 0x3ff0000000000000ull |
                uint64_t(low) |
                (uint64_t(high & 0xfffffu) << 32);
return bit_cast<double>(bits) - 1.0;
```

第一个 word 填 mantissa bit 0..31，第二个 word 的低 20 位填 bit 32..51。旧本地实现对 `high` 又做了一次 `((high >> 18) ^ high)`，相当于把 temper 的最后一步重复两次，因而破坏了上面 20 个 mantissa bit。

结果严格位于 `[0, 1)`。Blink/Wind 调用者随后先把返回的 double 转成 float，再执行区间乘法或 `0.0625f` 阈值比较；四个平台的 caller 汇编均显示 double-to-float conversion 位于 multiply/compare 之前。本地调用者当前保留这一舍入顺序。

## Blink 与 Wind 的消费边界

### Blink

- Eye builder 只为 `enabled` 条目分配 BlinkController；每个实际 ctor 无条件消耗一次 canonical double 初始化 `blinkTimer`。
- Blink step 只在 phase 11（闭眼 hold）倒计时到期时再消耗一次，并将 phase 改为 12。
- 随机间隔计算顺序是 `float rng = float(nextCanonical()); min + (max - min) * rng`。
- 因为 RNG 是进程全局，Eye metadata 的条目顺序和启用状态会改变所有后续 Blink/Wind 结果。

### Wind

- `emitAccumulator = abs(velocity) * dt + emitAccumulator`。
- 当 accumulator 为 `>= 0.0f` 时至少执行一次 emission attempt；每次 attempt 无论是否发射成功都会消耗第一个随机数，然后把 accumulator 减 1。
- 当 `float(rng) < 0.0625f` 时按索引从小到大寻找第一个 inactive slot。
- 四个平台都只检查 slot `[0,127]`。池满时不读 slot 128，直接跳过这次发射；旧源码中“会越界读一个控制字”的注释来自旧二进制/旧解读，已删除。
- 找到空槽后复制 `startPos` 的 float bits 到 `lifePos`，再消耗第二个随机数，计算 `yLo + (yHi - yLo) * float(rng)`。
- 即使 slot 池已满，只要第一个随机数低于阈值，仍不会消耗生成 Y 的第二个随机数，因为第二次取数位于成功找到空槽之后。

## 本地修复与回归覆盖

涉及文件：

- `cpp/plugins/motionplayer/EmoteBlinkRng.h`
- `cpp/plugins/motionplayer/EmoteBlinkRng.cpp`
- `cpp/plugins/motionplayer/EmoteBlinkController.h/.cpp`
- `cpp/plugins/motionplayer/EmoteWindEmitter.h/.cpp`
- `cpp/plugins/motionplayer/EmoteEngine.h/.cpp`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

除三处 RNG 算法修复外，本轮还做了两类证据卫生处理：

- 删除 Blink/Wind 这条调用链中已经失效的旧 `libkrkr2.so` 函数地址，把当前地址集中保留在本分析文件；
- 把原先声称 Wind 满池会读取 slot 128 的注释改为四当前参考实际行为。

确定性回归直接调用当前带 seed 参数的 RNG 构造函数并使用 MT19937 seed `5489`；这同时覆盖构造 recurrence。其前六个标准 tempered word 是：

```text
3499211612, 581869302,
3890346734, 3586334585,
545404204, 4161255391
```

按参考 canonical bit 拼接得到前三个结果：

```text
0.9138095996128959
0.19518461779778873
0.4823905248516196
```

该用例会同时捕获 twist parity 错误与第二 word 重复 temper 错误。时钟 seed 的截断顺序是运行时非确定边界，不用伪造固定时钟 fixture；其表达式在源码和四平台反编译证据中直接核对。

## 验证状态

- 四份 IDB：Blink/Wind caller 已重命名、补型、新鲜反编译并保存；RNG get/next/seed/regenerate 与 Eye builder/ctor 同样已命名和补型。
- `cmake --build --preset "Web Debug Build"`：通过。
- `cmake --build --preset "Wasmtime Headless Debug Build"`：通过。
- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过，仅有仓库既有的 `_tss` warning。
- 独立 Emscripten/Node smoke test 直接链接当前 `EmoteBlinkRng.cpp` 并实际执行 seed 5489 的前三次 canonical 输出：通过，输出分别为 `0.91380959961289587`、`0.19518461779778873`、`0.4823905248516196`。
- `git diff --check`：通过；仅输出工作区既有的 LF/CRLF 转换提示。
