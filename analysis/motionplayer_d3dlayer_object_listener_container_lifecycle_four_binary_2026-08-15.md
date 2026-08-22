# motionplayer D3DLayer / D3DLayerObject / listener 容器与生命周期四端恢复

日期：2026-08-15

## 1. 范围与结论

本轮只以 `reference/binaries/` 的四个当前参考二进制为事实源：

- Android arm64-v8a；
- Android armeabi-v7a；
- iOS arm64；
- iOS armv7。

目标是恢复 `D3DLayerObject`、`D3DLayer`、`DrawDeviceManagerItem` 和
`D3DLayerListener` 之间的源代码结构、对象布局、虚表、父子挂接、listener
容器 ABI、更新/绘制数据流和析构边界。旧 `libkrkr2.so` 注释没有被当作证据。

本轮最重要的纠正有四项：

1. `D3DLayerObject::OnUpdate` 的真实虚函数 ABI 是
   `(this, tjs_int updateState, const tTJSVariant &state)`，不是单 Variant；基类实现
   虽然忽略 `updateState`，但两个 root 调用点仍传入它。
2. 成员不是裸 `float[16]`，而是有 identity ctor/dtor 和 `set(...)` 调用的
   `cocos2d::Mat4`。由于 `Mat4::set` 自身还会把行参数映射到 column-major
   存储，最终结果是 `setMatrix` 保持输入原序、`setMatrixGL` 得到输入转置；旧
   Web 移植恰好写反。
3. `D3DLayerObject::TransformPoint` 是纯虚函数；`DrawDeviceManagerItem` 有一个
   显式、恒返 `false` 的 override，`D3DLayer` 则实现真实坐标变换。
4. 基类构造器只接收 script owner，先把 `Parent` 置 null 并注册 native adaptor；
   派生构造完成自己的字段后，再经统一 parent setter 移除旧 parent、写入新
   parent、调用 root add。旧移植直接在基类 initializer 中写 parent，源级生命周期
   顺序不精确。

## 2. factory 与构造调用链

### 2.1 D3DLayer factory

| 目标 | factory | native allocation | D3DLayer ctor |
|---|---:|---:|---:|
| Android arm64 | `0x52D308` | `0x90` | `0x5333F0` |
| Android armv7 | `0x49361C` | `0x74` | `0x496E0C` |
| iOS arm64 | `0x1002317E8` | `0x98` | `0x1002359AC` |
| iOS armv7 | `0x230594` | `0x78` | `0x234770` |

四端一致的错误边界：

```text
numparams < 1
  -> TJS_E_BADPARAMCOUNT

params[0] 不是 object
  -> TJS_E_INVALIDTYPE

params[0] 是 object，但没有 root 的 native class id
  -> TJS_E_INVALIDTYPE

否则
  -> new D3DLayer(objthis, rootNative)
```

因此“有一个非 object 参数”不能和“没有参数”合并为同一个
`TJS_E_BADPARAMCOUNT` 分支。

### 2.2 基类构造器与派生尾部

独立基类构造器：

| 目标 | D3DLayerObject ctor |
|---|---:|
| Android arm64 | 内联在 `0x5333F0` |
| Android armv7 | `0x496990` |
| iOS arm64 | `0x1002355B4` |
| iOS armv7 | `0x2342B4` |

它只接收 `(this, scriptOwner)`，依次完成：

1. 写基类虚表；
2. 保存 borrowed `scriptOwner`；
3. `Parent = nullptr`；
4. `FrontIndex = 0`、`BackIndex = 0`、`DrawPlane = 1`；
5. 构造空 listener list；
6. 若 owner 非 null，分配 native adaptor 并通过 `NativeInstanceSupport` 注册。

后续 vtable 逐槽核对确认：这里分配的 adaptor 只有 `vptr + D3DLayerObject *` 两个
字段，是 borrowed lookup view。它继承空 `Invalidate`；`Destruct` 只经 deleting dtor
删除 adaptor 自身，不删除也不清空所指对象。四份注册调用都忽略返回状态。具体
`D3DLayer` 的普通 NCBind adaptor 保持 non-sticky，才是业务对象所有者；详细槽位见
`motionplayer_drawdevice_multiple_inheritance_vtables_completion_lifecycle_four_binary_2026-08-15.md`。

