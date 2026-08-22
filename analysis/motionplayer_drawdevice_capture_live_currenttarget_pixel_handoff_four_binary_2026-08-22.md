# motionplayer DrawDevice capture live CurrentTarget 与 pixel handoff：四参考二进制对照

日期：2026-08-22

范围：`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS
armv7 四份当前参考二进制。本文重新从 canonical IDB fresh decompile/disassembly
`DrawDeviceObjectBase::capture` 及 software handoff helpers，专门纠正旧
`motionplayer_drawdevice_render_targets_capture_four_binary_2026-08-15.md` 中受早期单端/旧注释影响的
两条结论。

## 1. 本轮纠正的两条真实偏差

### 1.1 software handoff 调 `GetPixelData`

四端都调用 texture vtable：

| 虚槽 | LP64 | ILP32 |
|---|---:|---:|
| `GetPixelData` | `+0x40` | `+0x20` |
| `GetPitch` | `+0x50` | `+0x28` |

旧报告和 portable 源码把第一项写成 `GetScanLineForRead(0)`；这是错误的 vtable 解释。
真实调用没有 line 参数，且与 V273 software manager-item conversion 的独立
`GetPixelData -> GetPitch` 证据一致。

### 1.2 创建返回值不是后半段的稳定 authority

参考把 `CreateTexture2D` 返回值直接写入 `CurrentTarget`。child fanout、target Layer
conversion、software predicate 之后，它不继续使用一个保存初始返回值的 local，而是：

```text
pixels     = CurrentTarget->GetPixelData()  // load field #1
pitch      = CurrentTarget->GetPitch()      // load field #2
sizeTarget = CurrentTarget                  // load field #3
width      = sizeTarget->Width
height     = sizeTarget->Height
...
GPU: layer->AssignTexture(CurrentTarget)    // load field #4
...
CurrentTarget->Release()                    // handoff 后 load field #5
CurrentTarget = null
```

因此 `CurrentTarget` 是 live publication slot；reentrant callback 可以替换它，后续每一阶段会观察
当时的 field，而不是最初创建的 texture。旧源码的 local `target` 错误地把全部阶段固定到初始
pointer，改变了数据来源、Release 对象、泄漏/UAF 和异常部分提交边界。

## 2. 四端函数地图

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `capture` | `0x531468` | `0x495778` | `0x100233FA8` | `0x232CA8` |
| private render-manager accessor | `0x5327A0` | `0x49634C` | `0x100234E24` | `0x2339F0` |
| BaseLayer `SetSize` helper | `0x805724` | `0x62F6A4` | `0x100078A98` | `0x75C94` |
| `tTVPBaseTexture::Update` wrapper | `0x7FC094` | `0x62BC68` | `0x100409CFC` | `0x3F1B2A` |
| software-renderer predicate | 已命名 | 已命名 | 已命名 | `0x32930C` |

root/texture 关键布局：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `CurrentTarget` | `+0x158` | `+0xBC` | `+0xF8` | `+0x8C` |
| root `PrimaryWidth/Height` | `+0x1E0/+0x1E4` | `+0x124/+0x128` | `+0x180/+0x184` | `+0xF4/+0xF8` |
| texture direct Width/Height | `+0x0C/+0x10` | `+0x08/+0x0C` | `+0x0C/+0x10` | `+0x08/+0x0C` |

private render-manager helper 是函数局部 static accessor，缓存按名字 `opengl` 取得的 borrowed/
process-lifetime manager。iOS armv7 的 software predicate 在 V274 前仍是匿名 `sub_32930C`，本轮
按其全局缓存/normalized bool 数据流补成 `_guess` 名。

## 3. 完整 capture 数据流

V269 已闭合的 `UpdateObjects(0)`、shared Variant、live FrontItems tree cursor 与 callback
异常行为保持不变。本轮重新核验的完整后半顺序为：

```cpp
UpdateObjects_guess(0);

rm = GetD3DRenderManager_guess();
CurrentTarget = rm->CreateTexture2D(
    nullptr, 0, uint32(PrimaryWidth), uint32(PrimaryHeight), RGBA, 0);

offset = {OffsetX, OffsetY};
for(object in live FrontItems) {
    if(object->IsVisible() &&
       (!frontIndexLimit || object->FrontIndex < frontIndexLimit) &&
       (object->DrawPlane & 1))
        object->Draw(offset);
}

