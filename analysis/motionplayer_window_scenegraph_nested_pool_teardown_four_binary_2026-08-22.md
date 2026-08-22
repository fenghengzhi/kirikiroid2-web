# Motionplayer window 场景树、嵌套 autorelease pool 与退出析构链（四参考二进制）

日期：2026-08-22

## 1. 本轮闭合的链

V285 从 V284 的 `TVPWindowLayer::UpdateDrawBuffer -> Sprite -> OGL adapter` 反向追到窗口创建，
再沿 scene graph 的真实 retain/release 容器追到窗口关闭，fresh 对照
`reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、iOS armv7 四个目标后确认：

```text
TVPCreateAndAddWindow
  -> new TVPWindowLayer                         // Ref = 1
  -> TVPWindowLayer::init
     -> Sprite::create                          // Ref = 1 + current-pool debt
     -> Node::create                            // Ref = 1 + current-pool debt
     -> window.addChild(PrimaryLayerArea)        // Primary 1 -> 2
     -> PrimaryLayerArea.addChild(DrawSprite)    // Sprite  1 -> 2
     -> EventListenerMouse::create / dispatcher ownership
  -> window.autorelease                         // window Ref still 1 + debt
  -> TVPMainScene::addLayer(window)              // window 1 -> 2

ordinary current-pool clear, enrollment order:
  Sprite  2 -> 1
  Primary 2 -> 1
  listener creator reference -> dispatcher-owned state
  window  2 -> 1

TVPRemoveWindowLayer / InvalidateClose
  -> window.removeFromParent()
     -> parent.removeChild(window, cleanup=true)
        -> optional onExitTransitionDidStart / onExit
        -> cleanup
        -> window.setParent(nullptr)
        -> parent.children[index].release()
        -> compact pointer array; --end

if window Ref reaches zero synchronously:
  TVPWindowLayer::~TVPWindowLayer                // unlink window list first
  -> ScrollView / Node base destruction
     -> child Primary._parent = nullptr
     -> remove listeners/actions/schedules
     -> children Vector destructor releases Primary
        -> Primary Node destructor
           -> child DrawSprite._parent = nullptr
           -> children Vector destructor releases DrawSprite
              -> Sprite destructor releases _texture
                 -> OGL AdapterTexture2D destructor
                    -> adapter._name = 0
                    -> adapter._owner->Release()
                       -> possibly Kirikiri deferred recycle queue
```

`DrawSprite` 与 `PrimaryLayerArea` 字段只是 raw observers。四端的 `TVPWindowLayer` derived destructor
都只处理窗口双向链表、current-window 选择和自身非 Cocos 成员；没有手工 release 两个字段。真实所有权
是 `Node::_children` 的两条强边，最终由两个嵌套 `Vector<Node *>` 析构逐层释放。

## 2. 四端窗口函数和布局映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TVPCreateAndAddWindow` | `0xAA1A7C` | `0x7B83F4` | `0x100E42BB8` | `0xC5048C` |
| `TVPWindowLayer::create` | inlined in previous | `0x7B8434` | `0x100E42C14` | `0xC504D0` |
| window ctor | `0xAA3B1C` | `0x7B98CC` | `0x100E44B98` | `0xC5207C` |
| window `init` | `0xAA4078` | `0x7B9B5C` | `0x100E44D74` | `0xC522E0` |
| complete window dtor body | `0xAA3E48` | `0x7B9A4C` | `0x100E46CE0` | `0xC54330` |
| `TVPMainScene::addLayer` | `0xAA01C4` | `0x7B7260` | `0x100E4195C` | `0xC4EDE8` |
| remove wrapper / equivalent adjusting thunk | `0xAA1B94` | `0x7B846C` | `0x100E46B78` | `0xC541F2` |

iOS 的原始 C++ 符号被裁剪。定位方法是：bare `14TVPWindowLayer` RTTI -> primary/secondary vtable
headers -> ctor/dtor 写入的 address point -> create 中的 virtual-init 槽 -> init 内 Sprite/Node create 与两次
addChild。iOS remove 地址与 `iWindowLayer` adjusting thunk 可被 identical-code folding 合并；它们都先按
secondary-subobject offset 回到 complete object，再调用 Node 的 `removeFromParent` 槽。

