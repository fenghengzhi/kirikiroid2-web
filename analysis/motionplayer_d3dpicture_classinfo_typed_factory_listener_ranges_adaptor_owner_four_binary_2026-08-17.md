# motionplayer `D3DPicture` ClassInfo、typed Factory、listener、ranges 与 adaptor owner 四端恢复

日期：2026-08-17
纵切面：V205

## 范围

本纵切面只把 `reference/binaries/` 中四个当前参考发布物作为实现证据：

- Android arm64-v8a；
- Android armeabi-v7a；
- iOS arm64；
- iOS armv7。

V204 已确认 D3DImage concrete adaptor 是 image owner，root ManagedObjects set 与
D3DPicture 都只是 raw borrower。本轮继续闭合真正的 `global.D3DPicture`：

- 独立 ClassInfo、整宽 static-init guard 与 Regist/Unregist 事务；
- standard concrete adaptor 的布局、sticky/owner 语义和 producer 集合；
- typed two-argument Factory 的 outer gate、result clear、strict conversion、surplus、attach、
  rollback、exception 与 populated re-entry；
- native object 的字段布局、构造顺序、listener publication 与析构顺序；
- D3DLayer owner、D3DImage borrower 和 script adaptor owner 的相对寿命；
- `std::vector<{int32×4}>` 的四端 STL 实现、扩容上限、异常保证与 clear 边界；
- Draw 对 layer/image/ranges/scale 的实际读取边界。

四份产物均 stripped。不能从二进制证明的私有源名继续加 `_guess`。旧
`libkrkr2.so` 地址和旧注释不作为证据。

## 结论摘要

1. D3DPicture 有独立 ClassInfo：LP64 `0x20`、ILP32 `0x10`，不是 D3DLayer、
   D3DImage 或 D3DLayerBase/root 的 ClassInfo。
2. guard 为 `8/4/8/4` bytes。四端初始化器只测试低 bit/byte，但写入整宽整数 `1`。
3. registrar 的公开表固定为 typed `Factory(D3DLayer *, D3DImage *)`、四个 RW property、
   `assignImageRange`、`clearImageRange`、`setCoord`、`setScale`、`getScale`。
4. Factory outer gate 精确为：named call 返回 `TJS_E_MEMBERNOTFOUND`；恰好一个 Void
   成功产生 empty concrete shell；其它路径先 clear result，再要求 `argc >= 2`。
5. ordinary Factory 严格按 arg0 D3DLayer、arg1 D3DImage 的顺序转换，只读这两个参数，
   surplus 全部忽略。正确 class 的 empty adaptor 可转换为 null native pointer。
6. native storage 在 strict conversion 前分配。Android arm64 与两份 iOS 有
   new-expression unwind cleanup；Android armv7 没有，任一 converter 抛异常都会泄漏
   `0x40` raw storage。
7. 两个转换都完成后才进入 constructor/listener publication；constructor 在 AddListener
   之后只做不抛异常的字段 store，因此没有“已插 listener 后又因第二参数转换抛出”的路径。
8. attach 查询 receiver 的 D3DPicture concrete adaptor。查询失败会 deleting-dtor fresh
   picture，释放 range buffer并从 layer list remove，然后返回 `TJS_E_NATIVECLASSCRASH`。
9. populated receiver re-entry 直接覆盖 adaptor payload，不 delete旧 picture。旧 picture、
   range allocation、borrowed image/layer 和 layer 中的旧 listener node 全部泄漏。
10. concrete adaptor 是正常 picture owner，D3DLayer listener list 与 D3DImage 都只是 borrower。
    四端没有 `CreateAdaptor(existing D3DPicture *)` producer。
11. ranges 是三指针 `std::vector`，元素严格为 `{left,top,right,bottom}` 四个 int32；
    clear 只设 `end=begin`。合法满容量追加在四端均呈 `1,2,4,8,...` capacity。
12. Draw 不读 `scale`，不检查 empty ranges、null TransformLayer 或 null Image。render method
    非 null 时 empty vector 仍可进入 `OperateTriangles(..., triangleCount=0, ...)`。
13. layer 必须活过 picture 的 Draw 和 destructor；image 必须活过每次 Draw，但 picture
    destructor 不访问 image。
14. generated Unregist 四端都存在，integrated loader 没有调用链；runtime 不能假设 class
    publication 或 adaptor type state 会卸载。

## 独立 ClassInfo ABI

### 四端定位

