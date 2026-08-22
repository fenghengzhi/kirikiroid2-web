# MotionPlayer shared render selector 双函数、12 组手工 null cache 与边界生命周期四参考复核（2026-08-17）

## 1. 结论

`PrivateMotionGLL::Draw_GPU` 与 Player direct/D3D batch renderer 共用的 named render-method
selector，此前只闭合了 blend→method name 映射和“process-static cache”这一高层结论。本轮对
四份 selector 完整函数、两个 caller family、30 个静态 value slot 和全部 data/code xref 做
fresh 复核后，恢复出更精确、且会改变异常和并发边界的源结构：

1. 原版有两个独立函数：ordinary selector 与 alpha-test selector；没有一个接受
   `alphaTest` 参数的统一 selector。
2. 两个函数各有 6 个 switch cache group。ordinary 每组是三个源码概念中的两个实际 value：
   `{raw method pointer, int colorId}`；alpha-test 每组是
   `{raw method pointer, int colorId, int alphaThresholdId}`，总计 12 个 method、12 个 color ID、
   6 个 threshold ID，即每库 30 个 BSS-zero slot。
3. 这些都只是**零初始化的 function-local static value**，没有动态初始化 guard，也没有
   `__cxa_guard_acquire/release/abort`。method pointer 自身是手工 lazy-init null sentinel。
4. miss block 的顺序严格是：取得 manager、`GetRenderMethod`、立即把 raw pointer 发布到静态
   method 槽、再枚举 color ID，alpha-test 最后枚举 `alpha_threshold` ID。method 发布后任一
   ID 枚举失败都不会重试初始化；后续调用看见 method 非空，直接使用仍为 BSS 零或已部分写入
   的 ID。
5. 每次 ordinary selector 命中后都写 packed color；alpha-test 先写 packed color，再写固定
   threshold 64。两个 selector 都返回 raw method，没有 AddRef/Release、owner wrapper、退出
   析构、null fallback 或 software mapping。
6. portable 旧实现把两个函数压成 `RenderMethodCache_guess ordinary[6]/tested[6]`，且把 ID
   默认初始化为 `-1`。正常单线程成功路径结果相同，但 LP64 物理布局、函数调用链、初次发布
   窗口以及枚举失败/竞争后的 ID 值都不一致。本轮已恢复成两个函数和 12 组显式分支局部
   statics，ID 明确为零初始化。

这一结论也纠正了 V190 开始时的暂定 guard 假设：与 V189 alpha-mask 的 guarded dynamic
static initializer 不同，本 selector family 完全依赖手工 `if (!method)`，不得类推 guard
回滚或线程安全保证。

## 2. 四参考函数与 caller 闭包

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| alpha-test selector | `0x6D9470` | `0x59AE64` | `0x100128D00` | `0x127F38` |
| ordinary selector | `0x6D9898` | `0x59B1FC` | `0x100129134` | `0x12827C` |
| batch method-key transition | `0x6D8E3C` | `0x59AA38` | `0x1001287BC` | `0x127AF6` |
| `PrivateMotionGLL::Draw_GPU` | `0x6DA94C` | `0x59BFB4` | `0x10012A9B4` | `0x129724` |

fresh code xref 在每库都恰好给每个 selector 两个 caller：batch transition 与
`PrivateMotionGLL::Draw_GPU`。没有 registrar、static initializer、destructor 或第三条调用链。

batch transition 的共同顺序为：

```text
if (packedColor, blend, alphaOpAdd, alphaTest key unchanged)
    return batch.cachedMethod

flush pending triangles
publish all four new key fields
method = alphaTest
    ? selectAlphaTestRenderMethod(blend, packedColor, alphaOpAdd)
    : selectRenderMethod(blend, packedColor, alphaOpAdd)
batch.cachedMethod = method
return method
```

所以 unchanged batch key 不会再次进入 selector，也不会重复写 method parameter；changed key
则在 selector 抛出前已经发布新 key，method 槽尚未更新。`PrivateMotionGLL::Draw_GPU` 不使用
batch key：每个 sourceTexture 非空 item 都按 item mask/stencil byte 选择 alpha-test 或 ordinary
selector，且第三参数 `alphaOpAdd` 固定为 false；selector 返回后的 null check 位于首次
lookup/ID enumeration 之后，不能保护首次 `GetRenderMethod` 返回 null 的解引用。

