# MotionPlayer DrawDevice getPrimaryLayerBitmap manager-item、cached primary 与 raw texture 四参考闭环（V272）

## 1. 结论

V272 重新审计 root `getPrimaryLayerBitmap(index, targetLayer)`，纠正了 V150 公共回调报告和
portable 源码中同一条过时结论。四份参考二进制共同实现的并不是：

```cpp
source = Managers[index]->GetPrimaryLayer();
if(source) ...
```

而是：

```cpp
item = static_cast<DrawDeviceManagerItem *>(
    Managers[index]->GetDrawDeviceData());
if(item) {
    target = tTJSNI_Layer::FromVariant(targetLayer);
    source = item->PrimaryLayer;  // item构造时缓存
    target->AssignTexture(source->GetMainImage()->GetTexture());
}
```

因此唯一正常 no-op 是 manager 的 draw-device data item 为 null。item 非空后，target、cached
source、main image 和 texture 全部走严格裸指针链；没有 source-null 分支、重新采样 current
primary、临时 AddRef、RAII guard 或异常回滚。

## 2. 四端函数地图

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| root `getPrimaryLayerBitmap` | `0x52A4DC` | `0x4926AC` | `0x100230654` | `0x22F5B8` |
| non-null item split helper | caller内联 | `0x4926F8` | `0x1002306CC` | `0x22F5FC` |
| BaseLayer `ApplyFont` | `0x80C848` | `0x634004` | `0x10007E854` | `0x7BD38` |
| BaseLayer `AssignTexture` | `0x8071A0` | `0x6308A8` | `0x10007A164` | `0x772FC` |