layer = Layer::FromVariant(targetLayer);
if(TVPIsSoftwareRenderManager()) {
    pixels = CurrentTarget->GetPixelData();
    pitch = CurrentTarget->GetPitch();
    sizeTarget = CurrentTarget;
    width = sizeTarget->GetWidth();
    height = sizeTarget->GetHeight();
    layer->SetSize(width, height);
    layer->GetMainImage()->Update(
        pixels, pitch, 0, 0, width, height);
} else {
    layer->AssignTexture(CurrentTarget);
}

CurrentTarget->Release();
CurrentTarget = nullptr;
return true;
```

`GetMainImage()` 展开为 `ApplyFont -> raw MainImage load`；MainImage 只取一次。Update 使用此前
捕获的 `pixels/pitch/width/height` 和固定零 origin，不在 SetSize/ApplyFont 之后重读
`CurrentTarget`。

## 4. live-field 分阶段 snapshot

software 分支不是一个原子 texture snapshot，而是三个可分离来源：

| 阶段 | 数据 | authority |
|---|---|---|
| A | `pixels` | 当时 `CurrentTarget` 的 `GetPixelData()` |
| B | `pitch` | 再次重读 field 后的 `GetPitch()` |
| C | `width/height` | 第三次重读得到的同一个 pointer 的直接字段 |
| D | final Release | Layer handoff 完成后再次重读的 field |

由此得到以下原版边界：

- child `Draw` 或 target `NativeInstanceSupport` 可替换 field；outer capture 会放弃初始 pointer
  的 authority。
- `GetPixelData` 重入替换 field 后，pitch 可以来自 B；`GetPitch` 再替换后，尺寸可以来自 C。
- C 的 Width/Height 从同一 pointer 连续直接读取，中间没有 virtual callback，因此这两个值是一组。
- SetSize、ApplyFont、GetMainImage 或 Update 再替换 field，不改变已捕获的 A/B/C，但会改变 D。
- GPU 分支在 `AssignTexture` 调用前只重读一次 field；`AssignTexture` 内重入替换后，D 仍会
  Release 新的 live value，而不保证是刚传给 Layer 的 pointer。
- 没有任何 field value 的临时 AddRef、所有权验证或 identity 校验。

这一行为允许 `pixels/pitch/dimensions` 来自三张不同 texture，并可能在正常路径 Release 第四张。
这是参考真实的数据流，不能用“安全”的 local snapshot 或 RAII owner 自动统一。

## 5. ownership 与异常部分提交

`CreateTexture2D` 之前的 child-update shared Variant 有自己的析构/EH；target 创建之后没有
finally/scope guard。具体边界：

- Create 抛出：结果尚未 store，`CurrentTarget` 保留调用前旧/未初始化值。
- Create 正常返回 null：null 仍直接发布；child 可先执行，software 的第一次 vptr 访问或正常
  tail Release 最终进入 null 崩溃边界。
- field 被重入从 initial A 替换为 B：A 不再有可达 owner，通常永久泄漏。
- Layer conversion 抛出：保留当时 live field，不 Release、不清零。
- `GetPixelData/GetPitch/SetSize/ApplyFont/Update/AssignTexture` 任一抛出：同样保留最后 live
  publication；之前捕获的 raw pixels 没有清理动作。
- 正常尾部 Release 的是 D，而不是“本次 Create 的返回值”。若 D 是 borrowed pointer，会错误
  消耗外部 ref；若 D 为 null，会在虚调前崩溃。
- Release 抛出或 reentrant 删除 root：null store 不执行；后一种还使 store 成为 UAF。
- 只有 Release 正常返回后才写 `CurrentTarget=null` 并返回 true。

software branch 把像素复制到 Layer MainImage 后 Release D；GPU branch 的 `AssignTexture` 通常
为传入 texture 建立 Layer ownership，再由 tail Release D 配平创建 ref。但一旦 field 在两步间被
替换，配平关系不再成立；参考没有补偿。

## 6. 源码恢复与回归

`cpp/plugins/DrawDeviceD3D.cpp` 已：

- 把 CreateTexture2D 结果直接写入 `CurrentTarget`，删除后半 authority local；
- software 路径改用 `CurrentTarget->GetPixelData()`；
- 为 pixels、pitch、size pointer 分别重读 live field，Width/Height 从同一 `sizeTarget` 取值；
- GPU AssignTexture 与正常 tail Release 也直接重读 field；
- 保留无异常 cleanup、Release 后才 clear 的非事务行为。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增
`CaptureCurrentTargetReplacementLayerDispatch` 定向回归：

1. outer capture 创建原始 320x240 target 并完成 child draw；
2. outer target Layer conversion 的 `NativeInstanceSupport` 把 primary size 临时改为 3x2；
3. callback reentrant 调 nested capture；nested 创建/发布 3x2 target，随后用无效 target Variant
   使严格 Layer conversion 抛出，故留下 3x2 live `CurrentTarget`；
4. callback 捕获异常、恢复 primary size 320x240，再让 outer conversion 正常完成；
5. outer software handoff 必须重读 live field，使目标 Layer MainImage 最终为 3x2。

若仍使用旧 local authority，outer 会交付自己最初的 320x240 texture，测试即失败。该案例也保留
outer initial target 因 publication 被覆盖而泄漏的参考边界。当前 CTest 没有注册测试，故本轮该
回归只有 ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 双 syntax 覆盖；不能报告为已实际执行。

## 7. Recovery IDB 写回

四库合计写回：

- 57 条 function/line comment；
- 17 个 bookmark；
- 13 个 `_guess` semantic rename；
- 17 个 function prototype/type update；
- 17 次定向 force-recompile/decompile readback。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 14 | 4 | 3 / 4 | 4 | 368547695 | `FA7E0ECBA06E0DEF708F70B7D44CCD365902EC490CA08B83AD0DED80B3990063` |
| Android armv7 | 14 | 4 | 3 / 4 | 4 | 346744412 | `C9A9FFF807AE720C84706242E035004ADC55EA387CC26A059F8C6577C8916461` |
| iOS arm64 | 14 | 4 | 3 / 4 | 4 | 336228302 | `09C1761AD4B3E37D601BC86C4D941B3228E61C9CEFDA465522FCA5744D8BEA0B` |
| iOS armv7 | 15 | 5 | 4 / 5 | 5 | 377057222 | `6B564B884AEC76E3AD65FE0DCA5A57140A553343FB17778BC8C97294EEA15ECC` |

iOS armv7 继续使用不同路径 candidate：从 V273 authoritative canonical 的六组件制作
`candidate-v274`，写回、save、`idat -A`、fresh readback 后才逐组件发布。发布前 candidate
i64 为 `377057222` bytes / `F38FA139C447815D5A9AAEAD394F657C3DA65573A65953DFD79756040936B10F`；
发布逐组件 hash byte-identical，随后 canonical 自身最终 `idat -A` 得到上表 hash。四库均完成
最终 canonical `idat` 与 fresh readback，capture/helper 名称、prototype、入口注释全部保留。
prebackup/candidate 位于 `out/idb-recovery/v274-capture-live-current-target/`。

## 8. 构建与 Wasm 证据

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 `motionplayer-dll.cpp` syntax-only 均 exit 0；
- Web、Wasmtime main、Wasmtime guest 均构建成功，随后三目标均
  `ninja: no work to do`；
- 两个 CTest 目录 exit 0 并准确报告 `No tests were found!!!`；
- 三份 Wasm 均 `WebAssembly.validate=true` 且成功构造 Module；imports/exports 不变；
- Wasmtime main/guest 定向反汇编均显示 Create 返回值直接 store 到 root field `+0x8C`，
  software 分支在 vtable `+0x20`、`+0x28` 调用前各自 `i32.load +0x8C`，随后第三次 load
  field 再取 direct width/height；GPU 和尾部 Release 前又各自 load field；
- `git diff --check`、absolute-address/stale-pattern、零 session/process 审计见本轮最终检查。

最终 Wasm：

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85655094 | `539 / 69` | `EFCD127028781A4AF81DA18B9B1E36217DFF6372FD3E825ED4DE9FFCE0938D9F` |
| Wasmtime `index.wasm` | 85002235 | `538 / 69` | `0DF9E81608E53751FA265E0F7613C11F7C0AB83230A8D8D76AF169B2714D155D` |
| Wasmtime guest | 151508230 | `445 / 87` | `9ADA2D7E6856757ECBDBE907BE1AAF463493FD6058BCB8B870CA48E1D0865215` |

相对 V273，总大小分别为 `-1 / -1 / +8` bytes：Web/Wasmtime main 的 CODE 各 `-1`；guest
CODE `-3`、`.debug_str +0x0B`，其余 section payload size 不变，恰有 `-3 + 11 = +8`。该微小
delta 与定向反汇编共同确认修正没有扩张 public ABI、imports 或 exports。