## 3. switch 映射与共享组

两个 selector 的 switch 完全同构，只差 `_AlphaTest` method name 后缀和第三个 ID/parameter
write：

| `blend` 输入 | `alphaOpAdd` 是否参与 | ordinary method | alpha-test method | cache group |
|---:|---|---|---|---|
| `1` | 不参与 | `PsAddBlend_color` | `PsAddBlend_color_AlphaTest` | Add |
| `2` 或 `5` | 不参与 | `PsSubBlend_color` | `PsSubBlend_color_AlphaTest` | Sub，共享同一组 |
| `3` | 不参与 | `PsMulBlend_color` | `PsMulBlend_color_AlphaTest` | Mul |
| `4` | 不参与 | `PsScreenBlend_color` | `PsScreenBlend_color_AlphaTest` | Screen |
| 其他 | false | `AlphaBlend_color` | `AlphaBlend_color_AlphaTest` | default |
| 其他 | true | `AlphaBlend_color_a` | `AlphaBlend_color_a_AlphaTest` | default-a |

production callers 传入的 blend 已取低四位，但 selector 自身只是对完整 int 做上述 switch。
因此 case 2 与 5 必须共享同一 method/ID identity，不能为 blend 5 另建第七组；case 1..5 又
完全忽略 `alphaOpAdd`。Player direct/D3D raw batch 当前传 `alphaOpAdd=true`，PrivateMotionGLL
传 false，故两个 default group 在完整生产调用图中都有意义。

## 4. 单组公共控制流和虚调用 ABI

ordinary group 的共同源级形状：

```cpp
static iTVPRenderMethod *method = nullptr;
static int colorId = 0;

if(!method) {
    method = TVPGetRenderManager()->GetRenderMethod(name);
    colorId = method->EnumParameterID("color");
}
method->SetParameterColor4B(colorId, packedColor);
return method;
```

alpha-test group：

```cpp
static iTVPRenderMethod *method = nullptr;
static int colorId = 0;
static int thresholdId = 0;

if(!method) {
    method = TVPGetRenderManager()->GetRenderMethod(name);
    colorId = method->EnumParameterID("color");
    thresholdId = method->EnumParameterID("alpha_threshold");
}
method->SetParameterColor4B(colorId, packedColor);
method->SetParameterOpa(thresholdId, 64);
return method;
```

对应 vtable byte offset 按 ABI pointer size 缩放：

| 调用 | 64-bit offset | 32-bit offset |
|---|---:|---:|
| render manager `GetRenderMethod(name, nullptr)` | `+56` | `+28` |
| method `EnumParameterID` | `+16` | `+8` |
| method `SetParameterColor4B` | `+56` | `+28` |
| method `SetParameterOpa` | `+64` | `+32` |

`GetRenderMethod` 的第二/附加 hint 参数为 null；这里没有 V189 的 mutable compile hint，也不
调用 `GetOrCompileRenderMethod`。manager lookup 只发生在 selected group 的 method 为 null 时。

## 5. 30 槽四端精确物理布局

物理 group 顺序四端完全一致：先 alpha-test 的
`default, Add, Sub, Mul, Screen, default-a` 六组，再 ordinary 的同序六组。

LP64 每个 function-local static 被单独按 8-byte 边界放置：method 为 8 B，ID 只写/读低
4 B，后 4 B 是 alignment gap。因此 alpha-test group 跨 24 B，ordinary group 跨 16 B。
32 位两端所有 pointer/int 均为 4 B，group 分别为 12/8 B。这一布局直接排除了 portable 旧
`RenderMethodCache {pointer,int,int}` 数组作为原始 source shape：该 struct 在 LP64 自然大小
只会是 16 B，不会产生 observed 24-B alpha-test stride。

### 5.1 Android arm64-v8a

