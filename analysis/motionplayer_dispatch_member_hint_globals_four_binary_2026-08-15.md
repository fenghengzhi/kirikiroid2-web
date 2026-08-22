# MotionPlayer dispatch member-hint 全局与旧地址迁移（四参考，2026-08-15）

> 部分结论已被替代：下文 `meshCopy`/render-helper 段落来自较早的 recovery
> IDB 阶段。V159 的 fresh 四参考证据证明这里实际是连续十二个、每项 4 字节的
> renderer-primitive hint 族，而不是四槽或 iOS byte-item 结构。以
> `motionplayer_renderer_primitive_hint_family_four_binary_2026-08-16.md` 为准。

## 范围与结论

本轮重新核对 `MotionDispatch.h` 中仍带旧 `libkrkr2.so` 单端地址的八个 TJS
member-hint 槽：

```text
window, piledCopy, assignImages, Layer,
meshCopy, bezierPatchCopy, affineCopy, bufLayer
```

四份当前 1.3.9 参考共同证明：

- 每个槽都是独立的、进程级、零初始化 `tjs_uint32`；
- 使用该槽的 dispatch call 传入它的地址，TJS dispatch 可以原位更新 hint；
- 插件侧不在每次调用前清零，也没有卸载/失败 rollback；
- 同一槽会被该语义的多个 call site 复用，但不能把“同一字符串字面量”推广成整个进程
  所有调用都必须使用该槽；
- `SeparateLayerAdaptor` constructor 读取 `window` 时明确传 null hint，而 internal-layer
  materialization 的另一条 `window` 读取才使用这里的 `window` 槽；
- 本地 `SeparateLayerAdaptor` assign helper 原先对 `assignImages` 传 null，与四端都不符，
  已恢复为传 `assignImagesMemberHint_guess`；
- 源码中八个旧 Android arm64 绝对地址已经删除；地址只保留在本文件。

## 四端全局映射

| hint 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `assignImages` | `0x1AB544C` | `0x11118E8` | `0x101B69914` | `0x187D5B8` |
| `meshCopy` | `0x1AB5458` | `0x11118F4` | `0x101B69920` | `0x187D5C4` |
| `bezierPatchCopy` | `0x1AB545C` | `0x11118F8` | `0x101B69924` | `0x187D5C8` |
| `affineCopy` | `0x1AB5460` | `0x11118FC` | `0x101B69928` | `0x187D5CC` |
| `bufLayer` | `0x1AB5468` | `0x1111904` | `0x101B69930` | `0x187D5D4` |
| `window` | `0x1AB54A0` | `0x111193C` | `0x101B69968` | `0x187D60C` |
| `piledCopy` | `0x1AB54A4` | `0x1111940` | `0x101B6996C` | `0x187D610` |
| `Layer` | `0x1AB553C` | `0x11119BC` | `0x101B699FC` | `0x187D68C` |

旧源码注释写的是 Android arm64 `0x1AB844C`、`0x1AB8458`、`0x1AB845C`、
`0x1AB8460`、`0x1AB8468`、`0x1AB84A0`、`0x1AB84A4`、`0x1AB853C`。当前参考中
对应槽统一位于低 `0x3000` 的另一 BSS 布局；旧地址不是可继续沿用的 symbol identity。

八个当前槽在静态 image 中都为 0。iOS recovery IDB 已能把每个槽硬化为独立 4-byte
`unsigned int`。Android recovery loader 把同一区间表现为较大的聚合 BSS item；精确 offset
的 xref、注释和书签仍可确认，但不能把聚合基址的 Hex-Rays 表达误读为 64-bit hint。
例如 arm64 经常显示 `qword_1AAF7B8 + offset`，实际传给 dispatch 的最终地址仍是上表中
4-byte 槽。

## 字符串定位方式

普通 IDA string search 对八个名称除常见 ASCII 子串外大多返回空。四端实际 TJS
字面量均以 UTF-16LE 存在；使用包含终止 16-bit zero 的 byte pattern 后，八个名称在四端
都可定位。它们不是 UTF-32LE。代表性 literal 地址如下：

| literal | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `assignImages` | `0x14CAE2C` | `0xD7DBFE` | `0x10195C6C8` | `0x174EA2C` |
| `meshCopy` | `0x14D55FC` | `0xD851D4` | `0x10195B9FC` | `0x174DD60` |
| `bezierPatchCopy` | `0x14D5642` | `0xD8521A` | `0x10195BA42` | `0x174DDA6` |
| `affineCopy` | `0x14D6188` | `0xD85B26` | `0x10195C726` | `0x174EA8A` |
| `bufLayer` | `0x14D5866` | `0xD853D2` | `0x10195BCC4` | `0x174E028` |

`window`、`Layer` 等常用名字在整份二进制有多个 literal instance；全局 identity 必须通过
具体 dispatch call 的 hint argument 和槽 xref 确认，不能只凭最近字符串地址配对。

