# motionplayer DrawDevice 独立尺寸域与 write-only root state（四参考二进制）

## 结论

当前四个 `reference/binaries/` 中，DrawDevice root 至少保存五套不能合并的尺寸/位置状态：

1. 主基类的 dormant initial pointer 槽与 initial width/height pair：都只有构造写入，当前插件代码范围无业务读取；
2. 主基类的 dormant screen pointer 槽与独立的 live screen width/height pair：只有后者公开为 `screenWidth/screenHeight`，并给 `D3DLayer::TransformPoint` 提供中心点；
3. 具体 root 尾部的 `PrimaryWidth/PrimaryHeight`：决定 capture 和 Show render target 的创建尺寸；
4. 具体 root 尾部的 `ScreenLeft/ScreenTop`：公开属性可读写，但当前插件业务路径不消费；
5. `tTVPDrawDevice` 次基类中的 `DestRect`、`WinWidth/WinHeight`、`LockedWidth/LockedHeight`、`ClipRect`：由 Window/core 接口维护，各自独立于上述 root 字段。

同名的 width/height 并不互相同步：

- `setPrimarySize` 不写 `screenWidth/screenHeight`，不写 `DestRect`，也不写 inherited locked/window size；
- `setScreenRect` 不写 `PrimaryWidth/PrimaryHeight` 或 `DestRect`；
- `SetDestRectangle` 不写 screen/primary/window/locked size；
- `SetWindowSize` 只写 inherited `WinWidth/WinHeight`；
- inherited `GetSrcSize` 优先读取 `LockedWidth/LockedHeight`，而不是 root 的 `PrimaryWidth/PrimaryHeight`。

本轮还纠正了旧恢复中的命名置信度：主基类前三个 state byte 只有构造合并清零，之后没有
任何访问；第四个 byte 与 screen-size/force writers 共现，像 invalidation/dirty 标记，但
四端当前插件代码区都只有 store、没有 load，也没有清回 0。源码只能命名为
`RenderTextureDirty_guess`，不能把 dirty 语义当成已经证实的原始字段名。

## 五个尺寸域

| 域 | 构造值 | 已确认写者 | 已确认业务读者 | 明确不联动 |
|---|---|---|---|---|
| `InitialSizePointer_guess` | null | constructor only | 无 | 所有 post-constructor 路径 |
| `InitialWidth/Height_guess` | 工厂参数 | constructor only | 当前完整插件范围无业务读取 | 其余所有尺寸 setter |
| `ScreenSizePointer_guess` | null | constructor only | 无 | screen W/H 的公开读写与几何消费 |
| `ScreenWidth/Height` | 工厂参数 | constructor、`setScreenRect`、两个单字段 property setter | property getter、`D3DLayer::TransformPoint` | primary、dest、window、locked |
| concrete `PrimaryWidth/Height` | 工厂参数 | `setPrimarySize`、两个单字段 property setter | capture target 创建、Show target ensure、property getter | screen、dest、window、locked |
| concrete `ScreenLeft/Top` | 0/0 | `setScreenRect`、两个单字段 property setter | property getter；无当前业务读者 | `DestRect`、screen W/H、primary |
| inherited `tTVPDrawDevice` state | rect/pairs 初始 0 | Window/core virtuals 与 `SetLockedSize` | real touch transform、dest properties、`GetSrcSize` | root screen/primary 字段 |

两个 pointer-sized size slots 在构造中都清零。在当前已圈定的 motionplayer root 函数集、析构链以及唯一跨对象 screen-size 消费者 `D3DLayer::TransformPoint` 中，四端都没有构造后的读取、写入或清理。它们因此是当前版本的 dormant pointer slots，但原始类型、名称和历史用途仍无法恢复，故继续保留 `_guess`；不能因为它们邻接两个 int32 就重写成 `tTVPRect`，也不能把相邻 pair 的活跃性反推给 pointer。

## ABI 布局

主基类的 initial/screen/state 组：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initial pointer | `+0x20` | `+0x10` | `+0x20` | `+0x10` |
| initial W/H | `+0x28/+0x2C` | `+0x14/+0x18` | `+0x28/+0x2C` | `+0x14/+0x18` |
| screen pointer | `+0x30` | `+0x1C` | `+0x30` | `+0x1C` |
| screen W/H | `+0x38/+0x3C` | `+0x20/+0x24` | `+0x38/+0x3C` | `+0x20/+0x24` |
| four-byte state group | `+0x40..+0x43` | `+0x28..+0x2B` | `+0x40..+0x43` | `+0x28..+0x2B` |