| 目标 | ClassInfo | concrete class ID | guard | static init |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AAF670` | `0x1AAF680` | `0x1AAF690` | `0x42CAE8` |
| Android armv7 | `0x110E20C` | `0x110E214` | `0x110E21C` | `0x2FEFA4` |
| iOS arm64 | `0x101AEE490` | `0x101AEE4A0` | `0x101AEE4B0` | `0x10024CA10` |
| iOS armv7 | `0x1838E88` | `0x1838E90` | `0x1838E98` | `0x24E5FC` |

LP64 布局：

```cpp
struct D3DPicture_NCB_ClassInfo_guess {
    bool initialized;                 // +0x00
    unsigned char padding0[7];
    const tjs_char *className;        // +0x08
    tjs_int32 classID;                // +0x10
    unsigned char padding1[4];
    iTJSDispatch2 *classObject;       // +0x18
};                                    // 0x20
```

ILP32 布局：

```cpp
struct D3DPicture_NCB_ClassInfo_guess {
    bool initialized;                 // +0x00
    unsigned char padding0[3];
    const tjs_char *className;        // +0x04
    tjs_int32 classID;                // +0x08
    iTJSDispatch2 *classObject;       // +0x0C
};                                    // 0x10
```

guard：

| 目标 | size | test | store |
|---|---:|---|---|
| Android arm64 | `0x08` | low bit | qword `1` |
| Android armv7 | `0x04` | low byte/bit | dword `1` |
| iOS arm64 | `0x08` | low bit | qword `1` |
| iOS armv7 | `0x04` | low byte/bit | dword `1` |

Android 保留独立 leaves：

| 语义 | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x52DC40` | `0x493B4C` |
| GetID | `0x52DC50` | `0x493B58` |
| GetClassObject | `0x52DC60` | `0x493B64` |
| IsSubClass | `0x52DC70` | `0x493B6C`（短 leaf，旧 IDB 未建 func） |
| Set | `0x52DC78` | `0x493B74` |
| Clear | `0x52DCB0` | `0x493B9C` |
| zero ctor | `0x52DCCC` | `0x493BAC`（短 leaf，旧 IDB 未建 func） |

`Set` 是 first-publication-wins：`initialized != 0` 时返回 false，不替换三个字段。iOS
优化器把这些 trivial leaves 内联，但 static init、registrar、factory attach 与所有 receiver
resolver 都访问同一 tuple，语义没有变化。

## registrar、adaptor 与 no-unload

### member table

四端顺序完全一致：

```text
Factory(D3DLayer *, D3DImage *)
opacity RW
blendMode RW
stretchType RW
bicubicParam RW
assignImageRange
clearImageRange
setCoord
setScale
getScale
```

| 目标 | member registrar | Regist | Unregist | RegistBegin |
|---|---:|---:|---:|---:|
| Android arm64 | `0x52DCE0` | `0x53E774` | `0x53E8D8` | `0x53EA30` |
| Android armv7 | `0x493BBC` | `0x4A0A64` | `0x4A0AE8` | `0x4A0B64` |
| iOS arm64 | `0x100231DD0` | `0x100241AF0` | `0x100241B58` | `0x100241BB4` |
| iOS armv7 | `0x230B86` | `0x241810` | `0x2418C4` | `0x241974` |

共同事务：

```text
Regist:
    construct native class descriptor
    ClassInfo.Set(name, classID, classObject)
    replay the nine member descriptors in order
    publish global.D3DPicture

Unregist:
    replay the same descriptors in unregister mode
    remove global.D3DPicture
    ClassInfo.Clear()
```

Android armv7 和 iOS 两端显式保留 RAII registration state/flag dispatcher；Android arm64
把部分 end/clear 折叠到 virtual wrapper。该编译器差异不改变源级事务。

全插件 call/xref 审计仍只看到 integrated load/publication。没有 DrawDeviceD3D.dll unload、
registered-set erase 或 registrar vtable Unregist caller。因此 Unregist 是 generated dormant
path，不是实际 runtime 生命周期事件。

### concrete adaptor

standard adaptor：

```text
LP64: vptr + D3DPicture *native + bool sticky + padding = 0x18
ILP32: vptr + D3DPicture *native + bool sticky + padding = 0x0C
```

