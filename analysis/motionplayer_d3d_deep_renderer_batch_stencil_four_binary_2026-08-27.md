# Motion shared D3D deep renderer、TriangleBatch 与 stencil 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制共享同一个 D3D deep renderer。它由 D3DAdaptor capture 路径和 D3DLayer
direct-texture 路径共同调用，负责 stencil 引用预处理、clip 发布、逐 item admission、source
texture 取得、method 选择、affine/mesh 分发、TriangleBatch 合并/flush，以及正常路径上的
BeginStencil/EndStencil 配对。

本轮确认本地大部分 item、method 和 batch 语义已经正确，但发现并修复四处结构/边界偏差：

- 恢复原版第二个 type-erased target getter。它返回 `(referenceTexture, targetTexture)`；
  D3DAdaptor callable 在每次实际提交时重新读取 live target slot，而不是复用进入 renderer
  时的 target snapshot；
- 恢复 TriangleBatch stencil setter 的源参数顺序 `(writeRef, maskRef)`；
- 把 stencil 第一次超过 255 的错误处理从非参考插件日志改回标题 `MMotionPlayer`、正文
  `StencilCount overflow(256)` 的一次性 message box；
- 删除 deep renderer 内参考实现不存在的 logo diagnostic trace，并恢复 D3DLayer route 的
  strict `Variant::AsObject()` dispatch 泄漏、无用途 default-software selector call，以及
  TriangleBatch 未初始化的 trivial clip key。

本 slice 闭合 deep renderer 外层、target/source callable、method selectors、TriangleBatch 和
stencil 状态机。mesh 的公共 `buildAndSubmitMeshTriangles` 深层 helper 后续已由
`MP-R14-D3D-MESH-SUBMIT-CELLS` 独立闭合。

## 2. 四端主函数与 caller

### 2.1 shared deep renderer

| 平台 | deep renderer | 完整指令数 | 显式 unwind cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6AB39C` | 606 | DWARF/内联 cleanup chunks |
| Android armv7 | `0x57D3DC` | 655 | DWARF owner cleanup disposition |
| iOS arm64 | `0x100104450` | 545 | libc++ owner cleanup paths内联 |
| iOS armv7 | `0x101850` | 888 | `0x102284`，176 条 SjLj cleanup |

四端完整读取了 2694 条主函数指令以及 armv7 的 176 条 cleanup。共同源级参数顺序为：

```text
renderPreparedItemsToD3DTexture(
    initialTarget,
    targetTexturePairGetter,
    targetRect,
    sourceTextureGetter,
    mainItems,
    player,
    xOffset,
    yOffset)
```

本地把 `player` 恢复为成员函数隐式 `this`，其余参数和 callable owner 顺序保持一致。

### 2.2 D3DLayer caller

| 平台 | `Player::drawToTexture` | 完整指令数 | armv7 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6D3048` | 211 | — |
| Android armv7 | `0x5976AC` | 129 | — |
| iOS arm64 | `0x100123970` | 138 | — |
| iOS armv7 | `0x122C10` | 213 | `0x122E3A`，98 条 |

共同调用顺序：strict ResourceManager extraction → motion-context map lookup → two-list prepare →
main-list projection → source callable construction → `TVPIsSoftwareRenderManager()`（结果丢弃）→
private OpenGL `SetRenderTarget` → target rect construction → target-pair callable construction → deep
renderer。armv7 cleanup 逆序销毁 motion-context 临时量、两个 function owner 和两个 pointer
vector；它不释放 strict `AsObject()` 新增的 dispatch 引用。

D3DAdaptor caller 的 capture gate、source callable、target bind 和 owner envelope 已由
`MP-R14-MOTION-PRIVATE-OPENGL-ENVELOPE` 闭合；本报告把它的 target-pair callable 与 deep body
接通。

## 3. type-erased source/target getter

### 3.1 source getter

D3DAdaptor source getter 使用前一 slice 闭合的 atlas/fallback/software-map 数据流。D3DLayer
source getter 则只有一个 raw borrow：

| 平台 | D3DLayer source invoke | 完整指令数 |
|---|---:|---:|
| Android arm64 | `0x6F3BAC` | 3 |
| Android armv7 | `0x5B057A` | 3 |
| iOS arm64 | `0x100146084` | 4 |
| iOS armv7 | `0x1465BE` | 4 |

