# `__Private_Motion_GLLayer` class / vtable / `Draw_GPU`（四参考二进制，2026-08-27）

## 1. 结论

本切片闭合 direct SeparateLayer legacy backend 最后一条真实缺口：惰性内部类
`__Private_Motion_GLLayer` 的 class registrar、ClassID、native-instance factory、六个 raw
callback、derived Layer vtable、deque owner/destructor 和 `Draw_GPU` 队列消费状态机。

四端共同证明本地 class/queue/consumer 的主要源结构已经匹配；唯一新发现的运行语义偏差是
本地 headless build 在 `SetFace(dfAuto)` 后额外调用
`motionTracePrivateMotionGLLDraw`。四个完整 `Draw_GPU` body 都不存在该回调或等价 sidecar；
它会读 queue size、构造诊断数据并增加可抛/重入边界。本轮已删除该调用及
`MotionTraceWeb.h` include。

该类不是 `motionplayer.dll` 的公开 registrar row，也不应加入 316 条 public NCB 分母；它
只由 `SeparateLayerAdaptor::ensurePrivateMotionGLL` 的函数内 static class pointer 惰性创建，
再通过 native vtable `Draw_GPU` 被 Layer/DrawDevice 链调用。因此它属于 root-reachable
function-pointer/vtable/static-lifetime 分母。

## 2. 宽类名与唯一惰性根

按 ida-search-string 的 UTF-16LE 原始字节流程搜索完整
`__Private_Motion_GLLayer\0`，四端均唯一命中：

| 目标 | UTF-16LE 地址 | registrar | registrar caller |
|---|---:|---:|---:|
| Android arm64 | `0x14D6AF8` | `0x6DA664` | ensure `0x6D2D28` 内 `0x6D2D94` |
| Android armv7 | `0xD862F6` | `0x59BD98` | ensure `0x5974D0` 内 `0x59751E` |
| iOS arm64 | `0x10195D4BE` | `0x10012A73C` | ensure `0x100123670` 内 `0x1001236E8` |
| iOS armv7 | `0x174F822` | `0x1293D4` | ensure `0x122884` 内 `0x122932` |

每个字符串另有一条静态 data owner xref；代码 xref 全部汇入同一个 registrar。四个
registrar 又各自只有 ensure 的一条 code xref。Android armv7/iOS armv7 对部分 Thumb
function-pointer xref 识别不足，但 registrar slot、derived native vtable data 和另两端 code
xref 联合固定了相同拓扑，不能把 Thumb 的空 xref 误判成 dead-strip。

函数内 static 保存 raw class dispatch pointer：第一次进入 legacy ensure 的创建路径时 new
class，之后进程级复用；该 pointer 不由本模块析构。类创建/注册抛异常时 guard 不发布完成态，
后续调用可重试；成功后 ClassID 同时写 class object 和 process-global lookup slot。

## 3. class registrar 与六个 callback

### 3.1 完整 fresh 分母

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| class registrar | `0x6DA664`，103 | `0x59BD98`，97 | `0x10012A73C`，79 | `0x1293D4`，133 |
| registrar cleanup | body 内 landing | 无本帧 cleanup | `0x10012A888`，9 | `0x129584`，18 |
| native factory/ctor | `0x6DA810`，38 | `0x59BEE0`，8 + `0x59BF04`，22 | `0x10012A8AC`，25 | `0x1295CC`，65 |
| native allocation cleanup | factory body 内 | 无本帧 cleanup | `0x10012A914`，5 | `0x129686`，12 |

registrar 固定按以下四行注册，没有 `Draw_GPU` 脚本成员、clear/append/queue getter 或额外
factory：

1. 以类名本身注册 raw constructor，kind=method；
2. `setSize`，kind=method；
3. `visible` getter/setter，kind=property；
4. `absolute` getter/setter，kind=property。

四端 registrar 的共同结构：构造 `tTJSNativeClass(className)`，覆盖 derived class vtable，
安装 native factory，注册/保存 ClassID，再依次物化 constructor/method/property descriptor。
member descriptor 注册正常返回后才进入下一行；中途异常不回滚已经发布的前缀。

### 3.2 callback 地址与精确指令数

| callback | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| constructor | `0x6DB62C`，37 | `0x59C8D0`，41 | `0x10012B538`，29 | `0x12A182`，29 |
| setSize | `0x6DB6C0`，97 | `0x59C934`，48 | `0x10012B5AC`，36 | `0x12A1C4`，36 |
| visible get | `0x6DB84C`，32 | `0x59C9B4`，35 | `0x10012B63C`，24 | `0x12A222`，23 |
| visible set | `0x6DB8CC`，54 | `0x59CA0C`，37 | `0x10012B69C`，26 | `0x12A258`，25 |
| absolute get | `0x6DB9A8`，33 | `0x59CA68`，36 | `0x10012B704`，25 | `0x12A292`，24 |
| absolute set | `0x6DBA2C`，58 | `0x59CAC4`，37 | `0x10012B768`，26 | `0x12A2CA`，25 |
| 每端 callback 合计 | 311 | 234 | 166 | 162 |

