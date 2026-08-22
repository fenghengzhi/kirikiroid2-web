# Motionplayer window / OGL adapter / Sprite / autorelease 生命周期（四参考二进制）

日期：2026-08-22

## 1. 结论

V284 沿 V282 已恢复的 `tTVPOGLTexture2D::GetAdapterTexture` 与
`AdapterTexture2D::_owner` 反向引用继续向窗口和 Cocos 两侧闭合，fresh 复核
`reference/binaries/` 中四个参考目标后确认：

```text
DrawDeviceObjectBase::Show
  -> form->UpdateDrawBuffer(BackTarget)          // borrowed；不 AddRef
     -> oldAdapter = DrawSprite->getTexture()
     -> result = BackTarget->GetAdapterTexture(oldAdapter)
        new-size path:
          new AdapterTexture2D                   // Cocos Ref = 1
          ++BackTarget.RefCount                  // owner 反向引用
          adapter->autorelease()                 // 只压入当前 pool，不减计数
        same-size path:
          oldAdapter->_name = BackTarget.texture
          return oldAdapter                      // 不 autorelease，不重绑 _owner
     -> if (oldAdapter == result): return         // 不调用 Sprite::setTexture
     -> DrawSprite->setTexture(result)
        retain(result)                           // 新 adapter 先 +1
        release(oldAdapter)                      // 旧 adapter 后 -1/析构
        DrawSprite->_texture = result

normal DisplayLinkDirector::mainLoop tail:
  Director::drawScene()
    -> Scheduler::update()
       -> TVPMainScene::update()
          -> Application::Run()                  // Show 位于这侧
          -> iTVPTexture2D::RecycleProcess()
    -> scene visit / renderer submission
  -> current AutoreleasePool::clear()
     -> release(new adapter creator ref)         // 2 -> 1，Sprite 留一份
```

最重要的新结论不是“same-size 分支理论上可能有旧 owner”，而是该分支在正常窗口链中可达：
`setScreenRect` 的 screen width/height 变化会释放 Front/Back targets，但下一次 `Show` 仍按独立的
primary width/height 创建 targets。screen size 改变而 primary size 不变时，会得到一个**不同的
BackTarget owner、相同的 adapter dimensions**。窗口把旧 Sprite adapter 传回 `GetAdapterTexture`，
四端都会只把 adapter 的 GL name 改成新 BackTarget 的 name，随后因为返回指针与旧指针相等而跳过
`Sprite::setTexture`。结果是：

- adapter 的唯一 Cocos 强引用仍来自 Sprite；
- adapter 的 `_owner` 仍是旧 BackTarget，旧 owner 被延寿；
- 新 BackTarget 没有 adapter 反向引用，只有 DrawDevice root 的引用；
- adapter 显示的新 GL name 与维持 adapter 存活的 owner 不再是同一对象；
- root 以后最终 Release 新 BackTarget 时，它可以进入 Kirikiri deferred queue，而 adapter 仍保存其
  GL name。若同一轮成功 Show 在 recycle 前再次改写 name，普通路径可避开悬空窗口；若 Window gate、
  callback 或异常阻止 UpdateDrawBuffer，则 recycle 后 adapter 可暂时指向已删除的 GL name。

当前 portable 代码的执行语义已经保留上述边界。本轮只补充证据注释，没有添加 owner rebinding、
dynamic type check、额外 retain、局部 pool clear 或安全队列。

