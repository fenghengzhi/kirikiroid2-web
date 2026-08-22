# MotionPlayer deque #8 Transition owner、raw emplace 与异常边界四端恢复（2026-08-13）

## 1. 范围与结论

本文闭合 `EmoteEngine` deque #8 `transitionControl` 的 element 所有权、builder、
raw-pointer emplacement、STL block ABI、异常回滚、reset/正常析构，以及 Selector 对
controller 的借用寿命。Transition 与 Selector 的业务状态机、HM6/HM7 数据流、保存恢复
和 target API 仍见
`analysis/motionplayer_selector_transition_four_binary_2026-08-11.md`。

证据全部重新取自 `reference/binaries/` 的四份当前参考，不沿用旧 `libkrkr2.so`
注释。四端共同支持的源级 element 为：

```cpp
struct TransitionEntry {
    std::unique_ptr<EmoteVarController> ctl;
    ttstr label;
    uint8_t flag;
};
```

核心结论是：

- `ctl` 是 entry 内唯一的单指针 owner；Selector option 只借用 `ctl.get()`；
- builder 先完成 `new EmoteVarController(1)`，再把得到的 raw pointer 直接传给
  deque element 的 converting constructor；没有 builder-local `unique_ptr`；
- emplace 先形成 `{owner=raw, label=empty, flag=1}`，随后才读取/写入实际 label，最后
  写 HM6 `{type=7,index=原 metadata index}`；
- controller 构造失败由 C++ new-expression 释放 pending allocation；controller 已构造后
  若 deque 边界扩容失败，raw pointer 没有本地 owner，保留原版泄漏边界；
- emplace 成功后的 label/HM6 异常不 `pop_back`，已追加 entry 继续由 Engine 成员拥有；
- element 析构严格是 `label -> ctl`；controller owner 会先清空/取走 slot，再执行
  `EmoteVarController` 析构和 `operator delete`；
- metadata reset 依声明正序清 #8 再清 #9，产生短暂的 dangling Selector borrow；正常
  Engine 析构依声明逆序先清 #9 再清 #8，不产生该窗口。两条路径中 Selector 清理都不会
  解引用 option 的 borrowed controller，因此参考行为不需要 shared ownership。

本地旧 element 用 raw pointer 加 Engine 显式 delete 循环表达 ownership。它在普通销毁上
结果接近，但源结构、自动异常展开和 element 自身析构语义都不准确。本轮已把 #8 单独迁移
为 `std::unique_ptr<EmoteVarController>`，未由相邻 deque 的结论类推。

## 2. 四端函数映射

| 源码角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Transition builder | `0x66A8A4` | `0x557B84` | `0x1001A9C9C` | `0x1A9314` |
| raw emplace | builder 内联 | boundary helper `0x5678E4`，普通路径内联 | `0x1001A9F80` | `0x1A963C` |
| builder EH cleanup | landing blocks 内联 | landing region 紧随 builder body | `0x1001A9EC4` | `0x1A9540` |
| clear / range destruction | `0x680D1C` | `0x5633C0` / `0x563408` / `0x56348C` / `0x5634FA` | `0x1001B78A0` | `0x1B723C` |
| 完整 deque destructor | `0x6818D4` | `0x563D70` | `0x1001B8954` | `0x1B8034` |
| metadata reset | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |

Android ARM64 把 raw emplacement 普通/边界路径都内联到 builder。Android ARMv7 的普通
路径内联，只在当前 block 尾部调用 `0x5678E4`。iOS 两端使用独立 libc++ helper。
这些只是 STL/优化器差异；四端 element 写入集合与 source raw slot 行为完全一致。

四个 recovery IDB 中已使用下列语义名：

- `EmoteTransitionControlDeque_emplaceRaw_guess`，Android ARMv7 边界版本保留
  `...emplaceRawBoundary_guess`；
- `EmoteTransitionControlDeque_clear_guess` / `...destroyRange_guess`；
- `EmoteTransitionControlEntryRange_destroy_guess`；
- `EmoteEngine_buildTransitionControl_ehCleanup_guess`；
- `EmoteTransitionControlDeque_dtor_guess`。

