# D3DEmotePlayer 壳层工厂、双槽生命周期与 clone 四参考二进制复核（2026-08-12）

## 1. 范围与旧结论更正

本轮只处理 `D3DEmotePlayer` 壳层从 NCB 注册到销毁的完整纵向链：typed
factory、listener base、`clear`、raw `load`、typed `clone`、`EmoteObject`
clone、result slot、adaptor boxing 以及失败边界。证据全部重新取自
`reference/binaries/` 的四份当前数据库；旧 `libkrkr2.so` 地址不参与定位。

本轮推翻了两项已有本地/分析结论：

1. factory 不是手写 `tjs_error` raw factory，而是返回
   `D3DEmotePlayer*` 的 typed static factory；参数由 ncbind 按 `D3DLayer*`
   解箱，最后一个隐藏源码参数是 constructor `objthis`。
2. `clone` 不是无参、也不是自动沿用旧壳 owner。它有一个必需的
   `D3DLayer* targetOwner` 参数，新壳注册到调用者明确传入的目标 layer。

> **2026-08-17 owner 类型更正：** 本文最初虽然正确恢复了 typed wrapper、壳布局与
> listener 生命周期，却把 class-ID state 误归给相邻 `D3DImage`。四端 factory/clone
> unboxer 实际读取 `D3DLayer` 的独立 class ID；以下类型名已统一更正。

因此 `analysis/motionplayer_lifecycle_four_binary_2026-08-11.md` 第 14 节中
“clone 使用 old.owner”和“clone 无参”的描述被本文取代；其中已确认的双槽
destroy 顺序与 EmoteObject serialize/unserialize 结论仍然成立。

## 2. 四端函数映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| member registrar | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |
| `clear` native body | `0x530164` | `0x4948C4` | `0x100232C1C` | `0x231840` |
| raw `load` callback | `0x5301B4` | `0x494920` | `0x100232CB0` | `0x231890` |
| typed `clone` body | `0x53039C` | `0x4949D4` | `0x100232DC8` | `0x2319DC` |
| EmoteObject clone | `0x67CD58` | `0x5611FC` | `0x1001B50A4` | `0x1B4CFC` |
| shell ctor | clone/factory 内联 | `0x497824` | `0x100236300` | `0x235022` |
| shell dtor | `0x533FE0` | `0x497870` | `0x100236374` | `0x235076` |
| deleting dtor | `0x534078` | `0x497894` | `0x1002363A8` | `0x23509A` |
| listener-base dtor | dtor 内联 | `0x497988` | `0x1002364C4` | `0x235164` |
| typed factory FuncCall | `0x542A6C` | 外层模板合并/未单列 | 外层模板合并/未单列 | 外层模板合并/未单列 |
| typed factory invoke | `0x542B44` | `0x4A4080` | `0x100245DC0` | `0x2465B8` |
| D3DLayer arg unbox | `0x542CB8` | `0x49EE98` | `0x10023F8C0` | `0x23F19E` |
| raw-load FuncCall | registrar 内联模板 | `0x4A4828` | `0x100246784` | `0x2470E4` |
| typed-clone FuncCall | registrar 内联模板 | `0x4A49B8` | `0x1002469B8` | `0x247364` |

Android arm64 registrar 把多个小型 NCB method object 构造和部分 wrapper 逻辑内联进
`0x52E8E4`；另外三端保留了可独立读取的 load/clone FuncCall。该差异只影响
编译器函数边界，不影响注册类型。

## 3. member registration 的真实类型

四份 registrar 对三个成员的绑定一致：

```cpp
NCB_METHOD(clear);
NCB_METHOD_RAW_CALLBACK(load, &D3DEmotePlayer::loadCompat, 0);
NCB_METHOD(clone);
```

- `clear` 使用普通 typed `void (D3DEmotePlayer::*)()` method object；不是
  “TJS 名叫 clear、C++ 方法误叫 create”的原版 bug。旧本地 `create()` 只是端口命名。
- `load` 的 callback 类型是
  `tjs_error (*)(tTJSVariant*, tjs_int, tTJSVariant**,
  D3DEmotePlayer*)`。最后一项是 wrapper 预先取得的 native instance，不是
  `iTJSDispatch2 *objthis`。
