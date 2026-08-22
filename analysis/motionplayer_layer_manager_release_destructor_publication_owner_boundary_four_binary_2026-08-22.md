# Motionplayer `tTVPLayerManager` 引用计数、析构所有权与 BaseLayer 发布边界（四参考二进制）

## 1. 结论

V279 重新以 `reference/binaries/` 中四个参考目标为唯一原生证据，fresh 定位并反编译了
`tTVPLayerManager` 的构造、完整/删除析构、`AddRef/Release`、窗口注册委托、
`DrawDeviceData` 裸槽位，以及 `tTJSNI_BaseLayer` 创建/失效时的 manager owner 链。

四端共同恢复出的源码级结构与当前 portable source 一致：

```cpp
tTVPLayerManager::tTVPLayerManager(iTVPLayerTreeOwner *owner) {
    RefCount = 1;
    LayerTreeOwner = owner;
    DrawDeviceData = nullptr;
    DrawBuffer = nullptr;
    DesiredLayerType = ltOpaque;
    // raw layer pointers, flags and EnabledWorkRefCount initialized;
    // ReleaseTouchCaptureIDMark and ReleaseCaptureCalled not initialized.
}

tTVPLayerManager::~tTVPLayerManager() {
    if(DrawBuffer) delete DrawBuffer;
    // automatic members then destroy UpdateRegion and the three vectors.
}

void tTVPLayerManager::AddRef() { RefCount++; }
void tTVPLayerManager::Release() {
    if(RefCount == 1) delete this;
    else RefCount--;
}
```

本轮最重要的不是新增“安全”清理，而是确认以下看似缺失的动作都是四端一致的原版边界：

- `Release(1)` 不把 `RefCount` 写成 0，而是保留 1 直接进入 deleting destructor；
- `AddRef/Release` 都是普通非原子 32-bit load/add/store，没有 saturation、zero guard、
  `BeforeDestruction`、删除前通知、AddRef rescue recheck 或 reentry guard；
- manager 析构只删除 `DrawBuffer`，不清理/释放 `DrawDeviceData`、`LayerTreeOwner`、
  `Primary`、focus/capture 指针或三个 vector 中的 pointee；
- `DrawDeviceData` 是严格借用的 `void *` 槽，setter/getter 只有裸 store/load；
- normal primary invalidation 的顺序是 `DetachPrimary -> UnregisterSelfFromWindow -> Release ->
  BaseLayer.Manager=null`，最终 `Release` 执行期间 BaseLayer 字段仍发布旧 manager 指针；
- `ReleaseTouchCaptureIDMark` 与 `ReleaseCaptureCalled` 都没有构造初始化，不能用对象全零初始化
  或额外成员初始化“修复”。

因此 portable runtime 不需要语义修改。本轮只增加证据注释，防止后续按现代 RAII/线程安全直觉
改坏参考行为。

## 2. 四端函数映射

