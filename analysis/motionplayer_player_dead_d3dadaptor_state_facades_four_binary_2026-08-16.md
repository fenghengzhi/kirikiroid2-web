# Motion.Player 误拷贝 D3DAdaptor state façade 四端清理（2026-08-16）

## 结论

继 bg/caption façade 清理后，本地 Player 还残留同一早期复制块中的四项：

```cpp
void setSize(tjs_int, tjs_int);
void setClearColor(tjs_int);
void setResizable(bool);
void removeAllTextures();
```

四端共同表明它们不属于 Motion.Player：

- 完整 92-member Player registrar 均无四名；
- `setClearColor`、`setResizable`、`removeAllTextures` 每端只有一个 UTF-16LE literal，
  全部 registrar code xref 只属于 D3DAdaptor；
- `setSize` 是 Layer/Window/render 辅助路径广泛使用的动态 method 名，不能依赖全局唯一性；
  但每端 D3DAdaptor registrar 的对应 row 可由局部 xref 精确定址，Player registrar 仍无
  row，recovery function-name 查询也没有 Player 同名函数；
- 本地 Player 的 setSize、setResizable、removeAllTextures 零 caller；setClearColor 只有一处
  draw-cache smoke test 调用；
- `_width/_height/_clearColor/_resizable` 除四个死 setter 外没有 Player 消费者；
- 真正的 D3DAdaptor 字段、setter、texture-map clear 和注册 wrapper 都有四端来源，必须保留。

本轮只删除 Player façade 与四个孤立字段。`D3DAdaptor::setSize/setClearColor/setResizable/
removeAllTextures` 及其对象生命周期不变。

## Registrar 映射

| 目标 | Motion.Player registrar | Motion.D3DAdaptor registrar |
|---|---:|---:|
| Android arm64 | `0x6D3DA8` | `0x6AA274` |
| Android armv7 | `0x597EC8` | `0x57CC58` |
| iOS arm64 | `0x1001244F8` | `0x1001039A4` |
| iOS armv7 | `0x123848` | `0x100D94` |

四端 fresh function-name 查询正则
`Player.*(setSize|setClearColor|setResizable|removeAllTextures)` 均返回零函数。

## D3DAdaptor row 的字符串/xref 身份

### `setSize`

`setSize` 在图像中并不唯一：Layer 构造、SourceCache bake、SLA、canvas render 和其他宿主
类也会动态调用同名 TJS method。以下只列当前 D3DAdaptor registrar 精确消费的 literal：

| 目标 | literal | D3DAdaptor registrar xref |
|---|---:|---|
| Android arm64 | `0x14D57F8` | `0x6AA3D0` |
| Android armv7 | `0xD85350` | `0x57CC82`, `0x57CC8A`；后继 pool `0x57CDAC` |
| iOS arm64 | `0x10195BC10` | `0x1001039E4` |
| iOS armv7 | `0x174DF74` | `0x100DCC`, `0x100DD2`, `0x100DDE` |

同一 literal 被 render code 动态读取不表示 Player 具有 C++ `setSize` member；动态 receiver
是 Layer-like dispatch。Player NCB 表内没有 `setSize` descriptor。

### 三个每端唯一名称

| 目标 | `setClearColor` literal / xref | `setResizable` literal / xref | `removeAllTextures` literal / xref |
|---|---|---|---|
| Android arm64 | `0x14D5AA2` / `0x6AA440` | `0x14D5ABE` / `0x6AA4B0` | `0x14D5AD8` / `0x6AA4BC`, `0x6AA4C4` |
| Android armv7 | `0xD855C2` / `0x57CC94`, `0x57CC9C` | `0xD855DE` / `0x57CCA6`, `0x57CCAE` | `0x57CDC8` / `0x57CCBA` |
| iOS arm64 | `0x10195BF90` / `0x100103A04` | `0x10195BFAC` / `0x100103A24` | `0x10195BFC6` / `0x100103A44` |
| iOS armv7 | `0x174E2F4` / `0x100DEA`, `0x100DF0`, `0x100DFC` | `0x174E310` / `0x100E08`, `0x100E0E`, `0x100E1A` | `0x174E32A` / `0x100E26`, `0x100E2C`, `0x100E38` |

Android armv7 前两项另有紧随 registrar 的 literal-pool data xref `0x57CDB4` 与
`0x57CDBC`；它们没有函数归属，也不是 Player edge。

## D3DAdaptor 的真实状态与行为

D3DAdaptor 构造保存 width/height 并据此创建 target texture。`setSize` 直接覆写两个 int，
后续 texture replacement 的 null fallback 会重新读取它们。`setClearColor` 与
`setResizable` 也分别保存真实 int/Boolean 字段；当前四参考没有后续消费者，但字段写入本身
是已注册、可观察的 native operation，不能因“暂时不读”从 D3DAdaptor 删除。

`removeAllTextures` 清理 D3DAdaptor 的 source-texture ordered map。该 map 的 mapped holder
对 texture 做 intrusive AddRef/Release；clear 会释放 map owner，但不会破坏调用者仍持有的
引用。它与 Player/SourceCache 的 source cache 不是同一容器。

## 本地 Player 偏差

旧 Player 字段/方法只形成孤立写入：

```cpp
setSize(w, h)       -> _width = w; _height = h;       // 无读者
setClearColor(c)    -> _clearColor = c;               // 无读者
setResizable(v)     -> _resizable = v;                // 无读者
removeAllTextures() -> _sourceCacheNative->clearCache(); // 零 caller 的错误跨对象转发
```

最后一项尤其不能用“同样会释放 texture”辩护：D3DAdaptor 原生方法清自己的 ordered texture
map，而 Player 版本转发到 SourceCache，改变的是另一套缓存、owner 和异常边界。

## 修正与验证

- 删除 Player 四个 declaration/body；
- 删除 Player `_width/_height/_clearColor/_resizable`；
- 删除 draw-cache smoke 对 Player `setClearColor` 的死调用；
- 真实 Player adaptor absence 回归扩展到四名，保持失败 result 不变；
- D3DAdaptor 四方法、四字段、registrar 与已有 typed/lifecycle 测试保持不变；
- 四份 recovery IDB 的 Player/D3DAdaptor registrar 已追加归属注释、强制回读并保存；
- Player header/body 中四方法与四字段零匹配；D3DAdaptor 实现仍完整保留；
- motionplayer 测试 TU Emscripten syntax-only 通过；
- `Web Debug Build` 最终链接通过；
- 限定 `git diff --check` 无新增内容级 whitespace error，仅有既有 CRLF 提示。