随后 D3DLayer 派生构造器写派生虚表、`Visible = true`、四个零 clip，调用
`Mat4` identity 构造器，最后调用统一 parent setter。构造顺序不能简化为“基类
直接保存最终 parent”，因为 native adaptor 的注册发生在派生字段初始化之前。

parent setter 地址：

| 目标 | setter |
|---|---:|
| Android arm64 | 优化内联在 `0x5333F0`，remove helper 为 `0x529F78` / `0x52A18C`，add 为 `0x529CFC` |
| Android armv7 | `0x492388`，remove 为 `0x4922A4`，add 为 `0x4923B0` |
| iOS arm64 | `0x1002301E8`，remove 为 `0x100230088`，add 为 `0x100230234` |
| iOS armv7 | `0x22F298`，remove 为 `0x22F1E4`，add 为 `0x22F2BE` |

精确数据流为：

```text
if old Parent != null:
    erase at most one matching front-multiset node
    erase at most one matching back-multiset node
    if either erase succeeded:
        child.OnDetached()
        oldRoot.changedHook()     // 当前两个 root 实现均为空

Parent = newParent

if newParent != null:
    newParent.AddChild(this)
```

## 3. 对象布局与 STL ABI

### 3.1 D3DLayerObject

共同字段：

| 字段 | 64 位偏移 | 32 位偏移 | 所有权 |
|---|---:|---:|---|
| vptr | `+0` | `+0` | ABI |
| script owner | `+8` | `+4` | borrowed |
| Parent root | `+16` | `+8` | borrowed |
| FrontIndex | `+24` | `+12` | int32 |
| BackIndex | `+28` | `+16` | int32 |
| DrawPlane | `+32` | `+20` | int32，初值 1 |

listener list 从 64 位 `+40`、32 位 `+24` 开始，但 Android 的 libstdc++ ABI
与 iOS libc++ ABI 不同：

| 目标 | list 表示 | 基类大小 |
|---|---|---:|
| Android arm64 | `next, prev` circular sentinel，无 size | `0x38` |
| Android armv7 | `next, prev` circular sentinel，无 size | `0x20` |
| iOS arm64 | `next, prev, size`，size 在 `+56` | `0x40` |
| iOS armv7 | `next, prev, size`，size 在 `+32` | `0x24` |

每个 list node 在 64 位为 `0x18`，在 32 位为 `0x0C`，布局是
`next, prev, D3DLayerListener *payload`。

### 3.2 D3DLayer 派生字段

| 目标 | Visible | Clip[4] | `cocos2d::Mat4` | 最终大小 |
|---|---:|---:|---:|---:|
| Android arm64 | `+56` | `+60` | `+76` | `0x90` |
| Android armv7 | `+32` | `+36` | `+52` | `0x74` |
| iOS arm64 | `+64` | `+68` | `+84` | `0x98` |
| iOS armv7 | `+36` | `+40` | `+56` | `0x78` |

`Mat4` 本体恰为 `0x40` 字节。Android 通过 imported ctor 初始化 identity：

- arm64 `0x40D830`；
- armv7 `0x2E1614`。

iOS 静态链接体直接复制 identity 常量：

- arm64 `0x101135A34`；
- armv7 `0xF94446`。

这排除了“源代码里只是带 brace initializer 的 `float Matrix[16]`”这一结构。

## 4. 虚表恢复

三个相关 vtable address point：

| 目标 | D3DLayerObject | DrawDeviceManagerItem | D3DLayer |
|---|---:|---:|---:|
| Android arm64 | `0x19FA888` | `0x19FAC18` | `0x19FAEF8` |
| Android armv7 | `0x10AAE50` | `0x10AB028` | `0x10AB198` |
| iOS arm64 | `0x1019B0610` | `0x101AEE880` | `0x101AEEBE8` |
| iOS armv7 | `0x1778C9C` | `0x183907C` | `0x1839230` |

基类十个槽位的源级顺序：

