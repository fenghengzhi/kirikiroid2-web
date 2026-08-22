# MotionPlayer deque #6 Mouth entry owner、双 label 与 raw-emplace 四参考复原（2026-08-13）

## 结论

本轮不沿用端口中“普通 owning pointer + Engine 手工 `delete`”的旧解释，而是分别检查
`reference/binaries/` 四份当前参考的 Mouth builder、deque 边界插入、element range
destructor、deque destructor 与 builder 异常清理。四端共同指向以下源码级 element：

```cpp
struct EmoteMouthControlEntry {
    explicit EmoteMouthControlEntry(EmoteMouthController *raw)
        : ctl(raw) {}

    std::unique_ptr<EmoteMouthController> ctl;
    ttstr label;
    ttstr talkLabel;
};
```

`unique_ptr` 在这里表示标准库单指针 owner 的类型与自动析构语义，不增加 element
storage。字段顺序必须是 `ctl, label, talkLabel`：C++ 逆声明顺序析构自然得到
`talkLabel -> label -> controller`，与四端 element destruction 完全一致。64 位两端
element 是 24B，32 位两端是 12B。

Builder 也不是把一个局部 `unique_ptr` move 进 deque。controller 构造完成后，四端
都只把 raw pointer 放在寄存器或普通 stack slot 中，再调用/内联一个以 raw pointer
构造目标 element 的 emplace。目标首槽取得所有权，两个字符串槽被初始化为空；源 raw
slot 不清零，也不存在局部 owner 析构。因此：

- controller 构造失败时，C++ new-expression 会释放尚未完成的 allocation；
- controller 已构造而 deque map/block growth 失败时，没有 owner 清理它，原版泄漏；
- emplace 成功后，entry 已经成为 Engine member container 的持久元素，后续任一步骤
  抛异常都不会自动回滚这个 element，而会留下对应阶段的部分状态。

本地恢复用“构造阶段临时 guard，emplace 前 `release()`”表达这两个看似相反但都可观察
的边界。

## 四平台函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Mouth builder | `0x66A39C` | `0x557894` | `0x1001A988C` | `0x1A8ED0` |
| controller allocation / ctor | `0x66A510` / `0x66A520` | `0x55794A` / `0x557954` | `0x1001A9974` / `0x1001A9984` | `0x1A8FD2` / `0x1A8FDC` |
| raw-pointer emplace | builder 内联 | `0x5677BA` | `0x1001A9BEC` | `0x1A9280` |
| element range destruction | `0x6809DC` | `0x563178` | `0x1001B764C` | `0x1B7094` |
| 独立 element / owner dtor | `0x680B44` | `0x56323E` | range 内联 | range 内联 |
| deque #6 dtor | `0x681A5C` | `0x563E68` | `0x1001B89E4` | `0x1B8084` |
| builder EH cleanup | builder 内 landing blocks | 未保留独立函数 | `0x1001A9B24` | `0x1A9178` |

这些地址均来自四份 recovery IDB 的本轮重新反编译。语义名称继续保留 `_guess`，因为
容器角色、数据流和边界已确定，但参考没有暴露原始源码标识符。

## Element 布局与逆成员析构

四端逻辑字段完全相同：

```text
64 bit (24B): +0 owner pointer, +8 label, +16 talkLabel
32 bit (12B): +0 owner pointer, +4 label, +8 talkLabel
```

四份析构证据不是单纯的“最后有一个 delete”，而是明确保留同一次序：

1. Release `talkLabel`；
2. Release `label`；
3. 读取并清空首槽 owner；
4. 若 controller 非空，析构其首字段 12B keyframe deque；
5. `operator delete(controller)`。

Android ARM64 把单 element 逻辑保留为 `0x680B44`，range helper 以 24B stride 调它。
Android ARMv7 range helper 先释放 `+8/+4` 两个字符串，再调用 `0x56323E` 处理首槽。
iOS 两端把以上步骤直接内联在跨 block range destruction 中。四端共同存在的“先两个
字符串、后首槽 owner”是 `unique_ptr` 位于第一个声明字段的正证据，而不是 Engine
外层手工 delete 循环的形状。

## 两套标准库 deque 内部实现

Android 两端使用 libstdc++ deque cursor/map 形状。Mouth element 不能整除 512B，
实现按完整 element 数量分配实际 data block：

| Android ABI | element | data block allocation | 每 block element |
| --- | ---: | ---: | ---: |
| ARM64 | 24B | 504B | 21 |
| ARMv7 | 12B | 504B | 42 |

ARM64 普通路径直接在 cursor 处写 `{raw, 0, 0}`；跨 block 时先 reserve map、分配
504B 新 block，再写同一记录。ARMv7 只有边界路径调用 `0x5677BA`；该 helper 必要时
先扩 map，再分配 504B block，随后执行：

```text
destination.owner = *rawArgumentSlot
destination.label = null
destination.talkLabel = null
// 不执行 *rawArgumentSlot = null
```

iOS 两端使用 libc++ 的 map/start/size 形状，并无条件调用独立 raw-emplace helper。
其 block allocation 也只包含完整 element，并非固定把 4096B 全部分配出来：

