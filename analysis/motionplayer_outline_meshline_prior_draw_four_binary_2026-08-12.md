# MotionPlayer `outline` / `meshline` / `priorDraw` 四参考二进制复原

日期：2026-08-12

## 1. 结论

四份参考二进制共同证明：

- `Player::outline` 与 `Player::meshline` 都是持久 `tTJSVariant` owner，默认
  `Void`；getter 执行 `CopyRef`，setter 执行 Variant 拷贝赋值，不经过
  `ttstr` 转换；
- `Player::priorDraw` 是独立的 typed Boolean byte，默认 `false`；它不是
  `double`/Real 属性；
- `EmotePlayer::outline` 把输入先复制为临时 Variant，再拷贝赋值给其内嵌
  `Player`，随后析构临时值；getter CopyRef 内嵌 Player 的 Variant；
- `EmotePlayer::priorDraw` 直接转发内嵌 Player 的 Boolean byte；
- `outline` 和 `meshline` 不只是一组存储属性：普通 Layer/Canvas 提交在每个
  render item 的图像 primitive 之后，以二者“至少一个非 Void”为门控，根据
  `meshType` 提交轮廓 primitive；
- `priorDraw` 与轮廓样式互相独立。它控制 render-command 重建、提交过滤、
  opacity 除二以及 D3D/Layer 路径的先画行为，并不改变两个 Variant 的类型或
  所有权。

因此，旧移植把 `outline`/`meshline` 存成 `ttstr`，以及把 EmotePlayer 的
`priorDraw` 包装成 `double`，都与当前四个权威参考二进制不一致。旧移植同时完全
漏掉了普通 Canvas 提交后的轮廓 primitive 分支。

## 2. 宽字符串与注册

### 2.1 唯一 UTF-16LE 字符串

| 属性 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `outline` | `0x14D3E7A` | `0xD84830` | `0x10195CB7C` | `0x174EEE0` |
| `meshline` | `0x14D647A` | `0xD85D88` | `0x10195CB8C` | `0x174EEF0` |
| `priorDraw` | `0x14D3E8A` | `0xD84840` | `0x10195C670` | `0x174E9D4` |

三个名称都以完整 UTF-16LE（含终止零）搜索；每个目标在每份二进制中均只有一个
命中。Android 两端的 `outline` 与 `meshline` 位于不同常量池，不能依赖相邻地址
猜测；iOS 两端二者相邻。

### 2.2 Player 注册点与访问器

| 端 | `priorDraw` 注册 / get / set | `outline` 注册 / get / set | `meshline` 注册 / get / set |
|---|---|---|---|
| Android arm64 | `0x6D4570` / `0x6D6A28` / `0x6D6A30` | `0x6D49B0` / `0x6D6B10` / `0x6D6B1C` | `0x6D4A28` / `0x6D6B24` / `0x6D6B30` |
| Android armv7 | `0x5980AC` / `0x598E76` / `0x598E7C` | `0x5981BA` / `0x598F8A` / `0x598F98` | `0x5981D8` / `0x598FA0` / `0x598FAE` |
| iOS arm64 | `0x1001247B8` / `0x100125570` / `0x100125578` | `0x100124944` / `0x100125648` / `0x100125654` | `0x100124970` / `0x10012565C` / `0x100125668` |
| iOS armv7 | `0x123AC8` / `0x12476E` / `0x124774` | `0x123C42` / `0x12486E` / `0x12487C` | `0x123C6C` / `0x124884` / `0x124892` |

四份 IDB 将这些函数统一命名为：

- `Player_getPriorDraw_guess` / `Player_setPriorDraw_guess`；
- `Player_getOutline_guess` / `Player_setOutline_guess`；
- `Player_getMeshline_guess` / `Player_setMeshline_guess`。

### 2.3 EmotePlayer 注册点与访问器

