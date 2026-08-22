# Motion.D3DAdaptor 软件纹理 miss、emplace 提交与重入边界（四参考，2026-08-15）

## 结论

四份参考二进制的 software-source bridge 都采用同一个两阶段协议：

1. 先按 borrowed source-texture 指针查询有序树；
2. miss 后取得私有 `"opengl"` renderer，按固定顺序读取 source 数据并创建静态副本；
3. 用 `map.emplace(source, copy)` 尝试提交 holder；
4. 无论 emplace 实际插入还是发现 duplicate，caller 都忽略 emplace 返回值并返回本次
   `CreateTexture2D` 产生的 raw `copy`。

第 4 点是本轮补齐的关键边界。正常单线程、无重入路径已经做过 pre-find，因此不会命中
duplicate；但是 source 虚调用或纹理工厂在 pre-find 与 emplace 之间重入同一 adaptor，并先
插入同一 source key 时，外层调用的 emplace 保留旧节点，外层却返回新建且未缓存的 copy。
下一次普通命中又会返回树里的旧 copy。实现没有比较 emplace 的 inserted flag，也没有把新
copy 替换为 existing mapped value，更没有回收 factory creation reference。

## 四端函数与调用点

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| source texture getter | `0x6EE440` | `0x5AC518` | `0x10014019C` | `0x1414C0` |
| map find helper | 内联 | 内联 | `0x1001403A8` | `0x1416FC` |
| map emplace helper | `0x6EE778` | `0x5AC700` | `0x1001403FC` | `0x141736` |
| private manager lookup | `0x6EE604` | `0x5AC628` | `0x1001402CC` | `0x14161C` |
| source scanline query | `0x6EE620` | `0x5AC638` | `0x1001402E8` | `0x141632` |
| source pitch query | `0x6EE634` | `0x5AC642` | `0x1001402FC` | `0x141640` |
| source width/height load | `0x6EE63C` | `0x5AC646` | `0x100140304` | `0x141648/0x14164C` |
| source format query | `0x6EE648` | `0x5AC64E` | `0x100140310` | `0x141654` |
| `CreateTexture2D` | `0x6EE668` | `0x5AC662` | `0x100140330` | `0x14166E` |
| emplace call | `0x6EE67C` | `0x5AC670` | `0x100140348` | `0x141682` |

四端 getter 在 emplace call 后都从保存 `CreateTexture2D` 返回值的 local/寄存器形成最终
return；没有使用 helper 返回的 node、mapped pointer 或 inserted flag。Android arm64 的
local 是 `v38`，armv7 是 `v24`，iOS arm64 是 `v22`，iOS armv7 是 `v31`。这一共同数据流
排除了“duplicate 时返回 existing cached texture”的更安全化解释。

## 固定的外部调用顺序

四端共同顺序不是一个可任意重排的参数集合，而是：

```text
manager = get private OpenGL render manager
pixels  = source.GetScanLineForRead(0)
pitch   = source.GetPitch()
width   = source.GetWidth()       // native build inline 成字段读取
height  = source.GetHeight()      // native build inline 成字段读取
format  = source.GetFormat()
copy    = manager.CreateTexture2D(
              pixels, pitch, width, height, format, STATIC)
emplace(source, copy)
return copy
```

旧本地实现把 manager getter 与五个 source expression 全写在一次函数调用中。C++ 不保证这些
实参在所有目标/compiler mode 下按文本从左到右求值，因此对于会抛出或重入的虚调用，旧写法
没有表达四端共同顺序。当前源码使用显式 locals 固定 manager-first 和 source 查询次序。

这段顺序也划分了提交边界：在 emplace 前 map 从未被本次调用修改；manager 初始化、任一
source query 或 factory 抛出时，旧 map 保持原样。factory 返回 raw copy 后，才开始 node
allocation/holder construction。

## 节点布局与 STL ABI 差异

四端的源级容器仍是：

```cpp
std::map<iTVPTexture2D *, TJS::tTJSRefHolder<iTVPTexture2D>>
```

key 是 borrowed identity pointer；mapped holder 保存 copy 指针并无条件 AddRef。节点关键
载荷布局为：

| ABI | node size | key | mapped holder pointer |
| --- | ---: | ---: | ---: |
| Android GNU 64-bit | `0x30` | `+0x20` | `+0x28` |
| Android GNU 32-bit | `0x18` | `+0x10` | `+0x14` |
| iOS libc++ 64-bit | `0x30` | `+0x20` | `+0x28` |
| iOS libc++ 32-bit | `0x18` | `+0x10` | `+0x14` |

共同源码表达式经两套 STL 展开后，duplicate candidate 生命周期不同。

### Android / GNU libstdc++

- arm64 在 `0x6EE7A0` 分配 `0x30` 节点，`0x6EE7B4` 写 key/mapped，
  `0x6EE7C0` 对 mapped copy AddRef；之后才搜索树；
