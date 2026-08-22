# motionplayer `D3DLayer` ClassInfo、Factory、adaptor、listener owner 四端恢复

日期：2026-08-17
纵切面：V203

## 范围

本纵切面只以 `reference/binaries/` 中四个当前发布物为事实源：

- Android arm64-v8a；
- Android armeabi-v7a；
- iOS arm64；
- iOS armv7。

目标是把此前已经恢复的 `D3DLayerObject` 布局/listener 容器，继续向外闭合到：

- `global.D3DLayer` 独立 NCB ClassInfo 与静态初始化；
- generated `Regist` / `Unregist` 事务和真实可达性；
- raw native factory 与 descriptor wrapper 的完整错误边界；
- concrete owner adaptor、`D3DLayerBase` root adaptor、`D3DLayerObject` borrowed adaptor
  三种身份；
- concrete payload 的构造、父子挂接、listener list 和析构顺序；
- typed consumer、existing-native producer 的存在性；
- 重入、回调修改容器、root/layer/listener 相对寿命和 no-unload 边界。

旧 `libkrkr2.so` 的单目标注释没有被当作证据。四份二进制均 stripped，因此恢复的私有
名称继续保留 `_guess`。

## 结论摘要

1. `D3DLayer` 有自己独立的 ClassInfo：LP64 为 `0x20`，ILP32 为 `0x10`。它不是
   `D3DLayerBase` 的 root native class，也不是 `D3DLayerObject` 的 borrowed lookup class。
2. 四端 ClassInfo guard 的地址紧随 ClassInfo，但 guard 宽度随 ABI/compiler 为
   `8/4/8/4`，不是一律一字节；初始化器只读取低位，却执行整宽写 1。
3. raw factory 只消费 arg0：零参数返回 `TJS_E_BADPARAMCOUNT`；非 object、无 root adaptor、
   root payload null 都返回 `TJS_E_INVALIDTYPE`；合法 root 后面的 surplus 全部忽略。
4. descriptor wrapper 把“恰好一个 Void”保留为 empty-adaptor sentinel。合法 native 结果会
   raw-attach 到 concrete adaptor，保持 non-sticky；receiver 缺失/错误时先销毁刚构造的 layer，
   再返回 `TJS_E_NATIVECLASSCRASH`。
5. wrapper 对 populated receiver 只是覆盖 payload pointer，不删除旧 layer。因此 constructor
   re-entry 泄漏旧 concrete payload，并留下旧 `D3DLayerObject` borrowed adaptor/root 节点链。
6. concrete adaptor 是 `D3DLayer` 的唯一 owner。构造期额外注册的 `D3DLayerObject` adaptor
   只有 `vptr + borrowed pointer`，注册状态被忽略，失效/析构不删除也不清空 layer。
7. 没有找到 `CreateAdaptor(existing D3DLayer *)` producer。typed D3DEmotePlayer 和
   D3DPicture 只消费 concrete D3DLayer adaptor；正常 native payload 的唯一 producer 是 raw
   D3DLayer factory wrapper。
8. listener list 允许重复节点，remove 一次删除所有匹配节点。通知循环直接走 live list；回调
   删除当前 listener 会使后续迭代落入 iterator UAF 边界，四端都没有 snapshot/fence。
9. generated registrar vtable 均有相邻的 `Regist`/`Unregist` 槽，但 integrated loader 没有
   module-unload 或 registered-set erase caller；`Unregist` 是生成但不可达的清理路径。

## 三种必须分开的 class/adaptor 身份

### 1. concrete `D3DLayer` NCB ClassInfo

它服务于 `global.D3DLayer` 的脚本 class、empty concrete adaptor、factory attach、typed unbox。
普通 concrete adaptor 保持 non-sticky，并拥有 `D3DLayer` payload。

### 2. `D3DLayerBase` root adaptor

raw D3DLayer factory 的 arg0 只通过这枚 native class ID 解出 `DrawDeviceObjectBase *`。该
adaptor 是 `instance + sticky` 的独立借用视图，由 root 主基类构造注册和置 sticky；它不等于
arg0 的 concrete root class adaptor。

V208 进一步证明该内部身份有完整 `ncbClassInfo<DrawDeviceObjectBase>` tuple，但没有 global
class object；PreRegist 直接注册 ID，helper 先发布 native 再 REGISTER，caller 忽略失败 bool 并
严格 GET/置 sticky。existing native-null adaptor 会保留旧 sticky。精确失败矩阵见
`motionplayer_d3dlayerbase_classinfo_preregist_adaptor_sticky_failure_four_binary_2026-08-17.md`。

