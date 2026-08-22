# motionplayer Layer copy/operate：四参考二进制复原

日期：2026-08-13

本轮闭合 motionplayer 附着到全局 `Layer` 类的四个 typed native wrapper，及其共同
mesh/Bezier render 数据流。它们不是核心 `LayerIntf` 的逐格 `AffineBlt` API：参考实现把
source Layer、目标 Layer、脚本属性、render-method selector、共享 triangle backend、
`update` 与两类 debug overlay 全部留在 motionplayer attached native object 内。

本文中的绝对地址仅作四端证据索引，不进入编译源码注释。

## 1. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| face cache 刷新 | `0x69AB18` | `0x57551C` | `0x1000F936C` | `0xF63F8` |
| auto mode 读取 | `0x69ACD0` | `0x5755E4` | `0x1000F94CC` | `0xF6558` |
| mesh debug overlay | `0x69D1F0` | `0x5765C8` | `0x1000FAB34` | `0xF7C04` |
| Bezier debug overlay | `0x69D7B0` | `0x5768E8` | `0x1000FB054` | `0xF80C0` |
| mesh common render | `0x69E0D0` | `0x576E08` | `0x1000FB660` | `0xF86B0` |
| Bezier common render | `0x69E630` | `0x577184` | `0x1000FBBB8` | `0xF8C00` |
| mode/face -> bitmap method | arm64 wrapper 内联 | `0x577684` | `0x1000FC29C` | `0xF9328` |
| clear whole Layer | `0x69EF1C` | `0x577774` | `0x1000FC4B8` | `0xF9410` |
| `meshCopy` | `0x69F150` | `0x577924` | `0x1000FC6E8` | `0xF9654` |
| `operateMesh` | `0x69F304` | `0x577A44` | `0x1000FC864` | `0xF97F4` |
| `bezierPatchCopy` | `0x69FD7C` | `0x577F3C` | `0x1000FCF78` | `0xF9F08` |
| `operateBezierPatch` | `0x69FF30` | `0x57805C` | `0x1000FD0F4` | `0xFA0A8` |

四个 recovery IDB 已为这些函数写入语义名/注释并保存。Android arm64 优化器把
mode/face switch 内联进两个 operate wrapper；其余三端保留共同 helper。这是编译器差异，
映射内容一致。

## 2. attached class 的完整结构与注册顺序

native object 的语义字段仍是：

```cpp
struct MotionLayerExtensions_guess {
    iTJSDispatch2 *owner;       // raw、非 owning
    int faceCache;
    tTJSVariant debugMeshApp;
    tTJSVariant debugBezierApp;
};
```

owner 由附着它的 TJS Layer 控制寿命；两个 debug Variant 是 owning copy。完整 member 注册
顺序四端一致：

1. `debugMeshApp`
2. `debugBezierApp`
3. `meshCopy`
4. `operateMesh`
5. `drawMeshFrame`
6. `bezierPatchCopy`
7. `operateBezierPatch`
8. `drawBezierPatchFrame`
9. `drawBezierPatchMeshFrame`

本地已按此顺序完成 attached registrar；四个名称已从核心 `tTJSNC_Layer` native member 表
移除。核心 C++ `MeshCopy` 等函数暂留，因为现有准确 SLA 内部路径仍直接调用它们；它们不再
构成这四个脚本名称的 dispatch 目标。

## 3. 精确 typed wrapper 参数

四端 ABI 共同还原为：

```text
meshCopy(source, sx, sy, sw, sh, points, divX, divY,
         stretchType, clear)

bezierPatchCopy(source, sx, sy, sw, sh, controlPoints, divX, divY,
                stretchType, clear)

operateMesh(source, sx, sy, sw, sh, points, divX, divY,
            mode, opacity, stretchType)

operateBezierPatch(source, sx, sy, sw, sh, controlPoints, divX, divY,
                   mode, opacity, stretchType)
```

关键纠错：operate 两方法没有 clear 参数；第 11 个脚本参数是 `stretchType`。旧本地 wrapper
把它转成 Boolean clear，并把 stretch type 固定为 nearest，源自旧 `libkrkr2.so` 路径注释，
不适用于四个当前参考二进制。

四方法的 source 都严格转为 `tTJSNI_Layer`。没有 Bitmap fallback；非 Layer 对象走引擎现有
`Specify Layer class object` 异常。source main image 和 texture 也没有 null guard，保持自然
错误边界。

