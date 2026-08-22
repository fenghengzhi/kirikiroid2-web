# MotionPlayer Eye/Eyebrow enqueue 与 Eyebrow 生命周期四参考复原（2026-08-11）

> **2026-08-16 unordered 边界勘误：** Eye/Blink 与 Eyebrow 四端 gate 都是
> `B.LE/BLE`。`FCMP/VCMPE` 的 unordered 标志满足 `LE`，因此 NaN duration
> 也走 immediate、清两条 queue；本文旧版“NaN 入队”的表述已在正文更正。
> 五类 controller 的 `LS`/`LE` 分叉见
> `analysis/motionplayer_controller_duration_unordered_split_four_binary_2026-08-16.md`。

> 2026-08-13 补充：本文的 enqueue、Eyebrow slim-controller payload 与 deque #5
> 析构结论仍有效；但不要把末尾“owning raw pointer/delete”的旧 portable 表述外推到
> deque #4 Eye entry。deque #4 已通过 builder raw-emplace、四端 range destructor 和
> exception landing 单独闭合为 `{unique_ptr<EmoteBlinkController>, ttstr}`。deque #5
> 也已通过其自身独立的 builder/raw-emplace/range-dtor/EH 证据闭合为
> `{unique_ptr<EmoteEyebrowController>, ttstr}`。详见
> `motionplayer_eye_entry_owner_emplace_four_binary_2026-08-13.md`。

> 2026-08-14 构造入口复核：重新反编译 `0x661BEC / 0x552CDC /
> 0x1001A31F4 / 0x1A2560`，四端仍共同显示“两条 ABI-specific deque + 内嵌
> resolver + state/value/beginFrame”的无 vptr slim 布局。Android libstdc++ 在构造时
> 建立 deque map/block，iOS libc++ 只清零 lazy header；这属于 STL ABI 差异，不是额外
> 源码成员。四端构造数据流仍只读取 `beginFrame`、`edge`、`node`，最后令
> `trackValue = float(beginFrame)`，没有任何 Blink 字段或 RNG。此次审计据此删除了
> portable `.cpp` 中旧 `sub_6827A8/sub_6828FC/sub_6635DC` 单 A64 伪码，而未扰动已经
> 与四端一致的实现。

## 结论

本轮把 Eye 与 Eyebrow 的 value-track enqueue helper，以及 Eyebrow 从 metadata
构建到 Engine 析构的所有权闭环，在四个当前 1.3.9 参考二进制中重新定位并新鲜
反编译。最重要的新结论是：当 `duration > 0` 且 `append == false` 时，Eye 和
Eyebrow 都会同时清空 12B 主命令轨道与 resolver 已生成的 8B 次轨道，再压入新的
主 keyframe。本地旧实现只清主轨道，会让上一条命令遗留的路径段污染替换命令。

四端还共同确认：Eyebrow 是没有 vptr 的独立 slim controller；它拥有两条 deque、
一个内嵌 mesh resolver、状态标量和一个 `beginFrame`，而不是内嵌完整
`EmoteAngleController`，也不是从 BlinkController 继承。Engine deque#5 独占每个
controller；entry 先释放 label，再按成员逆序销毁 controller 的内部容器并 delete。

## 四平台函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Eyebrow builder | `0x669F7C` | `0x557618` | `0x1001A9540` | `0x1A8B68` |
| Eyebrow ctor | `0x661BEC` | `0x552CDC` | `0x1001A31F4` | `0x1A2560` |
| Eye enqueue | `0x660C90` | `0x5522FC` | `0x1001A2568` | `0x1A1850` |
| Eyebrow enqueue | `0x6626B4` | `0x553170` | `0x1001A3764` | `0x1A2B6C` |
| Eyebrow reset | `0x6628A4` | `0x553200` | `0x1001A37EC` | `0x1A2BD8` |
| Eyebrow step | `0x6629E0` | `0x553280` | `0x1001A38C8` | `0x1A2C56` |
| Engine setVariable dispatch | `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |
| Engine reset | `0x66BF6C` | `0x558888` | `0x1001AB03C` | `0x1AA714` |
| deque#5 destructor | `0x681B20` | `0x563EE4` | `0x1001B8A2C` | `0x1B80AC` |
| deque#5 element destruction | `0x680750` | `0x562FE0` | `0x1001B7514` | `0x1B6FC0` |
| Engine destructor | `0x67C898` | `0x5610E8` | `0x1001B8B4C` | `0x1B814E` |

Android ARM64 的 controller payload 逆序析构另有 `0x6808BC`；Android ARMv7
使用接收 owner slot、析构后置 null 的 `0x563070`。iOS 两端把同一逻辑内联在
deque#5 element-destruction helper 中。

四份 IDB 中写入的主要保守语义名为：

这里的 `*_ctor_guess(void *self, ...)` 是 stripped IDB 中按 ABI 展开的保守函数原型；
四端调用点实际属于 `new EmoteEyebrowController(dict)` 的真实 C++ 成员构造函数，
不表示源码里另有同名自由函数 helper。

```cpp
void EmoteEyebrowController_ctor_guess(void *self, const void *dict);
void EmoteBlinkController_enqueueValue_guess(
    void *self, float value, float duration, float power, bool append);