### 3. `D3DLayerObject` borrowed adaptor

`D3DLayerObject` 基类构造在 script owner 非 null 时注册这枚 adaptor。LP64 大小 `0x10`、
ILP32 大小 `0x08`，只有 vptr 和 borrowed payload。它不参与 concrete typed unbox，也不拥有
业务对象。

把这三者合并会产生三类错误：

- factory 用 concrete D3DLayer ID 去解 root，导致合法参数失败；
- borrowed adaptor 删除 layer，和 concrete adaptor 形成双 owner；
- concrete adaptor错误置 sticky，导致 layer 永不释放。

## ClassInfo ABI 与静态初始化

### 四端定位

| 目标 | ClassInfo | concrete class ID 字段 | guard | static init |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1AAF620` | `0x1AAF630` | `0x1AAF640` | `0x42CA88` |
| Android armv7 | `0x110E1E4` | `0x110E1EC` | `0x110E1F4` | `0x2FEF44` |
| iOS arm64 | `0x101AEE440` | `0x101AEE450` | `0x101AEE460` | `0x10024C9B0` |
| iOS armv7 | `0x1838E60` | `0x1838E68` | `0x1838E70` | `0x24E5A4` |

### LP64 布局

```cpp
struct D3DLayer_NCB_ClassInfo_guess {
    bool initialized;                 // +0x00
    unsigned char padding0[7];
    const tjs_char *className;        // +0x08
    tjs_int32 classID;                // +0x10
    unsigned char padding1[4];
    iTJSDispatch2 *classObject;       // +0x18
};                                    // 0x20
```

### ILP32 布局

```cpp
struct D3DLayer_NCB_ClassInfo_guess {
    bool initialized;                 // +0x00
    unsigned char padding0[3];
    const tjs_char *className;        // +0x04
    tjs_int32 classID;                // +0x08
    iTJSDispatch2 *classObject;       // +0x0C
};                                    // 0x10
```

### guard 宽度

| 目标 | guard 最终 data item | 读取 | 写入 |
|---|---:|---|---|
| Android arm64 | `0x08` | bit 0 | 64-bit `1` |
| Android armv7 | `0x04` | low byte/bit 0 | 32-bit `1` |
| iOS arm64 | `0x08` | bit 0 | 64-bit `1` |
| iOS armv7 | `0x04` | low byte/bit 0 | 32-bit `1` |

仅凭 `LDRB`/低位测试把 guard 建成 byte 会把整宽 store 覆盖到伪造的邻接全局。四库 readback
已按最终写宽分别建立 8/4 字节 data item。

Android 两端保留独立 ClassInfo leaf：

| 语义 | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x52CDEC` | `0x4933EC` |
| GetID | `0x52CDFC` | `0x4933F8` |
| GetClassObject | `0x52CE0C` | `0x493404` |
| IsSubClass | `0x52CE1C` | `0x493410` |
| Set | `0x52CE24` | `0x493414` |
| Clear | `0x52CE5C` | `0x49343C` |
| zero ctor | `0x52CE78` | `0x493450` |

`Set` 是 first-publication-wins：`initialized != 0` 时返回 false，不替换旧 name/ID/object。
`Clear` 清零四个逻辑字段。iOS 优化器把这些 leaves 全部内联，但 static initializer、registrar、
factory wrapper 和 typed unbox 对同一 tuple 的访问仍闭合了相同布局。

## registration 事务与 dormant Unregist

### 入口与 vtable 槽

| 目标 | member registrar | Regist | Unregist | registrar vtable Regist/Unregist |
|---|---:|---:|---:|---:|
| Android arm64 | `0x52CE8C` | `0x53B9AC` | `0x53BB10` | `0x19FE260` / `0x19FE268` |
| Android armv7 | `0x49345C` | `0x49E890` | `0x49E914` | `0x10ACB4C` / `0x10ACB50` |
| iOS arm64 | `0x100231618` | `0x10023F0A8` | `0x10023F110` | `0x101AF1F28` / `0x101AF1F30` |
| iOS armv7 | `0x230408` | `0x23E83C` | `0x23E8F0` | `0x183ABCC` / `0x183ABD0` |

