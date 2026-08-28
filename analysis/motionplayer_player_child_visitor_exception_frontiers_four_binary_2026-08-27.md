# Player 共享 child visitor 三组 capture/异常前沿（2026-08-27）

## 1. 结论

`setZFactor`、`getProcessedMeshVerticesNum` 和 `Player::contains` 共用
`visitChildPlayerDispatches_guess(const std::function<bool(Player*)>&)`。此前三个 coverage
row 的普通状态机、递归顺序、type-4 重复 index 0、owner 与浮点边界均已闭合，最后缺口
是 capture allocation 和异常穿越 visitor 时的精确清理。

本轮重新读取四端 12 个完整主函数、Android 六个 libstdc++ manager、iOS 六份 libc++
callable vtable、iOS armv7 三个完整 SjLj cleanup，并检查每个主函数 normal return 后的
全部 landing code。结果是共同 C++ 源形状一致，但 reference codegen 的 local EH landing
并不对称：

- Android arm64：三组都有显式 manager-destroy + `_Unwind_Resume`；
- iOS armv7：三组都有独立 21 条指令 SjLj cleanup，按 active pointer 选择 local/heap
  destroy slot 后 resume；
- iOS arm64：Mach-O LSDA 指向三个紧邻主 body、普通 code xref 为 0 的 cold cleanup，
  按 active pointer 选择 SBO local-destroy 或 heap-destroy slot 后 resume；
- Android armv7：完整函数和相邻 function catalog 没有 local cleanup landing。

allocation failure 都发生在 callable manager/active pointer 发布之前，不会销毁未构造
对象。Portable 源继续使用普通自动 `std::function`，不把编译器/异常模式差异硬编码成
平台分支。三个 coverage row 均可升级为 `IMPLEMENTED`。

## 2. 主函数与完整指令数

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| `Player_setZFactor_guess` | `0x6B1D6C`；56 | `0x5817B4`；48 | `0x100109198`；51 | `0x1069C4`；89 |
| `Player_getProcessedMeshVerticesNum_guess` | `0x6CE3F8`；50 | `0x594710`；41 | `0x10011FDA8`；41 | `0x11EA6C`；76 |
| `Player_contains_guess` | `0x6D071C`；113 | `0x595AF8`；108 | `0x1001218E8`；86 | `0x12065C`；123 |

全部 disassembly cursor 为 `done=true`。Android arm64 的函数尾包含 cleanup landing，
iOS arm64 的 cleanup 是主 body 之后由 Mach-O LSDA 选择的独立 cold function，iOS armv7
的 cleanup 是由 SjLj context 指向的独立函数。只有 Android armv7 的完整函数尾及相邻
function catalog只有 normal epilogue/stack-check，不存在本帧 local cleanup body。

## 3. capture 布局矩阵

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| zFactor capture | heap 8 B captured double；manager `0x6F0F84` | heap 8 B；manager `0x5AE598` | SBO `{vptr,double}`；vtable `0x1019ADBB8` | SBO；vtable `0x1777774` |
| processed count capture | heap 8 B pointer-to-result；manager `0x6F29EC` | heap 4 B pointer；manager `0x5AFA7C` | SBO `{vptr,pointer}`；vtable `0x1019ADC48` | SBO；vtable `0x17777BC` |
| contains capture | heap 24 B `{&x,&y,&found}`；manager `0x6F2AA4` | heap 12 B；manager `0x5AFAE6` | heap 32 B `{vptr,&x,&y,&found}`；vtable `0x1019ADC90` | SBO 16 B `{vptr,&x,&y,&found}`；vtable `0x17777E0` |

Android manager 的 mode 语义相同：

```text
mode 1: borrow/copy capture pointer
mode 2: allocate same-sized capture, copy scalar/pointers, publish destination
mode 3: if capture != null, operator delete(capture)
```

libc++ vtable 的 normal destroy slot同样统一：active pointer 等于 local buffer 时调用
`+32/+16` local destroy（这些 capture 都是 no-op），active pointer 指向 heap 时调用
`+40/+20` heap destroy（直接 `operator delete`）。

## 4. allocation-failure 提交前沿

### 4.1 setZFactor

顺序严格为：

```text
if current == requested: return
current = requested
syntheticRoot.delta.dirty = true
construct captured-double std::function
visit children
destroy callable normally
```

Android 的 `operator new(8)` 位于字段写入和 root dirty 之后。若 allocation failure：

- parent 的 zFactor 与 root dirty 已永久提交；
- callable manager 尚未写入，不能运行 mode-3 destroy；
- 没有 child 被访问。

iOS 两端使用 SBO，因此这一步没有 capture heap allocation；visitor 内部后续抛出仍保留
parent 与已访问 child 的 partial commit。

### 4.2 processedMeshVerticesNum

先把本地 uint32 counter 复制到栈，再构造只捕获其地址的 callable。Android heap
allocation failure 发生在 visitor 前；counter 没有发布到脚本结果，函数直接异常退出。
iOS 使用 SBO。无论哪端，已经完成的递归 child 调用都按 uint32 wrap 写回同一栈 counter，
但异常退出不返回 partial counter。

### 4.3 Player::contains

