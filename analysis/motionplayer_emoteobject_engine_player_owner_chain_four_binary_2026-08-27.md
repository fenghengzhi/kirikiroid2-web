# `EmoteObject → EmoteEngine → Player` owner 链四端审计

## 结论

`MP-L05` 已闭合。四个参考二进制共同恢复出同一条非多态、分层的 native owner 链：

```text
EmoteObject                         // D3D 路径的 raw-owner aggregate
    raw owner -> ResourceManager
    raw owner -> EmoteEngine
        single/unique owner -> Player
        single/unique owner -> 7 direct controllers
        raw owner -> optional wind emitter
        owns -> controller/container families
    owns -> vector<ttstr> modulePaths
```

`EmoteObject` 构造期间还创建一个 TJS refcount shell：shell 的 adaptor 是 sticky，因而只
borrow `ResourceManager`；`Player` 内的三个 Variant CopyRef 持有这个 shell。普通析构先
销毁 Engine/Player，使三个 refcount owner 释放，再删除 `ResourceManager`，所以正常路径
上 borrowed native 的生命周期被完整覆盖。

`EmoteObject` 和 `EmoteEngine` 都没有 virtual/deleting destructor；所有分配 owner 都显式
执行 ordinary destructor + scalar delete。clone 不共享 RM、Engine、Player 或 controller，
而是按 paths 重建整棵图后迁移 Engine state。

当前本地 raw/unique/refcount/borrowed 类型、publication 和故意泄漏的构造失败边界均已
匹配；没有 C++ 语义修改。四个 IDB 已补充确定性命名、owner 注释、书签并保存。

## 1. 四端函数与对象大小

所有表内 constructor/destructor/clone 本轮均 fresh decompile；constructor、destructor 和
clone 的完整 disassembly cursor 均为 `done=true`。

| 实体 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteObject` ctor | `0x67AF8C` / 404 | `0x5604B8` / 258 | `0x1001B4984` / 222 | `0x1B4500` / 364 |
| `EmoteObject` dtor | `0x67C800` / 38 | `0x5610BE` / 15 | `0x1001B5058` / 18 | `0x1B4CCE` / 15 |
| `EmoteObject::clone` | `0x67CD58` / 44 | `0x5611FC` / 37 | `0x1001B50A4` / 26 | `0x1B4CFC` / 63 |
| `EmoteEngine` ctor | `0x67B76C` / 848 | `0x560948` / 304 | `0x1001B7FB0` / 187 | `0x1B7788` / 318 |
| `EmoteEngine` dtor | `0x67C898` / 304 | `0x5610E8` / 71 | `0x1001B8B4C` / 97 | `0x1B814E` / 99 |
| `Player` ctor | `0x6CC110` / 593 | `0x5935C4` / 281 | `0x10011EC04` / 226 | `0x11D488` / 499 |
| `Player` dtor | `0x6CCEBC` / 311 | `0x593C24` / 99 | `0x10011F2A0` / 101 | `0x11DCC4` / 175 |

对象 allocation size：

| 对象 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmoteObject` | `0x28` | `0x14` | `0x28` | `0x14` |
| `ResourceManager` | `0xE8` | `0x80` | `0xC8` | `0x70` |
| `EmoteEngine` | `0x5D8` | `0x318` | `0x428` | `0x238` |
| `Player` | `0x568` | `0x3B0` | `0x4B8` | `0x348` |

这些大小只记录目标 ABI/STL 物理布局；本地 portable C++ 不通过 padding 强制复制。

## 2. `EmoteObject` 三成员拓扑

四端共同 logical layout：

```text
+0                 ResourceManager* _rm       raw owner
+ptr               EmoteEngine* _engine       raw owner
+2*ptr             vector<ttstr> modulePaths owner
```

没有 vptr、persistent RM dispatch Variant、shared control block 或 second Engine slot。
constructor 的完整 xref 分母只有两个 source-level producer：

1. `D3DEmotePlayer::load`/replacement path；
2. `EmoteObject::clone` 的 fresh-copy construction。

iOS 对 D3D caller 保留一条 no-adjust tail thunk；不是新 producer。

## 3. `EmoteObject` 构造 publication

共同顺序为：