| 语义 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| CreateEmptyAdaptor | `0x53EB84` | `0x4A0C4C` | `0x100241CCC` | `0x241AD0` |
| Invalidate | `0x53EBB8` | `0x4A0C70` | `0x100241D00` | `0x241AF4` |
| complete dtor | `0x53EBF8` | `0x4A0C8C` | `0x100241D40` | `0x241B10` |
| deleting dtor | `0x53EC54` | `0x4A0CC8` | `0x100241DA0` | `0x241B4A` |

Invalidate/dtor 的共同核心：

```cpp
if(native && !sticky)
    delete native;
native = nullptr;
sticky = false;
```

ordinary Factory attach 从不置 sticky，所以 adaptor 是 picture 的唯一正常 owner。D3DLayer
只保存 listener pointer node，D3DImage 只是被 picture 保存的 raw pointer。

四端 concrete class-ID xref 集合只有：ClassInfo/registrar、Factory attach、member receiver
resolver；没有 `ncbInstanceAdaptor<D3DPicture>::CreateAdaptor(existing)` 形状的 producer。

## typed Factory 外层状态机

### 四端入口

| 目标 | descriptor FuncCall | typed invoke | allocate/unbox | ctor |
|---|---:|---:|---:|---:|
| Android arm64 | `0x53F068` | `0x53F140` | `0x53F258` | inline in allocate |
| Android armv7 | `0x4A0F84` | `0x4A1014` | `0x4A10D0` | `0x4A11B0` |
| iOS arm64 | `0x1002421D8` | `0x10024227C` | `0x100242374` | `0x1002424BC` |
| iOS armv7 | `0x242034` | `0x2420A0` | `0x2421E4` | `0x242340` |

outer FuncCall 精确伪代码：

```cpp
if(membername != nullptr)
    return TJS_E_MEMBERNOTFOUND;

if(argc == 1 && argv[0].Type() == tvtVoid)
    return TJS_S_OK;                  // result 未 clear，native payload 仍 null

if(result)
    result->Clear();

if(argc < 2)
    return TJS_E_BADPARAMCOUNT;

return typedInvoke(argv[0], argv[1], objthis);
```

这与 V203/V204 raw factory wrapper 有两个重要区别：

- 它在 named/one-Void gate 之后、arity gate 之前 clear result；
- native 参数由 typed converters 抛异常，不是 raw factory 自己返回
  `TJS_E_INVALIDTYPE`。

### strict conversion

| converter | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| D3DLayer arg0 | `0x53F368` | `0x49EE98` | `0x10023F8C0` | `0x23F19E` |
| D3DImage arg1 | `0x53F464` | `0x4A0474` | `0x1002413E0` | `0x241002` |

顺序固定为：

```text
allocate raw D3DPicture storage
copy/unbox argv[0] as D3DLayer
destroy temporary Variant
copy/unbox argv[1] as D3DImage
destroy temporary Variant
construct D3DPicture(layerNative, imageNative)
```

converter 以 raise-on-error 模式查询对应 concrete class ID。null object/no instance 和 wrong
class 都抛出；但若对象拥有正确 adaptor、payload 本身为 null，则 converter 返回 null native。
因此两个正确 class 的 empty shells 可以构造一个字段为 null 的 populated D3DPicture；普通
scalar property 仍可访问，但 Draw 随后会在 null TransformLayer/Image 上失败。参考没有对此
添加 factory-level null reject。

`argv[2..]` 完全不读取。valid pair + surplus 与恰好两个参数等价。

## allocation 与 exception 矩阵

storage size：LP64 `0x60`，ILP32 `0x40`。四端都在 converter 之前调用 `operator new`。

| 目标 | raw-storage cleanup | temporary cleanup | conversion throw 后结果 |
|---|---:|---|---|
| Android arm64 | embedded `0x53F354` | live Variant dtor | delete `0x60`, resume |
| Android armv7 | 无 | unwind runtime only | 泄漏 `0x40` raw storage |
| iOS arm64 | `0x100242488` | live Variant dtor | delete `0x60`, resume |
| iOS armv7 | `0x2422F8` | SJLJ state destroys live Variant | delete `0x40`, resume |

Android armv7 `0x4A10D0..0x4A1186` 后没有 landing function或 raw-storage delete；下一段
可执行代码直接进入 constructor 区域。这与其 V204 D3DImage constructor-exception leak 同型，
不能用其它三端反向覆盖。

两个 converter 都在 constructor 之前运行，所以第二个 converter 抛出时 listener 仍未注册。
constructor 的 AddListener 之后只剩 vptr/field/vector-zero/default stores，没有后续分配或 converter；
因此本轮未发现 partial-constructor listener rollback 缺口。