| 端 | `outline` 注册 / get / set | `priorDraw` 注册 / get / set |
|---|---|---|
| Android arm64 | `0x67D9C0` / `0x67F1D4` / `0x67F1E4` | `0x67DA40` / `0x67F258` / `0x67F264` |
| Android armv7 | `0x5615AC` / `0x561FA4` / `0x561FB8` | `0x5615CC` / `0x562010` / `0x56201A` |
| iOS arm64 | `0x1001B54FC` / `0x1001B6078` / `0x1001B6088` | `0x1001B5528` / `0x1001B60DC` / `0x1001B60E8` |
| iOS armv7 | `0x1B5174` / `0x1B5DD4` / `0x1B5DE8` | `0x1B51A0` / `0x1B5E94` / `0x1B5E9E` |

统一命名为 `EmotePlayer_getOutline_guess`、
`EmotePlayer_setOutline_guess`、`EmotePlayer_getPriorDraw_guess`、
`EmotePlayer_setPriorDraw_guess`。

## 3. Player 字段布局与访问语义

| 端 | `outline` Variant | `meshline` Variant | `priorDraw` byte |
|---|---:|---:|---:|
| Android arm64 | `+1032` | `+1052` | `+1096` |
| Android armv7 | `+708` | `+720` | `+748` |
| iOS arm64 | `+920` | `+940` | `+984` |
| iOS armv7 | `+644` | `+656` | `+684` |

四端 fresh decompile 的共同伪代码为：

```cpp
tTJSVariant getOutline() const { return CopyRef(player.outline); }
void setOutline(const tTJSVariant &v) { player.outline = v; }

tTJSVariant getMeshline() const { return CopyRef(player.meshline); }
void setMeshline(const tTJSVariant &v) { player.meshline = v; }

bool getPriorDraw() const { return player.priorDraw; }
void setPriorDraw(bool v) { player.priorDraw = v; }
```

Android arm64 的 Boolean setter 显式出现 `a2 & 1`；另外三端直接写入已由 typed
property wrapper 规范化的 byte。两种机器码都是同一源级 `bool` 语义，不能据此把
armv7/iOS 参数提升为任意整数。

Variant getter 都调用平台对应的 CopyRef/copy-constructor helper；setter 都调用
同一类 `tTJSVariant_copyAssign_guess` helper。任何输入类型（Object、Integer、Real、
String、Void）都原样保留；不存在 `AsString()`。

## 4. 构造、析构与 owner 生命周期

构造函数：

| 端 | `Player_ctor_guess` | `priorDraw=false` 的存储点 |
|---|---:|---:|
| Android arm64 | `0x6CC110` | `0x6CC4D4` |
| Android armv7 | `0x5935C4` | `0x5937EE` |
| iOS arm64 | `0x10011EC04` | `0x10011EE30` |
| iOS armv7 | `0x11D488` | `0x11D818` |

`outline`、`meshline` 作为普通 Variant 成员默认构造为 Void；`priorDraw` 构造为
false。

析构函数与连续释放点：

