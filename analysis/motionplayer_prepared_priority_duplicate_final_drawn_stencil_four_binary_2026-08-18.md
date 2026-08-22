# MotionPlayer priority duplicate/re-entry 与 final drawn stencil 状态机四端复原（V236，2026-08-18）

## 1. 结论

V236 把已有 priority receiver/index 证据与同一 recursive builder 尾部的 type-12 post-pass 连成
完整状态机。四个当前参考二进制共同证明：

- retained priority dispatch 是 receiver/lifetime snapshot，不是 node deque、node count 或 node state snapshot；
- 每一轮只把本轮选中的 node `drawnThisFrame` 清成 false；
- 相同 index 可重复选择同一 node，每轮都会重新 clear，随后由该轮实际 admission 决定是否再置 true；
- earlier successful visit 已追加到 main 的 persistent item pointer，不会被 later visit 的 clear/failure 移除；
- priority 中未出现的 node 完全不被 clear，其旧 drawn byte 保留；
- ordinary/recursive loop 全部结束后，type-12 post-pass 按自然 node 顺序读取这些**最终 live bytes**。

因此两类看似矛盾的结果都是 native：

```text
omitted type-12 with stale drawn=true
    -> 没有本轮 ordinary materialization
    -> 仍可进入 post-pass / aux / self child seed

duplicate-selected type-12
    -> earlier visits 可多次把同一 item pointer 追加到 main
    -> last visit 若 admission 失败，final drawn=false
    -> post-pass 跳过，main 中 earlier pointers 仍保留
```

这不是 main membership 与 drawn byte 的不一致 bug，而是 trusted priority sequence、persistent item
和 selected-only byte publication 组合出的原生边界。

## 2. 四目标映射

| stage | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| recursive builder | `0x6BF714` | `0x58B178` | `0x1001148F8` | `0x1123D8` |
| live size/header read | `0x6C051C..0x6C052C` | `0x58B240..0x58B26E` | loop head / `0x100115378` tail | loop head / `0x112D14` tail |
| numeric getter | `0x6C0570` | `0x58B284` | `0x1001149D0` | `0x1124A2` |
| live selected-node lookup | `0x6C0574..0x6C05A0` | `0x58B286..0x58B2A2` | `0x1001149E0..0x1001149EC` | `0x1124CA` |
| selected drawn clear | `0x6C05A4` | `0x58B2A8` | `0x1001149F4` | `0x1124D4` |
| ordinary main append | `0x6C04DC` | `0x58B8BE..0x58B8DE` | `0x100115348..0x100115370` | `0x112B2E..0x112B48` |
| natural-order type-12 final-byte gate | `0x6C0B38` | `0x58BB36` | `0x100115418` | `0x112D8E` |

精确地址只用于 recovery IDB 对应，不进入编译源码。Android libstdc++ deque 通过 iterator/map
header 与 reciprocal 算 size/address，iOS libc++ deque 通过 size/start/block map 寻址；这些是 ABI
差异，源级状态机相同。

## 3. receiver snapshot 与 live node state 的分层

builder 在 `nodes.size() >= 2` 时先：

```text
priorityCopy = CopyRef(player.priorityContentVariant)
priorityDispatch = priorityCopy.AsObject()  // independent retain
priorityCopy.Clear()
```

该 dispatch 一直保活到当前 Player 的 stencil post-pass 结束。但每轮算法是：

```text
N_before = player.nodes.size()
position = N_before - logicalIndex - 1
raw = priorityDispatch[position]             // may re-enter
selected = low32_wrap(raw + 1)
node = unchecked player.nodes_live[selected]
node.drawnThisFrame = false
process node against live fields/container
logicalIndex++
continue while logicalIndex < player.nodes_live.size()
```

getter 可以清空/替换 Player persistent priority Variant，而剩余读取仍使用 retained old dispatch；
它也可以修改 node source/active/type 或 deque。position 属于 getter 前 size，selected-node addressing、
admission 和 loop tail 属于 getter 后 live state。四端没有 node/container snapshot 或 iterator validity repair。

## 4. duplicate 的每轮 byte 代数

对某个重复被选中的 node，设每轮 admission 结果为 `A_i`：

```text
visit i entry: drawn = false
if ordinary/type-3-wrapper admission succeeds: drawn = true
otherwise: drawn remains false
```

所以 loop 后该 node 的 final byte 等于最后一次访问的成功结果，而不是所有访问的 OR：

```text
finalDrawn(selected node) = A_last
```

main/aux/child vectors则是逐操作 append/insert 的持久副作用，不按该公式回滚。ordinary successful
visits每次 append 同一个 node-owned item pointer；later failure 不扫描或删除 earlier duplicates。

对于从未选择的 node，loop 不触及 byte：

```text
finalDrawn(omitted node) = entry stale byte
```

这也解释了为何不能在 builder 入口加一个“全 nodes 清零”便利 pass；它会同时破坏 duplicate 和
omitted 的 post-pass 行为。

