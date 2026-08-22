# Transition / Selector builder：所有权、first-match 借用、gate 与构造期数据流

日期：2026-08-15

本轮对四个当前参考恢复库中的 Transition builder、Selector builder 及其 raw-emplace helpers 分别重新反编译。结论没有从旧 `libkrkr2.so` 注释或相似 builder 外推。Selector 必须与 Transition 一起分析：前者持有后者 controller 的裸借用指针、清除 direct-write gate，并在自己的构造函数内立刻改变被借用 controller 的值。

## 1. 函数与对象 allocation

### Transition builder

| 参考 | `EmoteEngine_buildTransitionControl_guess` | 大小 | `EmoteVarController(1)` allocation |
|---|---:|---:|---:|
| Android ARM64 | `0x66A8A4` | `0x438` | `0x80` |
| Android ARMv7 | `0x557B84` | `0x1C8` | `0x48` |
| iOS ARM64 | `0x1001A9C9C` | `0x224` | `0x60` |
| iOS ARMv7 | `0x1A9314` | `0x22C` | `0x38` |

### Selector builder

| 参考 | `EmoteEngine_buildSelectorControl_guess` | 大小 | `EmoteSelectorController` allocation |
|---|---:|---:|---:|
| Android ARM64 | `0x66ACDC` | `0x94C` | `0x80` |
| Android ARMv7 | `0x557E04` | `0x350` | `0x48` |
| iOS ARM64 | `0x1001AA030` | `0x6B4` | `0x60` |
| iOS ARMv7 | `0x1A96D8` | `0x658` | `0x38` |

两类 controller 在同一 ABI 上恰好得到相同总 allocation size，但内部类型和 ownership 完全不同，不能据此合并：Transition 是一通道 `EmoteVarController`；Selector 含 command deque、状态字段和 `vector<Option>`。

## 2. Transition builder 的严格顺序

四份实现共同执行：

1. 循环前 snapshot metadata count。
2. 用原始 metadata index 取 element。
3. disabled 直接跳过，原始 index 仍递增。
4. `new EmoteVarController(1)`。
5. raw-emplace destination `{owner, empty label, flag=1}`；source raw slot 不清零。
6. 查询/复制 element `label` 到刚压入的 destination。
7. map get-or-insert 后写 `{type=7,index=原始 metadata index}`。

| 阶段 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| count snapshot | `0x66A938` | `0x557BB6` | `0x1001A9CF4` | `0x1A9390` |
| enabled test | `0x66AA0C` | `0x557C36` | `0x1001A9D7C` | `0x1A9406` |
| allocate/ctor | `0x66AA18..0x66AA24` | `0x557C40..0x557C46` | `0x1001A9D88..0x1001A9D94` | `0x1A9412..0x1A941C` |
| flag=1 destination write | `0x66AA44` / `0x66AAA8` | `0x557C62` / helper `0x56791E` | helper `0x1001AA018` | helper `0x1A96C8` |
| label getter/write | `0x66AB04..0x66AB50` | `0x557CA8..0x557CDA` | `0x1001A9DF0..0x1001A9E34` | `0x1A9484..0x1A94D0` |
| type-7 map publication | `0x66AB68..0x66AB70` | `0x557CE8..0x557CF0` | `0x1001A9E48..0x1001A9E50` | `0x1A94E0..0x1A94EA` |

### Transition deque element与 block

```cpp
struct TransitionEntry {
    unique_ptr<EmoteVarController> controller;
    ttstr label;
    uint8_t directWriteFlag; // explicit 1
    // ABI padding only
};
```

| ABI | raw-emplace | entry | elements/block | element bytes/block |
|---|---:|---:|---:|---:|
| Android A64 libstdc++ | builder 内联 | 24B | 21 | `0x1F8` |
| Android A32 libstdc++ | `0x5678E4` (`0x50`) | 12B | 42 | `0x1F8` |
| iOS A64 libc++ | `0x1001A9F80` (`0xB0`) | 24B | 170 | 4080B |
| iOS A32 libc++ | `0x1A963C` (`0x9A`) | 12B | 341 | 4092B |

raw-emplace 精确写 owner、空 label、byte flag=1；padding 不属于 source field。controller 构造完成后没有 caller RAII owner，因此 destination 构造前的 deque growth allocation failure 保留原版 raw-pointer leak 边界。append 成功后由 transition deque 独占 controller。

## 3. Selector builder 的严格顺序

对每个 selector metadata element：