| 布局 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| complete object size | `0x1AC8` | `0x1968` | `0x1AB8` | `0x15D0` |
| `iWindowLayer` subobject | `+1040` | `+800` | `+1008` | `+804` |
| `DrawSprite` | `+1096` | `+852` | `+1064` | `+852` |
| `PrimaryLayerArea` | `+1104` | `+856` | `+1072` | `+856` |
| `_prevWindow` | `+1120` | `+868` | `+1088` | `+868` |
| `_nextWindow` | `+1128` | `+872` | `+1096` | `+872` |

这些偏移显示两个重要事实：同一指针宽度的平台之间 Cocos ABI 也不同；32-bit 的两个窗口 field
offset 恰好相同不代表整类布局相同。portable 源码应复原成员关系和调用顺序，不能编码上述偏移。

## 3. `create/init/addLayer` 的精确行为与失败边界

四端 `TVPWindowLayer::create` 都使用 throwing `operator new`，执行顺序为：

```cpp
ret = new TVPWindowLayer(w);
ret->init();              // return value ignored
ret->autorelease();
return ret;
```

Android arm64 把这一小函数内联进 `TVPCreateAndAddWindow`，另外三端保留独立 body。与普通
Cocos `Node::create` / `Sprite::create` 不同，window create：

- 不使用 `std::nothrow`；
- 不检查 allocation null；
- 不检查 `init()` 返回值；
- 即使 inherited `ScrollView::init()` 返回 false，window `init` 仍继续创建 Sprite、Node、listener，
  最后返回 inherited result，而 create 仍 autorelease 并返回对象；
- 对 `Sprite::create()` / `Node::create()` 的 null 结果也没有检查，后续立刻解引用或 addChild。

普通 Node/Sprite create 的四端控制流则是 `nothrow new -> ctor -> virtual init`；init false 时调用
virtual deleting destructor并返回 null，成功时才 autorelease。

window 的 autorelease enrollment 在整个 init 之后发生。因此在没有 init 内部 pool 切换的正常路径，
同一个 current pool 中的顺序至少是 `DrawSprite -> PrimaryLayerArea -> mouse listener -> window`。
`TVPMainScene::addLayer` 紧接在 create 返回后执行，对 window 建立 scene-graph retain，随后才设置 view
size、content size/position 和可见状态。

## 4. `Node::_children` 是什么容器

四端都是一个三指针 contiguous vector：`begin / end / capacityEnd`，元素是 raw `Node *`，但外层
`cocos2d::Vector<Node *>` 对元素执行 Cocos retain/release。

| Node field | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| children begin/end/cap | `+424/+432/+440` | `+412/+416/+420` | `+384/+392/+400` | `+356/+360/+364` |
| child `_parent` | `+448` | `+424` | `+408` | `+368` |
| parent `_running` | `+544` | `+472` | `+512` | `+420` |

第一次向空且 capacity 小于 4 的 vector 添加 child 时 reserve 4。append helper 的共同语义是：

```text
if end == capacityEnd:
    grow/reallocate pointer storage
*end++ = child
child->retain()
```

四端 retain 发生在 child metadata、parent pointer、arrival-order 与 onEnter 调用之前。对应函数：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Node::create` | `0x1215E98` | `0xBEF240` | `0x10108907C` | `0xEC6EE8` |
| `Sprite::create` | `0x123C9C4` | `0xC01760` | `0x10117F120` | `0xFF067C` |
| `Node::addChildHelper` | `0x1218278` | `0xBF0390` | `0x10108A8AC` | `0xEC8688` |
| retaining Vector append | `0x110CE48` | `0xB0DCAC` | `0x10108AD74` | `0xEA0DCC` |

append 后的顺序为：设置 local z-order/tag 或 name、`child->setParent(this)`、写入全局递增的
order-of-arrival；若 parent 正在运行，再调用 child 的 onEnter，必要时调用 onEnterTransitionDidFinish；
最后传播 reorder/cascade dirty state。全局 arrival counter 无溢出防护。

## 5. scene-graph remove 的精确顺序

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `removeFromParent()` | `0x121855C` | `0xBF04BA` | `0x10108AA84` | `0xEC87B6` |
| `removeChild(child,cleanup)` | `0x1218590` | `0xBF04D8` | `0x10108AABC` | `0xEC87D4` |
| detach helper | inlined | helper reached through `0x2FBF2C` | `0x10108AB18` | `0xEC8824` |
| Vector erase/release | inlined | helper | `0x10108ACF4` | `0xEC893C` |
| `removeAllChildrenWithCleanup` | `0x12188D0` | `0xBF05C6` | `0x10108AC58` | `0xEC88D4` |

`removeFromParent()` 固定把 cleanup=true 传给 parent。parent vector 为空或找不到 child 时，
`removeChild` 静默不做任何事。找到后严格按以下顺序：

```text
if parent._running:
    child->onExitTransitionDidStart()
    child->onExit()