表中名字带 `_guess`，因为二进制没有为这些私有 C++ 函数保留精确源码符号；身份由虚表槽、字段、
caller/callee 和当前源码多向交叉确认。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| manager ctor/body | `Kirikiroid2_1.3.9_Android_arm64-v8a.so!tTVPLayerManager_ctor_guess@0x833F14` | `Kirikiroid2_1.3.9_Android_armabi-v7a.so!tTVPLayerManager_ctor_guess@0x64AA48` | `Kirikiroid2_1.3.9_iOS_arm64!tTVPLayerManager_ctor_body_guess@0x10031B3AC`, thunk `0x10031B480` | `Kirikiroid2_1.3.9_iOS_armv7!tTVPLayerManager_ctor_body_guess@0x320424`, thunk `0x320550` |
| ctor cleanup | inline landing `0x833FC8` | 无本地 landing | `tTVPLayerManager_ctor_cleanup_guess@0x10031B45C` | SjLj `tTVPLayerManager_ctor_cleanup_guess@0x320518` |
| complete dtor/body | `tTVPLayerManager_complete_dtor_guess@0x834014` | `tTVPLayerManager_complete_dtor_guess@0x64AAE0` | body `tTVPLayerManager_dtor_body_guess@0x10031B484`, thunk `0x10031B4EC` | body `tTVPLayerManager_dtor_body_guess@0x320554`, thunk `0x32059C` |
| deleting dtor | `tTVPLayerManager_deleting_dtor_guess@0x834094` | `tTVPLayerManager_deleting_dtor_guess@0x64AB2C` | `tTVPLayerManager_deleting_dtor_guess@0x10031B4F0` | `tTVPLayerManager_deleting_dtor_guess@0x3205A0` |
| `AddRef` | `0x834110` | `0x64AB3C` | `0x10031B504` | `0x3205B0` |
| `Release` | `0x834120` | `0x64AB44` | `0x10031B514` | `0x3205B8` |
| register / unregister owner | `0x834140` / `0x834158` | `0x64AB58` / `0x64AB62` | `0x10031B534` / `0x10031B54C` | `0x3205CC` / `0x3205D6` |
| Set/Get `DrawDeviceData` | `0x836E18` / `0x836E20` | `0x64C412` / `0x64C416` | `0x10031D544` / `0x10031D54C` | `0x322346` / `0x32234A` |
| Get `Primary` | `0x836E28` | `0x64C41A` | `0x10031D554` | `0x32234E` |
| BaseLayer construct | `tTJSNI_BaseLayer_Construct_guess@0x7FC818` | `tTJSNI_BaseLayer_Construct_guess@0x62C0B4` | `tTJSNI_BaseLayer_Construct_guess@0x1000749EC` | `tTJSNI_BaseLayer_Construct_guess@0x71C54` |
| BaseLayer construct cleanup | caller landing，ctor escape raw delete | 无本地 landing | `tTJSNI_BaseLayer_Construct_cleanup_guess@0x100074BC0` | SjLj `tTJSNI_BaseLayer_Construct_cleanup_guess@0x71E94` |
| BaseLayer invalidate | `tTJSNI_BaseLayer_Invalidate_guess@0x7FCBE8` | `tTJSNI_BaseLayer_Invalidate_guess@0x62C2F0` | `tTJSNI_BaseLayer_Invalidate_guess@0x100074C80` | `tTJSNI_BaseLayer_Invalidate_guess@0x71F48` |

manager 主虚表 address point 分别为 Android arm64 `0x1A239B0`、Android armv7
`0x10BF6F8`、iOS arm64 `0x1019B4398`、iOS armv7 `0x177B1A4`。`AddRef/Release` 是前两个
slot；deleting destructor 在 64-bit 表的 `+0x100`、32-bit 表的 `+0x80`。这是 ABI 字节偏移，
不写入 portable C++ 布局。

## 3. 构造布局与两个有意未初始化字段

64-bit 两端 manager allocation 为 `0xF0` bytes，32-bit 两端为 `0x90` bytes。字段语义和声明顺序
四端一致：

| 字段 | 64-bit offset | 32-bit offset | 构造动作 |
|---|---:|---:|---|
| primary / secondary vptr | `+0x00/+0x08` | `+0x00/+0x04` | 安装 manager / drawable 表 |
| `RefCount` | `+0x10` | `+0x08` | `1` |
| `LayerTreeOwner` | `+0x18` | `+0x0C` | 裸存 `owner` |
| `DrawDeviceData` | `+0x20` | `+0x10` | `nullptr` |
| `DrawBuffer` | `+0x28` | `+0x14` | `nullptr` |
| `DesiredLayerType` | `+0x30` | `+0x18` | `ltOpaque == 1` |
| capture / last-mouse pointers | `+0x38/+0x40` | `+0x1C/+0x20` | `nullptr` |
| `TouchCapture` begin/end/cap | `+0x48/+0x50/+0x58` | `+0x24/+0x28/+0x2C` | 全空 |
| `ReleaseTouchCaptureIDMark` | `+0x60` | `+0x30` | **不写** |
| `ModalLayerVector` | `+0x68` | `+0x38` | 全空 |
| `FocusedLayer` / `Primary` | `+0x80/+0x88` | `+0x44/+0x48` | `nullptr` |
| overall valid / `AllNodes` | `+0x90/+0x98` | `+0x4C/+0x50` | false / 全空 |
| `UpdateRegion` | `+0xB0` | `+0x5C` | `tTVPComplexRect` ctor |
| focus lock / visual changed | `+0xD8/+0xD9` | `+0x7C/+0x7D` | false / true |
| last mouse X/Y | `+0xDC/+0xE0` | `+0x80/+0x84` | `-1/-1` |
| `ReleaseCaptureCalled` | `+0xE4` | `+0x88` | **不写** |
| notifying / hold alpha | `+0xE5/+0xE6` | `+0x89/+0x8A` | false / true |
| `EnabledWorkRefCount` | `+0xE8` | `+0x8C` | `0` |

