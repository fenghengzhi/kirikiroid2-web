# MotionPlayer prepared ordinary-item admission / drawn publication 四端复原（2026-08-14）

## 结论

`Player_appendPreparedRenderItems_guess` 的 ordinary source path 在 node 通过类型/force 和
`source.valid` gates 后，立即把 persistent `drawnThisFrame` byte 写成 1；这一步发生在
`PreparedRenderItem` 懒创建、所有字符串/Variant/vector 字段刷新、visible-ancestor item
ensure、draw-affine materialization，以及最终 main-vector append 之前。

因此 `drawnThisFrame` 不是“成功进入 mainList 后的结果标志”。它更准确地表示 node 已进入
本轮 ordinary materialization；后续任一步抛出都不回滚该 byte，即使 caller main 仍没有该
item。本地旧实现把写 1 拖到 `mainList.push_back` 紧前，掩盖了 item allocation、owner/string
conversion 和 vector assignment 失败后的原生部分状态。本轮已恢复四端 publication 时点。

## 四目标映射

| 目标 | builder | source-valid gate | early drawn store | item ensure/allocation | final main append |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x6BF714` | `0x6C06AC` | `0x6C06B8` | `0x6C06BC..0x6C0720` | `0x6C04DC` |
| Android armv7 | `0x58B178` | `0x58B3EE` | `0x58B3F4` | `0x58B3FE` | `0x58B8BE..0x58B8DE` |
| iOS arm64 | `0x1001148F8` | `0x100114BC0` | `0x100114BC8` | `0x100114BD0` | `0x100115348..0x100115370` |
| iOS armv7 | `0x1123D8` | `0x112592` | `0x112598` | `0x1125A6` | `0x112B2E..0x112B48` |

Android arm64 的 item ensure 被内联；其余三端调用已命名的
`ensureNodePreparedRenderItem_guess`。最终 append 在四种 STL/vector ABI 下分别内联或调用
growth helper，但共同源语义都是 `mainList.push_back(persistentItemPointer)`。

## admission gates 与 byte 状态机

每个 priority-selected node 一开始先执行：

```text
node.drawnThisFrame = false
```

随后 type-4、active 和 type-3 分支处理完毕后，ordinary admission 是：

```text
hasOwnSource = node.source.valid

if node.forceVisible == 0:
    mask = player.preview ? 5193 : 5185
    if (mask & (1 << node.type)) == 0:
        continue

if !hasOwnSource:
    continue