NCB wrapper仍是 V150 已闭合的 2-argument typed method：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x5384F4` | `0x49B930` | `0x10023AD38` | `0x23A760` |

本轮 fresh decompile/disassembly覆盖表中四个业务入口及三个平台拆出的 non-null helper；Android
arm64把全部业务体保留在入口内。iOS 对 UTF 宽字面量的普通字符串视图只显示首字符，但控制流、
同一 throw helper 和 Android 两端完整字面量共同确认错误仍为
`"invalid layer manager index."`。

## 3. ABI 布局与错误 vtable 解释的纠正

先前把入口的 manager vtable LP64 `+0x18` / ILP32 `+0x0C` 误读成
`GetPrimaryLayer`。按 `iTVPLayerManager` 的真实槽顺序：

```text
AddRef
Release
SetDrawDeviceData
GetDrawDeviceData       <- LP64 +0x18 / ILP32 +0x0C
GetPrimaryLayerSize
GetPrimaryLayer         <- LP64 +0x28 / ILP32 +0x14
```

V271 `getPrimaryLayers` 的确调用后一个 `GetPrimaryLayer` 槽；V272 调用前一个
`GetDrawDeviceData` 槽。返回对象随后按 `DrawDeviceManagerItem` 布局读取构造期缓存字段：

| 目标 | Managers begin/end | item cached `PrimaryLayer` | layer `MainImage` | bitmap texture |
|---|---|---:|---:|---:|
| Android arm64 | root `+0x190/+0x198` | item `+0x40` | layer `+0x118` | image `+0x58` |
| Android armv7 | root `+0xE0/+0xE4` | item `+0x24` | layer `+0xCC` | image `+0x40` |
| iOS arm64 | root `+0x130/+0x138` | item `+0x48` | layer `+0x118` | image `+0x58` |
| iOS armv7 | root `+0xB0/+0xB4` | item `+0x28` | layer `+0xCC` | image `+0x40` |

iOS/Android LP64 的 item 偏移不同，ILP32 也不同，这是派生对象布局/ABI差异；不能把任一平台
offset复制到其它文件。字段语义则由四端完全相同的下游 `ApplyFont -> MainImage -> texture ->
AssignTexture` 数据流确认。

## 4. 精确调用顺序

四端共同顺序为：

```text
1  snapshot Managers.begin/end用于size检查
2  sign-extend 32-bit index；以unsigned size关系检查 index < size
3  失败：throw "invalid layer manager index."
4  重新/继续取得 Managers.begin并读取 Managers[index]
5  严格虚调 manager.GetDrawDeviceData()
6  item == null：立即返回，不转换target
7  target = Layer.FromVariant(targetVariant)
8  source = item.cached PrimaryLayer
9  source.ApplyFont()                    // GetMainImage的前半
10 image = source.MainImage              // 同一GetMainImage的返回值
11 texture = image.Bitmap/GetTexture     // direct raw field load
12 tail-call target.AssignTexture(texture)
```

关键顺序不是 C++ 表达式的任意等价重排：target conversion 明确发生在 cached source 字段加载
之前；item pointer明确在 target conversion之前取得。四端都只读取一次 item字段、一次
MainImage字段和一次 texture字段。

## 5. 索引与 null/crash 边界

`index` 是有符号32位 TJS integer窄化结果。ARM64先 `SXTW`，ARMv7保留相同比特并以unsigned
关系比较；所以：

- `index < 0` 被视为极大unsigned值并抛出；
- `index == Managers.size()` 抛出；
- 合法范围为 `[0, size)`；
- throw helper是noreturn；反汇编中保留的fallthrough begin reload只是编译器控制流形状；
- 合法index指向null manager时，在读取manager vptr处崩溃；没有manager guard；
- `GetDrawDeviceData()`返回null是唯一正常no-op，且连 target conversion也不发生；
- item非空、cached PrimaryLayer为null时，在`ApplyFont`入口严格崩溃；
- source非空、MainImage为null时，在texture field load严格崩溃；
- texture本身为null时仍把null传入`AssignTexture`，后者的bitmap实现随后严格解引用。

正常构造的 item 本来就要求 `manager->GetPrimaryLayer()`、owner和main image构造链，因此 cached
source null主要是破坏/悬挂状态边界，不是正常attach后的可达no-op。

## 6. current primary 与 manager-item snapshot

`DrawDeviceManagerItem`构造时只采样一次：

```text
Manager = manager
PrimaryLayer = manager.GetPrimaryLayer()
PrimaryOwner = PrimaryLayer.owner (+1 leak，既有结论)
...
```

V272不重新读取 `manager.GetPrimaryLayer()`。因此 attach后如果 manager current primary被detach、
替换或改为null，`getPrimaryLayerBitmap`仍使用旧 item中的raw cached pointer。该pointer没有独立
native lifetime owner；item只永久持有/泄漏 script owner ref，依赖owner保持native adaptor拓扑。

target conversion可执行 `NativeInstanceSupport`虚调并重入。入口已缓存item pointer，所以重入：

- 仅把 manager data槽替换/null：本次仍沿旧item继续；下一次调用看到新槽；
- detach/替换 manager current primary：本次仍读取item cached primary；
- 删除item：conversion返回后读取旧item字段，UAF；
- 让cached primary/native失效：后续ApplyFont/MainImage链UAF；
- 没有generation、重新验证、AddRef、锁或deferred mutation。

## 7. raw image/texture owner 与 alias

`GetMainImage()`是 inline `ApplyFont(); return MainImage;`。四端业务体只保留 raw source、raw image
和raw texture：

- `GetMainImage`不AddRef image；
- direct `GetTexture` field load不AddRef texture；
- 业务入口没有局部 Variant/smart pointer/cleanup record；
- `AssignTexture`前不会保护source texture lifetime。

`source == target`或两层已经共享同一texture时，底层
`tTVPNativeBaseBitmap::AssignTexture`先比较 `Bitmap == tex` 并返回false，不Release/AddRef同一pointer；
但外层 `tTJSNI_BaseLayer::AssignTexture`不检查该bool，仍继续：

```text
InternalSetImageSize
ImageModified = true
ResetClip
Update(false)
```

所以 alias不是整个方法no-op：texture owner不变，但Layer尺寸/dirty/clip/update side effect仍发生。

## 8. 异常与部分提交

业务入口没有需要展开的owned local，ARMv7也没有SJLJ cleanup。异常按发生点原样逃逸：

| 发生点 | 已发生的plugin/source side effect | rollback |
|---|---|---|
| index throw | 无manager/target访问 | 无 |
| manager `GetDrawDeviceData` throw | manager callback自身可能已有副作用 | 无 |
| target conversion throw | item已snapshot；conversion自身可能已重入 | 无 |
| source `ApplyFont`/`SetFont` throw | `FontChanged`已先清零 | 不恢复dirty flag |
| target bitmap old texture `Release`后失败 | old owner可能已释放，后续store/AddRef取决于失败点 | 无 |
| target new texture `AddRef`后续失败 | target bitmap可能已发布新pointer/ref | 无 |
| size/clip/update阶段失败 | texture assignment可能已完成，部分Layer状态已提交 | 无 |

四端从 texture raw field直接 tail-call `AssignTexture`；入口自己没有正常尾部、catch、texture Release
或source/item cleanup可执行。

## 9. portable修复与回归

`cpp/plugins/DrawDeviceD3D.cpp`：

- 把业务定义移到 `DrawDeviceManagerItem`完整定义之后；
- 让 `DrawDeviceManagerItem`反向friend root，直接表达四端都能观察到的item字段load，不引入
  Debug构建中会成为额外函数的猜测accessor；
- 合法index后调用 `manager->GetDrawDeviceData()`；
- item-null提前返回；
- item非空时严格保持 target conversion先于cached PrimaryLayer读取；
- raw source/main-image/texture直接交给`AssignTexture`，不添加防御或owner guard。

`tests/unit-tests/plugins/motionplayer-dll.cpp`新增真实NCB路径覆盖：

- data item为null时，非Layer target也成功no-op；
- item恢复后同一target严格conversion并抛出；
- negative和one-past-end index在target conversion前抛出；
- detach manager current primary后仍从item cached primary复制texture；
- target `NativeInstanceSupport`重入清空manager data槽，本次仍沿snapshot item完成；
- source==target时texture identity保持。

## 10. Recovery IDB 写回

四库合计写回：

- 88条function/line comment；
- 16个bookmark；
- 16个`_guess` semantic rename；
- 16个function prototype/type update；
- 16次定向force-recompile/readback。

| 目标 | comments | bookmarks | renames/types | force readback | final bytes | final SHA-256 |
|---|---:|---:|---:|---:|---:|---|
| Android arm64 | 21 | 4 | 3 / 3 | 3 | 368547676 | `13D711638377AE06BBD34F0D9708B410A64956124A6A8737FB84D912948808CC` |
| Android armv7 | 22 | 4 | 4 / 4 | 4 | 346744411 | `320F4BAC23713CA10B835F663ADCE37529A365B81D8D2743D65578EBF266EB0A` |
| iOS arm64 | 23 | 4 | 4 / 4 | 4 | 336228281 | `8A09FF85E34D4A4C3942CA3D9A7681009889F52D6F4337DDBAC41D3570819A6F` |
| iOS armv7 | 22 | 4 | 5 / 5 | 5 | 377008049 | `3A358DC50C6FAF76FBCEBC09389C6D792491F153B5FEE35FF5E04C65F89D13BA` |

四库最后再次顺序通过`C:\IDA\idat.exe -A`，随后从四个canonical路径逐库fresh打开；主入口、
armv7/iOS split helper、ApplyFont、AssignTexture的名称、prototype、函数大小和V272 function comment
全部回读成功，会话逐库关闭。

取证中G: Google Drive虚拟盘曾断开；隐藏启动既有`GoogleDriveFS.exe`后恢复。断盘前未发布任何
canonical写入。iOS armv7恢复后发现authoritative canonical为`377008030` bytes / `CB5A...`，
不同于V271报告中的candidate hash；因此没有用旧candidate覆盖：

- 先把现行canonical完整备份为`current-canonical-pre-v272.i64`；
- 从该现行canonical制作全新candidate并重放V272；
- candidate经save、`idat -A`、fresh MCP readback后才发布；
- publish前candidate为`377008049` bytes / `FDE32BB5...`，publish后逐字节一致；
- canonical自己的最终`idat -A`产生上表`3A358D...`最终hash；
- 较早由V271 candidate派生的`374AB6...` V272副本仍保留，但从未发布。

四库prebackup及两条armv7 candidate链均保存在
`out/idb-recovery/v272-getprimarylayerbitmap-manager-item/`。

## 11. 构建与 Wasm 证据

验证结果：

- ordinary与`KRKR2_WASMTIME_HEADLESS=1`两种`motionplayer-dll.cpp` syntax-only均exit 0；
- Web、Wasmtime主目标与Wasmtime guest均构建成功，随后三目标均`ninja: no work to do`；
- Web首次增量生成遇到既有cache中的`/upstream/emscripten/...` stale toolchain，重新执行
  `Web Debug Config` preset后cache恢复为绝对EMSDK路径并成功构建；
- 两个CTest目录均exit 0并准确报告`No tests were found!!!`，新增行为断言因此只获双syntax覆盖；
- Node对Web、Wasmtime和guest三份Wasm均`WebAssembly.validate=true`并成功构造Module；imports/
  exports分别为`539/69`、`538/69`、`445/87`；
- Wasmtime主Wasm与guest定向反汇编分别保留`0x100`与`0xD5`字节
  `DrawDeviceObjectBase::getPrimaryLayerBitmap`，显示signed negative gate、manager vector lookup、
  vtable `+0x0C` indirect GetDrawDeviceData、item-null branch、FromVariant、直接`i32.load +0x28`
  cached PrimaryLayer以及GetMainImage/GetTexture/AssignTexture调用；符号表没有
  `GetPrimaryLayerForBitmap_guess`额外helper；
- `git diff --check` exit 0，仅有工作树既有LF→CRLF warning；V272新增compiled source/test精确
  行范围中的reference absolute-address扫描为0命中；旧source-primary-null语句和额外guess accessor
  compiled-source扫描均为0命中；
- `mcp__idalib__idb_list`最终为`sessions=[]`, `count=0`，且无残留
  `ida`/`ida64`/`idat`/`idat64`/`idalib-worker`进程。

最终Wasm：

| 产物 | bytes | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85655024 | `340C21AF3D88627F7B725A272AE7C6AD2A96EFB29B8A29F7017BD72337369EEE` |
| Wasmtime `index.wasm` | 85002165 | `899B609DFD6AE860BFFE39C0E003C2A322799D2E47EA15325EFA4458AD33949B` |
| Wasmtime guest | 151508021 | `988E28F122822021C7641423C543A1A29136351FF7B1E66E86AA3900B728995E` |

相对V271：

| 产物 | total | CODE | name | 其他section delta |
|---|---:|---:|---:|---|
| Web | `+25` | `+0x0D` | `+0x0C` | 其余payload size不变 |
| Wasmtime | `+25` | `+0x0D` | `+0x0C` | 其余payload size不变 |
| guest | `+80` | `+0x0D` | `-0x04` | `.debug_info +17`，其余DWARF净`+54` |

两主产物的`13 + 12 = 25`精确闭合；guest的`13 - 4 + 17 + 54 = 80`也精确闭合。
新增CODE恢复真实manager-data/cached-item路径，name/DWARF变化来自方法定义位置与friend直接字段表达。

## 12. 闭合范围

V272闭合 `getPrimaryLayerBitmap` 的index边界、manager vtable槽、data-item null gate、构造期cached
primary、target conversion顺序、raw image/texture handoff、current-primary分离、reentrancy snapshot、
source/target alias及异常部分提交。

这不表示 motionplayer 全目标完成；下一纵切面继续沿尚未闭合的调用链、容器、owner和边界推进。