Android armv7 vtable 存的是 Thumb 函数指针 `entry + 1`。因此对偶函数入口的普通 xref 为空；
搜索小端 `0x49E891` / `0x49E915` 后，分别在 `0x10ACB4C` / `0x10ACB50` 找到真正槽位。

共同源级事务可表示为：

```text
Regist(registrar):
    state = { registrar.classObject, registering = true }
    RegistBegin(state)
    registerD3DLayerMembers(state)
    finish(state)                 // RegistEnd

Unregist(registrar):
    state = { registrar.classObject, registering = false }
    registerD3DLayerMembers(state)
    finish(state)                 // UnregistEnd
```

iOS 两端显式保留 registration-state RAII dtor 与 flag dispatcher：

| 语义 | iOS arm64 | iOS armv7 |
|---|---:|---:|
| state dtor | `0x10023F3A4` | `0x23EBA4` |
| finish dispatcher | `0x10023F3CC` | `0x23EC34` |
| RegistEnd | `0x10023F3E0` | `0x23EC44` |
| UnregistEnd | `0x10023F4DC` | `0x23ED78` |

异常清理也经过同一 state dtor，所以会按保存的 registering flag 进入相应 End。Android 两端
把部分 end/clear 逻辑折叠到了外层函数，但 member table 和状态差异相同。

四端 loader 闭包只有 load/publish 路径：`DrawDeviceD3D.dll` integrated registration bundle
构造 registrar 并调用 Regist。全插件搜索没有 module unload、registered module set erase 或
registrar Unregist virtual call。因此不能因为生成了 Unregist body，就假设脚本 class、ClassInfo
或已存在 adaptor 会在运行中被回收。

## raw D3DLayer factory

### 四端定位与 native 大小

| 目标 | raw factory | D3DLayer ctor | allocation |
|---|---:|---:|---:|
| Android arm64 | `0x52D308` | `0x5333F0` | `0x90` |
| Android armv7 | `0x49361C` | `0x496E0C` | `0x74` |
| iOS arm64 | `0x1002317E8` | `0x1002359AC` | `0x98` |
| iOS armv7 | `0x230594` | `0x234770` | `0x78` |

factory arg0 查询的 root class ID 与 concrete ID 明确不同：

| 目标 | concrete D3DLayer ID 字段 | D3DLayerBase/root ID 字段 |
|---|---:|---:|
| Android arm64 | `0x1AAF630` | `0x1AAF6F8` |
| Android armv7 | `0x110E1EC` | `0x110E250` |
| iOS arm64 | `0x101AEE450` | `0x101AEE518` |
| iOS armv7 | `0x1838E68` | `0x1838ECC` |

共同伪代码：

```cpp
tjs_error D3DLayer_factory_guess(
    D3DLayer **out,
    tjs_int argc,
    tTJSVariant **argv,
    iTJSDispatch2 *objthis) {
    if(argc < 1)
        return TJS_E_BADPARAMCOUNT;

    if(argv[0]->Type() != tvtObject)
        return TJS_E_INVALIDTYPE;

    iTJSDispatch2 *rootObject = argv[0]->AsObjectNoAddRef();
    D3DLayerBaseAdaptor *rootAdaptor =
        query(rootObject, D3DLayerBaseClassID);
    if(!rootAdaptor || !rootAdaptor->instance)
        return TJS_E_INVALIDTYPE;

    *out = new D3DLayer(objthis, rootAdaptor->instance);
    return TJS_S_OK;
}
```

factory 不读取 `argv[1..]`。四端因此共同接受任意 surplus，并保持 arg0 的转换/查询顺序。
`new` expression 在 constructor 抛异常时回收尚未发布的 raw storage；只有构造和写回都完成才
把 native pointer 交给 descriptor wrapper。

## descriptor wrapper 的完整状态机

| 目标 | factory FuncCall wrapper | concrete unbox helper |
|---|---:|---:|
| Android arm64 | `0x53C2A0` | `0x542CB8` |
| Android armv7 | `0x49EDE4` | `0x49EE98` |
| iOS arm64 | `0x10023F7DC` | `0x10023F8C0` |
| iOS armv7 | `0x23F114` | `0x23F19E` |

共同伪代码：

