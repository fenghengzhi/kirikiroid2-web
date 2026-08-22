# Motion 顶层类/命名空间 NCB 表面、发布顺序与生命周期（四参考）

日期：2026-08-14

## 1. 本纵切面的结论

四个当前参考二进制中的 `Motion` 不是一个由若干全局类和后置别名拼装出来的普通
native class。它是 motionplayer 模块唯一全局发布的顶层 NCB class/namespace，对应的同一
registrar body 严格按下面的顺序运行：

1. 发布 23 个静态整数常量；
2. setup 并发布 11 个内嵌 subclass；
3. 发布两个 namespace free-function descriptor；
4. 因 registrar 内没有显式 constructor row，`RegistEnd` 追加一个名为 `Motion`、总是返回
   `TJS_E_NOTIMPL` 的 dummy constructor；
5. 最后才把完整 class object 写到 `global.Motion`。

因此：

- `Motion.Player` 是第六个 in-flow subclass，不存在先发布 `global.Player` 再做 alias 的步骤；
- `Motion` 自身不能由脚本成功实例化；
- `doAlphaMaskOperation` 和 `getD3DAvailable` 是 `Motion` 的静态 namespace method，不是
  `Motion.Player` method；
- 注册不是 transaction：registrar 抛异常时，EH cleanup 仍调用 `RegistEnd`，所以可能把
  已完成的成员前缀连同 dummy constructor 发布到 `global.Motion`；
- auto-register vtable 保留一个反注册 wrapper；**如果被调用**，它会按相同正向顺序删常量、
  setup(false)+删 subclass、删 method，最后删除 `global.Motion` 并清空 Motion class info。
  但四端当前集成式 loader 没有 unload/registered-set erase/`AllUnregist` 调用链；正常加载后
  Motion 与 11 个 subclass ClassInfo 均保持到进程退出。旧稿把模板能力写成实际 unload，现更正。

本结论来自以下四个参考，而不是旧 `libkrkr2.so` 地址注释：

| 简称 | 目标 |
|---|---|
| A64 | Android arm64-v8a |
| A32 | Android armeabi-v7a |
| I64 | iOS arm64 |
| I32 | iOS armv7 |

未知的 C++ 模板实例名继续以 `_guess` 表示。

## 2. 根 registrar

| 目标 | `Motion_ncb_register_guess` | 大小 |
|---|---:|---:|
| A64 | `0x6D6EE8` | `0x784` |
| A32 | `0x5991D0` | `0x1FC` |
| I64 | `0x100125974` | `0x328` |
| I32 | `0x124B7C` | `0x2EC` |

A64 将多个小型 subclass item publication 和两个 method descriptor 的构造内联到根
registrar；A32/I64/I32 保留了更多独立 helper。这是优化/ABI 差异，不是脚本表面差异。

共同源代码骨架为：

```cpp
class Motion {};

NCB_REGISTER_CLASS(Motion) {
    // 23 Variant rows
    // 11 NCB_SUBCLASS rows
    Method(TJS_W("doAlphaMaskOperation"), &motion_doAlphaMaskOperation);
    Method(TJS_W("getD3DAvailable"), &motion_getD3DAvailable);
    // deliberately no NCB_CONSTRUCTOR
}
```

## 3. 23 个常量：名称、值和严格顺序

| # | script name | 值 |
|---:|---|---:|
| 1 | `LayerTypeObj` | 0 |
| 2 | `LayerTypeShape` | 1 |
| 3 | `LayerTypeLayout` | 2 |
| 4 | `LayerTypeMotion` | 3 |
| 5 | `LayerTypeParticle` | 4 |
| 6 | `LayerTypeCamera` | 5 |
| 7 | `ShapeTypePoint` | 0 |
| 8 | `ShapeTypeCircle` | 1 |
| 9 | `ShapeTypeRect` | 2 |
| 10 | `ShapeTypeQuad` | 3 |
| 11 | `PlayFlagForce` | 1 |
| 12 | `PlayFlagChain` | 2 |
| 13 | `PlayFlagAsCan` | 4 |
| 14 | `PlayFlagJoin` | 8 |
| 15 | `PlayFlagStealth` | 16 |
| 16 | `TransformOrderFlip` | 0 |
| 17 | `TransformOrderSlant` | 3 |
| 18 | `TransformOrderZoom` | 2 |
| 19 | `TransformOrderAngle` | 1 |
| 20 | `CoordinateRecutangularXY` | 0 |
| 21 | `CoordinateRecutangularXZ` | 1 |
| 22 | `MaskModeStencil` | 0 |
| 23 | `MaskModeAlpha` | 1 |

