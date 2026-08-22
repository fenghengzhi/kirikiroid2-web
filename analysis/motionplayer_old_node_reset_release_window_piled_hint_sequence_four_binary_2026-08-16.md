# motionplayer 旧树 reset 与 `releaseLayerId/window/piledCopy` hint 序列（四参考二进制）

日期：2026-08-16

## 1. 结论

四个 `reference/binaries/` 共同闭合了 V162 `parameter` 右侧的三个 32 位 mutable
member-hint 槽：

```text
parameter -> releaseLayerId -> window -> piledCopy
```

四端每一跳都严格为 4 bytes。三个新增闭合的槽分别只有一个语义 consumer：

- `releaseLayerId`：`Player::resetAndReleaseOldNodeTree`；同一 reset 内的两次必调 ID 与一次
  条件 render ID 全部复用它；
- `window`：`Player::materializeInternalRenderLayers` 的 retained target accessor getter；
- `piledCopy`：accurate-SLA post-draw 的七参数内部 Layer 调用。

consumer 单一不等于 storage 私有。旧移植把 reset 的槽放在 `PlayerMotionLoad.cpp` 匿名 namespace，
又把已有 `window/piledCopy` 声明排在较后的 SourceCache/descriptor 分组；这三者都没有表达四份
机器码中的连续全局身份。本轮把三项接回 `MotionDispatch.h` / `RuntimeSupport.cpp` 的精确序列，
删除 reset-local 重复槽。

同时，fresh reset decompile 再次确认旧树清理的完整所有权与边界：先保留一个固定
ResourceManager dispatch，再 invalidation、重置 HM1、按 live deque 次序释放非 root IDs，之后
才 erase suffix、清 label map 并释放 manager。普通 `FuncCall` failure 不分支；异常则在尚未到达
的 suffix/map commit 之前展开。

本文绝对地址只用于四份参考二进制坐标；编译源码只保留语义名，stripped 原名未知者继续带
`_guess`。

## 2. 连续全局地址

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `parameter` | `0x1AB5498` | `0x1111934` | `0x101B69960` | `0x187D604` |
| `releaseLayerId` | `0x1AB549C` | `0x1111938` | `0x101B69964` | `0x187D608` |
| `window` | `0x1AB54A0` | `0x111193C` | `0x101B69968` | `0x187D60C` |
| `piledCopy` | `0x1AB54A4` | `0x1111940` | `0x101B6996C` | `0x187D610` |

`releaseLayerId` global 的原始 xref 数为 2/9/1/8，但 force-recompile 后四端 pseudocode 都恰有
三次语义使用。差异来自 AArch64 page-address materialization、Thumb MOV/ADD PC 序列以及位于
函数尾外的 literal/异常相关引用，不代表额外 consumer。`window` 与 `piledCopy` 的原始 xref
数均为 2/3/1/2，去重后各只有一个函数。

`piledCopy` 后的下一机器地址在四个平台没有共同 consumer：Android arm64、Android armv7、
iOS arm64 与 iOS armv7 的优化/条件编译结果已经分叉。因此本轮只把四端共同、可证明的连续
前缀闭合到 `piledCopy`，不臆造一个跨平台第五成员。

后续 V164 已单独闭合 `Player::isExistMotion`：其 private-static hint 只有 iOS arm64 恰好紧邻
`piledCopy`，另外三端均被目标特有实体/空洞隔开，进一步确认这里不是共同第五槽。详见
`analysis/motionplayer_is_exist_motion_private_hint_borrowed_receiver_four_binary_2026-08-16.md`。

## 3. UTF-16LE literal 过滤

精确 patterns：

```text
releaseLayerId:
72 00 65 00 6C 00 65 00 61 00 73 00 65 00 4C 00
61 00 79 00 65 00 72 00 49 00 64 00 00 00

window:
77 00 69 00 6E 00 64 00 6F 00 77 00 00 00

piledCopy:
70 00 69 00 6C 00 65 00 64 00 43 00 6F 00 70 00
79 00 00 00
```

按真实 consumer xref 过滤后的 literal：

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `releaseLayerId` | `0x14D5A72` | `0xD85592` | `0x10195BF20` | `0x174E284` |
| `window` | `0x14CFC04` | `0xD8180A` | `0x10195C862` | `0x174EBC6` |
| `piledCopy` | `0x14D620A` | `0x5935A4` | `0x10195C870` | `0x174EBD4` |

`releaseLayerId` 每端都只有一处 raw-byte 命中，并同时被 ResourceManager NCB registration 与
reset 使用。iOS 的 `window` 各有五处、`piledCopy` 各有两处 raw-byte 命中；Android armv7
也有第二处 `piledCopy`。表中地址由相应 production getter/call 的 xref 选出，不能用第一处
字节命中代替。IDA 对若干 UTF-16 字符串显示成截断的 `"r"`、`"w"` 或 `"p"`，不改变原始
字节和参数地址。

