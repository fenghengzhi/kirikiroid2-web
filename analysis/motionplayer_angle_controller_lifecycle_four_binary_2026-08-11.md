# MotionPlayer `EmoteAngleController` 生命周期、12B deque 与 phase 边界四端复原

> **2026-08-16 unordered 边界勘误：** 四端 setter 都是 `B.LE/BLE` 跳入
> immediate，NaN 的 unordered 标志满足 `LE`。因此 NaN duration 会清 queue 并
> 立即提交归一化后的 angle，不会入队；正文旧表述已更正。与 Var 的 `B.LS/BLS`
> 分叉见
> `analysis/motionplayer_controller_duration_unordered_split_four_binary_2026-08-16.md`。

日期：2026-08-11

## 范围与证据原则

本轮重新检查角度 controller 的构造、字段初始化、逐帧 step、Engine 调用点、
析构和共享 12 字节 keyframe deque。四份 `reference/binaries/` 当前参考镜像共同
作为权威；旧 `libkrkr2.so` 地址和端口注释不作为证据。

这次复核推翻了两个会影响源码结构和运行结果的旧结论：

1. 旧名 `EmoteAngleController_ctor_12Bdeque` 对应的函数只是
   `std::deque<12-byte POD>` 初始化器，眼睛、眉毛、嘴、selector 与根 angle
   都会调用它；它不是完整角度 controller 的构造函数。
2. state-0 setup 并不保留旧 phase。Android ARM64、Android ARMv7、iOS
   ARM64 用一个“power word + zero word”的成对宽存储同时写 `powCount` 和
   `phase=0`；iOS ARMv7 拆成两个 32 位 store。四端语义完全一致。

## 四端函数和调用点映射

| 目标 | Engine ctor | angle 分配/初始化点 | root-controller step | angle step | Engine dtor / angle teardown |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `0x67B76C` | `0x67BA8C` | `0x673AC0` | `0x663A14` | `0x67C898`, inline at `0x67CA30` |
| Android ARMv7 | `0x560948` | `0x560B0E` | `0x55BFD4` | `0x553B98` | `0x5610E8` -> owner wrapper `0x563C44` -> deque dtor `0x565568` |
| iOS ARM64 | `0x1001B7FB0` | `0x1001B80A4` | `0x1001AFD50` | `0x1001A43C0` | `0x1001B8B4C` -> deque dtor `0x1001B6D80` |
| iOS ARMv7 | `0x1B7788` | `0x1B78CA` | `0x1AF4A4` | `0x1A3838` | `0x1B814E` -> deque dtor `0x1B6A38` |

四个 root-controller step 的顺序都是：

```text
position controller -> Player root coord
color controller    -> Player root color
scale controller    -> Engine reciprocal scale + Player root zoom
angle controller    -> Player setAngleRad
```

angle 指针在四个 Engine 对象中的偏移分别是 `+1096`、`+548`、`+728`、
`+364`。

## 完整对象布局与构造写覆盖

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| object size | `0x70` | `0x44` | `0x50` | `0x34` |
| 12B deque | `+0..+79` | `+0..+39` | `+0..+47` | `+0..+23` |
| `state` | `+80` | `+40` | `+48` | `+24` |
| `currentRad` | `+84` | `+44` | `+52` | `+28` |
| `targetRad` | `+88` | `+48` | `+56` | `+32` |
| `startRad` | `+92` | `+52` | `+60` | `+36` |
| `invDuration` | `+96` | `+56` | `+64` | `+40` |
| `powCount` | `+100` | `+60` | `+68` | `+44` |
| `phase` | `+104` | `+64` | `+72` | `+48` |
| trailing ABI padding | `+108..+111` | none | `+76..+79` | none |

四个构造点都只写到 `targetRad`：

- Android ARM64：`memset(object,0,0x50)` 只为 libstdc++ deque header
  清底，调用 12B deque 初始化器后，`STR XZR,[obj+0x50]` 清
  `state/currentRad`，`STR WZR,[obj+0x58]` 清 `targetRad`。
- Android ARMv7：先清 `0x28` 字节 deque header，初始化 deque，然后
  `STRD` 清 `state/currentRad`，再清 `targetRad`。
- iOS ARM64：libc++ 空 deque 无需立即分配 map/block；成组零 store 覆盖
  48 字节 deque header、`state/currentRad`，再清 `targetRad`。
- iOS ARMv7：两个 128-bit zero store 覆盖 24 字节 deque header 和
  `state/currentRad`，随后清 `targetRad`。

