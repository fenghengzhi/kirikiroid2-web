# motionplayer DrawDevice manager-item Draw 渲染链与 software cache：四参考二进制对照

日期：2026-08-21

范围：`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS
armv7 四份当前参考二进制。本文从四端 fresh decompile/disassembly 重新闭合
`DrawDeviceManagerItem::Draw`、base/software `GetDrawTexture` 虚槽、rect 参数、引用关系、
异常清理与 software cache 发布边界；旧 `libkrkr2.so` 注释不作为结论来源。

## 1. 本轮纠正的三个真实偏差

当前 portable 源码原先有三条与四份参考共同实现不符的数据流：

1. `Draw` 在 texture conversion 后，从返回的 texture 连续读取两组宽高；参考实现的两组
   宽高都读取原始 `drawBuffer`，且分处 virtual conversion 前后。
2. software override 调用 `source->GetScanLineForRead(0)`；参考调用独立虚槽
   `source->GetPixelData()`。
3. software override 先读取宽高，且只在 cache miss 时取得 private render manager；参考在
   函数入口先取得 render manager，再按 `GetPixelData -> GetPitch -> direct Width/Height` 取样，
   cache hit 也不能跳过 render-manager acquisition。

恢复后的共同伪代码为：

```cpp
void Draw(const D3DPoint_guess &) {
    owner = Parent;
    if(!owner) return;
    drawBuffer = Manager->GetDrawBuffer();
    if(!drawBuffer) return;

    ncbPropAccessor p(PrimaryOwner);
    type    = p.getIntValue("type", 0);
    opacity = p.getIntValue("drawOpacity", 255);
    x       = p.getIntValue("offsetX", 0);
    y       = p.getIntValue("offsetY", 0);

    rm = GetD3DRenderManager();
    method = rm->GetRenderMethod(opacity, true, type);
    if(!method) return;

    preW = drawBuffer->GetWidth();
    preH = drawBuffer->GetHeight();
    texture = virtual GetDrawTexture(drawBuffer);
    sourceRect = {0, 0, preW, preH};

    postW = drawBuffer->GetWidth();
    postH = drawBuffer->GetHeight();
    targetRect = {x, y, postW, postH};

    textures = {{texture, sourceRect}};
    target = owner->CurrentTarget;
    rm->OperateRect(method, target, target, targetRect, textures);
}
```

这里没有任何返回 texture 的宽高读取。`targetRect.right/bottom` 是第二次宽高的原值，
不是 `x + postW / y + postH`；当 offset 非零时，几何宽高因此是
`postW - x / postH - y`，不能用更“自然”的坐标写法替换。

## 2. 四端函数与 vtable 地图

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| manager-item `Draw` | `0x532CD4` | `0x496738` | `0x10023527C` | `0x233FC8` |
| software slot 10 | `0x532EFC` | `0x4968D4` | `0x1002354B0` | `0x23421C` |
| base slot 10 | `0x53321C` | `0x496B7C` | `0x10023576C` | `0x234508` |
| software vtable address point | `0x19FABB0` | `0x10AAFF4` | `0x101AEE818` | `0x1839048` |
| base vtable address point | `0x19FAC18` | `0x10AB028` | `0x101AEE880` | `0x183907C` |

两个 item 类型共享 slots 2..9；slot 3 是 `Draw`，slot 10 是源 texture 取得/转换。slot
10 的 ABI offset 是 LP64 `+0x50`、ILP32 `+0x28`。base 实现只是从 bitmap 返回裸 texture
字段；software 实现维护派生对象末尾的 private-OpenGL cache。

## 3. `Draw` 的完整调用顺序

四端共同顺序为：

1. 读取 item 的 `Parent`；null 立即返回。传入的 `D3DPoint_guess` 完全不读。
2. 严格读取 `Manager` 并调用 `GetDrawBuffer`；仅返回 bitmap null 时正常早退。没有
   `Manager == null` guard。
3. 用 `PrimaryOwner` 构造 `ncbPropAccessor`，因此 accessor 先 AddRef owner；owner null 会在
   accessor 构造的严格链上失败，不是默认属性路径。
4. 依次读 `type=0`、`drawOpacity=255`、`offsetX=0`、`offsetY=0`。每个
   `getIntValue` 都保持既有的 `MEMBERMUSTEXIST probe -> ordinary PropGet -> integer conversion`
   协议。
5. 取得 private render manager，调用 `GetRenderMethod(opacity, true, type)`。
6. method null：销毁 accessor 后返回；不读任何宽高，不做 texture conversion，也不读
   `CurrentTarget`。
7. 从已缓存的 `drawBuffer` 指针读第一组 `GetWidth/GetHeight`。
8. 调用 item vslot 10 `GetDrawTexture(drawBuffer)`。
9. 立即把返回 pointer 与 `{0,0,preW,preH}` 构成唯一 source pair；不 AddRef、不判 null。
10. 从同一个 `drawBuffer` pointer 再读一次 `GetWidth/GetHeight`。
11. 构造 `{offsetX,offsetY,postW,postH}`。
12. 从早先缓存的 `owner` 读取一次 `CurrentTarget`，同一 pointer 同时传 target/reference。
13. 以 count=1 的 `tRenderTexRectArray` 调 `OperateRect`。
14. 销毁 accessor。任意后半调用抛出时也先销毁 accessor，再原样重抛。

四端 guard 精确只有 `Parent`、`GetDrawBuffer()` 结果和 render method 三层。没有
`renderManager`、`PrimaryOwner`、返回 texture 或 `CurrentTarget` 的新增 guard。

## 4. 两阶段 geometry snapshot 的重入含义

第一组尺寸在 virtual slot 10 之前固定，第二组在 slot 10 返回后重新采样。因此 software
conversion 或任意派生 override 可以在 `GetPixelData/GetPitch` 重入时改变 drawBuffer texture/
尺寸，产生刻意不同的两个 domain：

```text
source pair rect  <- conversion 前 drawBuffer 尺寸
target rect       <- conversion 后同一 drawBuffer 尺寸
source pointer    <- virtual conversion 返回值
```

参考没有检查三者是否一致。由此得到以下边界：

- slot 10 返回 texture 的尺寸完全不参与 rect；返回尺寸与 pre/post 两组都可以不同。
- slot 10 返回 null 时，`Draw` 本身不再解引用它，而是把 null 放进 source pair；后续
  `OperateRect` 是否处理或崩溃由 render manager 决定。
- `Manager->GetDrawBuffer()` 只调用一次；重入替换 manager 的 live draw-buffer 槽不会改变本次
  已缓存 pointer。若重入销毁该对象，第二组尺寸读取进入 UAF 边界。
- `Parent` 也只在入口缓存一次，但 `CurrentTarget` 在 conversion 和第二组尺寸之后才读取；
  conversion 期间对同一 owner 的 target 更改会被本次 submit 观察到。
- `CurrentTarget` 只读一次，随后同一值复制给 target/reference；两参数之间没有第二次取样。
- 没有 source、target 或 bitmap 的临时 AddRef，也没有 submit 后 Release。

## 5. rect/array 的 ABI 形状

source pair 是 `{iTVPTexture2D *, tTVPRect}`：LP64 下 24 bytes，ILP32 下 20 bytes。
`tRenderTexRectArray` 只携带 pair pointer 与 count=1：LP64 下 16 bytes，ILP32 下 8 bytes。

`OperateRect` 虚槽为 render-manager LP64 `+0xA0`、ILP32 `+0x50`；四端均按：

```text
rm, method, CurrentTarget, CurrentTarget, &targetRect,
{ &sourcePair, 1 }
```

传参。Android arm64/armv7 和 iOS arm64 用普通 zero-cost exception cleanup；iOS armv7 的
SJLJ call-site state 明确把属性读取、两组尺寸、slot 10 和 `OperateRect` 全部包在 accessor
cleanup 范围内。只有 accessor/其 owner ref 被清理；source/target texture 从未被本函数拥有。

## 6. base `GetDrawTexture`

四份 base slot 10 都是二条/极短 leaf：

```cpp
return bitmap->GetTexture();
```

实际为直接读 bitmap texture 字段：LP64 `bitmap +0x58`，ILP32 `bitmap +0x40`。没有 bitmap
null guard、texture null guard、virtual call、AddRef、width/height 读取或 render-manager side
effect。普通 base item 因而不会在两阶段尺寸采样之间主动重入；但返回 null 仍按上一节进入
source pair。

## 7. software `GetDrawTexture` 的精确顺序

共同伪代码：

```cpp
rm = GetD3DRenderManager();                  // 总是第一项
source = bitmap->GetTexture();               // 裸字段
pixels = source->GetPixelData();             // virtual
pitch = source->GetPitch();                  // virtual
width = source->GetWidth();                  // direct field
height = source->GetHeight();                // direct field

