# motionplayer common builder：retired tree normal-only 清理、直接 Object Invalidate 与异常重试（四参考二进制）

日期：2026-08-18  
阶段：V241

## 1. 结论

四个参考二进制把 `SeparateLayerAdaptor` 的 pass 状态实现为两棵完整 ordered tree 与一个 insertion
sequence：

1. common command builder 入口若已存在 adaptor，先交换完整 active/retired tree，再把 sequence 清零；
2. 本轮首次需要 separate layer 时，先构造并发布 adaptor，再立即执行相同的 whole-tree swap/reset；
3. leaf/group/Layer/dispatch 任一阶段抛异常都会绕过 builder normal tail，retired tree 不被清理；
4. normal tail 才调用独立 retired cleanup helper；普通 `Invalidate` failure HRESULT 被忽略；
5. helper 遍历时不从 tree 擦除节点。它先完整复制当前 mapped payload，再只按 Variant 的 Object tag
   决定是否调用 `Object->Invalidate(0,null,null,Object)`；
6. 调用直接解引用 `Variant.Object`，没有 Object null guard，且完全不使用 closure 的 ObjThis；
7. 每个正常返回的 payload copy 先析构，随后才计算 successor；所有节点成功后才整体 destroy/reset tree；
8. 任一 `Invalidate` 抛异常时，最终 whole-tree clear 不会执行，原 tree 的全部节点仍在。重试从 first node
   重新开始，因此此前已成功失效的节点会再次收到 `Invalidate`；
9. 下一次 builder begin 不是“继续同一 retired iterator”，而是再次交换两棵完整 tree：异常退出时的
   partial active tree 与未消费 retired tree 整体交换角色，sequence 再次归零；
10. ordinary Canvas 与 accurate SLA 两个真实 caller 都只在 builder 正常返回后读取 prepared-list
    begin/end；builder 或 retired cleanup 的异常直接进入 caller unwind，不存在 bool/status 分支或 retry catch。

本轮据此修正 portable helper：删除 null Object 容错，删除 closure ObjThis receiver，改为直接 Object
receiver。ordered-map probe 同时锁定 ignored HRESULT、tree-intact exception 与 from-head retry。

## 2. 四端地址映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| common builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| existing adaptor load | `0x6C2254` | `0x58C7EC` | `0x10011680C` | `0x114160` |
| entry whole-tree swap | `0x6C225C..0x6C22E4` | `0x58C7F2` | `0x100116814` | `0x11418E` |
| entry sequence reset | `0x6C22EC` | `0x58C7FE` | `0x100116820` | `0x11419A` |
| lazy adaptor publication | `0x6C2460` | `0x58C964` | `0x100116954` | `0x11448A` |
| lazy swap / reset | `0x6C2468/0x6C2500` | `0x58C970/0x58C97C` | `0x100116964/0x100116970` | `0x11449A/0x1144A8` |
| normal-tail helper call | `0x6C3798` | `0x58D7E2` | `0x100117904` | `0x11539C` |
| retired cleanup helper | `0x6C46C4` | `0x58E174` | `0x10011844C` | `0x116280` |
| full payload copy start | `0x6C4714` | `0x58E1BE` | `0x1001184A4` | `0x116318` |
| Object tag gate | `0x6C4720` | `0x58E23A` | `0x100118538` | `0x1163B6` |
| direct Object Invalidate | `0x6C4740` | `0x58E24A` | `0x100118558` | `0x1163CE` |
| whole-tree destroy/reset | `0x6C4784..0x6C4794` | `0x58E27A` | `0x100118628..0x100118630` | `0x116454..0x116466` |
| ordinary Canvas call | `0x6C494C` | `0x58E3A4` | `0x1001187E4` | `0x1166AC` |
| accurate SLA call | `0x6C7254` | `0x59055E` | `0x10011AB08` | `0x118EFA` |

地址只保存在 `analysis/` 证据表；portable compiled source/comments 不携带单一参考目标的绝对地址。

## 3. 两棵 tree 的平台布局与 pass 状态机

四端语义一致，但 STL ABI 与 adaptor field offsets 不同：

