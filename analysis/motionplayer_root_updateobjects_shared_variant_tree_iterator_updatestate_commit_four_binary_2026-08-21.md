# MotionPlayer 根对象 UpdateObjects 共享 Variant、live tree iterator 与 UpdateState 提交边界四参考闭环（V269）

## 1. 结论

V269 从 `D3DLayerObject::OnUpdate` 的来参 identity 继续向上追到 root `capture` / `Show`。
四份当前 `reference/binaries/` 一致证明：

- 一整轮 root `FrontItems` 遍历只构造一个 `tTJSVariant`，不是每个 child 或每个 tree node
  构造一个；
- 所有 visible child 都收到同一个 Variant 地址，同时另收一个不会随 Variant mutation 改变的
  整数 `updateState` 参数；
- Variant 初始为 `tvtInteger`，payload 是调用者给出的 state；callback 可以沿 V268 已确认的
  cast-away-const 链改写它，后续 child 会看到改写后的 Variant；
- tree successor 在当前 child 的 `IsVisible` / `OnUpdate` 返回后才从 live current node 计算；
  future erase/insert 可改变本轮后续路径，current erase 会使 iterator increment 访问已释放 node；
- callback 异常会先析构这个可能已被改写的共享 Variant，然后原样向外传播，不继续后续 child；
- `Show` 先 snapshot `UpdateState`，整轮成功后才写 `UpdateState = 0`；callback 重入
  `update(newState)` 在正常返回时被零覆盖，在随后抛出时则因 commit 未执行而保留；
- `capture` 固定调用 `UpdateObjects(0)`，不读取也不消费 root `UpdateState`，callback 中的
  `update(newState)` 会留给下一次 `Show`。

这些都是同步、无 snapshot、无 deferred mutation、无 exception continuation 的原生边界。

## 2. 四端函数地图

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `capture` | `0x531468` | `0x495778` | `0x100233FA8` | `0x232CA8` |
| root `Show` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |
| `UpdateObjects` helper | 内联于两 caller | `0x4962C0` | `0x100234D3C` | `0x2338EC` |
| tree increment helper | `0x1480CD0` | `0xD52C58` | caller 内联 | caller 内联 |
| shared Variant dtor | `0xA0E078` | `0x760238` | `0x100319A60` | 已有 `tTJSVariant_dtor_guess` |
| armv7 SJLJ cleanup | 不适用 | caller EH fragment | 不适用 | `0x2339C6` |

Android arm64 只是在两个 caller 中各内联一份相同语义 body；它没有产生不同的 source-level
协议。其余三端保留独立 helper，为单 Variant owner、参数 ABI 和异常边界提供了最直接证据。

## 3. 共享 Variant 的精确构造与 ABI

共同 source shape 是：

```cpp
void UpdateObjects_guess(tjs_int updateState) {
    tTJSVariant state(updateState);
    for(D3DLayerObject *object : FrontItems) {
        if(object->IsVisible())
            object->OnUpdate(updateState, state);
    }
}
```

局部 Variant 尺寸和关键 lowering：

| 目标 | Variant size | tag store | payload store | first cursor | child payload |
|---|---:|---:|---:|---:|---:|
| Android arm64 capture | `0x18` | `0x53149C` | `0x5314A0` | `0x5314A4` | node `+0x20` |
| Android arm64 Show | `0x18` | `0x5318C8` | `0x5318CC` | `0x5318D0` | node `+0x20` |
| Android armv7 helper | `0x0C` | `0x4962E4` | `0x4962DE` | `0x4962E6` | node `+0x10` |
| iOS arm64 helper | `0x18` | `0x100234D58` | `0x100234D60` | `0x100234D64` | node `+0x20` |
| iOS armv7 helper | `0x0C` | `0x233916` | `0x23391C` | `0x233922` | node `+0x10` |

tag 都是整数 Variant 的 `4`。LP64 payload 接收 sign-extended `tjs_int`；ILP32 直接保存
32-bit 值。Variant 构造在 first cursor load 之前，析构在遍历正常结束之后，因此 owner 范围是
整轮 tree pass，而不是单次 callback。

