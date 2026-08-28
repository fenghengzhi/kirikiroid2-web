# BezierPatch 与 Layer motion 扩展 NCB 注册面四参考二进制联合恢复

日期：2026-08-27

## 1. slice 边界

本轮联合闭合直接附加到脚本 `Layer` 的两张小注册表：

- stateless `BezierPatch` 的 8 个几何静态方法；
- 本地以 `MotionLayerExtensions_guess` 标识的 per-Layer native hook，以及它发布的 2 个
  read/write property 和 7 个 mesh/Bezier 绘制方法。

这里闭合 attach/register/unregister/setup 入口、精确成员顺序、descriptor kind 和全部
callback 入口。后续
`analysis/motionplayer_bezierpatch_methods_geometry_inverse_four_binary_2026-08-27.md` 已闭合
BezierPatch 八个 callback 的数组/字典容器、Bezier 迭代、反向求解、owner 和边界；其余
九个 MotionLayerExtensions callback 也已由
`analysis/motionplayer_layer_extensions_callbacks_lifetime_render_four_binary_2026-08-27.md`
闭合其 per-Layer Variant、mesh/Bezier render、debug 和边界。

## 2. 两套 attached-class 调用链

### 2.1 `MotionLayerExtensions_guess`（有 instance hook）

| 平台 | attach/register | unregister | attach state setup | 9-row registrar |
|---|---|---|---|---|
| Android arm64 | `0x6E217C` | `0x6E227C` | `0x6E2408` | `0x6A1204` |
| Android armv7 | `0x5A2D00` | `0x5A2D60` | `0x5A2DF8` | `0x578A6C` |
| iOS arm64 | `0x100133D68` | `0x100133DCC` | `0x100133E2C` | `0x1000FE030` |
| iOS armv7 | `0x132FD0` | `0x133088` | `0x133144` | `0xFAFB0` |

register path 解析全局 `Layer` class dispatch，建立独立 class/native ID 状态并通过
hook-aware descriptor family 发布成员；unregister path 用同一 registrar 的 unregistering
状态撤销已经可见的前缀并清空状态。这个表不发布名为
`MotionLayerExtensions_guess` 的脚本 class；该名字只是本地 C++ owner 标签，九个成员直接
出现在 `Layer` 上。

实例 hook 是 lazy per-Layer owner：成员调用先查当前 Layer 的 native instance，缺失时才以
该 Layer dispatch 作为 non-owning owner 构造 extension native 并写回 attached instance。
因此两个 debug Variant 和 face cache 属于各 Layer 实例，而不是 registrar 全局单例。
完整 hook 的失败/重复 attach/Layer teardown 边界仍保留到生命周期 slice。

### 2.2 `BezierPatch`（stateless attached class）

| 平台 | attach/register | unregister | attached-class setup | 8-row registrar |
|---|---|---|---|---|
| Android arm64 | `0x6E627C` | `0x6E630C` | `0x6E6428` | `0x6A195C` |
| Android armv7 | `0x5A51C0` | `0x5A5244` | `0x5A52C0` | `0x578C48` |
| iOS arm64 | `0x100136FA4` | `0x10013700C` | `0x100137068` | `0x1000FE1F0` |
| iOS armv7 | `0x136B9C` | `0x136C50` | `0x136D00` | `0xFB146` |

四端 setup 都分配 attached native-class dispatch、写入 native class ID，并注册 `finalize`
静态成员；八个回调不查找或构造 per-Layer extension native。它没有脚本 constructor，也没有
每 Layer 的 BezierPatch state。源结构应保留一个 stateless helper class，而不是根据
`0xB0 / 0x70` 的 attached-class infrastructure 大小伪造 C++ 实例 padding。

## 3. `BezierPatch` 精确 8 行表

| # | 脚本名 | kind | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---|---|---|---|---|
| 1 | `affinePatch` | attached static method | `0x6A1D40` | `0x578D90` | `0x1000FE308` | `0xFB244` |
| 2 | `translatePatch` | attached static method | `0x6A2048` | `0x578F28` | `0x1000FE4B4` | `0xFB450` |
| 3 | `affineTranslatePatch` | attached static method | `0x6A2328` | `0x5790B0` | `0x1000FE640` | `0xFB64C` |
| 4 | `calcPatchBounds` | attached static method | `0x6A264C` | `0x579258` | `0x1000FE804` | `0xFB868` |
| 5 | `calcMeshBounds` | attached static method | entry `0x6A2A04` in `0x6A264C` | `0x5794F8` | `0x1000FEAB8` | `0xFBBDC` |
| 6 | `calcBezierPatch` | attached static method | entry `0x6A2D6C` in `0x6A264C` | `0x5797A0` | `0x1000FEE38` | `0xFC014` |
| 7 | `calcBezierPatchList` | attached static method | `0x6A3230` | `0x579A18` | `0x1000FF134` | `0xFC360` |
| 8 | `reverseCalcBezierPatch` | attached static method | `0x6A3874` | `0x579D48` | `0x1000FF508` | `0xFC7A4` |

Android arm64 把第 4-6 行编进一个从 `0x6A264C` 开始、大小 `0xBE4` 的 IDA 函数范围；
`0x6A2A04` 和 `0x6A2D6C` 是 registrar 实际保存的内部入口。其余三端三者各自是独立
函数。IDB 中仅给内部入口添加 line comment，不创建重叠伪函数。

## 4. `MotionLayerExtensions_guess` 精确 9 行表

