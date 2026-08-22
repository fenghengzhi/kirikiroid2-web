# MotionPlayer Wind emitter 构造、步进与槽位边界（四参考，2026-08-15）

## 结论

本轮用 `reference/binaries/` 中四个当前参考重新核对了 Wind emitter，纠正了 Web 版遗留的一个真实对象生命周期偏差：原版并不是“默认构造整个零值对象，再调用 `init(start,end)`”，而是 `new EmoteWindEmitter(start,end)`。该构造函数只清除 128 个 12 字节槽位各自的 `active` 字节；槽位 padding、未激活槽位的 `lifePos/yPos` 以及 `gate` 后的三字节 padding 都保持分配时的未定内容。

A64 保留了独立构造函数，另外三个优化产物把相同构造逻辑内联到 Engine 的 Wind setter。四份参考的对象大小、字段写入集合、槽位步长、RNG 消费顺序、推进顺序和有序浮点比较一致。

源码因此改为真实两参数构造，去掉粒子和 emitter 字段的 in-class 零初始化，并把旧的反编译伪代码式 `v4...v17` 步进函数改写为语义化但保持运算/写入次序的实现。

## 函数映射

| 参考 | `EmoteWindEmitter_step_guess` | 大小 | Engine Wind setter |
|---|---:|---:|---:|
| Android A64 | `0x665BC8` | `0x154` | `0x66DD8C` |
| Android A32 | `0x554E4C` | `0x14E` | `0x559900` |
| iOS A64 | `0x1001A5A24` | `0x154` | `0x1001AC718` |
| iOS A32 | `0x1A4FEC` | `0x170` | `0x1ABF24` |

独立构造函数仅在 Android A64 中保留：

- 函数：`0x66DEDC`，大小 `0x220`；
- 本轮恢复名：`EmoteWindEmitter_ctor_guess`；
- 恢复原型：`void (void *self, float startPos, float endPos)`；
- 唯一调用：setter 内 `0x66DE5C`，紧跟 `operator new(0x61C)`，随后才把新指针发布到 Engine owner 字段。

另外三份参考中的等价内联边界：

| 参考 | 分配 | active 清零循环 | endpoint/tail 写入 | owner 发布 |
|---|---:|---:|---:|---:|
| Android A32 | `0x5599A0` | `0x5599C4..0x5599CC` | `0x5599DA..0x5599FC` | `0x559A04` |
| iOS A64 | `0x1001AC7B8` | `0x1001AC7D8..0x1001AC7E4` | `0x1001AC7F0..0x1001AC814` | `0x1001AC818` |
| iOS A32 | `0x1ABFCE` | `0x1ABFF2..0x1ABFFA` | `0x1AC006..0x1AC024` | `0x1AC02C` |

## 精确对象布局与构造写入集合

分配大小恒为 `0x61C`（1564）字节：

```text
+0 .. +1535    EmoteWindParticle slots[128], stride 12
                  +0  uint8 active
                  +1  3-byte padding
                  +4  float lifePos
                  +8  float yPos
+1536            float startPos
+1540            float endPos
+1544            uint8 gate
+1545            3-byte padding
+1548            float yHi
+1552            float yLo
+1556            float velocity
+1560            float emitAccumulator
```

构造函数的可见写入严格为：

1. 写入 `startPos`；
2. 对 `i = 0..127` 写 `slots[i].active = 0`，步长严格为 12；
3. 写入 `endPos`；
4. 写入 `gate = 0`；
5. 写入四个尾部 float：`{ yHi, yLo, velocity, emitAccumulator } = { 1, 0, 0, 0 }`。

Android A64 使用位于 `0x14D3300` 的 16 字节常量完成最后一步；重新读取的字节是 `00 00 80 3F` 后接十二个零。其余参考用 32/64 位组合 store 写出相同值。

构造函数不写：

- 任一槽位的三字节内部 padding；
- 未激活槽位的 `lifePos`；
- 未激活槽位的 `yPos`；
- `gate` 后的三字节 padding。