void EmoteEyebrowController_enqueueValue_guess(
    void *self, float value, float duration, float power, bool append);
void EmoteEyebrowController_reset_guess(void *self);
void EmoteEyebrowControlDeque_dtor_guess(void *self);
```

源码参数顺序以 ARM32 caller 和四类 controller setter 的共同形状恢复为
`value/duration/power/append`。AArch64 把三个 float 放入 `S0..S2`、bool 放入
`W1`；反编译器未经定型时把整数寄存器参数提前显示，不代表源码把 `append` 放在
第二位。这与 Mouth/Angle/Var controller 已恢复的 setter 顺序一致。

## Builder 与对象布局

四个 builder 对 metadata 数组逐索引执行：

```cpp
for (metadataIndex = 0; metadataIndex < count; ++metadataIndex) {
    elem = eyebrowControl[metadataIndex];
    if (!elem.enabled)
        continue;

    ctl = new EmoteEyebrowController;
    construct(ctl, elem);
    deque5.push_back({ctl, elem.label});
    HM6[elem.label] = {type = 5, index = metadataIndex};
}
```

HM6 保存的是 metadata loop index，不是成功插入后的 deque ordinal。disabled
条目会让 index 域留下空洞；builder 与后续 setVariable 均不增加修正或边界检查。
有效 metadata 必须满足该隐含契约，本地不能擅自把 index 改写为 `deque.size()-1`。

| ABI | `sizeof(Eyebrow)` | 主 12B deque | 次 8B deque | resolver | state | value | beginFrame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Android ARM64/libstdc++ | `0x150` | `+0` | `+80` | `+160` | `+296` | `+300` | `+328` |
| Android ARMv7/libstdc++ | `0xB8` | `+0` | `+40` | `+80` | `+148` | `+152` | `+180` |
| iOS ARM64/libc++ | `0xF0` | `+0` | `+48` | `+96` | `+200` | `+204` | `+232` |
| iOS ARMv7/libc++ | `0x88` | `+0` | `+24` | `+48` | `+100` | `+104` | `+132` |

构造函数只读取 `beginFrame`、`edge` 与 `node`。`edge` 每项转换为
`pair<float,float>`；`node` 每行转换为独立 `vector<float>` 并进入 node-row
deque；最后 `trackValue = float(beginFrame)`。它不读取 Blink 的 endFrame、间隔、
frameCount 或 enabled 字段，也不消费共享 RNG。

主轨道在 Android libstdc++ 中以 504B 数据块容纳 42 个 12B 元素，次轨道以
512B 数据块容纳 64 个 8B pair。iOS libc++ 使用 4096B block：主轨道每块 341
项（4092B 有效负载），次轨道每块 512 项。portable `std::deque` 不复制 native
header 字节布局，但元素类型、顺序、所有权和 cursor 行为保持一致。

## Enqueue 的共同控制流

对 Eye 和 Eyebrow，四端函数体除了地址外是同一逻辑：

```cpp
if (!(duration > 0.0f)) {
    primary12B.clear();
    secondary8B.clear();
    trackValue = value;
    trackState = 0;
    return;
}

if (!append) {
    primary12B.clear();
    secondary8B.clear();
    trackState = 0;
}