```cpp
tjs_error D3DLayerFactoryDescriptor::FuncCall(
    const tjs_char *membername,
    tTJSVariant *result,
    tjs_int argc,
    tTJSVariant **argv,
    iTJSDispatch2 *objthis) {
    if(membername != nullptr)
        return TJS_E_MEMBERNOTFOUND;

    if(argc == 1 && argv[0]->Type() == tvtVoid)
        return TJS_S_OK;              // preserve empty concrete adaptor

    D3DLayer *fresh = nullptr;
    tjs_error er = rawFactory(&fresh, argc, argv, objthis);
    if(TJS_FAILED(er))
        return er;

    ConcreteAdaptor *adaptor =
        query(objthis, concreteD3DLayerClassID);
    if(!objthis || !adaptor) {
        delete fresh;
        return TJS_E_NATIVECLASSCRASH;
    }

    adaptor->instance = fresh;        // no release of previous value
    return TJS_S_OK;
}
```

### 可观察边界矩阵

| 输入/状态 | 结果 | native factory | native payload |
|---|---|---|---|
| `membername != null` | `TJS_E_MEMBERNOTFOUND` | 不调用 | 不变 |
| exactly one Void | `TJS_S_OK` | 不调用 | empty/null |
| argc 0 | `TJS_E_BADPARAMCOUNT` | 调用 | 不产生 |
| arg0 non-object | `TJS_E_INVALIDTYPE` | 调用 | 不产生 |
| arg0 object，无 root adaptor | `TJS_E_INVALIDTYPE` | 调用 | 不产生 |
| root adaptor payload null | `TJS_E_INVALIDTYPE` | 调用 | 不产生 |
| 合法 root + surplus | `TJS_S_OK` | 只读 arg0 | 新 layer |
| fresh layer，但 objthis/null 或 concrete adaptor 缺失 | `TJS_E_NATIVECLASSCRASH` | 已完成 | fresh 被删除 |
| populated concrete adaptor re-entry | `TJS_S_OK` | 已完成 | 旧 layer 泄漏，新 layer 覆盖 |

恰好一个 Void 是 sentinel；`Void + surplus` 不再命中 sentinel，会进入 raw factory，并因 arg0
不是 object 返回 `TJS_E_INVALIDTYPE`。

wrapper 不写 script result。`CreateNew` 的返回对象来自外层 native class machinery，而不是这个
factory descriptor 把 native pointer装进 Variant。

## concrete adaptor 生命周期

standard concrete adaptor 的自然布局是：

```text
LP64: vptr + D3DLayer *instance + bool sticky + padding = 0x18
ILP32: vptr + D3DLayer *instance + bool sticky + padding = 0x0C
```

相关生成函数：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateEmptyAdaptor | `0x53BDBC` | `0x49EA78` | `0x10023F284` | `0x23EAFC` |
| Invalidate | `0x53BDF0` | `0x49EA9C` | `0x10023F2B8` | `0x23EB20` |
| complete dtor | `0x53BE30` | `0x49EAB8` | `0x10023F2F8` | `0x23EB3C` |
| deleting dtor | `0x53BE8C` | `0x49EAF4` | `0x10023F358` | `0x23EB76` |

empty adaptor 是真实脚本 shell + null native payload。Invalidate/dtor 只在
`instance != null && sticky == false` 时删除 concrete payload。ordinary factory attach 从不写
sticky，所以 adaptor 正常失效时会删除 layer。

本纵切面从 concrete class ID 的所有 producer/xref 反查，没有找到
`CreateAdaptor(existing D3DLayer *, sticky, ...)` 路径。D3DEmotePlayer typed factory/clone 的
`D3DLayer` 参数 converter，以及 D3DPicture 的 typed/raw 参数路径，都只是 consumer。

## native constructor、borrowed adaptor 和 root 挂接

`D3DLayerObject` 基类构造的共同顺序：

1. 写 base vptr；
2. 保存 borrowed script owner；
3. `Parent = nullptr`；
4. `FrontIndex = 0`、`BackIndex = 0`、`DrawPlane = 1`；
5. 构造空 listener list；
6. owner 非 null 时分配 `vptr + this` borrowed adaptor；
7. 用独立 `D3DLayerObject` class ID 注册，忽略返回状态。

派生 `D3DLayer` 随后：

1. 写 derived vptr；
2. `Visible = true`；
3. 四个 clip scalar 清零；
4. 构造/复制 identity `cocos2d::Mat4`；
5. 经统一 parent setter 挂到 root 的 front/back 两个 multiset。

构造不是“先保存 parent 再注册 borrowed adaptor”。borrowed adaptor 在 Parent 尚为 null、派生字段
尚未完成时已经对 script owner 可见。注册失败不会撤销 adaptor allocation，也不会阻止 constructor
继续；这是四端共同的原始失败边界。

