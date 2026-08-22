# motionplayer DrawDevice root-state 四字节 dormant / writer-only 边界（四参考二进制）

日期：2026-08-15

## 结论

`DrawDeviceObjectBase` 主基类在 screen pair 与第一个有序容器之间保存恰好四个字节：

```cpp
uint8_t RootStateByte0_guess;
uint8_t RootStateByte1_guess;
uint8_t RootStateByte2_guess;
bool RenderTextureDirty_guess;
```

四端构造都从 group 起点执行一次 32-bit zero store，因此四个字节的构造值都是 0。
构造后的访问集合严格分成两类：

- byte 0/1/2：没有 read，没有 write，也没有析构清理；
- byte 3：四个公开 writer 在特定分支写 1，没有 reader，也没有任何路径写回 0。

第四字节与 screen-size target invalidation、`forceRenderTexture` 属性修改共现，因此
`RenderTextureDirty_guess` 是合理的行为提示，但不是已恢复原字段名。尤其不能再沿用旧
`libkrkr2.so` 阶段的“Show 读取并清 dirty”注释：当前四份参考没有这种机器码。

## 布局与构造

| 目标 | group 偏移 | byte 3 偏移 | ctor | 合并清零 store |
|---|---:|---:|---:|---:|
| Android arm64 | `+0x40` | `+0x43` | `0x531274` | `0x5312E0` |
| Android armv7 | `+0x28` | `+0x2B` | `0x495618` | `0x495662` |
| iOS arm64 | `+0x40` | `+0x43` | `0x100233C88` | `0x100233CE0` |
| iOS armv7 | `+0x28` | `+0x2B` | `0x23295C` | `0x2329B6` |

32-bit store 不是 padding 的偶然覆盖：四端都明确把它放在 screen pair 初始化后、第一个
标准库容器构造前。恢复源码必须保留四个构造为 0 的状态字节；但只有 byte 3 有后续行为。

## 全访问集合

本轮按十个函数一批重新反编译并扫描了：

- 四端 39/42/44/44 个当前已命名 `DrawDeviceObjectBase*` 函数；
- primary vtable 的 12 个槽，包括 capture、Show、completion、manager、cursor 与析构；
- 已知从 child/manager 反向读取 root 的跨对象消费者。

去重后审计的 root 函数数为 A64 51、A32 54、I64 56、I32 56。除构造合并清零外，
前三个 byte 没有命中；第四 byte 每端恰好四个 post-constructor store。

| writer | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `setForceRenderTexture` | `0x52BA44` | `0x492DD0` | `0x100230EE8` | `0x22FD46` |
| `setScreenRect` changed branch | `0x52BB00` | `0x492E50` | `0x100230F78` | `0x22FDAA` |
| `setScreenWidth` changed branch | `0x529AE8` | `0x492262` | `0x10022FF94` | `0x22F0D6` |
| `setScreenHeight` changed branch | `0x529B4C` | `0x49229E` | `0x10023001C` | `0x22F120` |

每个命中都是 byte store `1`。没有 load、compare、test、copy 或 zero store。

## 四个 writer 的边界

### `setForceRenderTexture`

没有相等检查。它总是保存传入 bool，并写 byte 3 = 1；不 release target。A64、I64、I32
机器码先写 force 值再写 state byte，A32 先写 state byte 再写 force 值。两个 store 独立、
非 volatile 且都不能抛异常，这个单平台重排不提供不同源码顺序的证据；恢复源码维持普通
“保存属性，再标记”表达即可。

### `setScreenRect`

总是先保存 concrete `ScreenLeft/ScreenTop`。只有 `ScreenWidth/ScreenHeight` 任一变化才：

1. 写新的 screen pair；
2. release/null FrontTarget；
3. release/null BackTarget；
4. 写 byte 3 = 1。

pair 相等时 byte 3 不变；left/top 即使变化也不会写它。

### 单字段 screen setter

`setScreenWidth` / `setScreenHeight` 都先比较旧值。相等严格 no-op；变化时写新值，按 Front
后 Back 的顺序 release/null target，最后写 byte 3 = 1。两者不处理 `CurrentTarget`。

## 为什么不能把 byte 3 当成已证实 dirty flag

如果这是当前版本真正被 Show 消费的 dirty flag，至少应出现某种 load/test，并通常在
处理后清回 0。四端均不存在这些动作。当前能证明的只有：

```text
initial value = 0
selected public mutations => value = 1
no in-plugin observation
no reset
```

它可能是旧实现残留、为链接外/内联外代码保留的状态、未来版本字段，或只是源码中仍被
赋值但优化后没有业务效果的 member。四份 stripped 二进制无法区分。因此：

- 字段必须保留，四个 writer 必须保留；
- 名称必须带 `_guess`；
- 不新增 getter、Show branch 或 per-frame reset；
- 测试只验证 writer 的外部伴随行为，不伪造无法观察的 state-byte 断言。

## 析构与对象复用边界

主基类析构 A64 `0x53244C`、A32 `0x49606C`、I64 `0x100233E1C`、I32 `0x232B14`
不读取也不清空四字节 group。对象内存随后由 concrete deleting dtor 释放，故没有独立状态
生命周期。若通过 placement-new 风格重用同一存储，主基类构造的合并 zero store 会重新
初始化全部四字节；普通 target release、transition stop、Show/capture 均不会重置它们。

## IDB / 源码落点

四份 recovery IDB 的完整主基类 ABI 类型都把 group 拆成四个成员；constructor、
`setForceRenderTexture`、`setScreenRect`、两个单字段 screen setter 已应用精确类型。group
清零、四个 byte-3 stores 与析构负证据均加入注释/书签并保存。

`cpp/plugins/DrawDeviceD3D.cpp` 保留三个 `_guess` byte 与一个 `_guess` bool。源码注释现在
明确：前三字节 constructor-only；第四字节 write-one-only、无 reader、无 reset。绝对
地址只保存在本文和 recovery IDB，不进入编译单元。