## attach、rollback 与 re-entry

typed invoke 的共同成功路径：

```cpp
D3DPicture *fresh = allocateUnboxConstruct(argv[0], argv[1]);
if(!fresh)
    throwNativeClassCreationFailed();

Adaptor *adaptor = query(objthis, D3DPictureClassID);
if(!objthis || queryFailed || !adaptor) {
    delete fresh;
    return TJS_E_NATIVECLASSCRASH;
}

adaptor->native = fresh;              // LP64 +8, ILP32 +4
return TJS_S_OK;
```

### attach failure

fresh picture 已完成 listener registration。receiver 缺失、class ID 不匹配或 adaptor null 时，
deleting dtor 执行：

```text
free ImageRanges allocation (fresh 通常为空)
D3DLayerListener::~D3DLayerListener
    -> borrowed layer->RemoveListener(this)  // remove all matching nodes
operator delete(picture)
return TJS_E_NATIVECLASSCRASH
```

iOS arm64 `0x100242344` 与 iOS armv7 `0x242186` 还显式保留 receiver/adaptor query 自身抛
C++ exception 时的 fresh-native cleanup。Android 编译器把相应 EH region折叠在 invoke/unwind
metadata 内；普通 TJS query error 仍走上面的显式 deleting-dtor 分支。

### populated receiver re-entry

success 只写 payload slot，不检查旧值。若 receiver 已有 native：

- 旧 D3DPicture 永久失去 adaptor owner；
- 旧 ranges allocation 保留；
- 旧 borrowed D3DImage/TransformLayer fields 保留；
- 旧 listener node 仍在原 D3DLayer list 中；
- 新 picture 另注册一个 listener node并成为 adaptor 新 payload。

旧 picture 本身仍分配着，所以 layer fan-out 调它暂时不是 UAF；但此后若旧 layer/image 提前销毁，
旧 picture 的 raw borrower/listener 关系没有任何 owner 能再正常收口。这是 observable leak/lifetime
boundary，不是应在移植版私自去重或自动 delete 的逻辑。

## native layout 与构造顺序

### 字段布局

| 字段 | LP64 | ILP32 | 初值/owner |
|---|---:|---:|---|
| vptr | `+0` | `+0` | listener/picture vtable |
| listener owner D3DLayer | `+8` | `+4` | borrowed |
| stretchType | `+16` | `+8` | `8` |
| bicubicParam | `+20` | `+12` | `-0.5f` |
| D3DImage | `+24` | `+16` | borrowed |
| duplicate TransformLayer | `+32` | `+20` | same borrowed layer |
| opacity | `+40` | `+24` | `255` |
| blendMode | `+44` | `+28` | `2` |
| ranges begin/end/capacity | `+48/+56/+64` | `+32/+36/+40` | owning allocation |
| tail/padding guess | `+72` | `+44` | `0` |
| coordX/coordY | `+76/+80` | `+48/+52` | `0/0` |
| scale | `+84` | `+56` | `1.0f`，Draw 不读 |
| scale tail guess | `+88` | `+60` | `0` |
| allocation size | `0x60` | `0x40` | concrete adaptor owns |

constructor 机器码顺序：

```text
install D3DLayerListener vptr
store listener owner layer
stretchType = 8
bicubicParam = -0.5f
if(layer) layer->AddListener(this)
install D3DPicture vptr
Image = image
TransformLayer = layer
Opacity = 255
BlendMode = 2
zero ranges/tails/coord
Scale = 1.0f
```

Android arm64 的 final derived vptr store 与字段 stores排布略有 instruction scheduling 差异；
源级 publication/ownership 顺序不变。

## destructor 与相对寿命

| 目标 | complete dtor | deleting dtor |
|---|---:|---:|
| Android arm64 | `0x53F560` | `0x53F5C4` |
| Android armv7 | `0x4A121C` | `0x4A1244` |
| iOS arm64 | `0x100242558` | `0x10024258C` |
| iOS armv7 | `0x2423AA` | `0x2423D2` |

共同顺序：

```text
restore D3DPicture dtor vptr
free ranges.begin if non-null
D3DLayerListener::~D3DLayerListener
    restore listener base vptr
    if(ownerLayer) ownerLayer->RemoveListener(this)
deleting variant only: operator delete(this)
```

destructor 不读取 `Image`，也不清 `TransformLayer`。结合 V203 listener list 结论：