## 调用数据流

### `window`

internal-layer materialization 的 hint-bearing read：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6CB644` | `0x592FCE` | `0x10011E348` | `0x11CB86` |

共同调用形状：

```text
target.PropGet(0, "window", &windowHint, &owner, target)
```

同一插件中的 `SeparateLayerAdaptor` constructor 则在 `0x6C3E44` / `0x58DC18` /
`0x100129920` / `0x12891A` 读取 `window`，四端 hint 参数都为 null。这说明 hint 是
call-family 状态，不是由字符串自动选择的全局 singleton。本地 constructor 已保持 null，
materialization 使用全局槽。

### `piledCopy`

`Player_updateAccurateSLAAfterDraw_guess` 在 `0x6CBF8C` / `0x59348A` /
`0x10011E9E0` / `0x11D27A` 调用七参数 `piledCopy`，四端均把上表槽地址作为
FuncCall hint。caller 不在调用前后显式读取、清零或恢复该 word。

### `assignImages`

`SeparateLayerAdaptor` assign 路径的关键 argument formation 为：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6A99FC/0x6A9A14` | `0x57C8FE/0x57C90A` | `0x1001035D0` | `0x1009A8/0x1009B2` |

随后以一个 source Variant 参数调用 `assignImages`。四端都不是 null hint。该槽还由
SourceCache/Player 的其他 assignImages 路径复用；字符串 xref 与 data-slot xref 落在同一
call family。当前源码已把 `callAssignImages_guess` 从：

```cpp
target->FuncCall(0, TJS_W("assignImages"), nullptr, ...);
```

恢复为：

```cpp
target->FuncCall(0, TJS_W("assignImages"),
                 &motion::detail::assignImagesMemberHint_guess, ...);
```

这个改动不增加错误处理或重试；dispatch 抛出时，hint word 是否已经被部分更新由被调
dispatch 决定，插件没有 rollback。

### `Layer`

四端创建 Layer 时共同传一个共享 hint：Android armv7 独立 helper `0x57AC5C`、iOS arm64
`0x100100908`、iOS armv7 `0xFDA78`；Android arm64 将同一源 helper 内联到多个 caller，
但这些 caller 都形成 `0x1AB553C`。源级调用是：

```text
global.CreateNew(0, "Layer", &layerClassHint,
                 &created, 2, {owner, target}, global)
```

helper 复用解释了为什么 iOS 的 data xref 可能只显示一个 helper，而该 helper 本身有多个
caller；不能据此误判为 per-call local hint。

### render helper family

`meshCopy`、`bezierPatchCopy`、`affineCopy` 是 FuncCall hint，`bufLayer` 是 PropGet hint。
`Player_renderToCanvas_guess` 中的代表性 argument formation：

| 名称 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `meshCopy` | `0x6C5270` | `0x58EAF2` | `0x100118E84` | `0x1173B8` |
| `bezierPatchCopy` | `0x6C5638` | `0x58EEC6` | `0x100119010` | `0x117598` |
| `affineCopy` | `0x6C547C` | `0x58ED08` | `0x100119208` | `0x1177FC` |
| `bufLayer` | `0x6C4FE8` | `0x58E85E` | `0x100118C34` | `0x117092` |

每个名称有自己的 4-byte slot；相邻地址不是数组索引协议，源码保持八个有语义名的独立
globals，而不暴露平台 BSS 布局。

## 生命周期与边界

共同生命周期是：

```text
image load: hint word = 0
first matching dispatch call: pass &hint
dispatch may read 0, resolve member, and publish an implementation-defined hint
later matching call sites: pass the retained word
plugin shutdown: no explicit destructor/reset for the scalar
```

这些 word 不拥有字符串、dispatch 或 member 对象；没有 AddRef/Release。它们是裸的可变缓存
状态，未使用 atomic/lock；多个线程同时经 dispatch 修改同一 slot 没有插件级同步保证。
不同 literal 的 slots 不 alias，单个 lookup 的失败/异常也不会触发其他 slots 的清零。

## IDB 与源码状态

- 四端所有八个槽均写入 zero-init/process-global/member-hint 注释；
- 四端 assignImages 和 window 代表 call site 已补参数语义注释；
- 四端为 assignImages/window/Layer 三个 anchor 添加书签；
- iOS 两端八个槽已建立独立 4-byte 类型和 `g_motion_*MemberHint_guess` 名称；
- Android 两端因聚合 BSS item 保留 exact-offset 注释/书签，未把 aggregate qword/dword base
  假装成单一 hint；
- 四份 recovery IDB 均已保存并返回 `ok=true`；
- `MotionDispatch.h` 删除八个旧单目标地址；
- `SeparateLayerAdaptor.cpp` 恢复 assignImages hint 参数。

二进制未保留原始 C++ variable names，因此本地和 IDB 的语义名继续带 `_guess`。
