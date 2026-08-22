# MotionPlayer generic source fallback 的分派、owner 与失败边界（四参考，2026-08-16）

## 1. 结论

四个 `reference/binaries/` 的 `MotionNode_findSource_guess` 尾部都不是直接调用 native
`ResourceManager::findSource(ttstr, ttstr)`。它们保留 Player 的第一份 ResourceManager
dispatch owner，以 TJS `FuncCall("findSource")` 发出两个 Variant 参数：

1. Player persistent motion context 的独立 Variant copy，类型原样保留；
2. 由 `src` 与 `icon` 组成的 fallback path String Variant。

resolver 把调用结果直接写入 persistent `SourceState::object`，并按“调用状态必须恰为
`TJS_S_OK` 且结果类型不是 Void”决定是否继续。继续路径先写 `valid = true`，然后对结果
做严格 Object 转换。一次转换取得的独立 owner 覆盖 width、height、originX、originY、
blank 和 clip 的完整 getter 序列；getter 即使重入并替换 persistent result Variant，也
不能缩短 receiver 生命周期。

clip 则只以 `Type == tvtObject` 为进入条件。Object 类型进入后再次严格转换并建立独立
owner；null Object closure 不会被友好降级成默认 clip。只有非 Object 类型才写入
`[0, 0, 1, 1]` 默认值。

## 2. fresh 四端映射

| 目标 | resolver | fallback FuncCall | hint | `valid=true` | source copy / strict Object | clip get / Type gate |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64 | `0x691CC8` | `0x692860` | `0x1AB5208` | `0x692868` | `0x692874` / `0x692890..0x6928C4` | `0x6929B0` / `0x6929D0` |
| Android armv7 | `0x570500` | `0x570A4E` | `0x111173C` | `0x570A52` | `0x570A58` / `0x570A6C` | `0x570B1E` / `0x570B26` |
| iOS arm64 | `0x1000F316C` | `0x1000F3900` | `0x101B696D0` | `0x1000F3908` | `0x1000F3914` / `0x1000F3930` | `0x1000F3A10` / `0x1000F3A1C` |
| iOS armv7 | `0xEF97C` | `0xF0100` | `0x187D400` | `0xF0104` | `0xF010E` / `0xF012C` | `0xF0238` / `0xF0240` |

四个 hint 地址都是 resolver 专用的 process-global mutable slot。xref 复核没有把它们与
native `ResourceManager::findSource` 的内部实现或其他同名调用混为一谈。

## 3. 参数数据流

四端在 resolver 入口都严格取得第一份 ResourceManager dispatch，并复制 Player 的
persistent context Variant：

| 目标 | ResourceManager Object conversion | context Variant copy |
|---|---:|---:|
| Android arm64 | `0x691D10..0x691D3C` | `0x691D48` |
| Android armv7 | `0x57052E` | `0x570532` |
| iOS arm64 | `0x1000F31B4` | `0x1000F31C0` |
| iOS armv7 | `0xEF9BA` | `0xEF9C4` |

fallback path 的构造规则仍是：先复制 raw `src`；`icon` 非空时追加 `/` 和 raw `icon`。
因此空 `src` 加非空 `icon` 会产生带前导斜线的路径，二者都空时仍会以空 String 发起
调用。TJS argv 的物理顺序始终为 `[contextVariantCopy, pathStringVariant]`，argc 为 2，
receiver 与 objthis 都是 retained ResourceManager dispatch。

这使 script override、代理对象、非 String context，以及 getter/call 重入都成为可观察
行为。原本地实现先执行 `static_cast<ttstr>(_findMotionContextVariant)` 再直调 native
method，既丢失 context 类型，也绕过 TJS override 和 raw HRESULT。

## 4. 状态与结果门槛

四端 fallback call 后的共同控制流为：