capture 只在完整 local shape scan miss 后构造。Android 和 iOS arm64 的 allocation
failure 均发生在 `found=false` 之后、任何 child 访问之前；没有对 Player/child 的写入。
iOS armv7 使用 SBO。local scan hit 在 capture 构造前直接返回 true，因而完全避开
allocation/visitor/EH 路径。

## 5. visitor 抛出时的 reference cleanup

### 5.1 Android arm64

三个 landing 分别从 `0x6B1E20`、`0x6CE494`、`0x6D08B8` 开始。共同逻辑：

```text
save exception
if manager != null:
    manager(functionStorage, functionStorage, mode=3)
_Unwind_Resume(exception)
```

manager mode 3 删除 heap capture，随后继续传播；parent/此前 child 的状态不回滚。

### 5.2 iOS armv7

SjLj cleanup 分别为：

- `Player_setZFactor_sjlj_cleanup_guess@0x106ACC`；21 条；
- `Player_processedMesh_sjlj_cleanup_guess@0x11EB40`；21 条；
- `Player_contains_sjlj_cleanup_guess@0x1207BC`；21 条。

三者都只接受 call-site 0；其它非零/越界 state 进入 abort/UDF 边界。call-site 0 按
`active == localBuffer` 选择 vtable local destroy，非 null heap pointer 选择 heap destroy，
null 则跳过，然后把 call-site 写为 `-1` 并 `__Unwind_SjLj_Resume`。当前三个 capture 都
实际使用 local buffer，所以 destroy 是 no-op，但控制流仍完整存在。

### 5.3 iOS arm64 Mach-O LSDA cold cleanup

三个主函数的 normal body 分别结束在 `0x100109268`、`0x10011FE50`、`0x100121A44`；
这三个地址本身正是相邻 cold cleanup 的入口，而不是“整个函数族结束”：

- `Player_setZFactor_unwind_cleanup_guess@0x100109268`；14 条；
- `Player_processedMesh_unwind_cleanup_guess@0x10011FE50`；14 条；
- `Player_contains_unwind_cleanup_guess@0x100121A44`；13 条。

前两个把 active pointer 与栈内 SBO buffer 比较；相等时调用 vtable local-destroy slot
`+32`，非 null heap pointer调用 heap-destroy slot `+40`，null 跳过。`contains` 使用同一
选择逻辑，但其 active pointer 实际指向 heap callable，因此异常路径会调用 heap destroy。
三者最后都 `_Unwind_Resume`。

这些 cold functions 的普通 `xrefs_to` 为 0，因为入口由 Mach-O LSDA 选择；与主 body
相邻、复用该栈帧 active/local slot 并调用精确 vtable destroy slot 才是其可达证据。

### 5.4 Android armv7

三个完整主函数分别到 `0x581830`、`0x59476C`、`0x595C1A` 结束。继续检查相邻 function
catalog 后仍只有 normal epilogue/stack-check，没有 landing body、SjLj context 或独立
cleanup function。因此只有该目标对异常穿越本帧没有可观察的主动 capture cleanup：
heap capture 可能保持未释放，或由该目标的异常/终止模式结束进程。这里仍不把“无 local
landing”夸大成所有外部异常一定可恢复；它是目标 codegen boundary。

## 6. 为什么 portable 源不增加平台分支

四端主 body、capture 字段、manager/vtable 和 normal destroy 全都对应同一普通 C++：

```text
std::function<bool(Player*)> visitor = lambda capture
visitChildPlayerDispatches(visitor)
```

是否采用 SBO、EHABI/SjLj/DWARF landing、以及优化器是否为本帧生成 cleanup 是编译器、
STL 与异常配置的结果。恢复目标要求源结构优先于 ABI padding/codegen；因此本地自动
`std::function` 是正确共同源形状。显式 `try/catch`、按平台 `release()` 或故意泄漏会
制造四端都不存在的源码分支。

## 7. 本地逐行对照

- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:629-639`：zFactor 本地提交、root dirty、
  captured-double recursive visitor；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:642-648`：local uint32 seed、by-reference
  capture 与 wrap accumulation；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:651-680`：共享 child visitor、type-4
  index-0 重复、type-3 direct native、live end 与 false short-circuit；
- `cpp/plugins/motionplayer/PlayerLayerQuery.cpp:777-795`：local-first contains、root
  exclusion、captured `{x,y,found}` 和 first-child-hit short-circuit；
- `cpp/plugins/motionplayer/PlayerCore.cpp:38-106`：同 coverage row 的 colorWeight 与
  independentLayerInherit leaf bodies。

本轮不需要 C++ 修改。共同自动对象结构已准确；现有 tests 已覆盖 normal/NaN/-0/
particle-index-zero/local-first/malformed-short-circuit，正式构建仍受当前工具链缺失限制。

## 8. disposition

`MP-D11-PLAYER-COLOR-INDEPENDENT-Z`、`MP-D11-PLAYER-PROCESSED-MESH` 和
`MP-D11-PLAYER-CONTAINS` 的 capture allocation、normal destroy、visitor throw、partial
commit 与 target EH/codegen 差异已闭合，均升级为 `IMPLEMENTED`。
