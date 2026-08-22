# MotionPlayer command builder `primaryLayer` 按需求值、hint 与生命周期四参考复原

日期：2026-08-17

> 2026-08-18 V237 follow-up：SLA 路径前后的 render-layer ID latch 已另行闭合。drawable
> clip/`rawFlag21` 在 SLA 惰性构造前提交；SLA 已存在时直接测试 latch，SLA 缺失时构造、发布并
> begin-pass 后再测试 latch。后续 require exception 不回滚 clip 或已发布 SLA。详见
> `analysis/motionplayer_render_layer_id_latch_persistence_release_four_binary_2026-08-18.md`。

## 1. 结论

四份当前参考二进制共同证明，`Player_buildRenderCommands_guess` 不会在函数入口预取
`Window.mainWindow`、owner raw dispatch 或 `primaryLayer`。公共 command builder 只在两个
彼此独立的惰性门内求值完整链：

1. 第一项 drawable 需要 persistent `SeparateLayerAdaptor`、且 Player owner 槽仍为空时；
2. 一个已经通过 geometry/admission 的 group 的 `composedLayer` 仍为 Void 时。

两条链都会独立执行简单表达式 `Window.mainWindow`，把结果包装为严格
`ncbPropAccessor`，然后以 flags 0、共享的 exact `primaryLayer` member-hint word 和非空 Void
结果槽读取 `primaryLayer`。普通 HRESULT 被忽略；没有 type/null 检查，也不会把失败转换为
友好的空 raw dispatch。

两条链的消费方式不同：

- SLA 路径把读取结果直接传给 `SeparateLayerAdaptor` new-expression，构造成功后才把 raw
  pointer 发布到 Player，并立即 begin pass；
- group 路径把 owner Variant 与 primary result Variant 传给共享
  `Motion_createLayerVariant_guess`，再 copy-assign 到 group 保存槽。

这推翻了本地旧端口中“builder 入口先调用友好 resolver 得到 scratch owner/parent，后续
leaf/group 共用 raw dispatch”的结构。按需重复求值本身是可观察行为：当脚本在同一 build
过程中改变 `Window.mainWindow` 或 `primaryLayer` 时，不同惰性门可以看到不同对象；没有触发
相应门时则完全不发生该脚本访问。

本文仅在分析记录中保留四参考绝对地址。编译源码只保留语义注释。

## 2. builder 与共享 hint 映射

| 目标 | `Player_buildRenderCommands_guess` | `primaryLayerMemberHint_guess` |
|---|---:|---:|
| Android arm64-v8a | `0x6C2208` | `0x1AB52D4` |
| Android armeabi-v7a | `0x58C7C4` | `0x11117E0` |
| iOS arm64 | `0x1001167BC` | `0x101B6979C` |
| iOS armv7 | `0x114118` | `0x187D4A4` |

该 word 在 V184 的 SourceCache bake 相邻 hint-family 纵切面中已经按四端共同证据建立并命名。
本轮 fresh xref/readback 将其拓扑闭合到 command builder 的两个语义调用点：

| 目标 | raw data xrefs | 拓扑说明 |
|---|---:|---|
| Android arm64-v8a | 6 | SourceCache constructor 的两条 materialization 指令；builder 两个调用各两条 materialization 指令 |
| Android armeabi-v7a | 9 | SourceCache constructor/chunk 与 builder 两个调用；包含 ARM literal-pool/materialization 引用 |
| iOS arm64 | 3 | 三个语义调用各一个直接 data xref |
| iOS armv7 | 8 | SourceCache constructor 两条；builder 两个调用各三条 materialization 引用 |

因此 xref 数不是语义调用次数；四端共同语义都是 SourceCache constructor 一次，加 builder
两次。不同 ISA 的地址物化和 literal-pool 形式导致 raw xref 数不同。

## 3. 两条 `primaryLayer` 调用

### 3.1 persistent SLA 惰性构造