```text
status = rm.findSource(contextVariantCopy, pathStringVariant,
                       output = source.object)
if (status != TJS_S_OK || source.object.Type == tvtVoid) {
    source.valid = false
    return
}
source.valid = true
strictSourceOwner = AsObject(CopyRef(source.object))
```

这里有三个容易被高层重写抹掉的边界：

- `TJS_E_FAIL` 即使把 output 写成非 Void，也只写 `valid=false`；output slot 不被额外清空；
- `TJS_S_OK + Void` 是普通 invalid return；
- `TJS_S_OK + 非 Void 非 Object` 先写 `valid=true`，随后在严格 Object 转换处抛错。

所以 `Type != Object || AsObjectNoAddRef() == nullptr` 的合并式友好过滤是不正确的。尤其
Object closure 的 dispatch 为 null 时，参考实现仍进入严格 owner/getter 路径，并在实际
使用处自然失败。

## 5. source owner 的完整寿命

成功路径在四端均按以下顺序构造 owner：

1. CopyRef persistent `source.object` 到临时 Variant；
2. 对临时 Variant 做严格 `AsObject`，取得 AddRef 后的 dispatch；
3. 立即析构临时 Variant；
4. 用同一 owner 读取 width、height、originX、originY、blank、clip；
5. clip 和 textureRect 完成后才 Release source owner。

对应的本地形状为 `ncbPropAccessor sourceObject{tTJSVariant(dst.object)}`，后续 helper 全部
接收 `sourceObject.GetDispatch()`。这不是单纯的性能优化：如果 width getter 重入并清空
`dst.object`，height 及后续 getter 仍必须在已经 retained 的 dispatch 上继续。逐项从
`dst.object` 重新取 dispatch 会在第二项开始错误抛出 Void-to-Object 转换异常。

## 6. clip 的独立类型门槛

source owner 先读取 clip Variant。四端随后只比较 Variant type tag：

```text
if (clip.Type == tvtObject) {
    clipOwner = AsObject(CopyRef(clip))
    left   = clipOwner.left
    top    = clipOwner.top
    right  = clipOwner.right
    bottom = clipOwner.bottom
} else {
    left = 0; top = 0; right = 1; bottom = 1
}
```

Android arm64 的 type gate 是 `0x6929D0`，Android armv7 是 `0x570B26`，iOS arm64
是 `0x1000F3A1C`，iOS armv7 是 `0xF0240`。四端 gate 到严格转换之间都没有 null-dispatch
检查。因此本地删除了 `&& clipValue.AsObjectNoAddRef()`，并用独立
`ncbPropAccessor` 覆盖四次 clip getter。

## 7. 源码与回归覆盖

本轮源码迁移包括：

- `Player::dispatchFindSource_guess`：严格保留第一份 ResourceManager TJS receiver，复制
  原始 context Variant，构造 path Variant，返回 raw FuncCall status；
- `Player::findSource`：保留便利返回接口，但内部走同一 TJS dispatch；
- `Player::findSourceForNode_guess`：观察 raw status/Void，成功后建立一个 source owner；
- `MotionDispatch.h`：增加 retained-dispatch 版 bool getter，使 blank 与其他属性使用同一
  生命周期边界；
- clip 路径改为纯 type gate 加严格独立 owner。

单元测试分别固定：

1. Integer context 以 Integer Variant 进入 argv[0]，没有提前转成 String；
2. receiver/objthis、成员名、argc、fallback path 和 result output 的完整 TJS call shape；
3. 失败 HRESULT 加非 Void output 时 `valid=false`，output slot 保留；
4. 成功 Integer output 先提交 `valid=true` 再抛严格 Object 转换异常；
5. width getter 清空 persistent `source.object` 后，同一 retained owner 仍完成全部 getter；
6. 非 Object clip 写入 `[0, 0, 1, 1]`。

portable C++ 注释只保留上述语义；所有绝对地址和 ABI 对照都限制在本文与四份 recovery
IDB 中。