callable 捕获 loaded-resource 子对象指针，但 invoke body 不读取捕获，只返回当前 item 的
`sourceState->texture`。没有 atlas retry、null gate 或 AddRef。source callback 返回后，deep
renderer 才重新读取同一 descriptor 的 texture rect，因此 callback 内的 descriptor 改写对
当前 item 立即可见。

### 3.2 target-pair getter

| 平台 | D3DAdaptor invoke | D3DLayer invoke |
|---|---:|---:|
| Android arm64 | `0x6EE8AC`（5） | `0x6F3C28`（4） |
| Android armv7 | `0x5AC830`（5） | `0x5B05B8`（4） |
| iOS arm64 | `0x1001405A0`（4） | `0x1001460FC`（3） |
| iOS armv7 | `0x141822`（5） | `0x146608`（4） |

返回值是两个 raw texture pointer 组成的小 aggregate；四端 invoke 都把同一个 pointer 写入
两个返回槽。deep renderer 在调用点把第一槽作为 `referenceTexture`、第二槽作为
`targetTexture` 交给 TriangleBatch。两个 callable 都忽略传入的 `IsBlendTarget()` bool 与
target rect：

- D3DLayer 捕获 initial target pointer，始终返回 `{target, target}`；
- D3DAdaptor 捕获 adaptor pointer，每次 invoke 都重新读取 adaptor target slot，再返回
  `{current, current}`。

这一差异有真实可观察性：source callback、method selector 或 mesh helper 可以重入并替换
adaptor target；render manager 早先绑定的 initial target、batch 的 target/reference 与退出时
adaptor slot 因而可能不再相同。原版不做一致性修复、AddRef 或 null 检查。

target getter 只在 method 非空后调用。affine 路径立即调用；mesh 路径要等公共 mesh helper
真正选出并提交 triangles，才在 submit callback 内调用。空 `std::function` 在 iOS 通过
`0x1000FAACC` / `0xF7BB2` 等 libc++ wrapper 抛 `std::bad_function_call`；Android 在 call-site
检查 manager/invoke slot 后走同一标准异常边界。

## 4. stencil 引用与 clip 预处理

`priorDraw=true` 完全跳过预处理：不清旧 stencil bytes、不重算 clip、不 Clear leaf Variant，
并返回 stencil count 0。render loop 仍会读取 item 上遗留的 mask/write bytes；因此 prior pass
可以在不调用 BeginStencil 的情况下重新触发共享 GL stencil state helper。这是原生状态泄漏
边界，不应“安全地”清零。

非 prior pass 的共同流程：

```text
for item: item.maskRef = item.writeRef = 0
count = 0
for item:
    require blendLowNibble != 6
    require drawFlag && !rawFlag16 && opacity != 0 && parentItem != null
    previous = count++
    if previous >= 255 && !overflowShown:
        overflowShown = true
        showMessageBox("StencilCount overflow(256)", "MMotionPlayer")
    ref = uint8(count)
    item.writeRef = ref
    propagate ref through parent chain and every ancestor child vector
    if chain has no drawable mask target: item.writeRef = 0
```

ancestor 自身只要求 `!rawFlag16 && !skipFlag0`；其 child 还要求 `opacity != 0`。borrowed
parent/child pointers 不增加 owner。count 保持 int，写入 byte 时按 256 wrap；第 256 个 ref 为
0，但 count 仍使 BeginStencil gate 为真。overflow global byte 在构造 message strings 前发布，
异常也不会让提示重试。

对每个 `drawFlag` item，clip 从 paint box 与 `[0,width]×[0,height]` 相交开始；只有 viewport
right>=left 且 bottom>=top 才再用 floor/ceil 后的 viewport 相交。right/bottom 的 compare-select
在 NaN 时选择 canvas bound；最终使用两个 ordered `>=` 排除空/反向 edge，unordered edge
会存活。有效且非 rawFlag16 时写 `rawFlag21=true`、四个 float clip 并 Clear leaf Variant；
否则只写 `rawFlag21=false`。

## 5. item admission 与几何分流

逐 item 的 gate 顺序严格为：

