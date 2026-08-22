# MotionPlayer VarController 共享 setter / 20B 原位构造四参考闭环（2026-08-12）

> **2026-08-16 再确认：** 四端 Var gate 均为 `B.LS/BLS`；NaN 时 unordered
> 标志不满足 `LS`，所以本文“NaN 不进入 immediate、仍可入队”的结论有效。不要
> 将 Angle/Blink/Eyebrow/Mouth 的 `B.LE/BLE` NaN-immediate 行为外推到 Var。
> 完整条件码对照见
> `analysis/motionplayer_controller_duration_unordered_split_four_binary_2026-08-16.md`。

## 1. 结论

四份当前参考二进制证明，直属 controller、selector 借用的 transition
controller、timeline track controller 和 timeline blend controller 共用同一个
`EmoteVarController` setter。生成 20 字节 keyframe 时，该 setter 不先建立一个
清零的临时对象再 `push_back`，而是把四个构造参数交给 deque 的 emplace helper，
直接在最终 deque slot 中按以下顺序写字段：

```text
word[3] = duration
word[4] = power
for i in 0..<count:
    word[i] = values[i]
commit deque end
```

因此 count 小于 3 时，未被 channel loop 覆盖的中间 word 保持未初始化。旧本地
`EmoteVarKeyValue20B keyframe{}` 会把它们清零，虽然正常 step 不读取这些 word，
但改变了原始源码结构、构造数据流和损坏状态下的边界行为。本轮已恢复原位构造，
并删除 EmoteEngine 中最后一份旧单库地址命名的私有重复 setter。

## 2. 当前四端映射

