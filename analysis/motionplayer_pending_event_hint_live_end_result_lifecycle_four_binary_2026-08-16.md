# MotionPlayer pending-event 双槽 hint、live-end 与 result 生命周期四参考复原（2026-08-16）

## 范围与结论

本纵切面从 V157 `assignImagesMemberHint_guess` 的紧邻下一地址开始，对四份
`reference/binaries/` fresh 审计 `Player_dispatchPendingEvents_guess`。四端共同闭合出连续的
2×4-byte process-wide member-hint family：

```text
onSync / onAction
```

并确认完整 dispatch/lifetime 行为：

- 空 vector 在 AddRef、callback-result 构造和任何 dispatch 前立即返回；
- 非空 vector 对传入 raw dispatch 只 AddRef 一次并保活整个 traversal；null dispatch 会在这里
  直接触发 native crash boundary，不会静默丢弃事件；
- traversal 持有一个默认构造的 callback-result `Variant`，所有 `onSync` / `onAction` 调用
  复用同一个非 null result pointer；回调写入值会成为下一调用看到的旧 result 内容；
- raw element cursor 每次固定前进一步，但 loop condition 每轮重新加载 vector 的 live end；
- `onAction` 为 type 0，按 param1→param2 copy-construct 两个参数并在调用后逆序析构；
- `onSync` 为 type 1，argc=0 且 params=null；未知 type 只跳过；
- 普通 HRESULT 被忽略，vector 不会被消费/清空；
- result 在 retained dispatch Release 之前析构；异常沿 C++ unwind 清理当前参数、result 和 receiver。

portable 代码原已恢复 retained receiver、单 result、live-end 和不消费行为，但两次 FuncCall 仍传
null named hint。四端都明确传双槽地址，本轮补齐两个 globals 和 call-site 参数，并把现有重入
探针收紧到准确 ABI。恢复名来自 stripped binary，保留 `_guess`；绝对地址只保留在本文和
recovery IDB。

## UTF-16LE 名称复核

Android arm64 的 `onAction` 和另外三端的 `onSync` 在 Hex-Rays 中被错误显示成单字节
`"o"`。本轮按 `ida-search-string` 工作流搜索带 16-bit NUL terminator 的 UTF-16LE bytes：

| literal | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `onSync` | `0x14D6144` | `0xD85B06` | `0x10195C6E2` | `0x174EA46` |
| `onAction` | `0x14D6152` | `0xD85B14` | `0x10195C6F0` | `0x174EA54` |

两个 pattern 在每库都恰好唯一命中一次，且 literal 地址与相应 call 的 string xref 一致。由此
确定 slot 名，不能采用 decompiler 的窄字预览。

## 双槽映射与邻接边界

| idx | recovered symbol / member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 0 | `onSyncMemberHint_guess` (`onSync`) | `0x1AB5450` | `0x11118EC` | `0x101B69918` | `0x187D5BC` |
| 1 | `onActionMemberHint_guess` (`onAction`) | `0x1AB5454` | `0x11118F0` | `0x101B6991C` | `0x187D5C0` |

两槽满足 `onAction == onSync + 4`，各自只有 pending-event dispatcher 一个真实 consumer。
紧邻下一地址已经是旧全局审计确认的共享 `meshCopyMemberHint_guess`：

| target | next `meshCopy` slot |
|---|---:|
| Android arm64 | `0x1AB5458` |
| Android armv7 | `0x11118F4` |
| iOS arm64 | `0x101B69920` |
| iOS armv7 | `0x187D5C4` |

因此本轮 family 在 8 bytes 后闭合，不能把随后的 render helper caches 并入事件组。

## 函数、call 与 owner 生命周期映射

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_dispatchPendingEvents_guess` | `0x6C1870` | `0x58C3A8` | `0x10011622C` | `0x113B64` |
| receiver AddRef | `0x6C18C0` | `0x58C3CE` | `0x100116280` | `0x113BDC` |
| result default/Void init | `0x6C18C4` | `0x58C3D4` | `0x100116284` | `0x113BE0` |
| `onSync` FuncCall | `0x6C1938` | `0x58C40C` | `0x1001162F8` | `0x113C26` |
| `onAction` FuncCall | `0x6C1984` | `0x58C446` | `0x100116344` | `0x113C7E` |
| result dtor | `0x6C19AC` | `0x58C462` | `0x10011636C` | `0x113C9C` |
| receiver Release | `0x6C19C0` | `0x58C46E` | `0x100116380` | `0x113CB0` |

vector 为空时控制流完全绕过表中所有 owner 操作。非空时 AddRef 先于 result 初始化；正常尾部
result dtor 又先于 Release。这种嵌套关系允许回调重入替换/销毁 Player 当前 dispatch bridge，
当前 traversal 仍由自己的一次 AddRef 保活旧 receiver。

## 两类事件的精确 ABI

| property | type 1 `onSync` | type 0 `onAction` |
|---|---|---|
| flags | 0 | 0 |
| member-hint | slot 0 `onSync` | slot 1 `onAction` |
| result | 同一非 null callback-result | 同一对象 |
| argc | 0 | 2 |
| params | null | `{copy(param1), copy(param2)}` |
| receiver / objthis | retained dispatch / same | same |
| ordinary HRESULT | ignored | ignored |

四端 onAction 参数构造和逆序析构点：

| target | param1 copy | param2 copy | param2 dtor | param1 dtor |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6C1948` | `0x6C1954` | `0x6C198C` | `0x6C1994` |
| Android armv7 | `0x58C416` | `0x58C420` | `0x58C44A` | `0x58C450` |
| iOS arm64 | `0x100116308` | `0x100116314` | `0x10011634C` | `0x100116354` |
| iOS armv7 | `0x113C32` | `0x113C42` | `0x113C82` | `0x113C88` |

