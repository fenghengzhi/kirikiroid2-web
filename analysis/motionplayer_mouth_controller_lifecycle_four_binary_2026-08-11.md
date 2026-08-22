# MotionPlayer `EmoteMouthController` 生命周期、双键输出与 12B deque 四端复原

> **2026-08-16 unordered 边界勘误：** Mouth 四端均以 `B.LE/BLE` 跳入
> immediate block；NaN 的 unordered 标志满足 `LE`，所以 NaN duration 不入队。
> 本文旧版相反表述已在正文更正；与 Var 的 `B.LS/BLS` 分叉见
> `analysis/motionplayer_controller_duration_unordered_split_four_binary_2026-08-16.md`。

> **2026-08-16 metadata source-identity 续证：** 四端都先 copy-construct 输入 Variant，
> 再构造真实 `ncbPropAccessor`；源 Variant 随即析构，accessor 独立持有 dispatch 到构造尾。
> `beginFrame` 使用 Eye/Eyebrow/Mouth 共用 hint family 的 slot 0，并非 mouth 私有的 raw
> dispatch helper。详见
> `analysis/motionplayer_emote_controller_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

日期：2026-08-11

## 范围与本轮纠错

本轮以 `reference/binaries/` 的 Android ARM64、Android ARMv7、iOS ARM64、
iOS ARMv7 四份当前参考镜像共同为权威，重新复原 mouth/talk controller 的完整
构造、setter、reset、step、状态持久化、Engine builder、owner 析构和底层 deque
行为。旧 `libkrkr2.so` 地址和端口中的历史注释不作为证据。

这次复核修正了三类源码结构错误：

1. mouth controller 有真实的字典构造函数，Engine 使用普通 C++ `new
   EmoteMouthController(element)`；它不是“默认 new 后调用一个 free ctor helper”。
2. mouth 首字段是裸 `std::deque<12-byte POD>`，没有嵌套完整 angle
   controller。析构也只需要普通默认析构加 owner 的 `delete`。
3. setter 的真实源码参数顺序是 `value, duration, power, append`。AArch64
   中三个 float 使用 `S0..S2`，最后的 bool/int 独立使用 `W1`；若只按寄存器
   编号阅读，很容易误写成 `append, value, duration, power`。ARM32 的
   `R1/R2/R3/[SP]` 调用点提供了无歧义的源码顺序证据。

## 四端函数与 owner 映射

| 目标 | ctor | set target | reset | step | restore state |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `0x663078` | `0x663214` | `0x6633A0` | `0x663448` | `0x663588` |
| Android ARMv7 | `0x55369C` | `0x553788` | `0x553804` | `0x553838` | `0x553910` |
| iOS ARM64 | `0x1001A3DE4` | `0x1001A3EE0` | `0x1001A3F58` | `0x1001A3FC8` | `0x1001A40EC` |
| iOS ARMv7 | `0x1A3200` | `0x1A3358` | `0x1A33B4` | `0x1A3402` | `0x1A3504` |

| 目标 | Engine mouth builder | allocation / ctor call | Engine normal dtor | mouth owner teardown |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x66A39C` | `0x66A514` / `0x66A520` | `0x67C898` | deque dtor `0x681A5C` -> range `0x6809DC` -> entry `0x680B44` |
| Android ARMv7 | `0x557894` | `0x55794E` / `0x557954` | `0x5610E8` | deque dtor `0x563E68` -> range `0x563178` -> entry `0x56323E` |
| iOS ARM64 | `0x1001A988C` | `0x1001A9978` / `0x1001A9984` | Engine dtor chain | entry clear `0x1001B764C` |
| iOS ARMv7 | `0x1A8ED0` | `0x1A8FD2` / `0x1A8FDC` | Engine dtor chain | entry clear `0x1B7094` |

setter 的唯一正常 caller 都是 Engine 的 timeline/manual variable 路由：

| 目标 | caller | call instruction |
|---|---:|---:|
| Android ARM64 | `0x66E608` | `0x66EA80` |
| Android ARMv7 | `0x559D84` | `0x559F64` |
| iOS ARM64 | `0x1001ACDBC` | `0x1001AD0B4` tail-call |
| iOS ARMv7 | `0x1AC5F4` | `0x1AC83E` |

