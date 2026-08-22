# motionplayer D3DImage holder、ManagedObjects 与生命周期四参考恢复（2026-08-15）

## 1. 范围与结论

本专题重新以 `reference/binaries/` 中四个当前参考为唯一 oracle，专门闭合
`D3DImage` 的对象布局、虚表、工厂、`ManagedObjects` 容器、纹理 holder、`load`
数据流，以及 `D3DPicture` 对 image 的持有关系。它不沿用旧 `libkrkr2.so` 注释。

四端共同证明：

- `D3DImage` 是带虚表的 3-pointer/3-word native 对象，不是无虚表 POD；
- 第一个数据字段是 borrowed root，第二个数据字段是堆分配的
  `TJS::tTJSRefHolder<iTVPTexture2D> *`；
- 虚表除 complete/deleting destructor 外还有第三槽，用于删除并清空当前 holder；
- constructor 把 `this` 插入 root 的 `std::set<D3DImage *>`，destructor 先释放当前
  holder，再从 set 删除自己；
- root 只拥有 set node，不拥有其中的 `D3DImage`，也不会在 root 析构时 detach 它们；
- `D3DPicture` 只借用 `D3DImage *`，不 AddRef、不复制 holder，也不参与 image 析构；
- 每次 `load` 都直接覆盖 holder pointer。旧 holder 及其 texture 引用会泄漏；
- software 分支创建新 texture 后也不释放创建时的初始引用。holder 再 AddRef 一次，
  `D3DImage` 析构只归还 holder 的这一份引用；
- factory 的零参数错误是 `BADPARAMCOUNT`，非 object 或错误 native root 是
  `INVALIDTYPE`。旧移植把非 object 也归为 `BADPARAMCOUNT`，已纠正。

## 2. NCB surface 与回调地址

公开成员顺序仍是：

```text
Factory
width RO
height RO
load
```

| 目标 | registrar | factory | width | height | load |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x52D768` | `0x52D98C` | `0x52DB48` | `0x52DB64` | `0x52DB80` |
| Android armv7 | `0x493950` | `0x4939F8` | `0x493ABC` | `0x493ACA` | `0x493AD8` |
| iOS arm64 | `0x100231AFC` | `0x100231BE8` | `0x100231CD8` | `0x100231CF4` | `0x100231D10` |
| iOS armv7 | `0x230932` | `0x2309DC` | `0x230AF4` | `0x230B02` | `0x230B10` |

factory/constructor 的 set-insertion 异常清理并非四端完全相同：Android arm64
`0x52DB34`、iOS arm64 `0x100231CC4`、iOS armv7 `0x230AC8` 都有 raw-object
allocation cleanup landing path；Android armv7 `0x4939F8` 则没有对应 landing path。
因此在后者中，image storage 已分配、随后 set node allocation 抛出时，`0x0C` raw
storage 会泄漏；其余三份会调用 `operator delete`。set insertion 本身未 commit node，
所以 cleanup 不需要执行 image destructor/set erase。

### 2.1 Factory 边界

四端等价流程：

```text
if numparams < 1:
    return TJS_E_BADPARAMCOUNT       // -1004

if arg0 is not Object:
    return/raise TJS_E_INVALIDTYPE   // -1003 through ncbind conversion path

query D3DLayerBase native instance from arg0
if dispatch/native instance/root is null:
    return TJS_E_INVALIDTYPE

