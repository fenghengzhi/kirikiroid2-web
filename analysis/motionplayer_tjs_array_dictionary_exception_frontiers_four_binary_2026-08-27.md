# TJS Array/Dictionary 容器与六组 Player getter 异常前沿（2026-08-27）

## 1. 结论

本轮闭合此前共用同一底层容器问题的 6 个 coverage row：

- `MP-C10-TJS-ARRAY-ITEMS`；
- `MP-A11-PLAYER-CAMERA-VECTORS`；
- `MP-A11-PLAYER-BOUNDS`；
- `MP-C11-PLAYER-VARIABLE-KEYS`；
- `MP-A11-PLAYER-CAMERA-OFFSETS`；
- `MP-C11-PLAYER-MODROOT-LAYERNAMES`。

四端共同源形状已经确定：TJS Array 的 native `Items` 是
`std::deque<tTJSVariant>`；Player getter 先取得拥有 Array/Dictionary 的局部 Variant 或
accessor，在 native 容器中逐项追加，最后才复制对象闭包到脚本返回槽。任何 append 或
`SetValue` 失败都不会返回半成品对象。

精确 EH codegen 并不四端对称：

- Android arm64：六组 getter 均存在本帧 landing，清理局部 Array Variant 或 fresh
  Dictionary 后 `_Unwind_Resume`；
- iOS arm64：Mach-O LSDA 指向紧邻主 body、普通 code xref 为 0 的独立 cold cleanup；
  六组 getter 和 deque add-back-capacity 都有精确 local-owner/staging 清理；
- iOS armv7：六组 getter 均通过独立 SjLj cleanup 完成同类清理；libc++ deque 的
  add-back-capacity 还有独立 temporary-map/new-block cleanup；
- Android armv7：完整函数流及相邻 function catalog 没有相应本帧 local cleanup；正常
  owner/发布路径一致，但异常穿越该帧时没有可观察的主动清理；
- Android libstdc++ 在 map 扩容成功、后续 block 分配失败时，可以保留 map 容量/居中
  位置的内部变化，但逻辑 size 与元素序列不变；
- iOS libc++ 的 map-growth path 先在临时 split-buffer 中完成 map、新 block 和旧 block
  指针搬运，最后才 swap 到 live deque。iOS armv7 的 cleanup 明确证明失败前不发布 live
  map，并释放已构造的临时资源。

portable 源无需新增平台分支或显式 `try/catch`。现有自动 Variant/accessor、
`std::deque<tTJSVariant>` 和末尾 return 的源结构就是四端共同来源；STL 布局、block
粒度与 landing 生成属于目标 ABI/STL/异常配置差异。6 个 row 均可升级为
`IMPLEMENTED`。

## 2. fresh 审计分母

### 2.1 deque append/reserve helper

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Real append/grow | `0x6DFC90`；47 条 | `0x5A099C`；40 条 | `0x1000FAED8`；47 条 | `0xF7F90`；54 条 |
| float append/grow | `0x684F98`；50 条 | entry `0x5545C8`；29 条；slow `0x5668FC`；41 条 | `0x1001210EC`；50 条 | `0x11FEE4`；55 条 |
| integer append/grow | caller inline/common grow | `0x5A0A2C`；30 条 | `0x100122C08`；43 条 | `0x121C1E`；52 条 |
| map/add-back-capacity | `0x53453C`；253 条 | `0x497D9C`；82 条 | `0x100127550`；193 条 | `0x126B28`；252 条 |
| 独立 unwind cleanup | 主 caller landing | 无 | `0x100127854`；20 条 LSDA cold | `0x126DB0`；41 条 SjLj |

上述 helper 均重新读取完整 decompile 与 disassembly，所有 cursor 为 `done=true`。Android
两端的 map helper 已标为 `TJSVariantDeque_reallocate_map_guess`；iOS 两端主 helper 已标为
`TJSVariantDeque_add_back_capacity_guess`。

### 2.2 六组 Player getter