24 个 callback 均 fresh decompile，完整 disassembly 全部 `cursor.done=true`。共同 raw
边界为：

- constructor 不自行检查 argc，先以本类 ClassID 做 unchecked GETINSTANCE，再把原
  `numparams/param/objthis` 原样转给 base Layer `Construct`；
- setSize 只做 `numparams >= 2` 下界检查，失败返回 `-1004`；成功时先 target
  GETINSTANCE，再按 argv[0]、argv[1] 做 Integer 转换，调用 native `SetSize`，surplus 忽略；
- 两个 getter 先 GETINSTANCE，result 非空时分别写 Boolean visible 与 Integer absolute；
- 两个 setter 先 GETINSTANCE，再分别做 Boolean/Integer conversion 后调用 native setter；
- raw callback 不增加 objthis/null/native 恢复层；公开 descriptor 外层的 member-name、
  receiver/result clear 等通用规则由 TJS native method/property object 承担。

本地 `PrivateMotionGLL_constructor/setSize/getVisible/setVisible/getAbsolute/setAbsolute` 与该
结构逐项一致。

## 4. native object、vtable 与队列 owner

四端 derived object 均先运行完整 `tTJSNI_BaseLayer` 构造，再覆盖三组 vptr，初始化
`_stencilCount=0` 和空 `std::deque<RenderItem>`，最后把 Layer type 设为 alpha (`2`)。
64/32 位对象大小分别受 BaseLayer 和 STL ABI 影响：Android arm64 `0x388`、Android armv7
`0x280`、iOS arm64 `0x368`、iOS armv7 `0x268`；这些大小不应转写成源码 padding。

derived primary vtable 中 `Draw_GPU` 槽直接指向：

- Android arm64 vtable data `0x1A177B8` → `0x6DA94C`；
- Android armv7 derived vtable data `0x10B95E8` → Thumb `0x59BFB5`；
- iOS arm64 vtable data `0x101ADF860` → `0x10012A9B4`；
- iOS armv7 vtable data `0x18318C0` → Thumb `0x129725`。

这条 data pointer 是 `Draw_GPU` 的真实可达根；不能要求普通 code xref。析构先恢复 derived
vptr，逆序销毁 deque：每个 element 先 Release owning source texture，再释放 point vector，
最后调用 BaseLayer 析构。queue 在 draw 时只读、不消费。

native construction failure disposition：

- Android arm64 在 factory body 内调用 BaseLayer dtor/operator delete；
- Android armv7 factory/ctor 没有本帧 cleanup；
- iOS arm64 冷区只 delete raw allocation并 resume；
- iOS armv7 单状态 SjLj cleanup delete raw allocation，cleanup throw/非法状态 abort。

registrar 的 iOS arm64/iOS armv7 cleanup 同样只处理已分配 class 与 class-name owner；
已注册的早期 descriptor 不做事务回滚。

## 5. `Draw_GPU` 完整四端 body

| 目标 | body | 指令数 | 独立 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6DA94C` | 407 | body 内 landing region `0x6DAEF8..0x6DAFB4` |
| Android armv7 | `0x59BFB4` | 420 | 无本帧 cleanup |
| iOS arm64 | `0x10012A9B4` | 416 | `0x10012B050`，72 |
| iOS armv7 | `0x129724` | 621 | `0x129DCC`，134，30-state SjLj |

四个主 body、Android arm64 landing、iOS arm64 cold cleanup 与 iOS armv7 cleanup 均完整
分页读取。四端总的共同伪代码：

```cpp
if (visiblecheck && !IsSeen()) return;
if (!Intersect(rect, requestedRect, ownRect)) return;

x += rect.left - requestedRect.left;
y += rect.top  - requestedRect.top;
targetRect = offset(rect, x, y);
ParentRectToChildRect(rect);
UpdateBitmapForChild = target->GetDrawTargetBitmap(targetRect, ignoredRect);
SetFace(dfAuto);

clip = offset(ClipRect, x, y);
stencilEnabled = stencilCount >= 1;
if (stencilEnabled) beginPrivateStencil(UpdateBitmapForChild.texture);
xf = float(x) - 0.5f;
yf = float(y) - 0.5f;

