# MotionPlayer `D3DAdaptor::clearTargetTexture` 局部静态缓存、guard 与异常生命周期四参考复核（2026-08-17）

## 1. 结论

本轮从 `g_randomMemberHint_guess` 的物理后继继续追踪。最初看到的连续 4 个
pointer-sized/word-sized 零槽并不是一个四指针容器，也不是四个新的 TJS member hint；四端
fresh decompile、data xref 与异常落地页共同证明，它们是
`D3DAdaptor::clearTargetTexture` 内两个独立 function-local static value 及其 ABI guard：

```cpp
static iTVPRenderMethod *method =
    TVPGetRenderManager()->GetRenderMethod("FillARGB");
static const int colorId = method->EnumParameterID("color");
```

`method` 是 borrowed/raw pointer，`colorId` 是普通 32-bit int；两者都没有 owning holder，
也没有 process-exit destructor。每个 static 有自己的 guard：第一阶段失败只回滚 method guard；
第二阶段失败只回滚 color-ID guard，已发布的 method 保留。此前把 iOS 短 helper 暂候选为
“资源清理/析构”的判断被否定：它是 `__cxa_guard_abort` 后继续 unwind 的异常落地页。

生产源码原来的两个 function-local statics、disabled early return、target==source
`OperateRect` 和 fresh manager lookup 已与四端一致，本轮没有改变算法，只补齐所有权、异常、
查找次数与无本地锁边界的 provenance 注释。

## 2. 物理 BSS 映射

| 目标 | `g_randomMemberHint_guess` | random 后空洞 | method value | method guard | color-ID value | value 后空洞 | color-ID guard | 后继对象 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x1AB5300` | `0x1AB5304..08`, 4 B | `0x1AB5308`, 8 B | `0x1AB5310`, 8 B | `0x1AB5318`, 4 B | `0x1AB531C..20`, 4 B | `0x1AB5320`, 8 B | `0x1AB5328` |
| Android armeabi-v7a | `0x11117F8` | 无 | `0x11117FC`, 4 B | `0x1111800`, 4 B | `0x1111804`, 4 B | 无 | `0x1111808`, 4 B | `0x111180C` |
| iOS arm64 | `0x101B697C8` | `0x101B697CC..D0`, 4 B | `0x101B697D0`, 8 B | `0x101B697D8`, 8 B | `0x101B697E0`, 4 B | `0x101B697E4..E8`, 4 B | `0x101B697E8`, 8 B | `0x101B697F0` |
| iOS armv7 | `0x187D4C8` | 无 | `0x187D4CC`, 4 B | `0x187D4D0`, 4 B | `0x187D4D4`, 4 B | 无 | `0x187D4D8`, 4 B | `0x187D4DC` |

64 位两端使用 8-byte Itanium C++ ABI guard；32 位 ARM 两端使用 4-byte ARM EABI guard。
guard 的快速路径只读取 low byte/bit 0，但对象宽度不能据此误建成单字节 global。LP64 的
`colorId` 后 4-byte 是为了把第二个 guard 对齐到 8 bytes，不属于 `colorId`。

四端全部为 BSS zero-init；没有 static-init bundle、`__cxa_atexit` 或析构器直接引用这些值。
后继地址均由 `Motion_doAlphaMaskOperation_guess` 消费，明确划定本轮对象边界，不能继续把后继
alpha-mask storage 合并进 D3D clear cache。

## 3. 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `D3DAdaptor::clearTargetTexture` | `0x6AB08C` | `0x57D184` | `0x100104130` | `0x10149C` |
| guard-abort/unwind landing | main body tail `0x6AB1DC/0x6AB1EC` | split range `0x57D270` | `0x10010425C`，第二入口 `0x10010426C` | SJLJ helper `0x101638` |
| render-manager accessor | `0x6930E4` | `0x570EA0` | `0x1000F3D90` | `0xF0834` |

参考文件均 stripped。新建 data 和 landing helper 继续使用 `_guess`，不宣称恢复作者原符号。
Android armv7 的 landing code 位于主函数后的 split range；该 physical range 后半还包含供
PC-relative load 使用的 literal-pool words，不能把那些 data xref 误判成额外 cleanup block。

## 4. 共同控制流与调用链

四端归一后的 source-level 控制流是：

```text
if (!self.clearEnabled)
    return

