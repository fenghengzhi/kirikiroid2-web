# motionplayer `PrimaryLayerManagerIndex` 全消费者与陈旧索引边界（四参考二进制）

## 结论

四个当前参考二进制一致把 `PrimaryLayerManagerIndex` 当成 `Managers` 向量中的**裸位置索引**，而不是稳定 manager ID：

- 构造时索引初始化为 `0`；
- 公开 setter 只验证“当前向量中存在该位置”，成功后只存数值；
- Add 不改变它；
- Remove 删除第一处匹配 manager 后也不修复、递减或钳制它；
- 所有主 manager 消费者都在使用时重新执行 `Managers[index]` 的范围检查。

因此删除操作可能产生两种完全不同的陈旧状态：

1. 索引仍在范围内：它会静默改指删除项之后左移进来的 manager，输入、尺寸、焦点、光标和 invalidation 整条链一起被重定向；
2. 索引超出范围：绝大多数消费者把“当前主 manager”视为 null 并 no-op/返回 null，但少数以“选中指针等于传入指针”为条件的通知函数在传入 null 时仍会进入 Window 调用。

另一个容易被旧代码注释误导的结论是：

- 两个 `tjs_int &x, tjs_int &y` 坐标变换在四份产物中都是 identity check：只检查索引能否解析到非空 manager，成功时不改坐标；
- 只有 `tjs_real &x, tjs_real &y` overload 真正缩放坐标；它按 primary size / `DestRect` 宽高缩放，并且不减 `DestRect.left/top`。

当前 `cpp/core/visual/impl/DrawDevice.cpp` 中整数 overload 的早期 `return true` 不是 Web 移植偶然遗留出的错误路径，而与四份参考二进制完全一致；真正过时、容易造成误读的是它后面的不可达旧缩放代码及其注释。

## 参考对象与入口

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `tTVPDrawDevice` vtable address point | `0x1A304D8` | `0x10C5CB4` | `0x1019B35A0` | `0x177AAA0` |
| integer device→primary transform | `0xA72AA0` | `0x796F96` | `0x1002DC214` | `0x2DBC48` |
| integer primary→device transform | `0xA72AD0` | `0x796FB4` | `0x1002DC244` | `0x2DBC66` |
| real device→primary transform | `0xA72B00` | `0x796FD8` | `0x1002DC274` | `0x2DBC84` |
| `DrawDeviceObjectBase::setLayerManagerIndex` | `0x52A498` | `0x492684` | `0x100230610` | `0x22F58C` |

本轮从 vtable address point 逐槽读取函数指针，再按 `iTVPDrawDevice` 的接口顺序映射。Android armv7 的 `OnTouchScaling` 入口 `0x79759C` 原先未被恢复 IDB 定义成函数；本轮补齐了 `[0x79759C, 0x7975BA)` 的函数边界。

## 基类对象布局

索引与向量在基类子对象中的相对位置四平台一致，仅指针宽度不同。

### 64 位

| offset | 字段 |
|---:|---|
| `+0x00` | vptr |
| `+0x08` | `Window` |
| `+0x10` | `PrimaryLayerManagerIndex` |
| `+0x18` | `Managers.begin` |
| `+0x20` | `Managers.end` |
| `+0x28` | `Managers.capacity_end` |
| `+0x30..+0x3C` | `DestRect` |
| `+0x40/+0x44` | `SrcWidth/SrcHeight` |
| `+0x48/+0x4C` | `WinWidth/WinHeight` |
| `+0x50/+0x54` | `LockedWidth/LockedHeight` |
| `+0x58..` | `ClipRect` |

### 32 位

| offset | 字段 |
|---:|---|
| `+0x00` | vptr |
| `+0x04` | `Window` |
| `+0x08` | `PrimaryLayerManagerIndex` |
| `+0x0C` | `Managers.begin` |
| `+0x10` | `Managers.end` |
| `+0x14` | `Managers.capacity_end` |
| `+0x18..+0x24` | `DestRect` |
| `+0x28/+0x2C` | `SrcWidth/SrcHeight` |
| `+0x30/+0x34` | `WinWidth/WinHeight` |
| `+0x38/+0x3C` | `LockedWidth/LockedHeight` |
| `+0x40..` | `ClipRect` |

所有 decompile 中的选择表达式都等价于：