| getter | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| cameraTarget | `0x6C9AF4`；88 | `0x592100`；36 | `0x10011CA20`；26 | `0x11B2E8`；64 |
| cameraPosition | `0x6C9C54`；88 | `0x592174`；36 | `0x10011CA9C`；26 | `0x11B3C0`；64 |
| bounds | `0x6C9E64`；234 | `0x59226C`；188 | `0x10011CBD4`；141 | `0x11B53C`；263 |
| variableKeys | `0x6CE77C`；99 | `0x5948D0`；43 | `0x1001200B4`；61 | `0x11EDC0`；84 |
| getCameraOffset | shared tail `0x6CDE90`；79 | `0x59441C`；68 | `0x10011F6EC`；50 | `0x11E220`；105 |
| getLayerNames | `0x6CE4C0`；143 | `0x594798`；54 | `0x10011FE88`；61 | `0x11EB7C`；100 |

这里的指令数包含函数内 normal path；Android arm64 还包含 return 后的 landing code，
iOS arm64 的 cleanup 是主 body 之后由 Mach-O LSDA 选择、没有普通 code xref 的独立 cold
function，iOS armv7 的 cleanup 则由 SjLj function context 指向。Android armv7 的完整
函数流检查到 epilogue/stack-check/最后 return，并继续检查相邻 function catalog，未发现
本帧 cleanup。

## 3. `std::deque<tTJSVariant>` 的四端物理实现

| 目标 | Variant 物理大小 | 每 block 元素数 | block 字节数 | STL 形状 |
|---|---:|---:|---:|---|
| Android arm64 | 20 | 25 | 500 | libstdc++ map + start/finish node/cur/first/last |
| Android armv7 | 12 | 42 | 504 | libstdc++ map + start/finish node/cur/first/last |
| iOS arm64 | 20 | 204 | 4080 (`0xFF0`) | libc++ pointer-map split buffer + absolute start/size |
| iOS armv7 | 12 | 341 | 4092 (`0xFFC`) | libc++ pointer-map split buffer + absolute start/size |

这些差异来自目标 `tTJSVariant` ABI 与 libstdc++/libc++ deque policy，不是 motionplayer
自己的平台条件分支。共同 C++ 声明仍是 `std::deque<tTJSVariant>`；不能为了匹配 block
字节数在 portable 源中恢复 ABI padding 或自定义 deque。

## 4. Android libstdc++ map/block 提交边界

Android 两端的 append boundary 具有相同顺序：

```text
if finish.cur has an element slot:
    construct scalar/String Variant at finish.cur
    ++finish.cur
else:
    if map has fewer than two usable pointer slots:
        reallocate/recenter map
    allocate one new element block
    publish new-block pointer in the next map slot
    construct element in the old block's final slot
    move finish iterator to the new block begin
```

map reallocator 的精确前沿为：

1. 先计算新 map 大小；越过容器上限时在任何 live 字段改变前抛 length error；
2. replacement map 的 `operator new` 也发生在 live map 改动前；分配失败保留旧 map；
3. 分配成功后 memmove block pointers，删除旧 map，再发布 replacement map、capacity 与
   recentered start/finish node；
4. 返回 append helper 后才分配新 element block。

因此存在一个容易被“强异常保证”概括抹掉的边界：map reserve/recenter 已成功提交后，
新 block 分配仍可能失败。此时 deque 的 map pointer/capacity/node 位置可以已经变化，但
没有新 block pointer 被发布、没有新元素被构造、finish 与逻辑 size 不增加；已有元素
值和顺序保持不变。

对 camera Real 与 bounds scalar 等标量，最后 placement construction 不再分配；对
variableKeys/getLayerNames 的 `ttstr` Variant，追加沿同一 block/map 路径，成功 placement
只复制引用所有权。caller 是否在随后异常传播时销毁整个 fresh Array，由第 6 节的目标
landing 差异决定。

## 5. iOS libc++ add-back-capacity 提交边界

### 5.1 共同算法

iOS 两端的 helper 先尝试无需分配地回收一个 front block 到 back：当 absolute start
足以跨过一个 block 时，start 减去 block 元素数，取出 front block pointer，必要时在现有
map 内居中，然后把该 block 放到 back。