- `clone` 的 method type 是
  `D3DEmotePlayer *(D3DEmotePlayer::*)(D3DLayer*)`，所以脚本端必须提供一个
  D3DLayer 参数，返回值由普通 strict native-object boxing 处理。

## 4. typed factory 与 D3DLayer 解箱

最接近四端机器行为的源码形态是：

```cpp
static D3DEmotePlayer *factory(iTJSDispatch2 *objthis,
                               D3DLayer *owner) {
    return new D3DEmotePlayer(owner);
}
```

`objthis` 位于源码参数表首位，是 ncbind `paramsFunctorWithInstance` 注入的
constructor object；脚本只提供 `owner`。这解释了四端 wrapper 的
`ArgsCount - 1 == 1`、arg0 的 `D3DLayer` class descriptor 解箱，以及返回指针
随后写入 `D3DEmotePlayer` adaptor 的数据流。

### 4.1 FuncCall/Invoke 顺序

共同边界为：

```text
if membername != null: delegate to base and return
if objthis == null: return TJS_E_NATIVECLASSCRASH
if argc == 1 and arg0 is Void: return OK without constructing

clear result Variant if result != null
if argc < 1: return TJS_E_BADPARAMCOUNT
owner = unbox arg0 through ncbInstanceAdaptor<D3DLayer>
raw = operator new(sizeof(D3DEmotePlayer))
shell = construct raw D3DEmotePlayer(owner)
if SetNativeInstance(objthis, shell) fails:
  delete shell
  return TJS_E_NATIVECLASSCRASH
return OK
```

这里“一参数 Void”是 ncbind 为 `CreateAdaptor` 保留的通用空实例路径，发生在 result
clear 与 arity gate 之前。普通少参调用则先清 result，再返回 `-1004`。

### 4.2 标准异常边界

参数转换不做本地手写 `Type()==tvtObject` 分支，而是：

```text
Variant.AsObjectNoAddRef()
ncbInstanceAdaptor<D3DLayer>::GetNativeInstance(obj, true)
```

因此：

- 非 Object、Void 或 null object 进入标准 `No instance.` 异常；
- Object 但没有 D3DLayer native adaptor 进入 `Invalid instance type.` 异常；
- 四端都先为 C++ `new` expression 分配 `0x38/0x24` raw storage，再做 D3DLayer
  参数转换；转换或构造抛出时，new-expression 的 EH cleanup 会销毁临时 Variant 并
  `operator delete(raw)`，因此不会注册 listener，也不会泄漏未构造 storage；
- adaptor 安装失败发生在壳构造和 listener 注册之后，但 typed factory 会调用
  deleting destructor，因而先拆双槽、再注销 listener、最后释放 shell storage。

该机器顺序不是源码把 `operator new` 手写在解箱前，而是标准 new-expression 的分配与
initializer 求值顺序。四端 cleanup 证据分别位于 Android arm64 `0x542C70..0x542CB0`、
Android armv7 landing pad `0x4A413C..0x4A4174`、iOS arm64 `0x100245ED0` 和 iOS armv7
`0x2466DA`。它与 clone body 的异常边界不同：clone 已把新壳交给裸局部指针，后续 primary
clone 抛出时没有同类 owner cleanup，仍会泄漏壳及 listener 注册。

旧手写 raw factory 没有精确复制 result clear、one-void suppression 和 typed factory
安装失败清理，已删除。

## 5. listener base 与壳对象布局

四端 ctor 证明 D3DEmotePlayer 的前缀是有状态 listener base，而不只是三虚函数
interface：

```text
D3DLayerListener base:
  vptr
  non-owning D3DLayer* owner
  int tag = 8
  float bias = -0.5f

D3DEmotePlayer derived fields:
  EmoteObject* primary = null
  EmoteObject* secondary = null
  float baseScale = 1.0f
  float userScale = 1.0f
  bool visible = false
  bool smoothing = false
```

| ABI | base 大小 | primary/secondary | scalar/flag | shell 总大小 |
| --- | ---: | --- | --- | ---: |
| 64-bit | `0x18` | `+0x18/+0x20` | `+0x28/+0x2C/+0x30/+0x31` | `0x38` |
| 32-bit | `0x10` | `+0x10/+0x14` | `+0x18/+0x1C/+0x20/+0x21` | `0x24` |