## 4. 函数与调用点映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| reset 入口 | `0x6B2AD8` (`0x3E0`) | `0x581F3C` (`0x240`) | `0x100109ACC` (`0x38C`) | `0x107358` (`0x382`) |
| `layerId1` call | `0x6B2C98` | `0x58202A` | `0x100109C3C` | `0x1074CA` |
| `layerId2` call | `0x6B2CDC` | `0x582064` | `0x100109C84` | `0x107512` |
| prepared render-ID call | `0x6B2D34` | `0x5820B8` | `0x100109CE0` | `0x107568` |
| materializer 入口 | `0x6CB57C` | `0x592F7C` | `0x10011E2BC` | `0x11CAC8` |
| `window` getter | `0x6CB644` | `0x592FCE` | `0x10011E348` | `0x11CB86` |
| accurate post-draw 入口 | `0x6CBD18` | `0x593344` | `0x10011E808` | `0x11D078` |
| `piledCopy` call | `0x6CBF8C` | `0x59348A` | `0x10011E9E0` | `0x11D27A` |

reset 的 caller 集合四端完全相同：

| caller | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player::buildNodeTree` | `0x6B2650` | `0x581CFE` | `0x100109820` | `0x1070DC` |
| `Player::updateMotionSubNodes` | `0x6BB714` | `0x587F2E` | `0x10011142C` | `0x10F206` |
| `Player` destructor | `0x6CCF24` | `0x593C5C` | `0x10011F2E8` | `0x11DD4E` |

因此 reset 同时服务于普通 rebuild、type-3 child 停止/替换路径和最终析构；不能只按 build
helper 的一次性清理理解其异常与重入边界。

## 5. reset 的共同数据流

四端统一伪代码：

```text
resourceManagerCopy = copy(player.resourceManager)
resourceManager = resourceManagerCopy.AsObject()  // independent AddRef
destroy resourceManagerCopy                       // before callbacks

visit owned child Variants for every node, including root:
    object = variant.AsObjectNoAddRef()
    ignore object.Invalidate(0, null, null, object)
    return true

for each live HM1 entry:
    entry.writeVal = 1.0
    entry.heapResult.clear()       // logical size zero, capacity retained
    // entry.weight is untouched

for index = 1; index < live nodes.size(); ++index:
    node = nodes[index]
    call releaseLayerId(Integer(node.layerId1), shared hint)
    call releaseLayerId(Integer(node.layerId2), shared hint)
    if node.preparedRenderItem != null and item.rawFlag20 != 0:
        call releaseLayerId(Integer(item.renderLayerId), same hint)