## 2. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TVPWindowLayer::UpdateDrawBuffer` | `0xAA5954` | `0x7BADD4` | `TVPWindowLayer_UpdateDrawBuffer_guess@0x100E45F84` | `...@0xC536D0` |
| iWindowLayer adjusting thunk | `0xAA69E8` | `0x7BB748` | `0x100E46B70` | `0xC541EA` |
| OGL `GetAdapterTexture` | `0xA4E394` | `0x785682` | `0x1002E3E00` | `0x2E3484` |
| adapter ctor | `0xA64F04` | `0x78E96C` | `0x1002F16C0` | `0x2F1C7C` |
| adapter complete dtor | `0xA650E0` | `0x78EA58` | `0x1002F185C` | `0x2F1E80` |
| `Sprite::setTexture(Texture2D*)` | `0x123D4DC` | `0xC01E18` | `0x10117F9F0` | `0xFF10B4` |
| Sprite complete dtor body | `0x123D258` | `0xC01C94` | `0x10117F814` | `0xFF0DF8` |
| `Texture2D` ctor | `0x12CB51C` | `0xC490C4` | `0x1011FFFE0` | `0x1091300` |
| `Ref` ctor | `0x1293A64` | `0xC2D6E8` | `0x1012780B0` | `0x1120370` |
| `Ref::retain` | `0x1293B58` | `0xC2D77C` | `0x1012781A0` | `0x112049C` |
| `Ref::release` | `0x1293B68` | `0xC2D784` | `0x1012781B0` | `0x11204A4` |
| `Ref::autorelease` | `0x1293B8C` | `0xC2D79A` | `0x1012781D4` | `0x11204BA` |
| `AutoreleasePool::addObject` | `0x1274E2C` | `0xC1DA10` | `0x100FC9238` | `0xDE2888` |
| `AutoreleasePool::clear` | `0x1274D50` | `0xC1D980` | `0x100FC91BC` | `0xDE27C0` |
| `PoolManager::getCurrentPool` | `0x12750B0` | `0xC1DB74` | `0x100FC93FC` | `0xDE2AAE` |
| `DisplayLinkDirector::mainLoop` | `0x12862A8` | `0xC260DE` | `0x101058904` | `0xE8FCA8` |
| `Director::drawScene` | `0x1283B34` | `0xC24AC0` | `0x101057138` | `0xE8E46C` |
| `TVPMainScene::update` | `0xA9FE04` | `0x7B6FFC` | `0x100E41600` | `0xC4EA18` |
| `TVPDrawSceneOnce` | `0xA9C064` | `0x7B4910` | `0x100E3E564` | `0xC4B830` |
| DrawDevice root `Show` | `0x531890` | `0x495978` | `0x100234294` | `0x232F1C` |
| root `setScreenRect` | `0x52BA98` | `0x492E0C` | `0x100230F38` | `0x22FD80` |

iOS Cocos 和窗口函数原始符号被裁剪；本轮写入的名字均保留 `_guess`。身份不是按地址接近猜测：
窗口函数由 `TVPWindowLayer` RTTI、多个 secondary vtable header 和 Android 对应虚槽交叉定位；
Sprite 函数由四端 `N7cocos2d6SpriteE` RTTI 字符串边界、primary vtable 和 Android 导出符号对齐；
Ref/autorelease 函数则从 OGL adapter 新建路径的直接调用链反向闭合。

## 3. `TVPWindowLayer` 虚表与 identity gate

四端 `iWindowLayer` secondary vtable 的 `UpdateDrawBuffer` 都位于 address point 后第 22 槽：

| 目标 | secondary-this 调整 | 槽宽偏移 | thunk -> body |
|---|---:|---:|---|
| Android arm64 | `-1040` | `22 * 8 = 176` | `0xAA69E8 -> 0xAA5954` |
| Android armv7 | `-800` | `22 * 4 = 88` | `0x7BB748 -> 0x7BADD4` |
| iOS arm64 | `-1008` | `22 * 8 = 176` | `0x100E46B70 -> 0x100E45F84` |
| iOS armv7 | `-804` | `22 * 4 = 88` | `0xC541EA -> 0xC536D0` |

各目标的 Cocos 类布局不同，但控制流完全相同：

```text
if tex == null: return
old = DrawSprite->virtual getTexture()
new = tex->virtual GetAdapterTexture(old)
if old == new: return
DrawSprite->virtual setTexture(new)
tex->GetScale(...)
calculate texture rect
DrawSprite->setTextureRect(...)
DrawSprite->setBlendFunc(DISABLE)
ResetDrawSprite()
```

具体 Sprite 虚槽体现 Cocos 版本差异：Android `setTexture/getTexture` 为 slot `165/166`
（arm64 byte `1320/1328`，armv7 `660/664`）；iOS 为 slot `168/169`
（arm64 `1344/1352`，armv7 `672/676`）。这类平台 Cocos ABI 偏移不能写入 portable 类布局。

identity gate 比“避免重复设置”更强：同指针时 scale、rect、blend 和 `ResetDrawSprite` 也全部跳过。
所以 same-size name-only update 不仅不改变引用计数，还保留上一次设置时计算出的 rect/scale。若新旧
owner 尺寸相同但内部 scale 语义不同，窗口不会重新取 `GetScale`；这同样属于四端共同边界。

