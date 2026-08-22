# Motion.D3DAdaptor 完整 NCB 表面、Factory 与 typed-nullsub 边界（四参考）

日期：2026-08-14

## 1. 结论

本纵切面重新从 `reference/binaries/` 的四个当前参考二进制恢复
`Motion.D3DAdaptor` 的脚本注册表、generated NCB descriptor、native factory、attach
回滚和参数转换边界。它补充并修正较早的
`motionplayer_d3d_adaptor_four_binary_2026-08-11.md`：旧文对 native renderer、对象布局和
纹理生命周期的结论仍然有效，但当时把 `registerBg` / `registerCaption` 只描述成“空函数”，
没有恢复两个空 native body 前面的 typed wrapper ABI。

四端共同结论是：

- registrar 有 1 个 `Factory` row，随后严格注册 11 个 method 和 4 个读写 property；没有
  raw callback；
- 15 个普通成员的次序为十个 method、`visible`、`alphaOpAdd`、`captureCanvas`、
  `canvasCaptureEnabled`、`clearEnabled`；
- `registerBg` 的 native body 虽然为空，descriptor 仍要求
  `(tTJSVariant, float, float, float, bool)`；
- `registerCaption` 的 native body虽然为空，descriptor 仍要求
  `(tTJSVariant, float, float)`；
- 因此两个 wrapper 的参数门槛、Variant copy、`AsReal`、double-to-float 窄化、bool 转换和
  转换异常全都可观察，不能把它们复原成 `void()`；
- native factory 最少要求 5 个参数，先验证 `Window`，再分配 native storage，随后按
  `param[1]` 到 `param[4]` 的顺序做四次整数转换，成功构造后才写出 native pointer；
- factory 本身没有 `result == nullptr`、`param == nullptr` 或 `param[0] == nullptr` 的友好
  检查；本地实现原先添加的两层 guard 不属于参考实现，已经移除；
- generated factory descriptor 有一个特殊的“恰好一个 Void”shell 分支：它在使用 receiver
  前直接成功，脚本 result 保持不变；
- 普通有效参数加 null receiver 会先完整构造 native，再因 adaptor attach 失败销毁并释放
  native，返回 `TJS_E_NATIVECLASSCRASH`；
- 所有 factory descriptor 路径不清空或写入脚本 result；普通 method wrapper 则在 receiver
  检查之后、arity 检查之前清空 result。

未知模板实例与 C++ helper 名继续保留 `_guess`。本文地址仅用于四参考证据映射；可编译源码
注释不写绝对地址。

## 2. 四参考与根地址

| 简称 | 目标 | member registrar | native factory | native ctor | factory descriptor FuncCall |
|---|---|---:|---:|---:|---:|
| A64 | Android arm64-v8a | `0x6AA274` | `0x6AA8F8` | `0x6AAEF0` | `0x6ECDB8` |
| A32 | Android armeabi-v7a | `0x57CC58` | `0x57CEBC` | `0x57D0AC` | `0x5AB004` |
| I64 | iOS arm64 | `0x1001039A4` | `0x100103C30` | `0x100103FA8` | `0x10013E548` |
| I32 | iOS armv7 | `0x100D94` | `0x100FD4` | `0x10128C` | `0x13F384` |

四个 registrar 都已重新 decompile，并用 UTF-16LE（包含终止零）搜索成员名，再以 registrar
xref 区分同名字符串在 Player 或其他 class registrar 中的重复出现。IDA 把某些宽字符串
错误渲染成单个 ASCII 字符，是 display/type noise，不是短别名。

## 3. 精确 registrar 表面与次序

源级等价 registrar 为：

```cpp
NCB_REGISTER_SUBCLASS_DELAY(D3DAdaptor) {
    Factory(&D3DAdaptor::factory);
    NCB_METHOD(setPos);
    NCB_METHOD(setSize);
    NCB_METHOD(setClearColor);
    NCB_METHOD(setResizable);
    NCB_METHOD(removeAllTextures);
    NCB_METHOD(removeAllBg);
    NCB_METHOD(removeAllCaption);
    NCB_METHOD(registerBg);
    NCB_METHOD(registerCaption);
    NCB_METHOD(unloadUnusedTextures);
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(alphaOpAdd, getAlphaOpAdd, setAlphaOpAdd);
    NCB_METHOD(captureCanvas);
    NCB_PROPERTY(canvasCaptureEnabled,
                 getCanvasCaptureEnabled,
                 setCanvasCaptureEnabled);
    NCB_PROPERTY(clearEnabled, getClearEnabled, setClearEnabled);
}
```