```text
evaluate "global.kag" -> temporary Variant

rmStorage = operator new(sizeof(ResourceManager))
ResourceManager_ctor(rmStorage, kag, 20 MiB)
this->_rm = rmStorage

rmDispatch = ResourceManager adaptor shell(native=_rm, sticky=true)

engineStorage = operator new(sizeof(EmoteEngine))
EmoteEngine_ctor(engineStorage, rmDispatch)
this->_engine = engineStorage

destroy temporary rmDispatch Variant
copy input vector<ttstr> -> this->modulePaths

for path in modulePaths:
    loaded = _rm->load(path)
metadata = loaded.metadata
base = metadata.base
Player.project = modulePaths.back()
Player.chara = base.chara
Player.play(force, base.motion)
Engine.applyMetadata(metadata)
```

`ResourceManager` 的 20 MiB 参数精确为 `0x01400000`。rmDispatch 只存在于 constructor
stack；Engine 本身不保存一份 dispatch member，而是原样交给 Player。Player constructor
建立三份独立 Variant CopyRef，分别服务 find-source、SourceCache 和 canonical
resourceManager owner。

## 4. sticky refcount shell 与 borrowed native

关系不是 `shared_ptr<ResourceManager>`：

```text
EmoteObject
    raw owner ----------------------------> ResourceManager native
                                              ^
                                              | borrowed when sticky=true
Player Variant x3 -> TJS dispatch -> ncbInstanceAdaptor<ResourceManager>
```

- TJS dispatch/adaptor 由 AddRef/Release 管理；
- adaptor sticky byte 为 true，所以 shell invalidation/destructor 不 delete native；
- `_rm` 是唯一 native deletion owner；
- Player 的三份 Variant 可独立 AddRef 同一 dispatch，不复制 native RM；
- constructor-stack Variant 释放后，Player Variants 仍让 shell 存活；
- adaptor 创建失败时 rmDispatch 可以保持 Void；参考不把它转成 `EmoteObject` 构造失败，
  后续 Player 仍按收到的 Variant 工作。

## 5. 构造失败与故意泄漏前沿

### `EmoteObject`

raw pointer member 的 publication 发生在各 pointee constructor 正常返回后：

- `ResourceManager` constructor 抛出：new-expression 只释放 pending RM storage；尚未发布；
- RM 已发布、adaptor/Engine allocation 或 Engine constructor 抛出：pending Engine storage按
  new-expression cleanup释放，但已发布 `_rm` 不删除；
- Engine 已发布后，paths copy、load、property read、play 或 metadata apply 抛出：已经完成
  的 vector/local Variant按 unwind析构，但 `_engine` 和 `_rm` 两个 raw owner都不删除；
- constructor failure 不会调用完整 `EmoteObject::~EmoteObject`。

这是四端共同的 leak boundary。用 `unique_ptr` 替换两个 raw member 会修复而改变原语义。

### `EmoteEngine`

Engine 采用单指针 owner 语义。owner construction 顺序为：

```text
Player
position controller
scale controller
color controller
angle controller
bust outer-force controller
hair outer-force controller
parts outer-force controller
```

六个 ordinary direct controller 的目标 allocation size 为
`0x80/0x48/0x60/0x38`，angle 为 `0x70/0x44/0x50/0x34`。每个 raw allocation 只有在
pointee constructor 成功后才写 owner slot。Player 或当前 controller 构造抛出时，pending
storage 由 new-expression cleanup释放，已完成的 earlier owner 按 reverse prefix销毁。

后续 Variant/hash/container 构造失败也会 unwind全部已经发布的八个 single owners。Android
old-libstdc++ 与 iOS libc++ 的 hash default-construction allocation不同，影响 landing shape，
不改变 source owner 顺序。

## 6. 普通析构顺序

### `EmoteEngine`

四端普通析构共同 phase：

1. delete live wind emitter；
2. 析构 late variable/timeline/hash/Variant members；
3. reset direct owner：parts → hair → bust → angle → color → scale → position；
4. ordinary `Player` destructor + scalar delete；
5. 析构 earlier timeline/controller deques，最终 #10 → #1。

Android 在若干 slot 上把 null store 调度到 pointee delete 后，iOS 常在 delete 前 store
null；这是 `unique_ptr::reset`/compiler scheduling 的 ABI边界。共同可移植语义是单 owner、
replacement/dtor 不共享 pointee。