1. 先查询 selector `label`，然后才查询 `enabled`。
2. disabled 时把已经读取的 selector label 从 `_variableLabels` 删除，随后跳过；不构造 entry、不发布 map ref。
3. enabled 时读取 `optionList` 并 snapshot option count。
4. 每个 option 读取 option `label`。
5. **每个 option 都从 transition deque 的 begin 重新开始线性扫描**。
6. 第一个 label 相等的 transition entry：借出 controller raw pointer、只把该 entry 的 flag 清零、从变量标签列表删除 option label，然后 break。
7. 没有匹配时 option 的 borrowed pointer 保持 null，也不删除 option label。
8. 读取 double `offValue`/`onValue` 并窄化成 float，按原顺序 push 16B/12B option。
9. `new EmoteSelectorController(move(options))`。真实 ctor 在返回前立即 `applySelection(index=0,duration=0,fade=0)`。
10. raw-emplace Selector entry，随后写 selector label，再发布 `{type=8,index=原始 selector metadata index}`。

| 阶段 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| metadata count | `0x66AD70` | `0x557E3E` | `0x1001AA084` | `0x1A9758` |
| selector label | `0x66B2FC..0x66B318` | `0x557EF4` | `0x1001AA138` | `0x1A97D6` |
| enabled test | `0x66B33C` | `0x557F12` | `0x1001AA154` | `0x1A9800` |
| disabled label removal | `0x66B3C8` | `0x5580F0` | `0x1001AA424` | `0x1A9AD2` |
| optionList lookup/count | `0x66B374..0x66ADD0` | `0x557F38..0x557F50` | `0x1001AA180..0x1001AA1A8` | `0x1A9830..0x1A9854` |
| option label | `0x66AEAC..0x66AEC8` | `0x557FA2` | `0x1001AA208` | `0x1A98AE` |
| first match borrow/flag=0/remove | `0x66AF60..0x66AF70` | `0x557FDE..0x557FE8` | `0x1001AA2A8..0x1001AA2B8` | `0x1A9954..0x1A9962` |
| off/on value reads | `0x66AF9C` / `0x66AFBC` | `0x557FEC` / `0x558022` | `0x1001AA2BC` / `0x1001AA2FC` | `0x1A9966` / `0x1A99BA` |
| Selector allocate/ctor | `0x66B0D4..0x66B0E8` | `0x558060..0x558066` | `0x1001AA378..0x1001AA384` | `0x1A9A22..0x1A9A2C` |
| raw entry append | `0x66B0F0..0x66B1C4` | `0x558070` | `0x1001AA394` | `0x1A9A3C` |
| entry label write | `0x66B1FC` | `0x5580BA` | `0x1001AA3F0` | `0x1A9AA6` |
| type-8 map publication | `0x66B200..0x66B210` | `0x5580BE..0x5580CC` | inline；new-node path `0x1001AA4C4..0x1001AA4F0` | inline；new-node path `0x1A9B64..0x1A9BA0` |

## 4. 宽字符串恢复

反编译器在若干 call site 把 UTF-16LE 数据重叠识别成 ASCII `"o"`。对完整宽字节序列搜索后，每个属性在每份参考中都只有一个命中：

| 属性 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| `optionList` | `0x14D39B0` | `0xD8444A` | `0x10195FDC6` | `0x175212A` |
| `offValue` | `0x14D39C6` | `0xD84460` | `0x10195FDDC` | `0x1752140` |
| `onValue` | `0x14D39D8` | `0x558268` | `0x10195FDEE` | `0x1752152` |

因此这些一字符显示都不是额外的短属性分支。

## 5. Selector entry：未写 gate 与容器实现

Selector deque element 的 source shape 是：

```cpp
struct SelectorEntry {
    unique_ptr<EmoteSelectorController> controller;
    ttstr label;
    uint8_t directEnqueueGate; // raw builder 不写
    // ABI padding
    vector<TransitionEntry *> targets; // 三个零 pointer
};
```

raw-emplace 写 owner、空 label、空 vector 三指针，但四份实现都没有写 gate byte 或 padding：

| ABI | raw-emplace | entry | elements/block | element bytes/block |
|---|---:|---:|---:|---:|
| Android A64 libstdc++ | builder 内联 `0x66B0F0..0x66B1C4` | 48B | 10 | `0x1E0` |
| Android A32 libstdc++ | `0x55841C` / boundary `0x567B0C` | 24B | 21 | `0x1F8` |
| iOS A64 libc++ | `0x1001AA9D8` (`0xB4`) | 48B | 85 | 4080B |
| iOS A32 libc++ | `0x1AA0C8` (`0x8E`) | 24B | 170 | 4080B |

完整 metadata dispatcher 在所有 builder 完成后调用 `syncSelectorControls_guess`，届时才用 `_selectorEnabled` 初始化这个 gate。因此 raw builder 到 sync 之间的字节确实未定义；它不是 false/true 的隐藏默认值。type-8 direct setter 在后续路径会检查 gate。

## 6. 借用关系、构造期副作用与生命周期

