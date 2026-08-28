# Player initVariables 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制共同证明，`Player::initVariables` 是普通 motion initializer 在参数表与节点树
之后立即执行的 eager variable-track builder。它把 motion `variable` list 物理顺序转换成
`deque<VariableLabelScope>`：一个 source item对应一个持久元素，不排序、不去重、不构建额外
label map。

函数一进入就 clear旧 deque；随后用一个 full-expression temporary accessor从当前
`_motionContentVariant`读取 `variable`。只有 exact Void直接返回。每个 list item先完成 numeric
lookup与Object accessor构造，再 append一个零初始化 deque元素；任何 named-property getter或
后续拼接抛出时，这个 partial元素保留。

本地 `PlayerMotionLoad.cpp:35` 和 `internal/value_structs.h:69` 已匹配四端共同源码结构、
owner前沿、字段顺序与边界，本轮无需修改运行时 C++。

## 2. 四端函数映射

| 平台 | 函数 | 完整指令 | 唯一 caller |
|---|---:|---:|---:|
| Android arm64 | `0x6CAB30` | 430 | `Player_initNonEmoteMotion@0x6B0A3C` |
| Android armv7 | `0x592944` | 215 | `Player_initNonEmoteMotion@0x580C28` |
| iOS arm64 | `0x10011D540` | 196 | `Player_initNonEmoteMotion@0x100108258` |
| iOS armv7 | `0x11BF04` | 275 | `Player_initNonEmoteMotion@0x1058F8` |

四个函数均 fresh decompile，并完整读取 430/215/196/275 条 disassembly；所有 cursor
`done=true`。每端都只有 ordinary initializer 一个 code xref，不存在 lazy getter、frame首次
使用或Emote wrapper的第二入口。

本轮又以 UTF-16LE+terminator raw pattern 搜索 `variable`、`label`、`scope`、`::`，四库所有
分页 cursor都完成：

| 平台 | `variable` | `scope` | build使用的 `::` |
|---|---:|---:|---:|
| Android arm64 | `0x14D61DA` | `0x14D61EC` | `0x14D619E` |
| Android armv7 | `0xD85B78` | `0x592C68` | `0xD85B3C` |
| iOS arm64 | `0x10195C7F2` | `0x10195C804` | `0x10195C78A` |
| iOS armv7 | `0x174EB56` | `0x174EB68` | `0x174EAEE` |

反编译器把 `variable`显示成 `"v"`、把 `"::"`显示成 `":"`；原始字节证明本地完整名称正确。

## 3. 四端共同源码伪代码

```text
initVariables():
    variableTracks.clear()

    variableList =
        owningAccessor(copy(motionContentVariant)).GetValue("variable")
    // temporary motion accessor is destroyed here

    if variableList.Type == Void:
        return

    list = owningAccessor(copy(variableList))
    count = list.Count()                         // exactly once

    for index in [0, count):
        item = owningAccessor(list[index])       // before append

        variableTracks.emplace_back()            // partial publication
        entry = variableTracks.back()

        entry.cascadeKey = item.GetValue("label", as ttstr)
        entry.value = +0.0
        entry.slot[0].typeZeroFlag = true
        entry.slot[1].typeZeroFlag = true
        entry.activeSlotCursor = 0

        entry.frameSource = item.GetValue("label", as Variant)

        scopeVariant = item.GetValue("scope", as Variant)
        scope = ttstr(scopeVariant)               // unconditional conversion
        if !scope.IsEmpty():
            entry.cascadeKey = scope + "::" + entry.cascadeKey
```

## 4. entry前的 clear和motion owner边界

deque clear是函数第一项动作，早于 motion-content Object转换与 `variable` getter。由此：

- `_motionContentVariant`非 Object而转换抛出时，旧 variable entries已经全部销毁；
- `variable` getter抛出时，旧 entries也不会恢复；
- clear销毁每个元素的 slot1 easing、slot0 easing、frameSource、cascadeKey，但保留标准库允许
  保留的 deque block/map storage，最终 Player member destructor再释放底层storage。

函数使用 `ncbPropAccessor(tTJSVariant(_motionContentVariant))` full-expression temporary取得
`variableList`。temporary accessor/dispatch在 getter返回后、Void gate之前释放；`variableList`
自己的 Variant owner则一直活到函数尾。因此 getter可清 Player canonical owner而不破坏返回 list，
但不能依赖motion accessor在后续Count/item阶段仍存活。

## 5. exact Void gate和list owner

只有 `variableList.Type()==Void`返回。空 Array是Object，仍建立list accessor并读取Count 0；
String/Integer/Real等非Void primitive进入strict Object转换边界，不被当成空 list。

list accessor独立retain返回 Array dispatch，贯穿Count和全部 numeric getter。Count只读一次；
callback改变Array长度不会改变loop upper bound。numeric index getter先产生Variant，再构造item
Object accessor：

- numeric getter失败/抛出时，不append元素；
- numeric result不是Object、AsObject抛出时，也不append元素；
- item owner构造成功后才append，因此所有 named getter期间 receiver稳定。

## 6. append-first partial element

每项 append调用 native deque value constructor，完整零初始化一个
`VariableLabelScope`，然后才读取第一个 named property。append分配失败时没有新元素；append
成功后任何异常都保留该元素。

四端共同的 source-level element顺序：

```cpp
struct VariableLabelScope {
    ttstr cascadeKey;
    int activeSlotCursor;
    double value;
    tTJSVariant frameSource;
    VarTrackSlot slot[2];
};
```