无法回收时分两类：

```text
现有 pointer map 有可用位置：
    allocate new element block
    在现有 map 的 spare/recenter path 发布 block pointer

pointer map 必须增长：
    construct temporary split-buffer map
    allocate new element block
    push new block pointer into temporary map
    move/copy every existing block pointer into temporary map
    swap temporary/live map fields
    delete old map storage now held by temporary object
```

temporary split-buffer constructor 在 map 指针数溢出时先抛 length error；否则先分配 map
storage，再发布临时 `{begin,current,end,cap}`。构造失败时 live deque 完全未改。

### 5.2 iOS armv7 的 SjLj cleanup

`TJSVariantDeque_add_back_capacity_unwind_cleanup_guess@0x126DB0` 对 staging path 的
call-site state 做三类清理：

- temporary map 已构造、new block 尚未成功：只析构 temporary map storage；
- new block 已成功、但还未转移所有权：先 `operator delete(newBlock)`，再析构 temporary
  map storage；
- 搬运现有 block pointers 期间失败：这些 pointers 仍由 live deque 拥有，只析构
  temporary map storage，不删除已有 blocks；
- 清理后把 SjLj state 置为 `-1` 并 `__Unwind_SjLj_Resume`。

swap live/temporary 字段位于 staging 工作全部成功之后；所以这些失败路径不会发布半成品
map，也不会改变 live start/size。成功 swap 后 temporary object 持有旧 map storage，normal
path 删除该 storage。

### 5.3 iOS arm64 的 LSDA cold cleanup

`TJSVariantDeque_add_back_capacity_guess@0x100127550` 的 193 条 normal-body 指令之后紧邻
`TJSVariantDeque_add_back_capacity_unwind_cleanup_guess@0x100127854`。后者没有普通 code
xref，因为入口由 Mach-O LSDA 选择；20 条完整指令仍直接显示：

- 一个入口先删除已分配但尚未转移所有权的新 block；其它入口跳过这一步；
- 统一把 temporary split-buffer 的 end cursor 归一到已构造范围；
- temporary map storage 非 null 时删除它；
- 最后 `_Unwind_Resume`。

所以 iOS 两端都明确实现 staging cleanup 和 live-swap-before-publication 边界，只是 arm64
使用 LSDA cold functions，armv7 使用 SjLj dispatcher。普通 `xrefs_to(cold)==0` 不是 dead
code 证据，也不能据此判定无 cleanup。

## 6. Player getter 的四端 owner/EH 矩阵

| 返回容器 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| cameraTarget / cameraPosition Array | landing 析构局部 Array Variant并 resume | 无本帧 cleanup | LSDA cold 析构局部 Array Variant并 resume | SjLj cleanup 析构局部 Array Variant并 resume |
| variableKeys Array | 同上 | 无本帧 cleanup | LSDA cold cleanup | 同上 |
| getLayerNames Array | 同上 | 无本帧 cleanup | LSDA cold cleanup | 同上 |
| bounds Dictionary | 清理 active scalar temp，Release fresh Dictionary并 resume | 无本帧 cleanup | LSDA cold ordinary case Release；destructor throw terminate | 普通 call-site Release fresh Dictionary并 resume；析构再次抛出 terminate |
| getCameraOffset Dictionary | Release fresh Dictionary并 resume | 无本帧 cleanup | LSDA cold ordinary case Release；两个 terminate thunk | 普通 call-site Release fresh Dictionary并 resume；析构再次抛出 terminate |

Android arm64 Array landing 分别位于各主函数 return 后：cameraTarget、cameraPosition、
variableKeys、getLayerNames 都调用同一个 Variant destructor helper后 `_Unwind_Resume`。
bounds 的多个 landing 先按 active temporary 状态清理值 Variant，最后恢复 accessor vptr、
对非 null Dictionary 调用虚 `Release` 并 resume；cameraOffset 同样释放 fresh Dictionary。

iOS armv7 独立 cleanup 为：