## D3DLayerObject/listener 容器 ABI

共同 base 字段：

| 字段 | LP64 | ILP32 | 所有权 |
|---|---:|---:|---|
| vptr | `+0` | `+0` | ABI |
| script owner | `+8` | `+4` | borrowed |
| Parent root | `+16` | `+8` | borrowed |
| FrontIndex | `+24` | `+12` | int32 |
| BackIndex | `+28` | `+16` | int32 |
| DrawPlane | `+32` | `+20` | int32，初值 1 |
| listener list | `+40` | `+24` | non-owning pointer nodes |

STL ABI 差异：

| 目标 | list 表示 | D3DLayerObject base size | node size |
|---|---|---:|---:|
| Android arm64 | `next, prev` circular sentinel | `0x38` | `0x18` |
| Android armv7 | `next, prev` circular sentinel | `0x20` | `0x0C` |
| iOS arm64 | `next, prev, size` | `0x40` | `0x18` |
| iOS armv7 | `next, prev, size` | `0x24` | `0x0C` |

node payload 只是 `D3DLayerListener *`，list 不拥有 listener。

### add/remove

```text
AddListener(null): no-op
AddListener(x): push_back(x), duplicates allowed

RemoveListener(null): no-op
RemoveListener(x): erase every node whose payload == x
```

remove 与 `std::list::remove` 一致，不是只删第一项。listener 构造自动 add 一次，调用者再显式 add
会留下重复节点；一次 remove 会把全部重复项清掉。

### 通知循环的 mutation 边界

`NotifyMatrixChanged`、`OnUpdate` 和 draw fan-out 都直接遍历 live list：

```cpp
for(D3DLayerListener *listener : Listeners)
    listener->IsVisible();
```

没有复制 pointer vector、没有预取稳定 next、没有 re-entrancy flag。若 callback 析构当前 listener，
listener dtor 会调用 `RemoveListener(this)`，删除当前节点以及所有重复节点；外层 iterator 随后的
increment 会访问已释放 node。参考实现因此要求回调期间不能删除当前 listener，portable 端不能擅自
用 snapshot 改变重复节点、追加节点和删除节点的可见性。

## 析构顺序与相对寿命

### concrete D3DLayer 析构

| 目标 | complete dtor |
|---|---:|
| Android arm64 | `0x5335AC` |
| Android armv7 | `0x496E6C` |
| iOS arm64 | `0x100235A38` |
| iOS armv7 | `0x234858` |

共同顺序：

1. 写回 derived/base 析构阶段 vptr；
2. 析构 `Mat4`；
3. 进入 `D3DLayerObject` dtor；
4. 若 Parent 非 null，从 root 两个 multiset 各移除至多一个匹配节点并触发 detach/change hook；
5. 清空 listener list 节点；
6. 结束 base dtor。

它不做以下任何操作：

- 不查询 script owner 上的 `D3DLayerObject` borrowed adaptor；
- 不把 borrowed adaptor payload 清零；
- 不把各 listener 的 owner backpointer 清零；
- 不删除 listener；
- 不 AddRef/Release Parent root。

因此存在明确的相对寿命约束：

- root 必须活到 child layer 从 multiset 自行脱离之后；
- listener 必须先于 layer 析构，否则 listener dtor 会通过 dangling layer 调 `RemoveListener`；
- script owner 若先于 concrete adaptor 失效，可删除 borrowed adaptor 本身，但不会影响 layer；
- layer 若先销毁，仍存活的 borrowed adaptor 会保留 dangling payload，后续查询即悬垂。

### constructor re-entry 的扩散泄漏

在 populated receiver 上再次走 factory wrapper 时，旧 concrete layer 不被删除。旧 layer 仍可能：

- 留在 root front/back multiset；
- 持有 listener list nodes；
- 被 script owner 上旧 borrowed adaptor 指向；
- 持有 Parent/root borrowed pointer。

新 layer 构造还会尝试在同一 script owner 上注册新的 borrowed adaptor。注册的 replace/失败语义属于
TJS native-instance map，而 concrete wrapper 对这些副作用不做 rollback。该行为不能简化成“只泄漏
一块平坦内存”。

## typed consumer 边界

concrete unbox helper 的共同逻辑：

```text
Variant must be object
  -> query object by concrete D3DLayer class ID
  -> query/adaptor failure follows generated conversion failure path
  -> adaptor exists: return adaptor.instance (may be null for empty shell)
```