`ReleaseTouchCaptureIDMark` 在首次 `PrimaryTouchDown` 开头的 `ReleaseTouchCapture(id)` 尾部可被读取，
但随后本次 touch-down 会把它覆盖为当前 id，再进入 callback/capture 决策。该初始读取的数值结果通常
被覆盖，并不等于源码可以补初始化；四端机器码共同保留了这个未初始化字段。`ReleaseCaptureCalled`
则在 `PrimaryMouseDown` callback 前先写 false，callback 后才读取。

构造异常差异来自编译器/EH ABI：Android arm64、iOS arm64、iOS armv7 会在
`UpdateRegion` 构造逃逸时按 `AllNodes -> ModalLayerVector -> TouchCapture` 逆序释放已构造 storage；
Android armv7 ctor 没有本地 personality/landing。共享源码仍应保持普通成员构造，不手写平台分支。

## 4. `AddRef/Release` 的精确边界

四端共同机器语义为：

```text
AddRef:
    RefCount = RefCount + 1       // plain 32-bit, non-atomic

Release:
    next = RefCount - 1
    if next != 0:
        RefCount = next
        return
    tail-dispatch manager deleting destructor
```

iOS armv7 以 `old == 1` compare 表达同一条件；其余目标也可能在伪代码中显示 old/next 的不同形式。
最终分支不 store `next == 0`，所以析构期间字段仍为 1。边界后果：

- `Release` 从 0 开始会把机器字写成 `0xFFFFFFFF`，不会视为重复 release；
- 计数加减没有原子同步保证；
- 删除过程中的 reentrant `Release` 仍会看到 1，可再次进入 deleting destructor；
- 删除过程中的 reentrant `AddRef` 不能取消已经开始的 delete，因为没有删除前通知或返回后 recheck；
- 这些路径已经进入 C++ 生命周期未定义区，本报告只记录实际控制流，不把 double-delete/UAF 的任意
  后续结果承诺为稳定业务输出。

当前 `if(RefCount == 1) delete this; else RefCount--;` 正是最接近共享源码的表达。改为 `--RefCount == 0`、
atomic refcount、先写零、临时 owner、zero guard 或 `BeforeDestruction` 都会改变可观察顺序。

## 5. 析构所有权与容器边界

四端 complete destructor 的共有顺序是：

1. 安装 manager 的两个 vptr；
2. snapshot `DrawBuffer`；非空则调用其 deleting-destructor virtual slot（64-bit `+8`，32-bit `+4`）；
3. 析构 `UpdateRegion`；
4. 释放 `AllNodes` vector storage；
5. 释放 `ModalLayerVector` vector storage；
6. 释放 `TouchCapture` vector storage；
7. deleting wrapper 在 complete body 正常返回后 raw-delete manager storage。

`DrawBuffer` 的动态对象是 `tTVPDestTexture`，因此源码级 `delete DrawBuffer` 同时保留 wrapper 的
虚析构和 wrapper storage raw delete。这里不能把 wrapper 与其 inherited `Bitmap` texture 混为一谈：
native-base dtor 只对 `Bitmap` 调用 `iTVPTexture2D::Release`；最终引用仍为 1 并进入 deferred vector，
底层 texture 要到后续 `RecycleProcess` 才真正删除。manager 不在调用前后把 `DrawBuffer` 字段写 null。
完整四端证据见 V281 `motionplayer_dest_texture_native_bitmap_deferred_release_lifetime_four_binary_2026-08-22.md`。

析构完全不读取或修改：

- `DrawDeviceData`；
- `LayerTreeOwner`；
- `Primary`、`FocusedLayer`、`CaptureOwner`、`LastMouseMoveSent`；
- `TouchCapture`、`ModalLayerVector`、`AllNodes` 中的 raw pointee/相应 TJS owner；
- `RefCount`。

三个容器都是普通 `std::vector` 控制块；析构只拥有 vector allocation，不拥有 raw pointer 指向对象。
normal primary teardown 依赖更早的 `DetachPrimary`/window unregister 清逻辑 owner 和 draw-device item；
manager destructor 不是 fallback。若 unregister 没有完成、抛出或绕过派生 draw-device remove，
`DrawDeviceData` 可以保持非空/悬空直到 manager storage 被释放。