- cameraTarget：`0x11B394`；12 条；
- cameraPosition：`0x11B46C`；12 条；
- bounds：`0x11B84C`；36 条；
- variableKeys：`0x11EEA6`；14 条；
- cameraOffset：`0x11E342`；26 条；
- getLayerNames：`0x11EC78`；14 条。

camera 两个 cleanup 覆盖三个 append 与末尾 result copy；variableKeys/getLayerNames cleanup
覆盖循环 append 与 result copy。它们都析构保存 Object/ObjThis 双引用的局部 Array
Variant，因此已成功追加的所有 Items 会随 Array native instance 一起释放。bounds 的
ordinary cases 0..16 和 cameraOffset 的 ordinary cases 0..3 都释放 fresh Dictionary；
对应的 destructor-throw states 进入 `clang_call_terminate`，不能伪装成继续恢复。

iOS arm64 的 LSDA-only cold cleanup 为：cameraTarget `0x10011CA88`（5 条）、
cameraPosition `0x10011CB04`（5 条）、bounds `0x10011CE58`（17 条）、variableKeys
`0x1001201B4`（6 条）、cameraOffset ordinary cleanup `0x10011F7D0`（11 条，另有两个
1 条 terminate thunk）以及 getLayerNames `0x10011FF7C`（6 条）。这些函数的普通
`xrefs_to` 均为空，但它们与主 body 精确相邻，保留对应栈寄存器/slot，并执行预期 owner
析构后 resume；这是 LSDA landing 的直接原生表面。

现在只有 Android armv7 的“无本帧 cleanup”结论。该结论来自每个完整函数流和相邻
function catalog，而不是一次字符串 grep；normal-path Variant/accessor destroy 后直接进入
epilogue/stack-check/return，没有 landing body 或独立 cleanup function。

## 7. 各 getter 的失败可观察性

### 7.1 cameraTarget / cameraPosition

两者各自创建 fresh Array，按 X、Y、Z 追加三个 Real，字段不受 cameraAlive/cameraActive
gate 影响。第 1、2 或 3 次 append 失败都发生在脚本返回槽写入前：

- Android arm64 / iOS arm64 / iOS armv7 销毁局部 Array，已经追加的前缀元素不会逃逸；
- Android armv7 没有本帧清理，局部 owner 可能保持未释放或随目标的异常/终止模式结束
  进程；但同样不会返回 partial Array。

### 7.2 variableKeys / getLayerNames

`variableKeys` 按 variable-scope deque 的物理顺序直接追加 `cascadeKey`，保留重复和空 key；
`getLayerNames` 按 node-label map 的 in-order 顺序追加 key，空 filter 发出所有 key，非空
filter 使用 UTF-16 substring 判定。二者没有中间 vector，也不调用脚本 `add`。

循环中任意 append 失败时，先前元素已经存在于 fresh Array 的 native Items，但返回槽尚未
发布该 Array。三个带 cleanup 的目标销毁整组前缀；Android armv7 保留目标 codegen 的
未清理边界。Android 的 map reserve 已成功、block allocation 随后失败时还可能保留第 4 节的
Array 内部 map-only mutation，但该对象仍不对脚本可见。

### 7.3 bounds

fresh Dictionary 在 ordered bounds 下按 `left, top, right, bottom, width, height, isValid`
依次写入；unordered bounds 只写 `isValid=false`。每次 `SetValue` 都是独立提交到尚未发布
的 Dictionary：后一次失败不会回滚该 Dictionary 内的前缀 keys，但函数也不会把它复制
到返回槽。

Android arm64 / iOS arm64 / iOS armv7 随即释放该 Dictionary，前缀 keys 一起销毁；
Android armv7 没有本帧 cleanup，可能留下 fresh Dictionary owner，但不存在脚本可见的
partial Dictionary。
min/max 字段、classifier 和 Player 本身均只读，因此失败不改变 Player 状态。

### 7.4 getCameraOffset / setCameraOffset / modifyRoot