具体 root 尾部：

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `PrimaryWidth/Height` | `+0x1E0/+0x1E4` | `+0x124/+0x128` | `+0x180/+0x184` | `+0xF4/+0xF8` |
| `ScreenLeft/Top` | `+0x1E8/+0x1EC` | `+0x12C/+0x130` | `+0x188/+0x18C` | `+0xFC/+0x100` |

`tTVPDrawDevice` 次基类内部布局见 `motionplayer_drawdevice_primary_manager_index_consumers_stale_boundaries_four_binary_2026-08-15.md`。次基类在完整 root 中分别始于 A64 `+0x178`、A32 `+0xD4`、I64 `+0x118`、I32 `+0xA4`。

## root 公开尺寸入口

### 复合入口

| 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `setPrimarySize` | `0x52BA54` | `0x492DE0` | `0x100230EF8` | `0x22FD52` |
| `setScreenRect` | `0x52BA98` | `0x492E0C` | `0x100230F38` | `0x22FD80` |
| `setForceRenderTexture` | `0x52BA38` | `0x492DCE` | `0x100230EE0` | `0x22FD40` |

`setPrimarySize(width,height)` 的精确顺序是先无条件写 concrete primary pair，再读取 Window；Window 非空才调用 vslot 0 `NotifySrcResize`。即使新旧值相同也写入并通知。它不：

- release Front/Back target；
- 写第四个 root-state byte；
- 修改 screen W/H 或 screen left/top；
- 修改 inherited locked/window/destination/clip 状态。

`setScreenRect(left,top,width,height)` 总是先写 concrete `ScreenLeft/ScreenTop`。随后比较主基类 `ScreenWidth/ScreenHeight` pair：

- pair 完全相同：立即返回；left/top 已经更新，但 target 与 state byte 不变；
- 任一尺寸不同：写 screen pair，按 Front 后 Back 的顺序 Release/null target，再向第四个 state byte 写 1。

`setForceRenderTexture` 不做相等检查：总是把输入规范成 bool 写入公开 force 字段，并向第四个 state byte 写 1；它不 release target。

### 单字段 getter/setter 的正确地址

旧 public-callback 笔记曾把 getter 地址误标为 setter，并把 screen width/height setter 误标为 screen left/top。本轮四端重新反编译后的映射如下。

| 属性/方向 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| primaryWidth get/set | `0x52BA78/0x52BA80` | `0x492DF4/0x492DFA` | `0x100230F18/0x100230F20` | `0x22FD68/0x22FD6E` |
| primaryHeight get/set | `0x52BA88/0x52BA90` | `0x492E00/0x492E06` | `0x100230F28/0x100230F30` | `0x22FD74/0x22FD7A` |
| screenLeft get/set | `0x52BB10/0x52BB18` | `0x492E56/0x492E5C` | `0x100230F88/0x100230F90` | `0x22FDB0/0x22FDB6` |
| screenTop get/set | `0x52BB20/0x52BB28` | `0x492E62/0x492E68` | `0x100230F98/0x100230FA0` | `0x22FDBC/0x22FDC2` |
| screenWidth get/set | `0x52BB30/0x529A94` | `0x492E6E/0x49222C` | `0x100230FA8/0x10022FF68` | `0x22FDC8/0x22F0BE` |
| screenHeight get/set | `0x52BB38/0x529AF8` | `0x492E72/0x492268` | `0x100230FB0/0x10022FFF0` | `0x22FDCC/0x22F108` |

primaryWidth、primaryHeight、screenLeft、screenTop 的单字段 setter 都是一个直接 store，没有通知、target release 或 state-byte 写入。

screenWidth/screenHeight 的单字段 setter 则先比较旧值：相同严格 no-op；变化时写值、release/null Front，再 release/null Back，最后写第四个 state byte。它们不处理 `CurrentTarget`。

## inherited core 尺寸入口

| 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `SetDestRectangle` | `0xA72EE0` | `0x7971E0` | `0x1002DC4B8` | `0x2DBE98` |
| `SetWindowSize` | `0xA73B08` | `0x79791C` | `0x1002DCED0` | `0x2DC602` |
| `SetClipRectangle` | `0xA72EF4` | `0x7971F6` | `0x1002DC4C4` | `0x2DBEA6` |

三者都是纯 store：rect 方法各复制 16 字节，window-size 方法写两个 int32。它们不调用 root changed hook，不 release target，也不写第四个 state byte。

`SetDestRectangle` 的值由 root 的 `destLeft/destTop/destWidth/destHeight` getter 直接读取，并由 real touch transform 使用宽高；整数鼠标/光标/invalidation 变换不使用它。`SetWindowSize` 的 pair 在当前 motionplayer 插件范围没有业务读取。`SetClipRectangle` 同样没有 root 绘制路径消费者。