`VarTrackSlot`共同顺序是 frame index、time、interval、type-zero/interp/merged bytes、value、
easing Variant。initVariables只显式修改 cascadeKey、cursor、outer value、frameSource和两个
typeZeroFlag；其他slot字段保持value-constructor的零/Void状态。

native element size为：

| 平台 | element stride | 可见deque block形状 |
|---|---:|---:|
| Android arm64 | 160 | 480-byte block，3 elements |
| Android armv7 | 128 | 512-byte block，4 elements |
| iOS arm64 | 160 | libc++ block arithmetic |
| iOS armv7 | 128 | libc++ block arithmetic |

这些是ABI坐标，不写入portable struct padding。

## 7. 两次独立 `label` 读取

第一项 named getter把 `label`直接转换为ttstr并CopyAssign到 `cascadeKey`。只有它成功后，四端
才依次提交：

```text
value = +0.0
slot0.typeZeroFlag = true
slot1.typeZeroFlag = true
activeSlotCursor = 0
```

随后第二次独立读取同一 `label`，这次保留原始 Variant到 `frameSource`。两次getter不能合并：

- side-effecting getter可让 cascadeKey与frameSource内容不同；
- 第一次getter抛出：append的全零/Void元素保留；
- 第二次getter抛出：cascadeKey与四项scalar/flag提交保留，frameSource仍Void；
- 第二次返回的Variant可以是任意类型，后续cursor路径再按Array/frame source语义访问。

本地已经保留两次调用和中间store顺序。

## 8. scope无条件转换和拼接边界

第三次 named getter先取得 `scope` Variant，再无条件构造ttstr。不能先检查Variant Type，也不能
只接受String：Integer/Real/Object等按TJS conversion规则转换或抛出；Void/空String规范化成
null-backed empty ttstr。

只在转换后的scope非空时覆盖cascadeKey：

```text
cascadeKey = (scope + "::") + oldCascadeKey
```

边界：

- empty scope保持第一项label getter的原key；
- nonempty scope + empty label产生带尾分隔符的 `scope::`；
- scope getter或转换抛出时，frameSource已经提交；
- 第一次concat分配抛出时cascadeKey仍旧label；最终assign抛出时按ttstr CopyAssign自身前沿；
- 不规范化已有 `/`、`::` 或重复分隔符。

TJS string allocator把length 0规范化为null backing，而 `ttstr::IsEmpty()`本身也检查null，故本地
content-level empty判断与四端反编译中直接测试scope backing pointer一致。

## 9. owner与正常/异常析构顺序

每次loop body在named getters期间同时持有：outer variableList Variant、list accessor、numeric
item result、item accessor、各getter temporary Variant、scope ttstr owner。persistent deque entry
另外独占cascadeKey、frameSource和两个easing Variants。

正常一项结束时，scope owner先释放，随后item accessor/dispatch释放；persistent entry不动。
异常时只析构截至抛点已构造的locals，不pop deque元素。outer list owners在整个loop unwind后释放。

下一次ordinary motion初始化会首先clear整个deque；Player析构也在parameter vector clear之后、
old node reset之前显式clear该deque。元素不持有MotionNode pointer或parameter pointer，因此它与
node tree之间只有调用顺序依赖，没有直接所有权边。

## 10. 与后续消费者的数据流

构建结果被以下路径消费：

- `variableKeys`：按deque物理顺序读取每个元素首字段cascadeKey，保留duplicate/empty key；
- variable cursor step/merge：使用frameSource读取frame项，写两个slot和active cursor；
- frameProgress：插值active slot并写outer `value`；
- Join reset：对非type-zero active slot执行 `HM4[cascadeKey]=value`；
- parameter/cascade binder与child update：按scope-normalized key关联值。

因此把容器换成map、按key去重、只存一次label、延迟构建或跳过空key都会改变原生数据流。

## 11. 平台差异

Android arm64/libstdc++在block末尾显式分配480-byte block并以160-byte stride append；Android
armv7使用512-byte block与128-byte stride。iOS两端是libc++ deque map/block实现。四端
owner和field store顺序一致。

iOS armv7额外指令主要来自SjLj unwind registration和32-bit ttstr AddRef/Release；Android
arm64额外展开了deque grow与ttstr concat。没有平台专属属性、scope规则或类型gate。

## 12. 本地对照与验证

- `cpp/plugins/motionplayer/PlayerMotionLoad.cpp:35`：clear、full-expression motion accessor、
  exact Void、Count snapshot、item-owner-before-append、label双读和scope拼接完全一致；
- `cpp/plugins/motionplayer/internal/value_structs.h:69`：VarTrackSlot字段/owner顺序一致；
- `cpp/plugins/motionplayer/internal/value_structs.h:90`：VariableLabelScope字段/逆序析构一致；
- `cpp/plugins/motionplayer/internal/player_containers.h:66`：保留deque而非map/vector替代；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:21757`：现有re-entrant owner、getter顺序、empty
  scope和partial publication测试覆盖本slice关键边界；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:12700`：consumer variableKeys保留fresh Array、
  physical order、duplicates和empty key。

本轮没有运行时C++修改。四库已写入 `Player_initVariables_guess`、函数注释和bookmark并保存。
coverage列检查、`git diff --check`和Python ledger脚本语法检查会与本轮其他slice统一执行。当前
环境仍无CMake/Emscripten正式工具链，不能声称unit/Web build通过。

ordinary initializer的parameter -> buildNodeTree -> initVariables连续三段现在都已闭合。下一步
回到完整root-reachable ledger，优先处理这些结构的首批update消费者以及仍编译存在的诊断
`node.index`/`parameterizeIndex`残留。
