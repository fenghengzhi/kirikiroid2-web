# MotionPlayer Blink/Wind RNG 对象结构、构造与跨块消费（四参考，2026-08-15）

## 结论

本轮在既有 MT twist/canonical 审计之上，重新恢复了共享 Blink/Wind RNG 对象自身的源码结构和生命周期。四份参考共同表明它不是当前 Web 旧实现中的简单 `{ uint32_t mt[624], left, pos }` 聚合体，而是一个有虚析构的多态对象：

```text
vptr
int left
alignment padding on 64-bit
pointer-width cursor
pointer-width mt[624]
```

状态槽虽随 ABI 为 64/32 位，seed recurrence、twist 和 temper 始终只使用低 32 位。这个结构使对象大小在 A64 上精确为 `0x1398`，在 A32 上精确为 `0x9CC`。

重新读取四份内联构造还发现了一个旧测试未覆盖的 Web 错误：原生第一项 seed recurrence 是标准的 `+1`，而旧 `get()` 的反编译式索引表达式在首项实际算成了 `+2`。过去 seed 5489 测试手工填充 state，绕开了生产 `get()` 构造路径，所以没有抓到该错误。

源码现已恢复为：

- `EmoteBlinkMt19937(uint32_t seed)` 真实构造函数；
- 虚析构；
- `left`、真实 state cursor、`uintptr_t mt[624]`；
- pointer-width 对象大小断言；
- `EmoteBlinkRng_getGlobal_guess` 的 lazy raw-global publication；
- `EmoteBlinkRng_nextCanonical_guess` 的两 word/cross-regeneration 精确消费。

## 函数映射

| 参考 | get global | 大小 | next canonical | 大小 | seed helper | regenerate |
|---|---:|---:|---:|---:|---:|---:|
| Android A64 | `0x9F0308` | `0xAC` | `0x9F00D0` | `0x238` | `0xA2A308` | 内联两份 |
| Android A32 | `0x7508D4` | `0x72` | `0x750838` | `0x9A` | `0x770E54` | `0x750578` |
| iOS A64 | `0x1002C24B0` | `0x98` | `0x1002C23E0` | `0xD0` | `0x1002964AC` | `0x1002C223C` |
| iOS A32 | `0x2C7878` | `0xF6` | `0x2C77DC` | `0x9A` | `0x29AAAC` | `0x2C7692` |

IDB 恢复名仍使用 `_guess`，因为参考已 strip：

- `EmoteBlinkRng_getGlobal_guess`
- `EmoteBlinkRng_nextCanonical_guess`
- `EmoteBlinkRng_seedFromSteadyClock_guess`
- `EmoteBlinkMt19937_regenerate_guess`

Android A64 将 regenerate 各内联一次到 canonical 的“第一个 word 前耗尽”和“两个 word 之间耗尽”路径。其他三份参考调用独立 helper。

## 虚析构与 vtable 证据

四份构造路径都会在对象 offset 0 写入 vtable address point：

| 参考 | vtable address point | complete destructor | deleting destructor |
|---|---:|---:|---:|
| Android A64 | `0x1A2D760` | `0x9F03B4` | `0x9F03C8` |
| Android A32 | `0x10C45E8` | `0x750960` | `0x750970` |
| iOS A64 | `0x101AFD0B8` | `0x1002C255C` | `0x1002C2570` |
| iOS A32 | `0x1840498` | `0x2C7998` | `0x2C79A8` |

每个 complete destructor 仅把本类 vptr 写回后返回；第二个 slot 是跳转到 `operator delete(void*)` 的 deleting destructor。vtable 前的 offset-to-top/RTTI 区均为零，符合 strip 后无 RTTI pointer 的单继承主对象。

RNG 正常生命周期中没有调用这些析构函数：global owner 第一次初始化后一直存活到进程终止。但虚析构仍是原始类定义的一部分，不能因为正常路径不 delete 就从源码结构中省略。

## ABI 布局

### 64 位参考

```text
+0       vptr                 8 B
+8       left                 4 B
+12      alignment padding    4 B
+16      cursor               8 B
+24      mt[624]              624 * 8 B
sizeof = 0x1398
```

### 32 位参考

```text
+0       vptr                 4 B
+4       left                 4 B
+8       cursor               4 B
+12      mt[624]              624 * 4 B
sizeof = 0x9CC
```

这解释了 64 位反编译中的 QWORD state stride。`uintptr_t` 是 Web 端最直接的可移植表达：它在两类指针宽度下同时复原对象大小和槽位步长；所有算法入口再显式转成 `uint32_t`，保留“高半部不参与”的原版语义。

Android 二进制字符串还保留了与本算法参数完全一致的 `mersenne_twister_engine<unsigned long,32,624,397,...>` template 实例；这与 LP64 下 `unsigned long` 为 8 字节、ILP32 下为 4 字节的槽宽一致。由于实际对象另有虚析构 wrapper 且原类符号已 strip，本轮不把 wrapper 的猜测类名当成事实。

## lazy 构造和 publication 顺序

四份 get-global 都是普通 unsynchronized raw pointer：

```text
load global
if null:
    operator new(native object size)
    seed = low32(steady_clock_ticks / 1000000)
    construct object(seed)
    publish global = object
return object
```

关键顺序是 `operator new -> seed helper -> vptr/state construction -> global publication`。这正是带参数 new-expression 的自然形状：先取得内存，再求 constructor argument，随后执行 constructor，成功完成后才赋给 global。

因此：