## 4. `Sprite::setTexture` 的精确强引用协议

Android 和 iOS 的 surrounding backend/material 条件不同，但实际 texture replacement block 一致：

```text
old = Sprite._texture
if old != incoming:
    if incoming != null:
        incoming->retain()
        old = Sprite._texture       // retain 后重读
    if old != null:
        old->release()
    Sprite._texture = incoming
    Sprite->updateBlendFunc()
```

顺序必须保留为 **retain new -> release old -> store new**：

- incoming 与 old 相同时整个 block 不执行；
- retain 是普通 32-bit `++RefCount`，不会分配或调用虚函数；
- release 是普通 `--RefCount`，变成零时立刻调用虚 deleting destructor；
- 因而不同 adapter 替换时，新 adapter 在旧 adapter 析构及其 `_owner->Release()` 前已经安全；
- 若旧 adapter 的 Cocos count 仍为 2（Sprite + 尚未清掉的 autorelease debt），这次 release 只到 1，
  旧 adapter dtor 延后到 pool clear；若 count 已为 1，则在 `setTexture` 中同步析构；
- store 位于旧 release 正常返回之后，没有 transactional rollback。

四端 Sprite complete dtor 也都会先 release 一个相邻 Cocos member，再 release `_texture`：Android
texture 字段为 arm64 `+832`、armv7 `+664`；iOS 为 arm64 `+800`、armv7 `+644`。因此 Sprite
销毁与 setTexture replacement 使用同一个最终 adapter release 机制。

`TVPWindowLayer::init` 创建普通 `Sprite::create()`，再把它作为 child 加到 `PrimaryLayerArea`；
Android 两端还直接证明 Sprite ctor 把 batch-node 字段初始化为 null。窗口 DrawSprite 走上述普通
strong-texture block，不是 batch-node texture-atlas 的只读旁路。

## 5. Adapter 初始引用与 autorelease 容器

四端 `Texture2D` ctor 都先调用 `Ref` ctor；`Ref` ctor把引用计数写为 1：64-bit 位于 `+8`，
32-bit 位于 `+4`。OGL adapter ctor随后：

```text
Texture2D::Texture2D()        // adapter Cocos count = 1
install Adapter vptr
adapter._name = owner.texture
adapter._owner = owner
++owner.RefCount
initialize dimensions/format/program state
```

`Ref::autorelease` 本身不改 count：

```text
pool = PoolManager::getInstance()->getCurrentPool()
pool->addObject(this)
return this
```

`getCurrentPool` 直接返回 PoolManager pool-pointer vector 的最后一个元素，没有 empty guard。
`addObject` 对 `std::vector<Ref *>` 做 raw pointer append，不 retain、不查重。64-bit元素宽 8，32-bit
宽 4；Android 使用 old-libstdc++ 风格 grow，iOS 使用 libc++ helper，满容量时都可能分配并抛出。

四端 `AutoreleasePool::clear` 不是保留 capacity 的普通 `vector::clear()`，而是先 detach：

```text
oldBegin = pool.begin
oldEnd   = pool.end
pool.begin = null
pool.end = null
pool.capacityEnd = null

for p in [oldBegin, oldEnd):
    p->release()

operator delete(oldBegin)
```

因此：

- release 顺序是 enrollment 顺序；
- pool 中重复 pointer 会被重复 release，没有去重；
- release/destructor 中若再次 autorelease，新对象进入已经置空后的新 vector，不会被本轮循环消费；
- old vector storage 在 normal tail 被释放，下一轮从新 vector 继续；
- 这与 Kirikiri deferred texture queue 的 snapshot-loop + live-vector `clear()` 不同：Cocos 的重入 append
  不会被尾部清空掉，也不会使当前旧数组迭代器失效。

V285 对这里补充一个必须区分的生命周期边界：上面的“下一轮继续”只适用于 **pool 本身继续存活**
的普通 `clear()`（例如 DisplayLinkDirector 的 root pool）。`AutoreleasePool::~AutoreleasePool()` 的顺序是
`clear() -> PoolManager::pop() -> string/vector member destruction`，而 clear 期间该 pool 仍是 current pool。
因此 release/dtor 中新产生的 autorelease 会写入正在析构对象的新 vector；析构尾部对
`std::vector<Ref *>` 只释放 raw-pointer storage，不会逐项调用 `Ref::release()`。这些重入条目不会进入
下一层 pool，也不会再被 clear，所代表的 autorelease debt 会被静默丢弃。完整四端证据见
`motionplayer_window_scenegraph_nested_pool_teardown_four_binary_2026-08-22.md`。