if cleanup:
    child->cleanup()
child->setParent(nullptr)
children[index]->release()      // 可能在这里同步 deleting-destructor
memmove(index, index + 1, tail)
--children.end
```

先清 `_parent`、再 release、最后 compact 是四端共同边界。若 release 把 child 降到零，child 的完整
析构发生在 parent vector 尚未 compact 时；但 child 已不能从 `_parent` 回头修改该 vector。找不到的
child 不会被 release。`removeAllChildrenWithCleanup` 则先逐个 onExit/cleanup/setParent(null)，再按
vector 顺序逐个 release，最后把 end 设回 begin；storage/capacity 保留到 vector 自身析构。

Android `TVPRemoveWindowLayer(nullptr)` 的代码还直接证明没有有效 null guard：null 不做 subobject
adjustment，却仍会解引用 null complete-object vptr。iOS folding thunk同样没有 null 检查。该 API 的
null 输入是崩溃/UB，不应为 portable 版本补成静默 no-op。

## 6. 三层 scene graph 的引用计数台账

初始 Ref ctor count 都为 1；autorelease 只登记 raw pointer，不改变 count；每条 child edge retain 1。

| 时点 | window | Primary | Sprite | 说明 |
|---|---:|---:|---:|---|
| 各自 create/ctor 后 | 1 | 1 | 1 | creator reference |
| 各自 autorelease 后 | 1 + debt | 1 + debt | 1 + debt | count 数值不变 |
| 两次内部 addChild 后 | 1 + debt | 2 + debt | 2 + debt | window->Primary；Primary->Sprite |
| main scene addLayer 后 | 2 + debt | 2 + debt | 2 + debt | scene->window |
| ordinary pool clear 后 | 1 | 1 | 1 | 三条 scene-graph edge 是唯一普通强引用 |

所以正常长期状态中，移除 window 的 parent edge 是 `window 1 -> 0`，窗口析构同步发生在
`Vector::erase` 内。若 create 和 remove 发生在同一个尚未 clear 的 pool 周期，状态是：

```text
remove window edge: window 2 -> 1; window._parent = null, object remains alive
later pool clear (enrollment order):
  Sprite  2 -> 1
  Primary 2 -> 1
  listener creator debt released
  window  1 -> 0
    window dtor releases Primary 1 -> 0
      Primary dtor releases Sprite 1 -> 0
```

因此 `removeFromParent()` 不是无条件的“并删除 this”。外部 retain、重复 autorelease enrollment 或尚未
清除的单次 debt 都能改变析构时点；但 parent pointer 与 scene traversal membership 已在 remove 时消失。

## 7. derived dtor、Node dtor 与 child 析构顺序

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Node` complete dtor body | `0x1215F24` | `0xBEF2A0` | `0x10108910C` | `0xEC6FD4` |
| children Vector release/clear helper | in dtor / `0x1215E28` | Cocos Vector dtor | `0x100E49394` | `0xC5695C` |

window derived dtor先修复 `_lastWindowLayer`、`_prevWindow/_nextWindow` 和 `_currentWindowLayer`，再进入
ScrollView/Node base dtor。Node dtor 对 children 的关键顺序是：

1. 遍历当前 children，将每个 `child->_parent` 直接置 null；
2. 移除 script handler、组件、actions、scheduler entries 和 scene-graph event listeners；
3. 析构 callback/function 成员；
4. children Vector 按 begin->end 调用每个 child 的 `release()`；
5. 释放 vector pointer storage，继续销毁 transform/Ref base。

Node dtor 的 vector path不会再次调用 child onExit 或 cleanup；正常 window remove 已经在外层
`removeChild(window,true)` 的递归 onExit/cleanup 阶段完成这些动作。直接 deleting 一个仍 running 且仍含
children 的异常路径不能假设 Node dtor替调用完整 removeAll protocol。

