# MotionPlayer deque #9 Selector owner、真实构造函数与 raw emplace 四端恢复（2026-08-13）

## 1. 范围与共同结论

本文用 `reference/binaries/` 中 Android ARM64、Android ARMv7、iOS ARM64、iOS
ARMv7 四个当前参考，独立闭合 `EmoteEngine` deque #9 Selector entry 的 source-level
所有权、controller 构造/析构、option move、raw-pointer emplacement、STL block ABI、异常
分界、reset 与正常析构。Selector/Transition 的业务状态机、HM6/HM7 数据流、保存恢复和
dormant target API 仍见
`analysis/motionplayer_selector_transition_four_binary_2026-08-11.md`。

四端共同支持的源级结构是：

```cpp
struct SelectorEntry {
    std::unique_ptr<EmoteSelectorController> ctl;
    ttstr label;
    uint8_t flag; // metadata builder 的 raw converting ctor 不初始化
    std::vector<TransitionEntry *> targets; // borrowed pointers
};

struct EmoteSelectorController {
    SelectorCommandTrack commandTrack;
    int32_t selState;
    int32_t selectedIndex;
    float invDuration;
    float accum;
    std::vector<SelectorOption> optionList; // refCtl is borrowed

    explicit EmoteSelectorController(std::vector<SelectorOption> &&options);
};
```

关键结论：

- entry 中 `ctl` 是唯一 owner；`targets[]` 与 controller 的 `optionList[].refCtl` 都是
  borrowed raw pointer；
- Selector 是真正的 C++ constructor，不是“默认构造对象后再调用 free ctor”；它构造
  command deque、清零状态、move-take option vector，并在 constructor body 内立即执行
  `applySelection(0, 0, 0)`；
- `applySelection` 在 constructor 中抛异常时，已构造的 option vector 与 command deque
  会逆序析构，外围 new-expression 随后释放 controller allocation；
- constructor 成功后 builder 只保留 raw pointer，并把它直接 emplace 到 entry owner；
  deque grow 失败没有本地 owner，保留参考实现的 controller 泄漏；
- emplace 成功后的 label/HM6 异常不 `pop_back`，已追加 entry 保留在 Engine 中；
- raw emplace 只写 owner、空 label、空 targets，不写 `flag` 或 padding；source raw-pointer
  slot 也不会被清零；
- entry 逆成员析构顺序是 `targets -> label -> ctl`；controller 再执行
  `optionList -> commandTrack`，从不解引用 borrowed Transition pointer；
- metadata reset 先清 #8 Transition、后清 #9 Selector；正常 Engine 析构则先 #9、后 #8。

本地旧实现的 raw owner 加显式 delete loop，普通退出结果大致相同，但不能表达 element
自身所有权。更重要的是，本地旧 free ctor 位于已经成功的默认构造之后：若
`applySelection(0)` 抛异常，new-expression 不会替这个外部 free function 回收 allocation。
本轮把它恢复为真正 constructor，并把 #9 entry 改为单指针 `unique_ptr` owner。

## 2. 四端函数映射

| 源码角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| Selector builder | `0x66ACDC` | `0x557E04` | `0x1001AA030` | `0x1A96D8` |
| Selector controller constructor | `0x66B778` | `0x5583B6` | `0x1001B7DFC` | `0x1B75EC` |
| constructor EH cleanup | body 尾部 `0x66B80C..0x66B85C` | 紧邻 body 的 `0x558406..0x558418` | `0x1001B7E80` | `0x1B76B6` |
| raw emplace | builder 内联 | `0x55841C`；boundary `0x567B0C` | `0x1001AA9D8` | `0x1AA0C8` |
| builder EH cleanup | landing blocks 内联 | builder 后 landing region | `0x1001AA6E8` | `0x1A9F44` / SjLj cases |
| clear / range destruction | `0x680F14` / `0x680FE8` | `0x563560` / `0x5635A8` / `0x56362C` / `0x5636F2` | `0x1001B79C8` | `0x1B7318` |
| target vector destructor | range body 内联 | range body 内联 | `0x1001B7B0C` | `0x1B7408` |
| option vector destructor | range/ctor EH 内联 | range/ctor EH 内联 | `0x1001B7E9C` | `0x1B76E8` |
| 完整 deque destructor | `0x6817C4` | `0x563CF4` | `0x1001B890C` | `0x1B800C` |
| metadata reset | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |

四份 recovery IDB 已使用 `EmoteSelectorControlDeque_emplaceRaw_guess`、
`...emplaceRawBoundary_guess`、`...clear_guess`、`...clearImpl_guess`、
`EmoteSelectorControlEntryRange_destroy_guess`、
`EmoteSelectorTargetVector_dtor_guess`、`EmoteSelectorOptionVector_dtor_guess`、
`EmoteSelectorController_ctor_ehCleanup_guess` 与
`EmoteEngine_buildSelectorControl_ehCleanup_guess` 等语义名。精确原始符号未知的名称继续
保留 `_guess`。

## 3. Controller 的真实对象结构

### 3.1 四端对象大小

| 参考 | command deque + base state | scalar 区 | option vector 起点 | controller 大小 |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 / libstdc++ | `+0..+83` | `+84..+99` | `+104` | `0x80` |
| Android ARMv7 / libstdc++ | `+0..+43` | `+44..+59` | `+60` | `0x48` |
| iOS ARM64 / libc++ | `+0..+51` | `+52..+67` | `+72` | `0x60` |
| iOS ARMv7 / libc++ | `+0..+27` | `+28..+43` | `+44` | `0x38` |

scalar 的共同源序为 `selState, selectedIndex, invDuration, accum`。64 位 ABI 在 accum
之后自然补四字节对齐再放三指针 vector；这不是 portable source 字段。command track
只有 `deque<12-byte command>` 加一个 base state word，不是完整 Angle controller。

### 3.2 Constructor 数据流

四端可归一为：

```cpp
EmoteSelectorController::EmoteSelectorController(
    std::vector<SelectorOption> &&options)
  : commandTrack{},
    selState(0), selectedIndex(0), invDuration(0), accum(0),
    optionList(std::move(options)) {
    applySelection(this, 0, 0.0f, 0.0f);
}
```

Android ARMv7 最直观：先清零 `+0..+39`，初始化 command deque，再清零 `+44..+71`；
随后把传入 vector 的 begin/end/cap 交换到 `+60/+64/+68`，源 vector 留空，最后调用
`applySelection(0)`。另外三端只有 STL ABI 与自然 padding 差异。

这也确定 option buffer 是 move-take，不是复制。option element 在 64 位为 16 字节
`{refCtl+0, off+8, on+12}`，32 位为 12 字节 `{refCtl+0, off+4, on+8}`；`refCtl`
只借用 deque #8 controller。

## 4. Constructor unwind 与 new-expression 分工

`applySelection(0)` 可能沿 Transition setter/deque allocation 路径抛异常。四端 constructor
unwind 都执行：

```text
destroy optionList storage
destroy commandTrack deque storage
resume exception
```

具体证据：

- Android ARM64 `0x66B80C..0x66B85C` 先检查/释放 controller `+104` option begin，
  再释放 libstdc++ command deque 的 blocks/map；
- Android ARMv7 constructor 正常 body 在 `0x558404` 返回，但紧邻的 landing pad
  `0x558406..0x558418` 读取 controller `+60` option begin、delete 后调用 command-deque
  destructor，再 `_Unwind_Resume`；
- iOS ARM64 `0x1001B7E80` 调 option-vector destructor，再调 command-deque destructor；
- iOS ARMv7 `0x1B76B6` 的 SjLj cleanup 调 `0x1B76E8`，再调 command-deque destructor。

这些 landing pad 只析构已构造的成员，不负责释放 `this` allocation。外围 builder 的
new-expression pending cleanup 负责第二层：constructor 若未成功返回，就对刚申请的
controller memory 调 `operator delete`。Android ARMv7 在 `0x558172` 明确走该 delete；
iOS ARM64 builder cleanup 的 controller allocation delete 位于 `0x1001AA72C`；另外两端
也有等价 landing/SjLj case。

因此不能把反编译调用形态机械写成：

```cpp
auto *ctl = new EmoteSelectorController(); // 已完整构造
EmoteSelectorController_ctor(ctl, std::move(options)); // 外部 free function
```

这种写法中第二行抛异常时，第一行的 new-expression 已经结束，不会自动 delete `ctl`，
与四端不符。

## 5. Builder、raw owner emplacement 与数据流

共同伪代码为：