```cpp
size_t count = (Managers.end - Managers.begin) / sizeof(void *);
iTVPLayerManager *selected =
    PrimaryLayerManagerIndex < count
        ? Managers.begin[PrimaryLayerManagerIndex]
        : nullptr;
```

它还会把向量中显式存在的 null 元素视为“没有 selected manager”。正常 Add 不禁止传 null，但随后会直接调用 `manager->AddRef()`，所以 null Add 落在崩溃边界；null 元素只能来自内存破坏或非标准构造路径。

## 公开 getter/setter

源级 setter 的语义是：

```cpp
if(index < 0 || Managers.size() <= static_cast<size_t>(index))
    throw "invalid layer manager index.";
PrimaryLayerManagerIndex = static_cast<size_t>(index);
```

四个优化产物均把两个判断合并成一次 unsigned 比较：`count <= unsigned(index)`。负数转换成很大的 unsigned 值，所以同样进入异常路径。异常前不写字段；成功路径不触发尺寸通知、对象更新、焦点迁移或任何 changed hook。

getter 只把存储的 `size_t` 强制转换回 `tjs_int`。常规公开路径只能从一个已验证的 `tjs_int` 写入，所以不会自行产生大于 `tjs_int` 的值；Remove 只会让索引相对新 count 变得陈旧，不会改变其数值。

## 三个坐标变换

### 两个整数 overload

四平台的两个函数分别编译成相同逻辑：

```cpp
return index < count && Managers[index] != nullptr;
```

它们不读取也不写入：

- `x/y`；
- `DestRect`；
- `LockedWidth/LockedHeight`；
- primary layer size。

所以鼠标点击、双击、按下/抬起/移动、滚轮、`SetCursorPos`、`SetAttentionPoint` 与 `RequestInvalidation` 的整数坐标在当前参考实现中都不缩放。失败也保留调用者传入的坐标。

### `tjs_real` overload

real overload 的顺序为：

1. 解析当前 indexed manager；不存在则返回 false，原坐标不变；
2. 调 manager 的 `GetPrimaryLayerSize(&pl_w, &pl_h)`；返回 false 时保留原坐标并返回 false；
3. 计算 `dest_w = DestRect.right - DestRect.left`、`dest_h = DestRect.bottom - DestRect.top`；
4. `x = dest_w != 0 ? x * pl_w / dest_w : 0.0`；
5. `y = dest_h != 0 ? y * pl_h / dest_h : 0.0`；
6. 返回 true。

它没有减去 `DestRect.left/top`，也不缩放 touch 的 `cx/cy`。primary size 可以是负值，产物没有额外钳制；只要 manager 报告成功，就按该有符号值参与 double 计算。

## vtable 中的全部索引消费者

### 尺寸、通知与整数鼠标链（slots 7–20）

| slot / 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 7 `GetSrcSize` | `0xA72F00` | `0x797206` | `0x1002DC4D0` | `0x2DBEB2` |
| 8 `NotifyLayerResize` | `0xA72F84` | `0x79724A` | `0x1002DC550` | `0x2DBEF6` |
| 9 `NotifyLayerImageChange` | `0xA72FC8` | `0x79726C` | `0x1002DC590` | `0x2DBF1A` |
| 10 `OnClick` | `0xA7300C` | `0x79728E` | `0x1002DC5D0` | `0x2DBF3E` |
| 11 `OnDoubleClick` | `0xA7303C` | `0x7972AC` | `0x1002DC600` | `0x2DBF5E` |
| 12 `OnMouseDown` | `0xA7306C` | `0x7972CA` | `0x1002DC630` | `0x2DBF7E` |
| 13 `OnMouseUp` | `0xA7309C` | `0x7972F6` | `0x1002DC660` | `0x2DBFA6` |
| 14 `OnMouseMove` | `0xA730CC` | `0x797322` | `0x1002DC690` | `0x2DBFCE` |
| 15 `OnReleaseCapture` | `0xA730FC` | `0x79734E` | `0x1002DC6C0` | `0x2DBFF6` |
| 16 `OnMouseOutOfWindow` | `0xA7312C` | `0x79736C` | `0x1002DC6F0` | `0x2DC014` |
| 17 `OnKeyDown` | `0xA7315C` | `0x79738A` | `0x1002DC720` | `0x2DC032` |
| 18 `OnKeyUp` | `0xA7318C` | `0x7973A8` | `0x1002DC750` | `0x2DC052` |
| 19 `OnKeyPress` | `0xA731BC` | `0x7973C6` | `0x1002DC780` | `0x2DC072` |
| 20 `OnMouseWheel` | `0xA731EC` | `0x7973E4` | `0x1002DC7B0` | `0x2DC090` |

