# MotionPlayer particle Array add/erase hint 与 result 生命周期四参考复原（2026-08-16）

## 范围与结论

本纵切面从 V155 六槽 EmoteEdit family 的紧邻下一地址开始，对四份
`reference/binaries/` 重新审计 `Player_updateParticleSystems_guess`、
`Player_stepParticleChildren_guess` 与数字索引 child helper。四端共同闭合出一个严格的
2×4-byte process-wide member-hint family：

- slot 0 是 Array `add`，只由 `Player_updateParticleSystems_guess` 消费；
- slot 1 是 Array `erase`，由 update 与 child-step 两条路径共享；
- update 中 `add` 和超量删除 `erase(0)` 都传 null result；
- child-step 的 `erase(index)` 传一个独立、默认构造的非 null result `Variant`；
- 数字索引 child helper 自己持有 element `Variant`，完成严格 Player native conversion 后在
  helper 返回前析构；它不把 element owner 交给 caller，也不与 erase result alias；
- family 后的下一槽只进入 render-source 解析路径，因此本轮边界在两个槽后闭合。

这里专门纠正了一次审计中间态：仅从 caller 栈槽观察，曾把 step 的非 null erase result
误判为索引元素 owner。随后对四端 element helper 重新反编译，四份结果都直接显示 helper
内部局部 `Variant` 的构造、numeric `PropGet`、native conversion 与析构；caller 中的
erase result 是另一个对象。portable 代码、测试、IDB 注释和书签均以这次四端复核后的结论
为准。恢复名来自 stripped binary，继续使用 `_guess`；绝对地址只保留在本文和 recovery IDB。

## 两槽 family 与相邻边界

| idx | recovered symbol / member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 0 | `particleArrayAddMemberHint_guess` (`add`) | `0x1AB543C` | `0x11118D8` | `0x101B69904` | `0x187D5A8` |
| 1 | `particleArrayEraseMemberHint_guess` (`erase`) | `0x1AB5440` | `0x11118DC` | `0x101B69908` | `0x187D5AC` |

两个 data item 在四端均为独立 4-byte `unsigned int`，满足 `erase == add + 4`。紧邻下一槽为：

| target | next slot | fresh consumer |
|---|---:|---|
| Android arm64 | `0x1AB5444` | render-source resolver only |
| Android armv7 | `0x11118E0` | render-source resolver only |
| iOS arm64 | `0x101B6990C` | render-source resolver only |
| iOS armv7 | `0x187D5B0` | render-source resolver only |

所以不能因为 BSS 中继续排列着 `loadSource`、`blendMode`、`assignImages` 等 cache，就把它们
并入 add/erase family。V155 的 last+4 正好等于本轮 add；本轮 erase+4 又正好跨入下一职责。

