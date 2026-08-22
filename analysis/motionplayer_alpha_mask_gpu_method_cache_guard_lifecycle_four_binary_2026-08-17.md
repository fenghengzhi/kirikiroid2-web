# MotionPlayer alpha-mask GPU method cache、guard、公共 setup 与边界生命周期四参考复核（2026-08-17）

## 1. 结论与本轮裁决

此前两轮已经闭合 `Motion_doAlphaMaskOperation_guess` 的 11 参数 Variant ABI、目标 clip
裁剪、空交集、六组逐像素公式和脚本可见 `fillRect/update` 调用，但 portable 实现仍把所有
非空操作统一降成 CPU 像素循环，没有恢复参考二进制的 GPU render-method family，也没有
表达 mode/op 判定之前的公共可写目标取得顺序。本轮以
`D3DAdaptor::clearTargetTexture` cache 的物理后继为入口，重新对四个完整函数、全部局部静态
data xref、guard release/abort 和六条 GPU submit 路径做 fresh 复核，得到以下共同结构：

1. 每个非空 overlap 都先严格转换 source Layer、再转换 destination Layer，并取得 source
   main-image texture；随后立即按当前 render manager 进入 software/GPU 公共 setup。这个
   setup **早于** `maskMode` 和 `op` 的合法性判定。
2. software 公共 setup 先取 source pixel data，再通过 Layer 级
   `GetMainImagePixelBufferForWrite()` 取得 destination 可写像素，随后读取双方 pitch 并计算
   起点；GPU 公共 setup 先取 destination 当前/reference texture，再以
   `GetTextureForRender(true, &overlapRect)` 取得 writable target。
3. 因而非空的 unsupported mode/op 虽然不做像素或 GPU operation，仍会发生上述 bitmap
   side effects，并最终脚本派发 `update`。不能把合法性检查提前，也不能为 no-op 省略可写
   target acquisition。
4. 六条 GPU 分支分别拥有独立的 process-lifetime function-local method pointer、独立
   32-bit mutable compile hint 和独立 ABI guard；三个 threshold 方法还各自拥有独立 parameter
   ID 和第二只 ABI guard。它们不是 array、map 或一个共享 program cache struct。
5. method pointer 是 raw/borrowed value，没有 AddRef、Release、owner wrapper 或 exit
   destructor。hint 零初始化且无 guard。method 初始化抛出时只 abort method guard，hint **不
   回滚**；parameter-ID 初始化抛出时只 abort parameter guard，已经发布的 method 保留。
6. threshold method 在每次 operation 前修改同一个缓存 method 的 `threshold` 参数，再用
   fresh render-manager lookup 提交。函数本体没有 mutex 或 snapshot；这一“写共享参数→提交”
   序列不具备本地并发原子性。

portable 源码已恢复这六个 GPU 路径、精确 shader/blend tuple、公共 setup 和异常可见的局部
static 声明结构；没有把 ABI guard storage 或 recovery 地址硬编码到源文件。

## 2. 四参考函数与后继物理边界

| 目标 | `Motion_doAlphaMaskOperation_guess` | alpha-mask static cluster | cluster 后继 |
|---|---:|---:|---:|
| Android arm64-v8a | `0x6AC4E4` | `0x1AB5328..0x1AB53C8` | `0x1AB53D0` |
| Android armeabi-v7a | `0x57E1E8` | `0x111180C..0x1111868` | `0x111186C` |
| iOS arm64 | `0x100104E68` | `0x101B697F0..0x101B69890` | `0x101B69898` |
| iOS armv7 | `0x10243C` | `0x187D4DC..0x187D538` | `0x187D53C` |

四份参考均 stripped，恢复名继续保留 `_guess`。cluster 后继已由既有四端纵切面识别为
`requestCharaMemberHint_guess`（`"chara"`），属于 Player load parameter/node member-hint
family；它不属于 alpha-mask cache，亦不是第七个 render method。

本轮还识别了两个独立的 split guard-abort landing helper：