因此旧 Web 版的 `EmoteWindParticle` 字段默认值、`slots[128] = {}` 和 emitter 各字段默认值会生成额外 store，不能视为无害的源码风格差异。本轮已移除这些隐式初始化，并用 placement-new 到预填充 `0xA5` 的存储测试精确锁定构造写入集合。

## setter 中的 owner 生命周期

Engine Wind setter 的 owner 字段分别位于 Engine 的：

- Android A64 `+1128`；
- Android A32 `+564`；
- iOS A64 `+760`；
- iOS A32 `+380`。

重建路径顺序为：

```text
读取旧 owner
  -> 若 endpoint cache 改变，delete 旧对象
  -> 不先清空 Engine owner 字段
  -> operator new(0x61C)
  -> EmoteWindEmitter(startPos / metadataScale,
                      endPos / metadataScale)
  -> 将新指针发布到 Engine owner 字段
  -> 写五个 Wind cache
  -> 写 emitter yHi/yLo/gate/velocity/emitAccumulator
```

旧对象删除后、分配前不会把 owner 字段置空。因此若 `operator new` 抛出，Engine 字段暂时仍是已经释放的旧地址；当前源码原样保持这个 raw-owner publication 顺序。停止路径则是 `delete` 后明确置空，并保留五个 cache 不变。

Android A64/iOS A64 的停止条件为：幅度零、端点相等，或两个频率都为零。两个 32 位参考保留了其编译版本的非对称条件：幅度零或 `freqX` 为零。该差异属于 setter，而不是 emitter 构造/step；现有 Web 端的指针宽度条件分支继续保留。

## `step(dt)` 数据流和调用链

内部 `step` 不读取 `gate`。Engine frame slice 的上层调用者先检查 gate，只有 gate 已由 setter 置 1 时才调用它。

每次调用的数据流严格如下：

```text
velocity
  -> if (value < 0) value = -value
  -> value * dt + emitAccumulator
  -> 回写 emitAccumulator
  -> ordered >= 0 ?
       do
         process-global lazy MT19937
           -> canonical double
           -> cast float chanceRoll
         chanceRoll < 1/16 ?
           -> 从 slot 0 开始找第一个 active == 0
           -> 找到：active=1, lifePos=startPos
           -> 再取一个 canonical double 并 cast float
           -> yPos = yLo + (yHi-yLo)*positionRoll
           -> 满池：不取第二个随机数
         emitAccumulator += -1
       while ordered >= 0
  -> 遍历全部 128 个槽
       active != 0 ?
         lifePos += velocity*dt
         velocity > 0 && lifePos > endPos ? active=0
         velocity < 0 && lifePos < endPos ? active=0
```

关键四参考边界：

| 语义 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| ordered `< 0` 绝对值分支 | `0x665BFC` | `0x554E78` | `0x1001A5A54` | `0x1A5026` |
| accumulator ordered `>= 0` | `0x665C10` | `0x554E96` | `0x1001A5A6C` | `0x1A5044` |
| chance RNG/`< 1/16` | `0x665C24..0x665C34` | `0x554EAC..0x554EC4` | `0x1001A5A80..0x1001A5A90` | `0x1A505C..0x1A5074` |
| 满池安全退出 | `0x665C54` | `0x554EDA` | `0x1001A5AB0` | `0x1A5088` |
| slot 激活/起点写入 | `0x665C5C..0x665C64` | `0x554EE0..0x554EE8` | `0x1001A5AB8..0x1001A5AC0` | `0x1A508E..0x1A5096` |
| 第二 RNG/y 写入 | `0x665C74..0x665C88` | `0x554EF4..0x554F10` | `0x1001A5AD0..0x1001A5AE4` | `0x1A50A4..0x1A50C0` |
| accumulator 减一/回环 | `0x665C8C..0x665C9C` | `0x554F14..0x554F28` | `0x1001A5AE8..0x1001A5AF8` | `0x1A50C8..0x1A50DC` |
| 128 槽推进开始 | `0x665CA0` | `0x554F30` | `0x1001A5AFC` | `0x1A50E4` |
| strict direction kill | `0x665CEC..0x665CF0` | `0x554F66..0x554F84` | `0x1001A5B48..0x1001A5B4C` | `0x1A511E..0x1A513C` |

