# Motionplayer `tTVPDestTexture`、native bitmap 与延迟 texture 回收生命周期（四参考二进制）

## 1. 结论

V281 继续沿上一轮 `tTVPLayerManager::~tTVPLayerManager -> delete DrawBuffer` 的边界向内追踪，
以 `reference/binaries/` 中四个参考目标的 fresh IDA 取证为唯一原生依据，恢复了：

- `tTVPDestTexture` 的内联构造、发布、完整析构与 deleting destructor；
- `tTVPBaseTexture -> iTVPBaseBitmap -> tTVPNativeBaseBitmap` 的成员布局和析构顺序；
- `Bitmap`、`PrerenderedFont`、`CachedText`、`Font.Face` 的所有权交接；
- `iTVPTexture2D::Release` 的最终引用行为及全局 `std::vector<iTVPTexture2D *>` 三指针控制块；
- Android old-libstdc++ 与 iOS libc++ 的扩容/clear/静态析构差异；
- `RecycleProcess` 在重复入队、析构期重入、扩容异常和进程退出时的精确边界。

最重要的结论是：

```text
tTVPLayerManager::~tTVPLayerManager
  -> delete DrawBuffer
     -> tTVPDestTexture deleting destructor
        -> tTVPNativeBaseBitmap complete destructor
           -> Bitmap->Release()
              if RefCount == 1:
                  deferredTextures.push_back(Bitmap)
                  // RefCount 仍为 1；此处不 delete texture
              else:
                  --RefCount
           -> PrerenderedFont->Release()
           -> destroy CachedText
           -> destroy Font.Face
        -> operator delete(DrawBuffer wrapper)

later, at the next frame/explicit exit/at-exit callback:
  iTVPTexture2D::RecycleProcess
    -> virtual deleting destructor for each queued texture
    -> deferredTextures.clear()   // retains allocation
```

因此源码中的 `delete DrawBuffer` 只同步删除 `tTVPDestTexture` 包装对象。其底层 texture 在最终引用时
仍以 `RefCount == 1` 留在延迟队列中，直到后续 `RecycleProcess` 才真正虚析构并释放 storage。

当前 portable implementation 的可执行语义已经与四端共同结构一致。本轮没有加入现代化 guard、
swap queue、原子引用计数、指针清零或构造期 `Bitmap = nullptr`；这些看似安全的修改都会改变参考边界。
本轮仅补充源码证据注释，并把函数名、地址、字段偏移和 ABI 差异写入本报告与四个 IDB。

## 2. 四端函数映射

名字带 `_guess`，因为这些私有函数没有保留可直接采用的完整源码符号；身份由 manager caller、
虚表槽、字段偏移、全局 vector xref、frame/exit caller 和四端共同控制流交叉确认。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| manager GetDrawTarget/create path | `tTVPLayerManager_GetDrawTargetBitmap_guess@0x834188` | `...@0x64AB7C` | `...@0x10031B578` | `...@0x3205F0` |
| manager GetOrCreate path | `tTVPLayerManager_GetOrCreateDrawBuffer_guess@0x8344A0` | `...@0x64AD98` | `...@0x10031B850` | `...@0x32096C` |
| `tTVPNativeBaseBitmap` ctor | `0xA74C68` | `0x79825C` | `0x10004FC50` | `0x4EFBC` |
| `tTVPBaseTexture` ctor | `0x7FBFF4` | `0x62BBF4` | `0x100409C34` | `0x3F1A0C` |
| base-texture ctor cleanup | inline `0x7FC074` | recovered `0x62BC4E` | `0x100409CB8` | SjLj inline `0x3F1ACC` |
| outer new cleanup | inline `0x834574` | recovered `0x64AE1C` | `0x10031B8FC` | SjLj inline `0x320A50` |
| derived complete dtor entry | shared base `0xA74E04` | shared base `0x79837C` | thunk `0x10031D52C` | thunk `0x322332` |
| derived deleting dtor | `0x836DF4` | `0x64C402` | `0x10031D530` | `0x322336` |
| native-base complete dtor | `0xA74E04` | `0x79837C` | `0x10004FE0C` | `0x4F234` |
| native-base noexcept cleanup | inline `0xA74E70` | recovered `0x7983B4` | `0x10004FE6C` | SjLj `0x4F2DC` |
| `tTVPPrerenderedFont::Release` | `0x843E30` | `0x65486C` | `0x100362C50` | `0x365E94` |
| `iTVPTexture2D::Release` | `0x846C48` | `0x6563C4` | `0x100323480` | `0x3288F4` |
| deferred vector grow | inline in `0x846C48` | `0x668C14` | `0x10033E818` | `0x340D68` |
| `RecycleProcess` | `0x846BE8` | `0x656388` | `0x100323408` | `0x3288B0` |
| vector storage dtor | `0x846BD8` | `0x656376` | thunk `0x100323404` | thunk `0x3288AC` |
| RenderManager static init | `0x42FA24` | `0x301EAC` | `0x10033F050` | `0x3415F4` |
| per-frame recycle caller | `TVPMainScene::update+0x38 @ 0xA9FE3C` | `update+0x20 @ 0x7B701C` | `0x100E4163C` | `0xC4EA56` |
| Android explicit-exit caller | `0x923A2C` | `0x6D57C8` | 不适用 | 不适用 |

