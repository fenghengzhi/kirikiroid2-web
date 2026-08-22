# `PrivateMotionGLL::Draw_GPU` 纹理、模板、stencil 与 mesh 提交边界（四参考二进制）

日期：2026-08-13

## 1. 范围与结论

本轮只以 `reference/binaries/` 四个参考目标及其 recovery IDB 为证据，恢复
`__Private_Motion_GLLayer::Draw_GPU` 从 Layer drawable 到 RenderManager 的 GPU 数据流。
旧 `libkrkr2.so` 地址和本地注释均不作为语义证据。

四端共同结论如下：

1. `Draw_GPU` 把 `target->GetDrawTargetBitmap(...)` 的返回值直接保存到继承自
   `tTJSNI_BaseLayer` 的 `UpdateBitmapForChild`，不写 `CurrentDrawTarget`；
2. 目标位图的当前纹理是 reference target，按所选 method 的 `IsBlendTarget()` 调
   `GetTextureForRender(...)` 得到 writable target；二者不是同一参数的重复传入；
3. render method 按 blend 低四位与 alpha-test byte 选择，并以函数静态对象缓存 method
   与 parameter ID；软件 RenderManager 没有另一套 `tTVPBBBltMethod` 分支；
4. `_stencilCount >= 1` 只控制整帧 `BeginStencil` / `EndStencil` 包裹。每个 item 的 GL
   stencil 状态机不受该计数控制；
5. geometry 0 直接提交两个 affine 三角形；geometry 1 先 offset 16 个控制点再生成
   Bezier 网格；geometry 2 只做点 offset；
6. geometry 1/2 最终都进入同一个通用 mesh helper。该 helper 为所有 cell 构造一个
   合并的目标顶点 vector 和源顶点 vector，最后只回调一次、只调用一次
   `OperateTriangles`；
7. 点 offset 在 `float` 精度完成后才转换为 `double`，而不是先提升再相加；
8. source texture 为空会终止整个 deque 遍历；method 为空只跳过当前 item；未知
   geometry 只跳过当前 item；
9. `Draw_GPU` 本身没有 target、目标位图或目标纹理的空指针保护，也不会在结尾恢复
   `glDepthMask(GL_TRUE)`。

## 2. 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `PrivateMotionGLL::Draw_GPU` | `0x6DA94C` | `0x59BFB4` | `0x10012A9B4` | `0x129724` |
| alpha-test method selector | `0x6D9470` | `0x59AE64` | `0x100128D00` | `0x127F38` |
| ordinary method selector | `0x6D9898` | `0x59B1FC` | `0x100129134` | `0x12827C` |
| float-point offset helper | `0x6D8F88` | `0x59AA9C` | `0x100128864` | `0x127B5A` |
| Bezier grid helper | `0x6D9138` | `0x59ABC8` | `0x1001289AC` | `0x127C6C` |
| common mesh helper | `0x69AFE4` | `0x575800` | `0x1000F974C` | `0xF685C` |
| affine submit helper | `0x69CF84` | `0x5763EC` | `0x1000FA874` | `0xF79E8` |
| mesh submit callback | `0x6DB41C` | `0x59C790` | `0x10012B3AC` | `0x12A080` |
| target-pair getter | `0x6DB57C` | `0x59C870` | `0x10012B4F8` | `0x12A15E` |
| stencil-test cache byte | `0x1AB5545` | `0x11119C5` | `0x101B69A05` | `0x187D695` |

四个 `Draw_GPU` 都直接调用各自表中的两个 selector、offset/Bezier helper、common mesh
helper 和 affine helper。mesh callback 与 target getter 由 `std::function` vtable 引用，
因此位于 Draw 主函数之后的 callable helper 区域；四端的捕获字段和调用顺序一致。

## 3. 入口、坐标与目标位图

共同伪代码：