| 槽 | 语义 | 基类实现 |
|---:|---|---|
| 0 | complete destructor | 真实析构 |
| 1 | deleting destructor | Android trap；iOS 真实 delete wrapper |
| 2 | `IsVisible()` | pure virtual |
| 3 | `Draw(offset)` | no-op |
| 4 | `OnParentHasParent()` | no-op |
| 5 | `OnDetached()` | no-op |
| 6 | `AddListener(listener)` | list push_back |
| 7 | `RemoveListener(listener)` | list remove-all |
| 8 | `OnUpdate(updateState, state)` | script call + listener fan-out |
| 9 | `TransformPoint(x, y)` | pure virtual |

Android 的基类 deleting-dtor 槽位不是普通析构：arm64 `0x529B98` 是
`BRK #1`，armv7 `0x492304` 是 2-byte `UDF #0xFE`，随后 `0x492306`
是独立的 2-byte `MOVS R0,R0` alignment padding，并不是 `UDF #0xDEFE`
四字节指令。iOS 则分别在
`0x10023012C`、`0x22F22E` 调析构 body 后进入 `operator delete`。这属于真实
平台 ABI 差异，不能把四端统一注释成 trap，也不能把 Android 结果外推到 iOS。

`DrawDeviceManagerItem` 覆盖槽 0..3 和 9；其槽 9 明确恒返 false：

- Android arm64 `0x532EF4`；
- Android armv7 `0x4968D0`；
- iOS arm64 `0x1002354A8`；
- iOS armv7 `0x234218`。

`D3DLayer` 同样覆盖槽 0..3 和 9，继承三个空 hook、listener add/remove 和
`OnUpdate`。

## 5. root add/remove 与重复节点边界

root add 地址：

- Android arm64 `0x529CFC`；
- Android armv7 `0x4923B0`；
- iOS arm64 `0x100230234`；
- iOS armv7 `0x22F2BE`。

对于非 null child，它先调用 `child.OnDetached()`；若 root 自身还有 Parent，再调用
`child.OnParentHasParent()`；然后把 child 指针分别插入两个 `std::multiset`，其空
comparator 分别解引用 child 的当前 front/back index，最后调用 root 的空 changed hook。节点
本身不保存整数 key。它不写
`child.Parent`，所以写 parent 的职责在前述 setter 中。

add 不做重复检查。对同一 child 连续 add 会在 front/back 两张表中各产生多个节点。
remove 和析构路径先按 child 的实时 index 取得等价区间，再按指针相等查找，因此每张表
最多删除一个匹配节点；成功删除任一项才调用
detach hook。remove 后也不把 `child.Parent` 清零，因此之后的 child 析构还会用保留的
Parent 指针再次尝试删除，其安全性依赖 root 仍然存活。

节点载荷、比较器和重复节点变更索引时的树不变量边界，见
`motionplayer_drawdevice_front_back_pointer_multiset_four_binary_2026-08-15.md`。

## 6. listener 容器与生命周期

### 6.1 base listener 布局

| 字段 | 64 位 | 32 位 | 初值/所有权 |
|---|---:|---:|---|
| vptr | `+0` | `+0` | ABI |
| D3DLayer owner | `+8` | `+4` | borrowed |
| stretch type | `+16` | `+8` | `8` |
| bicubic param | `+20` | `+12` | `-0.5f` |
| sizeof | `0x18` | `0x10` |  |

构造器保存 owner/defaults 后，在 owner 非 null 时调用虚槽 6
`owner->AddListener(this)`。析构器地址为 Android armv7 `0x497988`、iOS arm64
`0x1002364C4`、iOS armv7 `0x235164`；Android arm64 的同一 base-dtor 逻辑内联在
`D3DPicture` 析构 `0x53F560` 中。析构时若 owner 非 null，调用
`owner->RemoveListener(this)`。

owner 从不 AddRef/Release，也从不被清零。`D3DLayer` 自己的析构只销毁 list
节点，不遍历或删除 listener 对象，也不回写各 listener 的 owner。因此明确的生命周期
前置条件是：所有 listener 必须先于其 D3DLayer 析构。反过来会使 listener 析构通过
dangling owner 调用 `RemoveListener`。