`tTVPDestTexture` vtable address point 分别是：

- Android arm64 `0x1A23B08`；
- Android armv7 `0x10BF7A4`；
- iOS arm64 `0x1019B44F0`；
- iOS armv7 `0x177B250`。

64-bit deleting slot 是 address point `+8`，32-bit 是 `+4`。Android 两端把 derived complete slot
直接合并到 base complete dtor；iOS 两端保留一个只跳转到 base complete dtor 的 derived thunk。
这是链接/ABI 合并差异，不是共享源码类层次差异。

## 3. 对象布局与源码层次

四端共同证明的源码层次仍是：

```cpp
class tTVPNativeBaseBitmap {
    virtual ~tTVPNativeBaseBitmap();
    tTVPFont Font;
    bool FontChanged;
    int GlobalFontState;
    tTVPPrerenderedFont *PrerenderedFont;
    // ascent/cache scalar fields
    int TextWidth, TextHeight;
    ttstr CachedText;
    iTVPTexture2D *Bitmap;
};

class iTVPBaseBitmap : public tTVPNativeBaseBitmap { ... };
class tTVPBaseTexture : public iTVPBaseBitmap { ... };

class tTVPDestTexture : public tTVPBaseTexture {
    bool HoldAlpha = true;
};
```

本轮与析构直接相关的字段布局如下：

| 字段 | 64-bit offset | 32-bit offset | 析构角色 |
|---|---:|---:|---|
| native-base vptr | `+0x00` | `+0x00` | complete dtor 开头切回 base vtable |
| `Font.Height/Flags/Angle` | `+0x08/+0x0C/+0x10` | `+0x04/+0x08/+0x0C` | 标量，无析构动作 |
| `Font.Face` | `+0x14` | `+0x10` | 最后销毁的 owning `ttstr` backing |
| `FontChanged` | `+0x1C` | `+0x14` | 标量 |
| `GlobalFontState` | `+0x20` | `+0x18` | 标量 |
| `PrerenderedFont` | `+0x28` | `+0x1C` | 裸 owning ref，直接 `Release` |
| `TextWidth/TextHeight` | `+0x44/+0x48` | `+0x34/+0x38` | 标量 |
| `CachedText` | `+0x4C` | `+0x3C` | 倒数第二个 owning string |
| `Bitmap` | `+0x58` | `+0x40` | 虚调用 `iTVPTexture2D::Release` |
| derived `HoldAlpha` | `+0x60` | `+0x44` | 标量，无析构动作 |

derived allocation 为 64-bit `0x68` bytes、32-bit `0x48` bytes。没有单独的 derived owning member；
derived 析构的实际工作全部落在 native-base complete dtor，随后 deleting wrapper raw-delete 整个 allocation。

## 4. 构造、发布与异常边界