guard(method):
    manager0 = TVPGetRenderManager()
    method = manager0->GetRenderMethod("FillARGB", nullptr)

guard(colorId):
    colorId = method->EnumParameterID("color")

method->SetParameterColor4B(colorId, uint32(color))
rect = { 0, 0, targetTexture->width, targetTexture->height }
manager1 = TVPGetRenderManager()
manager1->OperateRect(method,
                      targetTexture, targetTexture,
                      rect, emptyTextureRectArray)
```

对应虚表 byte offset 在 64/32 位两端按 pointer size 缩放：

| 调用 | 64-bit vtable offset | 32-bit vtable offset |
|---|---:|---:|
| render manager `GetRenderMethod` | `+56` | `+28` |
| method `EnumParameterID` | `+16` | `+8` |
| method `SetParameterColor4B` | `+56` | `+28` |
| render manager `OperateRect` | `+160` | `+80` |

enabled 的首次成功调用会执行两次 manager accessor：一次初始化 method，一次提交
`OperateRect`。之后 method 和 color ID 都走 guard fast path，每次 enabled clear 只执行提交前
的第二次 accessor。`clearEnabled == false` 时两只 guard 都不触达，因此 disabled 调用不会提前
冻结 render manager 或 method identity。

## 5. 数据流、矩形和提交 ABI

四端共同执行以下顺序：

1. 把显式 `color` 参数原位传给 `SetParameterColor4B`；对象成员 `_clearColor` 不参与该路径；
2. 从当前 `_targetTexture` 读取 width/height，构造 `{0, 0, width, height}`；
3. 再次取得当前 render manager；
4. `OperateRect` 的 target 与 source/reference 参数都传同一个 `_targetTexture`；
5. 最后一个参数是空的 `tRenderTexRectArray`，不是包含 texture/rect element 的数组；
6. 返回值均被忽略，函数本身返回 `void`。

四端都没有 method、target texture 或第二次 manager accessor 的本地 null guard。因而这是
trusted-engine boundary：若 `GetRenderMethod` 正常返回 null，第一只 guard仍会成功发布 null，
随后第二阶段的虚调用会失败；若 `_targetTexture` 为 null，则 method 参数已经被写入后才在尺寸
读取处失败。本地不能增加 early return 来改变这一顺序。

## 6. 两级 publication 与异常回滚

两个 statics 的 guard 完全独立，publication 顺序为：

```text
acquire method guard
  -> call manager accessor/GetRenderMethod
  -> store method value
  -> release method guard

acquire color-ID guard
  -> call method/EnumParameterID
  -> store color-ID value
  -> release color-ID guard