参数是拥有引用的独立 Variant，不是 event record 内部字段地址。脚本回调可以在运行中修改或
清空 event vector；当前调用的参数仍保有原值直到逆序析构。ordinary failure 不跳出 loop；抛
异常时 active params 先 unwind，再销毁 shared result，最后 Release receiver。

## raw cursor、live end 与重入边界

四端循环都保存一次 raw `event` pointer，并在每轮结束按原生 element stride 增加；condition
不是冻结初始 count，而是重新读取 vector.end。可观察后果：

1. callback `push_back` 且 capacity 足够、不发生 reallocation 时，新 end 会被下一轮看到，
   新追加事件在同一 traversal 内继续 dispatch；
2. callback 触发 reallocation、erase 或 clear 时，旧 raw cursor 被标准 C++ 规则失效；原生没有
   修复/重取 begin，因此后续行为是 trusted-container UB boundary；
3. callback 持续追加且不 reallocate 可以使 traversal 长期追随增长的 live end，原生没有次数
   限制；
4. 未知 event type 既不 dispatch 也不报错，但 raw cursor仍前进；
5. dispatcher 不 erase、不 clear，正常返回后同一 vector 再次调用会重新从 begin 派发全部记录。

portable 的 `event = data()` + 每轮比较 `data() + size()` 保留这一形状；不得改成 range-for、
冻结 size 或先复制 vector，因为三者都会改变重入边界。

callback-result 只默认构造一次；每次 FuncCall 都能覆盖它，下一次回调会接收同一地址和上一
次留下的 Variant 内容。native 本身从不转换、读取或清空 result。这里只把它当回调 ABI 所需
owner，最终在 receiver Release 前析构。

## portable 源码与回归探针

- `MotionDispatch.h` / `RuntimeSupport.cpp`：在 V157 三槽后按原生顺序增加
  `onSyncMemberHint_guess`、`onActionMemberHint_guess` 两个零初始化 `tjs_uint32`；
- `PlayerFrameProgress.cpp`：将两个原有 null hint 替换为准确 process-wide slot 地址；其余
  retained receiver、single result、raw cursor/live end 和 cleanup 代码保持不变；
- 收紧 `pending event dispatch reloads live end and does not consume` 探针：
  - 对 type 0 断言 member=`onAction`、flags=0、slot1、同一 non-null result、两个参数值/
    顺序、params非null、receiver==objthis；
  - 对 callback追加的 type 1 断言 member=`onSync`、flags=0、slot0、同一 result、argc0/
    params=null、receiver==objthis；
  - recorder 返回 `TJS_E_FAIL`，仍必须访问新追加事件，锁定 ordinary HRESULT ignore；
  - 再次 dispatch 同一 vector 仍得到 action→sync，锁定“不消费”；
  - 双槽互不 alias，也不与 V157 `assignImages` alias。

## IDB 回写

四份 recovery IDB 均已完成并保存：

- 对连续 8-byte range 整体 `undefine`；
- 建立 `onSyncMemberHint_guess` / `onActionMemberHint_guess` 两个独立
  `unsigned int` data item，每项 size=4；
- 在双槽、两个 FuncCall head 和 dispatcher function 写入准确 ABI、live-end、owner cleanup
  注释；
- bookmark 统一为
  `V158 complete 2-slot pending-event onSync/onAction member-hint and live-end dispatch family`；
- force-recompile dispatcher 后 fresh decompile 在四端都直接显示双槽语义名；即使个别 literal
  预览仍为 `"o"`，hint 名、UTF-16 pattern 和 call comment保持准确；
- fresh entity readback 每库恰好两个连续 size=4 事件槽；四份 IDB 原位保存，二进制输入字节
  未修改。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- 为规避当前 CMake build-preset 重解析 `$env{EMSDK}` 的 Windows 环境缺陷，两树均用官方
  configure preset + 显式 Emscripten toolchain absolute path 干净配置，再按 binary directory
  完整构建；两次均通过。
- Web `index.wasm` 为 85,648,454 bytes；Headless 为 84,995,595 bytes。相对 V157 两端均增加
  142 bytes，符合两处 non-null hint address materialization 与新增零初始化槽。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；不虚报 runtime CTest 执行。
- `git diff --check` 在本文完成后执行；工作区 LF→CRLF 提示不属于内容错误。

## 下一纵切面

V159 应从双槽紧邻的 `meshCopyMemberHint_guess` 开始，对既有 render helper 全局审计做当前四端
fresh 闭合：至少确认 `meshCopy / bezierPatchCopy / affineCopy` 三槽与后续 Layer properties 的
真实 family 边界、所有 consumer set，以及旧文档中 iOS data item 仍为 size=1/聚合边界的
过时 IDB 状态。只有 fresh 四端结果一致后，才调整 portable global 排列或补 call-site hints。