四端 GetDrawTarget/GetOrCreate 路径共同恢复为：

```text
raw = operator new(0x68 or 0x48)
tTVPBaseTexture::tTVPBaseTexture(raw, w, h)
    tTVPNativeBaseBitmap::tTVPNativeBaseBitmap(raw)
    if w == 0: w = 1
    if h == 0: h = 1
    raw.Bitmap = TVPGetRenderManager()->CreateTexture2D(
        nullptr, 0, w, h, RGBA)
install tTVPDestTexture vptr
raw.HoldAlpha = true
manager.DrawBuffer = raw
raw.Fill(full_rect, black)
raw.HoldAlpha = manager.HoldAlpha
```

vptr 安装和 `HoldAlpha = true` 的指令次序会因编译器调度略有不同，但两者都在构造完成、manager
发布前发生。`DrawBuffer` 在 `Fill` 前发布，最终 manager `HoldAlpha` copy 则在 `Fill` 正常返回后。
因此：

- base/derived 构造逃逸：outer new-expression cleanup raw-delete allocation，不发布 `DrawBuffer`；
- 构造完成后 `Fill` 逃逸：`DrawBuffer` 已发布，保留有效 derived wrapper；
- 此时 final `SetHoldAlpha` 尚未执行，derived 值仍是构造默认 `true`；
- 没有 rollback guard 或发布撤回。

### 4.1 `Bitmap` 的有意未初始化窗口

四个 `tTVPNativeBaseBitmap` ctor 都初始化 Font、`PrerenderedFont = nullptr`、font state 和 text-size
cache，却都不写 `Bitmap`。随后 `tTVPBaseTexture` ctor 才把 `CreateTexture2D` 返回值赋给该字段。

这意味着如果 render-manager lookup 或 `CreateTexture2D` 在赋值前以 C++ 异常逃逸，编译器生成的
base cleanup 会调用 `tTVPNativeBaseBitmap::~tTVPNativeBaseBitmap`，而后者会读取尚未初始化的
`Bitmap`，甚至可能经随机非空值执行虚调用。四端都保留同一 partial-construction UB。

当前源码同样不在 native-base ctor 初始化 `Bitmap`。不能为了“安全”补 `Bitmap = nullptr`，因为它会
实质改变四端共同证明的异常边界。普通 null dereference/crash 本身不发生 C++ unwind；这里记录的是
lookup/virtual call 以异常逃逸时的 cleanup 行为。

## 5. 完整析构与 deleting destructor

manager 的源码级 `delete DrawBuffer` 通过 base pointer 的虚表 deleting slot 进入 derived wrapper。
四端完整控制流是：

```text
tTVPDestTexture deleting dtor(this):
    tTVPDestTexture complete dtor(this)
        // Android: slot directly aliases native-base complete dtor
        // iOS: one-instruction thunk reaches native-base complete dtor
        install tTVPNativeBaseBitmap vptr
        if this->Bitmap != null:
            this->Bitmap->Release()        // texture vtable slot 2
        if this->PrerenderedFont != null:
            this->PrerenderedFont->Release()
        destroy this->CachedText
        destroy this->Font.Face
    operator delete(this)                  // unsized; only after normal return
```

关键顺序和缺失动作：

- `Bitmap` 在 `PrerenderedFont` 前 release；
- 两个裸字段都不在调用前或调用后清零；
- derived `HoldAlpha` 没有独立清理；
- `CachedText` 在 `Font.Face` 前析构；
- deleting wrapper 只在 complete dtor 正常返回后调用 unsized `operator delete`；
- manager 的 `DrawBuffer` 槽也不在 `delete` 前清零。

### 5.1 隐式 `noexcept` cleanup

base complete dtor 的异常 landing 四端共同表达：

```text
if Bitmap->Release or PrerenderedFont->Release escapes:
    destroy remaining CachedText
    destroy remaining Font.Face
    terminate
```

