# MotionPlayer resolveRenderSource 三槽 hint family 与 owner tree 四参考复原（2026-08-16）

## 范围与结论

本纵切面从 V156 particle Array `erase` 的紧邻下一地址继续，对四份
`reference/binaries/` fresh 反编译 `Player_resolveRenderSource_guess`，并重新读取后续 data
xref/consumer set。四端共同证明紧邻区不是旧预告中的 `loadSource / src / assignImages`，而是：

```text
loadSource / blendMode / assignImages
```

三个槽都是独立、零初始化、进程级 4-byte member-hint：

- `loadSource` 只由 resolver fallback 消费；
- `blendMode` 由 resolver descriptor read 和另外六条 render/query builder 路径共享；
- `assignImages` 由 resolver work-Layer call 和另外三条 Layer/adaptor 路径共享；
- 第四槽已经进入 `Player_dispatchPendingEvents_guess` 的 `onSync`，因此本 family 在 12 bytes
  后闭合。

resolver 的 portable 算法和 accessor owner tree 已在 V128 恢复正确；本轮 fresh 四端复审没有
发现需要改变其控制流。需要修复的是 portable 全局声明/定义仍把 `blendMode`、`loadSource`、
`assignImages` 分散放在旧的语义分组中，掩盖了原始源码/链接区的三槽相邻结构；另外 V156
专项文档把中槽误写成了 `src`。本轮已重排 globals、修正文档并增加准确 call ABI/identity
探针。符号仍来自 stripped binary，保留 `_guess`；绝对地址只写在本文和 recovery IDB。

## 三槽精确映射与边界

| idx | recovered symbol / member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---:|---:|---:|---:|
| 0 | `loadSourceMemberHint_guess` (`loadSource`) | `0x1AB5444` | `0x11118E0` | `0x101B6990C` | `0x187D5B0` |
| 1 | `blendModeMemberHint_guess` (`blendMode`) | `0x1AB5448` | `0x11118E4` | `0x101B69910` | `0x187D5B4` |
| 2 | `assignImagesMemberHint_guess` (`assignImages`) | `0x1AB544C` | `0x11118E8` | `0x101B69914` | `0x187D5B8` |

三个槽严格满足 `base + index * 4`。紧邻下一双槽为：

| next member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | consumer |
|---|---:|---:|---:|---:|---|
| `onSync` | `0x1AB5450` | `0x11118EC` | `0x101B69918` | `0x187D5BC` | `Player_dispatchPendingEvents_guess` only |
| `onAction` | `0x1AB5454` | `0x11118F0` | `0x101B6991C` | `0x187D5C0` | same |

IDA 在部分端把 UTF-16 字符串错误渲染成单字节 `"l"`、`"b"` 或 `"o"`；literal bytes、其余
平台和 dispatch call 位置共同确认真实成员分别为 `loadSource`、`blendMode`、`onSync` /
`onAction`。hint identity 依据最终 address argument 与 data xref，而不是错误的字符串预览。

## resolver 函数与三次 dispatch

