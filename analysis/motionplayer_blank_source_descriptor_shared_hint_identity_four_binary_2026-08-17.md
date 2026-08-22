# source descriptor `blank` 读写共享 hint 身份的四参考复原（2026-08-17）

## 结论

四个参考二进制一致证明，`ResourceManager::findSource` 创建
`blank/<width>:<height>:<originX>:<originY>` descriptor 时写入 `blank = 1`，与
`MotionNode` generic source fallback 后续读取 `source.blank`，使用的是同一个
process-lifetime 4-byte TJS member-hint cache。它不是两个因同名字符串而偶然相似的
function-local static。

两个消费者的 dispatch 方向、flags 和 Variant 类型不同：

- publisher：`PropSet`/`SetValue`，`TJS_MEMBERENSURE`（`0x200`），Integer 1；
- consumer：`PropGet`/`GetValue`，flags `0`，转换为 bool。

member-hint word 缓存的是 TJS 成员查找状态，不拥有 property value，也不绑定读或写方向；
因此同一 word 跨越这两种调用形状是参考实现的可观察身份。

本地源码本轮之前已经让两条路径使用 `motion::detail::blankMemberHint_guess`，所以不需要
改变运行时数据流。本轮补齐四库中仍为 `unk_*` 的数据名和边界、强化源码语义注释，并
让已有 retained-owner 回归直接检查读侧收到该全局 word 的精确地址。

## 四端映射

| 目标 | `MotionNode_findSource_guess` | `ResourceManager_findSource_guess` | `blankMemberHint_guess` |
|---|---:|---:|---:|
| Android arm64 | `0x691CC8` | `0x6A7F1C` | `0x1AB521C` |
| Android armv7 | `0x570500` | `0x57BDE0` | `0x1111750` |
| iOS arm64 | `0x1000F316C` | `0x100102594` | `0x101B696E4` |
| iOS armv7 | `0xEF97C` | `0xFF890` | `0x187D414` |

数据槽在四端都恰好位于 `originYMemberHint_guess` 与 `clipMemberHint_guess` 之间：

```text
width, height, originX, originY, blank, clip,
left, top, right, bottom, x, y
```

邻接顺序支持其属于 source/geometry schema family，但各槽仍是独立 4-byte mutable data
item，不能建模成一个可迭代数组。原始 C++ 数据标识符已被 strip，故保留 `_guess`。

## fresh xref 与 ABI 差异

重新取得 data xrefs：

| 目标 | 总 xrefs | 语义 consumers |
|---|---:|---|
| Android arm64 | 4 | MotionNode read + ResourceManager write |
| Android armv7 | 6 | MotionNode read + ResourceManager write |
| iOS arm64 | 2 | MotionNode read + ResourceManager write |
| iOS armv7 | 5 | MotionNode read + ResourceManager write |

两个 64/32-bit ARM Android 目标以及 iOS armv7 会把同一次源码取址物化为多条指令或
literal-pool 引用，部分 xref 还落在 IDA 未归入函数的 pool head；iOS arm64 在这两个
调用点各形成一个 PC-relative data xref。因此 xref 个数不是消费者个数。

按函数 fresh decompile 后，八份函数都直接解析为同一
`blankMemberHint_guess`，不再出现旧 `unk_*` 表达；每个函数的语义调用点恰好一次。

## publisher 数据流

`ResourceManager_findSource_guess` 的 blank 分支四端共同为：

```text
pieces = split(path, "/")
if pieces[0] != "src":
    if pieces[0] != "blank": return Void

    dims = split(pieces[1], ":")
    dictionary = new Dictionary accessor
    dictionary.width   = dims[0]  // String, MEMBERENSURE
    dictionary.height  = dims[1]  // String, MEMBERENSURE
    dictionary.originX = dims[2]  // String, MEMBERENSURE
    dictionary.originY = dims[3]  // String, MEMBERENSURE
    dictionary.blank   = 1        // Integer, MEMBERENSURE,
                                  // &blankMemberHint_guess
    return independent dictionary object
```