逐 row 表：

| 序号 | script name | kind | native signature / target family |
|---:|---|---|---|
| 0 | `D3DAdaptor` | Factory | custom native factory callback |
| 1 | `setPos` | method | `void(int, int)`；native nullsub |
| 2 | `setSize` | method | `void(int, int)` |
| 3 | `setClearColor` | method | `void(int)` |
| 4 | `setResizable` | method | `void(bool)` |
| 5 | `removeAllTextures` | method | `void()` |
| 6 | `removeAllBg` | method | `void()`；native nullsub |
| 7 | `removeAllCaption` | method | `void()`；native nullsub |
| 8 | `registerBg` | method | `void(tTJSVariant,float,float,float,bool)`；native nullsub |
| 9 | `registerCaption` | method | `void(tTJSVariant,float,float)`；native nullsub |
| 10 | `unloadUnusedTextures` | method | `void()`；native nullsub |
| 11 | `visible` | RW property | `bool get()` / `void set(bool)` |
| 12 | `alphaOpAdd` | RW property | `bool get()` / `void set(bool)` |
| 13 | `captureCanvas` | method | `void(tTJSVariant)` |
| 14 | `canvasCaptureEnabled` | RW property | `bool get()` / `void set(bool)` |
| 15 | `clearEnabled` | RW property | `bool get()` / `void set(bool)` |

这里的 interleaving 是证据的一部分。本地 registrar 原先把 `captureCanvas` 放在全部四个
property 之前；四端都把它放在前两个 property 和后两个 property 之间，现已按参考顺序修复。
顺序不仅影响静态审计，也可能影响成员枚举和异常时的 partial-prefix publication。

### 3.1 成员宽字符串证据

A64 的 registrar-local UTF-16LE 字面量是连续且无歧义的一组：

| name | address | name | address |
|---|---:|---|---:|
| `setPos` | `0x14CFC8E` | `setSize` | `0x14D57F8` |
| `setClearColor` | `0x14D5AA2` | `setResizable` | `0x14D5ABE` |
| `removeAllTextures` | `0x14D5AD8` | `removeAllBg` | `0x14D5AFC` |
| `removeAllCaption` | `0x14D5B14` | `registerBg` | `0x14D5B36` |
| `registerCaption` | `0x14D5B4C` | `unloadUnusedTextures` | `0x14D5B6C` |
| `alphaOpAdd` | `0x14D5B96` | `captureCanvas` | `0x14D5BAC` |
| `canvasCaptureEnabled` | `0x14D5BC8` | `clearEnabled` | `0x14D5BF2` |

`visible` 在 A64 有 `0x14BEF98` 与 `0x15187D2` 两个候选；registrar xref 选定属于当前 row
的引用。其余平台也存在 `setPos`、`setSize`、`visible` 的多 class 重复：

- A32 当前组包含 `setSize 0xD85350`、`setClearColor 0xD855C2`、
  `setResizable 0xD855DE`、`removeAllCaption 0xD855F8`、`registerBg 0xD8561A`、
  `unloadUnusedTextures 0xD85630`、`alphaOpAdd 0xD8565A`、
  `canvasCaptureEnabled 0xD85670`、`clearEnabled 0xD8569A`；
- I64 当前连续组位于 `0x10195BF82` 至 `0x10195C0E0`；
- I32 当前连续组位于 `0x174E2E6` 至 `0x174E444`。

对重复字符串只以代码 xref 和 registrar 控制流定 row，不以“第一个文本命中”定 row。

## 4. descriptor family 映射

多个成员共享同一组 NCB 模板实例。下表记录最外层 `FuncCall` / property entry：