四端在调用 `OnUpdate` 时都同时传：

1. 原始 integer `updateState`；
2. 这一个局部 Variant 的地址。

`D3DLayerObject::OnUpdate` 又按 V268 直接把第二项地址交给 script `onUpdate`，没有 copy。
因此同一 tree 中的不同 child、同一 child 的 duplicate node 都观察同一地址。若前一个 script
callback 把 Variant 从整数改成字符串/object，后续 callback 收到的是变异后的 Variant；但独立的
integer 参数仍保持 helper 入口值。最终 destructor 也按变异后的 tag/payload 清理资源。

## 4. live 红黑树遍历与 mutation 边界

四端容器 cursor 布局：

| 目标 | first slot | sentinel | node payload | increment |
|---|---:|---:|---:|---|
| Android arm64 | root `+0x60` | root `+0x50` | `+0x20` | libstdc++ helper |
| Android armv7 | root `+0x38` | root `+0x30` | `+0x10` | libstdc++ helper |
| iOS arm64 | root `+0x48` | root `+0x50` | `+0x20` | libc++ inline |
| iOS armv7 | root `+0x2C` | root `+0x30` | `+0x10` | libc++ inline |

当前 node 的 callback 顺序是：

```text
child = current.payload
if child.IsVisible():
    child.OnUpdate(integerState, sharedVariant)
current = tree_successor(current)   // only now
```

由此得到的边界不能套用 V268 的 `std::list` tail 规则：tree insertion 是否同轮可见取决于新 node
相对当前 cursor 的结构位置和 successor 计算结果，并不是简单的“append 一定可见”。共同结论是：

- 删除 future node：返回后的 successor 在已变更 tree 上计算，可能跳过该 node；
- 插入 future-position node：若它落在尚未经过的 successor 路径中，本轮可见；落在已过位置则不可见；
- callback 对 current 调用公开 front-index setter 会 erase/reinsert current，旧 cursor 仍指向被释放
  node；若同一 payload 有 duplicate node，setter 只 erase 一个 node 后就改共享 key，留存 duplicate
  的比较键随之原地变化，还会破坏 tree ordering；
- duplicate payload node 会重复调用，而且复用同一个 Variant 地址；
- 删除 current node：返回后以 freed node 为输入求 successor，形成 UAF/UB；
- 销毁 root/tree：sentinel、current 和 Variant owner 所在 frame/对象关系都失效，行为未定义；
- 每轮 callback 持续插入新的 future-position node 时，遍历可以被无限延长。

原版没有 prefetch-next、snapshot vector、mutation generation、reentrancy guard 或 deferred erase。

## 5. 返回值与异常清理

`IsVisible` 为 false 时跳过 `OnUpdate`，但仍在随后从 live current node 求 successor。
`OnUpdate` 的 bool 返回值不参与 root helper 控制流；普通 `tjs_error` 又已在 D3DLayer script bridge
中被忽略，因此只有真正的 C++ 异常会中止。

异常路径共同语义是：

```text
save active exception
destroy shared Variant
resume/rethrow active exception
```

- Android arm64 的两份内联 body 分别有 landing cleanup；
- Android armv7 helper 有对应 EH cleanup fragment；
- iOS arm64 helper 的 landing 调用 Variant dtor 后 resume；
- iOS armv7 用 SJLJ `call_site=1` 覆盖 `IsVisible`、`call_site=2` 覆盖 `OnUpdate`，两 case
  在 `0x2339D4` 汇合，析构 Variant、把 call-site state 复位为 `-1` 后 resume。

没有 catch、错误码转换、继续下一 child，也没有回滚 callback 已经完成的外部副作用。

## 6. `Show` 的 UpdateState 事务边界

四端共同顺序：

```text
snapshot = root.UpdateState
UpdateObjects(snapshot)
root.UpdateState = 0        // normal-success commit only
if root.Window == null:
    return
... manager settings / target / draw / present ...
```

关键 lowering：

