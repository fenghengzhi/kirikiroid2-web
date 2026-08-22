# SeparateLayerAdaptor shared `hitThreshold` member-hint 边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致表明，`SeparateLayerAdaptor` 的 payload-bearing 与 payload-free 两条
Layer resolve 路径向 target Layer 发布 `hitThreshold = 256` 时，复用同一个 4-byte、进程级
可变 member-hint word。它与 V180 已闭合的 `absolute` hint 地址不同，且 assign 的
source-relative rebase 尾部没有第三次 `hitThreshold` publication。

这两个 consumer 的共同 ABI 为：

- `PropSet` flags 为 `TJS_MEMBERENSURE`（512）；
- member 为 UTF-16 `hitThreshold`；
- value 为临时 Integer `tTJSVariant(256)`；
- hint 为同一 `hitThreshold` word 的实际地址；
- receiver 与 `objthis` 都是 resolved target Layer dispatch；
- HRESULT 被忽略，之后按原控制流继续清理或返回。

四端中该 word 到 `absolute` word 的链接距离并不相同，因此本轮恢复的是共享 address
identity 与 consumer 边界，不声称二者构成一个连续 C++ array/struct。原符号已 strip，
名称继续保留 `_guess`。

## 精确数据与函数映射

| 目标 | `absolute` hint | `hitThreshold` hint | byte delta | payload resolve | payload-free resolve |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x1AB5540` | `0x1AB5574` | `0x34` | `0x6C3F28` | `0x6C90C4` |
| Android armv7 | `0x11119C0` | `0x11119E4` | `0x24` | `0x58DCD4` | `0x591DEC` |
| iOS arm64 | `0x101B69A00` | `0x101B69A14` | `0x14` | `0x100117E88` | `0x10011C628` |
| iOS armv7 | `0x187D690` | `0x187D6A4` | `0x14` | `0x115B34` | `0x11AE24` |

四个 `hitThreshold` 槽均为独立 size-4 `unsigned int` data item。raw data xref 数分别为
Android arm64 4、Android armv7 6、iOS arm64 2、iOS armv7 5；按函数边界与 dispatch call
折叠后，四端都严格是两个语义 consumer。Android armv7 的两处 `fn=null` 与 iOS armv7 的
额外 materialization 均落在同一函数生成的 literal-pool/cleanup 范围，不能虚构第三个
caller。

A64 的真实地址是 `0x1AB5574`。按 iOS 的 `absolute + 0x14` 机械外推会得到无 xref 的
`0x1AB5554`；fresh xrefs 和两函数反编译均否定该猜测。A32 的间距同样不同。这是本纵切面
不依赖单端 adjacency 或旧注释推断地址的直接例子。

## 字符串搜索与 literal 归属

本轮按 `ida-search-string` 流程对四库分别执行普通 string lookup，并对 ASCII、UTF-16LE、
UTF-32LE 三种编码 raw bytes 分页搜索到 cursor 结束：

- 四库的普通 `find(type=string, "hitThreshold")` 都为空；
- ASCII raw bytes 四库均无命中；
- UTF-32LE 四库均无命中；
- UTF-16LE 命中如下。

| 目标 | UTF-16LE raw matches | SLA 实际使用的 literal |
|---|---|---:|
| Android arm64 | `0x14D6ADE` | `0x14D6ADE` |
| Android armv7 | `0xD862DC` | `0xD862DC` |
| iOS arm64 | `0x101957C4A`, `0x10195D4A4` | `0x10195D4A4` |
| iOS armv7 | `0x1749FAE`, `0x174F808` | `0x174F808` |

每个 match 都以 `get_bytes` 检查了前后 UTF-16 code-unit 与终止零。iOS 的前一份 literal
分别只归属于 `sub_1000858A8` / `sub_83B44`，不是 SLA resolve；后一份才由两个 SLA
consumer 引用。Android 只有一份 raw literal，但它还有 SLA 之外的大 registrar/模块函数
xref；字符串共用不会扩大 hint data consumer 集合，因为那些额外函数从不取
`hitThresholdMemberHint_guess` 的地址。

普通 string-list 搜索为空而 raw UTF-16 查找稳定命中，也解释了旧分析中只依赖 IDA string
列表时容易把该属性误判为无引用的原因。A64 decompiler 曾把 literal 错显示成窄字符串
`"h"`，其 refs 实际仍指向完整 UTF-16 序列；raw bytes、xref 与 call-site 参数三者共同闭合
了语义。

## 两条 resolve 数据流

### payload-bearing resolve

四端共同顺序为：

```text
resolve/reuse payload entry and retained Layer Variant
target = strict assignable Layer accessor
ignore target.PropSet(MEMBERENSURE, "absolute",
                      adaptorBase + assignSequence,
                      &sharedAbsoluteHint)
++assignSequence
ignore target.PropSet(MEMBERENSURE, "hitThreshold",
                      256,
                      &sharedHitThresholdHint)