| signature family | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Factory FuncCall | `0x6ECDB8` | `0x5AB004` | `0x10013E548` | `0x13F384` |
| `void(int,int)` FuncCall | `0x6ECED4` | `0x5AB190` | `0x10013E778` | `0x13F5F8` |
| `void(int)` FuncCall | `0x6ED19C` | `0x5AB450` | `0x10013EB04` | `0x13F9E4` |
| `void(bool)` FuncCall | `0x6ED34C` | `0x5AB660` | `0x10013ED88` | `0x13FD44` |
| `void()` FuncCall | `0x6ED57C` | `0x5AB870` | `0x10013F038` | `0x1400A4` |
| `registerBg` FuncCall | `0x6ED684` | `0x5ABA0C` | `0x10013F25C` | `0x140324` |
| `registerCaption` FuncCall | `0x6EDBD0` | `0x5ABDC0` | `0x10013F724` | `0x1408A0` |
| bool property PropGet | `0x6EDFCC` | `0x5AC0C8` | `0x10013FAF4` | `0x140CF8` |
| bool property PropSet | `0x6EE0F0` | `0x5AC154` | `0x10013FBF0` | `0x140D5E` |
| `captureCanvas` FuncCall | `0x6EE204` | `0x5AC350` | `0x10013FDDC` | `0x1410A4` |

### 4.1 保留独立 helper 的 descriptor 构造链

A64 优化掉/内联了大部分 descriptor factory 链；可稳定识别的 descriptor vtable 为：

| family | A64 descriptor vtable | invoke/helper |
|---|---:|---:|
| Factory | `0x1A1BE98` | native factory `0x6AA8F8` |
| `int,int` | `0x1A1BFB8` | invoke `0x6ECF80` |
| `int` | `0x1A1C0D8` | outer conversion in `0x6ED19C` |
| `bool` | `0x1A1C1F8` | invoke `0x6ED468` |
| `void` | `0x1A1C318` | outer `0x6ED57C` |
| `registerBg` | `0x1A1C438` | invoke `0x6ED7A0` |
| `registerCaption` | `0x1A1C558` | invoke `0x6EDCEC` |
| bool property | `0x1A1C678` | PropGet/PropSet above |
| capture Variant | `0x1A1C798` | Variant converter `0x6EE360` |

A32：

| family | registrar helper | descriptor factory | descriptor ctor | call/get entry | invoke |
|---|---:|---:|---:|---:|---:|
| Factory | `0x57CE94` | `0x5AAE6C` | `0x5AAF84` | `0x5AB004` | native factory |
| `int,int` | `0x5AB0B8` | `0x5AB0EC` | `0x5AB128` | `0x5AB190` | shared |
| `int` | `0x5AB378` | `0x5AB3AC` | `0x5AB3E8` | `0x5AB450` | shared |
| `bool` | `0x5AB588` | `0x5AB5BC` | `0x5AB5F8` | `0x5AB660` | shared |
| `void` | `0x5AB798` | `0x5AB7CC` | `0x5AB808` | `0x5AB870` | shared |
| bg | `0x5AB934` | `0x5AB968` | `0x5AB9A4` | `0x5ABA0C` | `0x5ABAD0` |
| caption | `0x5ABCE8` | `0x5ABD1C` | `0x5ABD58` | `0x5ABDC0` | `0x5ABE80` |
| bool property | `0x5AC014` | `0x5AC058` | in factory | `0x5AC0C8` | setter `0x5AC154` |
| capture | `0x5AC278` | `0x5AC2AC` | `0x5AC2E8` | `0x5AC350` | shared |

I64：

| family | registrar helper | descriptor factory | descriptor ctor | call/get entry | invoke |
|---|---:|---:|---:|---:|---:|
| Factory | `0x100103BDC` | `0x10013E324` | `0x10013E4C8` | `0x10013E548` | native factory |
| `int,int` | `0x10013E628` | `0x10013E67C` | `0x10013E6E0` | `0x10013E778` | shared |
| `int` | `0x10013E9B4` | `0x10013EA08` | `0x10013EA6C` | `0x10013EB04` | shared |
| `bool` | `0x10013EC38` | `0x10013EC8C` | `0x10013ECF0` | `0x10013ED88` | shared |
| `void` | `0x10013EEE8` | `0x10013EF3C` | `0x10013EFA0` | `0x10013F038` | shared |
| bg | `0x10013F10C` | `0x10013F160` | `0x10013F1C4` | `0x10013F25C` | `0x10013F33C` |
| caption | `0x10013F5D4` | `0x10013F628` | `0x10013F68C` | `0x10013F724` | `0x10013F804` |
| bool property | `0x10013F9EC` | `0x10013FA48` | in factory | `0x10013FAF4` | setter `0x10013FBF0` |
| capture | `0x10013FC8C` | `0x10013FCE0` | `0x10013FD44` | `0x10013FDDC` | shared |