```cpp
if (visiblecheck && (!Visible || Opacity == 0)) return;
if (!Intersect(rect, inputRect, Rect)) return;

x += rect.left - inputRect.left;
y += rect.top  - inputRect.top;

targetRect = rect;
targetRect.set_offsets(x, y);
ParentRectToChildRect(rect);
UpdateBitmapForChild = target->GetDrawTargetBitmap(targetRect, ignoredRect);
Face = dfAuto;
UpdateDrawFace();

clipRect = ClipRect;
clipRect.set_offsets(x, y);
referenceTexture = UpdateBitmapForChild->GetTexture();
```

Android/iOS 64 位在 `UpdateBitmapForChild + 0x58` 直接取当前 Bitmap；32 位在
`+0x40` 取同一字段。`GetDrawTargetBitmap` 的输出 rect 不作为后续 render clip；原生
重新从该 Layer 的 `ClipRect` 复制并平移。现有本地实现把输出 rect 当 clip、写
`CurrentDrawTarget`、对 target/bitmap/texture 做空检查，均不是四端共同路径。

target-pair getter 捕获 `this`，每次提交执行：

```cpp
bitmap = this->UpdateBitmapForChild;
reference = bitmap->GetTexture();
writable = bitmap->GetTextureForRender(method->IsBlendTarget(), &clipRect);
return {reference, writable};
```

mesh callback 随后以 `writable` 作为 `OperateTriangles` 的 target、以 `reference` 作为
reference target。affine helper 内部执行同一组操作。原生不会预先固定取一次
`GetTextureForRender(true, ...)`，也不会显式 `SetRenderTarget`。

## 4. render method selector 与缓存

两套 selector 的 switch 完全一致，仅 method 名是否带 `_AlphaTest` 不同：

| blend 低四位 | ordinary | alpha-test |
|---:|---|---|
| `1` | `PsAddBlend_color` | `PsAddBlend_color_AlphaTest` |
| `2`、`5` | `PsSubBlend_color` | `PsSubBlend_color_AlphaTest` |
| `3` | `PsMulBlend_color` | `PsMulBlend_color_AlphaTest` |
| `4` | `PsScreenBlend_color` | `PsScreenBlend_color_AlphaTest` |
| 其他，第三参数 false | `AlphaBlend_color` | `AlphaBlend_color_AlphaTest` |
| 其他，第三参数 true | `AlphaBlend_color_a` | `AlphaBlend_color_a_AlphaTest` |

`Draw_GPU` 在四端都把 selector 的第三参数传为 false，因此 `_a` 分支在本调用链不会
命中，但它是 selector 的真实边界。每个 switch 分支各有函数静态 method pointer 与
parameter ID：首次进入时按名字 `GetRenderMethod`，枚举 `color`，alpha-test 分支再
枚举 `alpha_threshold`。随后每次调用写 color，alpha-test 固定写阈值 64。

初始化路径没有 `method == nullptr` 保护：如果首次 lookup 返回空，紧接着的
`EnumParameterID` 会解引用空指针。selector 返回后，Draw 仍有一次空检查；它主要覆盖
缓存/并发等非正常状态，不构成首次 lookup 的友好失败。四端都没有
`RenderManager::IsSoftware()` 或软件 blit-method 映射。

颜色共同计算：首个 packed color 若等于 `0xFF808080`，RGB 改为 `0xFFFFFF`；否则取
低 24 位。最高字节以 `uint8_t(opacity)` 覆盖。

## 5. stencil 生命周期与全局状态缓存

若 `stencilCount >= 1`，四端执行：

```cpp
RenderManager->BeginStencil(referenceTexture);
glDisable(GL_DEPTH_TEST);
glStencilMask(255);
glClearStencil(0);
glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT); // 0x500
glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
glDepthMask(GL_FALSE);
glDisable(GL_STENCIL_TEST);
stencilTestEnabledCache = 0;
```

遍历每个已选中 method 的 item 时，无论 `stencilCount` 是否为零，都执行以下状态机：