## 对象布局与构造写覆盖

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| object size | `0x70` | `0x48` | `0x50` | `0x38` |
| naked 12B deque | `+0..+79` | `+0..+39` | `+0..+47` | `+0..+23` |
| `state` | `+80` | `+40` | `+48` | `+24` |
| `currentValue` | `+84` | `+44` | `+52` | `+28` |
| `endVal` | `+88` | `+48` | `+56` | `+32` |
| `accum` | `+92` | `+52` | `+60` | `+36` |
| `invDur` | `+96` | `+56` | `+64` | `+40` |
| `powField` | `+100` | `+60` | `+68` | `+44` |
| `startVal` | `+104` | `+64` | `+72` | `+48` |
| `beginFrame` | `+108` | `+68` | `+76` | `+52` |

四个构造函数共同执行：

```cpp
EmoteMouthController::EmoteMouthController(const tTJSVariant &dict)
    : valueTrack12B(), state(0), currentValue(0.0f), endVal(0.0f) {
    ncbPropAccessor object{tTJSVariant(dict)};
    beginFrame = object.GetValue<tjs_int>(L"beginFrame", sharedHintSlot0);
}
```

`accum/invDur/powField/startVal` 没有构造初始化。Android 两端先清 deque
header，再调用共享 12B deque 初始化器，然后只清 `state/currentValue/endVal`；
iOS 两端的成组 zero store 同样只覆盖 deque header 和这三个标量。最后读取
`beginFrame`，属性缺失时落为 0。对象大小没有额外未解释尾部字段。

因此首次正常 state-0 setup 或状态恢复之前，这四个尾字段仍是 allocator 留下的
未定位值。把它们改成 in-class `=0` 会静默改变“构造后立即序列化”等边界，不是
1:1 复原。

## 真实 C++ 构造、异常 unwind 与析构

四个 builder 都先检查元素的 `enabled`，再为完整 mouth 对象分配存储并直接调用
字典构造函数。Android ARM64 在 `0x66A808` 保留标准 new-expression cleanup：
若构造抛异常，先 `operator delete` 尚未完成的对象存储，再继续 unwind。iOS
构造函数自己的 unwind 路径则先析构已构造的 deque 子对象：ARM64
`0x1001A3E90 -> 0x1001B6D80`，ARMv7 `0x1A32EA -> 0x1B6A38`；Android
两端分别在 `0x663198` 和 `0x553762` 内联/落入同等清理。

这些路径只有写成真实构造函数和普通 `new` 才能自然恢复。旧端口的
`new default + free ctor` 不具备相同的 C++ 子对象构造状态与异常语义。

mouth 类没有 vtable、继承、额外数组或独立析构逻辑；唯一 owned 子对象是
12B deque，所以 controller 自身使用默认析构。2026-08-13 对 element owner/emplace
的独立复核进一步确认：Engine 不是靠外层手工循环管理它，而是由 entry 首字段的
单指针 `unique_ptr` 自动 delete。

mouth entry 的共同语义是：

```cpp
struct EmoteMouthControlEntry {
    std::unique_ptr<EmoteMouthController> ctl;
    ttstr label;       // beginFrame 输出键
    ttstr talkLabel;   // currentValue 输出键
};
```

`unique_ptr` 仍只占一个 pointer，所以 entry 在 64 位目标为 24 字节，在 32 位目标为
12 字节。声明顺序使正常 teardown 先释放 `talkLabel` 和 `label` 的字符串引用，再
析构 controller 的裸 deque、delete controller，并把 owner 槽清零。iOS 的 clear
helper 语义相同，只是 libc++ 容器展开不同。raw-pointer emplace、deque block ABI 与
异常/部分初始化边界见
`analysis/motionplayer_mouth_entry_owner_emplace_four_binary_2026-08-13.md`。

iOS builder 的 Hex-Rays 曾把第二个 key 显示为单字符 `"t"`。原始数据
`0x10195FDB2`（ARM64）和 `0x1752116`（ARMv7）实际都是 UTF-16
`talkLabel`；这是 IDA 在重叠字符串边界处建立了错误短字符串，并非平台差异。