| group | alpha-test method | color ID | threshold ID | ordinary method | color ID |
|---|---:|---:|---:|---:|---:|
| default | `0x1AB5590` | `0x1AB5598` | `0x1AB55A0` | `0x1AB5620` | `0x1AB5628` |
| Add | `0x1AB55A8` | `0x1AB55B0` | `0x1AB55B8` | `0x1AB5630` | `0x1AB5638` |
| Sub (`2/5`) | `0x1AB55C0` | `0x1AB55C8` | `0x1AB55D0` | `0x1AB5640` | `0x1AB5648` |
| Mul | `0x1AB55D8` | `0x1AB55E0` | `0x1AB55E8` | `0x1AB5650` | `0x1AB5658` |
| Screen | `0x1AB55F0` | `0x1AB55F8` | `0x1AB5600` | `0x1AB5660` | `0x1AB5668` |
| default-a | `0x1AB5608` | `0x1AB5610` | `0x1AB5618` | `0x1AB5670` | `0x1AB5678` |

predecessor 是独立的 `g_sharedD3DAdaptor_guess @0x1AB5588`（8 B）；selector cluster
为 `0x1AB5590..0x1AB5678`，下一 unrelated cluster 从 `0x1AB5680` 开始。

### 5.2 Android armeabi-v7a

| group | alpha-test method | color ID | threshold ID | ordinary method | color ID |
|---|---:|---:|---:|---:|---:|
| default | `0x11119F4` | `0x11119F8` | `0x11119FC` | `0x1111A3C` | `0x1111A40` |
| Add | `0x1111A00` | `0x1111A04` | `0x1111A08` | `0x1111A44` | `0x1111A48` |
| Sub (`2/5`) | `0x1111A0C` | `0x1111A10` | `0x1111A14` | `0x1111A4C` | `0x1111A50` |
| Mul | `0x1111A18` | `0x1111A1C` | `0x1111A20` | `0x1111A54` | `0x1111A58` |
| Screen | `0x1111A24` | `0x1111A28` | `0x1111A2C` | `0x1111A5C` | `0x1111A60` |
| default-a | `0x1111A30` | `0x1111A34` | `0x1111A38` | `0x1111A64` | `0x1111A68` |

predecessor `g_sharedD3DAdaptor_guess @0x11119F0` 为 4 B；cluster
`0x11119F4..0x1111A68`，下一 unrelated cluster 从 `0x1111A6C` 开始。

### 5.3 iOS arm64

| group | alpha-test method | color ID | threshold ID | ordinary method | color ID |
|---|---:|---:|---:|---:|---:|
| default | `0x101B69A30` | `0x101B69A38` | `0x101B69A40` | `0x101B69AC0` | `0x101B69AC8` |
| Add | `0x101B69A48` | `0x101B69A50` | `0x101B69A58` | `0x101B69AD0` | `0x101B69AD8` |
| Sub (`2/5`) | `0x101B69A60` | `0x101B69A68` | `0x101B69A70` | `0x101B69AE0` | `0x101B69AE8` |
| Mul | `0x101B69A78` | `0x101B69A80` | `0x101B69A88` | `0x101B69AF0` | `0x101B69AF8` |
| Screen | `0x101B69A90` | `0x101B69A98` | `0x101B69AA0` | `0x101B69B00` | `0x101B69B08` |
| default-a | `0x101B69AA8` | `0x101B69AB0` | `0x101B69AB8` | `0x101B69B10` | `0x101B69B18` |

predecessor `g_sharedD3DAdaptor_guess @0x101B69A28` 为 8 B；cluster
`0x101B69A30..0x101B69B18`，下一 unrelated cluster 从 `0x101B69B20` 开始。

### 5.4 iOS armv7

| group | alpha-test method | color ID | threshold ID | ordinary method | color ID |
|---|---:|---:|---:|---:|---:|
| default | `0x187D6B4` | `0x187D6B8` | `0x187D6BC` | `0x187D6FC` | `0x187D700` |
| Add | `0x187D6C0` | `0x187D6C4` | `0x187D6C8` | `0x187D704` | `0x187D708` |
| Sub (`2/5`) | `0x187D6CC` | `0x187D6D0` | `0x187D6D4` | `0x187D70C` | `0x187D710` |
| Mul | `0x187D6D8` | `0x187D6DC` | `0x187D6E0` | `0x187D714` | `0x187D718` |
| Screen | `0x187D6E4` | `0x187D6E8` | `0x187D6EC` | `0x187D71C` | `0x187D720` |
| default-a | `0x187D6F0` | `0x187D6F4` | `0x187D6F8` | `0x187D724` | `0x187D728` |

predecessor `g_sharedD3DAdaptor_guess @0x187D6B0` 为 4 B；cluster
`0x187D6B4..0x187D728`，下一 unrelated cluster 从 `0x187D72C` 开始。

