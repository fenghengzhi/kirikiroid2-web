# motionplayer dead value、引用计数 no-op、未初始化局部与 inactive tail 审计（四参考二进制，2026-08-28）

## 1. 结论

`MP-B12` 已闭合。四个参考二进制共同证明：原实现不是“所有对象先清零，再按需覆盖”的
安全化模型，而是普通 C++ 的选择性构造、局部变量默认初始化、成员声明顺序和 RAII 临时
owner 共同形成的边界。即使一个值最终没有参与正常结果，只要它仍会触发转换、异常、
AddRef/Release、容器 copy 或析构，它就是共享源码 token，不能作为“优化”从本地实现删除。

本轮没有发现需要修改生产 C++ 的新偏差。本地已经保留以下四类共同边界：

1. dead store/dead result：`_dirty=false`、严格 PSB width/height 读取、重复 `left` 发布、
   controller scratch 输出等，即使某端优化器删除机器 store 或最终数值不被消费，源码步骤仍在；
2. 引用计数 no-op：`ncbPropAccessor`/`tTJSVariant`/`ttstr` 临时 owner 即使不提供最终业务值，
   仍保留精确的转换、AddRef、Release、异常与析构时点；
3. 未初始化局部/成员：geometry 15 个 double、controller curve tail、spring output、
   SourceCache color tail、PSB resource size、Bezier accumulators、anchor carry 等不得补默认值；
4. inactive tail：MotionNode copy-only record、PreparedRenderItem dormant POD、wind inactive slot、
   TriangleBatch clip key、selector entry gate、Player dormant/tail slots等只由真实 writer 写入，
   复制/序列化/析构也严格按实际 owner 与 trivial 字段的区别处理。

这些边界中的未初始化读属于原实现的 C++ 未定义行为或 ABI/优化器残值前沿。它们能够证明
“源码没有初始化”，却不能证明一个跨运行稳定的具体数值或字节串。因此本项不添加会读取
未初始化对象的测试，也不把某次 native 残值、allocator 内容或寄存器 residue 固化成 oracle。

## 2. 本轮 fresh 四端取证

四个目标和配套 IDB 在本轮重新核对，Hex-Rays 可用；每个地址都只在所属二进制内使用。
共审计 18 组语义范围 × 4 端 = 72 个独立入口/内部入口，全部执行 fresh decompile；Android
arm64 的 SourceCache/Bezier 合并函数内部入口和 Android armv7 的 ObjSource 误并内部入口另以
完整 disassembly 取证。所有 72 个范围都读取完整反汇编和双向 xref。

| 目标 | 范围数 | 完整指令 | xrefs-to | xrefs-from | IDB 处理 |
|---|---:|---:|---:|---:|---|
| Android arm64-v8a | 18 | 8,032 | 63 | 19 | 18 个审计锚点、1 个书签、已保存 |
| Android armv7 | 18 | 5,059 | 53 | 18 | 18 个审计锚点、1 个书签、已保存 |
| iOS arm64 | 18 | 4,217 | 60 | 18 | 18 个审计锚点、1 个书签、已保存 |
| iOS armv7 | 18 | 6,419 | 54 | 18 | 18 个审计锚点、1 个书签、已保存 |
| **合计** | **72** | **23,727** | **230** | **73** | **72 个锚点、4 个书签、四个 IDB 已保存** |

四个合并/误并内部入口不能附着 Hex-Rays citem comment，已在精确指令地址改用 line comment；
其余锚点同时进入 disassembly/decompiler comment。

## 3. 四端函数映射

表内 `internal` 表示入口位于 IDA 合并函数内，但开头有独立栈帧/函数序言；它不是缺失项。