Android arm64 把 landing 合并在 `0xA74E70`，Android armv7 本轮从未定义 Thumb bytes 恢复为
`0x7983B4`，iOS arm64 为独立 `0x10004FE6C`，iOS armv7 通过 `0x4F2DC` SjLj cleanup。

cleanup 不会重试尚未执行/已经逃逸的裸指针 `Release`。由于 complete dtor 不正常返回，derived
deleting wrapper 的 raw `operator delete(this)` 也不会执行。尤其当 `Bitmap->Release()` 的 vector
扩容抛出 `bad_alloc` 时：texture 保持 `RefCount == 1` 且未入队；string 成员被清理；随后 terminate。

## 6. `tTVPPrerenderedFont::Release`

四端 helper 都是普通非原子引用计数：

```text
if RefCount == 1:
    complete destructor
    operator delete(this)
else:
    --RefCount
```

最终路径同样不先写 0；非最终路径没有 zero guard，0 会按普通 32-bit 减法变为 `0xFFFFFFFF`。
它与 texture 的关键区别是：prerendered font 在最终 release 时同步析构和释放；texture 最终 release
只入队，真正删除被推迟。

## 7. `iTVPTexture2D::Release` 与 deferred vector

四端共享源码级结构与当前 `RenderManager.cpp` 完全一致：

```cpp
static std::vector<iTVPTexture2D *> deferredTextures;

void iTVPTexture2D::Release() {
    if(RefCount == 1)
        deferredTextures.push_back(this);
    else
        --RefCount;
}
```

64-bit texture `RefCount` 位于 `+8`，32-bit 位于 `+4`。最终分支有以下精确性质：

- 不把 `RefCount` 写成 0，也不减到 0；
- 不执行 `delete this`；
- vector 中保存裸 pointer，不 AddRef；
- 没有 duplicate/null/already-queued guard；
- 同一对象在 recycle 前再次 `Release()` 仍看到 1，会再次压入同一 pointer；
- `RefCount == 0` 走 else 并写成 `-1`；
- 所有计数和 vector 操作都没有原子/锁保护。

`push_back` 扩容异常前不会修改 `RefCount`。append 无需扩容时只是 store pointer 并推进 end；扩容
失败时 vector 保持旧控制块，texture 仍是 count 1、未入队，异常原样向 caller 传播。

### 7.1 三指针控制块

四端都是标准 vector 的 `begin/end/capacity_end`：

| 目标 | begin | end | capacity_end | element width |
|---|---:|---:|---:|---:|
| Android arm64 | `0x1ADBB60` | `0x1ADBB68` | `0x1ADBB70` | 8 |
| Android armv7 | `0x113570C` | `0x1135710` | `0x1135714` | 4 |
| iOS arm64 | `0x101B934A8` | `0x101B934B0` | `0x101B934B8` | 8 |
| iOS armv7 | `0x18A3D10` | `0x18A3D14` | `0x18A3D18` | 4 |

本轮已在四个 IDB 中分别命名为 `deferred_textures_begin/end/capacity_end_guess`。

### 7.2 Android old-libstdc++ 扩容

Android arm64 把完整 growth 内联在 `Release@0x846C48`；armv7 调用 `0x668C14`，其 capacity
recommendation 为 `0x668C80`，allocation helper 为 `0x668CC0`。两端共同逻辑是：

```text
oldSize = (end - begin) / sizeof(pointer)
newCapacity = oldSize + max(oldSize, 1)
clamp/throw against max_size
newBegin = operator new(newCapacity * sizeof(pointer))
newBegin[oldSize] = this
memmove(newBegin, oldBegin, oldSize * sizeof(pointer))
operator delete(oldBegin)
begin = newBegin
end = newBegin + oldSize + 1
capacityEnd = newBegin + newCapacity
```

max element count 为 arm64 `0x1FFFFFFFFFFFFFFF`、armv7 `0x3FFFFFFF`。旧元素是 trivially
copyable raw pointers，所以使用 `memmove`，没有逐项 AddRef/析构。

### 7.3 iOS libc++ 扩容

iOS arm64 `0x10033E818` 与 armv7 `0x340D68` 恢复为 libc++ split-buffer 风格：