它不会退回查询 `D3DLayerObject` borrowed ID，也不会接受只带 `D3DLayerBase` root ID 的 object。
这保证 D3DEmotePlayer/D3DPicture 的 typed layer 参数绑定到 concrete class identity，而不是任意可绘制
对象。

## 源码与测试恢复

本纵切面没有改变已对齐的 executable control flow；修改集中在 provenance/边界注释和回归覆盖：

- `cpp/plugins/DrawDeviceD3D.cpp`
  - 明确三种 class/adaptor identity；
  - 记录 raw factory surplus、outer attach/failure/re-entry；
  - 记录 concrete destructor 不 detach borrowed adaptor；
  - 记录 live listener list 回调修改的 iterator UAF；
  - 记录 generated Unregist 存在但 integrated loader 不可达。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 验证 exactly-one Void 创建 empty concrete shell；
  - 验证 `Void + surplus` 不命中 sentinel，而返回 `TJS_E_INVALIDTYPE`；
  - 验证合法 root + surplus 正常构造，concrete native payload 非 null。
- `analysis/motionplayer_drawdevice_multiple_inheritance_vtables_completion_lifecycle_four_binary_2026-08-15.md`
  - 把旧“由本地 ncbind.hpp 推断 non-sticky attach”升级为四份 descriptor wrapper 的直接证据。

## recovery IDB 写回

四份 `out/ida-recovery/` 数据库均已原地保存：

- 8 个最终 data item：4 个 ClassInfo + 4 个整宽 guard；
- 4 个跨 ABI `D3DLayer_NCB_ClassInfo_guess` 类型；
- 76 个从 stripped/default 名迁移到 `_guess` 语义名的函数；
- 34 个函数签名应用；
- 66 个 ClassInfo、factory、adaptor、vtable、ctor/dtor 和 owner 边界注释地址；
- 4 个 V203 bookmark，均落在 concrete factory/adaptor attach 边界；
- 100 次针对受影响函数的 Hex-Rays cache invalidate/readback 请求，全部成功。

最终 readback 逐库确认：

- ClassInfo size 为 `0x20/0x10/0x20/0x10`；
- guard data item size 为 `0x08/0x04/0x08/0x04`；
- descriptor pseudocode 直接读取命名后的 concrete `classID` 字段；
- raw factory 仍读取不同的 root class ID；
- 四库保存成功。

## 验证

本轮完整验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整 motionplayer unit-test TU
  `em++ -fsyntax-only` 均成功；新增 empty/surplus case 在两种配置中都编译，输出只有既有
  `_tss` literal-operator warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均完成最终链接；因注释
  timestamp 只重编译 `DrawDeviceD3D.cpp` 和对应 guest object，输出只有既有 `_tss`、
  pthread/memory-growth、JSPI 和 JS-library warning；
- Node `WebAssembly.Module` 对两份最终 `index.wasm` 都解析成功；Web imports/exports 为
  `539/69`，headless 为 `538/69`；
- 精确 artifact：

| 产物 | 文件大小 | SHA-256 | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---|---:|---:|---:|---:|---:|
| Web | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` | `0x1BD2F` | `0xD5B2` | `0x1A427D0` | `0x5A4017` | `0x3185E3C` |
| Headless | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` | `0x1BA4E` | `0xD5DA` | `0x19EA77E` | `0x5A1267` | `0x3141CD2` |

两份大小、hash、imports/exports 和全部表列 section 与 V202/V201 精确相同，符合“插件
executable source 未变、测试不进入最终 artifact”。两套
`ctest --output-on-failure` 均以 0 退出，但仍报告 `No tests were found!!!`；新增回归的当前
保障是两套完整 TU 编译和最终双链接，不虚构已经登记的 Catch2 runner。

## 未外推的边界

- stripped 产物不能证明原始私有 helper 拼写，恢复名保留 `_guess`；
- 本纵切面不把 Android 和 iOS 不同 STL 实现强行统一成一个二进制布局；
- 不因为 `Unregist` body 存在就发明 runtime unload；
- 不修复 populated re-entry leak、dangling borrowed adaptor、root/listener 相对寿命或 callback
  iterator UAF，因为这些都是四端共同的可观察边界；
- `D3DImage`、`D3DPicture` 各自独立 ClassInfo/factory/adaptor producer 和 managed-set/listener owner
  拓扑仍需下一纵切面重新从四端审计，不能从 D3DLayer 机械外推。