| 语义范围 | Android arm64-v8a | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Player constructor | `Player_ctor@0x6CC110` | `Player_ctor@0x5935C4` | `Player_ctor@0x10011EC04` | `Player_ctor@0x11D488` |
| MotionNode load initializer | `Player_initializeNodeFromLayer_guess@0x6B1058` | `...@0x580FA4` | `...@0x100108720` | `...@0x105E70` |
| Point zero-argument NCB constructor | `Point_NCB_ctor_dispatch_guess@0x6DCBC4` | `...@0x59D858` | `...@0x10012CF5C` | `...@0x12BA08` |
| EmoteVarController constructor | `EmoteVarController_ctor@0x664410` | `...@0x554180` | `...@0x1001A4AD0` | `...@0x1A3FEC` |
| Blink controller constructor | `EmoteBlinkController_ctor@0x65FD48` | `...@0x551B34` | `...@0x1001A1C8C` | `...@0x1A0E50` |
| Selector controller constructor | `EmoteSelectorController_ctor@0x66B778` | `...@0x5583B6` | `...@0x1001B7DFC` | `...@0x1B75EC` |
| simple spring/hair-parts wrapper | `motion_EmoteEngine_stepHairParts_guess@0x678B28` | `...@0x55EE98` | `...@0x1001B29D0` | `...@0x1B24D8` |
| SourceCache loadSource | internal `0x6A4F88` in `0x6A4CD4` | `SourceCache_NCB_loadSource_guess@0x57ACC8` | `...@0x1001009AC` | `...@0xFDB50` |
| SourceCache packed tint | `SourceCache_applyPackedCornerTint_guess@0x6A48F8` | `...@0x57A754` | `...@0x10010032C` | `...@0xFD4B4` |
| ObjSource texture materialization | `ObjSource_ensureTexture_guess@0x6D7834` | internal `0x599A34` in `0x5999F4` | `...@0x10012686C` | `...@0x125D4C` |
| calcPatchBounds | internal/root `0x6A264C` | `motion_BezierPatch_calcPatchBounds_callback@0x579258` | `...@0x1000FE804` | `...@0xFB868` |
| calcMeshBounds | internal `0x6A2A04` in `0x6A264C` | `motion_BezierPatch_calcMeshBounds_callback@0x5794F8` | `...@0x1000FEAB8` | `...@0xFBBDC` |
| calcBezierPatch | internal `0x6A2D6C` in `0x6A264C` | `motion_BezierPatch_calcBezierPatch_callback@0x5797A0` | `...@0x1000FEE38` | `...@0xFC014` |
| anchor node phase | `Player_updateLayersPhase3_AnchorNode_guess@0x6BD908` | `...@0x589C00` | `...@0x100113024` | `...@0x110908` |
| append prepared items | `Player_appendPreparedRenderItems_guess@0x6BF714` | `...@0x58B178` | `...@0x1001148F8` | `...@0x1123D8` |
| deep D3D renderer | `motion_Player_renderPreparedItemsToD3DTexture@0x6AB39C` | `...@0x57D3DC` | `...@0x100104450` | `...@0x101850` |
| ttstr ASCII lowercase | `ttstr_ToLowerCase_ASCII@0xA0AB28` | `...@0x75EA48` | `...@0x1001A0614` | `...@0x19F838` |
| wind constructor/setWind inline owner | `motion_EmoteWindEmitter_ctor_guess@0x66DEDC` | `motion_EmoteEngine_setWind_guess@0x559900` | `...@0x1001AC718` | `...@0x1ABF24` |

## 4. 共同源码模型

四端的共享控制流可归纳为：

```text
construct T:
    construct non-trivial members in declaration order
    write only explicit default/member-initializer fields
    leave every other trivial scalar/byte/slot untouched

execute wrapper:
    perform required Variant/String/Object conversion even if business result is unused
    retain temporary owner(s)
    run callback/container operation; exceptions observe the live owner prefix
    release temporary owner(s) at the original lexical boundary

copy/grow container element:
    copy/move each non-trivial owner according to the actual STL lowering
    copy the complete declared trivial payload, including dormant/indeterminate fields
    do not invent initialization while copying

serialize:
    publish only explicitly named live fields
    omit dormant entry gates, target vectors, padding and inactive scratch payload

destroy:
    explicitly release raw owners in body order
    destroy non-trivial members in reverse declaration order
    perform no cleanup for trivial dormant bytes
```