## 裸 12B deque 的实现差异

元素是 trivial 的三个 32 位 word：

```cpp
struct MouthKeyframe {
    float value;
    float duration;
    float power;
};
```

它与 angle、eye、eyebrow 等轨道复用相同的 12B deque 基础设施，但不等于嵌套
这些 controller：

- Android/libstdc++ 空 deque 构造时即建立至少 8-entry map 并分配一个
  504-byte block；一个 block 容纳 42 个 12B 元素。对象 header 为 ARM64
  80 字节、ARMv7 40 字节。
- iOS/libc++ 空 deque 只有全零 header，首次 push 才分配 map/block；header
  为 ARM64 48 字节、ARMv7 24 字节。
- 元素没有析构动作。clear 释放不再使用的 blocks、重设 begin/end cursor；完整
  destructor 还释放剩余 block 和 map。实现布局不同，逻辑 queue 行为一致。

## setter 的源码签名与 ABI 证据

共同源码级签名是：

```cpp
void setTarget(EmoteMouthController *self,
               float value, float duration, float power, bool append);
```

ARM32 caller 在调用前把转换后的三个 float 依次送入 `R1/R2/R3`，再把
`append` 存到首个 stack argument；callee 比较 `R2` 的 duration，把 `R1`
写为 current/value，把 `R1/R2/R3` 作为连续 keyframe 入队，并从栈读取 append。

AArch64 按 AAPCS 的独立寄存器类别分配同一源码签名：self 在 `X0`，三个 float
在 `S0/S1/S2`，虽然 append 是最后一个源码参数，却在整数寄存器序列的 `W1`。
所以反编译器按“self, W1, S0, S1, S2”强行排列出的 prototype 不是源码顺序。
把 prototype 按上述真实顺序回写后，四端 fresh decompile 一致。

共同 setter 语义：

```cpp
if (!(duration > 0.0f)) {
    queue.clear();
    currentValue = value;
    state = 0;
} else {
    if (!append) {
        queue.clear();
        state = 0;
    }
    queue.push_back({value, duration, power});
}
```

power 按原始 32-bit float word 保存，没有 clamp、finite 检查或转换。正常 C++
caller 的 bool 一定规范为 0/1；若绕过源码 ABI 直接注入非规范值，各工具链生成的
机器判断并不完全相同（Android ARM64 测 bit 0、iOS ARM64 测 nonzero、ARM32
比较 1），这是机器级调用边界，不是正常 C++ 可观察差异。

## reset 与 step 状态机

四端 reset 完全一致：

```cpp
if (!queue.empty()) {
    currentValue = queue.back().value;
    state = 0;
    queue.clear();
} else if (state != 0) {
    currentValue = endVal;
    state = 0;
}
```

它不修改 `accum/invDur/powField/startVal/beginFrame`。队列非空时最后一个 queued
target 优先于正在运行的 `endVal`；队列为空且 state 已为 0 时什么也不做。

共同 step 可还原为：

```cpp
if (state == 1) {
    accum += invDur * dt;
    if (accum >= 1.0f) {
        accum = 1.0f;
        currentValue = endVal;
        state = 0;
    } else {
        currentValue = pow(accum, powField) *
                       (endVal - startVal) + startVal;
    }
} else if (state == 0 && !queue.empty()) {
    state = 1;
    startVal = currentValue;
    endVal = queue.front().value;
    invDur = 1.0f / queue.front().duration;
    powField = queue.front().power; // raw word
    accum = 0.0f;
    queue.pop_front();
}

*outBeginFrame = float(beginFrame);
*outCurrentValue = currentValue;
return float(beginFrame);
```

setup 与 interpolate 是互斥分支：state-0 调用最多消费一个 keyframe，只建立下一
段并发布旧 current；后续 step 才推进 accum。Android 使用 double `pow` 后回写
float，iOS 使用 `powf`；这是工具链数学库边界，极端输入的末位可能不同。

Engine 每帧把 `float(beginFrame)` 写到 entry 的 `label`，把 currentValue 写到
`talkLabel`。这是 mouth entry 成为唯一“双 HM7 key controller”的数据流原因；
beginFrame 自身不随 step 推进，它只是字典/变量写入/状态恢复控制的固定整数。