```cpp
if (writeRef != 0) {
    enableStencilOnceThroughCache();
    glStencilFunc(GL_LEQUAL, writeRef, 255);
    if (maskRef != 0) {
        glStencilMask(maskRef);
        glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
    } else {
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    }
} else if (maskRef != 0) {
    enableStencilOnceThroughCache();
    glStencilMask(maskRef);
    glStencilFunc(GL_ALWAYS, maskRef, 255);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
} else if (stencilTestEnabledCache) {
    glDisable(GL_STENCIL_TEST);
    stencilTestEnabledCache = 0;
}
```

若 `stencilCount >= 1`，遍历后只调 `RenderManager->EndStencil()`。没有
`glDepthMask(GL_TRUE)`，也不在 End 后重置全局 cache。软件 manager 同样不绕开
Begin/End；GL 调用只由具体构建平台是否存在 GL 决定，而不是运行时 `IsSoftware()`。

## 6. 点 offset 与 Bezier 网格

offset helper 先 reserve 输入 point 数。offset 与一个全局 `{0.0f, 0.0f}` sentinel
逐 bit/逐 float 相等时，直接把每个 float 坐标转换为 double；否则先做：

```cpp
float x = point.x + offset.x;
float y = point.y + offset.y;
out.emplace_back(double(x), double(y));
```

`Draw_GPU` 传入的 offset 是 `float(x) - 0.5f, float(y) - 0.5f`。geometry 0 的前三个
点也内联执行完全相同的 float-add-then-convert 规则。

Bezier helper 先为 X/Y division 建立 cubic basis 表，再按 `y = 0..divY`、
`x = 0..divX` 生成网格；每个采样点无条件读取 16 个 double control points。没有
`points.size() >= 16` 检查，也没有 `divX/divY >= 1` 检查。负 division 令对应 loop 不
执行；零 division 仍可能生成一行/一列采样，但后续 cell 数为零而不提交。

2026-08-16 的后续四端 fresh 指令复核已经闭合 basis 与 16-point accumulation 的逐操作
顺序：四个 basis 项均为固定的 scalar `FMUL` 链；每个 patch contribution 都先计算
`basisY*basisX`，再分别执行 `weight*coordinate` 和旧 accumulator 的 `FADD`，没有 FMA 或
重结合。NaN/Inf 传播和有限值舍入末位现也由位级回归锁定。详细证据见
`motionplayer_private_motion_bezier_scalar_order_four_binary_2026-08-16.md`。

## 7. affine 提交

四端 affine helper 的共同边界：

1. source rect 为空，返回 false；
2. source rect 任一边为负或超出 source texture 宽高，抛 `TVPOutOfRectangle`；
3. 通过 target-pair getter 按 `method->IsBlendTarget()` 得到 reference/writable；
4. clip rect clamp 到 writable target 的宽高；clamp 后为空，返回 false；
5. 目标六点为 `p0,p1,p2,p1,p2,p1-p0+p2`；
6. 源六点为 `(L,T),(R,T),(L,B),(R,T),(L,B),(R,B)`；
7. 一次 `OperateTriangles(method, 2, writable, reference, clipped, ...)`。

三点数量没有运行时检查；builder 的 geometry 0 构造约束提供三个点。

## 8. common mesh helper 与合并批次

common mesh helper 在四端还被 LayerBitmap/D3D 路径复用。PrivateMotionGLL 直接传入
自定义 callback，绕过外层 stretch-type wrapper。

正常输入的共同算法：

```cpp
sourceXs[i] = left + width  * i / divX; // double，不 floor/ceil
sourceYs[j] = top  + height * j / divY;

for each selected cell (row, col) {
    p00 = grid[row * stride + col];
    p10 = grid[row * stride + col + 1];
    p01 = grid[(row + 1) * stride + col];
    p11 = grid[(row + 1) * stride + col + 1];

    dst += {p00, p10, p01, p10, p01, p11};
    src += {s00, s10, s01, s10, s01, s11};
}

if (!dst.empty()) callback(sourceTexture, src, dst); // exactly once
```

这里的精确顺序是 `{00,10,01,10,01,11}`。callback 以 `dst.size()/3` 作为
triangle 数，把唯一的 source texture 与整批 source point vector 组成一个
texture-array element，再调用一次 `OperateTriangles`。顶点顺序、导数、退化边和
背面剔除方向均可观察，不能只按两组三角形覆盖的同一四边形来判断等价。

