# accurate SLA 入场、float clip 与 pass 边界四端复原（2026-08-16）

## 1. 结论

四个参考二进制把 accurate SeparateLayerAdaptor 分成两个明确函数边界：

```text
Player_renderToSeparateLayerAdaptor_guess(Player *, SLA *)
  -> prepareRenderItems(main, aux), failure returns
  -> applyPreparedRenderItemProjection(main)
  -> read cached ogl_accurate_render choice
  -> Player_renderAccurateSeparateLayerAdaptor_guess(Player *, SLA *, main, aux)
  -> updateAccurateSLAAfterDraw(SLA.targetLayer)
```

accurate renderer 本身不接收 target raw dispatch 或预先查询的 canvas width/height。它从
`SLA.targetLayer` 取得 owning Variant/object，构造 TJS `Layer` accessor，严格按 `width`、
`height` 顺序 PropGet。没有 native `tTJSNI_BaseLayer` 尺寸 fallback、`width/height>0` gate、
`hasMotionContent` gate 或 cached-native-ResourceManager 可用性 gate。

取得尺寸后，renderer 计算 particle outside rect，随后在 `buildRenderCommands` **之前**交换
SLA active/retired ordered maps 并把 per-pass sequence 清零。retired tree 只在函数正常尾清理；
异常展开不会执行该清理，留给下一次 begin swap。

## 2. 外层调用链与真实签名

| 目标 | prepare / result gate | projection | accurate call |
|---|---:|---:|---:|
| Android arm64 | `0x6D2A74` / `0x6D2A78` | `0x6D2A84` | `0x6D2B38` |
| Android armv7 | `0x597358` / `0x59735C` | `0x597368` | `0x5973D8` |
| iOS arm64 | `0x1001233FC` / `0x100123400` | `0x10012340C` | `0x1001234CC` |
| iOS armv7 | `0x1225E8` / `0x1225EC` | `0x1225FA` | `0x1226D2` |

四端 accurate call ABI 都是 `Player, SLA, &mainList, &auxList`。调用返回值不参与分支，随后
立即 CopyRef `SLA.targetLayer` 并调用 post-draw。原本本地实现额外传入 raw target、width、
height 并返回 bool；这既隐藏了 TJS 尺寸读取身份，也制造了参考二进制没有的 early return。

外层旧实现还在 prepare 前调用 `hasMotionContent()`，忽略 `prepareRenderItems` 的 bool 结果，
并在读取 accurate 配置前用 native Layer 查询尺寸。四端相反：prepare 结果是唯一的此阶段
admission，projection 完成后才读取静态配置，accurate target 尺寸属于被调函数内部。

## 3. target Layer 与尺寸读取

| 目标 | target Variant copy / AsObject | Layer accessor | width dispatch | height dispatch |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C70DC..0x6C711C` | `0x6C7120..0x6C712C` | `0x6C7130..0x6C7150` | `0x6C7158..0x6C7178` |
| Android armv7 | `0x590494..0x5904A6` | `0x5904AA..0x5904B4` | `0x5904B8..0x5904D4` | `0x5904DA..0x5904F4` |
| iOS arm64 | `0x10011AA38..0x10011AA54` | `0x10011AA58..0x10011AA64` | `0x10011AA68..0x10011AA88` | `0x10011AA90..0x10011AAB0` |
| iOS armv7 | `0x118DA8..0x118DFE` | `0x118E02..0x118E16` | `0x118E1A..0x118E46` | `0x118E4C..0x118E78` |

两个 PropGet 的结果直接解释为 signed int32，再各自转 float，组成两份独立的
`[0,0,width,height]`：一份传给 `computeParticleOutsideRect_guess`，另一份不经 outside-factor
变换地传给 `buildRenderCommands_guess`。零、负值也沿该管线传播。

## 4. begin/build/end 的正常与异常边界

| 目标 | begin swap / sequence=0 | build commands | normal-tail retired cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6C71A8..0x6C7240`（inline） | `0x6C7254` | `0x6C891C..0x6C8920` |
| Android armv7 | `0x590544..0x590550` | `0x59055E` | `0x5918A2..0x5918A4` |
| iOS arm64 | `0x10011AAE4..0x10011AAF4` | `0x10011AB08` | `0x10011BEEC..0x10011BEF0` |
| iOS armv7 | `0x118ED8..0x118EE8` | `0x118EFA` | `0x11A476..0x11A47E` |