`Recutangular` 的拼写错误是四端共同 script ABI，不能改成 `Rectangular`。

### 3.1 UTF-16LE 字符串地址

普通 IDA string search 会受宽字符串识别噪声影响。本次对完整名字加 UTF-16LE NUL
terminator 后用 byte search 复核。下表严格按 registrar 顺序排列；最后两行是两个 method
name。

| 名称 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `LayerTypeObj` | `0x14D6702` | `0xD85F88` | `0x10195CFFE` | `0x174F362` |
| `LayerTypeShape` | `0x14D671C` | `0xD85FA2` | `0x10195D018` | `0x174F37C` |
| `LayerTypeLayout` | `0x14D673A` | `0xD85FC0` | `0x10195D036` | `0x174F39A` |
| `LayerTypeMotion` | `0x14D675A` | `0xD85FE0` | `0x10195D056` | `0x174F3BA` |
| `LayerTypeParticle` | `0x14D677A` | `0xD86000` | `0x10195D076` | `0x174F3DA` |
| `LayerTypeCamera` | `0x14D679E` | `0xD86024` | `0x10195D09A` | `0x174F3FE` |
| `ShapeTypePoint` | `0x14D67BE` | `0xD86044` | `0x10195D0BA` | `0x174F41E` |
| `ShapeTypeCircle` | `0x14D67DC` | `0xD86062` | `0x10195D0D8` | `0x174F43C` |
| `ShapeTypeRect` | `0x14D67FC` | `0xD86082` | `0x10195D0F8` | `0x174F45C` |
| `ShapeTypeQuad` | `0x14D6818` | `0xD8609E` | `0x10195D114` | `0x174F478` |
| `PlayFlagForce` | `0x14D6834` | `0xD860BA` | `0x10195D130` | `0x174F494` |
| `PlayFlagChain` | `0x14D6850` | `0xD860D6` | `0x10195D14C` | `0x174F4B0` |
| `PlayFlagAsCan` | `0x14D686C` | `0xD860F2` | `0x10195D168` | `0x174F4CC` |
| `PlayFlagJoin` | `0x14D6888` | `0xD8610E` | `0x10195D184` | `0x174F4E8` |
| `PlayFlagStealth` | `0x14D68A2` | `0xD86128` | `0x10195D19E` | `0x174F502` |
| `TransformOrderFlip` | `0x14D68C2` | `0xD86148` | `0x10195D1BE` | `0x174F522` |
| `TransformOrderSlant` | `0x14D68E8` | `0xD8616E` | `0x10195D1E4` | `0x174F548` |
| `TransformOrderZoom` | `0x14D6910` | `0xD86196` | `0x10195D20C` | `0x174F570` |
| `TransformOrderAngle` | `0x14D6936` | `0xD861BC` | `0x10195D232` | `0x174F596` |
| `CoordinateRecutangularXY` | `0x14D695E` | `0xD861E4` | `0x10195D25A` | `0x174F5BE` |
| `CoordinateRecutangularXZ` | `0x14D6990` | `0xD86216` | `0x10195D28C` | `0x174F5F0` |
| `MaskModeStencil` | `0x14BE8F4` | `0xD7671C` | `0x10195D2BE` | `0x174F622` |
| `MaskModeAlpha` | `0x14BE914` | `0xD7673C` | `0x10195D2DE` | `0x174F642` |
| `doAlphaMaskOperation` | `0x14D6A2E` | `0xD86288` | `0x10195D3DA` | `0x174F73E` |
| `getD3DAvailable` | `0x14D6A58` | `0x5994BC` | `0x10195D404` | `0x174F768` |