- `RemoveListener` 是 `std::list::remove`，删除全部等值 node；
- D3DLayer 必须活过 D3DPicture destructor，否则 base dtor沿 dangling owner 调 virtual remove；
- D3DImage 不必为了 picture destructor 存活，但必须活过所有 Draw；
- D3DPicture 不 AddRef layer/image，不注册 image backpointer，不接收 image invalidation。

最小相对寿命图：

```text
script D3DPicture concrete adaptor
    owns -> D3DPicture native
              borrows -> D3DLayer (Draw + destructor)
              borrows -> D3DImage (Draw only)
              owns -> ranges allocation

D3DLayer listener list
    borrows -> D3DPicture native

D3DImage concrete adaptor
    owns -> D3DImage native
```

## ranges vector 内部实现与边界

### element 与 public methods

element 是 trivial 16-byte tuple：

```cpp
struct ImageRangeTuple_guess {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
```

`assignImageRange` 不排序、不 clamp、不检查 `right >= left` 或 `bottom >= top`；任意四个
int32 bit pattern 按原顺序 append。`clearImageRange` 只执行 `end = begin`，不 shrink、不 free、
不改 capacity。

| 目标 | assign | clear | grow helper | STL family |
|---|---:|---:|---:|---|
| Android arm64 | `0x52E244` | `0x52E2C4` | `0x53390C` | libstdc++ |
| Android armv7 | `0x493D80` | `0x493DD0` | fast `0x497256`, grow `0x497284` | libstdc++ |
| iOS arm64 | `0x100231FD8` | `0x100232034` | `0x100235E84` | libc++ |
| iOS armv7 | `0x230D3A` | `0x230D84` | `0x234C30` | libc++ |

### growth 公式

Android libstdc++：

```cpp
newCapacity = size + max(size, 1);
```

iOS libc++：

```cpp
newCapacity = max(size + 1, 2 * capacity);
```

grow helper 只在合法 vector 的 full-capacity 分支进入，所以 `size == capacity`，四端共同序列为：

```text
0 -> 1 -> 2 -> 4 -> 8 -> ...
```

上限：

| pointer width | max tuple count | max nominal bytes |
|---|---:|---:|
| 64-bit | `0x0FFFFFFFFFFFFFFF` (`2^60-1`) | `< 2^64` |
| 32-bit | `0x0FFFFFFF` (`2^28-1`) | `< 2^32` |

超过上限进入 `vector::_M_emplace_back_aux`/`__throw_length_error` 或 allocator length_error。
新 allocation 成功后才复制旧 trivial tuples并释放旧 begin；allocation/length failure 不发布新的
begin/end/capacity。iOS temporary-buffer path还显式 cleanup temporary allocation后 resume。

### A64 stale IDB boundary 修正

旧 recovery IDB 把 `0x533868` 的 D3DImage `std::set` erase-range helper错误延长到
`0x533A24`，从而把 `0x53390C` 的独立 vector grow prologue标成函数内 label。V205 依据：

- 前函数在 `0x533904` `RET`，`0x533908` 是 noreturn exception call；
- `0x53390C` 从完整 AArch64 save-register prologue重新开始；
- 唯一 caller 是 D3DPicture assign full-capacity branch；
- 它按 16-byte tuple计算 size/capacity、allocate、copy、publish。

恢复库现已拆成：

```text
D3DImage_managedSetEraseRange_helper_guess  0x533868..0x53390C
D3DPicture_ImageRanges_emplaceGrow_guess    0x53390C..0x533A24
```

这也说明“旧 IDB 已命名函数边界”不能替代当前四参考指令证据。

## Draw 数据流与边界

| 目标 | Draw | append vertex |
|---|---:|---:|
| Android arm64 | `0x53F62C` | `0x53F95C` |
| Android armv7 | `0x4A1270` | `0x4A14E4` |
| iOS arm64 | `0x1002425C4` | `0x10024295C` |
| iOS armv7 | `0x242400` | `0x242798` |

共同数据流：

1. 取得 DrawDevice 私有 render manager；
2. `GetRenderMethod(opacity, true, blendMode)`；null 立即返回；
3. source/destination 两个 point vector各 reserve `rangeCount * 6`；
4. 从 borrowed TransformLayer 复制 matrix；
5. 用同一 layer virtual transform picture coord，覆盖 matrix translation；
6. 每个 tuple 发六点 `(L,T),(R,T),(L,B),(R,T),(L,B),(R,B)`；
7. 从 borrowed Image 的当前 holder 取 texture；
8. clip 为完整 target；
9. `OperateTriangles(..., rangeCount * 2, target, target, ..., one texture)`。