1. 排除 blend low nibble 6；
2. 排除 `skipFlag0`、`rawFlag16`，以及 `priorDraw && !skipFlag1`；
3. 排除 source descriptor blank byte；
4. prior pass 用 C++ signed division 把 opacity 除二；
5. `opacity <= 0` 时只有 nonzero stencil mask ref 可以继续；
6. 调 source getter；
7. callback 返回后读取 source rect，要求 `right > left && bottom > top`；
8. 组装 packed color、切换 batch stencil state、选择 method；method null 时跳过；
9. `meshType` 0/1/2 分别进入 affine、Bezier patch、composite mesh；其他值无提交。

packed color 取 `packedColors[0]`：特殊值 `0xFF808080` 映射为白色 `0x00FFFFFF`，否则
保留低 24 bits；opacity 先截断为 uint8 再写 alpha byte。method selector 的 alphaOpAdd 参数
在四端都是 literal true，D3DAdaptor 的同名 property 只保留脚本 echo。alpha-test key 只看
maskRef，不看 writeRef。

affine 构造六个 destination/source vertices；destination 的第四角是
`p1 - p0 + p2`。meshType 1 先按 source 宽高分配 patch divisions、offset 16 control points并
tessellate；meshType 2 offset composite points并复用同一 vector作为 bounds/mesh。公共 mesh
helper 的 submit callback 才执行 target getter与 batch append。

## 6. TriangleBatch 内部布局与 cache key

### 6.1 helper 等价类

| helper | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| setStencilState | `0x6D8C98`（104） | `0x59A948`（77） | `0x1001286A4`（70） | `0x127A1E`（71） |
| selectMethod | `0x6D8E3C`（82） | `0x59AA38`（38） | `0x1001287BC`（42） | `0x127AF6`（39） |
| appendTriangles | `0x6D9290`（120） | `0x59AD20`（69） | `0x100128AFC`（67） | `0x127DAA`（71） |
| flush | caller/helpers 内联 | `0x59ADD8`（50） | `0x100128C08`（61） | `0x127E6A`（70） |

构造后的共同字段是 null method/source/target/reference、两个空 point vectors、private manager、
mask/write 0、packedColor `0xFFFFFFFF`、blend `-1`、两个 bool false。trivial clip rect 未初始化；
第一次有效 append 一定先由其他 key component mismatch 触发 empty flush，再发布 clip，因此
正常路径不会读取未初始化 rect。

`setStencilState(writeRef, maskRef)` 只有 key 变化才 flush；flush 抛异常时不发布新 bytes、不改
GL。成功后先写 mask/write，再应用 GL state。

`selectMethod(blend, color, alphaOpAdd, alphaTest)` 的四字段 key 相同就返回 cached method；变化
时先 flush，再发布四字段，然后调用 selector，最后才写 method。selector 抛异常会留下新 key
与旧 method。

`appendTriangles` 的 key 是 method、source、target、clip 四个 int 和 packedColor；故意不含
referenceTexture。mismatch 时先 flush，再缓存 method/source/target/reference/clip；它比较但
不写 packedColor，因为该字段由 selectMethod 拥有。随后先扩展 source vector，再扩展
destination vector；第二次分配失败可以留下长度不对称的 batch。

`flush` 只看 destination vector 是否为空。它以 `destination.size()/3` 调一次
`OperateTriangles`，textures 数组恰好一个 `(source, sourcePoints)`，然后依次 rewind
destination/source end。OperateTriangles 抛异常时两个 end 都不回退，后续 flush 会重试
同一批。这也解释了 reference-only 变化为什么继续使用当前 batch 最先缓存的 reference。

## 7. method selectors

| selector | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| AlphaTest | `0x6D9470`（247） | `0x59AE64`（282） | `0x100128D00`（240） | `0x127F38`（261） |
| normal | `0x6D9898`（177） | `0x59B1FC`（218） | `0x100129134`（178） | `0x12827C`（214） |

两组 selector 各有六个独立 arm：1=Add，2/5=Sub，3=Mul，4=Screen，default 按 alphaOpAdd
选 AlphaBlend `_a` 或普通版本。normal arm 各自拥有 method pointer + color ID；AlphaTest arm
另有 threshold ID。所有 static 都是 BSS/constant-zero，无 `__cxa_guard`；method pointer 是
非同步初始化 sentinel。