```text
required = size + 1
recommended = max(required, 2 * capacity)
clamp/throw against max_size
construct temporary split buffer(recommended)
append this
swap/steal temporary storage into vector
destroy temporary old-storage holder
```

iOS armv7 额外显式展开 SjLj cleanup。临时 allocation 或迁移逃逸时会清临时 buffer 并 resume；
原 vector 控制块在 commit 前保持有效。虽然满容量 push 时数值增长通常也表现为空 vector -> 1、
非空翻倍，但 Android/iOS 的 STL helper 结构和 EH ABI 必须作为编译器/标准库差异保留。

## 8. `RecycleProcess` 的容器与重入边界

四端共同机器语义是：

```text
savedBegin = deferredTextures.begin
savedEnd = deferredTextures.end
for (it = savedBegin; it != savedEnd; ++it):
    tex = *it
    if tex != null:
        call tex virtual deleting destructor

// std::vector<raw pointer>::clear(), capacity retained
deferredTextures.end = deferredTextures.begin
```

Android 两端直接 reload live `begin` 并赋给 live `end`。iOS 两端展开 libc++ 的 trivial-element
`clear` pointer arithmetic，但结果同样是把当前 end 退回当前 begin。四端都：

- 不释放 vector allocation；
- 不把旧 pointer bytes 写零；
- 不在调用 deleting destructor 前 erase 当前 entry；
- 显式跳过 null entry；
- 不捕获 deleting destructor 的异常。

### 8.1 重入 append 不扩容

若某个 queued texture 的析构过程对另一个 count-1 texture 调用 `Release()`，且 vector 尚有容量：

1. 新 pointer 被写在 live old-end 后；
2. recycle loop 使用进入函数时快照的 `savedEnd`，所以本轮不访问新 entry；
3. loop 尾部 `clear()` 操作当前 live vector，把 end 直接退回 begin；
4. 新 entry 被静默丢弃，不会留到下一轮，pointee 泄漏其最后一个未消费的 count-1 生命周期。

这正是普通 range-for + `clear()` 的共享源码边界；不能改成 while-pop、swap-local 或两队列而仍声称
一比一复原。

### 8.2 重入 append 触发扩容

若析构期 append 触发 growth，旧 vector allocation 被释放并替换，但 loop 的 `it/savedEnd` 仍指向旧
allocation：

- 若还有未处理 snapshot entry，下一次 dereference 访问已释放 storage；
- 即使刚好处理最后一个 entry，随后的旧指针递增/比较仍处于失效迭代器边界；
- normal tail 仍会清空新 live vector，导致重入 append 丢失。

这是容器迭代器 UB，报告只记录控制流，不承诺释放内存中的任意后续值。

### 8.3 重复 pointer

由于 `Release(1)` 不改变计数也不做 queued 标记，recycle 前重复调用可形成 `[p, p]`。loop 对第一项
执行 deleting destructor 后，第二项仍尝试从同一已释放 object 读取 vptr 并再次删除，形成 UAF/
double-delete 边界。普通 owner 计数正确时不会自然制造重复项，但实现本身没有防护。

## 9. 静态生命周期和调用点

四端 RenderManager static init 都按以下顺序执行：

```text
deferredTextures.begin = null
deferredTextures.end = null
deferredTextures.capacityEnd = null
__cxa_atexit(vectorStorageDestructor, &deferredTextures, dso)
register tTVPAtExit(priority = 1500, iTVPTexture2D::RecycleProcess)
// continue render-manager preference/factory registration
```

vector static destructor只处理 vector allocation；raw pointer element 是 trivial，不删除 pointee。若最终
at-exit recycle 后又产生新 queued item，或 reentrant item 被 clear 丢失，vector storage dtor 不会补删它。

正常帧路径中，四端都在 application `Run()` 返回后立即调用 `RecycleProcess`，再进入 post-update/FPS
统计等逻辑。Android 的 `TVPExitApplication` 还在 compact event 之后、且仅当 render manager 不是
software 时显式 recycle，然后才调用 Java exit/`exit(code)`。此外 priority-1500 at-exit callback 提供
静态退出阶段的统一入口。