边界：

- stored `scale` 只由 set/get 访问，Draw 四端都不读；
- ranges 为空时没有专门 early return；method 非 null 就可调用 triangle count 0；
- `TransformLayer == nullptr` 会在 matrix/transform virtual call崩溃；
- `Image == nullptr` 会在 texture lookup崩溃；
- Image 非 null但 holder null 时 texture pointer 可为 null，reference 仍把它交给 render manager；
- rect 反向、零面积或超出 texture 没有预检查；它们照样变成 vertex/source coords。

## 源码与测试同步

`cpp/plugins/DrawDeviceD3D.cpp` 增强地址无关注释：

- 明确 Image、TransformLayer 与 listener owner 都是 raw borrower；
- 记录三指针、16-byte tuple、clear-preserves-capacity 与满容量倍增；
- 记录 typed Factory 的 one-Void、strict order、surplus 和 constructor publication顺序；
- 记录 non-sticky attach、attach-failure cleanup、populated re-entry leak、no producer/no unload。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 扩充 registration ABI 回归：

- 恰好一个 Void 创建 empty D3DPicture shell；
- empty shell 的 native payload 为 null，`opacity` getter 返回 `TJS_E_NATIVECLASSCRASH`；
- `Void + D3DImage` 进入 strict D3DLayer converter并抛出；
- arg1 传 D3DLayer 进入 strict D3DImage converter并抛出；
- valid `(D3DLayer,D3DImage)` 加 surplus 仍成功并 attach native。

旧总专题
`analysis/motionplayer_drawdevice_class_identity_ncb_surface_lifecycle_four_binary_2026-08-15.md`
同步修正 named/result/strict/surplus/rollback/re-entry、Android armv7 exception leak 和 vector growth。

## recovery IDB 写回

四份 `out/ida-recovery/` 数据库已原地保存并关闭：

- 8 个最终 typed data item：4 个 independent ClassInfo + 4 个整宽 guard；
- 8 个新 ABI 类型：每库一份 ClassInfo 和一份 concrete adaptor；
- 85 个 stripped/default 函数迁移为 D3DPicture/ClassInfo/adaptor/factory/vector `_guess` 名；
- 65 个函数/global type application；
- 94 个 ClassInfo、registration、factory、exception、listener、vector、Draw 注释地址；
- 16 个 V205 bookmark，每库 4 个；
- 1 个 A64 stale function boundary split，恢复 D3DImage set helper 与 D3DPicture vector helper。

最终保存目标：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`；
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`；
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`；
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`。

## 验证

最终验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均通过；首次检查准确发现私有 `D3DPicture` C++ 类型不能从测试 TU
  直接引用，回归改为 public script ABI（empty `opacity` crash / valid property success）后两套
  都成功，输出只有既有 `_tss` literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 都完成最终链接，只有既有
  `_tss`、pthread/memory-growth、JSPI 与 JS-library warning；并行初始化 headless 环境时还出现
  一条 `emsdk_set_env.ps1` 已不存在的 Remove-Item 诊断，但 build 本身 0 退出；
- Node `WebAssembly.Module` 对两份 `index.wasm` 解析成功；Web imports/exports `539/69`，
  headless `538/69`；
- 最终 artifact：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

两份 artifact 的 size、hash、imports/exports 和 section 与 V204/V203 精确一致；本轮生产源码
只有注释增强，新增回归不进入最终插件产物。两套 `ctest --output-on-failure` 均 0 退出但报告
`No tests were found!!!`，因此回归当前由两套完整 TU 编译与最终双链接保障，不表述为已运行
Catch2 runner。`git diff --check` 0 退出，只有 dirty worktree 中既有 LF→CRLF warning，无新增
trailing-whitespace error。

## 未外推边界

- 不把正确 class 的 empty native adaptor视为 factory invalid；converter 明确返回 null native；
- 不用三端 raw-storage cleanup“修复” Android armv7 leak；
- 不为 populated receiver re-entry 自动 delete旧 picture或去重 listener；
- 不把 D3DLayer list 或 D3DImage raw pointer误写成 owner/AddRef；
- 不把 stored scale补进 Draw；
- 不因 vector family 不同就发明合法状态下的 capacity 差异；
- 不把 generated Unregist 描述成实际 runtime unload；
- 不把 corrected recovery name 当成原始私有源码拼写，仍保留 `_guess`。