| 语义 | Android arm64-v8a | Android armeabi-v7a | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| shared var setter | `0x6646E0` | `0x5542B0` | `0x1001A4C44` | `0x1A418C` |
| 20B deque emplace helper | `0x664870` | `0x55433C` | `0x1001A4CDC` | `0x1A41F4` |
| Engine general setVariable | `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |
| initialize timeline state | `0x66D03C` | `0x5590E8` | `0x1001ABD5C` | `0x1AB4B0` |
| initialize track controllers | `0x66DC20` | `0x559848` | `0x1001AC5DC` | `0x1ABDA4` |
| seek timeline | `0x66EE30` | `0x55A0F8` | `0x1001AD2C0` | `0x1ACA22` |
| apply timeline window | `0x6671FC` | `0x555BC0` | `0x1001A6BDC` | `0x1A636C` |
| set timeline blend controller | `0x67098C` | `0x55ACDC` | `0x1001AE178` | `0x1AD918` |

以上 32 个 caller/helper body 均在本轮强制失效 Hex-Rays cache 后重新反编译。
四份 IDB 的 helper 已统一命名为：

- `EmoteVarController_setTarget_guess`
- `EmoteVarKeyValue20B_emplaceBack_guess`

精确原作者符号名不可得，因此继续保留 `_guess`。

## 3. shared setter 的共同控制流

四端归一伪代码：

```cpp
void setTarget(Controller *controller,
               const float *values,
               float duration,
               float power,
               bool append) {
    if (duration <= 0.0f) {
        controller->queue.clear();
        controller->state = 0;
        for (int i = 0; i < controller->count; ++i)
            controller->currentValue[i] = values[i];
        return;
    }

    if (!append) {
        controller->queue.clear();
        controller->state = 0;
    }

    controller->queue.emplace_back(
        values, controller->count, duration, power);
}
```

共同边界：

- `duration <= 0` 包含负值、正零和负零；NaN 不进入 immediate 分支。
- immediate 分支清完整 deque、写 `state=0`，只覆盖 `currentValue[0..count)`。
- immediate 分支不写 phase、inverse duration、power、start 或 target arrays。
- 动画且 `append=false` 时同样只清 deque 和 state，再 emplace；不重置 phase。
- `append=true` 保留旧 deque 和 state，直接追加。
- `count` 没有 clamp。`count<=0` 不复制 channel；`count>4` 会越过 20B 元素
  channel 区继续写，属于原始未防御边界。
- values/currentValue 没有 null guard；正常构造函数负责建立 count 对应的数组。

Android arm64 把 deque clear 展开成 block 释放和 iterator reset；其余三端较多保留
独立 clear helper。这只是 STL/优化差异，共同源码控制流一致。

## 4. 20B emplace helper 的写序和容器生命周期

四端 helper 都接收 deque、`values`、`count`、`duration`、`power` 五个转发实参的
地址。这个形状与 `deque.emplace_back(values, count, duration, power)` 的完美转发
实例一致，而不是“先构造一个完整临时对象再 copy”的形状。

共同顺序：

1. 检查末端是否还有一个 20B slot；不足时先扩展 deque map/block。
2. 取得最终目标 slot。
3. 写 slot word 3 为 duration。
4. 写 slot word 4 为 power。
5. 从 word 0 开始复制恰好 `count` 个 32 位 channel word。
6. 最后才推进 deque end/size。

没有以下步骤：

- 没有 memset/value-initialize 20B；
- 没有先写一个栈上 20B temporary；
- 没有 copy/move 完整五个 word；
- 没有异常回滚或 count 上限检查。

按 count 展开后的字段状态：

| count | word 0 | word 1 | word 2 | word 3 | word 4 |
| ---: | --- | --- | --- | --- | --- |
| `<=0` | 未初始化 | 未初始化 | 未初始化 | duration | power |
| `1` | channel 0 | 未初始化 | 未初始化 | duration | power |
| `2` | channel 0 | channel 1 | 未初始化 | duration | power |
| `3` | channel 0 | channel 1 | channel 2 | duration | power |
| `4` | channel 0 | channel 1 | channel 2 | channel 3/alpha | power |

count 4 对 word 3 的覆盖发生在 duration/power 已写之后，因此 color controller 的
alpha 同时成为 step 后续读取的 duration。这与既有 controller-setter 文档一致；
本轮新增的是“其余未覆盖 word 不清零”和“直接在 deque slot 构造”的证据。

### STL/ABI 差异

- Android 两端使用 libstdc++ deque，每个数据 block 容纳 25 个 20B 元素；
  Android arm64 可见 500 字节 block 分配。
- iOS 两端使用 libc++ deque，每个数据 block 容纳 204 个 20B 元素。
- map/header/iterator 表达式不同，但 slot 写序、元素 stride、扩容先于构造以及
  end 最后提交完全一致。

这些块容量是宿主 STL 实现细节，只记录在分析文件；共享源码继续使用普通
`std::deque<EmoteVarKeyValue20B>`。

## 5. Engine caller 拓扑

### general setVariable / TYPE 7

四端 HM6 type 7 路由都先检查 transition entry gate；gate 为真时，把单个局部
float 的地址、转换后的 duration/power 和 Engine append byte 传入 shared setter。
它不调用专用 Animator helper。

### timeline state 初始化

新建 count-1 blend controller、替换并析构旧 owner 后，以
`values=&state.blendWeight, duration=0, power=0, append=false` 调 shared setter。
Android arm64 把 setter 完整内联；另外三端保留直接 call。

### timeline track controller 初始化

只处理非空且非 instant track。controller 不存在时新建 count-1 controller；已存在
时用局部 float zero 和三个零/false 实参调用 shared setter。Android arm64 再次
内联 setter，另外三端直接 call。

### seek/window

内部 track route 直接把 frame 中 `value` 字段的地址传给 shared setter；普通 route
调用 Engine general setVariable。二者共用同一 transition 与 easing 值。四端这里都
保留直接 shared-setter call。

### timeline blend setter

非插入查找 state；命中后按需初始化 timeline state，再把参数 `value` 的地址、
transition、easing 和 Engine append byte传给 shared setter，最后写 autoStop double。
四端均为直接 call。

因此共享源结构是“一个 setter + 多类 count-1 caller”，不是本地此前的“一个共享
setter，再在 EmoteEngine 内复制一份只支持 scalar 的私有 setter”。

## 6. 本地逐行对照与修正

### 修改前

- `EmoteVarController_setTarget_guess` 的分支、clear/state 和 channel loop 基本正确，
  但使用 `EmoteVarKeyValue20B keyframe{}`，错误清零完整元素。
- 它随后 `queue.push_back(keyframe)`，引入参考调用链中不存在的完整 temporary 和
  copy/move 阶段。
- EmoteEngine 另有 `emoteAnimatorSetKeyframes_0x667300`，只显式写 channel 0，
  immediate 路径把 scalar 广播到全部 count；参考 shared setter 则始终读取
  `values[i]`。当前调用者 count 都是 1，所以表面结果相同，但源码结构和破坏状态
  边界不一致。
- 私有 helper 名中的地址来自旧 `libkrkr2.so`，不是当前四端身份。

### 修改后

- `EmoteVarKeyValue20B` 增加四参数构造函数，在函数体内严格按
  duration → power → count channels 的顺序写入，不初始化其余 word。
- shared setter 改为 `queue.emplace_back(values, count, duration, power)`。
- 删除 EmoteEngine 私有 helper。
- TYPE 7、timeline blend/state/controller/seek/window 的所有 scalar caller 都传真实
  float lvalue/字段地址进入 shared setter。
- 保留 count 无 clamp、未初始化 word、duration/alpha alias 和 append/state 边界。
- 编译源码注释只保留语义；当前地址和 ABI 偏移集中在本文。

## 7. IDB 改进

四份 IDB 已完成：

- `EmoteVarKeyValue20B_pushBack_guess` 更正为
  `EmoteVarKeyValue20B_emplaceBack_guess`；
- 写入五个转发实参的源码级 prototype；
- 添加写序、未初始化 word、Android/iOS block 容量注释；
- 强制重新反编译 helper 与 shared setter；
- setter 伪代码现在明确显示调用 `emplaceBack_guess`，不再误导为完整对象 push。

本轮结束时四份 IDB 分别保存。

## 8. 验证范围

现有 controller 单元测试已经覆盖 shared setter 的 immediate、append、replace、
count 2 channel、count 4 alpha/duration alias 和 power 字段；timeline 测试覆盖 blend、
seek/window 和 TYPE 7/transition controller 的可见字段。未新增“读取未初始化 word”
测试，因为那会在 C++ 层制造未定义读取，不能作为合法 oracle。

本纵切完成后的验证结果：

- Web motionplayer 静态库重编译通过；
- Wasmtime motionplayer 静态库重编译通过；
- 完整 motionplayer Catch 翻译单元的 Emscripten `-fsyntax-only` 通过；
- Web `index.html` 完整链接通过；
- Wasmtime guest wasm 链接与 exnref 转换通过；
- 相关文件 `git diff --check` 通过。

诊断只有仓库既有的 `_tss` literal-operator、imagepacker `nodiscard`、Emscripten
pthread/memory-growth、JSPI 和 JS library 警告。当前 CMake 配置没有可直接运行的
Catch motionplayer 目标，因此这里只声明测试翻译单元通过编译，不虚报 runtime
结果。
