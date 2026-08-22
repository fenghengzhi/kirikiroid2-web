# ObjSource retained texture / NCB 生命周期四参考审计（2026-08-14）

## 范围与结论

本纵切面只回答 `ObjSource` 的对象结构和所有权问题：谁构造它、NCB adaptor 何时销毁它、
`PSBRawNode` 与 lazy texture 各由谁持有、纹理在什么时点发布，以及失败路径是否回滚。
像素格式、RL 解码和 palette 展开已经由 PSB/source audit 覆盖，这里不重新推导。

四端共同支持的源级结构是：

```cpp
class ObjSource {
    PSB::PSBRawNode source; // retained PSB owner + borrowed raw-node address
    iTVPTexture2D *texture; // one retained lazy reference; initially null
};
```

64 位对象为 24 字节，32 位对象为 12 字节；没有尾部隐藏字段。`texture` 不是 borrow，
也不是 `unique_ptr`：`CreateTexture2D` 的返回值被直接写入 raw slot，`ObjSource` 析构只在
非 null 时调用一次虚 `Release`。`source` 的 raw node 地址本身不独立 retain；其同伴 owner
指针维持 PSB 存储寿命。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ResourceManager::findSource` | `0x6A7F1C` | `0x57BDE0` | `0x100102594` | `0xFF890` |
| source-hit native allocation | `0x6A83A0` (`0x18`) | `0x57BED0` (`0x0C`) | `0x10010270C` (`0x18`) | `0xFFA44` (`0x0C`) |
| `CreateAdaptor` | `0x6E9504` | `0x5A7A04` | `0x10013A190` | `0x13A274` |
| NCB default native construct | `0x6E12DC` | `0x5A1E3C` | `0x10013291C` | `0x131974` |
| `ObjSource` native dtor | `0x6E145C` | `0x5A1EE8` | `0x100132A60` | `0x131AF8` |
| adaptor conditional destroy | `0x6FBD70` | `0x5B6EA8` | `0x10014E588` | `0x150350` |
| lazy texture materialization | `0x6D7834` | `0x599A34` | `0x10012686C` | `0x125D4C` |
| texture-slot publish | `0x6D7C84` | `0x599CA2` | `0x100126BB8` | `0x1260C2` |
| `drawLayer` | `0x69AAB8` | `0x5754E4` | `0x1000F930C` | `0xF63C0` |

恢复 IDB 中以上函数已使用 `_guess` 语义名标记；表中地址只用于证据复核，不进入编译源
注释。

## 自然布局与两种构造入口

### `ResourceManager::findSource` source-hit 路径

四端在严格导航 `root["source"][group]["icon"][icon]` 后执行相同协议：

```text
native = operator new(sizeof(ObjSource))        // 24B / 12B
native.source.owner = iconEntry.owner
if owner != null: ++owner.refcount
native.source.node = iconEntry.node
native.texture = null
dispatch = ObjSourceAdaptor::CreateAdaptor(native, sticky=false, throw=false)
```

Android arm64 在 `0x6A83A4..0x6A83BC` 明确复制 owner/node、对 owner 加一并把第三槽清零；
Android armv7 对应 `0x57BED6..0x57BEE6`。iOS arm64 以 raw-node copy helper 构造前两槽，
随后在 `0x100102724` 清 `+16`；iOS armv7 对应 `0xFFA54..0xFFA5E` 清 `+8`。

这证明本地 `new ObjSource(iconEntry)` 的 source copy 需要 retain owner，而 texture 必须在
调用 adaptor 前已经为 null。

### 脚本 `ObjSource()` 默认构造

NCB `Construct` 的四个 specialization 各自分配同样的 24/12 字节 record，并将三个指针槽
全部置零，然后取得脚本 native-instance metadata，把 native 指针写入 metadata 的 native
槽。若 metadata 查询/附着失败，四端都调用 `ObjSource` native dtor，再 `operator delete`，
并返回 `TJS_E_NATIVECLASSCRASH`（`-1008`）。

因此默认构造不是“无 factory”或脚本层纯壳：它确实创建一个空 `ObjSource`。空 source 的
宽高 getter 走非 dictionary 默认值，`drawLayer` 在 category gate 处返回，析构也安全。

## NCB adaptor 的 native/sticky 生命周期

adaptor 本身在 64 位的关键字段为 `native@+8, sticky@+16`，32 位为
`native@+4, sticky@+8`。四端 conditional destroy 等价于：

```text
native = adaptor.native
if native != null && adaptor.sticky == false:
    native.~ObjSource()
    operator delete(native)
