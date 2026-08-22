# MotionPlayer ordinary PreparedRenderItem 持久覆盖与异常前缀四端复原（V234，2026-08-18）

## 1. 结论

本轮只闭合 `Player_appendPreparedRenderItems_guess` 的 ordinary source-item 路径。
Android arm64、Android armv7、iOS arm64、iOS armv7 四个当前参考二进制共同证明：
`PreparedRenderItem` 不是按帧重建的快照。node 通过 ordinary admission 后先发布
`drawnThisFrame=true`，再懒创建或复用 node 自己的持久 item，并按固定顺序覆盖字段。

最早的 owning-field 链是：

```text
ownerLabel
  -> skipFlag0 / rawFlag16 / skipFlag1
  -> commandKey
  -> numeric / geometry / source / parent / mesh suffix
  -> late draw-affine
  -> mainList.push_back(item)
```

因此任意异常都不是事务回滚。尤其是 persistent motion-context Variant 转 `ttstr` 时抛出：

- `drawnThisFrame` 已经是 true；
- reused item 的 `ownerLabel` 和三个 flag 已按本轮输入刷新；
- `commandKey` 自身仍是上一次成功值，因为临时 `ttstr` 尚未构造完成；
- layer/sort/coordinate、color/corners、`commandSrc`、blend/opacity/source、draw、parent、
  paint/viewport、mesh 和 draw-affine 等整个后缀仍是上一次成功值；
- 当前 item 尚未追加到 caller main vector。

这也纠正本地旧顺序：此前把 `sourceState`、`commandSrc`、draw/blend 和 viewport 提前写入，
会在 command-key conversion 或其他中途异常时暴露并不存在的“新后缀”。

## 2. 四目标映射

表中地址仅是 recovery IDB 的证据坐标；编译源码不包含这些绝对地址。

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| ordinary item ensure/reuse | `0x6C0724` | `0x58B3FE` | `0x100114BD0` | `0x1125A6` |
| `ownerLabel` | `0x6C0754` | `0x58B42E` | `0x100114C0C` | `0x1125D8` |
| three-flag prefix | `0x6C0780` | `0x58B454` | `0x100114C40` | `0x112602` |
| `commandKey` conversion/commit | `0x6C07DC` | `0x58B49C` | `0x100114CA4` | `0x112658` |
| numeric prefix | `0x6C07FC` | `0x58B4AC` | `0x100114CC0` | `0x112668` |
| source-clip color remap | `0x6C0974` | `0x58B554` | `0x100114D6C` | `0x11271E` |
| raw corners | `0x6C098C` | `0x58B56A` | `0x100114D8C` | `0x112736` |
| `commandSrc` | `0x6C09D8` | `0x58B5A0` | `0x100114DD8` | `0x11277A` |
| blend/opacity/source/stencil suffix | `0x6C09E4` | `0x58B5A8` | `0x100114DF0` | `0x112782` |
| draw flag | `0x6BF9B8` | `0x58B5D6` | `0x100114E30` | `0x11285C` |
| nullable ancestor / parent item | `0x6BF9BC` | `0x58B5D8` | `0x100114E38` | `0x11285E` |
| paint/viewport | `0x6BFA3C` | `0x58B5F8` | `0x100114E58` | `0x112888` |
| mesh suffix | `0x6BFA64` | `0x58B622` | `0x100114E7C` | `0x1128AC` |

Android arm64 的 ordinary ensure 仍是 inline block；其余三个目标调用 V233 已识别的
`ensureNodePreparedRenderItem_guess`。不同优化器会把相邻的 raw pointer、opacity 或整数 store
交错调度，但四端在这些 store 之间都没有可能抛出的源级操作，所以不会形成不同的 C++
异常可见 commit boundary。

## 3. 共同源级覆盖顺序

把寄存器调度折叠为可观察的源级顺序后，四端 ordinary path 为：

1. 通过 type/force 与 `source.valid` gates，写 `node.drawnThisFrame=true`；
2. ensure/reuse `node.preparedRenderItem`；
3. 赋值 `ownerLabel`；
4. 写 `skipFlag0`、`rawFlag16`、`skipFlag1`；
5. 将 persistent context Variant 转成临时 `ttstr`，再提交 `commandKey`；
6. 写两个 layer id、共享 sort/Z、coordinate mode、triangle priority；
7. 写 command X/Y、source origin X/Y 和四个 matrix double；
8. 复制并乘四角 packed colors，再用 persistent SourceState clip 做 color remap；
9. 复制 raw node corners；
10. 赋值 active-slot `commandSrc`；
11. 写 active-slot blend、accumulated opacity、borrowed `SourceState *`、stencil 与 draw flag；
12. 对 nullable visible-ancestor 执行 item ensure，并提交 borrowed `parentItem`；
13. 复制 raw paint AABB；
14. 从 nullable clip pointer 复制 viewport，或写 native invalid rectangle；
15. 写 mesh type/divisions，赋值 composite vector，并在 type-1 分支更新 Bezier/raw mesh vectors；
16. 对 corners、对应 mesh vectors、paint 与 viewport 执行 late draw-affine；
17. 把同一持久 item raw pointer 追加到 caller main vector。

`commandCompositeMeshPoints` 总是赋值，因此空输入也会清旧 vector。后两个 mesh vector 只在
type-1/有效控制点分支到达，保留其各自条件性 stale-state 边界。该 mesh 分支的更细异常矩阵不在
V234 扩张；本轮只固定它相对于普通前缀、parent/paint/viewport 和 final append 的位置。

## 4. commandKey 异常矩阵