## 6. 首次绑定与普通稳定帧的计数

设 DrawDevice 新建的 BackTarget 为 `O0`，adapter 为 `A`。普通第一次发布：

| 时点 | `O0` Kirikiri count | `A` Cocos count | 持有关系 |
|---|---:|---:|---|
| root 获得 Create 返回值 | 1 | - | root -> O0 |
| adapter ctor 完成 | 2 | 1 | root -> O0；A -> O0 |
| `A->autorelease()` | 2 | 1 | pool 记录 A，不改 count |
| `Sprite::setTexture(A)` retain 后 | 2 | 2 | Sprite -> A；pool debt -> A；A -> O0 |
| 正常 main-loop pool clear 后 | 2 | 1 | Sprite -> A；A -> O0；root -> O0 |

后续未重建的普通 Show 把同一个 `O0` 和 `A` 送入 same-size 分支：只重复写入同一个 GL name，
所有 count 均不变，也没有新的 autorelease enrollment。

## 7. 不同尺寸 replacement 的两种析构时点

旧 root reference 通常会先被 target invalidation/reallocation Release，使旧 owner 从 2 降为 1，
只剩 adapter 反向引用。随后不同尺寸产生新 adapter `B`。

### 7.1 旧 adapter 的 pool debt 已清

```text
B ctor/autorelease: B count 1, new owner count 2
Sprite::setTexture(B):
    retain B            // 1 -> 2
    release A           // 1 -> 0，立即进入 A dtor
      A._name = 0
      oldOwner.Release  // count 1，压入 Kirikiri deferred vector
      Texture2D dtor + raw delete A
    Sprite._texture = B
later Cocos clear:
    release B           // 2 -> 1
```

若这次 replacement 位于 `TVPMainScene::update -> Application::Run` 内，oldOwner 在随后同一次
`iTVPTexture2D::RecycleProcess` 中即可真正删除。

### 7.2 旧 adapter 仍带 pool debt

若 `A` 与 `B` 在同一个尚未 clear 的 pool 周期内先后创建，replacement 时 A count 为 2：

```text
setTexture release A: 2 -> 1       // 不析构
main-loop clear old pool entries:
    release A: 1 -> 0              // 此时才 dtor，oldOwner 入 deferred vector
    release B: 2 -> 1
```

该 owner 入队发生在完整 `drawScene` 之后，而本帧 `TVPMainScene::update` 的 Kirikiri recycle 已经
执行完，所以 oldOwner 正常要等到后续 scheduler tick 才真正删除。nested pool 时以各自当前 pool 的
clear 点为准，不能把“autorelease”简化为固定的“本帧结束删除”。

## 8. same-size 不同 owner 的正常可达状态机

四端 `setScreenRect(left, top, screenW, screenH)` 都：

1. 总是保存 left/top；
2. 只有 screenW 或 screenH 变化时保存新 screen size；
3. Release/null FrontTarget；
4. Release/null BackTarget；
5. 写 root state byte 1。

而 `setPrimarySize(primaryW, primaryH)` 只保存另一对尺寸并通知 Window；`Show` 重建 texture 时使用
primary dimensions，不使用 screen dimensions。故以下序列完全属于正常公开路径：

```text
before screen change:
    old Back O0 count = 2           // root + adapter A
    A count = 1                     // Sprite
    A._owner = O0
    A._name = O0.texture

setScreenRect changes only screen size:
    root releases O0: 2 -> 1
    root BackTarget = null

next Show:
    create O1 with same primary dimensions, O1 count = 1
    UpdateDrawBuffer(O1)
      GetAdapterTexture(A):
        A._name = O1.texture
        return A
      old == new, so skip Sprite::setTexture

after Show:
    A count = 1                     // Sprite
    O0 count = 1                    // A._owner，旧 owner 被延寿
    O1 count = 1                    // root only，新 name 没有 adapter owner ref
    A._owner = O0
    A._name = O1.texture
```