primary12B.push_back({value, duration, power});
```

两个 `clear` 都是实际存在的独立容器操作。Android ARM64 展开为恢复 begin/end
cursor、释放多余数据块并保留基础 map/block；Android ARMv7 与 iOS 则调用各自的
deque clear helper。第二个 clear 不是编译器为第一个 deque 生成的尾部代码，因为
它以 ABI 对应的次轨道偏移重新取 header，四端完全一致。

边界行为：

- `duration == +0.0f` 与 `-0.0f` 都走立即 snap；
- 负 duration 同样立即 snap；
- `duration = NaN` 的 unordered 标志满足原生 `LE`，因此走立即 snap；
- `append=true` 保留主轨道与当前 resolver 次轨道，并只追加一项；
- `append=false` 即使当前 state 为 idle，也会丢弃次轨道；
- power 是一个 float word 原样进入 keyframe 第三个 DWORD，没有 int 转换。

最后一点解释了旧端口偏差的可观察方式：旧命令已经 resolve 出
`secondary8B={{a,b},...}`，随后脚本以非 append 方式设置一个正 duration 新目标。
参考实现先丢弃旧 secondary；旧本地实现保留它，新主 keyframe setup 后可能继续
消费不属于新目标的 pair，造成一次或多次错误跳变。

## Step、reset 与状态生命周期

Eyebrow step 的状态机保持现有四参考恢复：

1. state 0 每次最多弹一个主 keyframe，调用共享 mesh resolver 重建次轨道，保存
   resolved span、duration 倒数与 power，并进入 state 1；
2. state 1 每次最多弹一个次轨道 pair；相等端点直接写 value，否则设置 target、
   direction 并进入 state 2；
3. state 2 按 power curve 推进，越过 target 时钳到 target 并回到 state 1；
4. 每次 step 直接输出 `trackValue`，没有 Blink phase、RNG 或最终眼睑 remap。

reset 的优先级为：

```cpp
if (!primary12B.empty()) {
    state = 0;
    value = primary12B.back().value;
    primary12B.clear();
    secondary8B.clear();
} else if (state != 0) {
    state = 0;
    value = secondary8B.empty()
        ? target
        : secondary8B.back().first;
    secondary8B.clear();
}
```

因此 reset 不是“取最后一个 pair 的 second”：有次轨道时明确取最后 pair 的 first。
idle 且两轨均空为 no-op。Engine reset/skip 固定在 Blink 之后、Mouth 之前遍历整个
deque#5；四端均不检查 controller null。

## Entry 所有权与析构顺序

Engine deque#5 的逻辑 element 是：

```cpp
struct EyebrowEntry {
    EmoteEyebrowController *ownedController;
    ttstr label;
};
```

它在两个 64 位 ABI 上是 16B，在两个 32 位 ABI 上是 8B。Engine 析构的 reverse
member order 到达 deque#5 后，每个 entry 执行：

1. Release entry label；
2. 若 controller 非空，按以下顺序销毁 payload；
3. `operator delete(controller)`；
4. 最后释放 deque data blocks 与 map/header allocation。

controller payload 的逆序为：

```text
mesh.outputRows（并逐个销毁 row 内 path deque）
-> mesh.nodeRows（并逐行销毁 vector<float>）
-> mesh.edgeTable
-> secondary8B
-> primary12B
```

没有 vptr、虚析构、共享引用、resolver 外部分配或 beginFrame 尾部清理。portable
结构把成员按 `primary, secondary, mesh, scalars` 声明，普通 C++ 默认析构正好产生
同一源码级逆序。2026-08-13 对 wrapper 本身的独立复核进一步确认首字段不是普通
owning raw member，而是单指针 `unique_ptr<EmoteEyebrowController>` owner；其 storage
仍为一个 pointer word，label/controller 的相对析构结论不变。

## 本地修正

- 从 `EmoteEngine.cpp` 的匿名旧地址 helper 中拆出
  `EmoteBlinkController_enqueueValue_guess` 与
  `EmoteEyebrowController_enqueueValue_guess`，分别放回 controller 源文件；
- 参数顺序恢复为 `value/duration/power/append`；
- 修正两者 `duration>0 && append=false` 分支，补上次 8B 轨道 clear；
- `EmoteEngine::setVariable` type 4/5 分派改为调用这两个具名 helper；
- 清理触及区域中只对应旧 `libkrkr2.so` 的地址与 M2 临时注释；
- 新增回归用例，覆盖 Eye/Eyebrow replace、append 保留和 immediate snap 的两轨
  边界。

## IDB 与验证状态

四份 IDB 已写入 ctor/enqueue/reset/deque-dtor 名称和源码级原型；builder、ctor、
enqueue、reset、entry destruction 与 payload reverse destruction 均追加了证据注释。
所有 enqueue helper 在定型后重新反编译，四端都明确显示 append=false 分支连续清
两条 deque。每个 enqueue helper 都只有一个直接 caller，即 Engine setVariable
type 4 或 type 5 分支。

验证结果：

- `cmake --build --preset "Web Debug Build" --target motionplayer`：通过；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target motionplayer`：通过；
- 复用 Web Debug `compile_commands.json` 的真实 Emscripten 参数，并加入
  `out/syntax-check` 的 Catch2/test-config 头目录，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过，仅有
  仓库既有的 `_tss` 字面量运算符弃用 warning；
- 当前 `out/windows/debug` 没有 `build.ninja`，因此本轮没有可直接运行的原生
  `motionplayer-dll` executable；新增回归用例完成了完整翻译单元编译验证，但未把
  语法检查误报成运行时 Catch2 通过；
- 四份 IDB 均已原位保存，保留本轮名称、函数原型和证据注释。