精确原始符号未知的名称继续带 `_guess`。

## 3. builder 数据流与 owner 建立

四端共同伪代码为：

```cpp
for (int metadataIndex = 0; metadataIndex < count(transitionControl);
     ++metadataIndex) {
    Variant elem = transitionControl[metadataIndex];
    if (!getBool(elem, L"enabled"))
        continue;

    EmoteVarController *ctl = new EmoteVarController(1);
    transitionDeque.emplace_back(ctl);
    TransitionEntry &back = transitionDeque.back();
    back.label = getString(elem, L"label");
    HM6[back.label] = {7, metadataIndex};
}
```

这里的 `new` 是普通 C++ new-expression，而不是“先分配、把 raw pointer 放进本地
`unique_ptr`、再 move 到 deque”。反编译中 operator new 与 constructor 必然分开出现，
不能据此虚构 source-level pending owner。

四端 raw emplace 都执行相同三项初始化：

```text
entry.ctl   = raw controller pointer
entry.label = empty ttstr/null string holder
entry.flag  = 1
```

helper 从调用者提供的 raw-pointer slot 读取指针，但不把该 source slot 清零。这同时证明：

1. 目标字段是从 raw pointer 构造的单指针 owner，而不是从本地 `unique_ptr` move；
2. owner 建立后 entry slot 负责最终 controller 析构；
3. owner 建立前若 deque grow 抛异常，source slot 仍只是无 cleanup 的 raw pointer。

enabled gate 位于 allocation/emplace 之前。disabled metadata 不分配 controller、不产生
占位 entry，但 loop index 仍递增。HM6 index 使用原始 `metadataIndex`，不是 enabled entry
在 deque 中的压缩下标。builder 入口不清空旧容器，所以重复调用继续追加。

## 4. element 自然 ABI

### 4.1 64 位参考

Android ARM64 与 iOS ARM64 的 Transition entry 都为 24 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 8 | `unique_ptr<EmoteVarController>` 单指针存储 |
| `+8` | 8 | `ttstr label` |
| `+16` | 1 | `uint8_t flag` |
| `+17..+23` | 7 | 尾部 ABI padding / 未使用字节 |

### 4.2 32 位参考

Android ARMv7 与 iOS ARMv7 的 Transition entry 都为 12 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 4 | `unique_ptr<EmoteVarController>` 单指针存储 |
| `+4` | 4 | `ttstr label` |
| `+8` | 1 | `uint8_t flag` |
| `+9..+11` | 3 | 尾部 ABI padding / 未使用字节 |

尾 padding 没有独立读写，不能在 portable C++ 中声明为源字段。`unique_ptr` 的默认 deleter
没有额外状态，因此其对象表示在四端都恰好是一根 pointer；这解释了本地从 raw pointer
迁移到 `std::unique_ptr` 后仍保持目标位宽上的 24/12 字节自然布局。

字段声明顺序也由析构体反向验证：每个 range destructor 都先释放 `label`，再销毁 `ctl`
owner。`flag` 是平凡字段，不出现析构调用。

## 5. deque block ABI

| 参考 | STL | entry 大小 | block allocation | 每 block entry 数 |
| --- | --- | ---: | ---: | ---: |
| Android ARM64 | libstdc++ | 24 | `0x1F8` / 504 | 21 |
| Android ARMv7 | libstdc++ | 12 | `0x1F8` / 504 | 42 |
| iOS ARM64 | libc++ | 24 | `0xFF0` / 4080 | 170 |
| iOS ARMv7 | libc++ | 12 | `0xFFC` / 4092 | 341 |

Android 符合 libstdc++ `max(512 / sizeof(T), 1)` 的 element-count 规则：24 字节取
`floor(512/24)=21`，12 字节取 `42`，实际 block allocation 均是 504 字节。边界
emplace 在旧 finish slot 构造本项，并让新 block 提供新的尾后 cursor。

