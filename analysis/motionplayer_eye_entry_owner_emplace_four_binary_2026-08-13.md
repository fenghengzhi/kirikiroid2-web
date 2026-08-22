# MotionPlayer deque #4/#5 Eye/Eyebrow entry owner / raw-emplace 四参考复原（2026-08-13）

## 结论

本轮针对 `EmoteEngine` 最早的两个 controller owning container——deque #4（Eye）和
deque #5（Eyebrow）——分别检查四个当前参考二进制的 builder、边界 push helper、
element range destructor、deque destructor 与 builder 异常清理。旧源码把 element
首字段写成普通 controller pointer，再由 Engine 的手工循环 `delete`。这不能解释四端
共同存在的单指针 owner 析构形态，也不能自动产生 element 的逆成员析构顺序。

四端共同源码级结构应恢复为：

```cpp
template <class Controller>
struct EyeLikeEntry {
    explicit EyeLikeEntry(Controller *raw) : ctl(raw) {}

    std::unique_ptr<Controller> ctl;
    ttstr label;
};
```

这里的模板只是并列展示共同字段形状；原工程很可能仍使用两个命名 element 类型，不能
因为机器布局相同就把两种业务记录合并。`unique_ptr` 表示标准库层面的单指针 owner
语义；它不增加 element storage。
64 位两端 element 仍为 16B，32 位两端仍为 8B。声明顺序必须保持 `ctl` 后 `label`：
C++ 逆声明顺序析构会先 Release `label`，再销毁 controller owner，正好与四份
range destructor 一致。

Builder 不是“先建立局部 `unique_ptr`，再 move 到 deque”。它在 controller 构造成功
后保留一个 raw pointer，并把这个 raw pointer 作为 element 构造参数传给 deque
emplace。边界 helper 把 pointer 写入目标首槽、把 label 初始化为空，却不清零源 pointer
slot。这还带来一个很具体的 shipped exception boundary：controller 构造失败会释放
尚未完成的 allocation；controller 已构造而 deque reserve/growth 失败时，没有局部 owner
替它清理，因此该 controller 泄漏。本地恢复刻意保留这条边界。

## 四平台函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Eye builder | `0x669B5C` | `0x55739C` | `0x1001A91F4` | `0x1A8800` |
| 边界 raw-pointer emplace | builder 内联 | `0x56756A` | `0x1001A94B8` | `0x1A8B04` |
| element range destruction | `0x6804C4` | `0x562E48` | `0x1001B73DC` | `0x1B6EEC` |
| 独立 owner-slot dtor | range 内联 | `0x562ED8` | range 内联 | range 内联 |
| deque #4 dtor | `0x681BE4` | `0x563F60` | `0x1001B8A74` | `0x1B80D4` |
| builder EH cleanup | builder 尾部 landing blocks | 未独立保留 | `0x1001A93FC` | `0x1A8A08` |

deque #5 的独立映射如下；这些地址来自对 Eyebrow 自身路径的重新反编译，不是由 #4
地址平移或相似性推断：

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Eyebrow builder | `0x669F7C` | `0x557618` | `0x1001A9540` | `0x1A8B68` |
| 边界 raw-pointer emplace | builder 内联 | `0x567692` | `0x1001A9804` | `0x1A8E6C` |
| element range destruction | `0x680750` | `0x562FE0` | `0x1001B7514` | `0x1B6FC0` |
| 独立 owner-slot dtor | range 内联 | `0x563070` | range 内联 | range 内联 |
| deque #5 dtor | `0x681B20` | `0x563EE4` | `0x1001B8A2C` | `0x1B80AC` |
| builder EH cleanup | builder 尾部 landing blocks | 未独立保留 | `0x1001A9748` | `0x1A8D70` |

保守语义名中的 `_guess` 是刻意保留的：二进制没有提供原始 C++ 符号，当前名字描述
已证实的容器角色，而不声称恢复了作者原名。

## Element 布局与 deque block 实现

| ABI | Engine 中 deque #4 起点 | element | controller allocation | data block | 每 block element 数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android ARM64/libstdc++ | `+240` | 16B | `0x170` | `0x200` | 32 |
| Android ARMv7/libstdc++ | `+120` | 8B | `0xD8` | `0x200` | 64 |
| iOS ARM64/libc++ | `+144` | 16B | `0x110` | `0x1000` | 256 |
| iOS ARMv7/libc++ | `+72` | 8B | `0xA8` | `0x1000` | 512 |