```cpp
for (int metadataIndex = 0; metadataIndex < count(selectorControl);
     ++metadataIndex) {
    Variant elem = selectorControl[metadataIndex];
    ttstr label = getString(elem, L"label");
    if (!getBool(elem, L"enabled")) {
        removeVariableLabel(label);
        continue;
    }

    std::vector<SelectorOption> options;
    for (Variant optionMeta : elem[L"optionList"]) {
        TransitionEntry *match = firstTransitionWithLabel(optionMeta[L"label"]);
        options.push_back({match ? match->ctl.get() : nullptr,
                           getFloat(optionMeta, L"offValue"),
                           getFloat(optionMeta, L"onValue")});
        if (match) {
            match->flag = 0;
            removeVariableLabel(match->label);
        }
    }

    EmoteSelectorController *ctl =
        new EmoteSelectorController(std::move(options));
    selectorDeque.emplace_back(ctl);
    selectorDeque.back().label = label;
    HM6[selectorDeque.back().label] = {8, metadataIndex};
}
```

disabled 项不分配 controller、不形成 entry，但 HM6 index 仍采用原 metadata loop index。
option Transition 匹配为线性 first-match；找不到时 `refCtl=nullptr`，constructor 的初始
`applySelection(0)` 跳过该项。匹配后只转移直接写权限，不转移所有权。

raw-emplace helper 的输入是一个装有 raw pointer 的调用者 slot。四端都只把其值复制进
entry `ctl` 字段，不把 source slot 清零。这是“从 raw pointer 构造目标 unique owner”，
不是 local `unique_ptr` move。

## 6. Selector entry 自然布局

### 6.1 64 位

Android ARM64 与 iOS ARM64 都是 48 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 8 | `unique_ptr<EmoteSelectorController>` 单指针 storage |
| `+8` | 8 | `ttstr label` |
| `+16` | 1 | `uint8_t flag`，builder 不写 |
| `+17..+23` | 7 | alignment/padding，builder 不写 |
| `+24/+32/+40` | 24 | `vector<TransitionEntry *> targets` begin/end/cap |

### 6.2 32 位

Android ARMv7 与 iOS ARMv7 都是 24 字节：

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| `+0` | 4 | `unique_ptr<EmoteSelectorController>` 单指针 storage |
| `+4` | 4 | `ttstr label` |
| `+8` | 1 | `uint8_t flag`，builder 不写 |
| `+9..+11` | 3 | alignment/padding，builder 不写 |
| `+12/+16/+20` | 12 | `vector<TransitionEntry *> targets` begin/end/cap |

普通与边界 emplace 都写 owner、空 label 和三个零 vector pointer；没有任何 store 覆盖
gate。不能因为相邻 Transition entry 的 flag 初值为 1，就给 Selector flag 增加 default
member initializer。portable entry 的 raw converting constructor 故意不列 `flag`，使这条
native 边界保持为 indeterminate；业务路径在读取前是否写 gate，由参考调用链本身决定。

## 7. deque block ABI

| 参考 | STL | entry 大小 | block allocation | 每 block entry 数 |
| --- | --- | ---: | ---: | ---: |
| Android ARM64 | libstdc++ | 48 | `0x1E0` / 480 | 10 |
| Android ARMv7 | libstdc++ | 24 | `0x1F8` / 504 | 21 |
| iOS ARM64 | libc++ | 48 | `0xFF0` / 4080 | 85 |
| iOS ARMv7 | libc++ | 24 | `0xFF0` / 4080 | 170 |

Android 分别符合 `floor(512/48)=10` 与 `floor(512/24)=21`。A32 boundary helper
`0x567B0C` 明确申请 504 字节，随后在 24-byte slot 写 owner/empty label/empty targets；
A64 clear 以 480-byte block 遍历。iOS clear 以 `0x55` / `0xAA` 做 block index 与余数，
对应 4080 字节。Web 源码只保留 typed `std::deque`，不复制 native STL 常数。

## 8. 异常边界

### 8.1 Constructor 失败

如第 4 节，成员 unwind 与 new-expression pending allocation delete 两层都存在。最终没有
entry，也没有 controller/option/command-deque 泄漏；builder 的 moved-from local option
vector 为空，其析构不重复释放已由 controller unwind 释放的 buffer。

### 8.2 Constructor 成功、deque grow 失败

constructor 返回后 builder 只有 raw pointer。边界 emplace 可能为 block/map 分配而抛出；
四端 builder cleanup 在这个 call-site 阶段都没有 controller destructor/delete，也没有
local owner reset。目标 entry 尚未形成，因此新 controller 泄漏。

