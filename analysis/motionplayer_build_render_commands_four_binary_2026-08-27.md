# Player `buildRenderCommands` 与组合层生产阶段（四参考二进制，2026-08-27）

## 1. 证据范围

| 端 | 主函数 | size | body instructions | 组合层阶段入口 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C2208` | `0x1BAC` | 1766 | `0x6C325C`，item body `0x6C3274` |
| Android armv7 | `0x58C7C4` | `0x104A` | 1348 | `0x58D2E2`，item body `0x58D312` |
| iOS arm64 | `0x1001167BC` | `0x1198` | 1083 | `0x1001174B4`，item body `0x1001174DC` |
| iOS armv7 | `0x114118` | `0x12C4` | 1582 | `0x114E8A`，item body `0x114EA8` |

四端主函数合计5779条指令，均重新decompile并按240条分页完整读取disassembly。Android
armv7和iOS arm64取得完整主函数伪代码；另外两端的大函数伪代码输出发生体积截断，因此以
完整disassembly、引用字符串、调用目标和另两端完整伪代码互证。iOS armv7还存在独立
`0x1153DC`、`0x756`字节、565条指令的81-state SJLJ cleanup；该cleanup也已完整decompile和
逐页读取。四端主函数加cleanup共6344条指令。

本地恢复把单体参考函数按语义拆成：

- `Player::buildRenderCommands`：pass生命周期、main指针序列和尾清理；
- `Player::emitPreparedLeafLayerCopy_guess`：active ordered map中的叶层解析与copy；
- `Player::composePreparedGroupLayers_guess`：aux/group指针序列、组合Layer和child mask。

拆分点不增加容错、事务回滚或额外owner；各阶段的写入和异常可见性仍按参考单体顺序排列。

## 2. pass、main容器与裁剪写入顺序

main/aux和group child均是连续的借用 `PreparedRenderItem*` 序列。四端直接解引用元素，没有
null slot检查；null不是空项语义，而是sharp boundary。

函数入口若 `_renderSeparateLayerAdaptor` 已存在，立即交换active/retired树并开始本pass，
然后才遍历main。main item的共同控制流为：

1. `drawFlag == false`：不计算clip，也不清理 `rawFlag20/rawFlag21/clipRect`；
2. `drawFlag == true && rawFlag16 != 0`：仅清 `rawFlag21`；
3. 其余draw item先把paintBox与四条target/camera边相交；数值有效的viewport再按
   left/top=floor、right/bottom=ceil收窄；
4. 最终横、纵两个轴都必须满足严格小于，失败仅清 `rawFlag21`；
5. 成功先写 `rawFlag21=true`、四float `clipRect`和toward-zero整数dirty rect，之后才进入
   任何可能抛异常的TJS/adaptor回调。

ARMv7的伪代码容易把最后门槛误读成只检查横轴。Android `0x58D2B6`和iOS `0x114382`的
真实指令都是条件执行的第二次 `VCMPE`/`VMRS`：第一轴非negative时第二次比较不执行并直接
reject；第一轴成立后第二轴仍须negative。AArch64用 `FCMP`+`FCCMP`表达同一双轴严格门槛。
因此本地 `left < right && top < bottom` 已正确，本轮没有把它改成横轴单门槛。

## 3. SeparateLayerAdaptor懒创建与layer id latch

成功clip之后若adaptor仍为空，四端执行：

1. 求值 `Window.mainWindow`并保留owner Variant；
2. `operator new`先取得待构造存储，再求值 `primaryLayer`构造参数；
3. 构造 `SeparateLayerAdaptor`；
4. 仅在构造成功后把raw pointer发布到Player字段；
5. 新adaptor立刻执行与入口已有adaptor相同的begin-pass。

因此 `primaryLayer`读取或构造异常不会把半成品指针发布到Player。反过来，成功发布后
begin-pass若抛异常，Player仍持有该adaptor。

`rawFlag20`是require-id latch。为false时才无参数dispatch `requireLayerId`，把返回值转换并
写入 `renderLayerId`，最后才把 `rawFlag20`置true。回调异常会留下false和原数值slot；没有
按名称复用、预先latch或回调后的二次检查。

## 4. 叶层payload、ordered map与source copy

叶层resolver输入固定包含：Player completionType、false outline/meshline、command src、blend
0、四个opaque-white color、paintBox+viewport、corners，以及meshType 1的command Bezier向量
或meshType 2的command composite向量。type 1这里故意不是普通execute使用的实际
`meshPoints`。

resolver在 `SeparateLayerAdaptor` active ordered map中按32-bit render layer id解析/搬移节点，
返回Layer Variant与created-or-changed byte。调用者先把Variant发布到item `leafLayer`，再从
它的独立CopyRef取得Object owner；只有created-or-changed为真才继续物化source。false直接
返回，但刚发布的 `leafLayer`仍保留。

物化顺序为：

1. descriptor依次写key、src、blendMode=0；color数组按0..3写四个white值；
2. 解析source，固定先读width、再读height；
3. 叶Layer先写Integer `neutralColor=0`，再以两个Real参数调用 `setSize`；
4. width/height先按float计算 `right-left`/`bottom-top`，之后才提升为Real；
5. meshType 0用TL/TR/BL三点，逐点先提升double，再做 `corner - 0.5 - clipOrigin`并
   `affineCopy(clear=true)`；
6. meshType 1用command Bezier向量和共享cell-division流程；meshType 2用command composite
   向量与保存的divX/divY；两者分别调用Bezier/mesh copy，clear均为true。

Layer/source/item/sourceState指针都按可信边界使用。descriptor、color、source Variant、
source accessor与leaf Object owner的析构范围由iOS ARMv7 81-state cleanup互证；没有
native image探测、备用Layer恢复或诊断copy。

## 5. aux/group union与反直觉空矩形边界

组合阶段直接使用调用者传入的四条target边，不从width/height重建原点零矩形。每个group：

1. 以group自身paintBox作为union seed；
2. 只合并 `child.rawFlag21`为真的child paintBox，使用的不是child clipRect，也不额外检查
   child `rawFlag16`；
3. 先与camera四边clamp，保存这四个中间值；
4. group viewport数值有效时再floor/ceil并收窄为最终clip；
5. 空判断却使用第3步的camera-clamped值，条件是 `left > right || top > bottom`；相等边界
   仍被接纳；
6. 空时只清group `rawFlag21`，其余字段保持；非空时继续使用第4步最终值。

第5步意味着：camera-clamped union非空，但viewport narrowing把最终clip变成反向尺寸时，
参考实现仍会向Layer发送负Real width/height并最终发布反向clip。iOS ARMv7 `0x115028`附近
直接证明compare读取的是camera-clamped寄存器，而非收窄后的寄存器。本轮新增测试专门锁定
该行为，避免日后按常识把空判断移到viewport之后。

AArch64的union `FCSEL`和ARMv7条件move在相等/NaN时具有可观察的operand选择。本地把child
paint operand放在 `std::min/max`第一参数，保留四端在相等、signed zero和NaN时的选择方向。

## 6. composed Layer、child mask与发布时机

只有 `composedLayer.Type() == tvtVoid`才重新求值 `Window.mainWindow.primaryLayer`并通过共享
Layer factory创建对象；非Void值不做类型修复或native-Layer恢复。随后以Real width/height
调用 `setSize`，再以argc5 `fillRect(0,0,width,height,0)`清空。

child mask只要求 `rawFlag21`为真且 `leafLayer`非Void。destination composed Variant先CopyRef，
再快照child left/top，然后source leaf Variant CopyRef；width/height、Player maskMode和group
stencilComposite在source CopyRef之后读取，所以CopyRef触发的重入可以改变后半段参数。mask
阈值固定64，source offset固定0,0，目标offset与child尺寸按signed toward-zero边界转换。

group的 `rawFlag21=true`、`rawFlag16=false`和最终clipRect只在setSize、fillRect及全部child
mask正常完成后发布。中途异常保留旧flags/clip，但已成功创建的composedLayer会保留；这是
有意的部分提交，而非事务。

## 7. normal-only尾清理与81-state异常路径

main和group阶段正常结束后，只有本次确实begin过adaptor pass才执行end-pass，清理没有从
retired树搬回active树的Layer节点。iOS ARMv7正常尾位于 `0x11538C`附近；独立81-state
cleanup只释放各state已构造的Variant/accessor/Object/vector临时，不调用这个retired-tree
清理。异常返回后保留已交换树和此前所有已发布字段，下一次调用从该持久状态继续。

本地因此继续使用显式normal-flow布尔标记，没有RAII scope guard补做end-pass，也没有catch
回滚。

## 8. 本轮源代码收敛

四端主函数具有生产TJS成员字符串，但没有motion path、logo chain、snapshot、stderr格式化
或headless render trace数据流。本轮从 `PlayerRenderExecute.cpp`删除：

- build入口/出口 `motionTraceRenderBuildCommands*`；
- matched motion path及logo trace/snapshot enable判断；
- clip failure `optional<string>`、两段格式化compare日志；
- leaf-copy logo日志和额外motionPath参数；
- `SNAPCMD`筛选/打印以及最终计数日志；
- 因此不再需要的 `MotionTraceWeb.h`与`<optional>` include。

这些侧车会增加字符串分配、路径匹配、格式化、stderr I/O和新的异常点，删除后主函数恢复为
四端共同的数据流。另新增双轴严格clip和viewport后反向group extent两类回归断言；本轮没有
修改已确认正确的双轴门槛，也没有给可信指针容器增加null过滤。

## 9. 验证状态

本报告对应两个coverage slice，均标记 `IMPLEMENTED`：主/pass/leaf producer，以及内联的
composed-group producer。四个IDB的主函数已统一命名为 `Player_buildRenderCommands_guess`；
iOS ARMv7 cleanup命名为 `Player_buildRenderCommands_SjLj_cleanup`，双轴门槛、group空判断、
normal-only尾部均已加注释，四库已保存。

`git diff --check`通过；coverage维持12列。正式CMake/unit/Web build仍因当前机器没有CMake、
Ninja、Emscripten且没有既有build/out目录而无法运行。新增测试是源码级验证资产，不能声称
已经执行通过。