这解释了为什么“结果未使用”不等于“调用无副作用”，也解释了为什么不同编译器可以在同一
共享源码上显示不同数量的 zero stores、AddRef/Release 调用或 stack residue。

## 5. dead value / dead store 分类

### 5.1 纯算术/读取结果未消费，但读取和异常仍可见

- KRKR atlas 路径严格读取 `truncated_width` 和 `truncated_height` 并执行整数转换，随后丢弃
  两个数值。missing/wrong-type/转换异常仍是行为，不能删除读取。
- `calcMeshBounds` 在创建 tessellated vector 后仍从输入构造一个独立 accessor；bounds 循环不读
  accessor 的值，但 conversion、Object acquire、Release 和异常边界都保留。
- `calcMeshBounds` 对 `left` 做两次相同 Dictionary publication。第二次通常只覆盖等值，仍可能
  触发 setter/status/callback 行为，不能折成一次。
- physics pass 将三个 outer-force controller 写入同一 scratch buffer。scratch 最终数值不消费，
  controller 的 queue/state/current 数组更新仍是主副作用。
- wind pool 满时仍消耗 chance RNG draw，但不消耗 y RNG draw；“没有生成粒子”不等于 no-op。

### 5.2 dead initial store 与编译器消除

Engine `_dirty` 的共享源码默认值是 `false`，构造末段在每个 direct-controller seed 前写 `true`。
Android 两端保留初始 zero store；两个 iOS 优化体可删除这次被后续覆盖的 store。这里应复刻源码
成员初值，而不是根据某一 iOS 产物删掉 initializer，也不能把 Android store误判成额外状态。

Android arm64 的 dormant variable-binder clone、Android-only legacy load residual helper、某些
autoload count getter和 releaseTargetTexture helper属于 dead-strip/inline 后的机器函数 disposition，
不是要求本地复制第二套活跃业务对象。它们的来源字段/调用残余已保留；产品内无 caller 的机器
clone 不应伪造成新的共享源码调用链。

## 6. no-op AddRef/Release 与临时 owner

### 6.1 `calcMeshBounds` retained unused accessor

四端都先 CopyRef 输入 Variant、执行 `ToObject`/取得 dispatch owner，再销毁 Variant temporary；
accessor owner活到 callback 尾并 Release。Android arm64 内部入口的 `sub_A0DEE0`、Object acquire、
`sub_A0E078` 与末尾 vtable Release序列，与其他三端的显式 Variant copy/dtor和 dispatch Release
一致。本地 `ncbPropAccessor unusedInputAccessor(flatControlPoints);` 精确保留该 lexical lifetime。

### 6.2 “最终引用数不变”仍不是可删除操作

下列模式在正常尾部可能形成净零 refcount delta，但仍不能删：

- Variant/ttstr 按值参数或局部 CopyRef 后立即离开作用域；
- Object 和 ObjThis 指向同一 dispatch 时分别 AddRef、分别 Release；
- source/cache list copy 先 AddRef 新 node 的 Variant/string owners，再销毁旧 node；
- accessor 先取得独立 Object owner，再销毁转换用 Variant；
- temporary texture/map candidate在 duplicate path先 retain 后 release。

AddRef/Release 本身可触发 BeforeDestruction、对象复活、嵌套 Release、析构回调或线程竞态；中间
allocation/conversion也可抛。故不能用“净引用数为零”将整个序列化简为 borrow。完整 refcount根和
删除 thunk 已由 `MP-L16/MP-B10` 审计，本项确认这些 no-op-shaped 临时步骤没有被本地高层代码删除。

## 7. 未初始化局部与成员矩阵