| 目标 | snapshot load | fanout call/body | success reset |
|---|---:|---:|---:|
| Android arm64 | `0x5318C0` | 内联 `0x5318C8..0x53192C` | `0x531934` |
| Android armv7 | `0x495990` | call `0x495994` | `0x49599E` |
| iOS arm64 | `0x1002342BC` | call `0x1002342C0` | `0x1002342C4` |
| iOS armv7 | `0x232F4C` | call `0x232F50` | `0x232F5E` |

reset 位于 helper 正常返回、共享 Variant 正常析构之后；不在 cleanup，也没有 scope guard。于是：

- callback 只执行 `update(41)` 并正常返回：外层 pass 仍继续使用 snapshot/共享 Variant，随后把
  字段覆盖为 `0`，下一次 `Show` 收到 `0`；
- callback 执行 `update(53)` 后抛出：helper 只析构共享 Variant并向外抛，reset 不执行，下一次
  成功 `Show` 收到 `53`；
- callback 在前几个 child 已完成后抛出：已完成的副作用不回滚，后续 child 不执行；
- reset 严格位于 Variant destructor 正常返回之后；四端没有围绕 destructor 再设置 catch 或
  fallback commit；
- Window-null 不改变这个提交顺序：fanout 正常完成后仍清零，再由 window gate 返回。

## 7. `capture` 不消费 UpdateState

四端 capture 分别在下列位置以字面量零进入同一 fanout：

| 目标 | call/body |
|---|---:|
| Android arm64 | 内联自 `0x53149C` |
| Android armv7 | `0x49578E: UpdateObjects(0)` |
| iOS arm64 | `0x100233FCC: UpdateObjects(0)` |
| iOS armv7 | `0x232CBE: UpdateObjects(0)` |

capture 不在 fanout 前读取 root `UpdateState`，fanout 后也不写零。因此：

- 每个 visible child 收到 integer `0` 和初始 integer-0 shared Variant；
- capture 前已经 pending 的 root UpdateState 保持不变；
- callback 在 capture fanout 中调用 `update(newState)`，该值保持 pending，下一次 `Show` 消费；
- callback 抛出时同样只清理共享 Variant，capture 后续 texture 分配/绘制完全不发生；
- callback 改写 shared Variant 只影响本次 capture fanout 和最终 destructor，不会自动写回
  root `UpdateState`。

## 8. 源码和回归改动

`cpp/plugins/DrawDeviceD3D.cpp` 的行为原本已经使用一轮一个局部 Variant；本轮补上经四端确认的：

- shared identity 与 callback mutation；
- live tree post-callback successor；
- current erase UAF；
- exception cleanup/propagation；
- capture 不消费 state；
- Show success-only reset 与重入覆盖规则。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 扩展 recorder 和 windowless root 场景：

- duplicate FrontItems node 两次 `onUpdate` 的 `param[0]` 地址完全相同，整数历史都是 23；
- callback 在 state 29 pass 中重入 `update(41)` 并正常返回，下一 Show 收到 0；
- callback 在 state 31 pass 中重入 `update(53)` 后抛出，下一成功 Show 收到 53。

测试只锁定定义良好的 identity/commit 行为；没有把 current erase UAF 或 comparator 失序写成
不稳定运行时断言。

## 9. Recovery IDB 写回

四库合计：

- 82 条 function/line comment；
- 19 个 bookmark；
- 17 个 `_guess` function rename；
- 16 个 function type/prototype update；
- 17 次定向 force-recompile/readback。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 24 | 6 | 4 / 4 | 4 | 368547676 | `A48E773C7852284480E8CAC87186125EBFA057592F934C1A0458BDC11C65CCF4` |
| Android armv7 | 17 | 5 | 5 / 5 | 5 | 346744394 | `83D70BEB77ED7BA0A1B7D591D433275522D9B81DA3FEF8ECB16E51FDA085E651` |
| iOS arm64 | 17 | 4 | 4 / 4 | 4 | 336228262 | `FC5E4C73DEA92AA48E389958A0D7F1683B987756B6C4F12CE29944117ACB4F07` |
| iOS armv7 | 24 | 4 | 4 / 3 | 4 | 376967012 | `7DC44C81EAE6F09C38211643C5BBAB7002397130E178C25876AF0C20A249E19F` |