`sx + sw`、`sy + sh` 在 32-bit signed integer 中完成。实现以显式 two's-complement bit
运算复刻 wrap result，避免 C++ signed overflow UB。

## 4. face cache 与 auto 规则

每次 copy 及每次 mode 映射都会刷新 face cache：

```text
face = owner.face，缺失/失败默认 0
if face == 128:
    type = owner.type，缺失/失败默认 0
    if type == 2 or 13 <= type <= 28: face = 0
    else if type == 12:               face = 4
    else:                             face = 1
```

这里读取的属性是 `type`，不是 `engineType`。Android arm64 IDB 曾因相邻旧符号把 UTF-16
字面量显示成 `aEngineType_*`；iOS 两端反编译与四端 raw bytes 均确认实际字符为 `type`。

copy 的 `holdAlpha` 规则：

- face 0：false；
- face 1：读取 owner `holdAlpha`，缺失默认 false；
- face 4：false；
- 其他 face 抛出方法特定错误：
  - `meshCopy: not drawable face type.`
  - `bezierPatchCopy: not drawable face type.`

operate 若 mode 为 128，先读取 owner `type`：只在值 `> 28` 时改为 1；负值与 0..28 原样
保留。之后刷新 face 并映射 bitmap method。operate 在映射完成以后才创建长寿命 owner
accessor，并且无条件读取 `holdAlpha`。两个 operate 的错误文本都刻意是：

```text
operateMesh: not drawable face type.
```

包括 `operateBezierPatch`，不能“修正”为自己的方法名。

## 5. mode/face 到 bitmap method 的完整映射

| mode | face 0 | face 1 | face 4 | 其他 face |
|---:|---:|---:|---:|---|
| 1 | 1 | 0 | 15 | fail |
| 2 | 3 | 2 | 14 | fail |
| 12 | 13 | 11 | 12 | fail |

其余共同分支：

```text
3->4, 4->5, 5->6,
8->7, 9->8, 10->9, 11->10,
13->16, 14->17, ... 28->31
```

mode 6、7 及所有其他值失败。这恰好对应 `tTVPBBBltMethod` 的数值表，但 wrapper 不能把它
重新当 operation mode 交给核心 Layer API，否则会发生第二次映射。本地 attached path 直接
把这个结果交给 render manager selector。

## 6. clear 的脚本可观察行为

copy 的 clear 为 true 时调用一个独立 helper。四端共同顺序是：

1. 构造临时 owner accessor，AddRef；
2. 读取 `neutralColor`；
3. 读取 `height`；
4. 读取 `width`；
5. 动态调用 owner：

```text
fillRect(0, 0, width, height, neutralColor)
```

6. 销毁五个参数 Variant、result Variant，释放 accessor。

这会清整张目标 Layer，不是 `clipLeft/Top/Width/Height`。旧本地 core wrapper 的
`FillRect(ClipRect, NeutralColor)` 因而不等价。

## 7. common mesh/Bezier render 数据流

普通 mesh common helper：

1. 严格把 points Variant 转 Object owner；
2. `GetCount()`；对负 count 做 toward-zero `/2`；reserve `count/2`；
3. `count >= 2` 才循环，每个坐标先 MEMBERMUSTEXIST probe，成功后再进行第二次 indexed
   read + Real 转换，失败坐标写 0；奇数末坐标忽略；
4. 构造目标 owner accessor，依次读 `clipLeft`、`clipTop`、`clipWidth`、`clipHeight`；
5. clip right/bottom 为 32-bit add；
6. 从 attached owner 取目标 Layer native；从 source Variant 严格取 source Layer native；
7. 先使 source main image/font 状态就绪并取得 source texture；
8. 当前 render manager 根据 `(opacity, holdAlpha, bitmapMethod)` 选择 render method；
9. method 非空时才使目标 main image就绪，设置 manager 的 `StretchType` parameter，并调用
   已闭合的共享 triangle backend；
10. backend 返回 true 时动态调用 `update(clipLeft, clipTop, clipWidth, clipHeight)`；
11. method 非空时，无论 backend 是否选中 cell，都执行 debug overlay。