`entry.commandKey = ttstr(_findMotionContextVariant)` 先构造临时字符串。若 Variant 是 Octet，
TJS conversion 在 destination string assignment 之前抛出。对 reused item，状态精确为：

| 状态 | 异常后值 |
|---|---|
| `node.drawnThisFrame` | 本轮 true |
| `entry.ownerLabel` | 本轮值 |
| `skipFlag0/rawFlag16/skipFlag1` | 本轮值 |
| `entry.commandKey` | 上次成功值 |
| layer ids / sort / coordinate / matrix / origins | 上次成功值 |
| colors / corners | 上次成功值 |
| `commandSrc` / blend / opacity / `sourceState` | 上次成功值 |
| stencil / draw / parent | 上次成功值 |
| paint / viewport / mesh / late affine | 上次成功值 |
| caller main append | 本轮未发生 |

若 item 原先为空，V233 的 lazy constructor 仍只给 selected owners/flags 建立有效状态；异常后的
numeric/pointer/geometry suffix 是 dormant allocator storage，不能解释成零值。V234 的确定性测试
刻意复用一次成功 item，以便在合法 C++ 读取范围内比较 old suffix，而不读取 fresh dormant POD。

## 5. 本地源码修正

`cpp/plugins/motionplayer/PlayerRenderItems.cpp` 的 ordinary block 已按共同顺序重排：

- owner 与 flag prefix 位于 `commandKey` 之前；
- layer/sort/coordinate/origin/matrix、color/remap、corners 位于 `commandSrc` 之前；
- blend/opacity/borrowed source、stencil/draw 位于 ancestor materialization 之前；
- paint 位于 viewport 和 mesh 之前；
- draw-affine 仍是 final main append 之前的最后 native mutation。

Web-only `nodeIndex`、`hasOwnSource` 和 portable ancestor index 跟随各自 native 阶段更新，但不被
误写成 native item 成员。可能分配的 Web diagnostic `sourceKey` narrow 已移到完整 native overwrite
和 draw-affine 之后；这样 port-only conversion failure 不会截断原生字段前缀。编译源码注释只描述
语义顺序，未引入四参考绝对地址。

## 6. 确定性回归边界

已有 type-3 child fixture 先成功填充 leaf item并建立 self-parent。V234 在同一对象上保存
`commandKey`、layer id、sort、`commandSrc`、blend、opacity、parent 和 paint 的旧值，然后把：

- owner label、blank/prior-draw flags 改成可区分的新前缀；
- layer id、Z、src、blend、opacity、ancestor 和 bounds 改成可区分的新后缀输入；
- child Player persistent context 改成一字节 Octet。

异常后断言 owner/raw/prior flag 已刷新，而 command key 与所有采样后缀仍等于旧值；同时继续断言
`drawnThisFrame=true`、caller main 为空、wrapper child vector 已清，且 outer aux 已保留 wrapper。
节点输入随后恢复，避免污染同一用例后面的 type-12 stencil topology 检查。

该完整 Catch2 翻译单元已在 ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 Emscripten 模式下
syntax-compile。当前 Web/Wasmtime CMake 配置不注册 tests，且 production targets 不编译这个 unit
TU，因此本轮没有把 syntax coverage 描述成已执行的 Catch2 runtime pass。

## 7. recovery IDB 写回

四个 canonical recovery IDB 均顺序打开、写回、保存、health probe、关闭。V234 共增加：

- 52 条 line/function comment，每端 13 条；
- 20 个 bookmark，每端 5 个；
- 0 个新 rename，0 个新 type（继续复用既有 `_guess` builder/ensure identity）。

注释固定 owner/flags/key、numeric、remap/corners、source/late flags、parent、paint/viewport、mesh 等
commit stage，并明确 command-key exception 后的 stale suffix。最终 IDA session audit 为 0。

## 8. 构建与产物验证

V234 的 production source 改动后，Web 3-step 与 Wasmtime 4-step incremental build 都成功；追加
test-only 断言后两棵 production tree 均为 `ninja: no work to do`，符合 unit TU 不属于这两个 target
的配置。两份 Wasm 均可由 Node 构造为 `WebAssembly.Module`：Web imports/exports 为 `539/69`，
Wasmtime 为 `538/69`。两棵 CTest tree 均明确报告 `No tests were found`。

相对 V233，FUNCTION、GLOBAL、DATA、`name` 和 imports/exports 不变；ordinary store 调度恢复令两端
CODE 和总 module 各减少 `0xF` / 15 bytes。

| product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,350 B | `C9D32EF6905F4324A43E7E779D1DC2BC8E7BD80A5DB69002F96FDBB42393D3A4` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,491 B | `A9779E4139CF9F499A3CF0397D2CED1EBB18C78EC359D9EDA7F1BF2982F6FF55` |

| section | Web | Wasmtime | delta from V233 |
|---|---:|---:|---:|
| FUNCTION | `0x1BD2D` | `0x1BA4C` | `0` |
| GLOBAL | `0xD5C2` | `0xD5EA` | `0` |
| CODE | `0x1A41993` | `0x19E9941` | `-0xF` each |
| DATA | `0x5A3EE0` | `0x5A1130` | `0` |
| `name` | `0x3185DD5` | `0x3141C6B` | `0` |

## 9. 未闭合边界

V234 没有把 ordinary-path 顺序泛化到其他 builder 分支。type-3 wrapper、type-12 stencil parent/mask、
priority duplicate/re-entry，以及 render-layer materialization 会覆盖不同子集，并有各自的 vector/string/
ancestor 异常窗口。下一纵切面应先闭合 wrapper 与 stencil 的持久覆盖/stale-suffix 矩阵，再判断是否需要
拆出 render-layer 独立恢复。