allocate D3DImage
initialize vptr, Owner, Picture=null
insert this into Owner.ManagedObjects
return native pointer and TJS_S_OK
```

allocation size 在 64-bit 为 `0x18`，32-bit 为 `0x0C`。Android arm64、Android
armv7、iOS arm64 将小 constructor 内联进 factory；iOS armv7 保留 constructor
`0x234948`。

constructor 没有 defensive `Owner == null` 分支；factory 已在分配前证明 root 非空。
destructor 则保留 Owner-null guard，但 root 从不主动把该字段清空。

## 3. 精确对象布局与虚表

### 3.1 对象布局

| 字段 | 64-bit | 32-bit | 语义 |
|---|---:|---:|---|
| vptr | `+0` | `+0` | D3DImage virtual table address point |
| Owner | `+8` | `+4` | borrowed `DrawDeviceObjectBase *` |
| Picture | `+16` | `+8` | owning pointer to heap `tTJSRefHolder<iTVPTexture2D>` |
| sizeof | `0x18` | `0x0C` | 无额外尾字段 |

holder 自身只含一个 texture pointer，因此 64-bit allocation 为 `8`，32-bit 为 `4`。

### 3.2 虚表三槽

| 目标 | vtable address point | complete dtor | deleting dtor | clear-holder virtual |
|---|---:|---:|---:|---:|
| Android arm64 | `0x19FAF58` | `0x5336F4` | `0x533800` | `0x533824` |
| Android armv7 | `0x10AB1C8` | `0x496F60` | `0x496FB8` | `0x496FC8` |
| iOS arm64 | `0x101AEEC48` | `0x100235B7C` -> `0x100235CFC` | `0x100235B80` | `0x100235B94` |
| iOS armv7 | `0x1839260` | `0x234980` -> `0x234AD8` | `0x234984` | `0x234994` |

第三槽不是公开 NCB member。它执行：

```cpp
delete Picture;       // holder destructor calls Texture->Release()
Picture = nullptr;
```

Android complete destructor 将这段逻辑直接调用/内联；iOS complete destructor 调用保留的
第三槽 implementation。恢复源码用 `ClearTextureHolder_guess` 表示尚未取得原符号名的第三
虚方法，`_guess` 明确保留命名不确定性。

## 4. holder 的精确类型与引用计数

holder 与仓库核心已有的 `TJS::tTJSRefHolder<T>` 完全同构且行为一致：

```cpp
TJS::tTJSRefHolder<iTVPTexture2D> *Picture;
```

constructor 的机器码是：

```text
allocate pointer_size bytes
holder.Object = texture
++texture.RefCount
this.Picture = holder
```

texture 字段布局也在四端交叉一致：

| 字段 | 64-bit | 32-bit |
|---|---:|---:|
| vptr | `+0` | `+0` |
| RefCount | `+8` | `+4` |
| Width | `+12` | `+8` |
| Height | `+16` | `+12` |

holder destructor 不直接 decrement 字段，而是调用 texture 虚表中的 `Release`：64-bit
`vptr+16`，32-bit `vptr+8`。这正好对应 `iTVPTexture2D` 的两个 destructor slot 之后的
`Release()`。

`width`/`height` 只检查 `Picture` 是否为空；若 holder 存在便直接解引用其 texture。
参考没有第二层 texture-null guard。`GetTexture` 同样只把 holder-null 映射为 null。

## 5. load 的完整数据流

四端等价：

```text
sourceLayer Variant
  -> tTJSNI_Layer::FromVariant
  -> layer.GetMainImage()            // 恰好一次；内部会 ApplyFont
  -> main image texture
  -> global render manager IsSoftware?
       no  -> 使用 source texture
       yes -> source.GetScanLineForRead(0)
              source.GetPitch()
              source.Width / Height
              private "opengl" manager CreateTexture2D(
                  pixels, pitch, width, height, RGBA, 0)
  -> new tTJSRefHolder(texture)       // direct AddRef
  -> Picture = newHolder             // 直接覆盖
  -> return                          // 无 Release