### 6.2 AddListener / RemoveListener

| 目标 | AddListener | RemoveListener |
|---|---:|---:|
| Android arm64 | `0x531184` | `0x5311C8` |
| Android armv7 | `0x495286` | `0x4952AC` |
| iOS arm64 | `0x1002336C8` | `0x100233720` |
| iOS armv7 | `0x232572` | `0x23259A` |

精确边界：

- null listener：两者均 no-op；
- add：无查重，始终 push_back 一个新 node；
- remove：等价 `std::list::remove(listener)`，删除所有相等 payload，而非只删第一项；
- iOS 每次 add/remove 同步增减 list size；Android 根本没有 size 字段；
- list 保持插入顺序，所有 fan-out 均按此顺序执行。

后续 V267 四端逐指令复核又闭合了这里未展开的异常边界：`AddListener` 的 node
`operator new(0x18/0x0C)` 是唯一 source-level throw point，并且严格先于任何 sentinel
或 iOS cached-size 写入。node payload/离线 link 字段先完整初始化，之后的 Android
`_List_node_base::_M_hook` 或 iOS 内联 relink/`size++` 都只含 load/store，不调用用户代码。
因此 listener base constructor 中的虚调用若因分配失败而抛出，list 保持调用前状态，
不存在半链接 node，也不需要未完成 base destructor 执行 `RemoveListener` 回滚。

remove 的实现细节也存在稳定 ABI 分叉：Android arm64 内联逐节点 unhook/delete，Android
armv7 调用带 value-alias 保护的 libstdc++ specialization；iOS 两端把连续匹配区间 splice
到栈上临时 libc++ list、同步转移 source/temporary size，扫描结束后统一 clear/delete。
iOS armv7 为 splice call 保留 SJLJ cleanup，异常时先清临时 list 再 resume；实际 splice
helper 本身没有 call 或 source-language throw point。完整证据见
`motionplayer_d3dlayer_listener_registration_commit_exception_atomicity_four_binary_2026-08-21.md`。

回调循环在回调返回后才从当前 node 读取 next。回调若同步删除当前 listener，原生也会
从已释放 node 继续读取，属于未防护的 UAF/iterator invalidation 边界；恢复代码不应
擅自复制 list 或预取 next 来“修复”它。V268 继续闭合了其余 mutation/exception
状态：当前 node 保持存活时，删除 future node 会令 post-callback next 跳过它；tail append
会在同一轮被继续访问，callback 持续 append 因而可以让本轮永不抵达 sentinel。callback
抛出则直接中止 fan-out；没有 catch、deferred continuation 或容器回滚。

## 7. OnUpdate 的真实 ABI 与数据流

基类实现：

| 目标 | OnUpdate |
|---|---:|
| Android arm64 | `0x529B9C` |
| Android armv7 | `0x492308` |
| iOS arm64 | `0x100230140` |
| iOS armv7 | `0x22F23E` |

四端都是三个 machine argument：

```cpp
bool OnUpdate(tjs_int updateState, const tTJSVariant &state);
```

基类忽略 `updateState`，但不能从签名删除。它先在 script owner 非 null 时调用：

```text
ScriptOwner->FuncCall(
    0, "onUpdate", nullptr, nullptr,
    1, [&state], ScriptOwner)
```

这里的 `&state` 是第三个 machine argument 指向的原始 `tTJSVariant` 地址。四端都只把
该 pointer store 到单元素参数数组；没有 copy constructor、AddRef/Release、临时 Variant
destructor 或异常 cleanup。`FuncCall` 的 mutable `tTJSVariant **` ABI 因而实际接收了对
const-reference 的 cast-away-const view。返回的 `tjs_error` 完全不检查；普通失败码仍继续
listener fan-out，只有真正抛出的 C++ 异常会退出。

script callback 在首个 list cursor load 之前执行，所以它完成的 listener add/remove 对随后
fan-out 立即可见；若 callback 销毁 `this`，返回后的 cursor 初始化本身即会访问悬空对象。