## 5. type-12 post-pass 的最终观察

priority traversal 结束后，builder 另按 `_nodes[1..end)` 自然顺序执行：

```text
if node.type == 12 && (node.stencilType & 4) && node.drawnThisFrame:
    item = ensure(node.preparedRenderItem)
    aux.push_back(item)
    item.childItems.clear()
    item.childItems.push_back(item)
    expand live mask pointer vector
```

它不检查 node 本轮是否出现在 priority，不检查 item 是否本轮进入 main，也不从 main 反推 drawn。
因而 omitted stale-true node 即使 fresh item 尚不存在，也会在这里 ensure item 并建立 self-only child
vector；其 ordinary fields仍是 dormant/stale。若调用者随后进入 command/render consumer，这属于
trusted priority/state 的更尖锐边界，不能用 synthetic zero defaults 修复。

反过来，duplicate node 前几轮成功、末轮失败时，main 可含多个同一 item pointer，而 post-pass 因
final false 完全跳过。stable sort 后这些重复仍是独立 vector elements，persistent owner仍只有 node
一个。

## 6. 确定性 test-only 状态机

`PreparedPriorityProbeState` 增加 test-only `onNumericRead(readCount)` hook，在 dispatch 已记录 getter
调用、写 result 之前执行；它不进入插件公开面或 production target。

### omitted stale-true composite

inactive synthetic skeleton 中所有非 root node 先设 drawn=true；priority 全部返回 zero-based index 0，
因此只重复选择 inactive node 1。node 2 被改为 type-12/stencil-bit-4且不出现在 priority：

- node 1 每轮 clear 后保持 false；
- node 2 byte 保持旧 true；
- direct prepared builder 的 main 为空；
- post-pass aux 恰含 node 2 的 newly ensured item；
- node 2 child vector 恰为 `{self}`。

使用 direct prepared test entry 是刻意的：fresh omitted composite 没有 ordinary field publication，继续
进入 command generation 会读取 dormant fields，不适合作为可移植 C++ test oracle。

### duplicate last-failure composite

node 1 被完整初始化为可 ordinary-render 的 type-12/stencil-bit-4，priority 每轮仍选择它。getter hook
在最后一次 read 把 `source.valid=false`：

- 前 `count-1` 次 visit 成功，main 得到 `count-1` 个完全相同的 persistent item pointer；
- 最后一次 visit 先 clear，再因 invalid source admission 失败；
- final `drawnThisFrame=false`；
- type-12 post-pass 跳过，所以 aux 为空；
- earlier main duplicates 全部保留。

所有 SourceState/POD fields 在被 ordinary builder 读取前显式发布；测试不依赖 V232/V233 已删除的
fresh-object全零假设。

## 7. 源码与 IDB 结果

production `PlayerRenderItems.cpp` 的 selected-only clear、live getter/deque read、ordinary append 和
natural-order post-pass 已与四端一致，V236 不需要 production 修正，只扩充 test oracle 与证据注释。

四个 canonical recovery IDB 顺序打开、写回、保存、health probe、关闭。每端增加 6 条 comment、
3 个 bookmark，总计 24 comment、12 bookmark；没有新 rename/type。注释明确连接：

- getter 前 position与 getter 后 live selection；
- duplicate selected clear与 per-visit admission；
- earlier main pointer retention；
- loop-tail live size；
- final-byte type-12 post-pass与 omitted stale byte。

最终 IDA session audit 为 0。

## 8. 验证与产物

- complete motionplayer Catch2 TU 的 ordinary/headless Emscripten syntax compilation通过；
- Web/Wasmtime production trees均为 `ninja: no work to do`，因为 V236 只改 test TU/analysis；
- 两份 Wasm 都能由 Node 构造，imports/exports保持 Web `539/69`、Wasmtime `538/69`；
- 两棵 CTest tree 按当前配置均报告 `No tests were found`；
- 产品与 V235 字节级一致。

| product | size | SHA-256 |
|---|---:|---|
| Web `out/web/debug/index.wasm` | 85,657,324 B | `ABF151F420BA5966A9DF12EBCB634D48572FE48852569F31402DC3F9BA349779` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,004,465 B | `CAD49F5B55252F9E416DB67B4B23BEB83D6A788B363B8EBD81596AE19FC51AFA` |

## 9. 下一边界

Prepared-item build topology至此已覆盖 constructor、ordinary、type-3 wrapper、type-12 post-pass 和
priority final-byte interaction。V237 follow-up 已先闭合 get-command/render-layer materialization 的
`rawFlag20/renderLayerId` persistent latch、clip/SLA 前缀与 reset release；详见
`analysis/motionplayer_render_layer_id_latch_persistence_release_four_binary_2026-08-18.md`。剩余高价值
边界是 latch 后的 local geometry、source resolver、leaf/composed Layer owner 与 partial commit。