deque #5 的 element/block 尺寸和对应 Engine 起点为：

| ABI | Engine 中 deque #5 起点 | element | controller allocation | data block | 每 block element 数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android ARM64/libstdc++ | `+320` | 16B | `0x150` | `0x200` | 32 |
| Android ARMv7/libstdc++ | `+160` | 8B | `0xB8` | `0x200` | 64 |
| iOS ARM64/libc++ | `+192` | 16B | `0xF0` | `0x1000` | 256 |
| iOS ARMv7/libc++ | `+96` | 8B | `0x88` | `0x1000` | 512 |

逻辑字段在四端相同：

```text
64 bit: +0 owner pointer, +8 ttstr label
32 bit: +0 owner pointer, +4 ttstr label
```

Android 使用 libstdc++ deque cursor/map 形状和 512B data block。Android ARM64
builder 在普通非边界路径直接写 `{raw, 0}`；到达 block 尾部时内联 reserve/map grow、
分配新 512B block，再写同一二元组。Android ARMv7 把边界路径保留为独立 helper。

iOS 使用 libc++ 的 map/start/size deque 表示。两端 builder 都无条件调用独立 emplace
helper；helper 必要时先扩 map/block，再按 start+size 定位目标，写入 owner pointer 和
空 label，最后 `size++`。iOS range destruction 跨 4096B block 逐 element 行走。
portable `std::deque` 不复制这两套 header 的字节布局，但保持 element 类型、块化序列、
插入次序与自动析构语义。

## Builder 数据流

Eye 与 Eyebrow 各自的四端共同控制流是：

```cpp
count = getCount(categoryControl);
for (metadataIndex = 0; metadataIndex < count; ++metadataIndex) {
    elem = categoryControl[metadataIndex];
    if (!elem.enabled)
        continue;

    raw = new CategoryController(elem);
    categoryDeque.emplace_back(raw);  // constructs owner from raw; label = empty
    categoryDeque.back().label = elem.label;
    HM6[categoryDeque.back().label] = {categoryType, metadataIndex};
}
```

Eye 的 `categoryType=4`，Eyebrow 的 `categoryType=5`。两者 controller payload 和
allocation size 不同，但 raw-emplace/label/HM6 次序相同。

几个次序都可观察，不能合并成一个“更安全”的临时 record：

1. `enabled` gate 先于 allocation；disabled 项不创建 controller，但 loop index 仍前进；
2. controller 完整构造后才调用 deque emplace；
3. 新 element 先以空 label 存在，随后才执行 `label` PropGet/CopyRef；
4. HM6 insertion 最后发生，保存 metadata loop index，不是 deque ordinal；
5. 任一后半段操作抛异常时，已成功 emplace 的 entry 留在 deque 中，没有事务回滚。

两个 category 各四份 emplace 实现最关键的共同指令语义都是：

```text
destination.owner = *rawArgumentSlot
destination.label = null
// no: *rawArgumentSlot = null
```

如果实参是一个被 move 的局部 `unique_ptr`，move construction 必须把源 owner 清零，
而且正常路径还必须析构局部 owner；四端都没有这些动作。这个负证据与 raw-pointer
converting constructor 完全吻合。

## 异常边界

### Controller 构造失败

- Android ARM64 两个 builder 尾部都保留 pending allocation 的 `operator delete` landing；
- iOS ARM64 `0x1001A93FC`（Eye）和 `0x1001A9748`（Eyebrow）中的
  `operator delete` 分支接收 builder 保存在寄存器中的 controller allocation，随后
  清理 TJS 临时量并 resume unwind；
- iOS ARMv7 两个 SjLj builder 在 allocation、controller ctor、deque emplace 前分别写不同
  call-site state；cleanup dispatcher 只有 controller 构造阶段会进入 pending allocation
  的 `operator delete` 分支；
- Android ARMv7 两个 builder 都没有保留独立 EH cleanup 函数，但 new-expression/ctor 顺序
  与另外三端相同，也没有把 pointer 写入 member 或 entry 后再调用 ctor。