| role | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_resolveRenderSource_guess` | `0x6BEF50` | `0x58AD94` | `0x1001143E0` | `0x111E08` |
| fallback `loadSource` call | `0x6BF058` | `0x58AE1A` | `0x1001144BC` | `0x111EFE` |
| fast `blendMode` GetValue | `0x6BF12C` | `0x58AE82` | `0x100114564` | `0x111FA8` |
| fast `assignImages` call | `0x6BF47C` | `0x58AF14` | `0x100114630` | `0x11205C` |

64-bit ABI 通过 hidden X8 sret 返回 `tTJSVariant`，32-bit ABI 把 result-storage pointer 作为
首个参数。四端都在函数入口把 result type 初始化为 Void：

| target | result Void initialization |
|---|---:|
| Android arm64 | `0x6BEF88` |
| Android armv7 | `0x58ADB0` |
| iOS arm64 | `0x100114418` |
| iOS armv7 | `0x111E4E` |

随后 `loadSource` 或 `assignImages` 直接把该 caller-owned result storage 传给脚本 dispatch；
返回的 Variant 不是从某个 accessor owner 临时对象搬运出来，也不会因为 cleanup work/color/
descriptor 或 cache receiver 而提前析构。

## consumer set

### `loadSource`

fresh xref 在四端都只有 resolver 一条真实代码 consumer：

| target | hint materialization refs |
|---|---|
| Android arm64 | `0x6BF034`, `0x6BF03C` |
| Android armv7 | `0x58AE02`, `0x58AE08`（另有函数尾 literal-pool data xref） |
| iOS arm64 | `0x10011449C` |
| iOS armv7 | `0x111EE0`, `0x111EEC` |

### `blendMode`

四端归并 containing function/tail chunk 后都有同一七条语义 consumer：

1. `Player_resolveRenderSource_guess`；
2. `Player_buildRenderCommands_guess`；
3. `Player_renderToCanvas_guess`；
4. `Player_renderAccurateSeparateLayerAdaptor_guess`；
5. `Player_calcViewParam_guess`；
6. `Player_getCommandList_guess` 路径；
7. `Player_buildPrivateMotionGLLCommands_guess`。

代表性四端 consumer 地址如下；Android arm64 的 getCommandList 大函数 tail chunk 被 IDA 错归
到 `EmotePlayer_getCommandList_guess` 小 thunk，仍由实际 xref head `0x6D1060` 证明该路径：

| path | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| resolver | `0x6BEF50` | `0x58AD94` | `0x1001143E0` | `0x111E08` |
| build commands | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| canvas | `0x6C4820` | `0x58E2CC` | `0x1001186E0` | `0x11653C` |
| accurate SLA | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| calc view | `0x6CE908` | `0x594958` | `0x1001201CC` | `0x11EED4` |
| get command list | xref `0x6D1060` | `0x595FF0` | `0x100121EB0` | `0x120CF8` |
| private GLL | `0x6DBB18` | `0x59CB20` | `0x10012B7D0` | `0x12A304` |

这说明 `blendModeMemberHint_guess` 虽然在 resolver 三槽的中间，绝不是 resolver-private cache；
portable 只能移动同一个 global 的声明/定义，不能创建一个新的 resolver blendMode slot。

### `assignImages`

四端共同有四条语义 consumer：

| path | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `SeparateLayerAdaptor_assignFromAdaptor_guess` | `0x6A965C` | `0x57C814` | `0x10010347C` | `0x100874` |
| resolver | `0x6BEF50` | `0x58AD94` | `0x1001143E0` | `0x111E08` |
| accurate SLA | `0x6C7088` | `0x590468` | `0x10011A9E8` | `0x118D70` |
| update after draw | `0x6CBBB8` | `0x59327C` | `0x10011E6CC` | `0x11CF20` |

因此这里同样复用既有 `assignImagesMemberHint_guess`，不为 fast path 复制私有 hint。

## fallback 数据流、对象所有权与边界

fast-path gate 在四端严格为：

```text
sourceObject.Type == Object
&& internalRenderLayer.Type == Object
&& sourceObject.AsObjectNoAddRef() == internalRenderLayer.AsObjectNoAddRef()
```

只比较 Variant type 和 Object dispatch pointer，不比较 objthis，也没有 non-null guard。两个
typed-null Object 会被判相等并进入 fast path；portable 不得增加“更安全”的 null gate。

gate 为 false 时，四端 fallback 同构：

1. copy-construct Player 持久 `_sourceCacheObject` 到临时 Variant；
2. `AsObject()` 取得 retained raw cache receiver；
3. 立即析构步骤 1 的 Variant，receiver AddRef 仍存活；
4. 先 copy-construct caller 的 `sourceObject` 参数 Variant；
5. 再 copy-construct Player 持久 `_sourceDescriptor` 参数 Variant；
6. 对 cache receiver 执行 flags=0 的
   `loadSource(sourceObject, descriptor)`，hint=slot0，result=caller sret，argc=2，
   objthis=receiver；
7. ordinary HRESULT 被忽略；按 descriptor→source 逆序析构参数；
8. Release cache receiver，返回 result。

四端清理地址：

| target | descriptor dtor | source dtor | cache Release |
|---|---:|---:|---:|
| Android arm64 | `0x6BF060` | `0x6BF068` | `0x6BF078` |
| Android armv7 | `0x58AE1E` | `0x58AE24` | `0x58AE2E` |
| iOS arm64 | `0x1001144C4` | `0x1001144CC` | `0x1001144DC` |
| iOS armv7 | `0x111F02` | `0x111F08` | `0x111F18` |

脚本普通失败仍返回它写入 sret 的值；异常则沿 C++ unwind 逆序清理。回调可重入替换 Player 的
持久 `_sourceCacheObject`/descriptor，但当前调用继续使用已 retained 的旧 receiver 与两个参数
副本。

## fast path owner tree 与提交顺序

gate 为 true 时，四端仍与 V128 结论一致：

1. 从持久 descriptor Variant 构造 descriptor `ncbPropAccessor`；
2. flags=0、slot1 读取一次 `blendMode`，ordinary status ignored，无 HasValue probe；
3. 从持久 color Variant 构造 color accessor，按 numeric index 0..3 各 flags=0 读取一次
   Integer，无 named hint、无 probe；
4. 从持久 work-Layer Variant 构造 work accessor；
5. copy-construct持久 primary/internal Layer Variant为单个参数；
6. `work.assignImages(primary)` 使用 flags=0、slot2、caller sret、argc=1、objthis=work；
7. 参数 Variant 在调用后析构；然后 work 按 height→width 各执行 hinted HasValue，非负 status
   才做第二次 flags=0 Integer get，缺项保持 0；
8. 对 work Layer 应用四角 tint；
9. 正常 cleanup 严格为 work→color→descriptor，result 独立返回。

cleanup 地址继续为：

| target | work Release | color Release | descriptor Release |
|---|---:|---:|---:|
| Android arm64 | `0x6BF588` | `0x6BF598` | `0x6BF5B4` |
| Android armv7 | `0x58AFA0` | `0x58AFB6` | `0x58AFCE` |
| iOS arm64 | `0x1001146F8` | `0x100114710` | `0x10011472C` |
| iOS armv7 | `0x11212A` | `0x11213C` | `0x112152` |

descriptor owner 跨越 color/work 全部操作，color owner 跨越 work `assignImages`、尺寸读取和 tint；
不能在最后一次直接读取后提前 Release。回调重入修改持久 Variants 时，当前栈上的三个 accessor
仍继续持有各自旧 dispatch。普通 dispatch failure 不形成跳转 gate，conversion/脚本异常才走
unwind；已完成的 result 或 Layer mutation 不回滚。

## portable 源码与测试

- `MotionDispatch.h`：从早期 render-key 组移出 `blendModeMemberHint_guess`，从旧 SourceCache
  组移出 `loadSourceMemberHint_guess` / `assignImagesMemberHint_guess`，在 V156 两槽后按四端
  精确顺序声明三槽 family；
- `RuntimeSupport.cpp`：按相同顺序移动三只既有零初始化 `tjs_uint32` 定义；没有创建新 slot，
  所有旧 consumer 仍引用同一对象身份；
- `SourceCache.cpp`：resolver 控制流、参数顺序、owner 与 cleanup 均已匹配，未做语义改动；
- V156 文档把相邻中槽从过时的 `src` 更正为 `blendMode`；
- `render-source scalar accessors...` 探针由测试局部 hint 改为真实共享
  `blendModeMemberHint_guess`；
- 新增 `RenderSourceHintCallRecorder` 与
  `render-source adjacent hint family preserves call ABI and shared identities`：断言
  loadSource 两参数顺序、assignImages 单参数、flags=0、非 null同一 result storage、
  receiver==objthis、准确 hint pointer、ordinary failure仍可写 result，并断言三槽 pairwise
  distinct、且不与 V156 erase slot alias。

## IDB 回写

四份 recovery IDB 均已完成：

- 对三槽完整 12-byte range 先整体 `undefine`，清除 iOS 端旧的 24-byte assignImages 聚合 item
  和 Android 端未命名 BSS 表达；
- 按升序建立三个独立 `unsigned int` data item，命名为
  `loadSourceMemberHint_guess`、`blendModeMemberHint_guess`、
  `assignImagesMemberHint_guess`，每项 size=4；
- 写入 resolver-private/shared consumer 注释，向 resolver function comment 追加三槽调用与 owner
  cleanup 说明；
- bookmark 统一为
  `V157 complete 3-slot resolveRenderSource loadSource/blendMode/assignImages member-hint family`；
- 强制刷新四端 resolver Hex-Rays cache；fresh decompile 在三次调用处直接显示三个新名字；
- fresh entity readback 每库恰好三项、地址连续、size=4；四份数据库均已原位保存，二进制输入
  字节未修改。

## 验证

- 普通 motionplayer test TU 语法检查通过；仅既有 `_tss` warning。
- `KRKR2_WASMTIME_HEADLESS=1` test TU 语法检查通过；仅同一既有 warning。
- `Web Debug Build` 以显式 Emscripten toolchain 干净配置并完整构建通过；`index.wasm` 为
  85,648,312 bytes。
- `Wasmtime Headless Debug Build` 同样完整构建通过；第一次工具等待超时后遗留的同一 `em++`
  进程短暂锁住 `index.wasm`，该进程自然完成后续跑为 `ninja: no work to do`，没有源代码或
  符号链接错误；最终 `index.wasm` 为 84,995,453 bytes。
- Node `WebAssembly.Module` 解析成功：Web 539 imports / 69 exports，headless 538 imports /
  69 exports。
- `llvm-objdump -h` 成功解析两份 Wasm section table。
- 两个 build tree 的 CTest 均报告 `No tests were found!!!`；不虚报 runtime CTest 执行。
- `git diff --check` 在本文完成后执行；工作区 LF→CRLF 提示不属于内容错误。

## 下一纵切面

V158 应从三槽紧邻下一地址开始，fresh 审计 `Player_dispatchPendingEvents_guess` 的
`onSync / onAction` 双槽、统一 pending-event vector 的 live-end traversal、result Variant
复用、参数复制/析构和 callback 重入边界。旧 IDB 的字符串渲染在部分端已经显示为单字节
`"o"`，必须用四端 literal bytes 与事件 kind 控制流共同定名，不能沿用该错误预览。