I32：

| family | registrar helper | descriptor factory | descriptor ctor | call/get entry | invoke |
|---|---:|---:|---:|---:|---:|
| Factory | `0x100FAC` | `0x13F068` | `0x13F290` | `0x13F384` | native factory |
| `int,int` | `0x13F410` | `0x13F438` | `0x13F4F8` | `0x13F5F8` | shared |
| `int` | `0x13F7FC` | `0x13F824` | `0x13F8E4` | `0x13F9E4` | shared |
| `bool` | `0x13FB5C` | `0x13FB84` | `0x13FC44` | `0x13FD44` | shared |
| `void` | `0x13FEBC` | `0x13FEE4` | `0x13FFA4` | `0x1400A4` | shared |
| bg | `0x14013C` | `0x140164` | `0x140224` | `0x140324` | `0x1403BC` |
| caption | `0x1406B8` | `0x1406E0` | `0x1407A0` | `0x1408A0` | `0x140934` |
| bool property | `0x140B9C` | `0x140BE0` | in factory | `0x140CF8` | setter `0x140D5E` |
| capture | `0x140EBC` | `0x140EE4` | `0x140FA4` | `0x1410A4` | shared |

32 位 method descriptor 大小为 `0x24`，64 位为 `0x40`；factory descriptor 较小，分别
为 `0x20` / `0x38`。property descriptor 同时保存 getter/setter target，因而比普通 method
descriptor 更大。大小是模板 ABI 证据，不应被复制进跨平台 C++ 对象布局。

## 5. native factory 的数据流和异常生命周期

四端共同伪代码：

```text
createInstance(out_native, numparams, param, objthis):
    if numparams < 5:
        return -1004

    window = param[0].AsObjectNoAddRef()
    if window.IsInstanceOf("Window", window) != true:
        throw "must set Window object"

    storage = operator new(native_size)
    width   = param[1].AsInteger()
    height  = param[2].AsInteger()
    centerX = param[3].AsInteger()
    centerY = param[4].AsInteger()
    construct D3DAdaptor(storage, window,
                         width, height, centerX, centerY)
    *out_native = storage
    return 0
```

`native_size` 分别是 A64 `0x68`、A32 `0x40`、I64 `0x50`、I32 `0x34`；差异来自指针宽度
以及 GNU STL / libc++ map header ABI，源级字段语义见旧 D3DAdaptor 文档。

关键顺序：

1. `numparams < 5` 在任何参数解引用前；
2. `param[0]` 的 dispatch 转换和 `Window` 检查在 allocation 前；
3. `operator new` 在四次整数转换前；
4. 四次转换严格按参数索引递增；
5. 构造成功后才写 `*out_native`；
6. 多余参数完全忽略；
7. conversion 或 ctor 抛出时，C++ new-expression landing pad 对 storage 执行
   `operator delete`；不会写出 half-constructed pointer。

factory 没有以下本地“防御性”分支：

```cpp
if(!result) return TJS_E_INVALIDPARAM;
if(!param || !param[0]) return TJS_E_INVALIDPARAM;
```

在 count 已满足后传入这些非法指针属于原生自然解引用边界。为了一比一恢复，本地两项 guard
已经删除；普通脚本 wrapper 会提供正常的 `param`/`out_native`，测试不主动触发进程级崩溃。

## 6. generated Factory descriptor

Factory descriptor 的共同控制流为：

```text
FuncCall(membername, result, argc, argv, objthis):
    if membername != null:
        return -1001                  // result untouched

    if argc == 1 && argv[0].Type == Void:
        return 0                      // receiver/result untouched

    native = null
    hr = nativeFactory(&native, argc, argv, objthis)
    if hr != 0:
        return hr                     // result untouched

    hr = attachNative(objthis, native)
    if hr succeeds:
        return 0                      // result untouched

    destroy native
    operator delete(native)
    return -1008
```