然后按 list 顺序调用每个 listener 的 `IsVisible()`，把所有返回值按位 OR；没有短路，
每个 callback 都会执行，最终只返回低 Boolean 位。

root 的共同 helper 在 Android armv7 `0x4962C0`、iOS arm64
`0x100234D3C`、iOS armv7 `0x2338EC` 保留为独立函数；Android arm64 则内联在两个
调用者：

```text
capture:
    Variant state(0)                    // one owner for the whole tree pass
    for visible child in live FrontItems:
        child.OnUpdate(0, state)        // every child receives the same address

Show:
    snapshot = UpdateState
    Variant state(snapshot)             // one owner for the whole tree pass
    for visible child in live FrontItems:
        child.OnUpdate(snapshot, state) // every child receives the same address
    destroy state
    UpdateState = 0                     // normal-success commit only
```

capture 地址分别为 `0x531468`、`0x495778`、`0x100233FA8`、`0x232CA8`；Show
地址分别为 `0x531890`、`0x495978`、`0x100234294`、`0x232F1C`。

V269 继续确认 tree successor 只在当前 `IsVisible` / `OnUpdate` 返回后从 live current node
计算：future erase/insert 可改变后续路径，current erase 后 increment 是 UAF。共享 Variant 可沿
script bridge 被前一 child 改写，后一 child 观察改写值但 integer snapshot 不变；异常先析构共享
Variant再逃逸。`Show` 的零写不是 cleanup：重入 `update(newState)` 在正常返回时被覆盖，在随后
抛出时保留；`capture` 固定传 0 且完全不读/清 root UpdateState。完整证据见
`motionplayer_root_updateobjects_shared_variant_tree_iterator_updatestate_commit_four_binary_2026-08-21.md`。

V270 对同一 FrontItems tree 的 `getChildren` 继续闭合：它构造 `ncbArrayAccessor`，忽略 fresh
Array `NativeInstanceSupport` 的普通 status，直接向 `tTJSArrayNI::Items` deque emplace
`ScriptOwner/ScriptOwner` Object closure；exact `IsValid==1`检查用调用前snapshot，append却在回调后
重读live field。successor仍在 IsValid/append之后计算，current erase同样UAF；异常析构accessor、
Release Array并清理已追加元素。完整证据见
`motionplayer_drawdevice_getchildren_native_array_items_live_owner_deque_lifecycle_four_binary_2026-08-21.md`。

## 8. Mat4 与 setMatrix / setMatrixGL 的双重映射

| 目标 | setMatrix | setMatrixGL | Mat4::set |
|---|---:|---:|---:|
| Android arm64 | `0x52D578` | `0x52D628` | import PLT `0x40AD70` |
| Android armv7 | `0x4937AE` | `0x49383E` | import thunk `0x2DEBA8` |
| iOS arm64 | `0x1002319E4` | `0x100231A74` | `0x101135A60` |
| iOS armv7 | `0x2307FE` | `0x230892` | `0xF94488` |

`cocos2d::Mat4::set` 接收按“行”命名的 16 个参数，但写入 column-major `m[]`：

```text
m[] = p0,p4,p8,p12, p1,p5,p9,p13,
      p2,p6,p10,p14, p3,p7,p11,p15
```

`D3DLayer::setMatrix` 先把脚本参数按以下顺序传给它：

```text
m0,m4,m8,m12, m1,m5,m9,m13,
m2,m6,m10,m14, m3,m7,m11,m15
```

经过 `Mat4::set` 的第二次映射后，最终内存是：

```text
Matrix.m[] = m0,m1,m2,m3, m4,m5,m6,m7,
             m8,m9,m10,m11, m12,m13,m14,m15
```

`setMatrixGL` 则把 `m0..m15` 原序直接交给 `Mat4::set`，所以最终内存是一次转置：

```text
Matrix.m[] = m0,m4,m8,m12, m1,m5,m9,m13,
             m2,m6,m10,m14, m3,m7,m11,m15
```