## 10. 本地实现逐行对照

### `cpp/core/visual/LayerBitmapIntf.cpp`

- `w/h == 0` 分别提升为 1；
- 通过 `TVPGetRenderManager()->CreateTexture2D(nullptr, 0, w, h, format)` 创建；
- 返回 pointer 直接赋给 inherited `Bitmap`；
- 没有在 call 前写 `Bitmap = nullptr`。

与四端 base-texture ctor 一致。

### `cpp/core/visual/impl/LayerBitmapImpl.cpp`

- native-base ctor 初始化 Font/prerender/font-state/text-size，但不写 `Bitmap`；
- dtor 先 `Bitmap->Release()`，再 `PrerenderedFont->Release()`；
- `CachedText`/`Font.Face` 由自动成员析构随后逆序完成；
- 没有手写 clear、catch 或 fallback delete。

本轮只补注释说明有意保留 partial-construction window 与 texture deferred release。

### `cpp/core/visual/RenderManager.cpp`

- 容器精确为 `static std::vector<iTVPTexture2D *>`；
- `Release` 精确为 `if(RefCount == 1) push_back(this); else --RefCount;`；
- `RecycleProcess` 精确为 range-for `delete tex` 后 `clear()`；
- at-exit priority 精确为 `TVP_ATEXIT_PRI_RELEASE + 500 == 1500`。

本轮只补注释固定 duplicate、snapshot/end、reentry growth 和 final-count 边界；没有把队列替换为更安全
但架构不同的实现。

### `cpp/core/visual/LayerManager.cpp`

manager dtor 仍只执行 `if(DrawBuffer) delete DrawBuffer;`。本轮补充说明 wrapper 与底层 texture 的
同步/延迟删除分界，没有改变 manager owner 结构。

## 11. 验证与 IDB 持久化

- 四个目标二进制和四个 `.i64` 均在本轮开始前逐一核对；未把 `.i64` 当作第五至第八个目标；
- 四个 `Release`、四个 `RecycleProcess`、四个 native-base ctor/dtor、derived deleting dtor、
  prerender Release、static init/storage dtor 和三端独立 growth helper 均通过原生 `mcp__idalib__*`
  fresh 定位/反编译；
- Android armv7 原先未定义的 `0x7983B4`、`0x62BC4E`、`0x64AE1C` Thumb cleanup 已恢复为函数；
- 四个 IDB 已写入 `_guess` 函数名、deferred vector 三指针名、vtable/字段/异常/reentry 注释；
- 保存后关闭并重新从 canonical `.i64` 冷启动，确认名称、恢复函数和伪代码可读；
- IDA 冷启动产生的 `.id0/.id1/.nam` 工作文件已在关闭会话后删除；它们是可再生 scratch，
  `reference/binaries/` 最终恢复为严格 8 个文件；
- portable source 只改注释；`cmake --build out/web/debug` 增量构建通过（5/5），只出现仓库既有的
  `_tss` literal、LZ4 deprecated、pthread memory-growth、JSPI experimental 与 JS library warning。

## 12. 后续方向

本轮已经闭合 `manager delete DrawBuffer -> wrapper dtor -> native-base members -> deferred texture queue ->
frame/exit recycle`。下一轮可沿 texture deleting slot 向具体 software/GL texture subclasses 深入，恢复：

1. 各 concrete `iTVPTexture2D` destructor 对 CPU bitmap、Cocos `Texture2D`、adapter texture/cache 的
   实际释放顺序；
2. 删除期间是否存在会反向调用 `iTVPTexture2D::Release` 的 concrete member/callback，从而判断上述
   recycle reentry 边界在原生对象图中的可达性；
3. render-manager shutdown、compact 与 concrete texture pool/cache 的交接顺序；
4. `tTVPBaseBitmap` 非-texture 派生路径是否复用 native-base 的未初始化 `Bitmap` partial-construction
   边界，以及各 ctor 是否在 base cleanup 前完成赋值。
