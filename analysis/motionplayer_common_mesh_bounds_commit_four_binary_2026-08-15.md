# MotionPlayer common-mesh bounds commit 时序与失败原值四端复原（2026-08-15）

## 1. 结论

common-mesh 的 `tTVPRect &computedBounds` 同时是输入 clip 与成功 output，但四参考不在
完成几何筛选后立即写回。精确成功尾部顺序是：

```text
invoke submit callback
Release current source texture
write bounds.left/top/right/bottom
destroy temporary vertex vectors
return true
```

因此以下路径都保留 caller 传入的四个 rect word：

- clip 本身为空；
- bounds/control-point 快路径完全不相交；
- 逐 cell 过滤后 `selectedCells` 为空；
- callback 抛异常；
- callback 正常返回之前发生的任何异常。

portable 实现此前在 `selectedCells.empty()` 检查和 callback 之前就写回 bounds，导致空
selection 返回 false 仍改写 caller rect，也让 callback 在同一调用栈中看到过早提交的
output。本轮把写回移到 callback 与 source Release 之后。

## 2. 四端成功尾部

| 目标 | callback 起点 | source Release | bounds 四字写回 | return-true commit |
| --- | ---: | ---: | ---: | ---: |
| Android ARM64 | `0x69C5FC`（实际 `BLR 0x69C618`） | `0x69C61C`–`0x69C628` | `0x69C62C`–`0x69C64C` | `0x69C668` |
| Android ARMv7 | `0x575E5C`（实际 call `0x575E68`） | `0x575E74` | `0x575E78`–`0x575E7C` | `0x575E90` |
| iOS ARM64 | `0x1000FA1D8` | `0x1000FA1E8` | `0x1000FA1F0`–`0x1000FA1F4` | `0x1000FA258` |
| iOS ARMv7 | `0xF7238` | `0xF7246` | `0xF724C`–`0xF7258` | `0xF729E` |

A32、i64、i32 decompiler 在这一尾部都直接形成相同的 C++ 顺序。A64 用逐指令核对：
`BLR` callback 后才从 current source vtable 取 Release slot，Release 返回后才连续把
四个 `W` 值写入 caller rect。

## 3. empty selectedCells

| 目标 | empty 检查 | source Release / false 分支 |
| --- | ---: | ---: |
| Android ARM64 | `0x69BA10`（size `SUBS`） | `0x69CA90`–`0x69CAA0` |
| Android ARMv7 | `0x575E12` | `0x575E9A`–`0x575E9C` |
| iOS ARM64 | `0x1000F9E08` | `0x1000FA26C`–`0x1000FA270` |
| iOS ARMv7 | `0xF6F38` | `0xF72AC`–`0xF72AE` |

这些分支都在 vertex-vector reserve/expand、callback 和 bounds store 之前离开。它们只
释放 current source、清理已经构造的临时容器并返回 false，不写 caller rect。

空 input clip 的快速失败也采用同一原则。它不是“先算 output bounds，再以 bool 表示
是否提交”；bool=false 意味着 bounds output 没有 commit。

## 4. callback 异常边界

四端 bounds store 都位于间接 callback 调用之后，故 callback 抛出时这些 store 不可达。
临时 vector 和 source 的 owning cleanup 由 unwind 路径执行，但 caller rect 仍是原值。
这也是写回不能提前到 callback 之前的原因：即使调用方通常不在 callback 中读取同一个
rect，异常后的外层 catch 仍能观察四个 word 是否被改写。

callback 若主动改写 caller rect，成功返回后 native 的四字 store 会覆盖它；callback
抛出则不会覆盖。portable 的提交点现与这个顺序一致。

## 5. 测试与 IDB 写回

common-mesh 单 cell unit case 现同时验证：

- callback 内看到的 rect 仍是原 input clip；
- callback 正常返回、source 临时引用释放后，caller rect 才变成 mesh bounds；
- clip 与 cell 不相交导致 empty selection 时 callback 为零次且 rect 原值不变；
- callback 抛异常时 source 引用恢复平衡且 rect 原值不变。

四份 recovery IDB 均在主入口、empty-selection 分支和成功 tail 写入统一注释/bookmark，
并原位保存。真实 `motionplayer_test_args.rsp` 的 Emscripten `-fsyntax-only` 通过（仅
既有 `_tss` warning）；`cmake --build out/web/debug -j 8` 重新编译
`MotionRenderBackend.cpp`，成功归档 `libmotionplayer.a` 并链接
`index.html/index.wasm`（仅既有 Emscripten warning）。