| 目标 | split landing helper |
|---|---:|
| iOS arm64 | `0x100106250` |
| iOS armv7 | `0x103604` |

Android arm64 的 abort tails 仍在主函数 physical range 内。Android armv7 的九个 abort site
散布于不连续 cold ranges 与 literal pools 之间，不能为命名便利把整段 data/code 粗暴合并成
一个函数；本轮只对可证明的 site 逐点注释。

## 3. 非空路径的公共数据流与求值顺序

8 月 16 日 Variant/脚本派发复核已经证明，裁剪在 native Layer 转换之前，空交集不访问
source。进入非空路径后的四端共同顺序进一步闭合为：

```text
source Variant -> strict Layer conversion
destination Variant -> strict Layer conversion
sourceLayer.mainImage->GetTexture() -> sourceTexture
sourceRect = {srcX, srcY, srcX + width, srcY + height}
sourceTextures = one element {sourceTexture, sourceRect}

if (TVPIsSoftwareRenderManager()) {
    srcPixels = sourceTexture->GetPixelData()
    dstPixels = destinationLayer->GetMainImagePixelBufferForWrite()
    srcPitch  = sourceTexture->GetPitch()
    dstPitch  = destinationLayer->GetMainImagePixelBufferPitch()
    srcPixels += srcY * srcPitch + srcX * 4
    dstPixels += dstY * dstPitch + dstX * 4
} else {
    dstReference = destinationLayer.mainImage->GetTexture()
    dstTarget = destinationLayer.mainImage
                    ->GetTextureForRender(true, &overlapRect)
}

switch (maskMode/op) {
    ... one of six software loops or six GPU operations ...
    ... unsupported combination does nothing here ...
}

destination.update(dstX, dstY, width, height)
```

关键边界不是单纯“GPU 与 CPU 结果相同”，而是公共 setup 已经产生可观察状态：

- software 的 destination Layer accessor会执行 image-modified/writable-independence 路径；
- GPU 的 `GetTextureForRender(true, ...)` 会建立或切换可写 render target；
- 这些动作发生后才知道 mode/op 是否受支持；
- 任一步自然失败都会中断函数，尾部 `update` 只在公共 setup 和选中 operation 都正常返回后
  执行；
- 四端没有 source bounds、negative source coordinate、null image/texture 或 pitch 的额外友好
  检查，仍属于 trusted-engine boundary。

空交集保持既有独立边界：不转换 source，不执行上述 setup；只有 `op == 1` 时脚本
`fillRect` 整个 destination clip，且不再派发 `update`，这里也不检查 `maskMode`。

## 4. 六组软件/GPU 操作矩阵

令 `S` 为 source alpha、`D` 为 destination alpha。software 只改 alpha byte，B/G/R 保持
不变；GPU 分支由 source texture、blend state 和 destination reference/target 组合出同一语义。

| `maskMode` | `op` | software 结果 | GPU method | op 前清 overlap 外四条带 |
|---:|---:|---|---|---|
| 1 | 1 | `D = S * D / 255` | `AlphaMask` | 是 |
| 1 | 2 | `D = (255-S) * D / 255` | `AlphaMaskRev` | 否 |
| 1 | 5 / 6 | `D = S + (255-S) * D / 255` | `AddAlphaMask` | 否 |
| 0 | 1 | `S < threshold` 时 `D = 0` | `AlphaMaskThreshold` | 是 |
| 0 | 2 | `S >= threshold` 时 `D = 0` | `AlphaMaskThresholdCrop` | 否 |
| 0 | 5 / 6 | `S >= threshold` 时 `D = 255` | `AlphaMaskThresholdFill` | 否 |

`op == 1` 的四个条带继续通过 retained destination dispatch 调脚本 `fillRect`，且位于选中
software loop/GPU operation 之前。`op == 5` 不能因低位与 1 重合而进入清除路径。

unsupported behavior 同样是矩阵的一部分：`maskMode` 非 0/1，或 0/1 下 `op` 不是
1/2/5/6 时，overlap 不变且不调用 render method，但公共 setup 已完成，最后仍脚本派发
`update`。