helper 在入口为 source texture 建立临时 owning 引用，在退出释放，以保证 callback 与
中间 vector 构建期间的生命周期。它还对 source rect 越界、软件纹理扩展/平铺、cell
可见性和 clip pruning 有额外分支。已确认：GPU 越界路径不会静默套用本地
floor/ceil 修正，软件路径会进入专门的 texture expansion helper；但该大型 helper 的
source-repeat helper、bitmap factory 交接和不对称 copy 已在 2026-08-15 重新四端闭合
（见 `motionplayer_common_mesh_repeat_handoff_four_binary_2026-08-15.md`）；每个
clip-pruning 算术边界仍需继续逐块核对。因此不能把整个异常输入面写成已 100% 解决。

## 9. deque 遍历与边界行为

共同遍历顺序：

```cpp
for (RenderItem &item : commands) {
    if (!item.sourceTexture) break;
    method = item.alphaTest
        ? selectAlphaTest(blend & 0xf, color, false)
        : selectOrdinary(blend & 0xf, color, false);
    if (!method) continue;

    applyStencilState(item); // 不受 stencilCount gate
    switch (item.geometryType) {
        case 0: submitAffine(...); break;
        case 1: offset; bezier; submitMesh(...); break;
        case 2: offset; submitMesh(...); break;
        default: break;
    }
}
```

`ResetClip()` 只在完整遍历/EndStencil 后执行；它不是 scope guard。提交或 helper 抛异常
时，C++ 栈对象会析构，但 `EndStencil()` 与 `ResetClip()` 不会被强制补调。这也是不能
用“更安全”的本地 RAII 改写隐藏原生边界的原因。

## 10. 本地过时实现差异与落地原则

| 本地旧行为 | 四参考共同行为 |
|---|---|
| software method fallback | 始终按名字取 GPU render method |
| 每次枚举 method parameter ID | 每个 selector 分支函数静态缓存 method/ID |
| target 同时作为 target/reference | writable 与原 Bitmap reference 分开 |
| 写/清 `CurrentDrawTarget` | 直接写 `UpdateBitmapForChild`，不碰 CurrentDrawTarget |
| 使用 GetDrawTargetBitmap 输出 clip | 使用 Layer `ClipRect` 平移后的副本 |
| stencil item 状态受 count/software gate | item 状态机始终运行 |
| 只清 stencil、结尾恢复 depth mask | 清 depth+stencil，结尾不恢复 depth mask |
| double offset 运算 | float 相加后转换 double |
| affine/Bezier/mesh payload 防御检查 | 依赖 builder invariant，非法 payload 可越界读取 |
| source 网格 floor/ceil | 精确 double 等分 |
| 每个 cell 一次 draw | 合并全部选中 cell，一次 draw |

落地时，正常 builder 产生的 payload 必须按右栏恢复。尚未闭合的大型 common-mesh
异常分支应保留 `_guess` 命名并在本文件记录，不以无证据的防御性行为冒充原版。

## 11. 落地与验证

本轮已把四端共同语义落到 `cpp/plugins/motionplayer/PrivateMotionGLL.cpp`：

- 删除 software blit-method fallback，按 blend/alpha-test 分支懒缓存 named method 与
  parameter ID；
- `Draw_GPU` 直接保存 `UpdateBitmapForChild`，以平移后的 Layer `ClipRect` 提交；
- BeginStencil 使用原位图纹理并清 depth+stencil；item stencil 状态机不再受 count 或
  software gate；EndStencil 不再自行恢复 depth mask；
- affine/mesh 提交按 `IsBlendTarget()` 分离 writable target 与 reference target；
- 点坐标按 float 精度相加后转换 double；Bezier 按 offset 后的 16 个 double 控制点生成；
- mesh source 坐标不再 floor/ceil，cell 顶点顺序精确保持为
  `00,10,01,10,01,11`，全部 cell 合并为一次 `OperateTriangles`；