| 端 | `Player_dtor_guess` | `tags` | `meshline` | `outline` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6CCEBC` | `0x6CD01C` | `0x6CD024` | `0x6CD02C` |
| Android armv7 | `0x593C24` | `0x593C98` | `0x593CA0` | `0x593CA8` |
| iOS arm64 | `0x10011F2A0` | `0x10011F32C` | `0x10011F334` | `0x10011F33C` |
| iOS armv7 | `0x11DCC4` | `0x11DD96` | `0x11DDA0` | `0x11DDAA` |

顺序固定为 `tags -> meshline -> outline ->` 更早构造的 Variants，证明二者是成员
owner 而非借用的字符串指针或 transient render 参数。getter 产生的副本对 Object
dispatch 独立 AddRef，可安全越过 Player 析构。

## 5. EmotePlayer 转发与临时对象

内嵌 Player 指针位置：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `+1064` | `+532` | `+696` | `+348` |

四端 setter 的共同结构不是字符串转发：

```cpp
void EmotePlayer::setOutline(tTJSVariant incoming) {
    tTJSVariant temporary = CopyRef(incoming);
    embeddedPlayer->outline = temporary;
    temporary.~tTJSVariant();
}
```

getter 直接 CopyRef 内嵌 Player 的 `outline`。`priorDraw` getter/setter 则直接读取/
写入内嵌 Player byte，所以 NCB 公共 ABI 是 Boolean：TJS getter 产出 Integer Boolean，
不是 Real。

## 6. Canvas 轮廓消费者

共同消费者：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |

它们均为 `Player_renderToCanvas_guess`。渲染循环先完成本 item 的图像提交与相关
临时对象释放，然后执行：

```cpp
if(outline.Type() != tvtVoid || meshline.Type() != tvtVoid) {
    switch(item.meshType) { /* frame primitive */ }
}
```

类型门控点分别为 `0x6C59BC`、`0x58F27A`、`0x100119574`、`0x117B5A`。
它检查 Variant 的 type tag，不执行 truthiness 或字符串空值判断。因此：

- `Integer(0)` 仍然启用；
- 空字符串仍然启用；
- Object 仍然以同一 dispatch owner 转发；
- 只有两者同时 Void 才跳过。

### 6.1 `meshType == 0`：四条 `drawLine`

共同调用：

```cpp
for(int edge = 0; edge < 4; ++edge) {
    int next = (edge + 1) & 3;
    layerClass.FuncCall(
        "drawLine",
        CopyRef(outline),
        corners[edge].x - 0.5,
        corners[edge].y - 0.5,
        corners[next].x - 0.5,
        corners[next].y - 0.5,
        objthis = targetLayer);
}
```

参数数目固定为 5；四个坐标是 Real Variant。循环从 edge `-1` 递增到 0 的编译形态
只是优化结果，逻辑边序为 `0->1, 1->2, 2->3, 3->0`。每次调用后反向析构本次五个
参数。调用返回值被忽略，失败不终止后续边。

### 6.2 `meshType == 2`：`drawMeshFrame`

调用为：

```cpp
drawMeshFrame(
    CopyRef(outline), CopyRef(meshline),
    buildInterleavedPointArray(item.commandCompositeMeshPoints, -0.5, -0.5),
    item.meshDivX, item.meshDivY);
