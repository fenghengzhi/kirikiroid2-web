# motionplayer `initNonEmoteMotion` 的 `parameter` 全局 hint 与分支生命周期（四参考二进制）

日期：2026-08-16

## 1. 结论

四个 `reference/binaries/` 共同证明，`Player::initNonEmoteMotion` 非 Object 分支读取的
`parameter` 使用一个独立 32 位进程级 mutable member-hint 槽。它虽然只有 init 一个
consumer，却不是 `PlayerCore.cpp` 可以任意放置的函数/TU 私有 cache word：四端都把它精确
放在 `visible -> setPos -> opacity -> isValid -> parameter -> releaseLayerId` 全局序列中，且
每相邻槽严格相差 4 bytes。

因此 V147 的“init-private `parameter` 槽”结论需要细分：consumer 集合确实只有 init，storage
identity 却属于插件级全局序列。本轮删除旧 `Player_parameterListHint_guess`，改由
`motion::detail::parameterMemberHint_guess` 表达该地址身份。`parameterize` 仍是另一处更早的
共享槽，同时由 init 与 node-field initializer 使用；相同前缀不表示同一 cache word。

本文绝对地址只用于四份参考二进制的分析坐标；编译源码继续只保留语义名，stripped 原名未知
的符号保留 `_guess`。

## 2. 四端坐标与全局边界

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| previous `isValid` hint | `0x1AB5494` | `0x1111930` | `0x101B6995C` | `0x187D600` |
| `parameter` hint | `0x1AB5498` | `0x1111934` | `0x101B69960` | `0x187D604` |
| next `releaseLayerId` hint | `0x1AB549C` | `0x1111938` | `0x101B69964` | `0x187D608` |
| UTF-16LE `parameter` | `0x1518CFE` | `0x580F7C` | `0x10195C84E` | `0x174EBB2` |
| `Player::initNonEmoteMotion` | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |
| exact `parameter` getter | `0x6B0DCC` | `0x580DC2` | `0x1001084AC` | `0x105B70` |
| next-slot consumer | `0x6B2AD8` | `0x581F3C` | `0x100109ACC` | `0x107358` |

`parameter` 的精确 UTF-16LE pattern 为：

```text
70 00 61 00 72 00 61 00 6D 00 65 00 74 00 65 00 72 00 00 00
```

Android armv7 的 `0xDBE0EC`、iOS arm64 的 `0x10196D886`、iOS armv7 的 `0x175FC32`
也含相同原始字节，但没有本 getter 的 code xref；表中位置才是 init 实际使用的成员名。
Android armv7 的真实 literal 位于 code-near literal pool，仍由 init 指令直接引用。

四端 fresh global-xref 去重后，`parameter` 槽都只有 `Player::initNonEmoteMotion` 一个语义
consumer。右侧相邻槽由旧 node tree 的释放路径使用，其函数已恢复为
`Player_resetAndReleaseOldNodeTree_guess`（各 ABI 的函数切分会让精确显示形式不同）。这条右边界
排除了把 `parameter` 并入 `isValid`、并入 node-release 槽或留作无布局关系的 TU-local 静态量。

## 3. 与 `parameterize` 的关系

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | consumer |
|---|---:|---:|---:|---:|---|
| `parameterize` | `0x1AB53FC` | `0x1111898` | `0x101B698C4` | `0x187D568` | init + node fields |
| `parameter` | `0x1AB5498` | `0x1111934` | `0x101B69960` | `0x187D604` | init only |

两次读取都是同一长期 `motionContent` accessor 上的 typed `tTJSVariant` `GetValue`：flags 为 0，
receiver 与 objthis 都是被 accessor 保留的 motion dispatch，caller 不对普通 HRESULT 另行分支。
差异只在成员名、hint address 和后续控制流，不能因只有一个 consumer 就把第二个槽降格为局部
实现细节。

## 4. 精确分支、局部对象与错误边界

四端共同控制流为：

```text
parameterize = motionContent.GetValue("parameterize", Variant, 0,
                                      &global_parameterize_hint)

if parameterize is Object:
    appendParameterEntry(parameterize)
    finalizeParameterTable()
    if parameterEntries is not empty:
        selected = &parameterEntries.front()
    // empty append preserves the previous selected pointer
else:
    parameter = motionContent.GetValue("parameter", Variant, 0,
                                       &global_parameter_hint)
    parseParameterList(parameter)
    destroy parameter

    if parameterize is Integer:
        range-check the converted signed index
        selected = &parameterEntries[index]
        // invalid index throws only after parameter parsing and local teardown
    else:
        selected = null
```

几个容易被高级伪代码隐藏的边界：

- Object 分支根本不读 `parameter`；append/finalize 后只有 vector 非空才覆盖 selected，空结果
  保留旧值；