本地现已把 Blink/Eyebrow 恢复成接收 metadata dict 的真实 C++ 构造函数；builder 直接执行
`new CategoryController(elem)`。因此 allocation 的构造失败清理由 new-expression 自身生成，
不再需要用“默认构造 + 自由函数 helper + pending guard”人工拼接这条边界。

### Deque growth/emplace 失败

controller ctor 成功后，四端只把 raw pointer 放入一个普通 stack slot/寄存器，再进入
deque growth：

- Android ARM64 的 512B block allocation 可能在写目标 element 前抛出；
- Android ARMv7 helper 的 map reserve 或 512B block allocation可能抛出；
- iOS helper 的 map/block reserve 可能在目标 element construction 前抛出。

这些调用点的 landing 都不会调用对应 controller destructor，也不会销毁一个局部
controller owner。因此 growth 失败会泄漏已完整构造的 controller。这不是建议的
现代 C++ 写法，但它是参考实现的边界行为；若本地用：

```cpp
std::unique_ptr<EmoteBlinkController> pending(new ...);
deque.push_back({std::move(pending), {}});
```

就会在 growth 失败时自动回收 controller，反而偏离原版。当前实现直接保存
`new CategoryController(elem)` 的 raw 结果，再 `emplace_back(raw)`：构造失败仍由
new-expression 回收 allocation，而构造成功后的 deque-growth failure 仍没有局部 owner。

## Element 与 deque 析构

两个 category 各自的四份 range destructor 对每个 element 都固定执行：

```cpp
entry.label.~ttstr();
entry.ctl.~unique_ptr<CategoryController>();
```

ABI 展开不同：

- Android ARM64：Release `+8` label；若 `+0` 非空，逆序销毁 Blink payload、delete；
  最后把 owner slot 写零；
- Android ARMv7：Release `+4` label，再调用接收 `+0` slot 地址的独立 owner dtor；
  owner helper 销毁 Blink payload、delete，最后清 slot；
- iOS ARM64/ARMv7：Release label，先把 owner slot exchange 为 null，再销毁 Blink
  payload并 delete。这与 libc++ `unique_ptr` 的先 exchange 后 delete 形状一致。

随后 outer deque destructor 才释放 data blocks 与 map/header allocation。四端都不做
controller null 的业务级容错；null 只在 owner destructor 中作为标准空 owner 分支处理。

## 本地修正

- `EmoteEyeControlEntry_Deque4::ctl` 与 `EmoteEyebrowControlEntry_Deque5::ctl` 分别从
  普通 raw pointer 改为各自 controller 的 `std::unique_ptr`；
- 两种 element 都增加 raw-pointer converting constructor，使 deque 直接在目标 slot
  构造 owner；
- 保持字段声明为 `ctl` 后 `label`，恢复 label-before-controller 析构；
- `buildEyeControl_guess` 与 `buildEyebrowControl_guess` 都直接执行真实构造
  `new CategoryController(elem)`，随后向对应 deque `emplace_back(raw)`；
- 所有借用调用改为 `.get()`；Engine 正常析构和 metadata reset 删除 deque #4/#5
  的手工 delete 循环，交给 element owner；
- 测试中仍可保留 raw observer pointer，但唯一 ownership 已转移到 deque element；
- 顺便修复上一纵切面遗留的四处测试 raw-member 访问，改为 direct owner `.get()`。

## 验证与 IDB 状态

- `cmake --build out/web/debug -j 8`：通过；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 复用 Web Debug 的真实 Emscripten
  defines/includes/ABI 参数，并加入既有 `out/syntax-check` Catch2/test config，执行
  `-fsyntax-only`：通过；唯一诊断为仓库既有 `_tss` literal-operator 弃用 warning；
- `git diff --check`：通过（仅工作树 CRLF 提示）；
- 四份 recovery IDB 已写入 builder allocation/raw-emplace、element range destruction、
  owner-slot 与 deque destruction 注释；可独立识别的 helper 已改为保守 `_guess` 名并保存。

本纵切面只闭合 deque #4 和 #5。deque #6..#10 仍需各自重新检查 builder 实参类型、
move/elision、range destructor 与异常 landing，不能因为 payload 形状相似就批量推断为
同一 owner 类型。
