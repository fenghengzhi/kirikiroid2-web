# MotionPlayer type-3 wrapper / type-12 stencil 持久覆盖与 stale source 四端复原（V235，2026-08-18）

## 1. 结论

V235 闭合 `Player_appendPreparedRenderItems_guess` 中两个不应与 ordinary item 合并的路径：

1. non-preview type-3 node 的 persistent wrapper population；
2. ordinary traversal 完成后的 type-12 stencil-composite post-pass。

四个当前参考二进制共同证明，type-3 wrapper 只覆盖一个很小的字段子集：

```text
drawnThisFrame = true
ensure persistent item
ownerLabel
paintBox
viewport
optional root draw-affine(viewport, paintBox)
drawFlag = false
stencilComposite
optional aux append -> optional ancestor ensure
parentItem
childItems clear -> child recursion
caller-main range insert
```

最重要的新纠正是：**wrapper 路径从不写 `PreparedRenderItem::sourceState`**。它也不写
`commandSrc`、ordinary admission flags、blend/layer/sort/matrix/coordinate/origin、corners/colors、
opacity 或 mesh/Variant tail。fresh wrapper 的这些 POD/pointer 字段继续保持 V233 证明的 dormant
storage；若同一 persistent item 曾由其他路径写过，它们保持 stale value。

因此旧 iOS armv7 IDB 函数注释“every published prepared item stores exactly one source descriptor”
过度概括。正确限定是：ordinary item 发布一个 borrowed `sourceState`；type-3 wrapper 发布零个。
V235 已在该 IDB 追加明确 correction，并删除本地 wrapper 的错误 pointer store。

type-12 post-pass 则完全不刷新 parent item 的 ordinary 字段。它只把已经 ordinary-materialized 的
同一 item 追加到 aux，并按 `self -> mask expansions` 重建 `childItems`。

## 2. type-3 wrapper 四端映射

地址仅是 recovery IDB 证据坐标，不进入编译源码注释。

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| drawn publication | `0x6BFB24` | `0x58B906` | `0x100114F94` | `0x112B7A` |
| lazy ensure/reuse | `0x6BFB1C..0x6BFB8C` | `0x58B910` | `0x100114F9C` | `0x112B88` |
| owner-label commit | `0x6BFB94..0x6BFBBC` | `0x58B914..0x58B940` | `0x100114FAC..0x100114FE0` | `0x112B8C..0x112BBA` |
| paintBox copy | `0x6BFBC8` | `0x58B948..0x58B950` | `0x100114FF0` | `0x112BC2..0x112BCA` |
| viewport/null sentinel | `0x6BFBCC..0x6BFBE0` | `0x58B952..0x58B972` | `0x100114FF4..0x100115014` | `0x112BCE..0x112BE6` |
| root-affine stage | `0x6BFBEC..0x6BFE64` | `0x58B976..0x58BA46` | `0x100115024..0x100115088` | `0x112BEE..0x112C64` |
| draw=false / stencil | `0x6BFE68..0x6BFE70` | `0x58BA48..0x58BA50` | `0x1001150A0..0x1001150A4` | `0x112C70..0x112C76` |
| aux append / ancestor ensure | `0x6BFE8C..0x6BFEF4` | `0x58BA5C..0x58BA90` | `0x1001150B0..0x1001150F0` | `0x112C7C..0x112CB8` |
| parent commit | `0x6BFF08` | `0x58BA98` | `0x1001150F4/0x100115104` | `0x112CBA/0x112C8E` |
| child logical clear | `0x6BFF0C` | `0x58BA9E` | `0x100115108..0x100115128` | `0x112CC0..0x112CD8` |
| child recursion | `0x6BFF3C` | `0x58BABC` | `0x100115160` | `0x112CFA` |
| caller-main range insert | `0x6BFF50` | `0x58BACA` | `0x100115174` | `0x112D0C` |

对 item `sourceState` 的 ABI offset，LP64 是 `+0x100`、ILP32 是 `+0xE4`。四个 wrapper 窗口
从 owner-label 到 child range insert 均没有对应 store。Android arm64 将 ensure inline；另外三端
复用 `ensureNodePreparedRenderItem_guess`，这不改变字段子集或异常边界。

## 3. wrapper 数据流与生命周期

type-3 child resolver 仍先于 wrapper/plain 分流，借用 persistent child Variant 的 dispatch/native
Player。resolver 成功且 `drawFlag || stencilCompositeMaskReferenced` 后：