```

参数数目固定为 5。点数组 helper 遍历 item 的 composite mesh vector，将每点的 x/y
加上 `{-0.5f,-0.5f}` 后，以 Real Variant 交错压入新 TJS Array。

### 6.3 `meshType == 1`：Bezier 两支

先从 item 的 Bezier world point vector 构造同样偏移的交错数组。随后仅检查
`meshline.Type()`：

- `meshline != Void`：
  `drawBezierPatchMeshFrame(outline, meshline, points, divX, divY)`，argc=5；
- `meshline == Void`：
  `drawBezierPatchFrame(outline, meshline, points)`，argc=3；第二参数仍明确传入
  Void meshline，并未省略或以 outline 替代。

五参支路的分割算法是 unsigned 管线：

```cpp
width = sat_u32_toward_zero(sourceWidth);
height = sat_u32_toward_zero(sourceHeight);
split = (uint32(division) * width) / (width + height); // W-register wrap
divX = split + 1;
divY = uint32(division) - split + 1;
```

与统一后的 `renderBezierPatchCellDivisions_guess` 相同。两个 AArch64 reference 的
inline `UDIV` 对零 denominator 返回 0；两个 ARMv7 reference 把该边界交给外部
runtime helper，Web 采用直接可证的 AArch64 profile。完整四端地址、转换饱和和
wrap 边界见
`motionplayer_render_bezier_cell_division_four_binary_2026-08-14.md`。两支 dispatch
返回值均被忽略。

### 6.4 其他 `meshType`

`meshType` 既非 0/1/2 时不提交轮廓 primitive。共同 switch 没有 fallback。

## 7. `priorDraw` 数据流

`priorDraw` 位于同一 Player 属性区，但不是上面 outline switch 的条件。四端
`Player_renderToCanvas_guess` 都在进入 item 循环前及循环内多次读取它：

- false 时清理/重建普通 render commands；true 时复用先前准备状态；
- true 时要求 item 的 prior-compatible byte；
- true 时 opacity 使用有符号 `/ 2`（向零舍入）；
- 同一 byte 也被 D3D texture、SeparateLayerAdaptor 与相关预提交路径读取。

本纵切面未改变项目中已经按四端恢复的这些消费者；本次修复的是公共 typed
Boolean ABI，避免 EmotePlayer wrapper 将它错误注册成 Real。

## 8. 本地源码修复

- `Player.h`
  - `_outline`、`_meshline`：`ttstr -> tTJSVariant`；
  - getter 返回 Variant CopyRef；setter 使用拷贝赋值而非字符串或 move 转换；
  - `_priorDraw` 保持 `bool` 且默认 false。
- `EmotePlayer.h`
  - EmotePlayer 与内部 D3D wrapper 的 outline 转发改为 Variant 拷贝；
  - priorDraw 转发改为 `bool`，使 NCB 注册产生 Integer Boolean ABI。
- `PlayerRenderInternal.{h,cpp}`
  - 新增 `drawRenderItemFrame_guess`；
  - 复原两-Variant 门控、0/1/2 分型、方法名、argc、参数类型、`-0.5`
    偏移、Bezier division 和忽略返回值行为。
- `PlayerRenderExecute.cpp`
  - direct 图像提交后、buffered `operateRect` 后都调用同一 frame helper；
  - 分支位于图像 primitive 之后，保持原版调用时序。
- `main.cpp`
  - 属性注释更新为 Variant owner / Boolean，并移除本纵切面过时的单一
    `libkrkr2.so` 地址说法。

## 9. 测试覆盖

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增/扩展：

- Player 默认 `outline/meshline=Void`、`priorDraw=false`；
- Object owner 传入/getter 同 dispatch；Integer outline 经 NCB setter 不被字符串化；
- `priorDraw` 的零/非零 typed Boolean 转换，getter 类型必须是 `tvtInteger`；
- 两个 getter Object alias 都可越过 Player 析构并继续访问 dispatch；
- EmotePlayer C++ wrapper 保留 Variant 与 bool；其实际 NCB adaptor 另行验证
  Integer outline 不被字符串化、priorDraw getter 仍产出 Integer Boolean；
- frame helper 的两-Void gate；
- affine 四条闭合边的调用次数、顺序、`-0.5` Real 坐标与失败仍继续；
- type 2 `drawMeshFrame` 的 Variant/Array/Integer 参数布局；
- type 1 meshline Void 的三参 `drawBezierPatchFrame`；
- type 1 meshline 非 Void 的五参 `drawBezierPatchMeshFrame` 和 unsigned division；
- type > 2 不调用。

## 10. IDB 回写

四份 IDB 均完成：

- 40 个 Player/EmotePlayer 访问器统一命名；
- 访问器写入字段类型、偏移、CopyRef/copyAssign、typed Boolean 与临时 Variant
  生命周期注释；
- 15 个公共属性注册点写入类型注释；
- 构造、析构和 outline gate/四个 geometry 分支写入语义注释；
- 每端 10 个访问器以及 ctor/dtor/renderToCanvas 共 13 个函数均强制刷新，并在当前
  对话中 fresh decompile；
- 四份数据库均成功原位保存。

## 11. 验证

- 使用 Web `compile_commands.json` 的真实 Emscripten 参数，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过；唯一诊断
  是仓库既有 `_tss` literal-operator 弃用警告；
- `cmake --build out/web/debug --target motionplayer --parallel 8`：通过；
- `cmake --build out/wasmtime/debug --target motionplayer --parallel 8`：通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest --parallel 8`：
  通过，包括最终 `wasm-opt`；
- `cmake --build out/web/debug --target krkr2 --parallel 8`：通过；
- `cmake --build out/web/debug --parallel 8`：完整默认 Web target 通过；
- 上述两个静态库、Wasmtime guest、Web `krkr2` 与完整 Web target 连续复验均为
  `ninja: no work to do`；
- `git diff --check`：通过；仅输出工作树既有 LF/CRLF 转换警告。

首次 Wasmtime guest 复验期间，前一条超过工具等待窗口的同目标 Ninja 仍在后台
执行 `wasm-opt` 并占用输出文件，使重叠启动的 linker 一次返回 Windows
`permission denied`。等待原构建完成后，产物正常生成；随后相同命令成功并获得
no-work 结果，故这不是源码或链接符号失败。