## 函数映射

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_updateParticleSystems_guess` | `0x6BC4BC` | `0x588A48` | `0x100111D08` | `0x10F51C` |
| `Player_stepParticleChildren_guess` | `0x6BEB84` | `0x58AB50` | `0x1001140C8` | `0x111AF8` |
| `ParticleArray_getNativePlayerAt_guess` | `0x6BEA58` | `0x58AAB0` | `0x100113FE4` | `0x1119DC` |

fresh data-xref readback 给出以下 consumer set：

| slot | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `add` | `0x6BD5BC`, `0x6BD5D0` | `0x58949C`, `0x5894AE` | `0x100112D08` | `0x110690`, `0x11069A` |
| `erase` update | `0x6BD620`, `0x6BD634` | `0x5894E0`, `0x5894EC` | `0x100112D74` | `0x1106E8`, `0x1106F2` |
| `erase` step | `0x6BEC34`, `0x6BEC48` | `0x58AC46`, `0x58AC50` | `0x100114158` | `0x111C48`, `0x111C52` |

ARM64 的 ADRP/ADD、Thumb 的 MOVW/MOVT 与 literal-pool 会为一次 source-level 地址读取产生
多个 raw xref；归并 containing function 后，`add` 只有 update 一个 consumer，`erase` 恰有
update 和 step 两个 consumer。

## 三个调用边的精确 ABI

| edge | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| update `add(child)` | `0x6BD5DC` | `0x5894B6` | `0x100112D18` | `0x11069E` |
| update `erase(0)` | `0x6BD640` | `0x5894FA` | `0x100112D84` | `0x1106F6` |
| step `erase(index)` | `0x6BED10` | `0x58AC58` | `0x100114224` | `0x111C56` |

四端对三个调用边都给出同一 dispatch ABI：

| property | update `add` | update `erase(0)` | step `erase(index)` |
|---|---|---|---|
| flags | 0 | 0 | 0 |
| named hint | add slot | erase slot | 同一 erase slot |
| result pointer | null | null | 非 null、独立默认 result `Variant` |
| argument count | 1 | 1 | 1 |
| argument | copy-constructed child `Variant` | Integer(0) | Integer(current index) |
| receiver / objthis | particle Array / same Array | same | same |
| ordinary HRESULT | ignored | ignored | ignored |

`add` helper 必须从 caller 的 child `Variant` 再 copy-construct 一个参数对象；不能把原对象地址
直接借给脚本回调。两个 erase helper 调用都先建立 Integer 参数 `Variant`。返回码不形成控制流
gate，但脚本回调或 conversion 抛出的 C++ 异常仍沿正常 unwind 传播。

## child helper 与 step 生命周期

四端 element helper 的共同序列是：

1. 在 helper 栈帧内默认构造 element `Variant`；
2. 对 Array receiver 执行 flags=0 的 numeric `PropGetByNum(index)`，result 指向该局部对象，
   `objthis` 仍是 Array；
3. 从 element 的 object/no-add-ref 视图执行严格 Player native-instance conversion；
4. 保存得到的 raw `Player *`；
5. 在 helper 内析构 element `Variant`；
6. 将 raw pointer 返回 caller。

四个 helper 内局部析构点分别为：

| target | element Variant destructor |
|---|---:|
| Android arm64 | `0x6BEB10` |
| Android armv7 | `0x58AAFC` |
| iOS arm64 | `0x100114060` |
| iOS armv7 | `0x111A6C` |

因此 caller 对 child 的使用发生在 numeric getter 的 element owner 已释放之后；它依赖 Array
本身继续持有该对象。step 第一遍若判定删除，随后才建立另一只默认 result `Variant` 并调用
`erase(index)`。四端的可观察顺序为：

1. helper 中 indexed element 参数/owner 析构并返回 raw child；
2. caller 根据 `_allplaying` 与可选 outside-rectangle strict-overlap 计算删除条件；
3. 建立独立默认 erase-result；
4. erase helper 建立 Integer(index) 参数并调用脚本；
5. erase helper 返回并析构 Integer 参数；
6. caller 重新读取 Array `count`；
7. `--childIndex`，从而让 loop increment 后重试同一 numeric slot；
8. result `Variant` 在 count 读取之后析构。

这个顺序解释了两个重要边界：脚本可以把任意值写入 erase result，但 native 不读取该值；若
脚本返回普通失败或根本没有缩短 Array，count 仍会刷新，随后循环可能一直重试同一槽。result
必须活到 refreshed count 之后，但绝不能与已在 helper 内销毁的 element owner 合并。

update 的 max-count 路径不同：新 child 先经 `add` 发布，再读取 `count`；只有 signed count
大于 `particleMaxNum` 时调用一次 `erase(0)`，不接收结果，也不在本分支重复刷新 count。
`particleMaxNum == 0` 因而会删掉刚发布到非空 Array 的第零项一次。

## portable 源码改动

- `MotionDispatch.h` / `RuntimeSupport.cpp`：在 V155 六槽后按原生相邻顺序增加两个独立
  `tjs_uint32` cache：`particleArrayAddMemberHint_guess`、
  `particleArrayEraseMemberHint_guess`；
- `MotionNodeBridge.cpp`：移除匿名局部 add/erase hints，两个 helper 改用 process-wide
  共享对象；`particleArrayErase_guess` 接受可选 result pointer并原样转发；
- `MotionNode.h`：公开上述 result 形状，但 numeric child helper 保持只有 Array/index 两参；
- `ParticleArray_getNativePlayerAt_guess` portable 形状恢复为 helper 内局部 element
  `Variant`，完成转换后随函数返回析构，不向 caller 暴露 owner；
- `PlayerUpdateParticles.cpp`：update 的 `add` 与 `erase(0)` 保持 null result；child-step
  删除分支建立独立 `eraseResult`，使其作用域覆盖 post-erase count read。

这些改动没有把绝对地址写入编译源码，也没有为未知原始标识伪造确定名。

## 回归探针

新增 `ParticleArrayCallRecorder` 与
`particle Array mutation family preserves hints and worker erase result slot` 测试，锁定：

- `add` 的 flags=0、准确 member、准确 process-wide hint、null result、单个 child copy 参数与
  receiver/objthis；
- `erase` 与 add 的 hint 身份不同，两个 erase 调用之间又共享同一 hint；
- worker 形状可传入独立非 null result，回调写入值会落到该对象；
- update 形状仍可省略 result 并实际传 null；
- erase 的负 index 仍按 TJS Integer 原样进入 dispatch，不在 helper 内截断或校验。

测试刻意不再把 result 命名为 retained element，也不通过 numeric helper 向 caller 暴露
element owner，以免再次固化被四端 helper 反编译否定的 alias 关系。

## IDB 回写

四份 recovery IDB 均已完成并保存：

- 对两槽完整 8-byte 范围先整体 `undefine`，再建立两个独立 4-byte `unsigned int` data item；
- 写入 `particleArrayAddMemberHint_guess` / `particleArrayEraseMemberHint_guess` 名称、consumer
  注释与函数注释；
- bookmark 统一为
  `V156 complete 2-slot particle Array add/erase member-hint and worker result family`；
- fresh entity readback 每库恰好两个 size=4 data item；fresh xref/decompile 回读显示 update 的
  result 参数为 null，而 step 的 result 参数指向 caller 局部对象；
- 在发现 owner-alias 中间结论错误后，四库 erase data comment、step function comment 与
  bookmark 已再次覆盖为“separate result Variant”，并重新保存；
- 二进制输入字节未修改。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- `Web Debug Build` 干净配置、完整编译与链接通过；`index.wasm` 为 85,648,312 bytes。
- `Wasmtime Headless Debug Build` 干净配置、完整编译与链接通过；`index.wasm` 为
  84,995,453 bytes。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；这里只报告 probe 编译通过，不
  虚报 runtime CTest 执行。
- `git diff --check` 在文档完成后执行；Git 若只报告工作区 LF→CRLF 提示，不视为内容错误。

## 下一纵切面

V157 应从两槽紧邻下一地址开始，fresh 审计 render-source resolver 的 member-hint、source
Variant/dispatch owner tree、失败/空值边界及其后 `blendMode` / `assignImages` 邻接槽。必须继续以
四端 consumer set 拆分 family，并先确认该 cache 是单一 resolver 私有还是跨 update/spawn
路径共享，再改 portable 代码。
