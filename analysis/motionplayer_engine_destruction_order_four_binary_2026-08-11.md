# EmoteEngine 完整析构阶段与容器释放顺序（四参考二进制，2026-08-11）

## 1. 范围

本文闭合
`analysis/motionplayer_lifecycle_four_binary_2026-08-11.md` 中此前保留的
“trailing typed STL 成员依赖本地自动析构”精确性缺口。四份当前参考二进制的
正常析构入口为：

| 目标 | `EmoteEngine` 正常析构 |
| --- | ---: |
| Android arm64 | `0x67C898` |
| Android armv7 | `0x5610E8` |
| iOS arm64 | `0x1001B8B4C` |
| iOS armv7 | `0x1B814E` |

本轮重新反编译的是以上四个正常析构体，不是 iOS 的 ctor-unwind landing pad。
Android 使用 libstdc++ 容器布局，iOS 使用 libc++；析构 helper 的展开程度和字段
偏移不同，但高层逆声明顺序完全一致。

## 2. 后半段字段映射

下表把影响析构次序的字段映射到四个 ABI；`V0/V1/V2` 分别是
`variableLabelsBase/variableLabels/variableFrameLists`，表内按声明正序列出：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Player owner | `+1064` | `+532` | `+696` | `+348` |
| controller owners | `+1072..+1120` | `+536..+560` | `+704..+752` | `+352..+376` |
| wind owner | `+1128` | `+564` | `+760` | `+380` |
| V0/V1/V2 | `+1208/+1228/+1248` | `+640/+652/+664` | `+840/+860/+880` | `+452/+464/+476` |
| HM4 instant set | `+1272` | `+676` | `+904` | `+488` |
| HM5 variable ranges | `+1328` | `+704` | `+944` | `+508` |
| HM6 variable refs | `+1384` | `+732` | `+984` | `+528` |
| HM7 label values | `+1440` | `+760` | `+1024` | `+548` |

Player 之前的较早字段也按四端共同顺序映射为：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| mirror-pattern vector | `+800` | `+400` | `+480` | `+240` |
| mirror match set | `+824` | `+412` | `+504` | `+252` |
| mirror miss set | `+880` | `+440` | `+544` | `+272` |
| HM3 timeline states | `+936` | `+468` | `+584` | `+292` |
| main/diff/active vectors | `+992/+1016/+1040` | `+496/+508/+520` | `+624/+648/+672` | `+312/+324/+336` |

这组偏移同时说明析构体并没有一块不可解释的“尾部 owner”：本地
`EmoteEngine.h` 中 HM1–HM7、三个 Variant、三个 timeline vector 和十组 deque
已经足以逐字段解释完整正常析构。

## 3. 四端共同析构伪代码

四份正常析构体可归一为：

```text
delete raw wind owner (the dying Engine slot is not cleared)

destroy HM7 label-value map
destroy HM6 variable-ref map
destroy HM5 variable-range map
destroy HM4 instant-variable set
destroy variableFrameLists Variant
destroy variableLabels Variant
destroy variableLabelsBase Variant

destroy direct controller owners in reverse construction order:
  parts outer-force
  hair outer-force
  bust outer-force
  angle
  color
  scale
  position

destroy owned Player

destroy active timeline labels
destroy diff timeline labels
destroy main timeline labels
destroy HM3 timeline-state map
destroy mirror miss set
destroy mirror match set
destroy mirror-pattern vector

destroy deque #10 loop
destroy deque #9 selector
destroy deque #8 transition
destroy deque #7 clamp
destroy deque #6 mouth
destroy deque #5 eyebrow
destroy deque #4 eye
destroy deque #3 bust-chain-2
destroy deque #2 bust-chain-1
destroy deque #1 hair/parts spring
```

Android arm64 将 HM7/HM6/HM4/HM3/mirror set/vector 的析构大段内联；其余三端
更多保留小 helper。是否内联不改变阶段。2026-08-21 的 V264 逐指令复核进一步确认
owner slot 的 concrete write timing 是稳定的平台差异：Android 两端都是 pointee
destructor/operator-delete 后才写 null，其中 arm64 还把“读取下一 owner”排在“清零上一
slot”之前；iOS 两端则先写 null，再调用 pointee destructor/operator-delete。它们仍归一到
同一个 single-owner reset 源码表达式，controller/Player 的实际销毁次序四端一致；不得再把
Android arm64 的流水化 store 笼统描述成四端都会发生的移动。完整更正见
`motionplayer_engine_normal_destruction_owner_slot_write_order_four_binary_2026-08-21.md`。

wind 不属于下一段所述的 `unique_ptr` owner：四端 `setWind` replacement 都先 delete
旧 emitter、保持 member slot 不变，再尝试新分配，成功初始化后才覆盖 field。详见
`motionplayer_wind_raw_owner_replacement_four_binary_2026-08-13.md`。

## 4. 可识别 helper