### `EmoteObject`

```text
if (_engine): EmoteEngine_dtor(_engine); scalar_delete(_engine)
if (_rm):     ResourceManager_dtor(_rm); scalar_delete(_rm)
destroy modulePaths vector
```

顺序不能互换：Player 析构和 Engine 容器析构会 Release/使用 retained RM dispatch shell；
RM native 必须一直活到整个 Engine/Player teardown 完成。

`EmoteObject` raw fields不在 pointee delete 后清零，因为 aggregate本身正处于析构。若
析构中的 refcount callback非正常重入同一个 EmoteObject，Engine delete完成后的窄窗口会
看到旧 raw address；参考没有 reentrancy guard 或 defensive null store。

## 7. clone/copy 是否共享

四端 clone 共同伪代码：

```text
copy = new EmoteObject(source.modulePaths)
state = source.engine.serialize()
copy.engine.unserialize(state)
return copy
```

因此 copy 有全新的：

- `ResourceManager`；
- sticky RM adaptor shell；
- `EmoteEngine`；
- `Player`；
- 七个 direct controllers 和所有 Engine containers。

只复制 path string handles 的值并通过 state Variant迁移运行状态，不共享 native owner。
constructor 抛出由 pending new cleanup处理；完整 copy建好后 serialize抛出会泄漏 copy，
unserialize抛出只清 live state Variant而仍泄漏 copy。该 clone temporary-owner 细节在
`MP-L14/MP-R21` 继续作为 state-transfer分母使用。

## 8. 两种顶层 owner

`EmoteEngine` constructor 的完整 xref 分母每端恰好两个 source producer：

```text
D3D path:
D3DEmotePlayer shell -> EmoteObject -> EmoteEngine -> Player

script facade path:
ncbInstanceAdaptor<EmotePlayer> -> Engine-sized EmotePlayer payload -> Player
```

第二条没有 `EmoteObject`、native `ResourceManager` raw owner或 paths vector；external
rmDispatch 从脚本/调用者进入 Engine并由 Player retained。这个差异由 `MP-L06` 独立汇总，
本报告只用它证明 Engine 不是仅有 D3D 一种 owner。

## 9. 本地映射

| 参考语义 | 本地位置 | 结果 |
|---|---|---|
| EmoteObject raw RM/Engine owners + paths | `EmotePlayer.h:59-89` | 匹配 |
| RM → sticky adaptor → Engine → paths顺序 | `EmotePlayer.cpp:50-79` | 匹配 |
| load/base/chara/motion/metadata顺序 | `EmotePlayer.cpp:81-108` | 匹配 |
| Engine → RM normal delete顺序 | `EmotePlayer.cpp:115-118` | 匹配 |
| fresh graph clone + state migration | `EmotePlayer.cpp:125-130` | 匹配 |
| Engine single Player/7-controller owners | `EmoteEngine.h:790-811` | 匹配 |
| normal reverse owner reset | `EmoteEngine.cpp:866-930` | 匹配 |
| Player three RM Variant CopyRefs | `PlayerCore.cpp` constructor / `Player.h` member ledger | 匹配 |

本任务没有 semantic C++ edit。IDB 中原 `_guess` 的 EmoteObject/Engine ctor/dtor/clone 名称
已在完整四端证据支持下改为确定性名称。

## 10. Disposition

| 观察项 | disposition |
|---|---|
| LP64/ILP32 与 STL 导致对象大小不同 | ABI/STL 差异 |
| Android/iOS owner slot null-store时点不同 | compiler/library lowering；source single-owner一致 |
| iOS ctor/dtor no-adjust tail thunk | linkage thunk，不是额外 owner/虚析构 |
| EmoteObject ctor失败后 raw owner泄漏 | 共享参考语义，保留 |
| clone完成后 state-transfer异常泄漏 copy | 共享参考语义，保留 |
| D3D路径与直接 EmotePlayer facade拓扑不同 | source-level owner差异，不能合并 |

`MP-L05` task-local 静态缺口为零；controller元素内部 owner、state restore和全对象总审计
仍由 `MP-L08`、`MP-L14/MP-R21`、`MP-L16/MP-V13` 独立跟踪。