getCameraOffset 依次发布 `x`、`y` 两个 Real（从保留的 float32 字段扩展到 TJS Real），其
Dictionary 失败语义与 bounds 相同。setCameraOffset 仅按 X/Y 次序窄化并覆盖两个 float32
字段，不访问 Dictionary；modifyRoot 则无条件把 root dirty 置 true。后二者没有新增的
container throw point，但与 getter 共用 coverage row，普通 body 早已闭合。

## 8. 四端共同伪代码与目标差异

共同源伪代码：

```text
makeArray(values):
    local arrayOwner = fresh TJS Array closure
    borrowed items = nativeInstance(arrayOwner).Items
    for value in values:
        items.emplace_back(value)
    return arrayOwner

makeDictionary(entries):
    local dictionaryOwner = fresh TJS Dictionary/accessor
    for (key, value) in entries:
        dictionaryOwner.SetValue(key, value, MEMBERENSURE, processHint)
    return closure(dictionaryOwner.dispatch, dictionaryOwner.dispatch)
```

必须保留的差异：

```text
Android libstdc++:
    map reserve 可以先于 element block allocation 提交；后一分配失败时允许 map-only change

iOS libc++:
    pointer-map growth 用 temporary split-buffer staging；成功后才 swap live map

A64 Android / A64 iOS LSDA cold / armv7 iOS SjLj getter:
    异常穿越时显式销毁 local owner

armv7 Android getter:
    完整 reference frame 无 local cleanup landing
```

这些差异不应在业务 C++ 中写成四套手工 allocator/`try` 分支；需要一比一保留的是共同
源结构和已记录的目标边界，而不是把优化器/标准库产物倒灌成 portable ABI 代码。

## 9. 本地逐行对照

- `cpp/core/tjs2/tjsArray.h:89-95`：`tTJSArrayNI::Items` 正是
  `std::deque<tTJSVariant>`；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:449-468`：fresh Array closure 的
  Object/ObjThis 双 owner、factory ref release、GETINSTANCE status==0 gate 和 borrowed Items
  pointer；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:415-430`：getLayerNames 的 map in-order、
  UTF-16 substring filter、direct native append 与末尾 return；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:736-749`：cameraTarget/cameraPosition 的
  fresh Array 与 X/Y/Z append 顺序；
- `cpp/plugins/motionplayer/PlayerCore.cpp:313-357`：bounds 的 fresh accessor、Y-before-X
  ordering、七键/单键顺序、末尾 Object/ObjThis closure；
- `cpp/plugins/motionplayer/PlayerCore.cpp:713-722`：variableKeys 的 scope-deque physical
  order 与 direct `ttstr` emplace；
- `cpp/plugins/motionplayer/PlayerCore.cpp:915-933`：getCameraOffset 的 x/y Dictionary、
  setCameraOffset float32 覆盖以及 modifyRoot unconditional dirty；
- `cpp/plugins/motionplayer/internal/player_containers.h:61-73`：variable-scope deque 与
  node-label map 的 portable owner/container 声明。

本轮不修改 C++：自动 owner、容器类型、插入顺序和返回发布点均已与联合证据一致。若加入
显式平台异常分支，反而会把 reference 的 STL/EH codegen 差异误恢复成不存在的业务源。

## 10. IDB 改进与 disposition

本轮在四个 IDB 中补充/修正了：

- Android 两端 deque map reserve 的提交顺序与 block-allocation-after-map 边界注释；
- iOS 两端 add-back-capacity、split-buffer constructor 及五个 lower map helper命名；
- iOS arm64 LSDA-only deque/getter cold cleanup 命名、注释与书签；
- iOS armv7 add-back-capacity cleanup 与六个 getter SjLj cleanup 命名；
- 四端 getter 的 local-owner cleanup/absence 注释和关键书签。

四个 IDB 已全部原位保存。六个 coverage row 的容器布局、map/block reserve、allocation
failure、partial commit、owner publication、unwind cleanup 与 target codegen 差异现已闭合，
状态升级为 `IMPLEMENTED`。正式 Debug build 仍因当前环境缺少 cmake/ninja/Emscripten
工具链而不可执行；这不被伪装成已完成的构建验证。