I64 的两个 MaskMode 名还在 `0x10196FC66/0x10196FC86` 出现一次，I32 还在
`0x1762012/0x1762032` 出现一次；那些 xref 属于后续其他 registrar，不改变这里的顺序。
IDA 偶尔把这些宽字面量渲染成单字符，是识别噪声，不代表 script alias。

## 4. 11 个 subclass：顺序和 helper 映射

严格顺序为：

1. `Point`
2. `Circle`
3. `Rect`
4. `Quad`
5. `LayerGetter`
6. `Player`
7. `SourceCache`
8. `ObjSource`
9. `ResourceManager`
10. `SeparateLayerAdaptor`
11. `D3DAdaptor`

| subclass | A64 setup/helper | A32 wrapper | I64 wrapper | I32 wrapper |
|---|---:|---:|---:|---:|
| Point | `0x6F9AC8` | `0x5995A0` | `0x100125D94` | `0x124F9C` |
| Circle | `0x6FA118` | `0x5995E4` | `0x100125E0C` | `0x124FE4` |
| Rect | `0x6FA508` | `0x599628` | `0x100125E84` | `0x12502C` |
| Quad | `0x6FA8F8` | `0x59966C` | `0x100125EFC` | `0x125074` |
| LayerGetter | `0x6FACE8` | `0x5996B0` | `0x100125F74` | `0x1250BC` |
| Player | `0x6FB0E4` | `0x5996F4` | `0x100125FEC` | `0x125104` |
| SourceCache | `0x6FB504` | `0x599738` | `0x100126064` | `0x12514C` |
| ObjSource | `0x6FB9F0` | `0x59977C` | `0x1001260DC` | `0x125194` |
| ResourceManager | `0x6FBEA4` | `0x5997C0` | `0x100126154` | `0x1251DC` |
| SeparateLayerAdaptor | `0x6FC2C4` | `0x599804` | `0x1001261CC` | `0x125224` |
| D3DAdaptor | `0x6FC6D8` | `0x599848` | `0x100126244` | `0x12526C` |

A64 的根函数内联了 setup 后的小 item 分配/发布；其余三端上表 wrapper 同时包含 setup、
4/8-byte `ncbSubClassItem` 和 root `RegisterNCM`。

每行的共同语义：

```text
subclass_info.Setup(name, isRegist)
if registering:
    if subclass class info already holds an object:
        Setup returns false
        wrapper throws "SubClass registration failed."
    construct subclass native class and run its registrar
    append subclass dummy constructor when that subclass has no constructor
    leave subclass class info initialized, but do not publish it globally
    allocate tiny ncbSubClassItem (8 bytes on 64-bit, 4 bytes on 32-bit)
    register its class dispatch on Motion as nitClass | TJS_STATICMEMBER
    release/delete the tiny item
else: // only if the dormant auto-register Unregist wrapper is invoked
    run subclass member unregistration and clear subclass class info
    delete the corresponding member from Motion
```

setup、subclass member registration 或 item allocation 抛异常时都没有 rollback。已经设置的
subclass class info 与更早的 `Motion` 成员会保留，直至显式 unregistration 或进程退出。

根 `RegisterItem` helper 为：

| A64 | A32 | I64 | I32 |
|---:|---:|---:|---:|
| `0x6F9E8C` | `0x5B5C14` | `0x10014CCB8` | `0x14E740` |

它只在 item name 等于根 class name `Motion` 时设置 `_hasCtor`。11 个 subclass name 与两个
method name 都不相等，所以根 `_hasCtor` 最终仍为 false。

## 5. 根 class info 与全局发布生命周期

### 5.1 auto-register 回调