因此共同源码级构造边界是：

```cpp
EmoteAngleController::EmoteAngleController()
    : state(0), currentRad(0.0f), targetRad(0.0f) {}
```

`startRad/invDuration/powCount/phase` 没有构造初始化，两个 64 位目标末尾
只有对齐 padding，并不存在旧端口声明的 `int32_t pad` 源字段。调用
immediate angle setter 会再次写 `state/currentRad`，仍不会补写这四个尾字段。
所以在首次 setup 或 unserialize 之前序列化这些字段，会观察 allocator 留下的
未定位模式；这是参考实现的真实边界，不应在移植层擅自全清零。

## `12B deque init` 不是 angle ctor

Android 两端把 libstdc++ 的 deque map/block 初始化保留成独立函数：

| 目标 | 12B deque init | 五个普通构造调用点 |
|---|---:|---|
| Android ARM64 | `0x683B90` | `0x65FD98`, `0x661C38`, `0x6630B4`, `0x66B7A4`, `0x67BAA8` |
| Android ARMv7 | `0x56545C` | `0x551B60`, `0x552D02`, `0x5536C2`, `0x5583CE`, `0x560B24` |

五个 owner 依次对应 eye、eyebrow、mouth、selector 与 Engine root angle。
输入 count 在这些构造路径均为 0。libstdc++ 空 deque 仍建立至少 8-entry map
并分配一个 504-byte block；每块容纳 42 个 12-byte POD。iOS libc++ 空 deque
只保留零 header，首次 push 时才分配存储。

这说明 eye、eyebrow、mouth 的首字段应是裸
`std::deque<EmoteAngleKeyValue12B>`，而不是包含 angle 七个标量字段的完整
`EmoteAngleController`。旧端口的嵌套模型不仅多出不存在的标量 owner，也使
第二个容器的源码级邻接关系错误。当前源码用共享别名
`EmoteAngleKeyframeQueue` 恢复这一结构；selector 仍保留自己真实存在的
`queue + state` command-track wrapper。

## setup 的 power/phase 成对存储

关键指令如下：

| 目标 | 指令点 | 指令语义 |
|---|---:|---|
| Android ARM64 | `0x663BC4` | `STP W10, WZR, [self,#0x64]` |
| Android ARMv7 | `0x553D26` | `STRD R0, R1, [self,#0x3C]`，此时 `R1==0` |
| iOS ARM64 | `0x1001A4598` | `STP W8, WZR, [self,#0x44]` |
| iOS ARMv7 | `0x1A39E4`, `0x1A39E8` | 先写 power，再显式写 `phase=0` |

Hex-Rays 在前三端把它显示为：

```cpp
*(uint64_t *)&self->powCount = (uint32_t)keyframe.powCountBits;
```

这个表达式的高 32 位不是“未写”，而是来自 zero register/zeroed register，
正好覆盖相邻的 phase。旧端口只 `memcpy` 低 32 位，因此让 setter 前保留的
phase 泄漏进新动画；当它为 1 时，下一次 animate 会立即完成。修正后 setter
仍按四端真实行为保留 phase，但首次 state-0 setup 一定把 phase 清零。

## 共同 step 状态机与数据流

源码级共同流程：

```cpp
if (state == 1) {
    phase += invDuration * dt;
    if (phase >= 1.0f) {
        phase = 1.0f;
        currentRad = wrap_6_2832(targetRad);
        state = 0;
    } else {
        currentRad = wrap_6_2832(
            pow(phase, powCount) * (targetRad - startRad) + startRad);
    }
} else if (state == 0 && !queue.empty()) {
    startRad = currentRad;
    targetRad = queue.front().endRad;
    if (targetRad - currentRad > pi)
        targetRad -= 6.28318531f;
    else if (currentRad - targetRad > pi)
        targetRad += 6.28318531f;
    state = 1;
    invDuration = 1.0f / queue.front().duration;
    powCount = queue.front().powCount; // raw float word copy
    phase = 0.0f;
    queue.pop_front();
}
*outRad = currentRad;
```

setup 和 animate 是互斥分支：pop 的当次只完成 setup，并立即输出旧
`currentRad`；下一次 step 才推进 phase。最短路径使用较精确的
`6.28318531f`，结果归一化循环使用截断的 `6.2832f`，两者不能合并。

Android 两端在 power curve 处调用 double `pow`，iOS 两端调用 `powf`；这是
工具链/数学库边界。共同源码的 float 输入和 float 存储不变，但极端输入最后
几位可能因目标数学库不同。

