# MotionNode `findSource` 唯一 hint 边界的四参考复原（2026-08-17）

## 结论

四个参考二进制一致把 generic source fallback 的
`ResourceManager.findSource(context, path)` dispatch 绑定到一个独立、进程生命周期的
4-byte member-hint word。该 word 紧邻 source/geometry schema，位于 `width` 槽之前；但
data xref 证明它只有 `MotionNode_findSource_guess` 一个 native function consumer，所以
源码应保持 `PlayerRender.cpp` TU-local，而不应仅因邻接关系提升成所有 motionplayer
翻译单元可见的通用 family。

本地此前已经用匿名 namespace 的 `findSourceMemberHint_guess` 表达这一 storage duration。
本轮没有改变运行时调用，只补齐 recovery IDB 中仍为 `unk_*` 的数据边界/名字、增加精确
语义注释，并让 dispatch 回归锁定同一 helper 连续调用时的 hint 指针稳定性。

## 四端函数与数据映射

| 目标 | `MotionNode_findSource_guess` | `findSourceMemberHint_guess` | data xrefs |
|---|---:|---:|---:|
| Android arm64 | `0x691CC8` | `0x1AB5208` | 2 |
| Android armv7 | `0x570500` | `0x111173C` | 3 |
| iOS arm64 | `0x1000F316C` | `0x101B696D0` | 1 |
| iOS armv7 | `0xEF97C` | `0x187D400` | 2 |

四端 data xrefs 全部归入同一个 MotionNode resolver；Android armv7 另有一处落在 IDA
未归函数的 literal-pool head。其余计数差异来自 ADRP/ADD、MOV/literal-pool 或 PC-relative
地址物化，不是额外消费者。fresh decompile/force-recompile readback 后，四端调用点都解析
为 `findSourceMemberHint_guess`，旧 `unk_*` 计数为零。

该槽与后续 schema 的顺序为：

```text
findSource,
width, height, originX, originY, blank, clip,
left, top, right, bottom, x, y
```

邻接只描述 native BSS 布局；每个槽仍是独立 4-byte mutable data item。

## fresh UTF-16LE 字符串证明

由于部分 decompiler listing 把宽字符串错误渲染成单个 `"f"`，本轮对 UTF-16LE 原始字节
`findSource` 重新搜索。每个目标都有两处字节命中，其中第一处无 xref，第二处同时被
MotionNode callsite 与 ResourceManager NCB registration 使用：

| 目标 | 无 xref duplicate | referenced `findSource` literal |
|---|---:|---:|
| Android arm64 | `0x14D52AA` | `0x14D5302` |
| Android armv7 | `0xD84E52` | `0xD84EAA` |
| iOS arm64 | `0x10195B566` | `0x10195B5C2` |
| iOS armv7 | `0x174D8CA` | `0x174D926` |

referenced literal 的语义消费者严格分为：

- `MotionNode_findSource_guess`：实际发出 TJS `FuncCall`；
- `ResourceManager_ncb_registerMembers_guess`：把 native method 注册到脚本 surface。

NCB registration 只共享成员名字 literal，不引用本轮 member-hint data；因此 hint 的 native
consumer 仍然只有 resolver 一个。

## 精确 dispatch 数据流

四端共同 call shape 为：

```text
contextArgument = copy(Player persistent motion-context Variant)
pathArgument    = String(src + optional "/" + icon)
argv            = [&contextArgument, &pathArgument]

status = retainedResourceManager.FuncCall(
    flags   = 0,
    member  = "findSource",
    hint    = &findSourceMemberHint_guess,
    result  = &persistentSourceObject,
    argc    = 2,
    argv    = argv,
    objthis = retainedResourceManager)
```

参数顺序不能交换。context 保留其原 Variant 类型，不预先转成 String；path 始终是 String。
`src` 为空且 icon 非空时保留前导 `/`，两者都空时仍以空 String 发起调用。

调用返回后共同门槛为：

```text
if status != TJS_S_OK || persistentSourceObject.Type == Void:
    source.valid = false
    return
source.valid = true
strictly acquire source Object owner
read width/height/originX/originY/blank/clip
```

因此 ordinary failure 即使写入非 Void output 也不继续；`TJS_S_OK + Void` 是 invalid；
`TJS_S_OK + 非 Void 非 Object` 先提交 `valid=true`，随后在严格 Object 转换处自然抛出。
hint word 不保存 HRESULT 或 output，也不改变这些提交边界。

## receiver 与生命周期

resolver 在调用前已经从 Player 的 ResourceManager closure 建立独立 retained dispatch owner；
该 owner 同时作为 receiver 与 objthis。context/path/result Variants 都跨完整 FuncCall 存活，
调用后按控制流逆序析构。脚本重入即使替换 Player 的 ResourceManager 或 context 字段，当前
调用仍由 snapshot 和独立 AddRef 保护。

`findSourceMemberHint_guess` 是零初始化、进程生命周期的 mutable lookup cache，不 AddRef
ResourceManager，也不拥有 context/path/result。它比单次调用活得更久，但与 receiver
owner tree 正交。

## 源码范围与回归

本轮源码保持既有结构：

- `findSourceMemberHint_guess` 留在 `PlayerRender.cpp` anonymous namespace；
- `Player::dispatchFindSource_guess` 继续是对 native inline block 的可复用抽取；
- 编译源码注释只记录唯一 native consumer、TU scope 与进程生命周期，不写绝对地址。

`Player::findSource` 是端口暴露的便利 wrapper，也通过同一抽取 helper。新增回归在同一
Player/ResourceManager 上连续调用两次，断言：

- 第一次 `findSource` 收到非空 hint 指针；
- 第二次收到精确相同的指针；
- 两次仍返回 ResourceManager 写入的 Integer result。

这只锁定 portable helper 的稳定 storage，不把便利 wrapper 计作第二个 native xref，也
不据此扩大四参考 consumer 集。

## recovery IDB 写回

`sla_a64_recovery`、`sla_a32_recovery`、`sla_i64_recovery`、`sla_i32_recovery`
均已完成：

- 一个独立 size-4 `unsigned int findSourceMemberHint_guess`，共 4 个 data items；
- data、function 与代表性 operand 三类注释，共 12 处；
- data/function 两类 bookmarks，共 8 个；
- resolver 每库 force-recompile，共 4 个函数；
- 四端 readback 都解析为新名字，旧 `unk_*` 为零；
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
- 两份产物与 V173/V172 等长，import/export ABI 表面不变；
- 两个 build tree 的 CTest 都报告 `No tests were found`，回归仍由 unit-test TU 的
  ordinary/headless 双配置编译覆盖；
- 定向 `git diff --check` 只有仓库既有 LF/CRLF 提示，没有 whitespace error。

## 结论边界

同名 `findSource` 的 ResourceManager NCB registration、native method implementation 与
MotionNode dispatch 不是同一类对象：只有最后者需要本轮 member-hint word。后续若发现
其他脚本调用 `findSource`，不能仅凭共享 UTF-16 literal 就复用该 cache，必须在四端证明
data address 身份。