EventListenerMouse 的四个 `std::function` 保存 raw `TVPWindowLayer *`。listener 的 scene-graph ownership
并不使 window 产生 C++ shared ownership；Node dtor在释放 Primary/Sprite 之前先让 EventDispatcher 移除
target listeners，避免后续正常事件派发再进入已析构窗口。

## 8. PoolManager 与嵌套 AutoreleasePool 的真实容器

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `PoolManager::getInstance` | `0x1274790` | `0xC1D77C` | `0x100FC8F94` | `0xDE2474` |
| manager `push` | `0x1274968` | `0xC1D848` | `0x100FC9094` | `0xDE25E0` |
| named pool ctor | `0x1274A78` | `0xC1D8A4` | `0x100FC90DC` | `0xDE2608` |
| pool dtor | `0x1274BD8` | `0xC1D918` | `0x100FC915C` | `0xDE26F8` |
| pool `clear` | `0x1274D50` | `0xC1D980` | `0x100FC91BC` | `0xDE27C0` |
| manager `pop` | `0x1274DCC` | `0xC1D9C8` | inlined `end -= 8` | inlined `end -= 4` |
| manager dtor | `0x127504C` | `0xC1DB40` | `0x100FC93BC` | `0xDE2A84` |

PoolManager 自身也是 `std::vector<AutoreleasePool *>` 三指针布局，元素是 raw pointer：push 不 retain、
不拥有 Cocos Ref。manager ctor reserve 10；首次 `getInstance` 还 heap-new 一个名为
`"cocos2d autorelease pool"` 的 root pool。每个 pool 的 object vector reserve 150。

```text
AutoreleasePool(name) ctor:
  construct empty vector<Ref*>
  construct name
  reserve(150)
  PoolManager::getInstance()->push(this)

~AutoreleasePool:
  clear()
  PoolManager::getInstance()->pop()
  destroy name
  destroy vector<Ref*> storage
```

`getCurrentPool()` 直接读 `*(manager.end - 1)`，没有 empty guard。pop 只做 `end -= one pointer`，不验证
top pointer是否等于正在析构的 pool，也不负责 delete。Android debug 目标在空栈时记录 assert 后仍继续
下减；iOS release 目标直接下减。这意味着 pool 必须严格 LIFO、必须在原线程/manager 上析构：提前析构
非 top pool 会清错对象的债务并弹掉真正 top；空栈 current/pop 都是越界。

PoolManager dtor反复读取 top，`delete top`；该 pool dtor自行 pop，所以 manager 再读新的 end，直到
begin==end，最后释放 pointer storage。top 为 null 会导致循环不前进；正常构造路径从不 push null。

## 9. `clear()` 重入与 pool dtor 重入不是同一件事

四端 clear 先 snapshot old begin/end，再把 live begin/end/cap 全部置 null，然后按登记顺序 release old
entries，最后释放 old storage。于是普通仍存活的 root pool：

```text
root.clear():
  detach old vector
  release old entries
    reentrant Ref::autorelease(x)
      -> current pool is still root
      -> x appended to root's fresh vector
  free old storage
next main-loop clear releases x
```

但 pool dtor 在 clear 完成前仍没有 pop：

```text
dyingPool.~AutoreleasePool():
  dyingPool.clear()
    reentrant autorelease(x) -> appended to dyingPool fresh vector
  manager.pop()
  vector<Ref*> destructor -> frees raw storage only; DOES NOT release x
```

因此 reentrant enrollment 在普通 clear 中“留到下一轮”，在 pool 析构中却被静默丢弃，不会迁移到
外层 pool。若 x 的 count 只等这一次 autorelease 来平衡，表现为泄漏。root pool 在进程关闭时由
PoolManager dtor删除，也服从同一边界。

另一个更尖锐的边界是 clear 中若重入代码构造了新 pool，new pool 会压到 dying pool 之上；原 pool
dtor随后 identity-blind pop 会弹掉新 top而不是自己，使 manager stack留下 dangling dying-pool pointer。
参考实现依赖“pool 析构期间不改变 pool stack”的外部不变量，而不是内部防护。

## 10. 从 window close 到 OGL owner recycle

V284 已确认 Sprite complete dtor 释放 `_texture`，AdapterTexture2D complete dtor先把 GL name 置 0，
再调用 `_owner->Release()`。把本轮 scene graph链接上后，完整 normal close path 为：