erase nodes[1..live end)
clear nodeLabelMap
release resourceManager
```

每个 release call 的 ABI 形状一致：

- flags `0`；
- member 为 UTF-16 `releaseLayerId`；
- hint 是同一个进程级 slot；
- result 为 null；
- `numparams=1`，参数是独立 signed Integer Variant；
- retained manager 同时作为 receiver 与 objthis；
- call 后立即析构本次 Integer Variant；
- ordinary HRESULT 完全不参与分支。

`layerId1/layerId2` 无数值门：0、负数、重复 ID 都照常 dispatch。第三个 ID 的唯一门是 prepared
pointer 非 null 且 `rawFlag20` 非零；`renderLayerId` 本身即使为 0/负数也不增加检查。root 节点
参与前面的 child-Variant visitor，却从 ID release 与 suffix erase 中排除。

## 6. 容器和重入边界

四端 deque ABI 仍保持既有差异：

| ABI | `MotionNode` stride | deque block 元素数 |
|---|---:|---:|
| Android arm64 / libstdc++ | 2632 | 1 |
| Android armv7 / libstdc++ | 2272 | 1 |
| iOS arm64 / libc++ | 2648 | 16 |
| iOS armv7 / libc++ | 2228 | 16 |

尽管索引计算差异很大，四端循环都从逻辑 index 1 开始，并在每轮重新观察 live end/size。脚本
回调若重入修改节点容器，后续循环条件看到的是新状态；当前 node 引用在同一节点的多次回调之间
没有额外稳定性保护。portable 的 index-based loop 保留了这个源级形状。

ResourceManager 在任何 child Invalidate 前就固定并独立 AddRef。回调替换/清空 Player 的 canonical
Variant 不会把后续 release 调用重定向到新对象；正常返回与异常展开都会释放该固定引用。

commit 边界如下：

- Invalidate 普通失败状态被 callback 忽略并继续；直接抛异常会阻止 HM1 reset、ID release、suffix
  erase 与 label-map clear 中尚未发生的部分；
- `releaseLayerId` 普通失败状态被忽略，后续 ID、suffix erase 与 map clear 仍执行；
- `releaseLayerId` 直接抛异常时，本次 Integer Variant与 retained ResourceManager 都在 unwind 中
  析构，但尚未完成的 ID、suffix erase 与 map clear 不执行；
- suffix erase 先析构所有非 root `MotionNode`，包括其 raw-owned prepared item；label map 随后
  整体 clear；root 留到 Player deque 自身析构或下次 reset。

## 7. `window` 与 `piledCopy` 右邻 consumer

`window` 是 materializer 的 typed Variant getter，flags=0，使用 retained target
`ncbPropAccessor` 的 dispatch 同时作为 receiver/objthis。它在创建并发布 primary internal Layer
之前执行。这个槽不同于 SeparateLayerAdaptor constructor 中另一个 null-hint `window` 读取。

`piledCopy` 位于 accurate-SLA post-draw：primary internal Layer dispatch 接收固定七参数
`(0,0,target,0,0,width,height)`，result=null，ordinary status 忽略。它不是 SourceCache 的
泛化局部槽；地址身份直接紧跟 materializer 的 `window`。

本轮不重写这两个 consumer 的既有数据流，它们的 source identity、partial-publication 和
void-return ABI 已由 V127 及后续纵切面闭合；这里只修正 process-global 声明/定义顺序。

## 8. 源码与回归落地

- `MotionDispatch.h` / `RuntimeSupport.cpp`
  - 把 renderer primitive 后继家族从五项扩为八项；
  - 在 `parameter` 后依次声明/定义 `releaseLayerId/window/piledCopy`；
  - 从错误的后续 descriptor 分组移走已有 `window/piledCopy` 定义，不产生第二份 storage；
- `PlayerMotionLoad.cpp`
  - 删除匿名 namespace 的 `nodeReleaseLayerIdMemberHint_guess`；
  - reset 三类调用统一使用 `detail::releaseLayerIdMemberHint_guess`；
- `Player.h`
  - 增加仅供 differential/unit probe 的 reset 薄入口与 canonical owner clear 薄入口，均不注册到
    Motion.Player NCB surface；
- `motionplayer-dll.cpp`
  - 八槽地址互异回归覆盖完整连续家族；
  - 扩展 layer-id recorder，记录 flags/hint/result；
  - 新增 `old node reset shares one recovered releaseLayerId hint`，覆盖 root 排除、0/负 ID、
    active/inactive prepared item、固定调用次序、同一 hint、null result、普通 failure 继续、首个
    callback 清 canonical/external owner 后的 retained receiver，以及 suffix/map commit。

当前 preset 没有注册 Catch2 runtime target，因此这些 assertion 只完成了普通/Headless 两种完整
test TU 语法/类型编译；本文不把它冒充 runtime pass。

## 9. IDB 回写

四份 recovery IDB 均完成并原位保存：

- 三个地址分别重建为独立 size-4 `unsigned int`：
  - `g_motion_releaseLayerIdMemberHint_guess`；
  - `g_motion_windowMemberHint_guess`；
  - `g_motion_piledCopyMemberHint_guess`；
- 每库在 reset 函数、左边界、三槽、三 literal、三 release call、window getter 与 piledCopy call
  共追加 13 处 V163 注释；
- 添加 `V163 complete reset releaseLayerId/window/piledCopy hint sequence` bookmark；
- reset、materializer 与 accurate post-draw 各 force-recompile；
- fresh reset pseudocode 四端都恰有三次 `g_motion_releaseLayerIdMemberHint_guess`，旧 local 名为 0；
- Android 两端 pseudocode 直接回读 window/piledCopy 语义名；iOS 的 ADRL/Thumb PC-relative
  operand 即使补 offset type 仍由 Hex-Rays 显示绝对表达式，但 globals catalog 精确回读四个连续
  item 的地址、名称和 size=4；
- 四份数据库全部保存成功。

## 10. 验证

2026-08-16 最终完成：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 test TU syntax-only 均通过，只有既有
  `_tss` 弃用 warning；
- Web Debug 完整依赖重建 `38/38`，成功链接；
- Wasmtime Headless Debug 完整依赖重建 `72/72`，成功链接；
- Node `WebAssembly.Module` parse：两份 wasm 均通过；
- `llvm-objdump -h`：两份 wasm section table 均通过；
- Web wasm：`85,648,287` bytes，539 imports / 69 exports；
- Headless wasm：`84,995,428` bytes，538 imports / 69 exports；
- 相比 V162，两份 wasm 都精确增加 62 bytes，import/export 数不变；
- 两个 CTest build tree 均可运行，但仍报告 `No tests were found`；
- warning 只有仓库既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI experimental 与
  JS-library 项，没有 V163 新 error。

本纵切面闭合旧树 reset 的 shared release hint、完整三槽全局序列及相关重入/commit 边界；
reset 的 child visitor 内部 type-4 固定索引 0 行为和 deque ABI 由既有 node-tree 纵切面继续约束。
这不表示整个 motionplayer 已达到完整一比一。