## 5. shader、method 编译参数与 blend tuple

前三个 alpha methods 各自把以下完全相同的 fragment shader 文本交给
`GetOrCompileRenderMethod(name, &hint, shader, 1, 0)`：

```glsl
void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }
```

三个 threshold methods 各自使用：

```glsl
uniform float threshold;
void main() { gl_FragColor = vec4(0,0,0,step(threshold, texture2D(tex0, v_texCoord0).a)); }
```

`SetBlendFuncSeparate(func, srcRGB, dstRGB, srcAlpha, dstAlpha)` 的精确 tuple 为：

| method | `func` | `srcRGB` | `dstRGB` | `srcAlpha` | `dstAlpha` |
|---|---:|---:|---:|---:|---:|
| `AlphaMask` | `0x8006` (`GL_FUNC_ADD`) | `0` | `1` | `0` | `0x0302` (`GL_SRC_ALPHA`) |
| `AlphaMaskRev` | `0x8006` | `0` | `1` | `0` | `0x0303` (`GL_ONE_MINUS_SRC_ALPHA`) |
| `AddAlphaMask` | `0x8006` | `0` | `1` | `1` | `0x0303` |
| `AlphaMaskThreshold` | `0x8006` | `0` | `1` | `0` | `0x0302` |
| `AlphaMaskThresholdCrop` | `0x8006` | `0` | `1` | `0` | `0x0303` |
| `AlphaMaskThresholdFill` | `0x8008` (`GL_MAX`) | `0` | `1` | `0` | `0x0303` |

方法初始化表达式包含 `GetOrCompileRenderMethod(...)->SetBlendFuncSeparate(...)` 的完整链；
只有链条正常返回后 method guard 才 release。每条 GPU operation 又做一次 fresh
`TVPGetRenderManager()`，然后：

```text
manager->OperateRect(method,
                     dstRenderTexture,
                     dstReferenceTexture,
                     overlapRect,
                     oneElementSourceTextureRectArray)
```

三个 threshold 分支在提交之前均执行：

```text
thresholdId = guarded-static method->EnumParameterID("threshold")
method->SetParameterOpa(thresholdId, threshold)
```

`threshold` 从 integer 参数直接传给 Opa setter；shader 内的 normalized parameter mapping 由
render-method 实现负责，本函数没有自行除以 255。

## 6. 四端 24 个局部静态 data item 的精确布局

每个非 threshold group 都是 `{method pointer, method guard, uint32 hint}`；LP64 在 hint 后有
4-byte alignment gap，以便下一个 pointer/guard 对齐。每个 threshold group 再追加
`{int32 parameterId, parameter guard}`，parameter ID 后的 LP64 gap 同样不属于 value。

### 6.1 Android arm64-v8a

| group | method pointer | method guard | hint | parameter ID | parameter guard |
|---|---:|---:|---:|---:|---:|
| `AlphaMask` | `0x1AB5328` | `0x1AB5330` | `0x1AB5338` | — | — |
| `AlphaMaskRev` | `0x1AB5340` | `0x1AB5348` | `0x1AB5350` | — | — |
| `AddAlphaMask` | `0x1AB5358` | `0x1AB5360` | `0x1AB5368` | — | — |
| `AlphaMaskThreshold` | `0x1AB5370` | `0x1AB5378` | `0x1AB5380` | `0x1AB5384` | `0x1AB5388` |
| `AlphaMaskThresholdCrop` | `0x1AB5390` | `0x1AB5398` | `0x1AB53A0` | `0x1AB53A4` | `0x1AB53A8` |
| `AlphaMaskThresholdFill` | `0x1AB53B0` | `0x1AB53B8` | `0x1AB53C0` | `0x1AB53C4` | `0x1AB53C8` |

method pointer/guard 为 8 B，hint/parameter ID 为 4 B，parameter guard 为 8 B。

### 6.2 Android armeabi-v7a

