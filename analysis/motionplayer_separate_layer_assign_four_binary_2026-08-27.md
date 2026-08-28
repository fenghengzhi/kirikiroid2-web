# SeparateLayerAdaptor `assign` inner body（四参考二进制，2026-08-27）

## 1. 四端入口与完整证据

| 端 | assign inner entry | instructions | decompiler状态 |
|---|---:|---:|---|
| Android arm64 | secondary entry `0x6A97F0`，RET `0x6AA060` | 537 | IDA把它与前一clear/landing-pad合并为 `0x6A965C`的大range；按secondary entry完整读disassembly |
| Android armv7 | `0x57C814` | 288 | 完整 |
| iOS arm64 | `0x10010347C` | 247 | 完整 |
| iOS armv7 | `0x100874` | 381 | 完整，含SJLJ call-site state |

四端真实assign body合计1453条指令，全部fresh decompile并按240条分页完整读取disassembly。
Android arm64的IDA function range `0x6A965C..0x6AA1D4`实际包含：主入口
`0x6A965C`的public clear正常体、其landing pads，以及 `_Unwind_Resume`后的独立assign
secondary prologue `0x6A97F0`。Hex-Rays只显示可从clear入口到达的前段，因此A64 assign结论
来自537条完整secondary-entry disassembly，并由其余三端完整伪代码逐调用互证；不能把
`0x6A965C`误标成assign。

A64 primary现命名 `SeparateLayerAdaptor_clear_guess`，secondary assign入口已注释/bookmark；
其余三端统一命名 `SeparateLayerAdaptor_assignFromAdaptor_guess`。四库均已保存。

## 2. target pass 与source容器边界

assign接收target `this`和借用source Adaptor引用。入口固定：

1. O(1)交换target active/retired完整红黑树；
2. target per-pass sequence写0；
3. 只遍历source active树，从leftmost开始按uint32 key升序走successor；
4. source retired树、private target、owner和target Variant不参与遍历。

source节点不被移动或擦除。target resolver按相同ordinal复用target retired Layer或创建新Layer，
并把source完整payload复制到target active。target normal尾才invalidate-and-clear仍未复用的
retired节点。

source和target可以别名。参考没有self-assign保护；别名时入口swap立即改变后续被遍历的
source active树，行为由交换后的真实树决定，不能提前return或复制整个source map快照。

## 3. 每个source item都重新把sequence置0

四端在每个source节点的call-local Layer Variant复制之后、调用共享payload resolver之前，
再次把target sequence写0：

- Android arm64 `0x6A9928`；
- Android armv7 `0x57C898`；
- iOS arm64 `0x10010352C`；
- iOS armv7 `0x10091E`。

因此resolver自己的第一条absolute写对每个节点都是 `target.absolute + 0`，然后内部sequence
增长到1；下一节点又重置为0。循环正常结束时sequence保留1（空source则保持入口写入的0）。

本地原来只在assign入口清零一次，多个source节点会发布0、1、2……。本轮把reset移入/复制
到每item resolver前，并把测试扩展为两个ordinal：第二target Layer的resolver-time absolute
必须仍为0；第二次assign后调用payload-free ordinal重载则观察到循环尾保留的sequence=1。

## 4. 每item owner栈与direct receiver

共同构造/析构顺序为：

1. 从source node `layerVariant`复制一份完整source closure，贯穿整个item；
2. target payload resolver返回并持有target Layer Variant；
3. 再从source node Layer Variant构造临时closure，strict `AsObject`取得source Object-only
   owner，立即销毁临时closure；
4. 从target Variant构造临时closure，strict `AsObject`取得target Object-only owner，立即
   销毁临时closure；
5. target `assignImages`的唯一参数再复制一次完整source Variant；
6. item尾按target Object owner → source Object owner → target Variant → 初始source Variant的
   顺序释放。

