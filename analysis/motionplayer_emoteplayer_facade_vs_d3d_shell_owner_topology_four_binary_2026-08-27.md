# `Motion.EmotePlayer` facade 与 `D3DEmotePlayer` shell owner 拓扑对照

## 结论

`MP-L06` 已闭合。四个参考二进制中两者是两个独立 script class、两个独立 ClassInfo/
factory/native-adaptor identity，也是两套不同的 native owner 图；名字相似不构成继承或
共享 payload：

```text
Motion.EmotePlayer TJS object
    owns -> ncbInstanceAdaptor<EmotePlayer>
        owns -> one Engine-sized direct facade payload
            owns -> Player + controller/container state
            retains/borrows -> caller-supplied ResourceManager dispatch/native

D3DEmotePlayer TJS object
    owns -> ncbInstanceAdaptor<D3DEmotePlayer>
        owns -> D3DLayerListener shell
            borrows/subscribes -> D3DLayer owner
            raw owns -> primary EmoteObject (lazy)
            raw owns -> secondary EmoteObject (lazy)
                each owns -> ResourceManager -> EmoteEngine -> Player graph
```

direct facade 没有 `EmoteObject`、内部创建的 native RM、modulePaths、D3D listener、primary/
secondary slots 或 D3D vtable。D3D shell factory 又不会预建 Engine graph：它只注册
listener并清空两个 raw slots；`load` 后才出现 `EmoteObject`。

本地两个类、两个 registrar 和两个 factory 已保持分离；没有 C++ 语义修改。四个 IDB
已添加 topology 注释/书签并保存。

## 1. 独立注册身份

| identity | 模块 | class/factory 形状 |
|---|---|---|
| `Motion.EmotePlayer` | `motionplayer.dll`，由 `emoteplayer.dll` root动态确保 | delayed NCB subclass，one-Variant typed Factory |
| `D3DEmotePlayer` | `DrawDeviceD3D.dll` | 独立 concrete class，one-`D3DLayer` typed factory |

`Motion.EmotePlayer` 的 73-row surface 与 `D3DEmotePlayer` 的 4 constants + 54 members由
不同 registrar发布。四端注册字符串、ClassInfo tuple、factory pointer、vtable/native
identity均不同；不存在 parent class ID、cast thunk、native offset或同一 class object alias。

## 2. factory 与 allocation

| 平台 | direct EmotePlayer factory | payload bytes | D3D factory | shell bytes |
|---|---:|---:|---:|---:|
| Android arm64 | `0x689D7C`（native build `0x689E94`） | `0x5D8` | `0x542B44` | `0x38` |
| Android armv7 | `0x56A310` | `0x318` | `0x4A4080` | `0x24` |
| iOS arm64 | `0x1001C5FBC` | `0x428` | `0x100245DC0` | `0x38` |
| iOS armv7 | `0x1C31C8`（native build `0x1C3310`） | `0x238` | `0x2465B8` | `0x24` |

本轮 factory完整 disassembly为 direct 68/62/51/87 条、D3D 89/78/67/115 条，所有
cursor为 `done=true`；四端 fresh decompile确认 allocation size和下层 constructor edge。

### direct facade

```text
require one Variant argument (one exact Void is empty-adaptor sentinel)
copy only arg0; ignore surplus
allocate exactly sizeof(EmoteEngine)
EmoteEngine_ctor(payload, rmDispatch)
attach payload to ncbInstanceAdaptor<EmotePlayer>
```

这个 direct payload 无额外数据字段和自己的 vptr；本地以无新增数据的
`class EmotePlayer : public EmoteEngine` 复用 typed member绑定。二进制能证明“同一
Engine layout与行为”，但不能单凭最终 layout证明原始源码究竟写了 public inheritance还是
composition/alias；重要的 owner事实是单个 Engine-sized allocation。

### D3D shell

```text
require one D3DLayer object
allocate sizeof(D3DEmotePlayer)
construct D3DLayerListener base and register with layer
primary = null
secondary = null
baseScale = 1.0f
userScale = 1.0f
visible/smoothing = false
attach shell to its own native adaptor
```

它不调用 `EmoteEngine_ctor`，factory后没有 `Player` 或 controller owner。

## 3. object layout 对照

### direct facade

direct facade 的完整 allocation就是 EmoteEngine source members：十个 controller deque、
timeline/selector/hash/vector/Variant state、Player single owner、七个 direct controllers、
wind emitter/cache/scalars。没有 outer shell字段。

### D3D shell

共同 logical layout：

| 字段 | LP64 | ILP32 | owner kind |
|---|---:|---:|---|
| vptr | `+0x00` | `+0x00` | D3DLayerListener polymorphic identity |
| D3DLayer owner/listener link | `+0x08/+0x10` family | `+0x04/+0x08` family | borrowed/subscription |
| primary `EmoteObject*` | `+0x18` | `+0x10` | raw owner |
| secondary `EmoteObject*` | `+0x20` | `+0x14` | raw owner |
| base/user scale | `+0x28/+0x2C` | `+0x18/+0x1C` | value |
| visible/smoothing | `+0x30/+0x31` | `+0x20/+0x21` | compatibility bytes |

因此 D3D shell 的 vtable只管理 listener/shell virtual surface；Engine和Player仍是它所拥有
的 `EmoteObject` 内部非多态 payload。

## 4. ResourceManager owner差异

