# MotionPlayer Player direct/D3D batch renderer（四参考二进制）

日期：2026-08-13

## 1. 结论

`Player` 的 direct-texture 入口与 `D3DAdaptor` 入口最终进入同一 raw renderer。它不是
`PrivateMotionGLL::Draw_GPU` 的逐 item 版本，也不是另一套 mesh rasterizer；原版在栈上
构造一个约 120-byte（64 位）/74-byte（32 位关键字段范围）的 batch accumulator，持有：

- 当前 render method；
- 当前 source/target/reference texture；
- 两个 `std::vector<tTVPPointD>`（destination/source），flush 后只回退 end，保留 capacity；
- 当前 clip rect；
- render manager；
- stencil mask/write 两个 byte；
- packed color、blend low nibble、`alphaOpAdd`、`alphaTest` 方法缓存键。

同一批次可跨多个 prepared item 合并。stencil、method、source/target、clip 或 packed
color 改变时先 flush；**reference texture 不在 append 的批次键中**，reference-only 变化会
继续合并并沿用先前缓存值。正常函数尾声再 flush 一次，然后在 stencil 被分配时调用
`EndStencil()`。异常展开只清理 callable/vector 等 C++ 对象，不补做正常尾声的 flush 或
`EndStencil()`。该不对称键已于 2026-08-15 重新逐端确认，详见
`motionplayer_triangle_batch_container_four_binary_2026-08-15.md`。

## 2. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `D3DAdaptor_renderFromPlayer_guess` | `0x6AB2E0` | `0x57D356` | `0x100104360` | `0x10177E` |
| `Player_renderPreparedItemsToD3DTexture_guess` | `0x6AB39C` | `0x57D3DC` | `0x100104450` | `0x101850` |
| `Player_drawToTexture_guess` 调 raw renderer 的点 | `0x6D3280` | `0x5977AC` | `0x100123AB8` | `0x122D92` |
| source-getter invoke wrapper | 内联/未单列 | 内联/未单列 | `0x100128644` | `0x1279D4` |
| batch stencil transition | `0x6D8C98` | `0x59A948` | `0x1001286A4` | `0x127A1E` |
| batch method transition | `0x6D8E3C` | `0x59AA38` | `0x1001287BC` | `0x127AF6` |
| batch append | `0x6D9290` | `0x59AD20` | `0x100128AFC` | `0x127DAA` |
| batch flush | 内联于前述 helper | `0x59ADD8` | `0x100128C08` | `0x127E6A` |
| common mesh backend | `0x69AFE4` | `0x575800` | `0x1000F974C` | `0xF685C` |

raw renderer 对 common mesh backend 各有两个直接调用（composite mesh 与 Bezier）：

- Android arm64：`0x6AB660`、`0x6AB740`
- Android armv7：`0x57D64A`、`0x57D724`
- iOS arm64：`0x1001046FC`、`0x1001048E0`
- iOS armv7：`0x101F26`、`0x102008`

这证明 direct/D3D 与 `PrivateMotionGLL` 共用 mesh 构造/裁剪后端；affine 则在 raw
renderer 内直接生成六个 target/source vertex 后交给 batch append。

## 3. 两个入口的 callable 与 target/reference

### 3.1 D3DAdaptor

`D3DAdaptor_renderFromPlayer_guess` 先检查 adaptor 的 `canvasCaptureEnabled` byte，建立
捕获 `{adaptor, player}` 的 source getter，调用 RenderManager `SetRenderTarget()`，再用
`(0,0,width,height)` 与 `(0.5f,0.5f)` 进入 raw renderer。

source getter 映射：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x6EE440` | `0x5AC518` | `0x10014019C` | `0x1414C0` |

它先读 persistent source descriptor 的 texture；为空时执行当前 atlas/layer load 链。
若进程默认 renderer 是 software，则按原 source texture identity 查 adaptor 的树形 map；
miss 时由私有 GPU manager 创建 static texture copy 并插入，hit 时复用 mapped texture。

target callback 映射：`0x6EE8AC` / `0x5AC830` / `0x1001405A0` / `0x141822`。
四端都忽略 `method->IsBlendTarget()` 布尔输入，返回 adaptor target texture 两次，所以
batch 的 target/reference 相同。

### 3.2 Player direct texture

source callback 映射：`0x6F3BAC` / `0x5B057A` / `0x100146084` / `0x1465BE`。
它只返回 prepared item 所指 persistent descriptor 的当前 texture，不执行 atlas retry。

target callback 映射：`0x6F3C28` / `0x5B05B8` / `0x1001460FC` / `0x146608`。
它同样忽略 `IsBlendTarget()` 并返回传入 target texture 两次。

direct wrapper 自己先 `SetRenderTarget(target)`，以 texture 实际 width/height 组成 clip，
并把调用者的 `x/y` float 原样传给 raw renderer。

## 4. raw renderer 的数据流

### 4.1 prepare 阶段

`priorDraw == false` 时：

1. 清所有 item 的 stencil mask/write byte；
2. 根据 mask-parent 链分配 8-bit stencil ref；计数超过 255 时只记录一次
   `StencilCount overflow(256)`，byte 自然截断；
3. 用 canvas 范围与 viewport 计算 float clip，写回 item；无交集或 rawFlag16 时清 draw byte；
4. 清 item 内的临时 layer Variant。

`priorDraw == true` 时跳过这整段，stencil count 为零。

### 4.2 stencil begin

只在计数至少 1 时调用 `BeginStencil(target)`，然后：

- disable depth test；
- stencil mask = 255；clear stencil = 0；
- `glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)`；
- stencil op = replace/keep/keep；
- depth mask false；
- disable stencil test，并把共享 enable cache 清零。

raw renderer 本身不调用 `SetRenderTarget()`；两个 wrapper 在进入前完成它。

### 4.3 item gate 与 source

逐 item 按以下顺序跳过：

- `(blend & 0x0f) == 6`；
- skipFlag0 或 rawFlag16；
- priorDraw 且 item 未标 prior-draw；
- source descriptor `blank` byte 非零；
- priorDraw 时 opacity 以朝零除法减半；`opacity <= 0 && stencilMaskRef == 0`；
- 调 source getter；随后重新读取同一个 descriptor 的 rect；rect 宽/高必须正。

空 `std::function` getter 会抛 `std::bad_function_call`，并非返回 null。原版没有对 getter
返回的 texture 做 null guard；后续使用保留其自然 fault/exception 边界。

### 4.4 stencil/method transition 顺序

每个 surviving item 先调用 batch stencil transition，再选择 method：

```text
if stencil key changed:
    flush pending triangles
    cache mask/write
    update GL stencil state

