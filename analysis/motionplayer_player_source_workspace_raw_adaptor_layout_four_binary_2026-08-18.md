# Player rootContent 后 source workspace 与 raw adaptor 布局/生命周期（V251，2026-08-18）

## 1. 结论

V250 闭合到 `rootContent` 后，四份参考二进制继续给出相同的直接成员序列：

```text
tTJSVariant rootContent
tTJSVariant findSourceResourceManager
tTJSVariant sourceCacheObject
tTJSVariant sourceDescriptor
tTJSVariant internalRenderLayer
tTJSVariant sourceColors
tTJSVariant internalSourceWorkLayer
SeparateLayerAdaptor *renderSeparateLayerAdaptor
```

这里是七个连续 `tTJSVariant` owner（含 V250 末端的 `rootContent`），随后是一个 pointer-sized raw
owner；四个 ABI 在 raw pointer 结束处都直接进入 pending stealth motion string，没有未解释的 member
或 padding。portable `Player.h` 原来把六个 workspace Variant 分散到 class 后部，又把 adaptor pointer
放到更晚的 variable-label storage 附近，不能复现参考对象的物理布局、自动 member 析构或 constructor
failure unwind。本轮把它们全部移回真实位置。

## 2. 四端精确 offset

| 成员 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| rootContent | `+0x268` | `+0x1A0` | `+0x1F8` | `+0x160` |
| findSource ResourceManager | `+0x27C` | `+0x1AC` | `+0x20C` | `+0x16C` |
| source-cache ResourceManager | `+0x290` | `+0x1B8` | `+0x220` | `+0x178` |
| source descriptor Dictionary | `+0x2A4` | `+0x1C4` | `+0x234` | `+0x184` |
| primary internal Layer | `+0x2B8` | `+0x1D0` | `+0x248` | `+0x190` |
| source colors Dictionary | `+0x2CC` | `+0x1DC` | `+0x25C` | `+0x19C` |
| internal source work Layer | `+0x2E0` | `+0x1E8` | `+0x270` | `+0x1A8` |
| raw SeparateLayerAdaptor pointer | `+0x2F8` | `+0x1F4` | `+0x288` | `+0x1B4` |
| next pending stealth motion | `+0x300` | `+0x1F8` | `+0x290` | `+0x1B8` |

Variant native size是 Android 64-bit/iOS 64-bit 的 `0x14`、Android armv7/iOS armv7 的
`0x0C`；work-layer Variant 后按 pointer alignment 到 raw pointer。四端 pointer 末端与下一 string
均连续，所以这里不存在曾被 portable `_chara/_motionKey/_outline` 暗示的中间 owner。

## 3. constructor：两个独立 ResourceManager owners

constructor 对同一个输入 ResourceManager 连续执行两次独立 `CopyRef`，不是让第二个 member alias 第一个
Variant 的 storage：

| 目标 | first CopyRef | second CopyRef |
| --- | ---: | ---: |
| Android arm64 | `0x6CC274` / `+0x27C` | `0x6CC280` / `+0x290` |
| Android armv7 | `0x593688` / `+0x1AC` | `0x593690` / `+0x1B8` |
| iOS arm64 | `0x10011ECB0` / `+0x20C` | `0x10011ECBC` / `+0x220` |
| iOS armv7 | `0x11D5DE` / `+0x16C` | `0x11D5EC` / `+0x178` |

因此两个 Variant 各持有一个独立引用计数 ownership。第二次 `CopyRef` 抛出时，第一次已经完成的 owner
进入 unwind；不存在“构造一份后 bitwise copy”或单 owner/shared slot 行为。

随后 constructor 的共同操作是：

```text
sourceDescriptor = new Dictionary
internalRenderLayer = Void
sourceColors = new Dictionary
internalSourceWorkLayer = Void
sourceDescriptor["color"] = sourceColors
renderSeparateLayerAdaptor = nullptr
```

V259 对完整 constructor body 的四端复核进一步固定了后继顺序：上述
`sourceDescriptor["color"] = sourceColors` 成功后，constructor 才向早先已经 default-constructed
的 node deque 追加 synthetic root并复制默认 transform order；两个 Dictionary factory-return
owners一直存活到 root设置之后。旧 portable constructor 曾把 root append移到 Dictionary setup
之前，现已恢复为这一共同顺序。完整异常边界见
`motionplayer_player_constructor_initialization_dictionary_root_order_four_binary_2026-08-18.md`。