```

异常路径在四端都闭合为 `__cxa_guard_abort(currentGuard)` 后继续 unwind：

- method 初始化抛出：method guard abort；两只 value 仍未形成可用的完整 cache；下一次 enabled
  clear 从第一阶段重试；
- color-ID 初始化抛出：只 abort 第二只 guard；method value 和第一只 released guard 保持不变；
  下一次 enabled clear 直接跳过第一阶段，只重试 color-ID；
- 两阶段都 release 后，后续 `SetParameterColor4B`、尺寸读取、manager accessor 或
  `OperateRect` 抛出时不触碰任何 guard，已发布 cache 永久保留。

Android arm64 把两个 landing entry 保留在主函数 physical tail；Android armv7/iOS arm64
使用 split landing range；iOS armv7 使用 SJLJ call-site state：state 0/1 选择 method guard，
state 2 选择 color-ID guard，state 3 进入 terminate/abort 路径。实现形式不同，source-level
回滚边界相同。

## 7. 所有权、静态析构与并发边界

`method` 只是 render manager 返回的 singleton/borrowed pointer；四端都没有 AddRef、Release、
shared_ptr/control block 或 owner wrapper。`colorId` 是 int。两者都是 trivially destructible，
因此：

- D3DAdaptor instance 析构不会清空 cache；
- 多个 D3DAdaptor instance 共享同一对 function-local statics；
- process/static teardown 不会释放 cached method；
- iOS/Android landing helper 只处理未完成初始化，不是 exit destructor。

guard 给首次初始化提供 ABI 级同步；release 后两个 cached value 只读。四端函数本体没有为
每次 clear 增加 mutex 或 snapshot。尤其 `SetParameterColor4B` 会修改共享 method 对象，随后才
调用 `OperateRect`；本函数自身不保证两个并发 clear 的“设置颜色 + 提交”原子性。更深层
render manager/method 是否串行化不由这段代码证明，因此 portable 复原不能凭空在这里加锁，
也不能把静态缓存改成每实例字段来规避跨实例共享。

## 8. data-xref 拓扑与旧候选裁决

按四个 value/guard 地址重新查询全部 data xref 后：

- method value 和 color-ID value 的语义 consumer 只有 clear 主体；
- guard 除 clear 主体外，只被对应 guard-abort landing entry 触达；
- 没有 registrar、static initializer、atexit destructor 或其他播放器路径访问这四槽；
- Android armv7 额外 raw xref 来自主函数后的 literal pool，并非额外消费者；
- 下一物理 word 已转入 `Motion_doAlphaMaskOperation_guess`，不是第三只 D3D clear static。

因此最初“四连续指针/可能带所有权容器”的候选彻底否定。准确的源结构是两个局部静态声明，
只是 linker/ABI 把每个 value 与 guard 连续排入 BSS；物理邻接不表示作者声明了一个 struct 或
array。

## 9. 本地源码对照

`cpp/plugins/motionplayer/D3DAdaptor.cpp` 原有生产表达已经吻合：

- `_clearEnabled` early return 位于两个 static 之前；
- `method` 与 `colorId` 是两条独立 static declaration；
- method 是 raw pointer，未引入所有权；
- explicit argument 经 `uint32` 传给 `SetParameterColor4B`；
- rectangle 覆盖完整 target texture；
- 提交前 fresh `TVPGetRenderManager()`；
- target/source identity 相同，texture-array 为空。

本轮只扩展注释，明确 guard-abort retry、无 exit dtor、首次/后续 lookup 次数、null/trusted
boundary 与无本地锁；不把 ABI guard storage 或 landing helper硬编码进 portable C++。

## 10. Recovery IDB 回写与验证

四份 recovery IDB 已完成：

- 新建 16 个独立 typed data items：每端 method pointer、method guard、color-ID int、color-ID
  guard；
- 新恢复/命名 3 个 split guard-abort helper/range；Android arm64 的两个 landing entry 位于
  已有 clear function tail，仅补行注释；
- 新增 25 条 function/data/landing 注释和 4 个书签；
- 对 7 个 clear/landing function 强制重新反编译并回读；
- 四库均原位保存成功。

本轮源码仅注释变化。验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` motionplayer 单测 TU syntax-only 均通过；只有仓库
  既有 `_tss` deprecated warning；
- Web Debug 与 Wasmtime Headless Debug 的 motionplayer objects/static library 和最终 Wasm
  链接均成功；
- Node `WebAssembly.Module` parse 成功，imports/exports 分别为 `539/69`、`538/69`；
- `llvm-objdump -h` 成功读取两份 wasm 的全部 section；
- Web/Headless CTest 都以 exit 0 返回，但两个配置当前都没有注册运行时测试；
- `git diff --check` exit 0，输出只有工作树既有 LF/CRLF 提示。

V188 产物与 V187 完全相同：

| 配置 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | 85,647,577 B | 539 / 69 | `0x1BD24` | `0xD5B2` | `0x1A407D5` | `0x5A3F37` | `0x3184928` |
| Wasmtime Headless Debug | 84,994,718 B | 538 / 69 | `0x1BA43` | `0xD5DA` | `0x19E8783` | `0x5A1187` | `0x31407BE` |

byte size、全部 section 和公开 import/export surface 的零变化符合 comment-only 生产改动范围。

## 11. 后继边界

四端 D3D clear cluster 结束后，下一批有引用的 BSS word 都进入
`Motion_doAlphaMaskOperation_guess`：A64 `0x1AB5328`、A32 `0x111180C`、I64
`0x101B697F0`、I32 `0x187D4DC`。该后继包含自己的缓存/guard 或 hint family，必须作为独立
纵切面重新做四端 decompile/xref/异常边界，不能沿用本轮的 `FillARGB` 命名。