两个 setter 写完后都按 list 顺序调用每个 listener 的 `IsVisible()`，并丢弃返回值。
`setClip`（`0x52D6BC` / `0x4938CE` / `0x100231AE8` / `0x23091E`）只写四个
float，不校验、不规范化、也不通知 listener。

## 9. D3DLayer Draw 与 TransformPoint

| 目标 | IsVisible | Draw | TransformPoint |
|---|---:|---:|---:|
| Android arm64 | `0x53361C` | `0x533624` | `0x533688` |
| Android armv7 | `0x496EC0` | `0x496EC6` | `0x496EFC` |
| iOS arm64 | `0x100235AA4` | `0x100235AAC` | `0x100235B10` |
| iOS armv7 | `0x2348AC` | `0x2348B2` | `0x2348E0` |

`Draw` 的真实虚槽参数是一个 8 字节、按 `const &` 传递的
`{ float x; float y; }` point，而不是 render target。四端均原样复制 root 的两个
32-bit float 字段，没有整数转换；剥离符号后无法证明原始类型名，因此恢复源码使用
`D3DPoint_guess`，不能再写成整数成员的 `tTVPPoint`。具体 `D3DLayer` 忽略这个
offset。只有 `Parent != null && Visible` 时才读取
`Parent->CurrentTarget`，然后不再检查 listener visibility，直接按 list 顺序调用每个
listener 的 `Draw(CurrentTarget)`。

`TransformPoint` 没有 Parent null guard：

```text
x = Matrix.m[12] + trunc_toward_zero(parent.screenWidth / 2)
                   + x * Matrix.m[0]
y = Matrix.m[13] + trunc_toward_zero(parent.screenHeight / 2)
                   + y * Matrix.m[5]
return true
```

宽高是 signed int32，除以二遵循 C++ 向零截断，因此负奇数尺寸不能替换为算术右移。
乘加仍是普通 float 运算，不应提升成 double 后再收窄。

## 10. 析构链

| 目标 | D3DLayerObject body/complete | D3DLayer complete | D3DLayer deleting |
|---|---:|---:|---:|
| Android arm64 | `0x533144` | `0x5335AC` | `0x5335E0` |
| Android armv7 | `0x496B18` | `0x496E6C` | `0x496E94` |
| iOS arm64 | body `0x10023002C`，complete thunk `0x100230128` | `0x100235A38` | `0x100235A6C` |
| iOS armv7 | body `0x22F128`，complete thunk `0x22F22A` | `0x234858` | `0x234880` |

D3DLayer 派生析构先运行 `Mat4` 析构，再进入 base。base 重写基类 vptr，尝试从
Parent 两张 pointer multiset 各删一个节点；若任一成功，调用基类阶段的空 detach hook 和 root
空 hook；最后只释放 list nodes。Parent、script owner、listener payload 和 listener
owner link 均不被清零。析构链没有查询或 detach 上述 borrowed native adaptor，因此
脚本对象若继续存活，该 adaptor 中会保留已失效的原始指针值；这是四端共同边界。

V277 对 manager-item 复用这条析构链的异常边界进一步拆分：Android arm64 与 iOS armv7 的
base-destructor landing 会在 detach/hook 逃逸时先释放仍存的 listener-list nodes，再进入
`__cxa_begin_catch -> std::terminate`；Android armv7 与 iOS arm64 没有本地 cleanup landing。
software manager item 还先 Release 自有 cache texture：前两端在该 Release 逃逸时先调用 base
dtor再terminate，后两端会直接越过base cleanup。四端 deleting destructor 都只在complete body
正常返回后 raw-delete item storage；不存在异常时仍强制 raw-delete 的外层 owner。

## 11. 本轮源代码与测试对齐

`cpp/plugins/DrawDeviceD3DIntf.h` 与 `cpp/plugins/DrawDeviceD3D.cpp` 已按上述证据恢复：

- `D3DLayerObject` ctor 只收 owner，新增统一 `SetParent_guess`；
- native adaptor 注册移到基类 ctor 顺序；
- root 提取 `UpdateObjects_guess(tjs_int)`，capture/Show 传真实双参数 ABI；
- 把 `D3DLayerObject::Draw` 的虚槽 ABI 从旧猜测的 texture pointer 修正为
  `const D3DPoint_guess &`；root 在 capture/Show 中按位复制两个 float 构造
  `{OffsetX, OffsetY}`，而具体
  D3DLayer 再从 Parent 取 `CurrentTarget` 交给 listener；