## 析构、owner 与异常清理

角度对象没有 vtable、基类、数组或额外 heap owner；唯一 owned 子对象是
12B deque。正常 Engine 析构顺序是 parts/hair/bust var controllers，angle，
color/scale/position，最后 Player。angle 的源码级析构就是普通默认析构：

```cpp
delete angle; // deque destructor, then object operator delete
```

- Android ARM64 把 libstdc++ deque block/map 释放完全内联进 Engine dtor。
- Android ARMv7 的 `0x563C44` 是 owner-slot wrapper：取指针，调用
  `0x565568` 释放所有 504-byte blocks 和 map，delete object，然后把 owner
  slot 写零。
- iOS ARM64 `0x1001B6D80` 和 iOS ARMv7 `0x1B6A38` 是 libc++ 12B deque
  析构链；它们先 clear/free blocks，再释放 map。相同代码还由其它 naked
  12B track 的正常析构和 ctor unwind cleanup 复用。

12B element 是三个 float 的 trivial POD，析构过程中没有逐字段释放。旧端口的
空 `EmoteAngleController_dtor` free helper 再接 `delete` 是反编译机械展开，
并不是最接近参考源码的结构，现已改为单一 `delete`。

## 边界行为

- `state` 不是 0 或 1 时，step 不改内部状态，只输出 `currentRad`。
- state 0 且队列为空时同样只输出当前值。
- shortest-path 只在差值严格大于 pi 时调整；等于 pi 不调整。
- setter 只允许 ordered-positive duration 入队；非正或 NaN 做 immediate 写并保留 phase。
  phase 的清零点是以后真正消费该 keyframe 的 setup。
- 正常 producer 不会把非正 duration 入队；若外部直接塞入 duration 0，setup
  会得到无 guard 的无穷 `invDuration`。下一次 `dt==0` 时会产生 NaN phase。
- wrap 使用重复加减，不是 `fmod`。NaN 会绕过两组比较并被保存；正负无穷会
  使对应加减循环不终止。
- phase、duration、power、角度都不做 finite 检查。NaN 使 `phase>=1` 为假，
  随后的 power/interpolation 按目标数学库传播 NaN。
- constructor 尾字段未定，但第一次正常 setup 会完整写
  `startRad/targetRad/invDuration/powCount/phase`，随后 animate 不依赖旧尾值。

## 本地源码和测试修正

- `EmoteAngleController` 改为带真实默认构造/默认析构的非多态 owning class；
  只初始化 `state/currentRad/targetRad`，删除假 `pad` 字段和 free ctor/dtor。
- Engine 的 angle owner 改成普通 `new` / `delete`。
- 新增 `EmoteAngleKeyframeQueue` 裸 deque 别名，eye、eyebrow、mouth 的首字段
  不再误嵌完整 angle controller；相关 producer/reset/step 直接操作 deque。
- angle setup 在复制 power 后显式 `phase=0`。
- 单元测试同时锁定两个相邻边界：setter 保留 phase；第一次 setup 清 phase、
  只 pop 一个 command 且仍输出旧 current value。
- 触及源码中的旧单库地址、错误 ctor 身份和“phase 永不重置”说明已清理；地址
  证据集中保留在本页。

## IDB 回写

四端写入并重新反编译的主要名称：

- `EmoteAngleController_step`
- `EmoteEngine_stepRootControllers_guess`
- Android 两端 `EmoteAngleKeyframeDeque_init_guess`
- Android ARMv7 / iOS 两端 `EmoteAngleKeyframeDeque_dtor_guess`
- Android ARMv7 `EmoteAngleController_ownerPtrDestroy_guess`

四端 step prototype 均修正为
`void(void *self, float *outRad, float dt)`；构造写覆盖、phase 成对 store、deque
共享身份和析构顺序均写入对应指令/函数注释。Hex-Rays cache 已强制失效并
fresh decompile，四份 IDB 已原位保存。

## 验证状态

Web Debug 与 Wasmtime Debug 增量构建均成功，并分别再次 dry-run 收敛到
`ninja: no work to do.`。真实 Emscripten 参数下的完整
`motionplayer-dll.cpp -fsyntax-only` 已通过，仅有仓库既有 `_tss`
literal-operator deprecation warning。`git diff --check` 通过，仅报告工作树
既有的 LF/CRLF 转换提示。

## 2026-08-15 增量闭合：setter、状态字典与严格 restore 生命周期