| 范围 | 明确写入 | 保持未初始化 | 第一个读/传播边界 | 本地 disposition |
|---|---|---|---|---|
| Point/Circle/Rect/Quad record | type discriminator | `double[15]` | geometry getters/contains/copy | user storage + only type write；不补零 |
| Player | owner/container默认和显式 scalar defaults | emote index、tag cursor/time、cached total/loop、tail dispatch residual | gated motion/load paths或dead helper | 成员无 initializer |
| MotionNode | owners与各 live默认 | cameraFov、LayerGetter bytes、shapeAABB、feedbackTimespan、64B record、trailing word |各自 gate、default copy | user-provided ctor +显式 byte records |
| Angle/Var/Blink tracks | idle/current/target或metadata | start/invDuration/pow/phase及track span/accum tail | setup/restore后 active step | 只初始化已证明字段 |
| selector outer entry | controller owner、empty label、empty targets | independent enqueue gate | setter/query可读；公开同步可覆盖 | entry `flag` 无 initializer |
| spring wrappers | anchor和可用 currentForce前缀 | short count force tail、skipped-step outputs | variable map publication | raw locals保持默认初始化 |
| SourceCache Entry | Variant/ttstr owners、byteWeight、blendMode | missing-color时 colors[1..3] | equality/list copy/bake/tint | `Entry entry;`，只写 colors[0] |
| ObjSource/PSB resource | resource pointer；成功时 size | null chunk-data时 size |原构造表达式的null gate | size raw local；仅编译器边界避免错误优化 |
| Bezier evaluator | 16 control points和U/V weights | x/y `+=` accumulators |第一个 control point累加 | `tTVPPointD result;` 无 value-init |
| anchor phase |每个合格node的live state | function-scope carried RGB base first use |equal/default-blend branch | raw `double`，不按node reset |

### 7.1 Bezier 的寄存器/栈差异

Android arm64 的内部 `calcBezierPatch` 入口从 stack slot装入一个 accumulator，另一个沿 floating
register residue继续 FADD；Android armv7和两端 iOS以不同 stack/register分配表现同一 `result.x +=`
和 `result.y +=` 源码。各端具体残值不是平台常量，不能选择其中一端补成稳定 seed。

### 7.2 embedded UTF-16 NUL lowercase

`AsLowerCase` 先按 stored `Length` 分配目标，随后取得独立 buffer；四端 lowercase loop均以
UTF-16 NUL终止，只处理 ASCII `A..Z`。输入 backing若在 stored Length中嵌入 NUL，copy停止后：

- 当前位置不补 terminator；
- stored Length不缩短；
- suffix未写，保留allocator内容；
- 后续 hash/equality/ordered compare仍按各自 NUL或stored-Length规则形成尖锐边界。

本地 `tTJSString::AsLowerCase` 已保持这个行为。`MP-B05` 的测试只锁定可确定的 embedded-NUL
hash/equality/ordering以及well-formed lowercase，不对未初始化suffix构造确定 expectation。

## 8. inactive tail 的 copy / serialize / destruct disposition

| 对象/容器 | inactive tail | copy/growth | serialize/publication | destruct |
|---|---|---|---|---|
| MotionNode | 64B dormant record + 4B trailing word | compiler-generated copy完整传播 | 无脚本字段 | trivial，无析构副作用 |
| SourceCache Entry | colors[1..3] missing-color tail | list copy完整复制，owner字段各自AddRef | bake/tint可消费全部4词 | Variant/ttstr逆序释放；int tail无动作 |
| PreparedRenderItem |多组trivial flag/rect/matrix/id | persistent item禁止copy；builder逐字段发布 | command list只读admitted字段 | vectors/Variants/strings逆序；POD无动作 |
| TriangleBatch | initial clip rect | 不复制 | first append先因其他key变化flush empty，再写clip | vectors释放；rect trivial |
| wind emitter | 128 slot的padding/life/y + gate padding | raw owner不复制 | active slot才发布life/y；kill只清active | raw delete，无per-slot析构 |
| selector deque entry | enqueue gate + empty targets | deque构造/搬移保持元素字段；targets owner正常 | selector state serialization明确省略两者 | targets vector和controller/label owner释放，gate trivial |
| KRKR atlas record | rect content size/BGRA tail在pass2前未写 | vector growth按平台成员顺序copy owner与trivial rect | pass2才消费/覆盖 | string/raw-node owner释放；BGRA不由record cleanup |
| Player final raw residual | uninitialized borrowed dispatch | Player不可copy | 活跃load path不发布/序列化它 | 不AddRef、不Release |