adaptor.native = null
adaptor.sticky = false
```

`sticky==true` 时 adaptor 明确不销毁 native record；仍会清 adaptor 自身的 native/sticky
状态。`ResourceManager::findSource` 调用 `CreateAdaptor` 时传入 false，因此通常由脚本
instance adaptor 最终拥有 native record。

四端类注册 wrapper/UTF-16 `ObjSource` xref 也确认这是 `Motion.ObjSource` 自己的 NCB 模板
实例，而不是相邻 `SeparateLayerAdaptor` 的 cleanup：

| 目标 | `ObjSource` UTF-16 数据 | registrar xref / wrapper |
|---|---:|---:|
| Android arm64 | `0x14D69DA` | `0x6D73D4/0x6D7418`, wrapper `0x6FB9F0` |
| Android armv7 | `0x599474` | `0x599384`, wrapper `0x59977C` |
| iOS arm64 | `0x10195D366` | `0x100125C18`, wrapper `0x1001260DC` |
| iOS armv7 | `0x174F6CA` | `0x124DEA..0x124DF6`, wrapper `0x125194` |

## 析构顺序：texture 后 source owner

四个 native dtor 都先读取最后一个 texture 槽；非 null 时从纹理 vtable 调用 `Release`。
之后才检查第一个 PSB owner 槽并降低其引用计数/调用 raw-owner release helper。中间的 raw
node 地址没有独立析构动作。等价源级顺序为：

```text
ObjSource::~ObjSource():
    if texture != null:
        texture.Release()
    source.~PSBRawNode() // release retained owner; node is borrowed within owner