iOS libc++ helper/clear 直接以 `0xAA` / `0x155` 做 block index 与余数运算。对应
allocation/边界常数分别是 `170*24=4080` 与 `341*12=4092`。四端 block 常数只属于
各 native STL ABI，不应复制进 Web 源码；portable 结构仍是
`std::deque<EmoteTransitionControlEntry_Deque8>`。

## 6. 异常边界

### 6.1 allocation 与 controller constructor

四端都先申请目标 ABI 下的 `EmoteVarController` 对象大小：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| ---: | ---: | ---: | ---: |
| `0x80` | `0x48` | `0x60` | `0x38` |

如果 operator new 本身失败，没有 allocation。若 `EmoteVarController(1)` 在构造内部抛出，
new-expression 的 pending cleanup 会对对象 allocation 调用 `operator delete`，同时已构造
的 controller 成员由 controller constructor unwind 负责。四端 EH 均支持这个边界；
iOS ARM64 cleanup `0x1001A9EC4` 和 iOS ARMv7 SjLj case 明确保留该 allocation-delete。

### 6.2 deque emplace/growth

controller constructor 正常返回后，builder 只持有 raw pointer。普通 block 内 emplacement
不需要分配；在 block/map 边界则可能由 deque grow 抛出。四端 cleanup 在该阶段都没有：

- `EmoteVarController` destructor；
- controller allocation 的 `operator delete`；
- 本地 single-pointer owner reset；
- 已存在 entry 的 pop/rollback。

所以 boundary grow 失败时 deque 不形成本项，而刚完成的 controller 泄漏。这不是希望在
现代代码中推广的模式，但属于参考二进制的可观察异常边界。本地 builder 因而必须直接：

```cpp
EmoteVarController *ctl = new EmoteVarController(1);
transitionDeque.emplace_back(ctl);
```

不能先用 local `unique_ptr` 持有再 `release()` 到 emplace 之后，也不能构造本地 owning
entry 再 `push_back(std::move(entry))`；两者都会在 grow failure 时回收 controller，改变
原版行为。

### 6.3 emplace 后的 label 与 HM6

emplace 成功后 entry 已稳定存在，且已拥有 controller、空 label、`flag=1`。随后任何
property/Variant/ttstr/HM6 操作异常都不会触发 `pop_back`：

| 抛出阶段 | Engine 成员中保留的状态 |
| --- | --- |
| label property getter / 转换 | `{owned controller, empty label, flag=1}` |
| label 赋值 | owner 与 flag 保留；label 处于该赋值路径已经形成的状态 |
| HM6 lookup/insert/value write | 完整 Transition entry 保留；HM6 可能没有节点，也可能保留已插入/部分写入节点 |

后续 reset 或 Engine destructor 会通过 entry 的 owner 正常清理 controller。因此“grow
failure 泄漏”与“post-emplace failure 由成员稍后清理”是两个不同的异常阶段，不能合并
为笼统的“push 失败会回滚”或“所有异常都会泄漏”。

## 7. flag、Selector borrow 与数据流

新 entry 的 `flag=1` 允许 HM6 TYPE 7 的直接写入路径把目标送入本 controller。Selector
builder 在 option label 第一次匹配某个 Transition entry 时：

```cpp
option.refCtl = entry.ctl.get(); // borrow only
entry.flag = 0;
removeVariableLabel(optionLabel);
break;
```

重复 label 只借用第一项。无匹配时 option pointer 保持 null。`flag` 只门控直接外部写入，
不门控 per-frame Transition step；被 Selector 借用后 controller 仍由 #8 每帧推进并把标量
输出写入 HM7。Selector option、Selector entry dormant `targets[]` 都不 delete controller。

owner 因而只有一条：

```text
Engine deque #8 entry unique_ptr
  └─owns─> EmoteVarController
               ▲
               └─borrowed by Selector option in deque #9
```

不应把该关系改成 `shared_ptr`，也不应在 Selector destructor 中回删或清空 Transition
entry。

## 8. reset 与正常析构是两种顺序

### 8.1 metadata reset