| 目标族 | active tree | retired tree | sequence | ordered-tree ABI |
|---|---:|---:|---:|---|
| Android arm64 | adaptor `+0x48` | `+0x78` | `+0xA4` | libstdc++ LP64 tree header |
| Android armv7 | adaptor `+0x24` | `+0x3C` | `+0x58` | libstdc++ ILP32 tree header |
| iOS arm64 | adaptor `+0x40` | `+0x58` | `+0x74` | libc++ LP64 `__tree` |
| iOS armv7 | adaptor `+0x24` | `+0x30` | `+0x40` | libc++ ILP32 `__tree` |

begin pass 的共同模型是：

```text
if Player.separateLayerAdaptor != null:
    swap(adaptor.activeTree, adaptor.retiredTree)
    adaptor.sequence = 0

... builder traversal ...

if normal return and adaptor != null:
    clearRetiredLayers(adaptor)
```

lazy path 不是特殊状态：constructor 完成后先把 raw adaptor pointer 写进 Player，再对新对象执行同一
`swap + sequence=0`。因此 factory/owner callback 发生在 publication 之前；publication 之后的 swap、leaf
materialization 或 tail exception 都会留下一个可由后续调用观察到的 persistent adaptor。

假设一次正常 pass 结束时 active 持有本轮 Layer、retired 已空，则下一次 begin 把旧 active 整体移到
retired，新 active 为空，resolver 从 retired 复用或留下未消费节点。若本轮异常退出：

- active 保留已经创建/复用并发布的 partial new-pass nodes；
- retired 保留尚未由 normal tail 清掉的 old-pass nodes；
- 下次 begin 再次 whole-tree swap，使两批节点整体交换 active/retired 角色；
- sequence 从零重启，没有保存异常点 iterator 或 ordinal cursor。

这与“异常后继续清上次 retired suffix”不同；tree role exchange 本身就是 retry 状态的一部分。

## 4. retired cleanup 的精确算法

共同伪代码可表达为：

```text
node = retired.first
while node != retired.sentinel:
    payloadCopy = copy_construct(node.mappedPayload)

    if payloadCopy.layerVariant.type == Object:
        object = payloadCopy.layerVariant.Object
        object->Invalidate(0, null, null, object)
        // ignore ordinary HRESULT

    destroy(payloadCopy)
    node = tree_successor(node)

destroy_all_nodes_and_reset(retired)
```

“copy before call”不是只复制 `tTJSVariant`：Android armv7 明确构造132-byte complete payload；其余三端
也逐字段复制 Variant、string/vector/geometry owners。payload copy 的 `layerVariant` 因而在 callback
期间独立持有 Object/ObjThis 引用，原 map node 不被提前擦除。

successor 在 callback 与 payload-copy cleanup 之后才计算。参考实现并没有为 callback reentry 修改同一
tree 建立稳定 iterator snapshot；portable 也保留这一顺序，而不把它宣传成支持 reentrant map mutation
的契约。

只有 loop 全部正常完成后，helper 才进入 tree destructor/reset：

- Android arm64 重置 root/left/right/count；
- Android armv7 销毁完整 libstdc++ tree；
- iOS arm64 与 armv7 调用 libc++ subtree destroy，随后重置 begin/root/count slots。

不存在“每成功一项立刻 erase”的渐进提交。

## 5. Variant closure 边界：Object、ObjThis、null 与 HRESULT

四端调用序列都只检查 Variant tag `tvtObject`，随后直接取 Object pointer：

```text
vtbl = object->vtable       // no null check
Invalidate = vtbl[slot]
Invalidate(object, 0, null, null, object)
```

LP64 的 virtual slot byte offset 为 `+0x70`，ILP32 为 `+0x38`。receiver 与最后一个 ObjThis 参数都来自
同一个 Object pointer；Variant closure 内另存的 ObjThis 不参与该调用。

因此边界是：

- `Object != ObjThis`：方法分派到 Object，传入的 objthis 也是 Object；
- `Object == null, ObjThis != null` 且 tag 仍为 Object：自然 null dereference，不静默跳过；
- non-Object tag：不调用 Invalidate，但 payload copy 仍正常析构；
- `Invalidate` 返回 `TJS_E_FAIL` 等普通 failure HRESULT：返回值不读，继续下一个 node；
- C++/TJS exception：立即离开 helper，不执行 successor 与 final tree clear。