## 边界行为

- `active` 是非零即活动；spawn 写精确值 1，kill 写精确值 0。
- 槽位搜索严格覆盖 `[0,127]`。满池不会访问 slot 128。
- 每个 emission attempt 都先消费 chance RNG；只有 chance 通过且找到空槽时才消费第二个 y RNG。
- 新创建的粒子参加同一次调用末尾的 128 槽遍历，因此可在 spawn 的同一调用中推进，甚至立即越界并被 kill。
- endpoint 比较严格：到达 `endPos` 恰好相等时仍活动，只有越过才 kill。
- `velocity == +0` 和 `velocity == -0` 均不选择任何 kill 方向，即使粒子已在 endpoint 外也保留。
- 绝对速度是显式 ordered `< 0` 分支，不是可任意替换的数学 `fabs`。NaN 不进入取反分支，负零也不进入。
- velocity 为 NaN 时 accumulator 变 NaN，emission gate 的 ordered `>= 0` 为 false；活动粒子的 `lifePos` 变 NaN，而两个 kill 比较都为 false，所以粒子保持 active。
- `0 * infinity` 仍按浮点运算产生 NaN；源码没有短路或零速度特判。
- `emitAccumulator == -0` 通过 ordered `>= 0`，会进入一次 attempt。
- accumulator 为负无穷时跳过 emission；为正无穷时每次减一仍是正无穷，原算法会无限循环。
- 负 `dt` 没有 clamp：它可以减少 accumulator，并让粒子按 velocity 的反方向改变位置；kill 方向仍仅由 velocity 符号决定。
- y 范围不排序、不 clamp，直接计算 `yLo + (yHi-yLo)*roll`。
- `step` 不修改 gate、startPos、endPos、yHi、yLo 或 velocity。

## 源码迁移

涉及文件：

- `cpp/plugins/motionplayer/EmoteWindEmitter.h`
- `cpp/plugins/motionplayer/EmoteWindEmitter.cpp`
- `cpp/plugins/motionplayer/PlayerCore.cpp`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

改动：

1. `init(start,end)` 改为真实 `EmoteWindEmitter(start,end)` 构造函数；
2. `setWind_guess` 改为直接 `new EmoteWindEmitter(start,end)`；
3. 去掉会清零未写字段的 in-class initializer；
4. 增加 12 字节粒子布局、1564 字节 emitter 布局及关键 offset 的编译期断言；
5. 删除旧的 `a1/v4...v17` 反编译伪代码注释和局部变量，按真实数据流语义命名；
6. 保持 chance/y RNG、first-free、accumulator store、同调用推进和 strict kill 的顺序。

## 验证

- Emscripten 单元测试翻译单元语法检查通过；只有仓库既有 `_tss` literal-operator 警告。
- Web Debug 全量编译/链接完成；首次前端等待超时后底层链接继续完成，随后增量复核为 `ninja: no work to do`。
- 单独把 `EmoteWindEmitter.cpp`、`EmoteBlinkRng.cpp` 和运行时 smoke harness 编译为 Emscripten single-file JS，并在 Node 执行通过。
- 新增测试覆盖：
  - placement-new 到 `0xA5` 存储，确认构造只清 active 和写明确尾字段；
  - 正/负方向 endpoint 相等存活、严格越界 kill；
  - NaN velocity 的 accumulator/位置传播与 ordered-kill 保活；
  - 负零 velocity 不选择 kill 分支；
  - seed 5 的确定性 spawn：同调用推进并越界死亡；
  - 成功 spawn 消费 chance+y 两个 canonical draw；
  - 满池只消费 chance draw，不消费 y draw。