| group | method pointer | method guard | hint | parameter ID | parameter guard |
|---|---:|---:|---:|---:|---:|
| `AlphaMask` | `0x111180C` | `0x1111810` | `0x1111814` | — | — |
| `AlphaMaskRev` | `0x1111818` | `0x111181C` | `0x1111820` | — | — |
| `AddAlphaMask` | `0x1111824` | `0x1111828` | `0x111182C` | — | — |
| `AlphaMaskThreshold` | `0x1111830` | `0x1111834` | `0x1111838` | `0x111183C` | `0x1111840` |
| `AlphaMaskThresholdCrop` | `0x1111844` | `0x1111848` | `0x111184C` | `0x1111850` | `0x1111854` |
| `AlphaMaskThresholdFill` | `0x1111858` | `0x111185C` | `0x1111860` | `0x1111864` | `0x1111868` |

所有 value/guard 均为紧凑 4 B item。

### 6.3 iOS arm64

| group | method pointer | method guard | hint | parameter ID | parameter guard |
|---|---:|---:|---:|---:|---:|
| `AlphaMask` | `0x101B697F0` | `0x101B697F8` | `0x101B69800` | — | — |
| `AlphaMaskRev` | `0x101B69808` | `0x101B69810` | `0x101B69818` | — | — |
| `AddAlphaMask` | `0x101B69820` | `0x101B69828` | `0x101B69830` | — | — |
| `AlphaMaskThreshold` | `0x101B69838` | `0x101B69840` | `0x101B69848` | `0x101B6984C` | `0x101B69850` |
| `AlphaMaskThresholdCrop` | `0x101B69858` | `0x101B69860` | `0x101B69868` | `0x101B6986C` | `0x101B69870` |
| `AlphaMaskThresholdFill` | `0x101B69878` | `0x101B69880` | `0x101B69888` | `0x101B6988C` | `0x101B69890` |

item width 与 Android arm64 相同。

### 6.4 iOS armv7

| group | method pointer | method guard | hint | parameter ID | parameter guard |
|---|---:|---:|---:|---:|---:|
| `AlphaMask` | `0x187D4DC` | `0x187D4E0` | `0x187D4E4` | — | — |
| `AlphaMaskRev` | `0x187D4E8` | `0x187D4EC` | `0x187D4F0` | — | — |
| `AddAlphaMask` | `0x187D4F4` | `0x187D4F8` | `0x187D4FC` | — | — |
| `AlphaMaskThreshold` | `0x187D500` | `0x187D504` | `0x187D508` | `0x187D50C` | `0x187D510` |
| `AlphaMaskThresholdCrop` | `0x187D514` | `0x187D518` | `0x187D51C` | `0x187D520` | `0x187D524` |
| `AlphaMaskThresholdFill` | `0x187D528` | `0x187D52C` | `0x187D530` | `0x187D534` | `0x187D538` |

所有 value/guard 均为紧凑 4 B item。

四库均已用 entity readback 验证 24 个命名 data item 的起点和 size；下一项 unrelated data
也单独回读，排除了把 alignment gap 或后继 member hint 吞进 cluster 的可能。

## 7. 初始化、publication 与异常回滚

每个方法 group 的 source-level 初始化可归一为：

```cpp
static std::uint32_t hint = 0; // BSS/constant initialization; no guard
static iTVPRenderMethod *method =
    TVPGetRenderManager()
        ->GetOrCompileRenderMethod(name, &hint, shader, 1, 0)
        ->SetBlendFuncSeparate(...); // guarded dynamic initialization
```

threshold group 再有：

```cpp
static int parameterId = method->EnumParameterID("threshold");
```

四端的 release/abort 拓扑共同证明：

- 首次 method 初始化正常返回：保存 method，release method guard；以后同一 branch 只走 guard
  fast path；
- `GetOrCompileRenderMethod` 或 `SetBlendFuncSeparate` 抛出：abort 当前 method guard，下次进入
  同一 branch 从 method 初始化重试；