### 4.1 map/set helper

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| HM7 dtor | inline | `0x564B18` | `0x100129F8C` | `0x128DB0` |
| HM6 dtor | inline | `0x564B78` | `0x1001B8790` | `0x1B7F48` |
| HM5 dtor | `0x682B5C` | `0x564BD8` | `0x1001B8804` | `0x1B7F84` |
| ttstr set dtor（HM4/HM2/HM1 共用 specialization） | inline | `0x563BE4` | `0x1001B7F3C` | `0x1B774C` |
| HM3 dtor | inline | `0x564C38` | `0x1001B888C` | `0x1B7FC8` |

32 位 Android 还保留了 pointer-sized owner helper：
`0x56351C` 销毁一个 `EmoteVarController` owner slot，`0x563C5E` 销毁 Player
owner slot。它们都接收 slot 地址、取出 owned pointer，若非空则先执行析构/delete，最后才
清零 slot；
不是非拥有裸指针的普通清零 helper。2026-08-13 联合 ctor-unwind 重新复核后，进一步确认
这些正是单指针 `std::unique_ptr<T>` specialization；Angle 对应 helper 为 `0x563C44`。

### 4.2 deque helper

| deque | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| #10 loop | `0x681700` | `0x563C78` | `0x1001B88C4` | `0x1B7FE4` |
| #9 selector | `0x6817C4` | `0x563CF4` | `0x1001B890C` | `0x1B800C` |
| #8 transition | `0x6818D4` | `0x563D70` | `0x1001B8954` | `0x1B8034` |
| #7 clamp | `0x681998` | `0x563DEC` | `0x1001B899C` | `0x1B805C` |
| #6 mouth | `0x681A5C` | `0x563E68` | `0x1001B89E4` | `0x1B8084` |
| #5 eyebrow | `0x681B20` | `0x563EE4` | `0x1001B8A2C` | `0x1B80AC` |
| #4 eye | `0x681BE4` | `0x563F60` | `0x1001B8A74` | `0x1B80D4` |
| #3/#2 bust chain（同一 specialization） | `0x681CA8` | `0x563FDC` | `0x1001B8ABC` | `0x1B80FC` |
| #1 hair/parts | `0x681D6C` | `0x564058` | `0x1001B8B04` | `0x1B8124` |

每个 owning deque helper 都先销毁 element payload，再释放 deque block/map。
#9 的 option 中保存的是对 #8 transition controller 的借用；原版逆序保证 #9
先析构，#8 owner 后析构。#3/#2 共享 element specialization，因此同一个 helper
被连续调用两次不是重复释放。

## 5. 原本地偏差

修改前的 portable 析构存在四类相对顺序偏差：

1. HM7..HM4、三个 Variant、三个 timeline vector、HM3/HM2/HM1 和 pattern
   vector 只依赖函数体退出后的自动析构，实际释放时点晚于 direct controller、
   Player 和手工清理的所有 deque；
2. deque #4..#10 和 spring deque #1..#3 在 direct controller/Player 之前就被
   清理，而四端都把十组 deque 放在 Player 之后；
3. transition deque #8 先于借用它的 selector deque #9 销毁，和原版逆序相反；
4. 多数 deque 只调用 `clear()`；元素虽然立即析构，deque 的 block/map allocation
   通常仍保留到自动析构 epilogue，仍不能复现 native helper 在该阶段释放 backing
   storage 的行为。

## 6. 本地恢复方式