```text
direct facade:
caller/external owner -> ResourceManager native
    ^ borrowed through caller-supplied dispatch/adaptor
Player Variant x3 -> AddRef same dispatch

D3D shell after load:
D3DEmotePlayer -> EmoteObject -> raw-own fresh ResourceManager native
Player Variant x3 -> AddRef sticky adaptor shell -> borrow same fresh RM
```

direct facade factory绝不执行 `global.kag`、`new ResourceManager` 或 paths copy。D3D的每个
EmoteObject则独立从 `global.kag` 创建 RM并复制自己的 paths。因此两个 facade 即使加载
相同资源名也没有 native RM/cache identity共享保证。

## 5. publication 与失败清理

### direct facade

- Engine/Player/controller constructor prefix使用普通 new-expression cleanup；
- 完整 payload只有在 Engine ctor正常返回后才交给 adaptor；
- receiver lookup/attach失败时 factory cleanup直接执行 Engine ordinary dtor + scalar delete；
- attach成功后 non-sticky adaptor成为唯一 payload owner；
- adaptor invalidation期间 native pointer在 pointee destructor+delete完成后才清空。

### D3D shell

- `D3DLayerListener` base在derived字段之前完成注册；
- factory attach失败时调用 shell deleting destructor，先清两个 raw owner（初始为空），再
  注销 listener，最后scalar-delete shell；
- later `load` 构造完整 EmoteObject后才发布 primary/secondary slot；其内部 raw-owner
  constructor failure边界见 MP-L05；
- clone先构造/注册新listener shell，再只clone primary；secondary不复制；inner clone抛出时
  新shell泄漏，这是参考边界。

## 6. 普通析构与 reentry窗口

### direct facade

TJS object invalidation：

```text
EmoteEngine ordinary destructor
    wind/later containers
    7 direct controllers reverse
    Player ordinary destructor + scalar delete
    earlier deques
scalar delete Engine-sized payload
clear adaptor native/sticky
```

### D3D shell

```text
delete secondary EmoteObject; secondary = null
delete primary EmoteObject;   primary = null
D3DLayerListener base destructor -> unregister from layer
scalar delete shell only in deleting-destructor entry
```

两个 EmoteObject delete发生时 listener仍注册；base unregister在derived complete destructor
之后。reference没有先摘listener再清engine的防御顺序。若 native/TJS release callback在此
期间异常重入，必须按 live slot/listener时点处理，不能把 teardown压成原子事务。

## 7. clone/copy与脚本功能差异

- direct `Motion.EmotePlayer` surface没有 native `clone` member；脚本实例由其 adaptor直接
  拥有同一个 Engine-sized payload；
- `D3DEmotePlayer.clone` 创建新listener shell，`EmoteObject.clone`又创建 fresh
  RM/Engine/Player/controller graph，随后 serialize/unserialize state；
- D3D clone只复制 primary，不复制 secondary；
- 两类的 `progress/draw/play/...` 看似相似，是 facade调用同类深层 Engine/Player逻辑，
  不是共享 outer native object；
- D3D surface的七个 TODO leaves、`IsVisible/Draw` listener slots和baseScale/userScale只属于
  D3D shell，不应添加到 direct facade。

## 8. owner图

```text
Motion.EmotePlayer script instance
└── refcount owns ncbInstanceAdaptor<EmotePlayer>
    └── non-sticky owns direct Engine-sized payload
        ├── single owns Player
        ├── single owns 7 direct controllers
        ├── raw owns optional wind emitter
        └── owns Engine containers / Variants
            └── Player Variant owners retain external RM dispatch

D3DEmotePlayer script instance
└── refcount owns ncbInstanceAdaptor<D3DEmotePlayer>
    └── owns D3DLayerListener shell
        ├── borrows/subscribes D3DLayer
        ├── raw owns primary EmoteObject (optional)
        │   ├── raw owns ResourceManager
        │   ├── raw owns EmoteEngine -> Player/controllers
        │   └── owns vector<ttstr> paths
        └── raw owns secondary EmoteObject (optional, same topology)
```

## 9. 本地映射与 disposition

| 要求 | 本地位置 | 结果 |
|---|---|---|
| direct Engine-sized facade | `EmotePlayer.h:102`; `EmotePlayer.cpp:487` | 匹配 |
| 独立 delayed registrar/Factory | `main.cpp:405` | 匹配 |
| D3DLayerListener shell + two raw owners | `EmotePlayer.h:304`; `DrawDeviceD3D.cpp:1753` | 匹配 |
| D3D独立 54-member surface | `DrawDeviceD3D.cpp:786` | 匹配 |
| primary-only clone / reverse raw teardown | `EmotePlayer.cpp:112-130`; D3D shell methods | 匹配 |

| 可能误判 | disposition |
|---|---|
| 名字都含 EmotePlayer，所以是同一 class | 否：independent ClassInfo/factory/adaptor/layout |
| direct facade继承 D3D shell | 否：无listener/vptr/raw slots，allocation等于 Engine |
| D3D shell直接内嵌 Engine | 否：lazy raw `EmoteObject*` 中间层 |
| 两者共享 ResourceManager/cache | 否：direct外借；D3D每个 EmoteObject自建 |
| D3D clone共享 Engine | 否：fresh完整图 + state迁移 |

`MP-L06` task-local 静态缺口为零；完整 controller element 与 state-transfer异常总审计继续
由 `MP-L08/MP-L14/MP-R21` 跟踪。