if(cache && cache->GetWidth() == width &&
            cache->GetHeight() == height) {
    cache->Update(pixels, RGBA, pitch,
                  {0, 0, signed(pitch) / 4, height});
    return cache;
}

if(cache)
    cache->Release();                        // 不先清字段
replacement = rm->CreateTexture2D(
    pixels, pitch, width, height, RGBA, 0);
cache = replacement;                         // 仅成功返回后 commit
return cache;
```

字段/虚槽映射：

| 语义 | LP64 | ILP32 |
|---|---:|---:|
| bitmap raw texture | `+0x58` | `+0x40` |
| texture direct Width/Height | `+0x0C/+0x10` | `+0x0C/+0x10` |
| source `GetPixelData` vslot | `+0x40` | `+0x20` |
| source `GetPitch` vslot | `+0x50` | `+0x28` |
| cache `Update` vslot | `+0x58` | `+0x2C` |
| render manager `CreateTexture2D` | `+0x18` | `+0x0C` |

software cache 字段位置为 Android arm64 item `+0x60`、Android armv7 `+0x38`、iOS
arm64 `+0x68`、iOS armv7 `+0x3C`。差异来自四端 base item/object ABI，不是两个 cache。

## 8. software cache 的边界行为

### 8.1 取样和匹配

- private render manager 在 bitmap/source 读取之前取得；其第一次初始化或异常即使 cache 命中
  也不可跳过。
- `GetPixelData` 与 `GetPitch` 都早于 width/height；它们的重入尺寸修改会参与本次 cache match/
  allocation。
- cache match 先比较 width，只有相等才读/比较 height；cache null 不读任何 cache 尺寸。
- source/cache 尺寸都是直接 32-bit 字段读取，不调用可重写的 width/height virtual。
- source、pixels 和 cache 都不 AddRef；virtual callback 删除 source 或 item 会进入 UAF。

### 8.2 cache hit

- `Update` format 固定 `TVPTextureFormat::RGBA`，pitch 原样传递。
- update rect 固定 `{0,0,pitch/4,height}`，right 不是 source width。
- `pitch/4` 是有符号除法并向零截断；负 pitch 也没有 guard。例如 `-5/4 == -1`。
- `Update` 返回值（若 ABI 有）忽略；抛异常时 cache 字段仍指向原对象，没有 rollback。
- Update 返回后重新读取 item cache 字段并返回，而不是保证返回调用前寄存器中的 pointer；
  reentrant Update 改写字段会影响返回值。

### 8.3 cache miss / size mismatch

- 旧 cache 非空时先 virtual `Release`，但成员不预先写 null。
- `CreateTexture2D` 使用此前捕获的 pixels/pitch/width/height；Release 后不重新采样 source。
- Create 抛出：字段仍保存刚 Release 的旧地址；若 Release 已删除对象，该成员成为悬空指针。
  析构或下一次调用再读/Release 会形成 UAF/double-release 边界。参考没有 scope guard 修复它。
- Create 正常返回 null：null 仍写入字段并作为正常结果返回，旧 cache 已永久丢失。
- 只有正常返回后的单一 store 是 publication commit；没有临时 AddRef 或失败恢复。

## 9. 源码与回归恢复

`cpp/plugins/DrawDeviceD3D.cpp` 已恢复：

- `Draw` 在 slot 10 前缓存 `sourceWidth/sourceHeight`，slot 10 后直接重新读取
  `drawBuffer->GetWidth/GetHeight` 构造 target rect；不再查询返回 texture 尺寸。
- software override 把 `GetD3DRenderManager` 提到入口，把像素接口改为 `GetPixelData`，并按
  `pixels -> pitch -> width -> height` 排序。
- miss 路径继续先 Release、后 Create、成功后写字段，刻意保留原版非异常安全窗口。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增 `DrawBufferTextureProbe`：

- probe 初始为 2x2，在 `GetPixelData` 内改成 3x2；
- `GetScanLineForRead` 与 `GetPixelData` 分别计数；
- `GetPitch` 记录事件；
- 通过 root `capture` 提供 live `CurrentTarget` 并执行 software manager-item `Draw`；
- 断言恰好观察 `GetPixelData` 一次、`GetScanLineForRead` 零次、`GetPitch` 一次，事件顺序为
  `pixels, pitch`，且回调后的 live 字段为 3x2。

两组 drawBuffer 几何采样的位置另由 Wasmtime 主/guest 定向反汇编锁定。CTest 配置当前没有
注册测试，所以该 probe 本轮获得普通与 `KRKR2_WASMTIME_HEADLESS=1` 两次 syntax-only 覆盖，
不能误报为已经实际运行。

## 10. Recovery IDB 写回

四库合计写回：

- 60 条 function/line comment（每库 15）；
- 12 个 bookmark（每库 3）；
- 12 个 `_guess` semantic rename（每库 3）；
- 12 个 function prototype/type update（每库 3）；
- 12 次定向 force-recompile/decompile readback（每库 3）。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 15 | 3 | 3 / 3 | 3 | 368547676 | `A2E821A740A0ACEC872399542847C53464C8B4E45B6C8FB265D24ED0451E3353` |
| Android armv7 | 15 | 3 | 3 / 3 | 3 | 346744411 | `1AE992BA473C8E7BA99D621EF5CA7F2D38C78DC4C75F66362E20F89E069D02C6` |
| iOS arm64 | 15 | 3 | 3 / 3 | 3 | 336228281 | `39FE946CFC9B7E9BBAA56EEF9A14063CCB65777ECF221E4FDEB10905A0782213` |
| iOS armv7 | 15 | 3 | 3 / 3 | 3 | 377040817 | `6040AA163EF6CD30B7F59C4E0988FED38766DCA6FC4911AA59154D88D2CC13DC` |

iOS armv7 遵循不同路径恢复流程：从当前 authoritative canonical 的六个组件制作新 candidate，
在 candidate 上写回、save、`idat -A`、fresh readback；candidate i64 在发布前为
`377040817` bytes / `84A8C0E3A33AE7C33D3626D0756C763B6B391B6E093756C4BF6746D6E02679CB`。
逐组件发布并验证 byte-identical 后，再对 canonical 自身执行最终 `idat -A`，得到上表最终 hash。
四库最后均顺序通过 canonical `idat -A`，再逐库 fresh 打开；三个 V273 名称、prototype 和入口
function comment 全部回读成功。prebackup/candidate 保存在
`out/idb-recovery/v273-manager-item-draw-render-chain/`。

## 11. 构建与 Wasm 证据

验证结果：

- `motionplayer-dll.cpp` ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 syntax-only 均 exit 0；
- Web、Wasmtime 主目标、Wasmtime guest 均构建成功，随后三目标均
  `ninja: no work to do`；第一次并行 source 两份 `emsdk_env.ps1` 时 Web shell 报过一次临时
  `emsdk_set_env.ps1` sharing warning，但构建 exit 0，之后串行 no-work 验证无该警告；
- 两个 CTest 目录均 exit 0，并准确报告 `No tests were found!!!`；
- 三份 Wasm 均 `WebAssembly.validate == true` 且成功构造 `WebAssembly.Module`；
- `git diff --check` exit 0，仅输出工作树既有 LF -> CRLF warning；
- 本轮 source/test 精确 absolute-address 扫描为 0；旧 `Draw` 返回-texture尺寸读取模式已不存在，
  software override 中相关路径只剩预期的 post-`GetPixelData/GetPitch` direct width/height。

最终 Wasm：

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85655095 | `539 / 69` | `183F853B23DAB2641ED115D9AB2FF0F24A5165AA95D7CC9940C8478EC67D66F5` |
| Wasmtime `index.wasm` | 85002236 | `538 / 69` | `0B9585FB34991F9200518485D8051E5F5A6D0E46037768364B80EEB2FE5C2D30` |
| Wasmtime guest | 151508222 | `445 / 87` | `01E7D7DE047AD92BD6B7E98AEBDC408DADFCBC67A52FB1094DDA938F0277B185` |

相对 V272，三份总大小分别为 `+71 / +71 / +201` bytes；imports/exports 均不变。主 Wasmtime
定向反汇编中，`Draw` 先连续调用 drawBuffer width/height，再执行 slot 10
`call_indirect`，随后第二次调用同一 width/height helper；software slot 10 则先调用 private
manager helper，依次执行 vtable `+0x20`、`+0x28`，之后才调用 direct width/height helpers，
cache hit 保留 `i32.div_s 4` 和 Update，miss 保留 Release 后 Create/field store。guest 的同三函数
显示完全同序，并以 exnref `try_table/catch_all_ref -> accessor destructor -> throw_ref` 保留异常清理。
base slot 10 在两份 Wasm 都只返回 bitmap raw texture。

## 12. 本轮未扩张的范围

本轮只恢复 manager-item `Draw` 及其 base/software slot 10。render manager 内部
`GetRenderMethod(opacity,true,type)` 对 type/blend selector 的完整分派，以及具体
`OperateRect` backend 如何裁剪越界 source/target rect，仍属于后续独立纵切面；本文不从当前
portable renderer 的实现反推参考行为，也不把该下游未闭合部分伪装成已恢复。