PreparedRenderItem constructor并非 whole-object memset：只构造三组 string backing、四个 vector、三枚
Variant tag以及少数 admission defaults。D3D TriangleBatch同理，只构造 vectors和明确 key defaults；
clip rect在首次可比较前由另一个 key变化保证先flush空batch并写入。给两者加 `{}` 或 member-wide
zeroing会改变共享源码结构，即使现有正常oracle看不到。

## 9. 四端差异分类

- **ABI**：pointer宽度、double自然对齐、Thumb bit、stack frame和exception ABI改变字段偏移与
  residue来源，不改变“未初始化”本身。
- **STL**：Android旧libstdc++与iOS libc++改变deque/list/vector/string的header、growth、candidate
  allocation和临时AddRef/Release时点；共享source container类型不变。
- **编译器/链接器**：whole/partial memset合并、dead initial store删除、helper inline、tail merge、
  function merge和dead-strip改变机器形态；不能倒推成不同source default。
- **真实source条件**：KRKR atlas member order受 `_LIBCPP_VERSION` 控制；wind constructor只在
  Android arm64保留out-of-line body，其他三端内联进同一setWind源码路径。
- **未知差异**：本项为零。所有 task-local形态均已归入上述类别；未初始化残值本身不是“未知
  平台语义”，而是明确的source UB边界。

## 10. 本地实现逐项对照

| 四端共同源码步骤 | 本地实现 | 结论 |
|---|---|---|
| Player选择性默认与final residual不初始化 | `cpp/plugins/motionplayer/Player.h`、`PlayerCore.cpp` | 匹配 |
| MotionNode user-provided ctor、copy-only record/word | `MotionNode.h` | 匹配 |
| geometry只写type、15 doubles不初始化 | geometry NCB实现 | 匹配 |
| controller live prefix初始化、curve tail不初始化 | `EmoteAngleController.h`、`EmoteVarController.*`、blink/eyebrow/mouth类型 | 匹配 |
| selector controller清零；outer entry gate不初始化 | `EmoteSelectorController.*`、`EmoteEngine.h` | 匹配；两层未混淆 |
| spring skipped path仍发布uninitialized outputs | `EmoteEngine.cpp::stepHairParts/stepBust` | 匹配 |
| SourceCache missing color只写slot0 | `SourceCache.h/.cpp` | 匹配 |
| resource size局部与null-path边界 | `PlayerResource.cpp`、`PSBFile.cpp` | 匹配；已标编译器边界 |
| Bezier unused accessor、duplicate left、uninitialized result | `MotionLayerExtensions.cpp` | 匹配 |
| anchor carried RGB base跨node且首次未初始化 | `PlayerUpdateAnchor.cpp` | 匹配 |
| PreparedRenderItem/TriangleBatch选择性构造 | `RuntimeSupport.h`、`MotionRenderBackend.h/.cpp` | 匹配 |
| wind只清active bytes和live tail | `EmoteWindEmitter.h/.cpp` | 匹配 |
| lowercase嵌入NUL留下未写suffix | `cpp/core/tjs2/tjsString.cpp` | 匹配 |

本项没有生产 C++ 修改，也没有新增依赖真实未初始化数值的测试。`git diff --check`、正式构建、
unit/runtime和native/ADB/Wasmtime差分由 `MP-V06`～`MP-V08`及对应差分任务统一执行。

## 11. 完成 disposition

- evidence status：`IMPLEMENTED`；
- task-local static gap：无；
- production semantic edit：无；
- test edit：无（有意不固化UB residue）；
- remaining verification：统一归 `MP-V01`～`MP-V08`；
- final acceptance：仍需 `MP-V09`～`MP-V16`和重新打开的最终分母审计收口。