base ctor 先写 owner/tag/bias，再对非 null owner 调虚函数 `AddListener(this)`；派生
slot 与 scalar 随后初始化。base dtor 在派生双槽已经销毁后才调用
`RemoveListener(this)`。owner 从不 AddRef/Release，是纯借用指针。

> **2026-08-16 visible consumer 补充：** `show`、`hide` 与 visible property 只访问
> shell 的 `+0x30/+0x20` byte；listener `IsVisible()` 不读它，在可选 scale sync 后固定
> 返回 true，`Draw()` 也不读。该 byte 是脚本可观察但不控制当前渲染的兼容状态，不能
> 转发到 Player root。完整四端 target 与回归见
> `motionplayer_d3d_visibility_shell_only_four_binary_2026-08-16.md`。

本地曾在派生 ctor/dtor 手写 AddListener/RemoveListener，并把 owner 字段放在派生字段
尾部；这虽然能在部分调用上工作，却没有恢复原始基类源码拓扑或 field order。本轮把
owner/tag/bias 和注册生命周期移入 `D3DLayerListener`，同时使 D3DLayer 的公开原生类型
对 typed converter 可见。

## 6. `clear` 与析构

四端 `clear` 顺序严格为：

```text
if secondary:
  secondary.~EmoteObject()
  operator delete(secondary)
if primary:
  primary.~EmoteObject()
  operator delete(primary)
primary = null
secondary = null
```

> **2026-08-13 owner 类型补充：** 两个 null store 在两次完整 delete 之后，不是每个
> delete 后各自清 slot。因此 secondary 已释放到 primary 也释放完成之间，secondary
> member 暂时保留悬空旧地址；普通 `unique_ptr::reset` 无法保留该 pair 协议。两个字段
> 是 raw owner。详见
> `motionplayer_d3d_shell_raw_slot_protocol_four_binary_2026-08-13.md`。

它不改 scale、visible、smoothing 或 owner。析构的派生阶段复用/内联完全相同的
secondary→primary teardown；随后 base dtor 注销 listener。恢复后的源码让
`~D3DEmotePlayer()` 调 `clear()`，再由 C++ 自动调用 base dtor，保持这两个阶段。

## 7. raw `load` 数据流、提交点和失败状态

共同流程为：

```text
raw FuncCall:
  reject null objthis without clearing result
  clear result
  resolve D3DEmotePlayer native instance without throwing
  if missing: return TJS_E_NATIVECLASSCRASH
  callback(result, argc, argv, native)

native callback:
  clear()
  vector<ttstr> paths
  for i in [0, argc): paths.push_back(ttstr(*argv[i]))
  temporary = operator new(sizeof(EmoteObject))
  EmoteObject::EmoteObject(temporary, paths)
  primary = temporary
  destroy paths
  return TJS_S_OK
```

关键边界：

- `argc == 0` 合法，仍尝试构造 paths 为空的 EmoteObject；没有 BADPARAMCOUNT。
- result 在 receiver native-instance lookup 之前清除；null `objthis` 是唯一发生在
  result clear 之前的 instance gate。
- callback 自身不清 result。旧本地在成功 load 后才清 result，错误 receiver 与参数
  转换异常时的可观察状态均不对。
- `clear()` 发生在任意参数字符串转换之前。第 N 个参数转换抛异常时，旧双槽已经销毁，
  primary/secondary 保持 null。
- EmoteObject ctor 成功前不发布 primary。vector allocation、operator new 或
  EmoteObject 初始化异常都留下双槽 null。
- EmoteObject 自身的两个内部 owner 是 raw pointer：RM 或 Engine member 发布后发生的
  构造异常会泄漏已经发布的前缀；这里只能保证外层 shell 双槽仍为 null。四端 ctor
  unwind 详见 `motionplayer_emoteobject_raw_owner_ctor_failure_four_binary_2026-08-13.md`。
- secondary 不会由 load 填充。

## 8. typed `clone(targetOwner)` 与返回 boxing

native body 精确为：

```cpp
D3DEmotePlayer *D3DEmotePlayer::clone(D3DLayer *targetOwner) {
    auto *copy = new D3DEmotePlayer(targetOwner);
    copy->primary = primary->clone_guess();
    return copy;
}
```