- Selector option 只保存裸 `EmoteVarController *`，不 delete transition controller。
- 多个 option 可以借用同一 transition。由于每个 option 都重新从 deque begin 扫描，同 label 的多个 transition entry 中永远只借第一个；后续同名 owner 的 flag 保持 1。
- Selector ctor 立即按 option vector 顺序应用 selection 0。option 0 使用 `onValue`，其余 option 使用 `offValue`。
- 如果后面的 option 又借到 option 0 的同一 controller，则后面的 `offValue` 会在同一次构造中覆盖前面的 `onValue`。原实现没有去重。
- 未匹配 option 保留 null；`applySelection` 跳过它。
- 正常对象析构按字段声明逆序先销毁 selector deque #9、后销毁 transition deque #8，borrowed controller 生命周期覆盖 selector 生命周期。
- metadata reset 则按原生 declaration-order clear：先清 #8 再清 #9。此时 selector options 会短暂悬空，但 Selector 析构只销毁 raw-pointer vector、不解引用 target，因而仍符合四参考行为。
- Selector deque growth 在 destination 构造前失败时也保留 raw controller leak；append 成功后的 label/map failure 不回滚 entry。

## 7. 稀疏 index 的未检查边界

Transition 和 Selector map ref 都保存原始 metadata index，而 deque 只包含 enabled elements。direct setter 的 type 7/type 8 分支直接执行 `_auxVarDeque8[ref.index]` / `_vectorVarDeque9[ref.index]`，没有从 metadata index 到 compacted index 的转换，也没有 bounds check。

因此 disabled hole 位于 enabled element 之前时，后续用该 label 走 controller-ref setter 可以错取或越界。这是四参考共同保留的输入约束/UB 边界，不能“修正”为 deque index 而偏离原实现。

## 8. 源码、测试与 IDB 更新

源码：

- 两个 builder 的 `v5/v6/v13/elem/opt/ctl/ref` 等占位名已迁移为 metadata/option/controller 语义名。
- 注释明确 first-match 从头重扫、borrow 不转移 ownership、真实 ctor 立即 apply index 0、两类 gate 的初始化差异。
- Transition/Selector entry 分别增加 `3 * pointer-width` / `6 * pointer-width` 布局断言。
- header 明确 sparse ref 被 downstream 直接索引的 native unchecked boundary。

测试：

- Transition disabled hole、重复/空 label、所有 enabled owner、flag=1、type-7 ref 后写覆盖。
- Selector disabled label removal、first duplicate transition borrowing、missing option null、type-8 sparse ref。
- 两个相同 option label 借同一 transition，后 option 在 ctor 中覆盖前 option 初值。
- placement-new 到 `0xA5` storage，验证 Selector entry constructor 初始化 owner/label/targets 但 gate byte保持 `0xA5`。

四份 IDB 已添加 builder/raw-emplace 注释与书签；Transition locals 迁移为 `metadataCount/metadataIndex/controller/labelSlot/controllerRef`，Selector 迁移核心 `metadataCount/metadataIndex/optionCount/optionIndex/selectorController`，可辨认 ABI 中还命名了 `transitionController`。

当前验证：定向 `git diff --check`、Emscripten 单元测试 TU 语法检查和最终完整 `Web Debug Build` 编译/链接均通过。构建只报告仓库已有的 `_tss`、imagepacker `nodiscard` 与 Emscripten pthread/JSPI 类警告。

## 9. 2026-08-16 Transition accessor/source owner 补完

本页既有 Transition raw controller ownership、flag=1、sparse type-7 publication，以及
Transition→Selector borrow 结论保持不变。新的四端 fresh source-identity 复核进一步证明
Transition builder 由 copied control Variant 构造 root `ncbPropAccessor`；每轮保留 indexed
getter 返回的 source element Variant，并用第二份 Variant copy 构造 element accessor。
`enabled`/`label` 由 element accessor 读取并复用 Eye/Eyebrow/Mouth 的共享 hints；公共尾部严格
accessor→source，循环尾再释放 root。完整地址、ignored-HRESULT 边界、probe 和 IDB 写回见
`analysis/motionplayer_transition_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

## 10. 2026-08-16 Selector nested accessor/source owner 补完

Selector 也已完成独立四端 source-identity 复核。其 outer selector element保留 indexed source
Variant并用第二份 copy构造 element accessor；selector label先于 enabled读取。`optionList`
named Variant 与逐项 option indexed Variant则直接由 temporary构造 nested accessor，不另留长期
source owner。option tail为 label→accessor；enabled selector tail为 optionList accessor→moved-
from vector→selector label→element accessor→outer source，最后释放 root。label/enabled复用共享
hints，optionList/offValue/onValue有三个 Selector-only hints，off/on是真正的 typed float
`GetValue`。完整证据见
`analysis/motionplayer_selector_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。