```text
node.drawnThisFrame = true
wrapper = ensure(node.preparedRenderItem)

wrapper.ownerLabel = node.layerName
wrapper.paintBox = node.bounds
wrapper.viewport = node.clipAABB ? *node.clipAABB : {1,1,-1,-1}
if root.drawAffineNonIdentity:
    if viewport ordered-valid:
        wrapper.viewport = transformAndRound(viewport)
    wrapper.paintBox = transformAndRound(paintBox)

wrapper.drawFlag = false
wrapper.stencilComposite = node.stencilType
// no wrapper.sourceState write

newParent = null
if node.drawFlag:
    callerAux.push_back(wrapper)
    if node.visibleAncestor != null:
        newParent = ensure(node.visibleAncestor.preparedRenderItem)
wrapper.parentItem = newParent

wrapper.childItems.clear()
child.appendPreparedRenderItems(
    wrapper.childItems, callerAux, inheritedColor,
    forcedChildDraw=true, inheritedPriorDraw)
callerMain.insert(end, wrapper.childItems.begin, wrapper.childItems.end)
```

mask-reference-only wrapper 不进 aux，并在正常路径提交 null parent。drawFlag wrapper 先进入 aux，
才读取/ensure ancestor。wrapper 本身从不通过最后一步进入 caller main；caller main 只接收其 child
range。`childItems.clear()` 对 raw-pointer vector 只把 end 退回 begin，保留 backing/capacity，且不
AddRef/Release/delete 元素。

## 4. wrapper 异常状态矩阵

wrapper 复用持久对象且没有 rollback，故阶段边界为：

| failure point | persistent state after failure |
|---|---|
| child resolver conversion/query call | wrapper path 尚未发布 drawn 或 item prefix |
| item allocation | `drawnThisFrame=true`，node item slot 仍为空 |
| ownerLabel assignment | drawn/已发布 item 保留 assignment 自身到达的 owner 状态；其余 wrapper 子集仍旧 |
| aux vector growth | owner、paint/viewport/affine、draw=false、stencil 已刷新；old parent 和 old childItems 保留 |
| ancestor item allocation | aux 已含 wrapper；old parent 和 old childItems 保留 |
| child recursion | parent 已提交；childItems 已清并保留递归完成的 pointer prefix；caller main 尚无 wrapper range |
| caller-main range allocation | wrapper child range 已完整，aux/parent/item 字段不回滚 |

在所有这些阶段，`sourceState` 都不是“尚未到达的 late write”；wrapper 路径根本不存在该 write。
fresh item 不能读取其 dormant pointer，正常 wrapper consumer 也不依赖它。测试使用显式 sentinel 才
观察 reused stale behavior，避免把未初始化存储当成零或合法指针。

## 5. type-12 stencil post-pass 四端映射

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| type/stencil/drawn gate | `0x6C0B38` | `0x58BB36` | `0x100115418` | `0x112D8E` |
| parent item ensure | `0x6C0B3C..0x6C0BA0` | `0x58BB3A` | `0x10011541C` | `0x112D9C` |
| aux append | `0x6C0BB0` | `0x58BB42..0x58BB58` | `0x100115424..0x10011544C` | `0x112DA4..0x112DBE` |
| child clear | `0x6C0BBC` | `0x58BB66` | `0x100115454..0x100115474` | `0x112DC4..0x112DDC` |
| self seed | `0x6C0BC4` | `0x58BB6A..0x58BB78` | `0x100115480..0x1001154A0` | `0x112DE2..0x112DF6` |
| mask loop | `0x6C0BE0` | `0x58BB88` | `0x1001154C8` | `0x112E16` |
| direct pointer append | `0x6C0C78` | `0x58BB9E..0x58BBEE` | `0x1001154E8..0x100115560` | `0x112E36..0x112EB2` |
| non-preview type-3 range insert | `0x6C0D84` | `0x58BBD8..0x58BBE8` | `0x100115540..0x100115558` | `0x112E8C..0x112EA0` |

这里的 qualifying parent 已在 ordinary pass 写 `drawnThisFrame=true` 并进入 main。post-pass 再次
ensure 只取得同一 persistent item，不重新覆盖 ordinary fields。完整顺序为：

```text
aux.push_back(parent)
parent.childItems.clear()
parent.childItems.push_back(parent)
for raw mask pointer in stored order:
    dereference immediately                  // no null guard
    if !drawn: continue
    if type == 0: append ensured item
    if type == 3 and currentPlayer.preview: append ensured wrapper
    if type == 3 and !preview: insert ensured wrapper.childItems range
```

顺序与重复 mask 均保留。preview byte 在每个 type-3 mask 分支 live 读取；没有进入 post-pass 时的
snapshot。child recursion 先完成子 Player 自己的 post-pass，因此共享 aux 的顺序仍是深度优先的
既有 entries，再到当前 Player 的 node-order type-12 entries。

## 6. stencil 异常边界