四库对这 30 个地址各执行整数 readback，共 120 次读取，全部为 0。新建 data item 后，
entity readback 每库恰好 30 个 `MotionRenderSelector_*` 名：method size 为 ABI pointer size，
两个 ID 均 size 4；predecessor 也恢复为准确 pointer size。

## 6. 手工 publication、异常与 null 边界

这里没有 guard abort，也没有强异常保证。单个 alpha-test group 的 publication state 可达：

| 阶段 | method | color ID | threshold ID | 下一次是否重试 init |
|---|---|---|---|---|
| 初始 | null | 0 | 0 | 是 |
| `GetRenderMethod` 返回非空后 | 非空 | 0 | 0 | 否 |
| color enum/store 后 | 非空 | 实际值 | 0 | 否 |
| threshold enum/store 后 | 非空 | 实际值 | 实际值 | 否 |

具体边界：

- manager accessor 或 `GetRenderMethod` 抛出：method 尚未发布，下一次进入同组会重试；
- `GetRenderMethod` 正常返回 null：null 被写回，紧接着的 `EnumParameterID` 自然解引用失败；
  若运行环境能从该失败继续，method 仍为 null，下一次会再次 lookup；
- color ID 枚举抛出：method 已非空，color/threshold 仍为 0；下一次永久跳过 init，并以 ID 0
  调 parameter setter；
- alpha threshold 枚举抛出：method 与 color 已发布、threshold 仍为 0；下一次以实际 color ID
  和 threshold ID 0 提交；
- parameter setter 抛出：全部 cache value 保留；alpha-test 中 color setter 抛出时不会执行
  threshold setter，threshold setter 抛出时 color 修改已经发生；
- ordinary group 是同一协议的两阶段子集。

portable 旧 `colorId=-1/thresholdId=-1` 在正常初始化成功时被覆盖，因而长期隐藏了偏差；只有
异常或数据竞争观察到 partial state 时才会把 `-1` 传给 setter，而四参考明确要求 BSS 零。

## 7. 所有权、静态析构与并发行为

12 个 method 都是 `GetRenderMethod` 返回的 raw/borrowed pointer：四端没有 AddRef、Release、
smart owner、manager holder 或 atexit destructor。18 个 ID 是 trivial int。因此 cache：

- 跨 Player、D3DAdaptor 和 PrivateMotionGLL instance 共享；
- 不随任何对象析构清空；
- 一个 group 首次正常发布后永久绑定当时 manager 返回的 method identity；
- 两个 selector 和六个 branch 彼此独立，未触达 group 保持全零；
- `2/5` 是唯一由两个 blend 值共同触达的 group。

手工 null check 没有 ABI guard、mutex、atomic 或 thread-local：

- 多线程可同时看到 null，执行重复 lookup 并交错覆盖 method/IDs；
- method pointer 先发布，另一个线程可在 ID 仍为 0 时跳过 init并调用 setter；
- 即使 cache 已稳定，每次 selector 仍修改共享 method parameter；alpha-test 的 color write 与
  threshold write，以及参数写入与后续 draw submit，都没有由本函数提供原子性；
- 更深层 manager/method 是否串行化不由此证据证明，portable 不能自行加锁或改成每实例 cache。

这与 guarded function-local dynamic initialization 的标准线程安全语义显著不同，正是本轮
不能把 V189 结构复用到这里的原因。

## 8. portable 源码修正

修改覆盖：

- `MotionRenderBackend.h/.cpp`：删除 unified four-argument selector、`gpuMethodName_guess`、
  `RenderMethodCache_guess` 和两个 `[6]` 数组；恢复独立 ordinary/alpha-test 三参数函数；
- 两函数各自显式表达 6 个 switch branch-local cache group，共 12 个 method、12 个
  zero `colorId` 和 6 个 zero `thresholdId`；保留 method→color→threshold publication 与每次
  color→threshold parameter write 顺序；
- `TriangleBatch_guess::selectMethod_guess`：key changed 后按 `alphaTest` 路由两个独立函数，
  再保存 raw return；unchanged fast path不变；
- `PrivateMotionGLL.cpp`：先计算 packed color，再按 item stencil/mask byte 调两个独立函数，
  两条都传 `alphaOpAdd=false`；selector 后 null check、stencil 与 geometry 顺序不变；