初始化顺序是 private manager → GetRenderMethod → 立即发布 method pointer → Enum `color` →
AlphaTest 再 Enum `alpha_threshold`。method 或 Enum 抛异常不会清 pointer或重试已发布阶段。
每次调用都重新 SetParameterColor4B；AlphaTest 随后再 SetParameterOpa(threshold, 64)。六组
method 名和本地实现逐字符串一致。

## 8. GL stencil 生命周期与异常边界

count>0 时 BeginStencil 顺序固定为：private manager `BeginStencil(initialTarget)` → disable depth
test → stencil mask 255 → clear stencil 0 → clear depth|stencil `0x500` → op
REPLACE/KEEP/KEEP → depth mask false → disable stencil test → shared enabled-cache byte 0。

每次 batch stencil key 变化：

- writeRef!=0：保证 stencil test enabled，func LEQUAL(writeRef,255)；maskRef!=0 时 mask=maskRef、
  op KEEP/REPLACE/REPLACE，否则 op 全 KEEP；
- writeRef==0 && maskRef!=0：保证 enabled，mask=maskRef，func ALWAYS(maskRef,255)，op 全
  REPLACE；
- 两者为 0：只在 shared cache 表示 enabled 时 disable并清 cache。

正常尾部先 final flush，再在 count>0 时调用 private manager EndStencil，最后析构两个 batch
vectors。任何 prepass、message box、source/target callable、selector、mesh helper、flush 或
EndStencil 异常都不做 GL rollback；特别是 BeginStencil 后异常不会补 EndStencil。iOS armv7
176 条 cleanup 只销毁 active message strings、mesh callback、临时 point vectors和 batch
vectors，然后 resume。共享 stencil cache、item bytes、method statics和 renderer target均保留。

## 9. 本地改动

- 在 `Player.h` 恢复 `D3DTargetTexturePair_guess` / `D3DTargetTextureGetter_guess`，deep member
  参数顺序重新对应 native callable graph；
- D3DLayer callable 捕获 target；D3DAdaptor callable捕获 adaptor并在每次提交时读 live slot；
  affine 与 mesh submit callback 均把 pair 第二槽作为 target、第一槽作为 reference；
- D3DLayer route 改用 strict `AsObject()` + NCB native extraction，保留每次调用的 dispatch
  引用泄漏；补回结果丢弃的 default-software selector call；
- stencil setter 改为 `(writeRef, maskRef)`，overflow 改回有序构造 message/caption 的一次性
  message box；
- 删除非参考 logo trace include/body；TriangleBatch clip 去掉显式零初始化；
- method selectors、batch asymmetric key、source-first append、throw-preserving flush 与 GL
  stencil helper逐行对照后保持现有运行实现。

## 10. 验证与剩余边界

- 上述 deep、D3DLayer caller、batch helper、method selector、target/source invoke、libc++
  bad_function_call wrapper与两个 armv7 cleanup 均 fresh decompile；全部指令按分页完整读取；
- overflow message box 的四端 wrapper（`0x9130F0` / `0x6CDA4C` / `0x10020E2FC` /
  `0x20C194`）也完整读取 133/47/56/95 条指令，确认 `msgbox_ok` overload 与 message/caption
  顺序；
- 相关函数已在四个 IDB 命名、注释、书签并原位保存；
- coverage 12列、deterministic NCB ledger、Python helper compile 与 `git diff --check` 继续作为
  本 slice 的可用验收；
- 当前环境缺少 CMake、Ninja 和 Emscripten，且 standalone syntax check 被缺失的
  `boost/locale.hpp` 阻塞，因此不宣称正式 native/Web build。

公共 mesh helper `0x69AFE4` / `0x575800` / `0x1000F974C` / `0xF685C` 后续已由
`MP-R14-D3D-MESH-SUBMIT-CELLS` 闭合，包括 repeat-texture owner、source row/column vectors、
cell admission、AABB、selected-cell container、six-vertex expansion、submit callback 与异常
引用边。相邻 Bezier basis/tessellation cache helper 后续也已由
`MP-R14-BEZIER-BASIS-TESSELLATION` 闭合。