后续影响：

- 再次 target invalidation 对 `O1.Release()` 是 final release，会直接把 O1 压入 deferred vector；
- 若同一轮 Show 成功创建 O2 并在 recycle 前调用 UpdateDrawBuffer，A 的 name 会先改成 O2.name，
  再删除 O1，普通连续渲染路径通常不悬空；
- 若 Window 为 null、form 为 null、Show/child callback 抛出或其它 gate 阻止 UpdateDrawBuffer，
  recycle 可以先删除 O1 的 GL texture，而 A 仍保存 O1.name；
- A 最终 replacement 或 Sprite 析构时，A dtor释放的是 O0，不是当前 name 对应的 O1/O2；
- O0 的 final Release 仍只入 Kirikiri deferred vector，实际 GL 删除取决于后续 RecycleProcess。

这些行为不能通过给 same-size update 增加 `_owner` rebinding“修复”；那会改变四个参考实现共同证明的
引用转移、deferred-delete 时点和异常边界。

## 9. frame / render / pool 的精确相对顺序

四端 `Director::drawScene` 都先 dispatch before-update event、调用 `Scheduler::update`、dispatch
after-update event，然后才 clear renderer、visit running scene、submit/render。四端
`TVPMainScene::update` 第一段都是：

```text
Application-side Run/callback
iTVPTexture2D::RecycleProcess
post-update/FPS work
```

四端 `DisplayLinkDirector::mainLoop` 的正常 valid branch则是：

```text
Director::drawScene()
PoolManager::getInstance()->getCurrentPool()->clear()
```

因此同一标准帧的关键顺序是：

```text
Show / UpdateDrawBuffer / possible synchronous old-adapter dtor
Kirikiri deferred texture recycle
Sprite scene render
Cocos autorelease pool clear / possible delayed old-adapter dtor
```

这解释了第 7 节的“一次 replacement，owner 可能本帧或下一帧 recycle”差异。

另一个必须保留的边界是 `TVPDrawSceneOnce(interval)`：四端都在时间到期时执行 post-update callback、
直接 `Director::drawScene`、swap/no-op helper、更新 lastTick并返回 0；函数内**没有** getCurrentPool 或
clear 调用。它通常仍可能嵌套在外层 main loop中，由外层尾部稍后 clear，但 modal/manual 连续调用
本身不会为每次 draw 建立或清理局部 pool。不能在 portable helper尾部擅自添加 `clear()`。

mainLoop 的 invalid branch也会同时跳过 drawScene 和 pool clear；restart 路径在 restart helper 内有
独立 clear。故“autorelease 一定在创建当帧结束释放”不是精确模型，正确模型是“在 enrollment 所属
current pool 的下一次 clear 时释放一次”。

## 10. 异常和非事务边界

### 10.1 adapter constructor / autorelease

- adapter ctor 在设置 `_owner` 后直接增加 owner count，随后才构造 size/string/program 状态；
- 若 owner increment 后的 derived constructor 操作抛异常，C++ 只清理已经完成的 base subobject，
  不调用尚未完成对象的 `AdapterTexture2D` destructor，因此该 owner increment 没有 `_owner->Release`
  对应动作；
- `GetAdapterTexture` 在完整 ctor 返回后另行调用 `ret->autorelease()`；若 pool vector growth 抛出，
  caller 没有 delete/Release guard，完整 adapter allocation及其 owner ref均保持泄漏；
- same-size path没有分配，因此不触发这些异常点。

### 10.2 replacement

- incoming retain先完成，旧 release之后才 store new；
- 旧 adapter dtor先把 `_name` 清零，再调用 owner `Release`，最后才进 `Texture2D` base dtor；
- owner final Release 的 deferred vector growth可以分配；adapter dtor没有本地恢复逻辑。该 dtor按 C++
  隐式 noexcept 语义执行，异常逃逸会进入 terminate，而不是让 `Sprite::setTexture`继续完成 store；
- `updateBlendFunc` 位于 store new之后，没有回滚旧 pointer/refcount的事务。

### 10.3 pool

- autorelease没有 duplicate guard，多次 enrollment会在 clear时多次 release；
- clear在 release前已经把 live pool vector置空，release过程中新增 autorelease不会参与当前旧数组循环；
- clear不捕获 Ref deleting destructor的异常；normal old-storage delete只在 release loop完成后执行。