边界后果：

- exactly-one-Void 是生成 shell object 的通道；它甚至允许 `objthis == nullptr`，因为分支在
  receiver 使用之前；
- zero args 不属于 shell special case，而是进入 native factory 并因 `<5` 返回 `-1004`；
- ordinary valid args + null receiver 会先执行 Window AddRef、目标纹理创建等完整 ctor 副作用，
  attach 失败后再完整 dtor/free；
- descriptor 不使用脚本 result 传回 native，也不清 result；result 可在所有上述路径保持调用前值；
- membername gate 先于 one-Void special case。

本地单测同时覆盖 one-Void/null receiver、CreateNew shell、membername、undercount、valid+null
receiver rollback、valid+surplus 和 result-preservation。

## 7. 普通 method wrapper 的共同边界

所有普通 typed method 的外层顺序为：

```text
if membername != null:
    return -1001                      // result untouched
if objthis == null:
    return -1008                      // result untouched
if result != null:
    result.Clear()
if argc < required:
    return -1004
native = unwrap(objthis)
if unwrap failed or native == null:
    return -1008
convert only required prefix
call native
return 0
```

所有 family 都只检查下限；surplus 被忽略且不转换。因此 surplus 放入不可转换 Octet 仍成功，
而 required prefix 中同样的 Octet 会从相应转换路径抛异常。

`void()` family 的 required count 为零，所以任意非负 argc 都通过；它不会读取 `argv`。这与
`registerBg/registerCaption` 形成可测试的差别。

## 8. typed native nullsubs

### 8.1 `registerBg`

共同 wrapper/invoke 语义：

```text
require argc >= 5
payload = owning-copy Variant(argv[0])
x       = float(argv[1].AsReal())
y       = float(argv[2].AsReal())
scale   = float(argv[3].AsReal())
enabled = bool(argv[4])
native.registerBg(payload, x, y, scale, enabled)  // nullsub
destroy payload and conversion temporaries
return 0
```

A64/A32/I64/I32 的定型后最外层伪代码分别在 `0x6ED684`、`0x5ABA0C`、
`0x10013F25C`、`0x140324` 明确显示 `<5` 或等价的 `>=5` gate。invoke helper 进一步证明
三个 `AsReal`、float 窄化和最终 bool 转换。

### 8.2 `registerCaption`

共同语义：

```text
require argc >= 3
payload = owning-copy Variant(argv[0])
x       = float(argv[1].AsReal())
y       = float(argv[2].AsReal())
native.registerCaption(payload, x, y)              // nullsub
destroy payload and conversion temporaries
return 0
```

四端 outer wrapper 分别在 `0x6EDBD0`、`0x5ABDC0`、`0x10013F724`、`0x1408A0`
显示 `<3` / `>=3` gate。

本地 header 原先把两项写为 `void registerBg()` / `void registerCaption()`，这会错误接受零参数，
也会消掉 required-prefix conversion 和异常。现已恢复为：

```cpp
void registerBg(tTJSVariant, float, float, float, bool) {}
void registerCaption(tTJSVariant, float, float) {}
```

native body 继续为空，只有 ABI 被修正。

## 9. 四个 bool 读写 property

`visible`、`alphaOpAdd`、`canvasCaptureEnabled`、`clearEnabled` 四项共享同一 property
descriptor family，但各自绑定不同 native getter/setter。

PropGet 顺序：

```text
membername != null -> -1001
getter missing     -> -1007
objthis == null    -> -1008
clear result if non-null
unwrap native; failure/null -> -1008
value = getter(native)
if result != null: assign integer Boolean
return 0
```

PropSet 顺序：

```text
membername != null -> -1001
setter missing     -> -1007
objthis == null    -> -1008
value pointer null -> -1
unwrap native; failure/null -> -1008
converted = bool(*value)
setter(native, converted)
return 0
```

本 class 四个 descriptor 都同时拥有 getter 和 setter，所以 `-1007` 是共享模板的潜在边界，
正常注册对象不会触发。setter 不清传入 value；getter result 使用 TJS integer Boolean 表示。