- **hint 是另一只无 guard 的 mutable uint32**。若 method initializer 在异常前已经通过
  `&hint` 写值，该值保留；guard abort 不清 hint，下次 retry 看见先前值。Android arm64
  landing tail 上曾存在的“hint remains zero”候选已由调用/存储顺序否定；IDB 中已追加纠正
  注释；
- parameter-ID 初始化抛出：只 abort parameter guard；method guard 已 release，method 和 hint
  均保留，下次只重试 `EnumParameterID`；
- 两阶段都成功后，`SetParameterOpa` 或 `OperateRect` 抛出不触碰任何 guard，缓存永久保留；
- 六个 method guard 互相独立，三个 parameter guard 也互相独立；一个 branch 的失败或成功
  不会发布、回滚或冻结另一个 branch。

这也是为何不能把六个缓存改写成一个循环初始化的 array/map：那会改变首次触达、失败后的
部分 publication、重试粒度、异常栈和不同 operation 之间的独立性。

## 8. 所有权、退出析构和并发边界

四端对六个 method value 都只有 raw load/store 和虚调用：没有 AddRef、Release、shared/unique
owner、manager/control block 或 `__cxa_atexit` 注册。hint、parameter ID 和 guard 也都是
trivial storage。因此：

- method identity 由首次进入各 branch 时的 render manager/compile lookup 决定；
- method cache 跨 Player/Layer/compositor instance 共享，并存活到 process 结束；
- compositor、Layer 或 Player 析构不清 cache；
- process teardown 不由本函数释放 method；
- guard landing 只回滚“尚未完成的动态初始化”，不是 static exit destructor。

ABI guard 只保护首次动态初始化。初始化成功后，method pointer、hint 和 parameter ID 都按本
函数路径只读；但 threshold branch 每次都会对 manager-owned/shared method 对象执行
`SetParameterOpa`，随后才取得 manager 并提交。四参考函数内没有 mutex、thread-local copy、
parameter snapshot 或 clone，故两个并发 threshold operation 可能在本地层面交错参数写入与
提交。更深层 render manager 是否串行化不由此函数证明，portable 源码不能凭空加锁，也不能
把 function-local statics 改为每实例字段来掩盖共享状态。

## 9. portable 源码修正

`cpp/plugins/motionplayer/PlayerRenderInternal.cpp` 的 `applyMotionAlphaMask_guess` 已从“公共
CPU loop + 预先 supported-mode/op gate”恢复为参考结构：

- 非空 strict Layer conversion 后立即取得 source texture/rect；
- 在 mode/op 判定前建立 software pixel/pitch 或 GPU reference/writable-target 公共 setup；
- software 分支保留六组精确整数结合顺序和 `<`/`>=` threshold 边界；
- GPU 分支恢复六个独立 method/hint static declaration、精确 name/shader/blend tuple；
- threshold 分支恢复三个独立 parameter-ID statics 和逐调用 `SetParameterOpa`；
- 每次 GPU operation 以 fresh manager lookup 执行 one-source `OperateRect`；
- `op == 1` 的四条 script `fillRect` 保持在 selected operation 前；
- 所有非空路径继续共用一次 script `update`，包括 unsupported no-op；
- 未加入 null、bounds、status、pitch 或 render-method fallback，也没有在编译注释写入任何
  reference absolute address。

结构回归统计为：6 个 `GetOrCompileRenderMethod`、6 个 `SetBlendFuncSeparate`、6 个
`OperateRect`、3 个 `EnumParameterID("threshold")`、3 个 `SetParameterOpa`，以及公共一次加
六个 operation 内部共 7 个 `TVPIsSoftwareRenderManager()`；旧
`supportedMaskMode/supportedItemFlags` 预筛选均为 0。

## 10. Recovery IDB 回写

四份 recovery IDB 已完成并原位保存：

- 每库新建/修正 24 个独立 typed data item，共 96 项，并逐项回读 size；
- 每库恢复六个 method、六个 method guard、六个 hint、三个 parameter ID、三个 parameter
  guard 的统一 `_guess` 名；