- common-mesh 调用期间额外持有 source texture 引用，覆盖回调前的中间容器生命周期。

四个 recovery IDB 均已写入并保存以下语义名/注释：两个 selector、point offset、Bezier
grid、common mesh、affine submit、PrivateMotionGLL mesh callback、target-pair getter 及
全局 stencil cache byte。

验证结果：

1. `cmake --build out/web/debug -j 8` 成功完成 `PrivateMotionGLL.cpp` 编译、
   `libmotionplayer.a` 归档及最终 `index.html/index.wasm` 链接；只有既有的 `_tss`
   literal-operator、pthread memory-growth 与 JSPI 实验性警告；
2. 复用 `out/web/debug/compile_commands.json` 中真实 motionplayer 编译参数，对
   `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only` 成功；同样只有既有
   `_tss` warning；
3. `git diff --check` 成功；
4. 已去除 wasmtime render-stage trace 中旧单目标 Draw 绝对地址标签，避免继续把过时
   `libkrkr2.so` 地址呈现为当前证据。

本机虽已有 `out/wasmtime/debug/krkr2_wasmtime_guest.wasm`，但 motion-playback runner
所需的 `reference/xp3/logo_test_oracle_15hz.xp3` 不在工作区，因此本轮不能诚实宣称已
完成 private Draw 的像素级/批次级 Wasmtime 差分。并且现有 headless
`OperateTriangles` 是空实现，只能覆盖 Draw 调用到达与上层 trace，不能直接断言三角形
批次内容。后续应在不伪造 oracle fixture 的前提下，为 headless RenderManager 增加
只读 capture（method 名、target/reference identity、clip、triangle count 与顶点流），
再与四参考原生捕获进行比较。

## 12. 2026-08-15 fresh 四参考纠错：cell winding

本节取代本文第 8、10、11 节旧版曾写入的
`00,01,10,01,11,10` 结论。旧结论来自恢复过程中积累的过时叙述，不受当前四份
`reference/binaries/` 的逐指令结果支持；不能据此反向修改 portable 实现。

重新从四份 recovery IDB 的 common-mesh 函数尾部核对，得到一致的两组三角形：

| 目标 | 函数入口 | 本轮核对的 cell 展开区间 | source / destination 顺序 |
| --- | ---: | ---: | --- |
| Android ARM64 | `0x69AFE4` | `0x69BBDC`–`0x69C5E8` | `00,10,01,10,01,11` |
| Android ARMv7 | `0x575800` | `0x575EAC`–`0x576128` | `00,10,01,10,01,11` |
| iOS ARM64 | `0x1000F974C` | `0x1000F9E6C`–`0x1000FA1C8` | `00,10,01,10,01,11` |
| iOS ARMv7 | `0xF685C` | `0xF6F90`–`0xF7228` | `00,10,01,10,01,11` |

Android ARM64 可直接看到 source vector 的六次 pair 写入依次为：
`(x0,y0)`（`0x69BBDC`）、`(x1,y0)`（`0x69BCA4`）、
`(x0,y1)`（`0x69BD74`）、`(x1,y0)`（`0x69BE3C`）、
`(x0,y1)`（`0x69BF0C`）、`(x1,y1)`（`0x69BFE0`）。随后 destination
vector 从 `p00`、`p10`、`p01`、`p10`、`p01`、`p11` 对应槽位展开；另三端的
decompile/disassembly 显示同一序列。

当前 `MotionRenderBackend.cpp` 已经是该顺序，本轮没有改动运行时代码。新增 unit
case 直接调用 common-mesh helper，以一个 cell 锁定 source/destination 各六点、单次
callback、texture identity 和临时引用平衡，防止以后再次按旧文档“修反”。四份 IDB
入口的互相矛盾旧注释也已替换为单一 fresh 结论并原位保存。复用真实
`motionplayer_test_args.rsp` 的 Emscripten `-fsyntax-only` 验证通过（仅既有 `_tss`
warning）；`cmake --build out/web/debug -j 8` 返回 `ninja: no work to do`，说明现有
runtime 产物已是最新；`git diff --check` 通过。