- compiled comments 不含 reference absolute address。

结构审计得到精确计数：12 个 `GetRenderMethod`、12 个 color enum、6 个 threshold enum、
12 个 `SetParameterColor4B`、6 个 `SetParameterOpa`、12 个 method static、12 个 zero color-ID
static、6 个 zero threshold-ID static；旧 cache struct/array/name helper 均为 0。

## 9. Recovery IDB 回写

四份 recovery IDB 原先把相邻数据错误吞进过大的 aggregate：例如 A64 的
`g_sharedD3DAdaptor_guess` item 从 predecessor 一直跨越 selector cluster，iOS arm64 也有
1,352-byte 聚合，iOS armv7 predecessor 甚至只建成 1 byte。此次：

- 每库整体 undefine `g_sharedD3DAdaptor_guess` predecessor 到 selector end，再准确重建 1 个
  pointer predecessor + 30 个 selector typed item；共 124 个 data item，其中 selector 本身
  120 项；
- 统一命名 `MotionRenderSelector_<group>[_AlphaTest]_{method,colorId,
  alphaThresholdId}_guess`；
- 每库追加 35 条 selector/data/caller/boundary 注释，共 140 条，并添加 1 个 V190 bookmark；
- 每库强制重新反编译 alpha selector、ordinary selector、batch transition 与
  PrivateMotionGLL Draw，共 16 个 function readback；
- fresh pseudocode 四库都显示 semantic slot names，不再显示吞并后的
  `qword_/dword_/unk_ + index`；全文 guard-call readback 为 0；
- entity size、120 个初值、caller xref 和前后边界均重新读取；四库原位保存成功。

所有名字保留 `_guess`，因为参考文件 stripped；绝对地址只进入本文和 recovery IDB。

## 10. 编译、Wasm 与差分验证

验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 motionplayer unit-test TU syntax-only 均
  exit 0；只有仓库既有 `_tss` deprecated warning；
- Web Debug 完整构建/最终链接成功（7 steps）；
- Wasmtime Headless Debug 完整构建/最终链接成功（10 steps）；
- Node `WebAssembly.Module` 对两份当前 `index.wasm` parse 成功；
- `llvm-objdump -h` 成功读取两份完整 section table；
- Web/Headless CTest 都 exit 0，但两者当前均为 `No tests were found`，因此这里只报告双模式
  test TU 编译覆盖，不虚报 runtime unit-test 执行；
- scoped `git diff --check` 通过，只有工作树既有 LF/CRLF 提示。

V190 artifact：

| 配置 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | 85,654,197 B | 539 / 69 | `0x1BD23` | `0xD5B2` | `0x1A4219A` | `0x5A3FB7` | `0x31848C0` |
| Wasmtime Headless Debug | 85,001,338 B | 538 / 69 | `0x1BA42` | `0xD5DA` | `0x19EA148` | `0x5A1207` | `0x3140756` |

相对 V189 两份 artifact 都精确 `+1,964 B`：FUNCTION section `-1 B`，CODE
`+0x8B5`（2,229 B），DATA `-0xA0`（160 B），name `-0x68`（104 B）；GLOBAL 与其余
section不变，imports/exports 也保持 539/538 与 69。双配置完全相同的 delta 符合删除一个
generic name/helper/两组显式非零 aggregate、恢复两个 branch selector 和全零局部 statics 的
改动形状，且没有扩大 Wasm ABI surface。

headless 当前产物仍是 `out/wasmtime/debug/index.wasm`；旧时间戳的
`krkr2_wasmtime_guest.wasm` 不参与本轮尺寸、parse 或 section 结论。

## 11. 后继边界

四端 selector cluster 的 predecessor 已闭合为 Player draw 的 shared D3D-adaptor raw pointer。
紧邻后继 `0x1AB5680 / 0x1111A6C / 0x101B69B20 / 0x187D72C` 被 Point/
GeometryShape NCB 构造、注册、copy/析构等大量函数共同引用，显然已进入另一套 class-state /
owner cluster，不属于第 13 个 render method。V191 可从该四端同构后继开始，恢复 Point/
GeometryShape NCB state、对象布局、构造/复制/析构和注册生命周期；不能因物理邻接把它并入
selector cache。