没有 source-primary null guard。新 shell 的 owner 是 `targetOwner`，不是旧 owner；新
shell 构造立即注册到目标 D3DLayer。只迁移 primary：secondary 保持 null，两个 scale
保持 `1.0f`，visible/smoothing 保持 false。

EmoteObject clone 四端共同执行：

```text
copy = new EmoteObject(source.modulePaths)
state = source.engine.serialize()
copy.engine.unserialize(state)
destroy state Variant
return copy
```

因此 RM、Engine 和 Player 是重新分配的独立链；内部状态经 serialize/unserialize
迁移，不是字段白名单复制。

### 8.1 typed method wrapper 的 result 行为

wrapper 顺序是：

```text
if objthis == null: return native-class crash without clearing result
clear result
if argc < 1: return bad-param-count
resolve source native instance
unbox targetOwner as D3DLayer
copy = source.clone(targetOwner)
if result != null:
  adaptor = CreateAdaptor(copy, sticky=false)
  result = object Variant(adaptor, adaptor)
  adaptor.Release()
return OK
```

ncbind 的 strict result converter 只在 `result != nullptr` 时运行。因此
`result == nullptr` 的成功调用仍创建并返回 native copy，但 wrapper 不 box、不 delete
该指针：新壳和 listener 会泄漏。这是模板的原始边界，不应由 clone 内部手工创建临时
Variant“修复”。同样，`CreateAdaptor` 返回 null 后 `ncbNativeObjectBoxing::Boxing`
仍直接解引用 `adpobj->Release()`；当前模板没有安全回滚分支。

若新壳注册成功后 `EmoteObject::clone_guess()` 抛异常，native body 也没有 owning
临时量替它删除 shell；该 shell 及其已注册 listener 留下泄漏。这一边界来自裸指针
源码结构，未引入智能指针或本地 catch 修正。

## 9. 本地实施与回归覆盖

本轮实施：

- 将 `D3DLayer` 类型声明/方法声明提升到共享 D3D 接口，使 motionplayer translation
  unit 能实例化精确的 `D3DLayer` boxing/unboxing map；实现仍留在 DrawDeviceD3D.cpp。
- `D3DLayerObject` 保留与原 vtable 同序的 `IsVisible/Draw` slots，但不继承带状态的
  shell-listener base，避免错误引入 owner/tag/bias payload。
- 恢复有状态 `D3DLayerListener` ctor/dtor 与 `0x18/0x10` size assertion。
- 恢复 `D3DEmotePlayer` 字段顺序与 `0x38/0x24` size assertion。
- raw factory 改为 typed `factory(iTJSDispatch2*, D3DLayer*)`。
- `create()` 改名为真实 `clear()`，注册恢复 `NCB_METHOD(clear)`。
- load callback 末参改为 `D3DEmotePlayer*`，删除 callback 内 result clear 与二次
  native lookup。
- clone 改为 `D3DEmotePlayer *clone(D3DLayer*)`，删除内部手工 adaptor boxing。
- 新增 wrapper 级测试，覆盖 raw-load null/wrong receiver result 顺序、typed factory
  D3DLayer arity、typed clone arity/result clear、返回 adaptor、目标 owner 以及新壳默认
  flags。

## 10. IDB 改进

四库新增/细化的主要 `_guess` 名称：

- `D3DEmotePlayer_NCB_TypedFactory_FuncCall_guess`
- `D3DEmotePlayer_NCB_TypedFactory_invoke_guess`
- `D3DLayer_NCB_unboxArg_guess`
- `D3DEmotePlayer_load_NCBFuncCall_guess`
- `D3DEmotePlayer_clone_NCBFuncCall_guess`
- Android arm64 缺失的 `D3DEmotePlayer_deleting_dtor_guess`

已有 `clearSlots/load/clone/ctor/dtor/deleting-dtor` 函数补入 source-level prototype，
其中 clone 明确显示第二参数 `targetD3DLayer`，load 明确显示最后参数 `self`。registrar、
factory、unbox、clear/load/clone、ctor/dtor 和 wrapper 均写入 result/arity/lifetime 语义
注释，Hex-Rays cache 已强制失效并 fresh decompile。四份数据库随后原位保存成功。