关键 tag/store anchors：

| 目标 | descriptor/layer/colors/work tags | raw pointer zero |
| --- | ---: | ---: |
| Android arm64 | `0x6CC2A8..0x6CC2B4` | `0x6CC5DC` |
| Android armv7 | `0x5936A6..0x5936B2` | `0x5938DA` |
| iOS arm64 | `0x10011ECD4..0x10011ECE4` | `0x10011EF44` |
| iOS armv7 | `0x11D60C` 起连续 group | `0x11D9CA` |

`descriptor.color` assignment 再为 colors Dictionary 增加一份脚本对象引用；它不把两块 Variant storage
合并。primary/work layer 则明确从 Void 开始，不在 Player constructor 中提前造 Layer。

## 4. lazy materialization 与 sticky partial commit

四端 materializer 只检查 primary `internalRenderLayer` 的 Variant type 是否仍为 Void。若是，顺序共同为：

```text
primary = new Layer
height = sourceCache.height
width  = sourceCache.width
primary.setSize(width, height)
work = new Layer
work.setSize(width, height)
... later setup ...
```

primary publication anchors：Android arm64 `0x6CB708`、Android armv7 `0x592FE4`、iOS arm64
`0x10011E368`、iOS armv7 `0x11CBA6`。共同边界是 primary Variant assignment 在 height/width getter、
`setSize` 和 work-layer factory 之前已经提交。

因此任意 later operation 抛出时：

- 已发布 primary Layer 不回滚；
- work Layer 可能仍为 Void，也可能已经发布；
- 下次调用因 primary 已非 Void而跳过整个 materializer，不会自动重试缺失步骤；
- 这是参考实现的 sticky half-initialized state，不能改成 transactional local owners 后一次性 commit。

descriptor/colors 的 persistent setup 同理逐项提交；constructor unwind 只负责已完成 member 的逆序析构，
不提供业务层回滚。

## 5. raw SeparateLayerAdaptor owner

raw slot 在 constructor 中明确清零。lazy builder 先分配内存并运行 adaptor constructor，只有 constructor
成功后才把指针写入 Player slot，所以 adaptor construction failure 不会留下悬挂的已发布 pointer。

Player destruction 则不是 `std::unique_ptr::reset()` 的等价时序。四端共同执行：

```text
if (renderSeparateLayerAdaptor != nullptr) {
    renderSeparateLayerAdaptor->~SeparateLayerAdaptor()
    operator delete(renderSeparateLayerAdaptor)
    renderSeparateLayerAdaptor = nullptr
}
// only then begin automatic Variant member teardown
```

raw-owner dtor anchors：Android arm64 `0x6CCF28`、Android armv7 `0x593C60`、iOS arm64
`0x10011F2F0`、iOS armv7 `0x11DD54`。slot clear发生在 pointee destructor/free 之后；若要一比一保留
可观察重入/诊断边界，就不能把 slot 提前交换成 null。

## 6. automatic member teardown 与 constructor unwind

raw owner处理完成后，workspace 的自动 member 析构严格按声明逆序：

```text
internalSourceWorkLayer
sourceColors
internalRenderLayer
sourceDescriptor
sourceCacheObject
findSourceResourceManager
rootContent
```

该 reverse run 的四端 anchors 是 Android arm64 `0x6CD0CC`、Android armv7 `0x593CFC`、iOS arm64
`0x10011F390`、iOS armv7 `0x11DE14`。它随后继续进入 V250 的 priority/motion/type-1/ramp owners。

constructor landing pads 显示相同原则：只销毁已构造完成的 members，并从最后完成者向前回滚。两个
ResourceManager `CopyRef`、两个 Dictionary factory/assignment 以及 later Variant setup 都各自形成新的
unwind frontier；raw pointer本身是 POD slot，不产生 automatic member destructor。

## 7. portable 源码修改

`cpp/plugins/motionplayer/Player.h` 已在 `_rootContentVariant` 后恢复：