旧 portable helper 同时有两处不符：`value.AsObjectNoAddRef()` 被用作 null guard，且调用
`AsObjectClosureNoAddRef().Invalidate(..., nullptr)`，会走 closure ObjThis。V241 改为直接
`object->Invalidate(0,nullptr,nullptr,object)`。

## 6. exception cleanup 与 retry 可观察性

Android arm64 在 `0x6C47C4..0x6C4814` 有显式 cold EH cleanup：释放本轮 copied payload 的 Variant、
string/vector owners后 `_Unwind_Resume`，但不触碰 retired tree。iOS armv7 的 SjLj call-site 3 从
`0x11651A` 进入 landing helper，同样析构当前 complete payload copy后
`___Unwind_SjLj_Resume`，tree 保持原样。

Android armv7 与 iOS arm64 没有在 helper body 内发射相同形态的显式 cold block；其 unwind table/
compiler cleanup形态不同。本报告不把另两端误写成相同机器码 landing layout。四端共同、可以由 CFG
确定的边界是：抛出点之后没有 successor，也到不了 final map destroy/reset；异常继续传播给 builder。

由于迭代期间从未 erase map node，retry 的可观察结果为：

```text
retired = [A, B, C]
A.Invalidate -> ordinary failure HRESULT  // ignored
B.Invalidate -> throws

after catch outside builder:
retired still contains [A, B, C]

retry clear:
A.Invalidate again
B.Invalidate again
C.Invalidate
then whole-tree clear
```

这也解释了为何 ordinary HRESULT 与 exception 必须分开建模：前者不阻断 progress，后者保留完整 tree。

## 7. builder tail 与两条 caller 的传播

common builder 尾部四端都先重载 Player 的 adaptor pointer；null 直接 return，nonnull 才调用 helper。
builder 的其他 exception landing pads只析构其栈上/局部 owners，没有任何第二条 retired helper edge。
所以 leaf、group composed Layer、alpha mask 或 tail Invalidate 抛出时都不会补偿性清树。

四端重新复核的两个真实 caller具有同一后继：

- ordinary Canvas：call 返回后立即读取 main prepared-pointer vector 的 begin/end，空则跳到 caller tail，
  非空进入 draw loop；
- accurate SLA：call 返回后同样立即读取 main vector begin/end，再进入逐 item Layer materialization；
- iOS armv7 两个 call-site 都先写 SjLj call-site 6；异常走 caller landing/unwind，而不是 fall through 到
  prepared-list read；
- 两类 caller都不读取 builder 返回寄存器（builder 是 `void`），不检查 cleanup HRESULT，也没有围绕
  builder 的 retry catch。

portable 的 `buildRenderCommands` 保持 `void`，由 C++ exception自然穿过 ordinary/accurate wrapper；
本轮没有添加错误码翻译或本地 catch。

## 8. 源码与测试变更

源码只修正 `cpp/plugins/motionplayer/SeparateLayerAdaptor.cpp` 的内部 invalidation helper：

- Object tag后不再容忍 null Object；
- 不再读取/调用 closure ObjThis；
- `Invalidate` 的 dispatch receiver与 objthis均为 Object；
- `value.Clear()` 仍只在正常返回后执行；异常时由 copied payload 的 RAII析构。

`SeparateLayerOrderedMap_guess::clear(true)` 的现有 copy-then-clear 结构已符合 tree-intact exception语义，
无需改写为 erase loop。

新增 Catch2 probe构造两个 ordinal：

1. 每个 Variant 的 Object 与 ObjThis 故意不同；probe只接受
   `{flags=0,member=null,hint=null,objthis=Object}`；
2. 第一个 Object 返回 `TJS_E_FAIL`，证明 ordinary HRESULT不阻断第二节点；
3. 第二个 Object 首次抛 `std::runtime_error`，验证 map仍非空且两个 ordinal均存在；
4. 关闭抛出后重试，验证第一、第二节点 call count均从1变2，最后 map为空。