四个 complete destructor 都没有覆盖 `DrawBuffer` deleting destructor 逃逸的本地 cleanup 序列；若该
调用异常跨层，后续 `UpdateRegion`/vector storage cleanup 与 deleting-wrapper raw delete 不在当前正常
指令路径上执行。不能据此在共享源码手写“保证清到底”的 catch。

## 6. BaseLayer 创建、发布与 normal final release

primary BaseLayer 构造的四端共同数据流是：

```text
raw = operator new(sizeof(tTVPLayerManager))
tTVPLayerManager(raw, layerTreeOwner)   // BaseLayer.Manager 尚未发布
BaseLayer.Manager = raw                 // ctor 正常返回后才发布
raw->AttachPrimary(BaseLayer)
raw->RegisterSelfToWindow()
```

64-bit allocation 为 `0xF0`，32-bit 为 `0x90`。Android arm64、iOS arm64、iOS armv7 的 EH 表会在
manager ctor 逃逸时 raw-delete 尚未发布的 allocation；Android armv7 caller 没有本地 cleanup landing。
一旦字段已经发布，四端 attach/register 阶段都没有 manager Release/null rollback。

normal invalidation 的四端共同顺序为：

```text
manager = BaseLayer.Manager
if manager != null:
    if manager->GetPrimaryLayer() == BaseLayer:
        manager->DetachPrimary()
        manager->UnregisterSelfFromWindow()
    manager->Release()
    BaseLayer.Manager = null
```

这里 `Release` 通过虚表 slot 1；字段 clear 只在调用正常返回后发生。因此 count==1 时：

1. `DetachPrimary` 与 owner unregister 已先执行；
2. manager deleting destructor 运行时 BaseLayer.Manager 仍指向正在析构的 manager；
3. manager destructor 本身不会清该 BaseLayer 字段；
4. deleting destructor 正常返回后 `Release` 才返回，BaseLayer 再写 null。

如果 manager 析构、draw-buffer 析构或 reentrant callback 逃逸/终止，field clear 不会执行。iOS armv7
`Invalidate` 虽有 SjLj context，但 manager 序列的 call-site 值为 `-1`；`0x72182` landing 只处理后续
child-array lock scope，不能误当作 manager-release rollback。Android arm64、Android armv7、iOS
arm64 的这段也没有对应本地 cleanup landing。

这条链与 V277/V278 的 draw-device 证据合并后，normal owner 顺序为：

```text
BaseLayer.DetachPrimary
  -> window UnregisterLayerManager
     -> derived DrawDevice Remove: clear manager DrawDeviceData, delete item, base remove/release
  -> BaseLayer-owned manager Release
     -> concrete manager destructor: delete DrawBuffer, destroy local containers
  -> BaseLayer.Manager = null
```

manager destructor 不应重复执行 device remove，也不应补清 data slot。

## 7. 当前源码判定

逐行对照后的判定：

- `cpp/core/visual/LayerManager.h` 的继承、字段顺序、三个 `std::vector`、raw/owned pointer 选型一致；
- `HoldAlpha = true` 的 default member initializer产生四端 ctor 中的 true store；
- constructor 对其余字段的显式赋值一致；两个 marker 没有赋值也是一致行为；
- destructor body 的唯一显式 owner 是 `DrawBuffer`，automatic member逆序析构一致；
- `AddRef/Release` 的源码 token 和边界一致；
- inline `Set/GetDrawDeviceData`、`GetPrimaryLayer` 都是裸槽位一致；
- `tTJSNI_BaseLayer::Construct` 的 `new -> publish -> attach -> register` 一致；
- `tTJSNI_BaseLayer::Invalidate` 的 `detach -> unregister -> release -> clear` 一致。

本轮没有语义修补；只在 `LayerManager.h/.cpp` 与 `LayerIntf.cpp` 写入上述取证注释。注释不包含 native
绝对地址，ABI offsets只保留在本报告。

## 8. IDB 写回

四个 canonical IDB 都写回了带 `_guess` 的函数身份、函数签名、V279 owner/lifetime 注释与书签，并在
保存后独立 cold-open fresh 回读关键函数。

| 目标 | comments | bookmarks | renames | types | canonical fresh readbacks |
|---|---:|---:|---:|---:|---:|
| Android arm64 | 15 | 6 | 12 | 12 | 6 |
| Android armv7 | 14 | 6 | 12 | 12 | 6 |
| iOS arm64 | 16 | 6 | 16 | 14 | 6 |
| iOS armv7 | 17 | 6 | 17 | 14 | 6 |
| 合计 | 62 | 24 | 57 | 52 | 24 |