node.drawnThisFrame = true                 // publication point
item = ensure(node.preparedRenderItem)
... materialize item ...
mainList.push_back(item)
```

mask `5185` 选择 normal 模式 types 0/6/10/12，`5193` 另加 preview type 3；非零
`forceVisible` 只绕过 type mask，不能绕过 `source.valid`。所有未通过 gate 的节点保留本轮开头
清出的 false，不创建 item、不追加 main。

由于 priority selection 允许重复和重入 container/state mutation，byte 是 live persistent
状态而非 main-vector membership 的反向索引。一次早先 selection 可以把 item 加进 main，后续
重复 selection 又先清 byte；若 getter 重入让第二次处理不再通过 admission，函数正常返回时
main 仍含早先 borrowed pointer，而 node byte 为 false。stencil-composite post-pass 读取的是
最终 live byte，因此会跳过该 node。这是 priority duplicate 与 publication 时点组合出的
trusted-state边界。

## materialization 与 final append

early store 后的共同阶段包括：

1. ensure/reuse node-owned persistent item；
2. owner label、active-slot command source、context key 等 owning string 更新；
3. source descriptor/Variant Web 投影和普通平凡字段写入；
4. packed color 组合、source-clip remap、opacity/stencil/draw flags；
5. nullable visible-ancestor item ensure 和 `parentItem` 写入；
6. raw paint/clip geometry、mesh/composite/Bezier vectors materialization；
7. canonical-root draw-affine 对 corners/selected vectors/rects 的 late in-place pass；
8. 最终把同一 persistent item raw pointer append 到 caller main。

item 生命周期、布局和字段逆析构见
`motionplayer_prepared_render_item_lifecycle_four_binary_2026-08-13.md`；parent-link 的只判 null/
self/异常发布边界见
`motionplayer_prepared_type3_wrapper_parent_link_four_binary_2026-08-14.md`。本纵切面只固定
admission byte相对于整个 materialization 和 main append 的位置。

正常路径上没有第二次 drawn store。也就是说它不是由 vector append 的成功返回提交；main
append 抛异常时仍保持 true。

## 异常后的可观察部分状态

四端共同边界如下：

- item allocation/constructor 抛出：drawn 为 true，node item owner 槽仍为空；
- persistent item 已存在或新建后 owner/string/Variant conversion 抛出：drawn 为 true，item
  保留已更新前缀，main 未追加本轮 pointer；
- ancestor item ensure 抛出：drawn 为 true，ordinary item 的此前字段已刷新，parent store
  尚未完成，main 未追加；
- mesh/vector assignment 或 draw-affine 后段抛出：drawn 为 true，item 保留对应前缀，main
  未追加；
- final main vector growth 抛出：drawn 为 true，item 已完整 materialized；vector 自身异常
  保证依四端各自 STL implementation，但 node/item 不回滚；
- recursive builder 因异常离开时不会进入当前 Player 的 stencil-composite post-pass；外层
  child/wrapper/particle caller 的已完成 aux、childItems 和 item 修改也不回滚。

## 本地修正与确定性测试

`PlayerRenderItems.cpp` 已把 `node.drawnThisFrame = true` 移到 `source.valid` admission 后、item
ensure 前，并删除 final `mainList.push_back` 前的旧 store。compiled source 注释现在只描述
源级顺序，不保存参考绝对地址。

测试利用一个无需 OOM 的确定性异常：把 child Player 的 persistent motion-context Variant
设置为一字节 Octet。ordinary item 在 materialization 中将 context 转 `ttstr`，Octet→String
按 TJS Variant 规则抛 conversion error。外层 type-3 wrapper 先清 `childItems` 并进入 child
builder，child leaf 通过 source admission 后抛出。测试确认：

```text
leaf.drawnThisFrame == true
outer caller main is empty
wrapper.childItems is empty
outer caller aux contains the already-appended wrapper exactly once
```

这同时证明 drawn store 早于 context conversion/final main append，以及 type-3 wrapper 的
aux-before-child-recursion 部分状态。测试随后恢复 context，不污染后续 stencil topology 检查。

### V234 后续纠正（2026-08-18）

本报告当时只固定 `drawnThisFrame` 相对于整个 materialization/final append 的时点；上面的
materialization 列表是阶段概览，不是字段级 overwrite 顺序。V234 对四个当前参考的完整 ordinary
窗口重新取证后确认，精确前缀为 `ownerLabel -> three flags -> commandKey`，随后才是 numeric、
color/corners、`commandSrc`、blend/opacity/source、draw、parent、paint/viewport、mesh 与 draw-affine。
因此 command-key conversion 抛出时，只有 owner/flag 前缀属于本轮，command key 与整个后缀保留
上次成功值；旧“active-slot command source、context key 等 owning string 更新”的并列写法不能用于
推断两者先后。源码顺序与确定性 stale-suffix test 已同步修正。完整四端映射见
`motionplayer_prepared_ordinary_overwrite_exception_prefix_four_binary_2026-08-18.md`。

## recovery IDB 改善

四个 recovery IDB 均在 early drawn store 和 final main append 处写入注释，明确二者之间没有
commit/rollback 关系；后续异常保留 byte 和 persistent item 前缀。

## 验证

- 修改后的完整 motionplayer Catch2 翻译单元 Emscripten syntax-only 检查通过，仅有既有
  `_tss` literal warning；
- `cmake --build --preset "Web Debug Build"` 完成 motionplayer 重编译、静态库链接和最终
  Web 输出链接；
- scoped `git diff --check` 通过，仅报告仓库既有的 LF/CRLF 转换提示；
- 四个 recovery IDB 在 early drawn publication 与 final main append 注释写回后均保存成功。