| 目标 | `Window.mainWindow` expression call | `primaryLayer` call |
|---|---:|---:|
| Android arm64-v8a | `0x6C23A4` | `0x6C2438` |
| Android armeabi-v7a | `0x58C908` | `0x58C954` |
| iOS arm64 | `0x1001168E4` | `0x100116944` |
| iOS armv7 | `0x1143F4` | `0x11446E` |

### 3.2 group composed-Layer 惰性构造

| 目标 | `Window.mainWindow` expression call | `primaryLayer` call |
|---|---:|---:|
| Android arm64-v8a | `0x6C339C` | `0x6C342C` |
| Android armeabi-v7a | `0x58D568` | `0x58D5A8` |
| iOS arm64 | `0x100117604` | `0x100117658` |
| iOS armv7 | `0x115080` | `0x1150D6` |

两组 call site 的 ABI 均为：

```text
receiver       = Window.mainWindow expression 返回的 accessor dispatch
member         = "primaryLayer"
flags          = 0
hint           = &primaryLayerMemberHint_guess
result         = non-null tTJSVariant，调用前为 Void
objthis        = 与 receiver 相同的 Window.mainWindow dispatch
HRESULT        = 普通返回值被忽略
```

结果槽不是 scratch raw pointer。调用完成后按 Variant copy/参数传递规则进入下一阶段，再在原生
清理点析构。若 `PropGet` 返回失败且没有写 result，Void 原样流入严格 consumer；代码没有再做
类型恢复、null 保护或 native Layer 转换。

## 4. `Window.mainWindow` expression wrapper 身份

本轮 fresh decompile/disassembly 将四端 wrapper 统一命名为
`TVPExecuteExpression_simple_guess`：

| 目标 | wrapper |
|---|---:|
| Android arm64-v8a | `0x8E4374` |
| Android armeabi-v7a | `0x6B47E4` |
| iOS arm64 | `0x100187430` |
| iOS armv7 | `0x184C68` |

Android arm64、iOS arm64、iOS armv7 都是把 null context 传给完整 evaluator 的薄 wrapper。
Android armv7 内联展开程度更高，保留了 `../../src/core/base/ScriptMgnIntf.cpp` 第 691 行的
source marker，并进入 script-engine evaluation；这与其他三端的 wrapper 身份相互印证。

它不是缓存好的 `mainWindow` getter，也不是 raw-dispatch resolver。每次 call 都重新执行脚本
表达式，因此不得把两处调用折叠成函数入口的一次求值。

## 5. 字符串与编码搜索

对 `Window.mainWindow` 执行普通字符串搜索以及 ASCII/UTF-8、UTF-16LE、UTF-32LE byte search，
并将每项搜索翻页到 cursor 结束。结果如下：

| 目标 | ordinary | ASCII/UTF-8 | UTF-16LE | UTF-32LE |
|---|---:|---:|---:|---:|
| Android arm64-v8a | 0 | 0 | `0x14D6164` | 0 |
| Android armeabi-v7a | 0 | 0 | `0x58D4E0`, `0x58DB64` | 0 |
| iOS arm64 | 0 | 0 | `0x10195C702` | 0 |
| iOS armv7 | 0 | 0 | `0x174EA66` | 0 |

armv7 的重复结果来自两个 call site 的 literal-pool/常量布局。iOS decompiler 某些视图只把
UTF-16 参数渲染成 `"W"`；逐字节读取证明它是完整 `Window.mainWindow`，不能按伪 C 的
截断显示把它误认成单字符表达式。

## 6. SLA 的精确求值、分配与发布顺序

四端共同源结构等价于：

```cpp
if(drawableNeedsSeparateLayer && renderSeparateLayerAdaptor == nullptr) {
    tTJSVariant owner;
    TVPExecuteExpression(TJS_W("Window.mainWindow"), &owner);
    ncbPropAccessor ownerAccessor{tTJSVariant(owner)};

    renderSeparateLayerAdaptor =
        new SeparateLayerAdaptor(
            ownerAccessor.GetValue(
                TJS_W("primaryLayer"),
                ncbTypedefs::Tag<tTJSVariant>(),
                0,
                &primaryLayerMemberHint_guess));

    renderSeparateLayerAdaptor->beginLayerPass_guess();
}
```

