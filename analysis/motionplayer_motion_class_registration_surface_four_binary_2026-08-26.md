# Motion 顶层类注册表面（MP-A14，四参考二进制，2026-08-26）

## 1. 目标与四端映射

本纵切面只恢复 `Motion` 顶层类 registrar 的发布表面，不把 namespace method 的
函数体、11 个 subclass 各自的 member registrar 或对象生命周期冒充为已闭合。

| 二进制 | registrar | constant helper | Point subclass wrapper/setup | 状态 |
|---|---|---|---|---|
| Android arm64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6D6EE8@0x6D6EE8` | `...!sub_6D766C@0x6D766C` | `...!sub_6F9AC8@0x6F9AC8` | 三者 fresh decompile |
| Android armv7 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_5991D0@0x5991D0` | `...!sub_5994DC@0x5994DC` | `...!sub_5995A0@0x5995A0` | 三者 fresh decompile |
| iOS arm64 | `Kirikiroid2_1.3.9_iOS_arm64!sub_100125974@0x100125974` | `...!sub_100125C9C@0x100125C9C` | `...!sub_100125D94@0x100125D94` | 三者 fresh decompile |
| iOS armv7 | `Kirikiroid2_1.3.9_iOS_armv7!sub_124B7C@0x124B7C` | `...!sub_124E68@0x124E68` | `...!sub_124F9C@0x124F9C` | 三者 fresh decompile |

四端 registrar 均由 `SourceCache`、`ObjSource`、`SeparateLayerAdaptor` 和
`D3DAdaptor` 的精确 UTF-16LE 字符串 xref 独立定位；没有使用一个二进制的地址
代替其它三个。

## 2. 共同源码控制流

```text
for constant in exact 23-row constant table:
    addConstant(classDispatch, constant.name, constant.value,
                TJS_STATICMEMBER)

for subclass in exact 11-row subclass table:
    if !subclass.Setup(subclass.name, registering):
        throw "SubClass registration failed."
    if registering:
        publish vptr-only/static subclass item on Motion

if registering:
    publish native function doAlphaMaskOperation
    publish native function getD3DAvailable
```

constant helper 的共同数据流为：

```text
ttstr key(wideName)
keyPtr = key.owner ? key.c_str() : sharedEmptyWideString
if registrar.registering:
    valueVariant = tjs_int(value)
    classDispatch.PropSet(flags | TJS_MEMBERENSURE,
                          keyPtr, hint=null,
                          &valueVariant, classDispatch)
destroy valueVariant
destroy key
```

四端 helper 都不检查 `PropSet` 返回值。Android arm64 的尾部析构返回值被
Hex-Rays 误提升成 helper 返回值；其它端也存在相同的尾调用/stack-check 展开。
源码层 helper 是无消费返回的注册操作，不能把某端 ABI 返回寄存器的残值当成
业务返回值。

Point wrapper 的四端共同边界为：

```text
ok = PointClassInfo.Setup(name, registering)
if !ok:
    throw TJS error L"SubClass registration failed."
if registering:
    item = new PointStaticSubclassItem
    classDispatch.addMember(name, item)
```

Android arm64 把 wrapper 外层展开进 Motion registrar，而其它三端保留一层
`ctx,name` wrapper；Point 的 `Setup` 本体在 Android arm64 仍为独立函数。

## 3. 精确常量表

四端顺序、值和传入 flags `0x10000` 完全一致：

| # | 名称 | 值 |
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

`Recutangular` 是二进制字面量的实际拼写，不能改成更自然的
`Rectangular`。

## 4. 精确 subclass 表

四端发布顺序一致：