| 阶段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Regist caller | `0x6F944C` | `0x5B55E4` | `0x10014C4A4` | `0x14DDD8` |
| Unregist caller | `0x6F95B0` | `0x5B5668` | `0x10014C50C` | `0x14DE8C` |
| RegistBegin | `0x6F9708` | `0x5B56E4` | `0x10014C568` | `0x14DF3C` |

`RegistBegin` 的共同顺序：

1. `TJSCreateNativeClassForPlugin("Motion", MotionAdaptor::CreateEmpty)`；
2. `TJSRegisterNativeClass("Motion")`；
3. 若 Motion class info 已初始化，抛 `"Already registerd class."`；原字符串保留拼写错误；
4. 保存 class name、native class ID 和 class object 到 process-global class info；
5. 把 class ID 写入 class object；
6. 注册 `finalize` native method；它的 callback 总是返回 `TJS_S_OK`。

### 5.2 `RegistEnd`、dummy constructor 和 global publication

| 项目 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| RegistEnd true body | `0x6F9960` | `0x5B5854` | `0x10014C798` | `0x14E1C4` |
| AddDummy helper | 内联 | `0x5B5954` | `0x10014C904` | `0x14E338` |
| finalize callback | `0x6F9888` | `0x5B57EC` | `0x10014C6AC` | `0x14E0B8` |
| dummy callback | `0x6F9AC0` | `0x5B5990` | `0x10014C968` | `0x14E36E` |

I64 common dispatcher 为 `0x10014C75C -> 0x10014C784 -> 0x10014C798`；I32 为
`0x14E124 -> 0x14E1B4 -> 0x14E1C4`。表中列出最终 registration body。

共同伪代码：

```text
if !_hasCtor:
    register native method "Motion" with NotImplCallback

global = TVPGetScriptDispatch()
if global == null:
    log "No Global Dispatch, Regist failed."
    return

value = owning Variant(classobj)
classobj.Release()                 // Variant now owns the local reference
global.PropSet(MEMBERENSURE, "Motion", value, global)
                                    // return status deliberately ignored
global.Release()
value destructor releases local owner
```

边界：

- dummy callback 本身无条件返回 `TJS_E_NOTIMPL == -1002`；
- 外层 `tTJSNativeClassMethod::FuncCall` 先处理 `membername` 和空 `objthis`，然后清空非空
  result slot，最后才进入 callback；
- `global == null` 时不会走正常的 `_classobj->Release()` publication path，class info 仍保持；
- `PropSet` 的错误码被忽略，因此 class info 完成并不保证 `global.Motion` 写入成功；
- `finalize` 和 dummy method 在尝试全局发布前已是 class member。

### 5.3 异常时的可见前缀

四端都存在调用 End 后继续 unwind 的 cleanup path：

- A64：`RegistEnd` 有 normal 与 EH cleanup 两类 callsite；
- A32：额外 xref 位于 registrar 的 EH cleanup region；
- I64：cleanup thunk `0x10014C4F8` / `0x10014C554` 调 common End 后 resume unwind；
- I32：cleanup thunk `0x14DE62` / `0x14DF12` 调 common End 后 resume unwind。

因此注册过程中若第 N 行抛异常：

1. 第 1..N-1 行已写入的成员不会回滚；
2. 某些 subclass class info 也可能已初始化；
3. cleanup 调 `RegistEnd`；
4. `_hasCtor` 仍为 false，于是追加 dummy `Motion`；
5. 若能取得 global，部分完成的 class object 仍可发布为 `global.Motion`；
6. 原异常继续传播。

这是一种“publish partial prefix while unwinding”的边界，不应在移植版中自行改成事务回滚。

### 5.4 dormant 反注册 wrapper（不是当前 loader 的实际 unload 路径）

| 项目 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| UnregistEnd false body | caller 内联为主 | `0x5B5914` | `0x10014C894` | `0x14E2F8` |

四端确实都保留以下 auto-register 虚函数体；这证明模板能力的精确行为，但不证明存在 caller。
共同流程为：