```

参考没有以下保护或补偿：

- 没有 layer/main-image/source-texture null guard；`FromVariant`/调用方必须满足前置条件；
- `CreateTexture2D` 返回 null 时没有 guard，holder constructor 会访问 null；
- 覆盖前不 `delete Picture`；
- software-copy 成功后不调用 `loaded->Release()`；
- software-copy 成功、随后 holder allocation 抛异常时也没有补偿 release。

### 5.1 可观察的引用计数结果

设原 source 的现有引用计数为 `N`：

| 路径 | load 后 | 当前 holder 正常析构后 |
|---|---:|---:|
| 非 software/source texture | `N + 1` | `N` |
| software/new texture（创建初始引用为 1） | `2` | `1` |

因此 software copy 的创建引用在本路径中不会被归还。若再次 `load`，旧 holder 指针也被
遗失，旧 holder 所持的额外引用同样不会归还。这里是原实现的边界行为，不应被 RAII
replacement 或临时引用清理“修正”。

## 6. ManagedObjects 的容器与析构协议

源级容器是：

```cpp
std::set<D3DImage *> ManagedObjects;
```

四平台因 STL ABI 不同，root 内的 field offset 不同：

| 目标 | ManagedObjects field | insertion | erase/range helper |
|---|---:|---:|---:|
| Android arm64 | `+0xA8`（内部 header `+0xB0`） | factory 内联 | `0x533868` |
| Android armv7 | `+0x5C` | `0x496FEC` | `0x4970F4` |
| iOS arm64 | `+0x78` | `0x100235BD8` | `0x100235D54` |
| iOS armv7 | `+0x44` | `0x234A38` | `0x234B88` |

排序 key 就是 `D3DImage *` 地址，node 的 mapped/key payload 只有该指针。constructor 使用
unique insertion；对正常 `new D3DImage` 而言地址天然未存在。destructor 以 `this` 做
equal-range 查找并删除对应 node；set 语义决定最多一个 node。

complete destructor 顺序严格为：

```text
restore D3DImage vptr
clear/delete current holder
Picture = null
if Owner != null:
    Owner.ManagedObjects.erase(this)
```

root destructor：

| 目标 | 地址 |
|---|---:|
| Android arm64 | `0x53244C` |
| Android armv7 | `0x49606C` |
| iOS arm64 | `0x100233E1C` |
| iOS armv7 | `0x232B14` |

它会显式虚析构 `Modules` map 的 owned values，但对 `ManagedObjects` 只析构红黑树 node；
不会 delete image，也不会写 `image.Owner = null`。因此生命周期前置条件是：

```text
D3DImage destruction must precede root destruction
```

违反后 image destructor 会沿 dangling Owner 访问已经析构的 set。参考没有 defensive
detach，恢复源码也不能新增。

## 7. D3DPicture 对 D3DImage 的借用

四端 D3DPicture constructor 再次确认 image 参数只是 raw store：

| 目标 | constructor/inline allocation | image field |
|---|---:|---:|
| Android arm64 | `0x53F258` | `+24` |
| Android armv7 | `0x4A11B0` | `+16` |
| iOS arm64 | `0x1002424BC` | `+24` |
| iOS armv7 | `0x242340` | `+16` |

constructor 不 AddRef image、不复制其 holder、不把 image 放进其他容器。destructor 只释放
range vector 并从 `D3DLayer` listener list 注销，不访问 image。Draw 时才执行：

```text
D3DPicture.Image
  -> D3DImage.Picture
  -> holder.Object texture
```

所以 `D3DImage` 必须比所有引用它且可能 Draw 的 `D3DPicture` 活得更久。提前销毁 image
会留下 dangling pointer；参考没有保护。

## 8. 本次源码与 recovery IDB 修正

源码：

- 用真实 `TJS::tTJSRefHolder<iTVPTexture2D>` 替换形似但无类型身份的本地 holder；
- 为 `D3DImage` 恢复 virtual destructor 与第三虚槽；
- factory 将非 object 改回 `TJS_E_INVALIDTYPE`；
- constructor 去掉参考不存在的 owner-null guard；
- width/height/GetTexture 去掉参考不存在的 holder 内 texture-null guard；
- `load` 只调用一次 `GetMainImage()`，去掉 layer/image/texture null fallback；
- software 分支去掉参考不存在的创建引用 `Release()`；
- 保留 repeated-load holder overwrite 泄漏；
- 单元测试补入 D3DImage 零参数与非 object factory 边界。

四个 recovery IDB 均新增：

- `D3DImage64_guess` / `D3DImage32_guess`；
- `TJSRefHolder_Texture2D64_guess` / `TJSRefHolder_Texture2D32_guess`；
- factory、width、height、load、constructor（若保留）、complete/deleting destructor、
  clear-holder 虚槽、set insertion/erase helper 的语义名；
- 关键函数 prototype、对象/引用/泄漏边界 comments 与 bookmarks。