这里继续保留已经证明的边界：`pieces[1]` 和 `dims[0..3]` 没有友好长度检查；每个
SetValue 都构造并销毁自己的临时 Variant；每次调用创建独立 Dictionary。`blank` 的
Integer Variant 在 SetValue 返回后销毁，hint word 只保留 lookup cache，不延长 Variant
或 Dictionary 生命周期。

## consumer 数据流与 owner 生命周期

`MotionNode_findSource_guess` 的 generic fallback 成功路径先取得一个 retained source
dispatch owner，然后依次读取：

```text
width -> height -> originX -> originY -> blank -> clip
```

`blank` getter 的 flags 为 `0`，receiver/objthis 是该 retained source owner，hint 参数是
publisher 使用的同一个 `blankMemberHint_guess`。返回值按参考 bool accessor 语义压到
低位并存入 `SourceState::blank`；之后才读取 `clip`。

source 的 persistent result Variant 即使被前面的 getter 重入清空，已经 AddRef 的 owner
仍覆盖完整 getter 序列，直到 clip/default rect 完成后才 Release。本轮共享的全局 hint
具有进程生命周期，既不 AddRef receiver，也不改变这个 owner 窗口。

这也说明相邻 `clipMemberHint_guess` 必须是另一独立 word：两者 property 名、类型转换和
后续控制流均不同。不能把 `blank` 与 `clip` 因邻接而别名。

## 源码与回归收紧

源码只做证据注释和测试可观察性增强：

1. `MotionDispatch.h` 明确 `blankMemberHint_guess` 被 publisher 与 consumer 复用；
2. `ResourceManager.cpp` 明确五次 SetValue 各用独立 schema slot，而 `blank` 那一槽会被
   后续 MotionNode reader 复用；
3. `SourceObjectReentryProbe` 除 property 顺序外记录每次 getter 的 hint 指针；
4. 现有 `node source fallback retains one owner across reentrant getters` 回归新增断言：
   - 第五次 getter 的指针精确等于
     `&motion::detail::blankMemberHint_guess`；
   - 它不等于第六次 `clip` getter 的指针。

probe 仍在 `width` getter 中清空 persistent source Variant，所以同一测试同时覆盖 retained
owner 生命周期和共享 hint 身份；后续 `blank` 访问成功说明缓存身份收紧没有错误缩短
receiver 生命周期。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery`
均已完成：

- 一个独立 size-4 `unsigned int` data item，命名为
  `blankMemberHint_guess`，共四个 data items；
- data、两个函数和两个代表性 operand 的注释，共 20 处；
- data/read consumer/write consumer 三类 bookmarks，共 12 个；
- 两函数每库 force-recompile，共 8 个；
- 八份 fresh readback 均解析为 `blankMemberHint_guess`，旧 `unk_*` 计数为零；
- 四份 recovery IDB 原位保存成功。

## 验证

- ordinary Emscripten syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- Web wasm：85,647,311 bytes，539 imports / 69 exports；
- Headless wasm：84,994,452 bytes，538 imports / 69 exports；
- 两份 wasm 均由 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 均完整解析 TYPE、IMPORT、FUNCTION、TABLE、TAG、GLOBAL、EXPORT、
  START、ELEM、DATACOUNT、CODE、DATA、name 与 target_features；
- 两份产物与 V172 等长，import/export ABI 表面不变，符合本轮未改变运行时代码的预期；
- 两个 build tree 的 CTest 均报告 `No tests were found`，运行时回归仍依赖 unit-test TU
  的 ordinary/headless 双配置编译；
- 定向 `git diff --check` 只出现仓库既有 LF/CRLF 提示，无 whitespace error。

## 结论边界

本轮只证明上述两个 callsites 共享 `blank` word。不能仅凭字符串相同推断任意未来
`blank` property access 也共享它；新增消费者仍需在四个参考中取得同一 data address
身份。反之，read/write 方向和 flags 不同也不能用来否定共享，因为四端已经直接给出
相同 backing address。