`EmoteEngine::~EmoteEngine()` 现在严格按第 3 节排序。2026-08-13 的 ctor-unwind 专题已纠正
direct Player/controller 字段类型：它们现在是与参考相同的单指针 `unique_ptr` owner，正常
析构在精确阶段调用 `reset()`，构造失败则由 member unwinding 自动清理已初始化前缀。
deque element 中的 owning pointer 仍需逐 builder 验证其临时对象/push 失败边界，因此不能
整体批量迁移。2026-08-13 已首先闭合 deque #4 Eye：其 element 是
`{unique_ptr<EmoteBlinkController>, ttstr}`，builder 以 raw pointer 直接构造目标 owner，
controller ctor 失败会释放 pending allocation，而 deque growth 失败保留原版泄漏边界。
deque #5 Eyebrow 随后也已用自身独立路径闭合为同一 wrapper 所有权模式，而不是由 #4
类推。deque #6 Mouth 又以独立四端路径确认 `{unique_ptr, label, talkLabel}`、raw emplace
和 `talkLabel -> label -> controller` 析构。因此 #4/#5/#6 的显式 delete 循环均已删除。
deque #7 Clamp 也已独立闭合：它没有 owning controller，element 是自然布局的
`{type,min,max,varLr,varUd}`，默认追加整记录清零，析构只执行
`varUd -> varLr`。deque #8 Transition 现也已用自身四端路径闭合为
`{unique_ptr<EmoteVarController>, label, flag}`：raw pointer 直接 emplace，析构执行
`label -> controller`，controller ctor 失败释放而 deque growth 失败泄漏；完整证据见
`analysis/motionplayer_transition_entry_owner_emplace_four_binary_2026-08-13.md`。deque #9
Selector 也已独立闭合为
`{unique_ptr<EmoteSelectorController>, label, uninitialized gate, targets}`：entry 逆析构为
`targets -> label -> controller`，controller 逆析构为 `option vector -> command deque`；
真实 constructor unwind、new-expression allocation cleanup、raw emplace 与 grow-failure
泄漏见
`analysis/motionplayer_selector_entry_owner_ctor_emplace_four_binary_2026-08-13.md`。#10 Loop
也已闭合为 `{unique_ptr<EmoteLoopController>, label}`：builder 用 raw pointer 直接
emplace，入队前异常与 grow failure 泄漏，成功后逆成员析构为
`label -> controller.keys -> controller`；见
`analysis/motionplayer_loop_control_four_binary_2026-08-12.md`。deque #1 simple spring
也已独立闭合为
`{unique_ptr<EmoteSpringState>, initFlag, shapeLabel, keyX, keyY, anchorX, anchorY}`：
真实参数 constructor 抛出时由 new-expression 回收 allocation，构造完成后的
property/grow failure 则保留 raw-pointer 泄漏；成功 emplace 后 entry 逆成员析构执行
`keyY -> keyX -> shapeLabel -> spring`。证据见
`analysis/motionplayer_hair_parts_entry_owner_ctor_emplace_four_binary_2026-08-13.md`。为使 typed container
deque #2/#3 chain spring 也已用自身 builder/constructor/emplace/clear 路径独立闭合为共同
element specialization：`{unique_ptr<EmoteBustChainSpring>, uninitialized initFlag,
shapeLabel, keyA, keyB, keyC, anchorX, anchorY}`；entry 逆析构为
`keyC -> keyB -> keyA -> shapeLabel -> spring`，constructor 后到 raw emplace 成功前继续
保留原版泄漏窗口。证据见
`analysis/motionplayer_chain_entry_owner_ctor_emplace_four_binary_2026-08-13.md`。为使 typed container
在指定阶段同时释放元素和 backing storage，文件内仍使用 helper：

```cpp
template <typename Container>
void releaseContainerStorageAtNativePhase(Container &container) {
    Container empty;
    container.swap(empty);
}
```

原 allocation 被交换进临时空容器并在该语句末尾析构；Engine 成员本身留下合法
空状态，所以函数体退出后的自动成员析构不会重复释放。Variant 则按逆序调用
`Clear()`，立即释放其 dispatch owner 并留下 void Variant。

这不是声称原版存在同名 helper；它是 portable C++ 在尚未全部恢复 deque element owning
wrapper 前复现 native 资源释放阶段的过渡实现。direct Player/controller 不再属于这个偏差。

## 7. 边界与可观察后果

- 三个 Variant 可能共享同一个 TJS Array dispatch；按
  `frameLists -> public labels -> base labels` 逆序 Release 会保留原版引用计数变化
  次序。
- HM7/HM6/HM5/HM4 的 key/value 析构现在早于 controller 和 Player；ttstr/TJS
  owner 的最后一次 Release 不再被推迟到所有运行时对象之后。
- Player 析构仍早于 HM3 timeline data/controller、mirror caches 和十组 metadata
  controller deque，和四端一致。
- wind 仍最先释放；#3/#2 chain spring 的 `collisionCurve` 在其后形成短暂悬空
  borrow，但 spring 析构不解引用它。
- selector option 的 transition pointer 在 selector 析构期间仍有效；没有引入
  shared ownership、null guard 或回删逻辑。
- metadata reset 不走上述逆析构顺序，而是先 clear #8、再 clear #9；该短暂 dangling
  borrow 窗口也由四端重新确认，且 #9 clear/destructor 不解引用 borrowed pointer。

## 8. IDB 与验证

四份 IDB 已给可识别的 map/set、owner-slot 和 deque destructor helper 写入
`*_dtor_guess` / `*_ownerPtrDestroy_guess` 名称，并在四个正常 Engine 析构入口追加
完整阶段注释。名称中的 `_guess` 表示精确原始源码标识符未知；容器角色和调用顺序
由字段偏移、连续调用点与 element destruction body 共同确定。

验证结果：

- `cmake --build --preset "Web Debug Build" --target motionplayer`：通过；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target motionplayer`：通过；
- 完整 `cmake --build --preset "Web Debug Build"`：最终 `index.html/index.wasm`
  链接通过；
- `cmake --build --preset "Wasmtime Headless Debug Build" --target
  krkr2_wasmtime_guest`：33 个受影响 guest object 重新编译，wasm 链接和 exnref
  转换通过；
- 复用 Web Debug `compile_commands.json` 的真实 Emscripten 参数，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；
- warning 仅为仓库既有的 `_tss`、imagepacker `nodiscard` 和
  pthread/memory-growth 提示；
- 当前 `out/windows/debug` 没有 `build.ninja`，因此没有把编译级验证表述为原生
  Catch2 运行通过；
- `git diff --check`：通过；
- 四份 IDB 已原位保存。