```

这正好对应 C++ 声明顺序：显式析构 body 释放 texture，body 返回后隐式执行 `_source`
成员析构。四端 native dtor 都不把 texture、owner 或 node 槽清零；slot clearing 属于 adaptor
壳，而不是 pointee。

## lazy texture 的发布协议

四个 `ensureTexture` 首先检查最后一个槽，非 null 立即返回。为空时读取严格 raw-node
`width/height/compress/pixel/pal`，准备 BGRA 对齐缓冲，构造临时 `tTVPBitmap`，填充 scanline，
最后执行：

```text
manager = TVPGetRenderManager()
texture = manager.CreateTexture2D(bitmap) // direct write to member slot
bitmap.Release()
AlignedDealloc(bgra)
```

关键点是 `CreateTexture2D` 返回值直接发布到 `ObjSource`：

- Android arm64：`STR X0, [X19,#0x10]`；
- Android armv7：`STR.W R0, [R11,#8]`；
- iOS arm64：`STR X0, [X19,#0x10]`；
- iOS armv7：`STR R0, [R1,#8]`。

发布发生在临时 bitmap release 和 aligned pixel free **之前**。没有临时 texture smart owner、
没有最后 exchange，也没有“Layer 成功后才提交”的事务式语义。这是 retained raw slot 的直接
证据；对象析构时恰好消费其一份引用。

## `drawLayer` 数据流与边界

四端共同流程是：

```text
if source.category != dictionary:
    return
ensureTexture()
layer = NativeInstanceSupport(target, Layer)
layer.AssignTexture(texture)
layer.SetSize(texture.width, texture.height)
```

`AssignTexture` 按已经闭合的 Layer/D3D 所有权协议自行 retain 新 texture、release 旧 texture，
所以 `ObjSource` 和 Layer 随后各自持有一份引用；`ObjSource` 没有转移/清空自己的槽。

需要保持的原版边界：

1. `CreateTexture2D` 若正常返回 null，null 会原样写槽；bitmap/pixel 临时量仍按正常尾部释放，
   随后的 `drawLayer` 没有 null guard，会自然解引用 null texture。
2. `AssignTexture` 或后续 `SetSize` 失败时，没有任何路径回滚已经发布的 ObjSource texture；
   下次调用看到非 null 槽会跳过重新 materialize。
3. 成功 `AssignTexture` 后、`SetSize` 前失败时，Layer 已经持有自己的引用，ObjSource 仍持有
   原引用。这不是 move/transfer。
4. `CreateTexture2D` 抛异常时各个像素临时量的 landing-pad 行为后来已由
   `motionplayer_objsource_texture_exception_matrix_four_binary_2026-08-15.md` 闭合：只清理
   部分 raw-node/palette/未完成 bitmap 临时量，不托管 decoded/BGRA，也不发布 texture。

## adaptor 创建失败的刻意非回收边界

`ResourceManager::findSource` 在 native 已完成 source owner retain 和 texture 零初始化后调用
`CreateAdaptor`。若 helper **正常返回 null**，四端都只把结果 Variant 设为 void，随后释放
raw-node/字符串等栈临时量；没有调用 `ObjSource` dtor，也没有 `operator delete(native)`。

因此这个失败分支泄漏刚分配的 native facade，同时额外保留其 PSB owner 引用。当前本地实现
显式保留了这一行为，不能改写为 `std::unique_ptr<ObjSource>` 或在 null branch 主动 delete，
否则会消除参考实现的边界。

这与 NCB 默认构造 attach 失败不同：默认构造 helper 自己明确 dtor+delete；findSource 已经把
native raw pointer 交给 CreateAdaptor helper，但 helper 返回 null 时调用方没有 reclaim。

## 本地实现对齐

本次没有改变运行时语义；现有实现已经满足四端证据：

- `SourceCache.h` 使用 `PSBRawNode` 后接 raw `iTVPTexture2D*`，对象自然尺寸正确；
- `ObjSource::~ObjSource` 显式 `Release` texture，随后由成员析构释放 source owner；
- `ensureTexture_guess` 直接写 `_texture`，再释放 bitmap 和 BGRA；
- `ResourceManager::findSource` 在 adaptor null 时直接返回 void，不回收 `src`；
- `drawLayer` 不添加 texture null guard，也不在 Layer 调用失败时清 `_texture`。

本次只把相关编译源中的旧绝对地址/单目标措辞替换为四参考语义注释，并把 texture publish、
析构顺序和 retained ownership 写清楚。四份 recovery IDB 同步补上语义名、所有权注释并保存。

## 相邻纵切面迁移状态

- `CreateTexture2D` 或 bitmap/pixel 处理中抛异常时的 landing pad 与临时量泄漏矩阵已由
  `motionplayer_objsource_texture_exception_matrix_four_binary_2026-08-15.md` 闭合；
- `ResourceManager` 完整 module map 节点/rehash ABI、record replacement 生命周期与 Win/KRKR
  页面 texture construction-reference 异常泄漏边界已由
  `motionplayer_resource_manager_module_map_lifecycle_four_binary_2026-08-14.md`、
  `motionplayer_resource_texture_construction_exception_four_binary_2026-08-15.md` 闭合；
- `SourceCache::Entry` 中脚本 source dispatch（borrowed）、Layer Variant owner、按值 list node、
  copy-before-erase、trim/clear 与析构引用图已由
  `motionplayer_source_cache_entry_lifetime_four_binary_2026-08-13.md`、
  `motionplayer_source_cache_clear_cache_boundary_four_binary_2026-08-13.md` 闭合。

以上三个原相邻开放项均已迁移为四端闭合结论，不再作为待办。它们不改变本纵切面对
`ObjSource` 三槽布局、retained texture、NCB owner 和正常/明确失败边界的结论。