release accessor and return Layer Variant
```

sequence increment 明确位于两个 property calls 之间。本轮只补 hint 参数，没有把 increment
移到 `hitThreshold` 后，也没有改变新增/复用 payload、`createdOrChanged`、active/retired map
commit 或 Variant handoff。

### payload-free ordinal resolve

```text
resolve/reuse retained Layer Variant only
target = strict assignable Layer accessor
publish absolute = adaptorBase + current assignSequence with shared absolute hint
publish hitThreshold = 256 with shared hitThreshold hint
return Layer Variant without incrementing assignSequence
```

该 overload 与 payload path 共用同一 hit word，但仍保持“不递增 sequence”的原生边界。
二者不是各自拥有一个函数局部缓存。

## 非 consumer 与边界

`assignFromAdaptor_guess` 每个 entry 会先调用 payload-bearing resolve，所以它间接触发一次
`hitThreshold` publication；随后 assign 尾部只重新发布 rebased `absolute` 以及
`visible/opacity/type/left/top`，没有第二次 `hitThreshold` write，也不直接取本 hint 地址。
因此 data xref 按直接 consumer 计为两个 resolve 函数，不能把 assign 再计一次。

`PlayerRenderExecute.cpp` 的 Wasmtime headless surrogate 也会用 portable helper 设置常量
`hitThreshold`，但四份 native reference 都没有 Player draw/execute 对该 hint data 或该 SLA
literal 的对应 xref。那条平台替代路径不是本轮 native plugin consumer，故没有凭同名属性
强行接入 shared word。

本 slot 只缓存 dispatch member identity，不拥有 target Layer、accessor、Variant 或 map
node。`PropSet` 返回失败时 helper 仍忽略 HRESULT；payload path 已经执行的 sequence increment
不会回滚，payload-free path 仍不递增。dispatch 抛 C++ 异常时沿现有临时 Variant/accessor
展开，active/retired tree 的渐进提交边界保持不变；本轮没有添加 catch、transaction 或新的
持久状态。

## portable 源码与探针

修改覆盖：

1. `RuntimeSupport.cpp`
   - 定义 process-lifetime `hitThresholdMemberHint_guess`；
2. `MotionDispatch.h`
   - 发布 shared word，并明确 native linked distance 随 target 变化，声明位置不等于连续
     layout 证明；
3. `SeparateLayerAdaptor.cpp`
   - payload-bearing 与 payload-free 两个 target publication 传入
     `&detail::hitThresholdMemberHint_guess`；
   - assign 尾部和 source getters 不增加同名调用；
4. `tests/unit-tests/plugins/motionplayer-dll.cpp`
   - 扩展 SeparateLayer recorder，锁定 source-created、target-assigned 与 ordinal-created
     Layer 上的 flags、value 256、实际 hint pointer 和 `objthis=self`；
   - 锁定 `absoluteMemberHint_guess` 与 `hitThresholdMemberHint_guess` 地址不同；
   - target dispatch 即使返回失败，后续 assign publications 仍继续。

compiled source comments 不包含参考库绝对地址。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery` 均已完成：

- 共 4 个槽建立为 size-4 `unsigned int` 并命名
  `hitThresholdMemberHint_guess`；
- 每库对 data、实际 UTF-16 literal、payload resolve、payload-free resolve 各追加一条注释，
  共 16 条；
- 每库一个 data bookmark，共 4 个；
- 每库 force-recompile 两个 consumer，共 8 次；
- 八份 fresh decompile readback 全部直接显示 `&hitThresholdMemberHint_guess`；
- symbol xref readback 与原 raw 计数一致，四份 recovery IDB 均原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- 扩展后的 recorder TU 在两个 macro profile 下均编译通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,648,775 bytes，539 imports / 69 exports；
- Headless wasm：84,995,916 bytes，538 imports / 69 exports；
- 相较 V180 两份 wasm 均增加 75 bytes，import/export ABI 表面不变；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 两个 build tree 的 CTest 均报告 `No tests were found`，没有虚报 runtime unit-test；
  recorder 由 ordinary/headless syntax-only 覆盖；
- `git diff --check` 通过，只出现 working tree 既有 LF/CRLF 转换提示；
- 四端 string/find-bytes、data xref、make-data、force-recompile、symbol readback 与 save 全部
  成功。

构建只出现项目既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 与 JS-library 警告，
没有新增 motionplayer 编译或链接错误。

## 结论边界

本轮闭合的是 SLA 两条 resolve path 的 `hitThreshold = 256` shared-hint identity、直接
consumer、sequence timing、status 与 owner 边界。它不把同名属性推广给 headless surrogate
或其它模块，也不从不同架构不一致的 byte gap 反推连续 native struct。assign 中
`type/left/top` 的 source-null/target-shared 分流已由 V182 以四端直接实参闭合，详见
`analysis/motionplayer_separate_layer_assign_type_left_top_shared_hint_boundary_four_binary_2026-08-17.md`。