iOS armv7 按 pre-backup → candidate → candidate `idat -A` → MCP edits/readback → candidate
`idat -A` → canonical 发布 → fresh canonical decompile/readback 的路径保存。最终四个 canonical
依次通过 `C:\IDA\idat.exe -A`；该最终 canonical pass 改变了 armv7 的数据库字节但不改变长度，
所以 candidate 又从最终 canonical 同步，最终 size/hash 逐字节一致。

## 10. 验证状态

验证结果：

- 完整 `motionplayer-dll.cpp` ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 syntax-only
  均 exit 0；新增 duplicate shared-address、正常重入覆盖、异常重入保留断言均经过编译；
- Web 首次构建在源码编译前的 CMake regenerate 再次暴露 cache 中
  `CMAKE_TOOLCHAIN_FILE=/upstream/...` 旧账；用 `Web Debug Config` preset 恢复为
  `C:/Users/fengxuexin/Developer/emsdk/upstream/emscripten/...` 后，Web 24/24 成功；
- Wasmtime 4/4、Wasmtime guest 1/1 成功；
- Web、Wasmtime、guest 随后顺序复跑均为 `ninja: no work to do`；
- 两个 build tree 的 `ctest --output-on-failure` 都 exit 0，但都准确报告
  `No tests were found!!!`，所以本轮新增 Catch2 断言只能表述为 syntax-validated，不能误称
  已由注册 test executable 运行；
- `git diff --check` exit 0，仅有工作树既有 LF→CRLF conversion warning；
- 本轮 compiled source/test 行范围中的 reference absolute-address 扫描为 0 命中；
- `mcp__idalib__idb_list` 为 `sessions=[]`, `count=0`，也没有残留
  `ida`/`ida64`/`idat`/`idat64`/`idalib-worker` 进程。

最终 Wasm：

| 产物 | bytes | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85655262 | `04E0BE9844ECD04DDCE7DC3640C7FD45F0A71AB05A71651F571387E2B5471E9B` |
| Wasmtime `index.wasm` | 85002403 | `D6BA87DA418A40816221CFC92C994E93B362E81FFB570F61D71226C728C50BBB` |
| Wasmtime guest | 151479034 | `C12DD6F52733E965C5AF063ABC35D0B5309B320E122B5947F4E92F4D7E767C90` |

相对 V268：

| 产物 | total delta | CODE | DATA | name | 其他 objdump section payload |
|---|---:|---|---|---|---|
| Web | `0` | `0x01A41061` 不变 | `0x005A3E40` 不变 | `0x03185F7B` 不变 | 全部 size 不变，哈希变化 |
| Wasmtime | `0` | `0x019E900F` 不变 | `0x005A1090` 不变 | `0x03141E11` 不变 | SHA-256 逐字节不变 |
| guest | `+1` | `0x013D7D9B` 不变 | `0x004D1630` 不变 | `0x01421EBA` 不变 | 所有列出的 debug section size 也不变 |

V269 没有修改 product statement，只补注释与测试。Wasmtime 主产物逐字节不变直接验证 product
codegen 未变。Web 因 preset 恢复触发 24-step 旧生成单元重编译，虽然总长和所有 section size
不变，哈希仍不同；guest 重新链接后总容器长增加 1 字节而 `llvm-objdump -h` 所列 payload size
全部不变。二者不能被表述成可执行语义变化，也不把同 size 的哈希差异错误归因到某个具体 section。

## 11. 闭合范围

V269 已闭合 root fanout 的 Variant owner/identity、tree cursor 计算点、defined mutation visibility、
current erase UAF、callback/Variant 异常、Show UpdateState success-only commit，以及 capture 的
state 非消费边界。

这不表示 motionplayer 全目标完成；下一纵切面继续沿尚未闭合的调用链、容器、owner 与边界行为推进。