本节只采用四份当前 `reference/binaries/` 的 fresh decompile/disasm、原始字节和
xref。它补齐此前文档未覆盖的 direct setter、七字段序列化以及 restore 的
Variant/accessor 生命周期；旧 `libkrkr2.so` 注释仍不作为证据。

### 函数映射

| 目标 | `setTarget` | `step` | serialize state | restore state |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x663870` | `0x663A14` | `0x663C10` | `0x663DF4` |
| Android ARMv7 | `0x553AD4` | `0x553B98` | `0x553D58` | `0x553EE8` |
| iOS ARM64 | `0x1001A4308` | `0x1001A43C0` | `0x1001A45B8` | `0x1001A4770` |
| iOS ARMv7 | `0x1A3798` | `0x1A3838` | `0x1A3A24` | `0x1A3C70` |

`setTarget` 四端共同流程为：

```cpp
while (endRad < 0.0f)     endRad += 6.2832f;
while (endRad >= 6.2832f) endRad -= 6.2832f;

if (!(duration > 0.0f)) {
    queue.clear();
    state = 0;
    currentRad = endRad;
    return;
}
if (!append) {
    queue.clear();
    state = 0;
}
queue.push_back({endRad, duration, powCount});
```

它不写 `targetRad/startRad/invDuration/powCount/phase`。NaN angle 绕过两组
归一化比较；NaN duration 的 unordered 标志满足原生 `LE`，因此走 immediate。
正负 infinity
仍会卡在对应循环。Android libstdc++ 的 12B deque block 是 504 bytes、容纳
42 个元素；iOS 的具体 block/index 常数来自 libc++ ABI，不应在共享源码中手写。

### 七个宽字符串不是单字符

IDA 的普通 string 识别在 iOS 和部分 Android 位置把 `TJS_W` 字面量显示成
`"p"`、`"t"`、`"s"`、`"e"` 或 `"f"`。按 `ida-search-string` 流程先检查
数据库，再分别搜索普通 string、UTF-8、UTF-16LE 和 UTF-32LE；结果是四端均只
在 UTF-16LE 得到完整的七个 key，UTF-32LE 无命中。`get_bytes` 和 xref 又确认
这些地址同时被本节 serialize/restore 引用。

下表按 `phase, tick, speed, exponent, frame, prev, target` 顺序列地址：

| 目标 | 七个 UTF-16LE 字面量地址 |
|---|---|
| Android ARM64 | `0x14D3828`, `0x14D3890`, `0x14D385C`, `0x14D384A`, `0x14BD76E`, `0x14D3886`, `0x14BFDDC` |
| Android ARMv7 | `0xD842FC`, `0xD84370`, `0xD8433C`, `0xD8432A`, `0xD84308`, `0xD84366`, `0xD777D4` |
| iOS ARM64 | `0x10195FBDA`, `0x10195FC6E`, `0x10195FC3A`, `0x10195FC28`, `0x10195FBE6`, `0x10195FC64`, `0x10195FBF6` |
| iOS ARMv7 | `0x1751F3E`, `0x1751FD2`, `0x1751F9E`, `0x1751F8C`, `0x1751F4A`, `0x1751FC8`, `0x1751F5A` |

例如 iOS ARM64 的 `phase` 原始 bytes 是
`70 00 68 00 61 00 73 00 65 00 00 00`，不是单独的窄字符 `p`。因此本地已有
完整 key 名是正确的，错误只在 IDA 字符串类型。

### serializer 字段、顺序与共享 hint

四端都新建 Dictionary，并严格按下列顺序 `PropSet(TJS_MEMBERENSURE, ...)`：

| key | value source |
|---|---|
| `phase` | `state` |
| `tick` | `phase` |
| `speed` | `invDuration` |
| `exponent` | `powCount` |
| `frame` | `currentRad` |
| `prev` | `startRad` |
| `target` | `targetRad` |

每个 key 使用一个进程级可变 member-hint 槽；同一槽不只由 Angle 使用，还由
Var、eye、eyebrow、mouth、selector 的 state serializer/restorer 复用，`frame`
槽还被 `EmoteEngine_buildVariableList_guess` 读取复用。下表同样按上述七 key
排列：

| 目标 | 七个共享 hint 槽地址 |
|---|---|
| Android ARM64 | `0x1AB4EC0`, `0x1AB4EF4`, `0x1AB4EDC`, `0x1AB4ED8`, `0x1AB4EC4`, `0x1AB4EF0`, `0x1AB4ECC` |
| Android ARMv7 | `0x1111458`, `0x111148C`, `0x1111474`, `0x1111470`, `0x111145C`, `0x1111488`, `0x1111464` |
| iOS ARM64 | `0x101B69F70`, `0x101B69FA4`, `0x101B69F8C`, `0x101B69F88`, `0x101B69F74`, `0x101B69FA0`, `0x101B69F7C` |
| iOS ARMv7 | `0x187D990`, `0x187D9C4`, `0x187D9AC`, `0x187D9A8`, `0x187D994`, `0x187D9C0`, `0x187D99C` |

对这些槽做 data-xref 后，四端得到相同的跨控制器复用集合，而不是七组
Angle 私有 cache。当前源码因此恢复成七个共享 `_guess` 全局槽，并把对应 state
路径和 buildVariableList 的 `frame` 读接到相同地址身份。

### restore 的对象转换、所有权与提交边界

restore 不是“若入参非 object 则忽略”。四端的共同对象生命周期是：

```text
copy input tTJSVariant closure
  -> require/convert to tvtObject (failure throws before any field write)
  -> AddRef Object dispatch into ncbPropAccessor
  -> destroy copied Variant before the first property probe
  -> seven strict MEMBERMUSTEXIST probes
  -> accessor Release on normal or exceptional exit