iOS armv7 额外经过不同路径 candidate 写入、compressed save、6 函数 candidate fresh readback、
pre-V279 canonical/loose component 可恢复备份、verified packed 发布、canonical 独立 auto-analysis/save、
移开 active loose work files、再次 cold-open 6 函数回读。四端最终 health 均曾在无 stale loose work file
干扰的 cold-open 中确认 `auto_analysis_ready=true`、`hexrays_ready=true`，module/input/imagebase 对应
各自目标。

最终 canonical IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368,547,379 | `B6077CFBE3A581CD14B9F1A74B5A8FD53E35C3BDCDEFBBFFF23D18791BC32DB9` |
| Android armv7 | 346,739,067 | `398CAF334F375A987B97FF86C7AB66660ABF6600A6D89B3D25974ECC5DC93614` |
| iOS arm64 | 336,228,270 | `6E1E8E7A29CDF5B5F9A370FB09BBB2827F0D4B24954558AB41F38C37DFFCF2F8` |
| iOS armv7 | 377,376,687 | `C4D07E28277D707E6EAC85FA7FF0991DA544DCE7FD5A967145E8B0A80D7D4B3E` |

pre-V279 packed/loose components、candidate、发布前后 loose work files 均保存在：

`out/idb-recovery/v279-layer-manager-release-ownership/`

本轮只移动 active IDA loose work components到上述 recovery 目录，没有不可恢复删除。

## 9. 构建与审计

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种 `motionplayer-dll.cpp` syntax-only 均 exit 0，
  只有既有 `_tss` literal-operator warning；
- Web、Wasmtime main、Wasmtime guest 三目标均重新编译/链接成功；随后三目标复跑均为
  `ninja: no work to do`；
- `ctest --test-dir out/web/debug` 与 `ctest --test-dir out/wasmtime/debug` 均 exit 0，两个目录当前都
  报 `No tests were found!!!`；
- Node `WebAssembly.validate` 与 `new WebAssembly.Module` 对三份产物全部成功；imports/exports 保持
  `539/69`、`538/69`、`445/87`；
- Wasmtime core object 定向反汇编确认 `AddRef` 是普通 i32 `+1/store`，`Release` 是
  `count==1 -> vtable deleting dtor`、否则 `-1/store`；complete destructor 仍是动态删除
  `DrawBuffer` 后按 `UpdateRegion -> AllNodes -> Modal -> TouchCapture` 收束；
- `git diff --check` exit 0；收尾 IDA MCP session 为 0，IDA GUI/batch process 为 0。

构建产物：

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85,655,133 | `539 / 69` | `ED302DA152AE45E40932A1547AC12029D92338A9EA6E5702AF09F67536F2D98D` |
| Wasmtime `index.wasm` | 85,002,274 | `538 / 69` | `76205ACFBEECB3699C79E40FC2E5E6DBD998C697A5CC5CB147E4D1E0F598A128` |
| Wasmtime guest | 151,508,398 | `445 / 87` | `7A4760A4EB07B1C6E0310E162E6C9333CF130E514DC50E5485B02DDDCDE52EC0` |

| 产物 | FUNCTION | GLOBAL | CODE | DATA | `.debug_str` |
|---|---:|---:|---:|---:|---:|
| Web | `0x1BD30` | `0xD5C2` | `0x1A40FFF` | `0x5A3E40` | — |
| Wasmtime main | `0x1BA4F` | `0xD5EA` | `0x19E8FAD` | `0x5A1090` | — |
| guest | `0x16190` | `0xB1C3` | `0x13D7E10` | `0x4D1630` | `0x1530816` |

Web/main 总大小与 V278 相同，guest 总大小增加 23 bytes；三者上述选定 section 长度均保持不变，
所以 guest 的长度增量位于表外 metadata/custom section。总 hash 已变化；section 长度相同不等于
字节内容相同，因此不把 hash 差异
强行归因于某一个 custom section。关键 manager 方法已另做 Wasmtime object 定向反汇编，控制流与
本轮结论一致。

## 10. 下一层

下一层可以沿两条高价值链继续：

1. `DetachPrimary` 内部 focus/capture/touch/modal owner 释放的精确重入顺序及 callback 后续 reload；
2. `tTVPDestTexture` complete destructor 内部 bitmap/texture storage、异常 terminate 与 manager
   `delete DrawBuffer` 的交接边界。