| # | 脚本名 | kind | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---|---|---|---|---|
| 1 | `debugMeshApp` | read/write property | get `0x6A1768`, set `0x6A1774` | get `0x578BB4`, set `0x578BC0` | get `0x1000FE1C8`, set `0x1000FE1D4` | get `0xFB11E`, set `0xFB12A` |
| 2 | `debugBezierApp` | read/write property | get `0x6A177C`, set `0x6A1788` | get `0x578BC6`, set `0x578BD2` | get `0x1000FE1DC`, set `0x1000FE1E8` | get `0xFB130`, set `0xFB13C` |
| 3 | `meshCopy` | attached method | `0x69F150` | `0x577924` | `0x1000FC6E8` | `0xF9654` |
| 4 | `operateMesh` | attached method | `0x69F304` | `0x577A44` | `0x1000FC864` | `0xF97F4` |
| 5 | `drawMeshFrame` | attached method | `0x69F5E4` | `0x577B50` | `0x1000FC9C0` | `0xF996C` |
| 6 | `bezierPatchCopy` | attached method | `0x69FD7C` | `0x577F3C` | `0x1000FCF78` | `0xF9F08` |
| 7 | `operateBezierPatch` | attached method | `0x69FF30` | `0x57805C` | `0x1000FD0F4` | `0xFA0A8` |
| 8 | `drawBezierPatchFrame` | attached method | `0x6A0210` | `0x578168` | `0x1000FD250` | `0xFA220` |
| 9 | `drawBezierPatchMeshFrame` | attached method | `0x6A0B3C` | `0x5786AC` | `0x1000FDAF8` | `0xFAAA4` |

四端 property descriptor 都有 ordinary getter/setter、空 indexed slots，并使用 hook-aware
receiver lookup。registrar 反编译有时只把宽字符串渲染为首字母；本轮直接读取四个 IDB
中对应 UTF-16LE bytes，逐端确认 `debugBezierApp`、`operateMesh`、`drawMeshFrame`、
`bezierPatchCopy`、`operateBezierPatch` 和 `drawBezierPatchMeshFrame` 的完整名称。Bezier
表中被缩成单字母的四个名称也以相同方法确认。

## 5. registrar 共同伪代码

```text
registerBezierPatch(state):
    attach stateless native class to Layer
    publishStaticMethod("affinePatch", affinePatch)
    publishStaticMethod("translatePatch", translatePatch)
    publishStaticMethod("affineTranslatePatch", affineTranslatePatch)
    publishStaticMethod("calcPatchBounds", calcPatchBounds)
    publishStaticMethod("calcMeshBounds", calcMeshBounds)
    publishStaticMethod("calcBezierPatch", calcBezierPatch)
    publishStaticMethod("calcBezierPatchList", calcBezierPatchList)
    publishStaticMethod("reverseCalcBezierPatch", reverseCalcBezierPatch)

registerMotionLayerExtensions(state):
    attach lazy per-Layer native-instance hook to Layer
    publishReadWriteProperty("debugMeshApp", getDebugMeshApp, setDebugMeshApp)
    publishReadWriteProperty("debugBezierApp", getDebugBezierApp, setDebugBezierApp)
    publishMethod("meshCopy", meshCopy)
    publishMethod("operateMesh", operateMesh)
    publishMethod("drawMeshFrame", drawMeshFrame)
    publishMethod("bezierPatchCopy", bezierPatchCopy)
    publishMethod("operateBezierPatch", operateBezierPatch)
    publishMethod("drawBezierPatchFrame", drawBezierPatchFrame)
    publishMethod("drawBezierPatchMeshFrame", drawBezierPatchMeshFrame)
```

每次 publish 都服从 registering flag；attach 异常清理只撤销已经可见的前缀。ARM64
大量展开 descriptor 分配和 cleanup，其余平台更多调用模板 publisher helper，但成员顺序、
kind 和 callback 等价类一致。

## 6. 本地逐行对照

`cpp/plugins/motionplayer/main.cpp` 当前两块注册代码与四端原生表完全一致：

- `NCB_ATTACH_CLASS(BezierPatch, Layer)` 恰好 8 个 method，顺序与第 3 节一致；
- `NCB_ATTACH_CLASS_WITH_HOOK(MotionLayerExtensions_guess, Layer)` 恰好 2 个
  read/write property 和 7 个 method，顺序与第 4 节一致；
- `NCB_GET_INSTANCE_HOOK` 保留按 Layer lazy 建立 native instance 的源结构；
- 没有给 `BezierPatch` 添加 constructor 或实例字段，也没有把 extension debug 状态做成
  进程全局。

因此本轮无需修改运行时 C++。

## 7. fresh 证据、状态与剩余工作

- 完整读取 8 个 registrar：MotionLayerExtensions 为 254/104/93/116 条指令，
  BezierPatch 为 241/66/62/78 条指令；
- 完整读取 16 个 attach/register 与 unregister wrapper：四端分别合计
  264/126/78/198 条指令；
- 完整读取 8 个 attach-state / attached-class setup：57+80、51+73、5+57、6+102 条指令，
  并 fresh decompile 各平台分支下的 register/unregister helper；
- 对 17 行对应的 19 个 callback 地址完成 fresh function map；Android arm64 的其中两个
  是合并函数内部 entry。四端直接读取 UTF-16LE 原始 bytes 复核被反编译器缩写的脚本名；
- 四个 IDB 已完成 wrapper/setup/registrar/callback 命名，Android arm64 内部入口已注释，
  两个 registrar 均添加书签并原位保存；
- 17 条注册面现为 `EVIDENCED_4_4`；本历史报告完成时 body 全部保留为
  `BODY_PENDING_SEPARATE_SLICE`，随后 BezierPatch 八条与 MotionLayerExtensions 九条均已
  由上述两个独立报告提升为 `IMPLEMENTED`。

完成本轮后，316 条本地候选中只剩 `EmotePlayer` 73 条未映射。后续 body 工作仍需闭合
其余工作转入 EmotePlayer callback 和完整 root-reachable helper/object/container 总账。