`GetSrcSize` 先无条件写出 `LockedWidth/LockedHeight`。只有两者均大于 0 才立即返回；只要任一非正数，就尝试当前 indexed manager：

- selected manager 不存在：保留刚写出的 locked 值，可能是 `(positive, nonpositive)` 的混合值；
- manager 的 primary-size 查询成功：保留 manager 写出的值；
- 查询失败：把两者都写成 0。

`NotifyLayerResize` 与 `NotifyLayerImageChange` 没有 `selected != nullptr` guard，而是直接比较 `selected == manager`。所以：

- 正常非空 manager 只在它正好是当前 selected 时通知 Window；
- stale out-of-range 时，非空 manager 参数不匹配，no-op；
- stale out-of-range 且参数也为 null 时，比较成立，随后直接解引用 `Window`；
- selected 正常但 `Window == nullptr` 时同样直接崩溃。

slots 10–20 都以当前索引为唯一目标。鼠标/滚轮链所调用的整数 transform 已被优化合并成一次 selection check，所以坐标原样传递；键盘与 capture/out-of-window 链本来就没有坐标变换。

### touch、输入状态和 manager→Window 链（slots 21–38）

| slot / 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 21 `OnTouchDown` | `0xA7321C` | `0x797410` | `0x1002DC7E0` | `0x2DC0B8` |
| 22 `OnTouchUp` | `0xA7334C` | `0x797494` | `0x1002DC878` | `0x2DC12C` |
| 23 `OnTouchMove` | `0xA7347C` | `0x797518` | `0x1002DC910` | `0x2DC1A0` |
| 24 `OnTouchScaling` | `0xA735AC` | `0x79759C` | `0x1002DC9A8` | `0x2DC214` |
| 25 `OnTouchRotate` | `0xA735DC` | `0x7975BA` | `0x1002DC9D8` | `0x2DC268` |
| 26 `OnMultiTouch` | `0xA7360C` | `0x7975D8` | `0x1002DCA08` | `0x2DC2C4` |
| 27 `OnDisplayRotate` | `0xA7363C` | `0x7975F6` | `0x1002DCA38` | `0x2DC2E2` |
| 28 `RecheckInputState` | `0xA73640` | `0x7975F8` | `0x1002DCA3C` | `0x2DC2E4` |
| 29 `SetDefaultMouseCursor` | `0xA73670` | `0x797616` | `0x1002DCA6C` | `0x2DC302` |
| 30 `SetMouseCursor` | `0xA736AC` | `0x79763C` | `0x1002DCAA8` | `0x2DC32A` |
| 31 `GetCursorPos` | `0xA736EC` | `0x79766E` | `0x1002DCAE8` | `0x2DC358` |
| 32 `SetCursorPos` | `0xA7378C` | `0x7976C6` | `0x1002DCB88` | `0x2DC3B0` |
| 33 `WindowReleaseCapture` | `0xA737D0` | `0x7976FE` | `0x1002DCBCC` | `0x2DC3F0` |
| 34 `SetHintText` | `0xA7380C` | `0x797724` | `0x1002DCC08` | `0x2DC418` |
| 35 `SetAttentionPoint` | `0xA73850` | `0x79775C` | `0x1002DCC4C` | `0x2DC458` |
| 36 `DisableAttentionPoint` | `0xA73898` | `0x797798` | `0x1002DCC94` | `0x2DC49E` |
| 37 `SetImeMode` | `0xA738D4` | `0x7977BE` | `0x1002DCCD0` | `0x2DC4C6` |
| 38 `ResetImeMode` | `0xA73914` | `0x7977F0` | `0x1002DCD10` | `0x2DC4F4` |

`OnTouchDown/Up/Move` 是这组中唯一真正缩放位置坐标的函数。它们先用 real overload 变换 x/y，再重新选择当前 manager 并转发；`cx/cy/id` 原样保留。`OnTouchScaling/Rotate/MultiTouch` 不做位置变换，只选择并转发。`OnDisplayRotate` 是空函数；`RecheckInputState` 转发给 selected manager。