```

相关 helper 映射：

| 目标 | Variant copy / accessor ctor | `AsObject`/conversion helper |
|---|---:|---:|
| Android ARM64 | Variant copy `0xA0DEE0`; accessor materialization inline in restore | non-object throw path `0xA0CD8C` |
| Android ARMv7 | accessor ctor `0x552A80` | `0x495308` |
| iOS ARM64 | accessor ctor `0x1001B6F34` | `0x100030294` |
| iOS ARMv7 | accessor ctor `0x1B6B54` | `0x125338` |

每个 probe 都是
`PropGet(TJS_MEMBERMUSTEXIST, key, sharedHint, &temporary, dispatch)`。HRESULT 失败
时返回 false 并保持目标字段不变；成功时立即做 Integer/Real 转换并提交字段。
所以后面 key 的 getter 或转换抛异常时，前面已写字段不会回滚，但 temporary 和
accessor 都会按 unwind 释放。

一个 `tvtObject` 但 Object 指针为 null 的 closure 也没有友好失败分支：accessor
会持有 null，第一次虚调用自然失败。`EmoteEngine_restoreBaseState_guess` 只检查外层
base Variant 是 Object；其 `rotate` 子项用 flags 0 读取并忽略普通 getter HRESULT。
若 `rotate` 缺失，产生的 Void 进入 Angle restore 后在 Object 转换处抛错，而不是
静默跳过。

字段目标仍保留发布版 bug：`prev` 写 `startRad`，紧接着 `target` 也写
`startRad`；`targetRad` 完全不被 restore。若两个 key 都存在，`target` 的值覆盖
`prev`；若 `target` 缺失，`prev` 的提交保留。

### 本地修正、IDB 回写与验证

- `restoreAngleControllerState_guess` 删除非 Object 静默返回，恢复 closure copy、
  `ToObject`、`ncbPropAccessor` retain、临时 Variant 提前 Clear 和 accessor unwind。
- `setTJSProperty`、strict getter 与 scalar restore helper 支持真实 hint 指针；七个
  state key 恢复跨控制器共享的进程级槽身份。
- `EmoteAngleController.cpp` 删除只适用于单一 ABI 的 `currentRad(+84)` 注释，保留
  字段语义；绝对函数/数据地址仍只保存在本分析页。
- 四份 IDB 把七个数据项重命名为完整 `*_utf16_guess`；三端 accessor ctor、三端
  `AsObject` helper 和 Android ARM64 Variant copy helper 改为语义名，serialize、
  restore、literal、hint 槽均补注释，restore 增加 bookmark，四份 IDB 已原位保存。
- 真实 Emscripten 参数的 `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有 `_tss`
  warning；`cmake --build --preset "Web Debug Build"` 以 4 个增量步骤重新编译
  Angle/Engine 并成功链接最终 `index.html`。

本节先闭合 Angle restore 的严格 Object/accessor 生命周期。Blink、Eyebrow、Mouth、
Var、Selector 以及 Eye/Eyebrow request queue 随后已完成 fresh 四端审计；它们的类型
guard 分型、Variant 输出提交边界与容器异常前缀详见
`analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md`。其中 Var
保留发布版唯一的 outer Object guard，其余 controller 不再使用过时的友好 guard。
