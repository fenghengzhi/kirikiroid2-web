# MotionPlayer render-source `key` 共享 hint 读写 family 四参考复原

日期：2026-08-17

## 1. 结论

四份当前参考二进制共同证明，render-source descriptor / serialized command 的 `key` member
由一个 process-wide mutable `tjs_uint32` hint word 支撑。该 exact pointer 同时服务六个
consumer functions：

1. `SourceCache_loadSource_guess`：唯一的 flags 0、非空 Variant result 读取；
2. `Player_buildRenderCommands_guess`：normal leaf descriptor 发布；
3. `Player_renderToCanvas_guess`：ordinary canvas submit descriptor 发布；
4. `Player_renderAccurateSeparateLayerAdaptor_guess`：accurate SLA descriptor 发布；
5. `Player_getCommandList_guess`：command Dictionary serialization；
6. `Player_buildPrivateMotionGLLCommands_guess`：private-GLL descriptor 发布。

后五类全部是 `TJS_MEMBERENSURE` PropSet。receiver 与 `objthis` 是同一个 descriptor/Dictionary
dispatch；普通返回状态不阻止后续 `src`、`blendMode`、color 或其他 command fields 继续发布。
唯一的 reader 也忽略普通 PropGet HRESULT：dispatch 写出的 Variant 会先 copy-assign 到
`SourceCache::Entry::key`，再析构读取临时量，即使 HRESULT 为负也不会由 accessor 擅自清空。

本地生产源码的六处 wiring 已经全部指向同一个
`detail::commandKeyMemberHint_guess`，所以本轮不改生产控制流；新增的是完整四参考 provenance、
IDB data boundary/consumer topology、源码注释和锁定读写 ABI/failure-through 的回归。

该槽在四份参考中都物理紧邻上一轮恢复的 `primaryLayerMemberHint_guess`，但 consumer 集完全
不同。邻接只帮助发现候选，不能把 `key` 误归为 primary accessor family，也不能据此推断
source-level 声明相邻或共享语义。

## 2. process-global 映射

| 目标 | `primaryLayerMemberHint_guess` | `commandKeyMemberHint_guess` | 间距 | 初始值 |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `0x1AB52D4` | `0x1AB52D8` | 4 B | 0 |
| Android armeabi-v7a | `0x11117E0` | `0x11117E4` | 4 B | 0 |
| iOS arm64 | `0x101B6979C` | `0x101B697A0` | 4 B | 0 |
| iOS armv7 | `0x187D4A4` | `0x187D4A8` | 4 B | 0 |

四端均为独立 size-4 data item。重新建 item 后 fresh decompile 能直接显示
`&commandKeyMemberHint_guess`，而不再把它渲染为相邻已命名 global 加偏移或匿名 BSS word。

### 2026-08-17 V187 后继 candidate 裁决

继续检查 `commandKey + 4` 后，LP64 两端和 iOS armv7 的该 word 均为无 xref 的 ABI
alignment gap；Android armv7 无此 gap。下一处共同对象不是 hint，而是 process-global PSB
OwnerFilter `std::function`，四端大小为 32/16/32/20 B，随后才是
`g_randomMemberHint_guess`。因此本报告恢复的 key data item 严格止于 4 B，既不能向后扩成
数组，也不能从物理后继推导另一个 member cache。详见
`motionplayer_psb_owner_filter_std_function_layout_static_lifetime_four_binary_2026-08-17.md`。

## 3. data xref 拓扑

| 目标 | raw data xrefs | 归一后的 consumer functions |
|---|---:|---:|
| Android arm64-v8a | 14 | 6 |
| Android armeabi-v7a | 21 | 6 |
| iOS arm64 | 6 | 6 |
| iOS armv7 | 17 | 6 |

iOS arm64 每个 consumer 恰有一个 `ADRL` data xref，最接近语义调用数。其他三端的 raw xref
更多，原因包括：

- AArch64 `ADRP + ADD` 两条地址物化；
- Thumb literal-pool、`MOVW + MOVT + ADD PC` 三条地址物化；
- SjLj/unwind chunk 对 literal 或 data address 的附加引用；
- `getCommandList` 两个 pointer-vector serialization pass 被编译器复制、合并或把同一地址
  hoist 到更大作用域；
- Android arm64 recovery 库仍把 `getCommandList` 大 body chunk 错归给 8-byte
  `EmotePlayer_getCommandList_guess` thunk。

因此必须按 function、member literal、flags、value 和 call ABI 归一，不能把 raw xref 数直接
解释为每帧调用次数。

Android arm64 还有一个既有 function-boundary 恢复缺口：`SourceCache_loadSource` 的相关 body
被包含在当前名为 `SourceCache_ctor_guess @ 0x6A4CD4` 的大 function item 中。本轮使用真实
`key` PropGet head 与数据流描述 consumer，不把错误 function owner 当成源码身份。