- 非 Object 分支一定先读并解析 `parameter`；`parameterize` 为 Integer 时也不能提前选择；
- `parameter` 局部 Variant 在选择/越界检查前已析构；`parameterize` 局部则继续跨过后续状态提交、
  node build 与 variable initialization；
- Integer 使用有符号边界检查，负值或超出 vector size 的值在 parse 已产生副作用后抛出；
- Void、Real、String 等其它 `parameterize` 类型都在 parse 完成后把 selected 置空；
- ordinary getter HRESULT 不被 caller 检查：dispatch 先写结果再返回 failure 时仍消费该结果；直接
  抛异常则按已建立的 accessor/Variant 逆序展开。

这继续保持 V147 已证实的长期 owner 关系：root-item accessor 在 `parameter` getter 执行时仍然
存活，motion-content accessor 比 root-item accessor 释放得更晚。把 hint 移入共享全局序列不会
改变 owner tree，只修正 cache-word identity。

## 5. 源码与回归落地

- `MotionDispatch.h` / `RuntimeSupport.cpp`
  - 在 `isValidMemberHint_guess` 之后声明/定义
    `parameterMemberHint_guess`；
  - 注释明确其 consumer 单一但 storage 属于精确进程级序列；
- `PlayerCore.cpp`
  - 删除 TU-local `parameterListMemberHint_guess`；
  - typed `parameter` getter 改用共享全局地址；
  - Object/非 Object 分支、局部 Variant 和 selection 顺序保持不变；
- `motionplayer-dll.cpp`
  - 把 `parameter` 纳入 renderer 后继全局槽的地址互异检查；
  - 新增 `ordinary init parameter getter uses the recovered global hint`：让 `parameterize` 写出
    Void 并返回普通 failure，随后让 `parameter` getter 抛异常；
  - 探针锁定 `loopTime/lastTime/tag/priority/parameterize/parameter` 六次读取顺序、flags、
    receiver/objthis、两个精确非 null hint、`parameter` 时 root 仍活，以及 root/priority/motion
    三个 dispatch 各析构一次。

测试 preset 目前没有注册 Catch2 runtime target，因此这里不把 assertion 的语法编译写成运行时
pass；决定性行为证据仍来自四端 fresh decompile/disassembly/xref。

## 6. IDB 回写

四份 recovery IDB 均完成并原位保存：

- `parameter` 精确地址重建为独立 4-byte `unsigned int`，命名
  `g_motion_parameterMemberHint_guess`；
- 在全局槽、有效 UTF-16 literal、init 函数、exact getter call 和右侧 node-release 边界写入
  V162 注释；
- 添加 `V162 complete init parameter member-hint/global boundary` bookmark；
- force-recompile 四端 init 后，fresh pseudocode 各出现一次新全局名，旧
  `Player_parameterListHint_guess` 出现次数为 0；
- fresh globals 回读确认四槽均为 size 4；
- 四份数据库全部保存成功。

V147 IDB 当时的 `Player_parameterListHint_guess` 是基于“单 consumer 即私有”的中间命名，已由
本轮更完整的相邻全局序列证据取代。

## 7. 验证

2026-08-16 最终完成：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整 test TU syntax-only 均通过，只有既有
  `_tss` 弃用 warning；
- Web Debug 与 Wasmtime Headless Debug 最终工作树构建均成功链接；首个组合命令在 Web 完成、
  Headless 继续执行时超过 120 秒上限，随后分别续跑确认 Web `no work to do`、Headless 完成
  最后一条 link；
- Node `WebAssembly.Module` parse 两份 wasm 均通过；
- `llvm-objdump -h` 两份 wasm 的完整 section table 均通过；
- Web wasm：`85,648,225` bytes，539 imports / 69 exports；
- Headless wasm：`84,995,366` bytes，538 imports / 69 exports；
- 相比 V161，两份 wasm 都精确增加 57 bytes，import/export 数不变；
- 两个 CTest build tree 均可运行，但仍报告 `No tests were found`；
- compiled-source stale scan 中 `parameterListMemberHint_guess` 为 0，新的
  `parameterMemberHint_guess` 只落在共享声明、定义、init getter 与两组定向测试；
- `git diff --check` 无 whitespace error，只有工作树既有 LF/CRLF 转换提示。

本纵切面只闭合 `parameter` 的全局存储身份及其 init 分支边界。其右侧
`releaseLayerId -> window -> piledCopy` 连续槽以及完整 node-tree reset 容器/调用链已由 V163
继续闭合，见
`analysis/motionplayer_old_node_reset_release_window_piled_hint_sequence_four_binary_2026-08-16.md`。
这仍不表示整个 motionplayer 已达到完整一比一。