- `_findSourceResourceManager`；
- `_sourceCacheObject`；
- `_sourceDescriptor`；
- `_internalRenderLayer`；
- `_sourceColors`；
- `_internalSourceWorkLayer_guess`；
- `_renderSeparateLayerAdaptor` raw pointer。

同时从 class 后部旧位置删除六个 Variant，从 variable-label storage 附近删除旧 pointer declaration，并
把注释改成 reference-independent 的 ownership/lazy-publication 描述。未知的 work-layer 私有源码名继续
保留 `_guess`；本轮没有因为布局证据虚构 definitive private symbol，也没有改写 source resolution、Layer
factory 或 adaptor 算法。

## 8. IDB 写回与 iOS armv7 安全保存

四库各写回 6 条 comment、6 个 bookmark；本轮没有新增 rename。anchors分别覆盖双 CopyRef、workspace
初始化、raw slot zero、primary publication、raw dtor和 Variant reverse teardown。

iOS armv7 使用 different-path packed save：

- V250 canonical 备份：
  `out/idb-recovery/v251-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v251.i64`；
- V251 candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v251.i64`；
- candidate 经独立 `C:\IDA\idat.exe -A` probe，退出码 0；
- 旧 loose `id0/id1/nam` 移入 `pre-v251-canonical-loose/`；
- candidate 安装 canonical 后，MCP reopen 成功读回 V251 constructor comments 与 V250 semantic names；
- candidate/canonical 都是 376,895,696 bytes，SHA-256
  `76D119E628A5BF6C2BAC0B7863B544A9140423C658F931D3E47607EBDE5EED73`。

四份最终 V251 IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,656,687 | `B195F343A7B7682C966DAE9E549FCD29B00CFD186F2C6438F0A39167EFB40440` |
| Android armv7 | 345,642,531 | `4E8B058F3471E996D66B9CD5BF959C8E81D285B4FB47F8EE05095050128D48CC` |
| iOS arm64 | 334,624,001 | `F0DFD17DFE4F65DF6A5BAAF415B0163BDFFD6239E347AE5EE9B8F9EAFCBE0EEF` |
| iOS armv7 | 376,895,696 | `76D119E628A5BF6C2BAC0B7863B544A9140423C658F931D3E47607EBDE5EED73` |

## 9. 验证与最终 wasm 基线

- complete motionplayer Catch2 TU ordinary/headless syntax：通过，仅既有 `_tss` warning；
- Web：33-step affected rebuild通过；
- Wasmtime：62-step affected rebuild通过；
- `krkr2_wasmtime_guest`：2-step build/link通过并完成 exnref转换；
- Web、Wasmtime、guest 三条 build命令 no-work复验通过；
- scoped `git diff --check`：无 whitespace error；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,354 | `0x1BD31` | `0x1A410BD` | `0x5A3E40` | `0x3185F7B` | `AC629DEC1C8F1BA961B66045093D2487D839B1B903C7BCC777B4842518637DA6` |
| Wasmtime `index.wasm` | 85,002,495 | `0x1BA50` | `0x19E906B` | `0x5A1090` | `0x3141E11` | `B4D564364847262DCBDF772F5843C22CF2C7AA1CECC9238C326D70D9F90A0B1C` |
| Wasmtime guest | 151,478,428 | `0x1618E` | `0x13D7DE1` | `0x4D1630` | `0x1421EBA` | `8F8A39D0491A3107C5DD766C5E31EC922965160CF06DD96E6F4C94CF9337AB09` |

相对 V250，Web/Wasmtime各缩8 bytes，全部来自 CODE section，FUNCTION/DATA/name不变；guest总大小
增加18 bytes，CODE增加4 bytes，其余净变化在调试/custom sections。变化与 member displacement及其
debug layout更新相符，没有引入新的函数或静态数据。

## 10. V252 follow-up

V252 已以四端 fresh constructor/destructor和 play/chara coordinators确认 raw adaptor 后直接为 pending
motion/chara owners、camera velocity triple、drawAffine six scalars与 particleOutsideRect。两个 pending
owner采用 direct-field nested flush且异常不清；其后80-byte POD区被 destructor完整跨过。完整 offset、
normal teardown、源码迁移、IDB安全保存与构建基线见
`analysis/motionplayer_player_pending_velocity_affine_rect_contiguous_layout_four_binary_2026-08-18.md`。