### 10.4 type boundary

same-size OGL路径仍用 `static_cast<AdapterTexture2D *>(orig)`，只通过 `getPixelsWide/High` 做尺寸门槛。
普通 `Texture2D` 或其它 subtype只要尺寸相同，也会被当作 adapter写共享 `_name` 字段；window identity
gate随后仍会跳过 setTexture。V284 没有添加 dynamic_cast 或 vtable检查。

## 11. 源码、IDB 与验证

源码只补充了容易被现代所有权直觉改坏的证据注释：

- `cpp/core/environ/cocos2d/MainScene.cpp`
  - `TVPDrawSceneOnce` 有意不清 autorelease pool；
  - `UpdateDrawBuffer` identity gate 同时是引用计数/owner gate；
- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  - screen-size invalidation保持 primary dimensions时，same-size different-owner路径正常可达；
  - 新 adapter 的 `Ref=1 -> autorelease enrollment -> Sprite retain -> pool release` 协议。

四个 IDB 已分别追加 UpdateDrawBuffer、GetAdapterTexture、Sprite setTexture、adapter dtor、
autorelease/add/clear、mainLoop/drawScene、TVPMainScene update、TVPDrawSceneOnce、Show 与 setScreenRect
注释和四组书签。iOS 两端新增 34 个 `_guess` 名称，覆盖窗口 body/thunk、Sprite/Texture2D/Ref、
PoolManager/AutoreleasePool、Director/mainLoop 和 direct-draw helper。

本轮还处理了一次必须如实记录的 IDB 发布故障：第一次四端 in-place save后，iOS arm64 packed IDB
无法 cold-open；当时的不可读副本已保存在 `out/idb-recovery/v284-cold-ios-arm64/`，没有把三端成功
冒充四端成功。随后从 V279 前的可读 packed backup恢复 iOS arm64，按 V279-V284 报告重新写回 106 个
semantic function name、deferred-vector三全局、35 组关键生命周期注释和 11 个书签，保存为独立
candidate并先 cold-open验证；11 个跨 V279/V280/V281/V282/V283/V284代表函数均保名且可反编译后，
才覆盖 canonical。恢复后的 iOS arm64 canonical 为 `336,826,551` bytes，SHA-256
`B882D70170B4B4576372CF86815774C58C81302FB01ECB92CDDE60A90E38A779`。

最终再从四个 canonical `.i64` 独立 cold-open，UpdateDrawBuffer、Sprite setTexture、pool clear、
TVPDrawSceneOnce 共 `16/16` 名称/符号身份正确、`16/16` 可 fresh decompile；四端 health 的 target、
imagebase、Hex-Rays 和 auto-analysis均匹配。关闭全部 worker后，把 cold-read产生的 17 个可再生 loose
work file可恢复地移到 `out/idb-recovery/v284-loose-readback-files/`；最终 IDA session数为 0，
`reference/binaries/` 严格恢复为四个原始目标和四个 packed IDB，共 8 个文件。

实测 GNU Bison 3.8.2 后，`cmake --build out/web/debug` 完成 5/5、退出码 0。固定产物
`index.html/index.js/index.wasm/vlfs.js/assets.zip` 均存在，`index.data` 不存在；只出现仓库既有的
literal-operator、compressed enum case、pthread memory-growth、实验 JSPI 与 JS-library warning。

## 12. 后续方向

本轮闭合了 OGL adapter 从 DrawDevice root 到窗口 Sprite、两个引用系统和逐帧清理点的完整链。
后续仍可继续：

1. 四端追踪 Window/PrimaryLayerArea/DrawSprite 的完整 child retain、removeFromParent与析构发布顺序，
   覆盖 Sprite 自身 autorelease debt和窗口退出/重入；
2. V286 已恢复 `RestoreNormalSize` 的旧/new GL name、scale与 metric失败边界；该函数不执行
   PixelData readback，原措辞已按四端 caller 证据纠正；
3. 追踪 target invalidation后 Window/form gate失败的真实调用者，枚举 name-owner分离后 stale GL handle
   在内置路径中的全部可达时间窗；
4. 恢复 PoolManager push/pop nested pool和 pool destructor异常边界，补齐 modal/manual draw的跨 pool归属。