manager→Window 方法的共同规则是：selected manager 不存在即 no-op，存在但与调用方 manager 不同也 no-op。匹配时直接使用 `Window`，没有 Window null guard。

`GetCursorPos` 有一个不同的输出边界：

1. selected 不存在时立即返回，完全不写 `x/y`；
2. selected 存在时先调用 `Window->GetCursorPos(x,y)`；
3. 调用方不是 selected，或随后整数 selection check 失败时，把 `x/y` 都清零。

`SetCursorPos` 和 `SetAttentionPoint` 的反向整数变换只做 selection check，最终坐标保持原值。

### 主 layer、焦点与 invalidation（slots 39–42）

| slot / 方法 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 39 `GetPrimaryLayer` | `0xA73950` | `0x797816` | `0x1002DCD4C` | `0x2DC51C` |
| 40 `GetFocusedLayer` | `0xA73984` | `0x797834` | `0x1002DCD80` | `0x2DC53A` |
| 41 `SetFocusedLayer` | `0xA739B8` | `0x797852` | `0x1002DCDB4` | `0x2DC558` |
| 42 `RequestInvalidation` | `0xA739E8` | `0x797870` | `0x1002DCDE4` | `0x2DC576` |

三个 layer/focus 方法均是“选择当前位置；不存在则 null/no-op；存在则直接调用对应 manager vslot”。它们不检查返回 layer 是否为空。

`RequestInvalidation` 对左上和右下分别执行整数 identity check，随后把 `right`、`bottom` 各加 1，再转发给再次选出的 manager。由于整数坐标不缩放，`DestRect` 的 origin/size 不参与这个路径。`right/bottom + 1` 使用普通 32 位有符号加法；二进制没有饱和或显式溢出处理。

## 删除后的状态转移

以 `Managers = [A, B, C]`、`PrimaryLayerManagerIndex = 1` 为例：

| 操作 | 新向量 | 存储索引 | 实际 selected | 后果 |
|---|---|---:|---|---|
| 无操作 | `[A,B,C]` | 1 | B | 正常 |
| Remove(A) | `[B,C]` | 1 | C | 静默重定向到 C |
| 从原状态 Remove(B) | `[A,C]` | 1 | C | 删除 primary 后由 successor C 顶替 |
| 从原状态 Remove(C) | `[A,B]` | 1 | B | primary 未变化 |
| 先 Remove(A)，再 Remove(C) | `[B]` | 1 | null | 陈旧越界 |

重复 manager 指针仍按位置处理。base Remove 只删除第一处匹配项，所以重复 Add 后删除一次也可能令同一 manager 指针仍处在 selected 位置；插件派生 Remove 已在此前 vertical 中证明会先清除这一个 manager 对象共享的 DrawDeviceData，因此重复项仍在向量中并不保证其插件 item 仍有效。

## 当前代码与测试落点

本轮没有改变运行时语义，因为共享 core 的控制流已经与四份参考一致；只修正了 `cpp/core/visual/impl/DrawDevice.cpp` 中会误导后续恢复工作的说明：明确整数 overload 是 identity check，后面的缩放块是不可达 legacy path，并明确 real overload 不减 destination origin。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增两组保护：

- `PrimaryManagerIndexProbeDrawDevice` 直接覆盖三 manager、删除前项后静默重定向、删除当前替代项后越界、整数 identity、real scaling、`GetSrcSize` 与 primary/focus null 边界；
- 公开 `layerManagerIndex` 属性覆盖合法 0、负数、`index == size`、删除至空后 getter 仍保留 0、setter 再次拒绝 0。

## 恢复 IDB 改进

四份 recovery IDB 均已：

- 将 vtable address point 命名为 `tTVPDrawDevice_vtable_address_point`；
- 命名三个坐标变换函数；
- 命名 slots 7–42 的全部 `tTVPDrawDevice` 消费者；
- 为 setter、变换、尺寸、通知、touch、cursor、primary 与 invalidation 添加行为注释；
- 添加 identity transform、real scaling 与 consumer vtable span 三个书签；
- 补定义 Android armv7 `OnTouchScaling` 函数边界。

这些命名和注释均来自本轮对 `reference/binaries/` 四份当前参考产物的重新取证，不依赖旧 `libkrkr2.so` 注释。