## 状态持久化边界

状态对象使用 `phase/mouth/frame/prev/target/tick/exponent/speed` 八个 controller
字段，外加 entry 的主 `label`。restore 对存在的属性逐个写回
`state/beginFrame/currentValue/startVal/endVal/accum/powField/invDur`，既不保存也
不恢复 command deque，亦不序列化 `talkLabel`。因此：

- ctor 留下未初始化的四个尾字段可由完整状态对象补齐；缺失属性仍保留旧值。
- restore 本身不清 queue。调用方若需要清空，必须由更外层 reset/流程保证。
- mouth 状态项按主 label 找 owner，第二输出 key 由 metadata entry 保持，不从
  snapshot 重建。

## 精确边界行为

- `state` 为 0/1 以外的值时，step 不推进也不消费 queue，但仍无条件发布两个
  输出并返回 beginFrame。
- state 0 且 queue 为空时也只是发布当前值。
- setter 仅在 duration 有序且严格大于 0 时入队；NaN duration 与非正 duration
  都走立即写。正负零也都走立即写。
- 正常 setter 不会把非正 duration 入队；若外部直接向裸 queue 塞入 duration
  0，setup 会无 guard 地得到无穷 `invDur`，下一次 `dt==0` 产生 NaN accum。
- `accum >= 1` 是 ordered compare。NaN 不完成动画，而是继续通过 pow/interpolate
  传播；所有输入都没有 finite 检查。
- 负 dt 会倒退 accum；负 accum 配非整数 power 的结果依目标数学库传播 NaN。
- 完成时只上限 clamp 为 1；没有下限 clamp，也没有自动在同一 step 开始下一段。
- beginFrame 每次从 32 位整数转换为 float；绝对值超过 float 的连续整数精度范围
  后会发生舍入，但 controller 内保存的整数不变。
- 两个输出指针没有 null guard；原生 caller 总是提供有效地址，越过此契约会直接
  触发非法访问。
- constructor 尾四字段未定；第一次正常 setup 会全部覆盖，之后 state-1 路径才
  读取它们。

## 本地源码与 IDB 回写

本地恢复包括：

- `EmoteMouthController` 改为真实字典构造函数、默认析构、禁复制普通类；只初始化
  四端实际写到的 `state/currentValue/endVal/beginFrame`。
- 删除 fake free ctor/dtor；Engine builder 使用普通构造表达式。后续 owner 复核已把
  entry 首字段恢复为 `unique_ptr`，两个 Engine 手工清理循环随之删除。
- 恢复独立 `EmoteMouthController_setTarget_guess`，签名按真实源码顺序排列；
  Engine variable route 不再保留旧单库地址命名的内联仿制 helper。
- mouth 首字段使用共享的 naked 12B keyframe queue，step/reset/serialize 全部直接
  操作真实字段。
- 测试锁定 ctor 写覆盖、setter 的 replace/append/immediate、setup-only、pow
  interpolation 与完成 clamp。

四端 IDB 已统一命名 ctor/setter/reset/step/restore 和 owner teardown；setter
prototype 已改成 `self,value,duration,power,append`，并在函数与唯一 caller 写入
AArch64 双寄存器类别说明。构造写覆盖、unwind、两个字符串 owner、iOS
`talkLabel` 重叠字符串误识别及 deque 析构链也已写入相应指令/函数注释。

## 验证状态

Web Debug 与 Wasmtime Debug 全目标增量构建均成功，并分别再次 dry-run 收敛到
`ninja: no work to do.`。首次并行构建的外层 60 秒工具超时没有连带终止 Windows
linker 子进程；一次过早的 Wasmtime 重试因与原 linker 同时写 `index.wasm` 而
报告 permission denied。等待原构建自然完成后，两目录均成功收敛，这不是源码
编译错误。

真实 Emscripten 参数下的完整 `motionplayer-dll.cpp -fsyntax-only` 已通过，仅有
仓库既有 `_tss` literal-operator deprecation warning。`git diff --check` 通过，
仅报告工作树既有 LF/CRLF 转换提示。四份 IDB 均已原位保存成功。