| # | subclass | Android arm64 setup | Android armv7 wrapper | iOS arm64 wrapper | iOS armv7 wrapper |
|---:|---|---|---|---|---|
| 1 | `Point` | `0x6F9AC8` | `0x5995A0` | `0x100125D94` | `0x124F9C` |
| 2 | `Circle` | `0x6FA118` | `0x5995E4` | `0x100125E0C` | `0x124FE4` |
| 3 | `Rect` | `0x6FA508` | `0x599628` | `0x100125E84` | `0x12502C` |
| 4 | `Quad` | `0x6FA8F8` | `0x59966C` | `0x100125EFC` | `0x125074` |
| 5 | `LayerGetter` | `0x6FACE8` | `0x5996B0` | `0x100125F74` | `0x1250BC` |
| 6 | `Player` | `0x6FB0E4` | `0x5996F4` | `0x100125FEC` | `0x125104` |
| 7 | `SourceCache` | `0x6FB504` | `0x599738` | `0x100126064` | `0x12514C` |
| 8 | `ObjSource` | `0x6FB9F0` | `0x59977C` | `0x1001260DC` | `0x125194` |
| 9 | `ResourceManager` | `0x6FBEA4` | `0x5997C0` | `0x100126154` | `0x1251DC` |
| 10 | `SeparateLayerAdaptor` | `0x6FC2C4` | `0x599804` | `0x1001261CC` | `0x125224` |
| 11 | `D3DAdaptor` | `0x6FC6D8` | `0x599848` | `0x100126244` | `0x12526C` |

这一行表只证明 Motion registrar 对 subclass 的 Setup/publication 顺序。每个
subclass 的构造、成员、ClassInfo publication、unregister 和 native owner 仍是
独立纵切面。

## 5. namespace method publication

两个 method 紧跟第 11 个 subclass，四端顺序一致：

| 名称 | Android arm64 body | Android armv7 body | iOS arm64 body | iOS armv7 body |
|---|---|---|---|---|
| `doAlphaMaskOperation` | `sub_6AC4E4@0x6AC4E4` | `sub_57E1E8@0x57E1E8` | `sub_100104E68@0x100104E68` | `Motion_doAlphaMaskOperation_guess@0x10243C` |
| `getD3DAvailable` | `sub_6ADD40@0x6ADD40` | `sub_57F4A8@0x57F4A8` | `sub_10010654C@0x10010654C` | `sub_103908@0x103908` |

本纵切面只确认名字、绑定目标和发布顺序。后续
`analysis/motionplayer_motion_alpha_mask_d3d_available_four_binary_2026-08-27.md` 已对两个
函数体取得四端 fresh decompile/disassembly，闭合 clip、software/GPU mask 矩阵、owner、
边界和 availability 逻辑；主台账现将两行标为 `IMPLEMENTED`。

## 6. 字符串类型修正

本轮为四库中被各自 Motion registrar 直接引用的 36 个 UTF-16LE 字面量设置了
正确 `unsigned short[N]` 类型，并为四个 constant helper 与四个 Point wrapper
补充了已由调用链证明的宽字符串参数类型。四库均已重新反编译并保存。

部分调用点仍因更外层未类型化 helper/registrar slot 显示单个 ASCII 首字符；
`find_bytes`、完整终止符、registrar xref 和 data item 类型共同证明真实完整键。
在其余 subclass/member helper 纵切面中继续修正相应签名，不能从残留显示重新
推导单字符源码。

## 7. 本地逐项对照

`cpp/plugins/motionplayer/main.cpp` 的 `NCB_REGISTER_CLASS(Motion)` 当前：

- 23 个 `Variant` 名称、值和顺序与四端一致；
- 11 个 `NCB_SUBCLASS` 名称和顺序与四端一致；
- `doAlphaMaskOperation`、`getD3DAvailable` 名称、顺序和本地绑定角色与四端
  registrar 一致。

本轮不修改 C++，因为 registrar 表面没有发现偏差。namespace method 函数体后来已由上述
独立报告闭合；subclass 各自表面和失败生命周期不由本历史注册面报告承担。

## 8. 验证与剩余项

- 四端 registrar、constant helper、Point wrapper 均有本轮 fresh decompile；
- 36 个 registrar 字面量在每个 IDB 中都由精确 UTF-16LE 搜索和 xref 选中；
- 四个 IDB 已分别保存；
- 仓库改动仅为分析/覆盖文档和生成工具，`git diff --check` 通过。

下一项按依赖顺序应枚举并闭合 11 个 subclass 各自的 member registrar；优先从
`Point/Circle/Rect/Quad` 这一组小而同构的表面开始。