null Object 的自然崩溃边界不在进程内单测故意触发；它由四端 direct dereference机器码证据锁定。

## 9. IDB 写回与 iOS armv7 安全保存

V241 在 Android arm64、Android armv7、iOS arm64 各写回12条语义 comment、4个 bookmark、1个 helper
rename；iOS armv7 写回14条 comment、4个 bookmark、1个 helper rename。四端 helper统一命名为
`SeparateLayerAdaptor_clearRetiredLayers_guess`。覆盖 begin swap/reset、lazy publication、normal-only
tail、complete payload copy、direct Object call、normal temp cleanup、success-only tree reset及可见 EH cleanup。

iOS armv7 继续遵循 V240 pack事故后的安全流程，没有再次调用 `idb_close(save=true)`：

1. live MCP session把更新显式另存为不同路径的 compressed copy；
2. 独立 `C:\IDA\idat.exe` 重新加载，日志出现 `CODEX_IDB_OPEN_PROBE_OK` 且退出码0；
3. MCP session以 `save=false`关闭；
4. 旧 canonical 与存在的 `.id0/.id1/.nam` 逐文件移动到
   `out/idb-recovery/v241-ios-armv7/pre-v241-canonical/`，没有删除；
5. 经验证的 packed copy移动到 canonical路径；
6. canonical 再由 MCP重开，读回 helper rename和两条 direct-Object comment，再以 `save=false`关闭。

更新后的 canonical为375,494,864 bytes，SHA-256
`D6C447F6C770B0E2A230FDFD4BCCC594644AC5FCBD0BECB0772BD7ED74D4BD93`。pre-V241 canonical保持
374,905,040 bytes、SHA-256
`7E2ADC370CFC611A122BFB3DD4C94D878E840F62516DD9A81423A3D5DC344F1C`，可完整恢复。final supervisor
audit为0个 open IDA sessions。

## 10. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax compilation：通过；
- Web Debug：`SeparateLayerAdaptor.cpp`重新编译、static library及主 wasm链接通过；
- Wasmtime Headless Debug：guest object、portable object、static library及主 wasm链接通过；
- `krkr2_wasmtime_guest`：重新链接并完成 exnref转换；
- Node `WebAssembly.Module` construction：通过；imports/exports仍为 Web `539/69`、Wasmtime `538/69`；
- Web/Wasmtime两棵 CTest：当前配置明确 `No tests were found`；
- Web/Wasmtime/guest final no-work：通过；
- `git diff --check`：目标文件无 whitespace error；
- final IDA supervisor audit：0 open sessions。

| product | size | CODE | DATA | name | SHA-256 |
|---|---:|---:|---:|---:|---|
| Web `out/web/debug/index.wasm` | 85,654,641 | `0x1A40F19` | `0x5A3E40` | `0x3185E59` | `12B90111E672774633C22CFFE22411E1A336CFA6DE6729376B223F37869DE1AD` |
| Wasmtime `out/wasmtime/debug/index.wasm` | 85,001,782 | `0x19E8EC7` | `0x5A1090` | `0x3141CEF` | `8AB3F1DFEA6EDD28FB1145373451EEE2DBD9A2A1DD237F8A417335096C91049F` |

相对 V240，两端 CODE/module各缩16 bytes；FUNCTION/GLOBAL/DATA/name与 imports/exports均不变。缩减来自
删除 null/closure receiver容错分支，不涉及 persistent layout或脚本 ABI。

guest SHA-256为
`06FB51CC95C03B836F0874B4127161C83809440A9C9F68716C806EE1E15BA51C`。

## 11. 下一边界

V242 转入两个真实 caller在 common builder正常返回后的 prepared-pointer traversal 与 exception owner
cleanup：分别锁定 ordinary Canvas draw loop与 accurate SLA item-loop如何消费 main/aux persistent
items、何时发布/释放 Layer Variants，以及 loop内任一 callback抛出后哪些栈 owner、prepared tree与 SLA
active/retired状态保留。已有 `renderToCanvas`/accurate-SLA粗粒度报告只作索引，必须按四参考重新闭环。