```text
rerun Motion registrar with isRegist=false
    delete 23 constants in the same order
    for each of 11 subclasses in the same order:
        Setup(false): unregister members and clear subclass class info
        delete subclass member from Motion
    delete doAlphaMaskOperation
    delete getD3DAvailable

global = TVPGetScriptDispatch()
if global != null:
    global.DeleteMember("Motion")
    global.Release()
clear Motion class info regardless of global availability
```

I64 false dispatcher 入口为 `0x10014C894`，I32 为 `0x14E2F8`。若显式调用，它也不是逆序
destruction，而是按 registrar 原正向顺序执行。fresh xref 中四端 Unregist 入口都只由各自
auto-register vtable 引用；`ncbAutoRegister::LoadModule` 仅经 Regist slot 遍历三个 list，成功后
只插入 registered set。四端没有 registered-set erase、module unload 或可达 `AllUnregist` 实体，
所以正常集成式生命周期不会进入本节伪代码。

## 6. 空 `Motion` adaptor 的对象布局与生命周期

`Motion` 是空/trivial native type。NCB adaptor 仍具有标准两字段布局：

| ABI | 对象大小 | vptr | native pointer | sticky |
|---|---:|---:|---:|---:|
| 64-bit | `0x18` | `+0` | `+8` | `+16` |
| 32-bit | `0x0C` | `+0` | `+4` | `+8` |

| helper | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| CreateEmpty | `0x6F985C` | `0x5B57CC` | `0x10014C680` | `0x14E098` |
| Invalidate | `0x6F9890` | `0x5B57F0` | `0x10014C6B4` | `0x14E0BC` |
| destructor thunk | — | — | `0x10014C6EC` | `0x14E0D6` |
| destructor body | `0x6F98C8` | `0x5B580C` | `0x10014C704` | `0x14E0EA` |
| deleting destructor | `0x6F991C` | `0x5B5844` | `0x10014C6F0` | `0x14E0DA` |

CreateEmpty 的共同结果：

```text
adaptor = operator new(0x18 or 0x0C)
adaptor.vptr = Motion adaptor vtable
adaptor.native = null
adaptor.sticky = false
return adaptor
```

Invalidate/destructor 的共同字段协议：

```text
if adaptor.native != null && adaptor.sticky == false:
    operator delete(adaptor.native)   // Motion is trivial/empty
adaptor.native = null
adaptor.sticky = false
```

公开的 dummy constructor 永远不创建 native `Motion`，所以正常 script construction 尝试只会在
host 的 class-instance initialization 阶段注册空 adaptor，然后 dummy 返回 `-1002`。

### 6.1 与宿主 `tTJSNativeClass::CreateNew` 的证据边界

以下是当前仓库 TJS2 host 的交叉核对，不冒充四个 motionplayer 插件二进制自身的证据：

1. `tTJSNativeClass::CreateNew` 先分配 `tTJSCustomObject`；
2. 无 membername 的 class `FuncCall` 调 `CreateNativeInstance()`，把上面的空 adaptor 注册到
   host object；
3. 成员复制完成后，`CreateNew` 再调用名为 `Motion` 的 constructor descriptor；
4. dummy 返回 `-1002`，因此 output `result` 不被写入；
5. 当前 host 的普通 constructor-error 分支没有释放刚分配的 `dsp`，而 exception 分支会释放。

第 5 点是 host-side cleanup 行为，不能仅凭四个插件参考推断目标宿主一定相同。因此生产代码
没有为它添加 motionplayer 自行回收，也没有在单测中故意制造一个无法取回的失败实例泄漏；
单测直接调用 dummy descriptor 来验证插件提供的精确 callback/wrapper 边界。

## 7. `doAlphaMaskOperation` descriptor 与 wrapper

### 7.1 descriptor chain

| 节点 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| register item | 根内联 | `0x5B776C` | `0x10014F138` | `0x151066` |
| factory | 根内联 | `0x5B7790` | `0x10014F184` | `0x15108C` |
| descriptor ctor | 根内联 | `0x5B77C0` | `0x10014F1D8` | `0x151148` |
| FuncCall | `0x6FCAF8` | `0x5B7820` | `0x10014F258` | `0x15123C` |
| convert/invoke | `0x6FCBD4` | `0x5B7890` | `0x10014F300` | `0x1512B4` |
| descriptor vtable | `0x1A1F068` | `0x10BD240` | `0x101AE7168` | `0x1835540` |