空 main list 也直接汇入 normal-tail cleanup。四端异常 landing pads 只析构已构造的 Variant、
vector 和 raw object owners，最终 resume unwind；没有 cleanup 调用。因此不能用无条件 RAII
guard 把 `endLayerPass_guess` 放进析构函数，也不能把 begin 移到 build 之后。

## 5. item admission

每次迭代先检查两个相邻 flag byte，再检查 raw opacity：

| 目标 | `skipFlag0/rawFlag16` loop gate | opacity load / zero gate |
|---|---:|---:|
| Android arm64 | `0x6C88F8..0x6C8908` | `0x6C72CC..0x6C72D4` |
| Android armv7 | `0x591888..0x591894` | `0x59063C..0x590644` |
| iOS arm64 | `0x10011BEC8..0x10011BED8` | `0x10011AB88..0x10011AB90` |
| iOS armv7 | `0x11A45E..0x11A468` | `0x118F7A..0x118F80` |

等价条件只有：

```text
skipFlag0 == false && rawFlag16 == false && opacity != 0
```

vector 元素被直接解引用，不检查 null；也没有 `sourceKey.empty()` admission。旧 Web gate 会把
command source 为空但仍具有原始 Variant/source descriptor 的 item 静默丢弃，并把 malformed
null element 从原版的失败边界改成 continue。

## 6. float clip 数值语义

四端 clip block：

| 目标 | paint/canvas + viewport intersection | final reversed-edge gate |
|---|---:|---:|
| Android arm64 | `0x6C72DC..0x6C7370` | `0x6C7374..0x6C7380` |
| Android armv7 | `0x590666..0x5907D4` | `0x5907D8..0x5907EA` |
| iOS arm64 | `0x10011AB98..0x10011AC2C` | `0x10011AC30..0x10011AC38` |
| iOS armv7 | `0x118FAA..0x119126` | `0x11912A..0x11913C` |

伪代码语义为：

```cpp
left   = fmax(paint.left, 0.0f);       // numeric-maximum NaN behavior
top    = fmax(paint.top, 0.0f);
right  = paint.right  < float(width)  ? paint.right  : float(width);
bottom = paint.bottom < float(height) ? paint.bottom : float(height);

if(viewport.right >= viewport.left && viewport.bottom >= viewport.top) {
    vl = floorf(viewport.left);
    vt = floorf(viewport.top);
    vr = ceilf(viewport.right);
    vb = ceilf(viewport.bottom);
    left   = vl < left   ? left   : vl;
    top    = vt < top    ? top    : vt;
    right  = right < vr  ? right  : vr;
    bottom = bottom < vb ? bottom : vb;
}

if(right < left || bottom < top) skip;
```

最后两次 FP compare 使用 MI/strict-negative 跳转，而不是 `<=`：零宽和零高 clip 继续渲染。
unordered/NaN 也不会触发最终 skip。viewport 有 NaN 时 ordered `>=` gate 为 false，不执行
floor/ceil intersection；paint/canvas 分支的 NaN 选择由 `fmax` 与显式 compare/select 保持。

clip 四边始终是 float。参考实现不会先转 int：fractional left/top 和 extent 原样进入
`setSize(Real)`、`-0.5-clipOrigin` geometry/debug offset、祖先 mask offset 差以及最终
`setPos(Real,Real)`。旧 `static_cast<int>` 加二次正 extent gate 同时改变普通小数、零 extent、
NaN 与越界值。

## 7. 源码落地与验证

`cpp/plugins/motionplayer/Player.h` / `PlayerRenderTargets.cpp`：

- accurate helper 恢复为 `void(Player,SLA,main,aux)` 数据流；
- target Variant/TJS Layer accessor 和 width→height PropGet 移回 helper 内部；
- 删除 native Layer 尺寸 fallback与正尺寸/content/source-cache early return；
- SLA begin 移到 build commands 之前，end 仍只位于正常尾；
- outer 恢复 prepare bool gate 与 projection-before-config 顺序；
- item admission 删除 source-key 和 null-pointer 容错；
- clip 恢复 float、strict reversed-edge 与 compare/select NaN 语义。

2026-08-16 当前验证：

- ordinary Emscripten 单元翻译单元语法检查：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 语法检查：通过；
- `Web Debug Build` 的 `motionplayer` target：30/30，通过；
- `Wasmtime Headless Debug Build` 的 `motionplayer` target：30/30，通过；
- 完整 `Web Debug Build`：3/3，通过。

输出只有项目既有的 `_tss`、imagepacker `nodiscard`、pthread + memory-growth、JSPI 与 JS
library warning；本轮没有新增编译或链接错误。