```text
scene children releases window
  -> window Node vector releases Primary
     -> Primary Node vector releases DrawSprite
        -> Sprite dtor releases adapter
           -> adapter dtor releases BackTarget owner
              -> owner count == 0 ? enqueue deferred vector : remain root-owned
```

owner 是否在此刻入 Kirikiri deferred queue 取决于 DrawDevice root reference：

- root 已经因 target invalidation/teardown Release 时，adapter 是最后一个 owner，Sprite 析构会入队；
- root 仍持有 Front/Back target 时，adapter release只去掉反向引用，owner继续由 DrawDevice持有；以后
  root Release才入队；
- adapter 自身仍有 autorelease debt时，Sprite release只从 2 到 1，adapter/owner release推迟到所属
  Cocos pool clear；如果该 debt属于正在析构且发生重入 enrollment 的 pool，还受第 9 节 debt-lost 边界。

owner 入 deferred queue 与真正 GL delete仍隔着 `iTVPTexture2D::RecycleProcess` snapshot。若入队发生在
`TVPMainScene::update` 内 recycle 之前，可同一 scheduler tick删除；若发生在 drawScene tail 的 Cocos
pool clear，通常要等后续 tick。window close、Cocos pool clear 和 Kirikiri recycle 是三个不同时间点。

## 11. 对 portable 源码的直接结论

本轮没有加入额外 retain/release、手工 delete、null guard、owner rebinding 或局部 pool clear。当前
`MainScene.cpp` 的结构行为与四端一致；只把以下容易误导后续复原的事实写成证据注释：

- `DrawSprite` / `PrimaryLayerArea` 是 raw observers，ownership 在 children vectors；
- derived window dtor不手工释放它们；
- init 的两条 addChild edge先于 pool clear建立；
- window create忽略 init result，且 window enrollment晚于 init 内对象；
- `removeFromParent()` 释放 parent edge，不保证当场 delete。

V284 报告也已补上“普通 clear重入存活、pool dtor重入 debt丢失”的适用范围修正。

## 12. IDB 落盘与构建验证

本轮把 iOS 两库中由 RTTI/vtable、ctor vptr write 和四端交叉映射恢复出的函数名写回 IDB：每个 iOS
目标 22 个，共 44 个；四库也写入了 window ownership、children retain/release ordering、pool clear/pop
边界等证据注释。先保存到候选目录并冷开验证，之后备份旧 canonical IDB、把候选发布到
`reference/binaries/`，再从最终 canonical 路径冷开。最终四库各抽查 window init、Node addChild helper、
AutoreleasePool clear 和 window complete dtor，共 16 个函数，Hex-Rays 反编译为 16/16 成功；iOS 恢复名
和 Android 原符号均在重开后存在。最终会话用 `save=false` 关闭，验证过程没有再次改写交付库。

| canonical IDB | bytes | SHA-256 |
|---|---:|---|
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so.i64` | 368,555,646 | `51AA0B2051ADA9E74940E6A15024BFABD76B1A5632993CF06BE9E5634B77B108` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so.i64` | 347,599,244 | `8B86867C6BE67C07DC74A6C64F9CDF2D3222ADB0B43BD8E6F0DD29DF8A7537F9` |
| `Kirikiroid2_1.3.9_iOS_arm64.i64` | 337,114,029 | `038BF4B221E832708FC20A45F854B232E48D9BC2BE6476A63FD096991FB9D71F` |
| `Kirikiroid2_1.3.9_iOS_armv7.i64` | 378,779,223 | `C653E160ED249FD35FAF3F44CDA030E42ED6CC073070772B4F7A5CED0157EA08` |

发布前的四个 canonical IDB 保存在 `out/idb-recovery/v285-prepublish/`；候选保存在
`out/idb-recovery/v285-scenegraph-pools/`。两次冷读生成的 `.id0/.id1/.id2/.nam/.til` 没有删除，分别
移动到 `out/idb-recovery/v285-loose-readback-files/`。整理后 `reference/binaries/` 严格只剩四个原始
参考二进制和四个 canonical `.i64`。

`cmake --build out/web/debug` 增量构建成功，实际使用 GNU Bison 3.8.2。固定产物 `index.html`、
`index.js`、`index.wasm`、`vlfs.js`、`assets.zip` 全部存在，`index.data` 不存在。构建仅出现项目已有的
literal-operator、pthread memory growth、JSPI experimental 和 JS library symbol warning，没有 error。