四端 `resetMetadataState` 按十个成员的声明正序清理：#1、#2、……、#8、#9、#10。
因此 #8 clear 先销毁 Transition owner，#9 Selector option 内的 raw borrow 随即短暂悬空，
直到下一步 #9 clear 销毁 Selector controller/option vector。

该窗口是安全的唯一原因是 #9 element/controller destructor 只释放自己的 command deque、
option vector buffer、label/targets 和 owner storage，不会解引用或 delete `Option::refCtl`。
本地 `resetMetadataState()` 保留 #8 `.clear()` 后 #9 clear 的原顺序，不为消除 dangling
窗口而交换两个容器。

### 8.2 正常 Engine destructor

正常 C++ member unwinding 按声明逆序运行：#10、#9、#8、……、#1。四端完整 deque
destructor 调用链均证明 #9 Selector 先销毁、#8 Transition owner 后销毁，所以正常析构
期间 option borrow 一直有效，直到 borrower 自身已消失。

本地 `EmoteEngine::~EmoteEngine()` 也先释放 #9 backing storage，再释放 #8。#8 entry
现在由 `unique_ptr` 自动完成 `label -> controller` 的逆成员析构，不再需要 Engine 外层
显式 delete loop。

## 9. clear/range destruction 与完整 deque destructor

四端 element body 可归一为：

```cpp
entry.label.~ttstr();
entry.ctl.~unique_ptr<EmoteVarController>();
```

Android ARM64 range helper 按 24-byte stride 穿过 504-byte block；Android ARMv7 的
`0x56348C`/`0x5634FA` 按 12-byte stride 先调用 label release，再调用已识别的
`EmoteVarController_ownerPtrDestroy_guess`。iOS clear helper 在 controller 路径先把 owner
slot 写零，再调用 controller destructor/delete；这与 libc++/compiler 对 single-pointer
`unique_ptr` 析构的 lowering 一致，不表示源代码有额外可观察成员赋值。

完整 deque destructor 在 element cleanup 后释放 block 与 map：Android 使用 libstdc++
range helper和 map/block tail，iOS clear 后释放余下 libc++ blocks/map。portable Web 代码
通过 typed `std::deque` 和在 native 阶段 swap-to-empty 的 helper同时触发 element 与 backing
storage 释放。

## 10. 本地恢复

本轮源码修改：

- `EmoteTransitionControlEntry_Deque8::ctl` 从 raw pointer 改为
  `std::unique_ptr<EmoteVarController>`；
- 增加从 raw controller 构造 entry 的 converting constructor，并保留测试所需的
  `{controller,label,flag}` convenience constructor；
- builder 从“构造本地 entry 再 move push”改为直接 `_auxVarDeque8.emplace_back(ctl)`，
  随后再赋 label；
- 删除 metadata reset 与正常 destructor 中 #8 的显式 delete/null loop；
- 所有 step/reset/setTarget/serialize/restore/Selector borrow observer 改用 `.get()`；
- 源码注释只记录四端共同语义，不写任何单一参考绝对地址。

本轮没有把 Selector #9 owner 一并迁移；#9 的 element gate 未初始化、controller 构造、
option vector move、entry raw-emplace 与异常回滚必须用其自身四端路径独立闭合。

## 11. IDB 写回与验证

四份 recovery IDB 已写入：

- raw-emplace、clear/range destruction、完整 destructor 与独立 EH cleanup 的语义名；
- entry 字段偏移、stride、block allocation/capacity 和 `label -> owner` 析构顺序；
- source raw slot 不清零、constructor-failure delete、grow-failure 无 controller cleanup；
- post-emplace label/HM6 异常无 pop，以及 reset/正常析构两种 #8/#9 顺序。

四份 IDB 在本轮语义写回后原位保存。源码验证包括 Web Debug 完整构建、复用真实
Emscripten 参数的 motionplayer Catch2 TU `-fsyntax-only` 和 `git diff --check`；不把未执行
的 Catch2 runtime 结果表述为已通过。完整 motionplayer 目标仍处于恢复中，#8 纵切面闭合
不代表整个插件已经达到 100%。