- `OnUpdate` 恢复整数参数但基类明确忽略它；
- `TransformPoint` 改为 pure virtual，manager 增加恒 false override；
- `D3DLayer` 成员改为真实 `cocos2d::Mat4`；
- 两个 matrix setter 改为调用 `Mat4::set`，修正最终内存映射；
- factory 区分 BADPARAMCOUNT 与 INVALIDTYPE。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增回归，锁定：

- identity Mat4 构造；
- `setMatrix` 原序与 `setMatrixGL` 转置；
- duplicate listener 节点会被逐个 fan-out；
- remove 一次删除所有重复节点；
- `OnUpdate` 的整数 ABI 参数不替代独立 Variant，脚本只收到 Variant；
- listener 析构时的第二次 remove 是 layer 仍存活条件下的 no-op；
- D3DLayer factory 的 0 参数/非 object 参数返回值不同。

## 12. 恢复库改进

四份 `out/ida-recovery/` 数据库已统一加入：

- 平台专用 `D3DLayerObject_*_guess`、`D3DLayer_*_guess`、
  `D3DLayerListener_*_guess` 结构，保留 Android/iOS list ABI 差异；
- factory、ctor/dtor、capture/Show/update helper、listener add/remove、matrix/clip、
  Draw/TransformPoint 和 manager override 的 `_guess` 命名与关键函数类型；
- vtable、生命周期和最终 matrix 映射注释/书签；
- V268 从 canonical recovery IDB fresh readback 发现，本段旧状态声明并未实际保存：Android
  arm64 仍把 `0x529B98..0x529C60` 合成一个函数，Android armv7 的 `0x492304` 也仍不是
  function。现已真正把 arm64 拆为 `0x529B98..0x529B9C` trap 与
  `0x529B9C..0x529C60` OnUpdate；armv7 则按真实 2-byte 指令恢复
  `0x492304..0x492306` trap，保留 `0x492306..0x492308` padding，并从 canonical
  再次重开/反编译确认。完整 V268 证据见
  `motionplayer_d3dlayer_listener_fanout_variant_identity_reentrancy_exception_four_binary_2026-08-21.md`。

## 13. 仍保留的不确定性

- 原始私有 helper 的符号拼写已丢失，因此 Web 源中的 `SetParent_guess`、
  `UpdateObjects_guess` 继续带 `_guess`；行为、调用位置和 ABI 已由四端闭合。
- root changed hook 的原始名称未知；当前两个具体 root 的对应虚函数都为空，因此没有为
  它臆造公开接口。
- 同步 callback 改写 listener list、root 先于 child/listener 析构等未防护路径属于原生
  前置条件/未定义边界，本轮只记录并保持，不把它们改成更安全但不一比一的行为。

## 14. V209 补充：borrowed adaptor、四槽容器与 factory 重入

V209 已把本报告第 2.2 节当时只记录为“注册 status 被忽略”的部分继续闭合到 core 容器：

- `tTJSCustomObject` 固定四槽；REGISTER first-empty、允许重复 ID、满槽返回 `-1`；
- GET oldest-first，因此重复 `D3DLayerObjectNativeInstance` ID 永远返回最旧 borrowed view；
- Finalize/析构分别按 `3 -> 0` 调 Invalidate/Destruct；
- 普通 D3DLayer shell 是 slot0 concrete + slot1 borrowed；两次 raw descriptor 重入填满
  slot2/slot3，第三次及以后 borrowed adaptor 注册失败但被忽略并泄漏；
- concrete slot0 被 wrapper 更新到最新 generation，root add/remove 仍看到 slot1 最旧 generation。

完整四端地址、伪代码、slot 时间线、异常 landing、IDB 写回和回归见
`motionplayer_d3dlayerobject_borrowed_adaptor_four_slot_container_reentry_lifecycle_four_binary_2026-08-17.md`。