| iOS ABI | element | data block allocation | 每 block element |
| --- | ---: | ---: | ---: |
| ARM64 | 24B | 4080B (`0xFF0`) | 170 |
| ARMv7 | 12B | 4092B (`0xFFC`) | 341 |

两端 helper 必要时扩 map/block，按 `start + size` 定位 destination，复制 raw controller
pointer、清零两个 ttstr slot，最后 `size++`。portable `std::deque` 不企图伪造这些
私有 header 字节布局，但 element 类型、块化顺序、raw construction、字段赋值和自动
析构语义与参考保持一致。

## Builder 数据流与双 HM6 发布

四端共同顺序可归一为：

```text
for metadataIndex in [0, mouthControl.count):
    elem = mouthControl[metadataIndex]
    if !elem.enabled:
        continue

    raw = new EmoteMouthController(elem)
    mouthDeque.emplace_back(raw)       // {owner, null, null}
    back.label = elem.label
    back.talkLabel = elem.talkLabel
    HM6[back.label] = {6, metadataIndex}
    HM6[back.talkLabel] = {6, metadataIndex}
```

这里有三个不能被“整理”的细节：

1. `enabled` gate 在 allocation 之前；disabled 元素不产生 entry，但 metadata index
   仍正常前进；
2. HM6 value 保存的是原 metadata index，不是跳过 disabled 后的紧凑 deque index；
3. `label`、`talkLabel` 与两次 HM6 upsert 是四个严格连续阶段；Mouth 是唯一为一个
   controller 发布两条 HM6 引用、每帧再写两条 HM7 输出的 leaf category。

每帧 step 把 `beginFrame` 的 float 输出写到 `HM7[label]`，把动态 mouth/talk value
写到 `HM7[talkLabel]`。变量写入路径也按 key 区分：主 `label` 直接改变 beginFrame，
`talkLabel` 则进入 controller 的 target/ramp setter。

## 构造、插入和后续阶段的异常边界

### Controller ctor 失败

- Android ARM64 builder 的 `0x66A808` 是 new-expression pending allocation delete；
- iOS ARM64 cleanup 的 `0x1001A9B60..0x1001A9B68` 对 ctor call-site 保存的 controller
  allocation 执行 `operator delete`；
- iOS ARMv7 SjLj dispatcher 只有 ctor 阶段对应的 state 进入 `operator delete(a37)`；
- Android ARMv7 没有独立 EH function，但调用顺序仍是 allocation 后直接 ctor，指针
  尚未写入 Engine element，符合相同 new-expression 语义。

本地直接构造 `new EmoteMouthController(elem)`，并仅在表达式成功返回后建立 guard，
因此不会遗漏 compiler-generated new-expression cleanup。

### Deque growth/emplace 失败

构造成功后，四端 emplace 接收 raw pointer，且目标 helper不清空源 slot。Android
ARM64 的 map reserve / 504B block allocation、ARMv7 raw helper、iOS 两端 map grow /
4080B 或 4092B block allocation 都可能抛出；对应 cleanup 不会 delete 已完成的
controller。本地必须在调用 `emplace_back(raw)` 前释放 guard，才能保留这条原版泄漏。

### Emplace 后部分初始化

一旦 emplace 成功，entry owner 已经属于 Engine member deque。后续异常的可观察状态按
执行阶段递增：

| 抛异常阶段 | 已保留状态 |
| --- | --- |
| 读取/赋值 `label` | owning entry 已存在；两个 key 初始为空，label 可能仍空或已赋值 |
| 读取/赋值 `talkLabel` | owner 和 `label` 已保留；talkLabel 可能仍空或已赋值 |
| 第一次 HM6 upsert | owner 与两个 key 已保留；第一映射按容器抛出点可能尚未完成 |
| 第二次 HM6 upsert | 第一条 HM6 映射已经完成；第二条可能尚未完成 |

函数 unwind 只清理当前 TJS/ttstr 临时量和 dispatch owner，不调用 deque `pop_back`，
也不删除 controller。以后 Engine 析构或 metadata reset 会通过 element 的 unique owner
安全释放这个部分初始化记录。

## 本地源码与 IDB 回写

本地 `EmoteMouthControlEntry_Deque6` 已恢复成首字段
`std::unique_ptr<EmoteMouthController>`，保留 `label`、`talkLabel` 的声明顺序，并提供
与 native raw-emplace 对应的 raw-pointer converting constructor。所有运行期 caller
借用 owner 时显式 `.get()`；Engine 正常析构和 metadata reset 的手工 delete 循环已
删除，由 element 自动析构维持准确的字符串/controller 次序。

Builder 使用：

```cpp
std::unique_ptr<EmoteMouthController> pending(
    new EmoteMouthController(elem));
EmoteMouthController *raw = pending.release();
mouthDeque.emplace_back(raw);
mouthDeque.back().label = ...;
mouthDeque.back().talkLabel = ...;
```

四份 recovery IDB 已统一命名 raw-emplace、range/owner destructor 和可辨识的 EH cleanup，
并在 builder、ownership handoff、两个字符串槽及析构函数写入上述证据注释。