## 4. 六个 consumer 的代表性调用地址

| consumer | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| SourceCache key read | `0x6A5020` | `0x57AD12` | `0x100100A24` | `0xFDBE8` |
| normal builder publication | `0x6C2810` | `0x58CB30` | `0x100116BD0` | `0x114702` |
| canvas publication | `0x6C4AEC` | `0x58E53A` | `0x100118944` | `0x116CE0` |
| accurate-SLA publication | `0x6C75EC` | `0x5909D4` | `0x10011AE70` | `0x11935E` |
| getCommandList publication | `0x6D0FAC` | `0x596106` | `0x100121FC0` | `0x120E44` |
| private-GLL publication | `0x6DBCC4` | `0x59CC3A` | `0x10012BBF0` | `0x12A81C` |

Android arm64 getCommandList 一项是错误 chunk owner 下的真实 body head；其 source-level
function 起点在既有报告中映射为 `0x6D0E2C`。表格坚持记录调用指令本身，不使用错误的
8-byte thunk 地址替代。

## 5. reader ABI、结果生命周期与失败边界

四端共同源结构等价于：

```cpp
ncbPropAccessor descriptorAccessor(descriptor);
Entry entry;

entry.key = descriptorAccessor.GetValue(
    TJS_W("key"),
    ncbTypedefs::Tag<tTJSVariant>(),
    0,
    &commandKeyMemberHint_guess);
```

下层 ABI 为：

```text
operation  = PropGet
flags      = 0
member     = "key"
hint       = &commandKeyMemberHint_guess
result     = non-null Void tTJSVariant temporary
receiver   = descriptor dispatch
objthis    = same descriptor dispatch
HRESULT    = ignored by the typed accessor
```

`Entry` 的 key Variant 类型槽在调用前已经初始化为 Void。getter helper 使用自己的 result
临时量；调用返回后先把这个临时量 copy-assign 到 `entry.key`，再析构临时量。因此：

- dispatch 返回失败且未写 result：Void 被赋给 entry key；
- dispatch 返回失败但写出值：写出的值仍进入 entry key；
- conversion/copy/assignment 抛出：沿 Variant 原生异常路径退出，没有 HRESULT-friendly
  fallback；
- descriptor accessor 的 receiver 保活覆盖该读取，entry 不保留 descriptor/source dispatch。

随后 `src` 和 `blendMode` 使用各自的 defaulted typed getter 边界；它们不共享该 `key` hint。

## 6. 五类 publisher ABI

五个 publisher functions 的共同调用形状是：

```cpp
ncbPropAccessor descriptor(...); // getCommandList 使用 Dictionary accessor
descriptor.SetValue(
    TJS_W("key"), item.commandKey,
    TJS_MEMBERENSURE,
    &commandKeyMemberHint_guess);
```

下层 ABI 为：

```text
operation  = PropSet
flags      = TJS_MEMBERENSURE (0x200)
member     = "key"
hint       = &commandKeyMemberHint_guess
value      = borrowed address of current item's command-key Variant
receiver   = descriptor/Dictionary dispatch
objthis    = same dispatch
status     = ordinary return is not used to gate subsequent fields
```

normal builder、canvas、accurate 和 private-GLL 随后都继续写 `src`、`blendMode` 及 color
descriptor。getCommandList 随后继续写 `id`、`src`、`coordinate`、`opacity`、`blendMode` 以及
mesh/clip/bezier arrays。每个后继 member 使用自己的 exact hint；相同 descriptor receiver
不意味着这些 slots 可以合并。

getCommandList 的 source lambda 被 main/aux command ranges 复用。不同编译器可以内联复制两个
loop body、在支配块中 hoist literal/hint 地址，或合并后端 call block；这解释了该 function
内部 xref 数的差异，但不改变所有 command Dictionary key publications 使用同一 exact word。

## 7. UTF 编码搜索与 literal 身份

对精确带终止符的 `key` 执行 ASCII、UTF-16LE、UTF-32LE byte search，并对普通 string
search 翻页到结束：

| 目标 | ordinary substring hits | ASCII `key\0` | UTF-16LE `key\0` | UTF-32LE `key\0` | motionplayer literal |
|---|---:|---:|---:|---:|---:|
| Android arm64-v8a | 63 | 5 | 1 | 0 | `0x14D5832` |
| Android armeabi-v7a | 73 | 8 | 2 | 0 | `0xD8538A` |
| iOS arm64 | 231 | 25 | 4 | 0 | `0x10195BC7C` |
| iOS armv7 | 231 | 25 | 4 | 0 | `0x174DFE0` |

ordinary/ASCII hits 包含大量 C/C++ runtime、NSDictionary/JSON、调试文本和其他库 key 子串；
UTF-16 hits 也可能包含非 motionplayer 同名字面量。真正的 plugin literal 由“直接 xref 到上述
六个 consumer，同时在调用点与 exact hint pointer、flags 和 value 成对出现”确定，而不是
只凭文本内容猜测。