if method key changed:
    flush pending triangles
    cache packedColor/blend/alphaOpAdd/alphaTest
    call shared named render-method selector
```

method selector与 `PrivateMotionGLL` 共用同一组 process-static method/parameter-ID cache。
raw renderer 四端传给 selector 的 `alphaOpAdd` 均是 literal `true`，并不读取
`D3DAdaptor.alphaOpAdd`。alpha-test 由 stencil mask byte 非零决定，threshold 固定 64。
没有 software `GetRenderMethod(opacity, hda, bm*)` fallback。

### 4.5 geometry

- affine：用 item 的三个 float 点分别执行 float `+ offset`，再转 double；第四点为
  `p1 - p0 + p2`。target/source 顺序均为 `00,10,01,10,01,11`。调用
  `method->IsBlendTarget()`，但两个 wrapper target callback 都忽略其结果并返回同一纹理对。
- composite mesh：先对所有 float point 加 raw renderer offset，再调用 common mesh backend。
- Bezier：先 offset 16 control points，按 command division 与 source width/height 算 cell
  division，tessellate 后进入同一个 common mesh backend。

common mesh backend负责几何/clip pruning，将所有 surviving cell 汇成一对连续 vertex
range 后调用一次 type-erased callback 交给 batch append。batch append 在 state 不变时把
range 继续追加，因此不是旧 port 的“每 cell 一次 `OperateTriangles`”。

### 4.6 batch append/flush 与生命周期

64 位布局由 iOS arm64 helper 的固定偏移交叉确认：

| offset | field |
|---:|---|
| `+0` | method |
| `+8` | source texture |
| `+16` | target texture |
| `+24` | reference texture |
| `+32/+40/+48` | destination vector begin/end/cap |
| `+56/+64/+72` | source vector begin/end/cap |
| `+80..+95` | clip rect |
| `+96` | render manager |
| `+104/+105` | stencil mask/write key |
| `+108` | packed color |
| `+112` | blend low nibble |
| `+116/+117` | alphaOpAdd/alphaTest |

flush 的 triangle count 是 `destination.size()/3`；source texture array 只有一个 element。
flush 后两个 vector 的 `end = begin`，capacity 不释放。正常尾声 flush 后才
`EndStencil()`；两个 vector 按 C++ 逆序析构并释放 capacity。

## 5. 本轮源码落地

- 新增 `cpp/plugins/motionplayer/MotionRenderBackend.{h,cpp}`：共享 named selector、共享
  stencil enable cache、stencil begin/apply/end 与 `TriangleBatch_guess`。
- `PrivateMotionGLL.cpp` 改用共享 selector/stencil backend，消除两个 translation unit 各自
  缓存造成的可观察偏差。
- `PlayerRenderTargets.cpp`：
  - raw renderer 参数移除过时的 adaptor `alphaOpAdd` 分支；固定 selector bool 为 true；
  - wrapper 自己 `SetRenderTarget()`；D3DAdaptor offset 改为原版 `0.5f,0.5f`；
  - source descriptor blank gate；移除原版不存在的 texture null/dimension guard；
  - affine float-add-then-double 与第四点重建；
  - mesh exact double source subdivision及 `00,10,01,10,01,11` 顺序；
  - 跨 item batch append/flush；
  - 移除 exception-path stencil RAII、`glDepthMask(true)` 与软件 method fallback；
  - 清除 `0x6ADFBC` 旧 libkrkr2 地址式函数名/trace label。
- 四个 recovery IDB 已写入上述语义名与注释并保存。

## 6. 验证与尚存精度边界

已通过：

- `cmake --preset "Web Debug Config"`（加载本机 Emscripten 环境）；
- `cmake --build out/web/debug -j 8`，完整链接 `index.html/index.js/index.wasm`；
- `git diff --check`。

仍需继续收紧：

1. common mesh backend 的 clip pruning、单次回调、software repeat handoff、winding 与
   callback/Release/bounds-commit 时序已于 2026-08-15 另行逐端闭合；本文件原先“尚未迁移、
   一次或多次回调”的备注已失效并在本次迁移中删除。
2. 本仓库缺少已记录的 `reference/xp3/logo_test_oracle_15hz.xp3`，本轮无法做对应 oracle
   frame/pixel replay；Wasmtime headless 的 `OperateTriangles` 仍为空实现，不能把它当像素验证。
3. batch 的 C++ source 字段顺序已按四端偏移表达语义，但 Web STL/ABI 不追求与 libc++
   对象字节布局相同；原版对象结构证据保留在本文件与 recovery IDB。