## 10. native 对象生命周期的交叉引用

本纵切面只重开 descriptor/factory/attach 边界。native ctor/dtor、目标纹理、Window 引用、
软件纹理 map、captureCanvas 和 renderer 数据流沿用且已由四端证据支持的旧专项结论：

- `analysis/motionplayer_d3d_adaptor_four_binary_2026-08-11.md`
- `analysis/motionplayer_shared_d3d_adaptor_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_render_source_texture_four_binary_2026-08-13.md`

factory attach 成功后，native 由 `ncbInstanceAdaptor<D3DAdaptor>` 持有；script shell release
触发 adaptor 销毁 native。attach 失败则 generated factory wrapper 立即执行同一 native dtor 和
`operator delete`，不会把 owner 留在 shell 中。

one-Void shell 没有 native；在随后对普通 method/property 调用时，wrapper 的 unwrap/native-null
路径返回 `-1008`。对 shell 再调用有效 Factory 可以 attach native；surplus 仍被忽略。

## 11. 写回四份恢复 IDB

四份 recovery IDB 均完成并保存了以下内容：

- registrar、native factory、native ctor；
- Factory descriptor factory/ctor/FuncCall 链；
- `int,int`、`int`、`bool`、`void`、bg、caption、bool property、capture Variant 的
  descriptor 构造/调用链；
- bg/caption invoke helper；
- 保守的 generated FuncCall、PropGet、PropSet 和 native factory 函数类型；
- registrar、factory、zero-arg、bg、caption、property entry 的语义注释；
- registrar、native factory、factory wrapper、bg wrapper、caption wrapper 的 bookmarks。

所有新推定符号均带 `_guess`。应用类型后对 native factory、bg wrapper、caption wrapper 再次
decompile，四端都保留正确的 `5/3` gate 和 factory publication 顺序。

保存结果：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`

## 12. 本地修复和验证

源码修复：

- `D3DAdaptor.h` 恢复两个 typed nullsub 的精确签名，并注释其 wrapper conversion 可观察性；
- `D3DAdaptor.cpp` 删除参考 factory 不存在的 result/param friendly guards；
- `main.cpp` 恢复 `visible` / `alphaOpAdd` / `captureCanvas` 的 registrar interleaving；
- `motionplayer-dll.cpp` 增加完整 factory、method、property、typed-nullsub 回归测试。

回归测试覆盖：

- 11 个 method descriptor 和 4 个 property descriptor 均存在；
- Factory descriptor、one-Void/null receiver、one-Void CreateNew shell；
- membername 和 undercount 的 result preservation；
- valid factory + null receiver 的 construct/attach-fail/destroy；
- valid factory + surplus；
- shared `int,int` wrapper 的 clear-before-arity 和 surplus ignore；
- bg `argc=4` 失败、`argc>=5` 成功、required float slot Octet 转换异常；
- caption `argc=2` 失败、`argc>=3` 成功；
- genuine zero-arg nullsub 忽略 surplus Octet；
- 四项 bool property 的 set/get；
- shell release 销毁 attached native。

验证命令/结果在纵切面收口时记录：

- D3D registrar 机器扫描：Factory 1、method 11、property 4、raw 0，row 名称和次序精确；
- header signature 机器扫描：bg/caption typed signature 精确；
- 完整 motionplayer unit-test TU Emscripten syntax：通过；仅有既有 `_tss` deprecated
  literal-operator warning；
- `Web Debug Build`：27 个步骤全部通过并链接 `index.html`；仅有既有编译/JSPI/JS library
  warnings；
- `git diff --check`：退出码 0；只报告工作区既有的 LF-to-CRLF conversion warnings。

## 13. 当前仍不声称的内容

- 不把优化造成的 helper 拆分差异误认为四套源代码差异；
- 不为未识别的 NCB 模板内部字段发明正式名字；
- 不把非法 C++ pointer 输入的自然崩溃写成脚本可依赖 API；
- 不声称单一 D3DAdaptor 纵切面完成了整个 motionplayer 的 100% 恢复；
- 其他 class 中仍可能存在旧 `libkrkr2.so` 单目标注释，必须在各自四参考纵切面中逐步替换。