ARM/AArch64 某些 listing 只把宽字面量渲染成 `"k"`；raw UTF-16 bytes 和另外两端的
`L"key"` decompile 显示证明它是完整 member name。

## 8. 与相邻/同文本 hint 的边界

四端都满足以下 pointer-identity 关系：

```text
&commandKeyMemberHint_guess != &primaryLayerMemberHint_guess
&commandKeyMemberHint_guess != &srcMemberHint_guess
&commandKeyMemberHint_guess != &commandIdMemberHint_guess
```

- `primaryLayer` 物理在前一 word，但只由 SourceCache constructor 和 command-builder 的
  primary accessor 链消费；
- `src` 紧随 key publication/read，但它有自己的跨消费者 global；
- `id` 与 key 一起进入 serialized command Dictionary，却复用另一个已闭合的 parser/
  serializer shared word；
- 其他模块中也存在文本为 `key` 的 property，但同文本不能建立 pointer identity。

member hint 是 dispatch ABI 的 mutable cache pointer；把多个同文本或相邻 member 错并成一个
word，会让脚本 dispatch 看到不同地址并改变缓存/重入行为。

## 9. 源码与回归

生产源码核对得到六处有效 wiring：

- `SourceCache.cpp`：一处 loadSource key GetValue、一处 private-GLL/common render descriptor
  SetValue；
- `PlayerRenderExecute.cpp`：normal leaf 与 ordinary/accurate submit 共两处 SetValue；
- `PlayerLayerQuery.cpp`：getCommandList command lambda 一处 SetValue；
- 这些 source sites 经内联/调用关系覆盖四参考中的五个 publisher functions。

因此没有新增或删除生产 dispatch。`MotionDispatch.h` 与 `RuntimeSupport.cpp` 只补充“1 reader +
5 publisher families 共用 process-global pointer”的 provenance 注释。

`motionplayer-dll.cpp` 新增回归：

- reader 锁定 flags 0、member、exact hint、non-null result 与 receiver==objthis；
- recorder 返回 `TJS_E_FAIL` 但先写整数，测试确认 typed getter 仍返回写出的值；
- publisher 锁定 `TJS_MEMBERENSURE`、exact shared hint、String Variant value 与
  receiver==objthis；
- recorder 返回 `TJS_E_FAIL`，SetValue 报 false但全部参数仍完整可见，对应生产 caller
  不用该 ordinary status 截断后续 field publication；
- 明确断言 key hint 与 primary/src/id 三个 words 地址不同。

## 10. Recovery IDB 回写

四份 recovery IDB 均完成：

- 在匿名 0-init BSS word 上建立 size-4 `unsigned int` data item 并命名
  `commandKeyMemberHint_guess`，共 4 个 data items；
- 每库写入 data、motionplayer UTF-16 literal 和六个 consumer call site 共 8 条注释，
  合计 32 条；
- 每库添加 1 个 V186 bookmark，共 4 个；
- 每库对六个 consumer 强制 recompile，共 24 次，全部成功；
- fresh decompile/readback 显示 reader 与 getCommandList publisher 已引用新语义名；其余大函数
  通过 call-site disassembly 回读 exact symbol/flags/value；
- 四份 recovery IDB 均原位保存成功。

Android arm64 的两个既有 chunk-owner 缺口只在注释中明确记录，本轮没有为追求显示整齐而
重划未经完整 CFG/exception-table 审计的大函数边界。

## 11. 验证与产物

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` motionplayer 单测 TU syntax-only 均通过；只有
  仓库既有 `_tss` deprecated warning；
- Web Debug 与 Wasmtime Headless Debug 完整构建、静态库和最终 Wasm 链接均成功；
- Node `WebAssembly.Module` parse 与 `llvm-objdump -h` 全 section 读取通过；
- Web/Headless CTest 均成功返回，但当前两个配置都没有注册运行时测试；
- `git diff --check` 成功，输出只有工作树既有 LF/CRLF 提示。

V186 产物与 V185 完全相同：

| 配置 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | 85,647,577 B | 539 / 69 | `0x1BD24` | `0xD5B2` | `0x1A407D5` | `0x5A3F37` | `0x3184928` |
| Wasmtime Headless Debug | 84,994,718 B | 538 / 69 | `0x1BA43` | `0xD5DA` | `0x19E8783` | `0x5A1187` | `0x31407BE` |

零产物 delta 符合“生产 wiring 已正确，本轮只补 compiled-away comments 与 unit-test-only
recorder/case”的修改范围。它用于确认没有意外漂移；共享 hint identity 和失败边界仍以 fresh
四端 xref/decompile/disassembly/byte search 为语义依据。