32-bit vtable slot 中 A32 `0x5B7821`、I32 `0x15123D` 是 Thumb-tagged pointer；真实函数
实体分别为偶地址 `0x5B7820`、`0x15123C`。

descriptor 大小在 64-bit 为 `0x38`，内层 dispatch 位于 `+32`；在 32-bit 为 `0x20`，
内层 dispatch 位于 `+20`。native method pointer 位于 `+48/+28`；为空时 ctor 抛
`"No method pointer."`。

### 7.2 精确 wrapper 语义

```text
FuncCall(self, flag, membername, hint, result, numparams, param, objthis):
    if membername != null:
        return TJS_E_MEMBERNOTFOUND       // -1001; result untouched
    if objthis == null:
        return TJS_E_NATIVECLASSCRASH     // -1008; result untouched
    if result != null:
        result.Clear()
    if numparams < 11:
        return TJS_E_BADPARAMCOUNT        // -1004; result is already Void

    dstLayer = owning dispatch-closure conversion(param[0])
    dstX      = param[1].AsInteger()
    dstY      = param[2].AsInteger()
    srcLayer = owning dispatch-closure conversion(param[3])
    srcX      = param[4].AsInteger()
    srcY      = param[5].AsInteger()
    width     = param[6].AsInteger()
    height    = param[7].AsInteger()
    threshold = param[8].AsInteger()
    maskMode  = param[9].AsInteger()
    op        = param[10].AsInteger()

    native(dstLayer, dstX, dstY, srcLayer, srcX, srcY,
           width, height, threshold, maskMode, op)
    destroy both owning closure temporaries
    return TJS_S_OK
```

超过 11 的参数完全忽略，不转换。两个 object-closure converter 是 owning temporary，native
调用完成后清理；整数为普通 `tTJSVariant::AsInteger`。转换/调用异常直接传播，wrapper 不 catch、
不回滚已发生的转换副作用。

native alpha-mask 行为已在
`analysis/motionplayer_alpha_mask_four_binary_2026-08-11.md` 单独闭合，本纵切面只恢复其
顶层 owner、descriptor 与 TJS 调用边界。

## 8. `getD3DAvailable` descriptor 与 wrapper

### 8.1 descriptor chain

| 节点 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| register item | 根内联 | `0x5B7C18` | `0x10014F7F8` | `0x1517C4` |
| factory | 根内联 | `0x5B7C3C` | `0x10014F844` | `0x1517E8` |
| descriptor ctor | 根内联 | `0x5B7C6C` | `0x10014F898` | `0x1518A4` |
| FuncCall | `0x6FD4BC` | `0x5B7CCC` | `0x10014F918` | `0x151998` |
| descriptor vtable | `0x1A1F188` | `0x10BD2D0` | `0x101AE7288` | `0x18355D0` |

### 8.2 精确 wrapper 语义

```text
if membername != null:
    return -1001                         // result untouched
if objthis == null:
    return -1008                         // result untouched
if result != null:
    result.Clear()
if numparams < 0:
    return -1004                         // theoretical negative argc boundary

value = native_getD3DAvailable()
if result != null:
    assign TJS Boolean/integer Variant (type tag 4, value 0 or 1)
return 0
```

所有非负 surplus 参数都被忽略，连转换也不发生。`objthis` 只是通用 wrapper validity gate；
native free function 不取得 `Motion` native instance，所以任意非空 receiver 都能通过这一层。

四端 native body 都返回：

```cpp
return !TVPIsSoftwareRenderManager();
```

renderer/backend helper 使用 process-static cache；这个 native body 的四端证据已在早期 baseline
中记录，本次补齐它的 owner 和完整 descriptor wrapper。

## 9. 与当前移植源码的逐项核对

当前 `cpp/plugins/motionplayer/main.cpp` 已与共同表面对齐：