- allocation 或 seed/constructor 异常时 global 仍为空；
- global 不会指向半构造对象；
- 没有 `__cxa_guard`、mutex、once flag 或原子 publication；
- 两线程首次并发调用存在重复构造/泄漏/竞态可能，不能擅自用 function-local static 改写；
- 正常路径不释放 global。

## constructor(seed) 精确 recurrence

构造写入顺序在四份优化产物中等价：

```cpp
left = 1;
uint32_t word = seed;
mt[0] = word;
for (uint32_t index = 1; index < 624; ++index) {
    word = 1812433253u * (word ^ (word >> 30)) + index;
    mt[index] = word;
}
cursor = mt;
left = 1;
```

Android/iOS A64 的反编译 loop counter 从 4 到 626，因为 state 首项位于对象的第三个 QWORD 槽之后；其表达式是 `counter + multiplier*... - 3`，即 state index `counter-3`。首轮 counter=4，增量是 `+1`。旧 Web 代码把它误抄成 `counter+1 ... -3`，首轮变成 `+2`。

seed 5489 的构造断言现在直接得到：

```text
mt[0] = 5489
mt[1] = 1301868182
mt[2] = 2938499221
```

不再由测试手工填充 state 来绕开生产构造函数。

## seed helper 宽度

四份 helper 继续确认：

```cpp
full_width_ticks = steady_clock::now().time_since_epoch().count();
quotient = full_width_ticks / 1000000;
seed = uint32_t(quotient);
```

- Android A64：`0xA2A338`；
- Android A32：宽除法位于 `0x770E78`，caller 使用低 32 位；
- iOS A64：`0x1002964DC`；
- iOS A32：`0x29AACC`。

必须先做完整 tick-width 除法，再截断商。Web 源码保持这个顺序。

## regenerate 的容器依赖

三个独立 helper 都把标准 MT wrap 拆成：

1. 前 227 项：读取尚未更新的远端 `i+397`；
2. 后 396 项：读取本轮已经更新的低索引 `i+397-624`；
3. 最后一项：使用已更新的 `mt[0]` 与 `mt[396]`。

每项的低 32 位公式：

```cpp
next = low32(mt[(i+1) % 624]);
y = (low32(mt[i]) & 0x80000000) |
    (next & 0x7ffffffe);
mag = (next & 1) ? 0x9908b0df : 0;
mt[i] = low32(mt[(i+397) % 624]) ^ (y >> 1) ^ mag;
```

Web 的单个 in-place modulo loop 保留同一读写依赖。不能先复制整份旧数组再统一计算，因为后 397 项需要读取本轮的新 state。

regenerate 还把 `cursor=mt`、`left=624`。Android A64 的两个内联副本在 `0x9F0118..0x9F01C4` 与 `0x9F0208..0x9F02AC`；其余独立 helper 入口见映射表。

## `left`/cursor 与两 word canonical

每个 low-32 tempered word 的抽象操作是：

```cpp
previousLeft = left;
left = previousLeft - 1;
if (previousLeft == 1)
    regenerate();
word = low32(*cursor++);
temper(word);
```

构造完成时 `left=1,cursor=mt`，所以第一次 canonical 的第一个 word 前必定 regenerate。一个 canonical 连续执行两次取 word，再按下式拼接：

```cpp
low  = temperedWord1;
high = temperedWord2;
bits = 0x3ff0000000000000 |
       uint64_t(low) |
       (uint64_t(high & 0xfffff) << 32);
return bit_cast<double>(bits) - 1.0;
```

正常首次调用之后状态为：

```text
cursor = mt + 2
left   = 623
```

`left` 的约定允许一次 canonical 在两个 word 中间跨块：若入口设为与原生不变量一致的 `left=2,cursor=mt+623`，第一个 word 消费旧块最后一项，第二个 word 前 regenerate，第二个 word 消费新块 `mt[0]`，最终状态为：

```text
cursor = mt + 1
left   = 624
```

新回归直接锁定这个边界。把 canonical 写成“先确保至少有两个 word，否则一次性 regenerate”会丢掉旧块最后一个 word，改变整个共享随机流。

## 源码迁移

涉及：

- `cpp/plugins/motionplayer/EmoteBlinkRng.h`
- `cpp/plugins/motionplayer/EmoteBlinkRng.cpp`
- `cpp/plugins/motionplayer/EmoteBlinkController.cpp`
- `cpp/plugins/motionplayer/EmoteWindEmitter.cpp`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

改动：

1. 恢复 seed constructor 与 virtual destructor；
2. 恢复 `left/cursor/pointer-width mt[624]` 顺序；
3. 编译期断言 64/32 位大小分别为 `0x1398/0x9CC`；
4. 修正生产 lazy constructor 的 recurrence off-by-one；
5. `pos` 索引改回真实 cursor 指针；
6. 调用面统一为带 `_guess` 的恢复名；
7. 清除 `v1/v2...` 旧伪代码注释和局部名；
8. 保留 raw global、无同步、双 word 和中途 regenerate 次序。

## 验证

- 完整 `motionplayer-dll.cpp` Emscripten 语法检查通过，仅有仓库既有 `_tss` warning。
- 独立 Emscripten/Node runtime smoke 通过：
  - WASM32 `sizeof(EmoteBlinkMt19937) == 0x9CC`；
  - `has_virtual_destructor == true`；
  - seed 5489 构造 state 首项正确；
  - 前三 canonical 为 `0.9138095996128959`、`0.19518461779778873`、`0.4823905248516196`；
  - 两 word 中途 regenerate 后 `left=624,cursor=mt+1`。
- `Web Debug Build` 完整编译/链接通过。