for (const RenderItem &item : queue) {
    if (!item.sourceTexture) break;
    color = (item.color0 == 0xff808080 ? 0xffffff : item.color0 & 0xffffff)
          | ((item.opacity & 0xff) << 24);
    method = item.stencilMask
        ? selectAlphaTest(item.blendMode & 15, color, false)
        : selectNormal(item.blendMode & 15, color, false);
    if (!method) continue;
    applyStencil(item.stencilMask, item.stencilWrite);
    switch (item.geometryType) {
      case 0: submitAffineThreePointQuad(..., xf, yf); break;
      case 1: control = offset(item.points); mesh = tessellate(control); submit(...); break;
      case 2: mesh = offset(item.points); submit(mesh, mesh); break;
      default: break;
    }
}
if (stencilEnabled) endPrivateStencil();
ResetClip();
```

### 5.1 entry、target 与坐标边界

visiblecheck 为 false 时不看 visible/opacity gate。intersection 失败在 target getter、face、
stencil和queue之前返回。x/y 的调整和 targetRect/child rect 转换顺序固定；target bitmap
写入 BaseLayer persistent `UpdateBitmapForChild`，face 随后设 128。clip 使用原 Layer
ClipRect 加 x/y，不使用 intersection rect重建。

`float(x/y) - 0.5f` 在整个 queue pass 中复用；不是 double - 0.5 再窄化。负值、极值和
float narrowing保持原生行为。

### 5.2 stencil 与 method

只有 `_stencilCount >= 1` 才执行 pass-level begin/end：bind private OpenGL target，disable
depth，clear stencil/depth mask状态并初始化 process/shared stencil-enabled byte。每 item 的
mask/write 两 byte 决定 `GL_ALWAYS`/`GL_EQUAL`、mask和 replace/keep；零/零会在之前已启用
时 disable stencil。method-null 只跳过当前 item，不改变 queue、也不终止循环。

pass-level begin 后任何异常都不会执行补偿 end；`ResetClip` 也只在 normal tail。GL state
和 BaseLayer partial commits因此保留，这与本地不使用额外 catch/rollback一致。

### 5.3 三种 geometry

- type 0：读取前三个 float point，按 `(p0,p1,p2,p1,p2,p1+p2-p0)` 构造 6 个 double
  destination vertex；source rect按 TL/TR/BL 两三角绕序；通过 private manager提交2个三角；
- type 1：把全部 stored float point加 offset并转 double，作为 4×4 control/bounds；按
  meshDivX/Y tessellate，再调用公共 mesh cell/AABB/triangle helper；
- type 2：同样 offset全部 point，但同一 vector同时作为 bounds/mesh；
- 其他 type：不分配 geometry temporary，不提交。

source texture 为空时是 `break`，不是 `continue`。source rect、texture范围、cell/admission、
Bezier basis与triangle callback的更深实现已分别由
`MP-R14-D3D-MESH-SUBMIT-CELLS`、`MP-R14-BEZIER-BASIS-TESSELLATION` 和 private OpenGL
manager slice闭合。

### 5.4 EH 与 partial commit

Android arm64 landing、iOS arm64 cold cleanup和iOS armv7 30-state switch都只销毁当前
geometry分支已经构造的 callable、小缓冲/heap vector及其分配，不清 queue、不释放 queue
中 owning texture、不撤销已提交三角、不恢复GL stencil、不调用ResetClip。callable cleanup
或 vector destruction再次抛出走 terminate/abort。Android armv7没有本帧 landing。

本地 `std::vector`/`std::array`/lambda RAII 正好表达该逐构造态清理；不应在函数外包
transaction guard。

## 6. 本地逐项对照与修改

| 联合证据 | 本地位置 | 修改前 | 当前 |
|---|---|---|---|
| private class四行 registrar/六callback | `PrivateMotionGLL.cpp:396` 起 | 匹配 | 匹配 |
| native factory、alpha type、deque owner | `PrivateMotionGLL.cpp:243` 起 | 匹配 | 匹配 |
| Draw_GPU entry/target/stencil/queue/geometry/tail | `PrivateMotionGLL.cpp:251` 起 | 核心匹配 | 匹配 |
| 四端无 diagnostic callback | 原 `PrivateMotionGLL.cpp:275` | headless-only `motionTracePrivateMotionGLLDraw` | 已删除 |
| private manager和shared mesh/Bezier helpers | `MotionRenderBackend.cpp` / `PrivateMotionGLL.cpp:163` 起 | 已由 companion slices闭合 | 闭合 |

没有修改公开 API 或 ABI padding；只删除四端不存在的 sidecar include/call。现有 private GLL
unit case继续覆盖 ClassID隔离、ensure复用/resize、native/dispatch同一queue、type2 vector swap、
clear与texture owner。正式构建仍受本机缺少 CMake/Ninja/Emscripten/Catch2限制。

## 7. IDB 与状态

四份 IDB 已统一命名 class registrar、native factory/ctor、六 callback、`Draw_GPU` 和
cold/SjLj cleanup，补充函数注释与 bookmarks，并原位保存。按本切片 unique 入口计数：

- Android arm64：859 条；
- Android armv7：781 条；
- iOS arm64：772 条（含 9/5/72 条三个 cold cleanup）；
- iOS armv7：1145 条（含 18/12/134 条三个 SjLj cleanup）。

状态：`IMPLEMENTED`。这同时闭合旧 coverage 中 private class registrar/native callbacks、
CreateNew/native allocation failure ABI 和 private Layer `Draw_GPU` consumer 两组 gap。