- armv7 在 `0x5AC722` 分配 `0x18` 节点，`0x5AC734` 完成 mapped AddRef；之后才
  确认插入位置；
- duplicate 时 arm64 在 `0x6EE84C` Release candidate holder、`0x6EE858` 删除节点；
  armv7 在 `0x5AC754` 进入同语义的 holder-release/node-delete helper；
- duplicate helper 返回 existing node，但上层 getter 不读取它。

所以 GNU duplicate 路径会暂时给新 copy 增加一份 holder reference，再在候选节点销毁时
放掉这份 reference；factory creation reference 没有被放掉，新 copy 仍作为本次调用的未缓存
返回值存在。

### iOS / libc++

- arm64、armv7 都先搜索 key；duplicate 直接返回 existing node，不分配 candidate node，
  也不对新 copy 做 holder AddRef/Release；
- 仅真正 miss 时，arm64 在 `0x100140490` 分配 `0x30` 节点并于 `0x1001404AC`
  AddRef mapped copy；armv7 在 `0x141786` 分配 `0x18` 节点并于 `0x14179C` AddRef；
- 上层同样忽略 helper 返回值，duplicate 时仍返回 factory 刚创建的新 copy。

这不是插件源码的 Android/iOS 条件分支；它是同一个 `std::map::emplace(source, copy)` 在
GNU libstdc++ 与 libc++ 下的节点构造策略差异。

## duplicate / 重入状态机

把 pre-find 后的外部调用窗口记为 `W`，共同状态机是：

```text
pre-find(source) == end

W:
  manager/source/factory callbacks may run
  copy = newly created texture

emplace(source, copy):
  if key is still absent:
      tree[key] = holder(copy)     // copy.AddRef
      return copy
  else:
      tree[key] remains oldCopy
      discard only candidate holder/node when GNU requires one
      return copy                  // not oldCopy
```

因此同线程重入的可观察序列可以是：

```text
outer pre-find: miss
outer CreateTexture2D callback -> inner getRenderTexture(source)
inner pre-find: miss; inner creates oldCopy; inner emplace succeeds; inner returns oldCopy
outer resumes with newCopy; outer emplace sees duplicate; outer returns newCopy
later ordinary call: map hit; returns oldCopy
```

命名中的 `oldCopy/newCopy` 表示相对 map publication 的先后，并不表示纹理内容相同。实现
没有锁；跨线程并发读写 `std::map` 本身是 C++ data race，不能把上述单线程重入结果推广为
受支持的并发协议。

## 失败与所有权边界

### emplace 前

- private manager getter 抛出：map 不变，没有 copy；
- scanline、pitch、format 等 source 调用抛出：map 不变，没有 copy；
- `CreateTexture2D` 抛出：map 不变，没有 copy；
- `CreateTexture2D` 返回 null：进入 mapped-holder construction 后无条件执行 null
  `AddRef`，在 node publication 前失败，不会成功缓存 null。

### copy 已创建后

- node allocator 抛出：map 不变，但 caller 没有清理已取得的 factory creation reference；
- successful insertion：holder 再 AddRef 一次，getter 不 Release factory creation reference；
- duplicate insertion：map 保留 existing mapped value，getter仍返回 new copy；GNU 只抵消
  candidate holder reference，libc++ 根本不建立 candidate holder；两者都不释放 factory
  creation reference；
- tree compare 使用 raw pointer ordering，没有内容/尺寸/版本比较，也没有 LRU refresh；
- map hit 直接 `GetObjectNoAddRef()`，不增加返回引用。

`removeAllTextures` 和 destructor 最终只对已发布 node 的 mapped holder Release 一次。未入树的
duplicate copy、allocator-failure copy，以及成功插入时保留下来的 factory creation reference
都不由 map teardown 处理。这里的“creation reference 保留”由同一纹理工厂在 target-texture
路径上的 create-once/release-once 契约独立佐证；本地不能额外 Release 来修补原版所有权。

## 本地恢复与 IDB

`cpp/plugins/motionplayer/D3DAdaptor.cpp` 已把 software miss 的 manager/source 查询拆成显式
locals，保持原版 callback/exception 顺序；`emplace(source, copy)`、忽略 inserted result、raw
copy return 和不释放 creation reference 均保持不变。

四份 recovery IDB 已完成并保存：

- 四端 `D3DAdaptor_softwareTextureMapEmplace_guess`；
- iOS 两端独立 `D3DAdaptor_findSoftwareTextureNode_guess`；
- getter 中 manager-first 查询顺序、emplace-result-ignored/reentrant-duplicate 注释；
- GNU candidate-before-lookup 与 libc++ lookup-before-candidate 注释；
- 四端 query-order / duplicate-commit 书签。

原始 C++ 私有 helper 名未留在二进制中，所有恢复名继续保留 `_guess`。