source/target属性与方法直接以各自Layer Object作receiver/objthis。没有Layer
NativeInstanceSupport测试、SeparateLayerAdaptor识别、private target/target/owner fallback或
null过滤；malformed/non-Object Layer Variant在strict转换/调用处传播异常。

本地原来只有source payload Variant引用，没有第1步独立owner；source/target还走通用
Adaptor解包且缺少Object-only AddRef。本轮恢复完整四owner栈并删除已经无caller的fallback
resolver。测试在source height probe与target assignImages回调处记录引用balance，验证named
Variant的两份Object/ObjThis引用之外仍有一份Object-only owner。

## 5. 图像、尺寸与getter默认值

target先执行 `assignImages(sourceVariant)`，argc=1、result=null。随后source getter顺序严格为：

1. height；
2. width；
3. target `setSize(Integer width, Integer height)`；
4. absolute；
5. visible；
6. opacity；
7. type；
8. left；
9. top。

每个getter等价于 `ncbPropAccessor::GetValue(default=0)`：先以
`TJS_MEMBERMUSTEXIST`和hint=null做一次presence probe；probe失败就返回0并跳过第二次读取；
probe成功后以flags=0再读，第二次HRESULT忽略，对其结果Variant执行Integer转换。probe结果
Variant本身不作为最终值。

setSize两个参数都是Integer Variant，参数数组顺序是width、height；不是Real，也不会因0/
负值跳过。assignImages、setSize和所有后续PropSet的HRESULT均忽略，普通失败不会中断item。

## 6. 六个属性写入与32位rebase

target resolver已经先写过temporary absolute与hitThreshold。assign随后按固定顺序覆盖/复制：

1. `absolute = wrapping_i32(targetBase + sourceAbsolute - sourceBase)`；
2. visible；
3. opacity；
4. type；
5. left；
6. top。

六次均为MEMBERENSURE、result不可见，使用全模块共享member hint。运算在四端都是32位寄存器
ADD/SUB自然回绕。本地原先使用signed C++表达式 `_absolute + absolute - source._absolute`，
溢出为UB；本轮改为两个显式uint32回绕步骤后再还原`tjs_int`。

写入异常与HRESULT失败不同：C++异常立即展开当前item owners并终止整个assign，不会继续后续
属性/节点，也不会执行normal-tail retired clear；之前已经创建、复用和写入的target active
节点全部保留。

## 7. normal-only retired clear与返回

source树走完后，target调用上一报告关闭的ordered-map invalidate-and-clear。只有该clear也
正常返回时inner assign返回success。任一payload复制、Layer callback、property conversion或
Invalidate异常都会绕过尾清理；无catch、无事务回滚。

普通TJS HRESULT失败全部忽略，所以即使target拒绝assignImages、setSize及六个PropSet，循环
仍处理剩余节点、clear retired并返回success。现有fail-dispatch测试继续覆盖这一点；新增的
第二节点同时验证失败的第一target不会阻止后续节点。

## 8. 源代码收敛与验证状态

本轮对 `SeparateLayerAdaptor::assignFromAdaptor_guess`完成：

- 每item sequence reset；
- 初始source Layer Variant完整CopyRef；
- source/target direct Object-only owner及逆序释放；
- 删除assign路径的Adaptor/private-target fallback；
- getter/call/setter恢复sharp direct-Object边界；
- absolute rebase显式32位回绕；
- 两节点sequence、source/target owner balance及ordinal尾sequence断言。

`git diff --check`通过；coverage保持12列。正式CMake/unit/Web build仍因本机缺少CMake、
Ninja、Emscripten且没有既有build/out目录而未运行。当前报告只关闭inner assign；后续
`motionplayer_separate_layer_ncb_surface_four_binary_2026-08-27.md`已关闭NCB参数计数、
native-instance转换、result规则与constructor attach rollback，public clear/destructor则由
`motionplayer_separate_layer_clear_destructor_four_binary_2026-08-27.md`关闭。