- 空 `class Motion {}`；
- 23 个 `Variant` 名称、值、顺序一致；
- 两个 `CoordinateRecutangular*` 保留错误拼写；
- 11 个 `NCB_SUBCLASS` 顺序一致，Player 为第六；
- 两个 `Method` 位于 D3DAdaptor 之后，顺序一致；
- 没有 `NCB_CONSTRUCTOR`；
- 添加的源码注释明确说明 dummy constructor 与最后全局发布，不再使用旧单目标地址。

本纵切面没有改变 native alpha-mask 或 D3D availability 算法；生产语义改动仅为恢复准确注释。

## 10. 新增的 executable contract

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增：

```text
Motion root NCB surface preserves registrar order and dummy construction
```

覆盖：

- 23 个常量均存在、类型为 integer、值完全一致；
- 11 个 subclass member 均为对象；
- dummy `Motion` descriptor：
  - 非空 membername -> `-1001` 且 result 保持；
  - null receiver -> `-1008` 且 result 保持；
  - 正常 descriptor 调用 -> `-1002` 且 result 已清为 Void；
- `doAlphaMaskOperation` descriptor：相同两个前置 gate，之后 0 args -> `-1004` 且 result 为
  Void；使用任意非空 receiver 证明它不取 Motion native instance；
- `getD3DAvailable` descriptor：负 argc -> `-1004` 且 result 为 Void；一个 surplus String
  不经转换、调用成功，返回 integer/Boolean 并等于当前 renderer backend 的反值。

同一文件中三个旧单目标绝对地址注释已改为语义描述，避免继续把旧 `libkrkr2.so` 地址当作
当前四参考证据。

## 11. recovery IDB 写回

四个 recovery IDB 均已写入并保存：

- 根 registrar、auto-register Regist/Unregist；
- RegistBegin、RegistEnd、UnregistEnd、AddDummy；
- finalize/dummy callback；
- Motion empty-adaptor CreateEmpty/Invalidate/destructor/deleting destructor；
- 11 个 `NCB_registerClass_<name>_guess` helper；
- 两个 method 的 register/factory/descriptor ctor/FuncCall/invoke 链；
- 八个 method FuncCall（两个 method × 四端）的统一八参 ABI；
- 根 registrar、class-info、global publish、两个 wrapper 的注释和书签。

fresh decompile 回读四端确认：

- root 和 RegistEnd 控制流仍正常；
- dummy callback 都重新显示 `-1002`；
- alpha wrapper 都显示 `-1001/-1008/clear/numparams<11/-1004`；
- D3D wrapper 都显示 `-1001/-1008/clear/numparams<0/-1004`；
- 所有新名字均保留 `_guess`。

保存目标：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 12. 验证状态

- 完整 `motionplayer-dll.cpp` Emscripten syntax check：通过；仅既有 `_tss` literal operator
  deprecation warning；
- 根 registrar machine scan：通过；`Variant=23`、`NCB_SUBCLASS=11`、`Method=2`、
  constructor macro call `=0`，且名称/枚举符号/顺序逐项一致；
- Web Debug build：通过；编译 `main.cpp`、重建 `libmotionplayer.a` 并链接 `index.html`；仅有
  仓库既有 `_tss`、pthread memory-growth、JSPI 和 JS library warning；
- `git diff --check`：通过；输出仅为工作树既有的 LF→CRLF 提示，没有 whitespace error。

## 13. 闭合边界

本文件闭合的是 `Motion` 顶层 class/namespace 的 NCB 表面、注册、dormant 反注册虚函数体、局部失败、adaptor
和两个 typed wrapper。它不重复宣称已经闭合：

- alpha-mask native compositor 内部（见专门文档）；
- 11 个 subclass 各自的全部 native 算法（分别由其纵切面文档闭合）；
- 宿主 TJS2 在所有目标应用版本中的失败实例回收实现；
- 整个 motionplayer 插件的剩余对象和容器。

所以该纵切面通过并不代表总目标完成。