本地必须保持：

```cpp
auto *ctl = new EmoteSelectorController(std::move(options));
selectorDeque.emplace_back(ctl);
```

不能用 local `unique_ptr` 覆盖 emplace，也不能先构造 local owning entry 再 move push；
两者都会消除 grow-failure 泄漏，改变参考边界。

### 8.3 emplace 后失败

entry 一旦形成，owner 已由 Engine 成员接管。随后的 label 赋值、HM6 lookup/insert/value
write 若抛出，不会 pop entry：

| 阶段 | 保留状态 |
| --- | --- |
| label 赋值前/中 | owned controller、empty/赋值中的 label、indeterminate gate、empty targets |
| HM6 前/中 | 完整 entry；HM6 可能无节点，也可能留下已插入/部分写入节点 |

这些 entry 最终由 metadata reset 或 Engine destructor 自动清理，与 grow-failure 的无 owner
泄漏是两个不同阶段。

## 9. 析构顺序与 borrowed lifetime

四端 range/clear body 的 element 语义是：

```cpp
entry.targets.~vector(); // raw elements，不 delete Transition entry
entry.label.~ttstr();
entry.ctl.~unique_ptr();
```

controller owner 析构继续执行：

```cpp
controller.optionList.~vector(); // refCtl raw elements，不 delete
controller.commandTrack.~SelectorCommandTrack();
operator delete(controller);
```

Android ARM64 `0x680FE8` 依次删除 entry `+24` targets buffer、release `+8` label，
然后取 `+0` controller；controller 内先删除 `+104` option buffer，再释放 `+0` command
deque blocks/map，最后 delete controller。iOS ARM64 `0x1001B79C8` 分别调用 target-vector、
option-vector 和 command-deque helper；iOS ARMv7 `0x1B7318` 是同一顺序。A32 Android
range/owner helper给出相同结果。

metadata reset 依成员声明正序先 clear #8，再 clear #9。此时 option `refCtl` 与 dormant
`targets[]` 会短暂悬空，但 #9 析构只销毁 raw-pointer buffers，从不解引用元素，所以不会
访问已死 Transition。正常 Engine destructor 依声明逆序先 #10、#9、#8，borrower 在
owner 前消失，没有 dangling 窗口。本地保留两条不同顺序，不引入 shared ownership。

## 10. 本地恢复与验证口径

本轮源码修改：

- `EmoteSelectorController_ctor` free helper 改为真正的
  `EmoteSelectorController(vector&&)` constructor；
- constructor 通过 member initializer 构造 command track、清零四个 scalar、move-construct
  option vector，再在 body 内初选 option 0；
- `EmoteSelectorControlEntry_Deque9::ctl` 从 raw pointer 改为
  `std::unique_ptr<EmoteSelectorController>`；
- entry 增加 raw-pointer converting constructor；该 constructor 不初始化 `flag`；
- builder 改为真实 new-expression 后直接 `_vectorVarDeque9.emplace_back(ctl)`，再赋 label；
- 删除 metadata reset 与正常 destructor 的 Selector 显式 free-dtor/delete/null loop；
- 所有 free helper observer call 改用 `.get()`；直接 `->` 访问保持 unique_ptr observer 语义；
- 测试 fixture 改为调用真实 constructor，并继续用 raw observer 检查 Engine-owned object；
- portable compiled-source 注释不记录绝对地址，地址只保存在本文。

四份 recovery IDB 已写入并保存：constructor EH、builder EH、raw emplace、boundary block、
clear/range destruction、target/option vector destructor、完整 deque destructor的语义名与
owner/borrow/异常注释。验证结果：

- 实际 GNU Bison 版本为 3.8.2，Emscripten 为 4.0.23；
- `cmake --build --preset "Web Debug Build"` 重新编译 Selector/Engine 相关对象，并成功
  链接 `index.html` / `index.wasm`；
- 复用 `out/web/debug/compile_commands.json` 中 `EmoteEngine.cpp` 的真实 Web define、
  include、pthread、wasm-exception 与 SIMD 参数，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only` 成功；
- warning 仅为仓库既有的 `_tss`、imagepacker attribute 与 Emscripten 实验选项提示；
- 本轮未执行 Catch2 runtime，因此不把 syntax-only 表述成运行时测试通过；
- `git diff --check` 结果在最终源码/文档检查后记录。

#9 纵切面闭合不表示整个 motionplayer 插件已经达到 100%。