inherited `GetSrcSize` 与 `PrimaryWidth/PrimaryHeight` 完全分离。它读取 `LockedWidth/LockedHeight`，两者不是正数时才查询当前 indexed manager。root 构造的 primary 参数不会写 locked pair，所以一个刚构造、没有 manager 的 `DrawDeviceD3D(320,240)` 同时满足：

- `primaryWidth/primaryHeight == 320/240`；
- `screenWidth/screenHeight == 320/240`；
- `destWidth/destHeight == 0/0`；
- inherited `GetSrcSize() == 0/0`。

## screen size 的唯一业务几何消费者

`D3DLayer::TransformPoint`：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---:|---:|---:|
| `0x533688` | `0x496EFC` | `0x100235B10` | `0x2348E0` |

四端等价为：

```cpp
x = matrix.m12 + float(screenWidth / 2) + x * matrix.m0;
y = matrix.m13 + float(screenHeight / 2) + y * matrix.m5;
```

除法先按有符号整数执行，再转 float，因此负奇数向 0 截断。`ScreenLeft/ScreenTop`、`DestRect.left/top`、primary size 和 root OffsetX/Y 都不参与这个方法。

四端反编译还给出一个重要的字段边界：64 位从 parent 的 `+0x38/+0x3C` 读两个
int32，32 位从 `+0x20/+0x24` 读两个 int32；它们正是 `ScreenWidth/ScreenHeight`。
前一枚 pointer 分别位于 `+0x30` 与 `+0x1C`，没有被加载。也就是说，几何消费链只
证明 adjacent pair 是 live state，完全不赋予 `ScreenSizePointer_guess` 任何 owner/cache/
descriptor 语义。

## Primary size 与 target 复用边界

capture 每次用 `uint32(PrimaryWidth/PrimaryHeight)` 创建独立 target。Show 的 ensure gate 只检查 `BackTarget`：

```text
BackTarget != null
&& BackTarget.width  >= uint32(PrimaryWidth)
&& BackTarget.height >= uint32(PrimaryHeight)
```

通过即复用 Front/Back；失败才 release 双 target 并按 primary size 重建。因此 direct primary setter 不需要显式 dirty 才能触发扩容，但缩小只会复用较大的旧 target。负 primary 尺寸按 unsigned 参与比较/创建，没有正数 guard。

screen size 变化会无条件 release 双 target，所以下次 Show 会重建；实际新 texture 尺寸仍取 primary pair，不取 screen pair。

## 第四个 root-state byte：仅写证据

本轮在四份 IDB 的完整 motionplayer 插件代码范围搜索该字段的精确成员偏移：

- A64/I64：`+0x43`；
- A32/I32：`+0x2B`。

每端恰有四个命中，且全是 store：

1. `setForceRenderTexture`；
2. `setScreenRect` 的尺寸变化分支；
3. `setScreenWidth` 的变化分支；
4. `setScreenHeight` 的变化分支。

没有 `LDRB/LDR/load`，Show、capture、target ensure、present 和 destructor 都不读取它。它可能供链接外代码、被优化掉的路径或旧版实现消费，也可能只是保留状态；当前四份参考不能区分。因此：

- 字段保留，写入顺序保持；
- 名称保留 `_guess`；
- 测试不虚构一个 getter 或消费路径；
- 文档不再宣称“dirty 被 Show 清除/读取”。

前三个字节的边界更窄：除构造时从 group 起点执行的一次 32 位零 store 外，四端完整
root helper、primary-vtable 12 槽与已知跨对象消费者中都没有单字节或组合访问。它们不是
第四字节的 alias，也没有 setter/getter。保留三个独立 `_guess` byte 是当前最小 ABI
表达；把它们删除成 padding 会丢失构造写行为，把它们命名成具体 flag 则没有消费者证据。

四个第四字节 writer 的精确 store 地址见
`motionplayer_drawdevice_root_state_bytes_dormant_writer_only_four_binary_2026-08-15.md`。

## 源码、测试与 IDB 落点

- `cpp/plugins/DrawDeviceD3D.cpp`：字段改为 `RenderTextureDirty_guess`，注释明确四端只有 writer、没有 reader；运行时 store 语义未变。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`：root surface 测试验证 constructor 的 primary/screen 与 inherited source/destination 初值、`SetDestRectangle`/`SetWindowSize` 对 screen 的独立性、`setPrimarySize` 对 screen/GetSrcSize 的独立性、`setScreenRect` 对 primary/DestRect 的独立性。
- 四份 recovery IDB：命名 force/screen/dest getter/setter和 inherited SetDest/SetWindow/SetClip；替换旧 dirty 确定语义注释，并添加尺寸域/state-byte 书签。

这些结论只来自四份当前 `reference/binaries/`，并显式纠正旧 `libkrkr2.so` 阶段遗留的地址标签和确定性命名。