对应 allocation/constructor/publication 地址：

| 目标 | allocation | constructor call | Player slot publish |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6C2400` | `0x6C2458` | `0x6C2460` |
| Android armeabi-v7a | `0x58C92E` | `0x58C95C` | `0x58C964` |
| iOS arm64 | `0x100116918` | `0x100116950` | `0x100116954` |
| iOS armv7 | `0x114438` | `0x114480` | `0x11448A` |

这里有一个由 new-expression 求值次序产生的重要边界：

1. expression 求得 mainWindow Variant；
2. accessor 严格取得 owner dispatch；
3. `operator new` 先取得 adaptor storage；
4. 随后才执行 `primaryLayer` `GetValue`；
5. constructor 消费 primary result；
6. constructor 成功后才发布 raw pointer 到 Player slot；
7. primary result 临时量析构；
8. 对刚发布对象立即 `beginLayerPass_guess()`；
9. accessor 释放 dispatch；
10. mainWindow Variant 析构。

因此 allocation 成功而 `GetValue`/Variant conversion/constructor 抛出时，new-expression 的
相应异常规则负责尚未发布对象；Player slot 仍为空。相反，publication 后的 result cleanup 或
begin-pass 异常不会回滚 Player slot。四端没有 builder 自己的 catch/rollback，也没有在读取
失败时改走友好 null 目标。

## 7. group composed Layer 的求值与提交顺序

group 路径只在 group 已通过前置 geometry/admission，且保存槽仍为 Void 时执行：

```cpp
if(group.composedLayer.Type() == tvtVoid) {
    tTJSVariant owner;
    TVPExecuteExpression(TJS_W("Window.mainWindow"), &owner);
    ncbPropAccessor ownerAccessor{tTJSVariant(owner)};

    group.composedLayer =
        detail::createLayerVariant_guess(
            owner,
            ownerAccessor.GetValue(
                TJS_W("primaryLayer"),
                ncbTypedefs::Tag<tTJSVariant>(),
                0,
                &detail::primaryLayerMemberHint_guess));
}
```

共享 factory 与 publication 映射：

| 目标 | factory call/inline block | composed Variant assign |
|---|---:|---:|
| Android arm64-v8a | `0x6C3454` 附近内联 | 紧随内联 factory 的 assign block |
| Android armeabi-v7a | `0x58D5B2` | `0x58D5BA` |
| iOS arm64 | `0x100117668` | `0x100117674` |
| iOS armv7 | `0x1150E6` | `0x1150F4` |

共同清理/提交顺序是：expression → accessor → primary result → shared factory → created Variant
copy-assign 到 group 字段 → created result 析构 → primary result 析构 → accessor Release → owner
Variant 析构。它没有在 builder 入口保存 raw owner/parent，也没有复用 SLA 门较早看到的
mainWindow/primaryLayer。

同一个 build 中可以有多个首次物化的 group；每个 `composedLayer == Void` 门都独立执行这条
链。已经有 composed Variant 的 group 不求值表达式，也不读取 `primaryLayer`。

## 8. 与旧端口结构的差异

| 旧本地结构 | 四参考共同结构 | 可观察影响 |
|---|---|---|
| builder 入口无条件解析 mainWindow owner | 只有 SLA/group 惰性门内执行表达式 | 无 drawable/无新 group 时不得访问脚本对象 |
| 入口无条件解析 primary raw dispatch | 每个触发门独立 GetValue 到 Variant | 同一 build 内可看到脚本侧对象变化 |
| scratch raw owner/parent 向 group helper 传递 | group 门内构造 owner/primary Variant | 恢复 Variant 生命周期与异常边界 |
| friendly resolver 做 type/null 保护 | 严格 accessor，HRESULT 忽略 | 失败自然流入 Void/异常路径 |
| 先得到 primary，再分配 SLA | new-expression allocation 先于参数 GetValue | 分配失败与属性读取副作用的先后可观察 |

单独的 `KRKR2_WASMTIME_HEADLESS` execute/diagnostic helper 仍可因 Web sidecar 需求使用现有
friendly resolver；本纵切面只恢复四参考都有的公共 `buildRenderCommands`。不得因为删除
common builder 的 scratch 数据流，就把未映射的 headless-only 路径冒充成 native 行为或一并
机械删除。

## 9. 源码恢复

`cpp/plugins/motionplayer/PlayerRenderExecute.cpp` 与 `Player.h` 已完成：

- 删除 common builder 入口的 `resolveMainWindowOwnerObject()` 与
  `resolveMainWindowPrimaryLayerObject()` 预取；
- `composePreparedGroupLayers_guess` 删除两个 raw scratch 参数；
- 每个 group Void 门内恢复 expression、strict accessor、exact primary hint/result 与共享
  Variant factory；
- SLA lazy gate 内恢复独立 expression/accessor/GetValue，并保留 direct new-expression；
- exact `primaryLayerMemberHint_guess` 在两处调用中复用同一个 process-global word；
- 保留 headless-only execute helper 的独立端口边界。

结构扫描确认 common group 部分恰有一处 expression 和一处 exact primary hint，SLA build body
也恰有一处 expression和一处 exact primary hint；相关公共 builder 范围没有旧 raw scratch
参数或入口 owner/primary resolver。

## 10. Recovery IDB 回写

四份 recovery IDB 均完成：

- expression wrapper 重命名为 `TVPExecuteExpression_simple_guess`；
- builder、wrapper、UTF-16 expression literal、primary hint data、两处 expression call、两处
  primary call 共写入 8 条语义注释，每库 8 条、合计 32 条；
- 每个 builder 加 1 个书签，共 4 个；
- 每库强制重编 builder 与 wrapper，共 8 次 force-recompile；
- fresh wrapper decompile 与 expression-call disassembly 已回读新名字和完整调用关系；
- 四份 recovery IDB 均原位保存成功。

`primaryLayer` hint data item 已由前序四参考 family 纵切面建立，本轮复用并闭合 consumer
拓扑，没有创建第二套同名或同用途 global。

## 11. 验证与产物

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两份完整 motionplayer 单测 TU syntax-only 均通过；
  只有仓库既有 `_tss` deprecated warning；
- `cmake --build out/web/debug` 与 `cmake --build out/wasmtime/debug` 均成功链接；
- Node Wasm parse 与 `llvm-objdump` 全 section 检查通过；
- Web/Headless CTest 均返回成功，但两个配置当前都没有注册运行时测试；
- `git diff --check` 成功，只有工作树既有 LF/CRLF 提示。

V185 产物：

| 配置 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | 85,647,577 B | 539 / 69 | `0x1BD24` | `0xD5B2` | `0x1A407D5` | `0x5A3F37` | `0x3184928` |
| Wasmtime Headless Debug | 84,994,718 B | 538 / 69 | `0x1BA43` | `0xD5DA` | `0x19E8783` | `0x5A1187` | `0x31407BE` |

相对 V184，两份 Wasm 均严格减少 1,375 bytes：CODE 减少 `0x3ED`/1,005 bytes，name 减少
`0x16E`/366 bytes，FUNCTION 减少 4 bytes；GLOBAL、DATA、imports、exports 及其余 section
不变。严格对称的减量与删除入口级友好解析链、恢复两处短内联按需求值一致，但二进制大小
只用于回归，不替代前十节的 fresh 四端语义证据。

本轮没有新增独立运行时 case：该路径属于 private build pass，现有测试工程没有 native oracle
或已注册 CTest harness。ordinary/headless 两份 syntax-only 覆盖了本轮受 `Player.h` 影响的测试
TU；行为结论来自四端 decompile/disassembly/xref/string/lifetime 联合证据。