- parent ensure failure：本次 aux 和 parent child vector 都未修改；
- aux growth failure：旧 child range 完整保留；
- clear 后 self push allocation failure：aux 已含 parent，child logical size 为零；
- later direct push/range-insert allocation failure：self 与此前 mask expansions 作为完整 prefix 保留；
- mask item ensure failure：此前 prefix 保留，当前 mask 未提交；
- null/raw-invalid mask pointer：在 `drawnThisFrame` load 处进入 native invalid dereference，而不是异常恢复；
- skipped undrawn/other-type target：ensure 不执行，不会仅因 mask reference 分配 item。

四端 raw-pointer vector 元素的 copy/move/destruction均不抛且无所有权动作；有容量时 append/insert
只做 pointer movement，无容量时先分配新 backing。allocation 失败不回滚已在 earlier operations
提交的 parent/aux/prefix，但当前单次 growth 不产生半个 pointer element。

## 7. 本地修正与回归边界

`PlayerRenderItems.cpp` 已删除 type-3 wrapper 的：

```cpp
wrapper.sourceState = &node.source;
```

相邻注释明确 fresh/stale pointer 语义；Web-only `hasOwnSource=false` 仍作为派生 sidecar 更新，不能
解释成 native source publication。ordinary item 的 late `sourceState=&node.source` 保持不变。

现有 child-Octet 确定性异常 fixture 在一次成功 wrapper 后显式把 wrapper source pointer 设为
`&leaf.source` sentinel，再改变 wrapper owner/bounds/stencil/ancestor 并触发 child ordinary
`commandKey` conversion。异常后测试确认：

- wrapper owner、paintBox、draw=false、stencil 和 parent 已按本轮输入提交；
- aux 已含 wrapper，wrapper childItems 已 clear；
- wrapper `sourceState` 仍是 sentinel，没有被 wrapper rebuild 覆盖；
- child leaf 保持 V234 的 owner/flag 新前缀与 commandKey/numeric/source/parent/paint 旧后缀；
- caller main 仍为空。

测试只读取显式初始化过的 sentinel/fields，不读取 fresh constructor 的 dormant pointer。完整 test TU
在 ordinary 与 headless Emscripten 两种模式 syntax-compile；当前 production CMake trees 不注册/运行
Catch2 test target，因此不把该检查描述成 runtime pass。

## 8. recovery IDB 写回

四个 canonical recovery IDB 均顺序打开、写回、保存、health probe、关闭。V235 共增加：

- 72 条 function/line comment，每端 18 条；
- 20 个 bookmark，每端 5 个；
- 0 rename，0 type（复用既有 builder/ensure/vector helper `_guess` identity）。

iOS armv7 函数入口 comment 特别追加 `V235 CORRECTION`，否定旧“所有 published item 都写一个
source descriptor”的泛化。最终 IDA session audit 为 0。

## 9. 构建与产物

V234 baseline 在构建前复制到 `out/validation/v235-baseline-*.v234.wasm`，SHA-256 与 V234 报告
一致。第一次无 emsdk shell 的 Web 自动重配置把 cache toolchain 临时解析为 `/upstream/...` 并在
编译前失败；产物未被替换。随后在加载 emsdk 环境的同一 shell 中用 `Web Debug Config` preset
就地恢复 cache，并完成 Web 24-step build；Wasmtime 对应 preset 完成 25-step build。

两份 Wasm 都能由 Node 构造为 `WebAssembly.Module`，imports/exports 仍为 Web `539/69`、
Wasmtime `538/69`。最终两棵 build tree 均为 `ninja: no work to do`；两棵 CTest tree 按当前配置
均报告 `No tests were found`。相对 V234，唯一 section/size 差异是 CODE 和总 module 各减少
`0x1A` / 26 bytes。

| product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,324 B | `ABF151F420BA5966A9DF12EBCB634D48572FE48852569F31402DC3F9BA349779` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,465 B | `CAD49F5B55252F9E416DB67B4B23BEB83D6A788B363B8EBD81596AE19FC51AFA` |

| section | Web | Wasmtime | delta from V234 |
|---|---:|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` | `0` |
| GLOBAL | `0xD5C2` | `0xD5EA` | `0` |
| CODE | `0x1A41979` | `0x19E9927` | `-0x1A` each |
| DATA | `0x5A3EE0` | `0x5A1130` | `0` |
| `name` | `0x3185DD5` | `0x3141C6B` | `0` |

## 10. 未闭合边界

V234/V235 已闭合 PreparedRenderItem constructor、ordinary overwrite、type-3 wrapper 和 type-12
stencil topology。V236 随后闭合 priority duplicate/re-entry 对 final `drawnThisFrame` 与 post-pass
的影响，见
`motionplayer_prepared_priority_duplicate_final_drawn_stencil_four_binary_2026-08-18.md`。尚未单独
闭合的 prepared-item 高价值边界是 get-command/render-layer materialization 对 Variant、Layer 与
renderLayerId 的后续覆盖/异常窗口。