- iOS 两端恢复并命名 split guard-abort helper；Android arm64 注释 main-function abort tails；
  Android armv7 对九个离散 abort site 逐点注释且保留 literal-pool 边界；
- 四库均补 compositor/common split/method init/guard abort、data ownership 和 retry 注释及
  bookmark；
- 强制重新反编译并回读所有相关 function，确认 semantic name 出现在伪代码中；
- 四库保存成功。

统一命名为：`MotionAlphaMask_method_guess`、`MotionAlphaMask_methodGuard_guess`、
`MotionAlphaMask_methodHint_guess`，其余五组按 method name 同构；threshold 三组另有
`_paramId_guess`、`_paramGuard_guess`。命名只表达四端共同语义，不宣称恢复作者原符号拼写。

## 11. 编译、Wasm 与差分验证

验证结果：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套 motionplayer unit-test TU syntax-only 均
  exit 0；只有仓库既有 `_tss` deprecated warning；
- `cmake --build out/web/debug` 完整链接成功（3 steps）；
- `cmake --build out/wasmtime/debug` 完整链接成功（4 steps）；
- Node `WebAssembly.Module` 对两份当前 `index.wasm` parse 成功；
- `llvm-objdump -h` 成功读取两份当前 Wasm 的完整 section table；
- Web/Headless CTest 均 exit 0，但两个 build tree 当前都报告 `No tests were found`；因此
  单测 TU 只有双模式编译覆盖，不把它声称为已执行 runtime unit tests；
- source/report/plan 的 scoped `git diff --check` 通过，输出仅有工作树既有 LF/CRLF 提示。

当前 V189 artifact：

| 配置 | 当前文件 | 总大小 | imports / exports | FUNCTION | GLOBAL | CODE | DATA | name |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Web Debug | `out/web/debug/index.wasm` | 85,652,233 B | 539 / 69 | `0x1BD24` | `0xD5B2` | `0x1A418E5` | `0x5A4057` | `0x3184928` |
| Wasmtime Headless Debug | `out/wasmtime/debug/index.wasm` | 84,999,374 B | 538 / 69 | `0x1BA43` | `0xD5DA` | `0x19E9893` | `0x5A12A7` | `0x31407BE` |

相对 V188，两份 artifact 的总大小都精确增加 4,656 B：CODE `+0x1110`（4,368 B）、
DATA `+0x120`（288 B），FUNCTION/GLOBAL/name 与 imports/exports 全部不变。完全相同的双目标
section delta 与本轮恢复六个 GPU branch、六组 shader/name 常量的代码/只读数据增量一致，且
没有扩大 Wasm ABI surface。

`out/wasmtime/debug/krkr2_wasmtime_guest.wasm` 是 2026-08-13 留下的旧产物，不是当前 CMake
最终链接输出；本轮所有 headless 尺寸、parse 和 section 结论均只针对当前
`out/wasmtime/debug/index.wasm`。

## 12. 与旧报告的关系及后继边界

`motionplayer_alpha_mask_four_binary_2026-08-11.md` 的裁剪/空交集/六组软件数学仍有效，但其
GPU 部分只有 method 名与“语义相同”的概括，不能替代本轮 cache layout、guard、shader、blend、
公共 setup、ownership 和异常回滚结论。其已注明被 8 月 16 日 supersede 的 native
`FillRect/UpdateByScript` 内容继续无效；脚本 Variant ABI 仍以
`motionplayer_alpha_mask_variant_abi_script_dispatch_four_binary_2026-08-16.md` 为准。

alpha-mask cluster 的物理后继 `requestCharaMemberHint_guess` 已在
`motionplayer_player_load_parameter_node_member_hint_family_four_binary_2026-08-16.md` 闭合，
因此下一轮不重复该 word。V190 应从四端 BSS/函数调用图中选择下一个尚未闭合的相邻 cache、
guard 或 owner region，并继续坚持四库 fresh evidence、独立边界和 recovery IDB 回写。