Bezier common helper在读取 clip 后，使用固定 32 个坐标的 direct-read parser，先形成 16 个
row-major 控制点，再利用共享 basis cache 生成 `(divX+1)*(divY+1)` 网格。backend 的
`boundsPoints` 是控制点 vector、`meshPoints` 是细分 vector，因此保留了控制点整体包围盒
快路径。后续 source/target/method/update 与普通 mesh 相同。

共享 backend、source texture AddRef/Release、越界 repeat、cell clipping、六点绕序、一次
callback 与异常 unwind 已在
`motionplayer_common_mesh_backend_four_binary_2026-08-13.md` 单独闭合。本纵切的 callback
进一步确认：

- reference texture 是目标 bitmap 当前 texture；
- target texture 由 `GetTextureForRender(method->IsBlendTarget(), &clip)` 得到；
- `OperateTriangles` 使用原始 full clip，而不是 backend 写回的 computed bounds；
- `StretchType` 通过 render manager parameter 传递，不是 backend 的额外几何参数。

## 8. debug overlay 与容器边界

method 非空以后：

- mesh 与 Bezier 都调用 mesh debug helper，appearance 为 `debugMeshApp`；
- Bezier 再调用 Bezier debug helper，appearance 为 `debugBezierApp`。

appearance 为 Void 时，helper 在构造全局 Layer accessor和 Array 之前返回。

mesh debug 与公开 `drawMeshFrame` 的容器行为不同：它在 helper 入口只创建一个外层 TJS
Array；每条横线/纵线开始时直接清空这个对象内部的 deque，再填入本线的二维点 Array。
因此每次 `Layer.drawLines(appearance, line)` 收到的是同一个脚本 Array identity，只是内容已被
原地替换；helper 结束时才释放它。横线先行、纵线后行，两个 loop 都是 inclusive。
appearance 同时作为调用参数与 Void gate。四端都显示外层 `createTJSArrayWithItems_guess`
在两个 loop 之前，而每轮调用的是同一 deque 的 clear helper；公开 `drawMeshFrame` 才是在
每一条实际绘制的线内新建并销毁外层 Array。

Bezier debug 在函数入口对 attached owner 额外 AddRef；取得 division=3 的 basis table后：

- 四次 row-direction sample，每次产生 4 点；
- 四次 column-direction sample，每次产生 4 点；
- 共调用 8 次 `Layer.drawBeziers(debugBezierApp, points)`；
- 没有公开 `drawBezierPatchFrame` 的三点截断或 reverse；那是公开 frame 方法独有的历史行为。

两类 helper 都忽略脚本返回 status，但仍构造并销毁 result Variant，因此脚本返回对象的
AddRef/Release 时机可观察。

## 9. 所有权、异常与返回边界

- wrapper 开始持有一个 owner accessor；face refresh、auto resolve 和 clear 分别使用自己的
  临时 accessor，形成嵌套 AddRef/Release；
- points/source 参数由 typed ncbind conversion 形成 owning Variant copy；
- source texture 的引用由共享 backend在任何几何验证前增加，成功、早退和异常 unwind 均按
  已闭合规则释放；
- callback 抛异常时不会调用后续 update/debug；
- backend 普通 false return 不调用 update，但 method 只要非空仍执行 debug；
- render method 为 null 时不取得目标 main image、不调用 backend、update 或 debug；
- 四个 typed method 返回 void；脚本层得到 Void。

## 10. 本地落地与验证

- `MotionLayerExtensions.{h,cpp}`：补齐四 wrapper、face/auto/method mapping、整层 clear、
  common mesh/Bezier render、update 与 debug overlay；
- `main.cpp`：按完整九成员顺序注册 attached Layer surface；
- `LayerIntf.cpp`：移除四个过时、参数错误且允许 Bitmap fallback 的核心脚本 wrapper；
- 共用 `MotionRenderBackend`，没有再造第二份 clip/repeat/triangle 实现；
- 清除了本纵切覆盖区域内残留的旧 `libkrkr2.so` 地址注释。

验证：

- `cmake --build out/web/debug -j 8`：通过；
- `git diff --check`：无 whitespace error，仅有工作树既存 LF/CRLF warning；
- 四个 recovery IDB：语义命名、函数注释、保存均成功；
- 当前无包含可保存脚本参数 identity、malformed custom Array、真实 Layer appearance 的既存
  fixture，因此没有凭空制造伪 oracle。本纵切以四端静态控制流、ABI 对照和全量 Web 构建
  闭合。
