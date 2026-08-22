# MotionPlayer 共享 Layer 工厂、ordinal resolver 与 workspace 异常边界四参考复原

日期：2026-08-15

> **后续补充（2026-08-17 / V185）**：command-builder composed Layer 传给共享 factory 的
> owner/parent Variant 来源现已闭合：每个 `composedLayer == Void` 门内重新求值
> `Window.mainWindow`，再以 strict accessor、flags 0、共享 exact hint 和非空 result Variant
> 读取 `primaryLayer`。它们不是 builder 入口预取的 raw scratch。完整求值与清理顺序见
> `motionplayer_build_render_commands_primary_layer_on_demand_hint_lifecycle_four_binary_2026-08-17.md`。

## 1. 结论

四份当前参考共同证明，motionplayer 并不存在 `SourceCache`、`SeparateLayerAdaptor`、
render-command builder 和 Player workspace 各自独立的一套 Layer factory。Android armv7、
iOS arm64、iOS armv7 三端都保留了同一个 standalone helper，并且该 helper 在每端恰好有
七个调用点。Android arm64 将相同短函数内联到这些调用者中；这是优化差异，不是源文件中
存在七份实现。

共同源语义是：

```cpp
tTJSVariant createLayerVariant_guess(
    const tTJSVariant &owner,
    const tTJSVariant &parent) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    iTJSDispatch2 *created = nullptr;
    tTJSVariant *args[] = {
        const_cast<tTJSVariant *>(&owner),
        const_cast<tTJSVariant *>(&parent)
    };
    (void)global->CreateNew(
        0, TJS_W("Layer"), &layerClassMemberHint_guess,
        &created, 2, args, global);
    tTJSVariant result(created, created);
    created->Release();
    global->Release();
    return result;
}
```

这里的尖锐边界是可观察语义，不是遗漏的防御代码：`CreateNew` 的 HRESULT 被丢弃，没有
global/created scope guard，没有 null 检查，也没有 class-dispatch 预查询。普通失败若留下
`created == nullptr`，控制流最终自然到达 null raw dispatch 的 `Release`；`CreateNew` 抛出时，
已经取得的 global raw ref 不由该 helper 回收。正常路径则先让返回 Variant 持有 Object 和
ObjThis 两份 closure 引用，再按 `created -> global` 顺序释放 factory raw owners。

本轮还闭合了同一 `SeparateLayerAdaptor` 上第二条 payload-free resolver。它与 full-payload
resolver 共用 active/retired 红黑树，但只搬移 Layer Variant；miss 才调用上述共享 factory。
它写 `absolute = base + sequence` 与 `hitThreshold = 256`，却不会像 full-payload resolver 那样
递增 sequence。accurate renderer 和 sticky shared-D3D draw route 都直接调用该 overload。

最后，Player 内部 primary/work Layer 的 lazy materializer 使用同一 factory 两次，并保持
primary-only gate、publish-before-sizing 和不回滚的 sticky partial state。各平台异常机制只
清理已构造的局部临时量，不会撤销已经写入 Player 成员的 primary/work Variant。

本文绝对地址只作为 `reference/binaries/` 四端分析坐标；编译源码不嵌入这些地址，也不沿用
旧 `libkrkr2.so` 注释。

## 2. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| shared Layer factory | inline | `0x57AC1C` | `0x1001008A8` | `0xFDA14` |
| payload-free ordinal resolver | `0x6C90C4` | `0x591DEC` | `0x10011C628` | `0x11AE24` |
| full-payload resolver | `0x6C3F28` | `0x58DCD4` | `0x100117E88` | `0x115B34` |
| command builder | `0x6C2208` | `0x58C7C4` | `0x1001167BC` | `0x114118` |
| workspace materializer | `0x6CB57C` | `0x592F7C` | `0x10011E2BC` | `0x11CAC8` |
| factory SjLj cleanup | n/a | n/a | n/a | `0xFDB22` |
| materializer SjLj cleanup | n/a | n/a | n/a | `0x11CDE0` |

三份 standalone helper 已统一命名为 `Motion_createLayerVariant_guess`；四端 payload-free
resolver 已统一命名为 `SeparateLayerAdaptor_resolveLayerOrdinal_guess`。原
`SeparateLayerAdaptor_createLayerVariant_guess` 名称范围过窄，因为 xref 集合跨越 SourceCache、
Player builder 与 workspace，并不隶属于 SLA resolver。

## 3. 七个 factory 调用家族

三份非内联目标的 helper 都恰好有七个 code xref，集合完全同构：

| 调用家族 | Android arm64 | Android armv7 call | iOS arm64 call | iOS armv7 call |
|---|---:|---:|---:|---:|
| `SourceCache` constructor | inline in `0x6A4CD4` | `0x57AB62` | `0x1001007D8` | `0xFD91C` |
| `SourceCache::loadSource` miss | inline in `0x6A4F88` merged chunk | `0x57AE8C` | `0x100100C3C` | `0xFDDB8` |
| command-builder composed Layer | inline in `0x6C2208` | `0x58D5B2` | `0x100117668` | `0x1150E6` |
| full-payload SLA resolver miss | inline in `0x6C3F28` | `0x58DE9C` | `0x1001180B0` | `0x115DEA` |
| payload-free ordinal resolver miss | inline in `0x6C90C4` | `0x591E88` | `0x10011C6EC` | `0x11AF20` |
| workspace primary Layer | inline in `0x6CB57C` | `0x592FD8` | `0x10011E358` | `0x11CB94` |
| workspace work Layer | inline in `0x6CB57C` | `0x5930C6` | `0x10011E4A0` | `0x11CCDE` |

这个精确的跨调用者 xref 集合是恢复“一个 source-level helper”的主要证据。仅比较 helper
body 会遗漏其源文件作用域；仅看 Android arm64 又会把编译器内联误判成多个独立函数。

## 4. Factory 数据流与引用所有权

四端正常路径共同为：

```text
global = TVPGetScriptDispatch()                    // raw owning ref
created = null
args[0] = &owner                                   // borrowed Variant address
args[1] = &parent                                  // borrowed Variant address

global.CreateNew(
    flags=0,
    member="Layer",
    hint=&sharedLayerHint,
    result=&created,
    argc=2,
    argv=args,
    objthis=global)
// status ignored

result = Object(created, created)                  // closure owns both refs
created.Release()                                  // release factory result owner
global.Release()                                   // release script-global owner
return result
```

它明确反证了本地旧 `createLayerObject` 形状：

- 不先 `PropGet("Layer")` 取得 class object；
- 不对 class object 执行匿名 `CreateNew(0, nullptr, ...)`；
- 不依据 HRESULT 把失败变成 null return；
- 不检查 owner、parent、global 或 created；
- 不把 raw created dispatch 返回给调用者再由调用者包装；
- 不以 caller `objthis` 替换 global receiver/objthis。

`PrivateMotionGLL` 的 private-class factory 是另一条独立对象工厂链，本轮没有把它错误并入
这个 helper。

## 5. 异常与普通失败边界

### 5.1 普通失败

四端都丢弃 `CreateNew` 返回码。若调用普通返回却没有发布 `created`：

1. helper 仍尝试建立 Object/ObjThis 相同的 Variant；
2. 随后无条件对 raw `created` 执行 `Release`；
3. 没有 safe-null Variant、错误码传播或重试分支。

Android arm64 的内联 miss branch 也显式保留 null 分支后对 null vtable 的后续解引用，因此
不能把三份 standalone helper 的行为解释为反编译器漏掉的检查。

### 5.2 `CreateNew` 抛出

源结构在 `TVPGetScriptDispatch()` 之后、`CreateNew` 之前没有 RAII owner。四端共同含义是：

- 已取得的 global raw ref 不会由 helper 正常尾部释放；
- 如果 callee 在发布 created 后再抛出，raw created 也没有正常尾部回收保证；
- 调用者只负责其自身已经构造完成的 Variant/accessor 临时量，不能替 helper 回收未发布的
  raw locals。

iOS armv7 提供最清楚的编译器证据：`Motion_createLayerVariant_unwindCleanup_guess` 的 SjLj
注册发生在 `CreateNew` 返回之后。其 ordinary cases 只销毁已经构造完成的 return Variant
prefix，再 resume；它不能覆盖 `CreateNew` 内抛出时的 raw global owner。另一个 selector 只在
cleanup 自身再次抛出时进入 abort，不是业务失败恢复。

Android armv7 使用 EHABI call-site metadata，iOS arm64 使用 Itanium EH tables/cold cleanup，
Android arm64 在各内联调用者中生成 landing pads。机制不同，但没有任何一端出现 source-level
catch、HRESULT repair 或 raw-global scope guard。

## 6. Payload-free ordinal resolver

共同伪代码为：

```cpp
tTJSVariant resolveLayerOrdinal_guess(uint32_t ordinal) {
    tTJSVariant result;
    auto retired = retiredLayers.find(ordinal);
    auto &active = activeLayers[ordinal];
    if(retired != retiredLayers.end()) {
        active.layerVariant = retired->second.layerVariant;
        result = active.layerVariant;
        retiredLayers.erase(retired);
    } else {
        active.layerVariant =
            createLayerVariant_guess(owner, targetLayer);
        result = active.layerVariant;
    }

    result.absolute = absolute + assignSequence;
    result.hitThreshold = 256;
    return result;                    // no ++assignSequence
}
```

四端对象偏移与既有 SLA 布局一致：owner/target Variant 位于对象前缀，active/retired 是同一
两棵 map，absolute/sequence 位于两棵树之后。resolver 没有第三套 map，也没有 Web-only
`Player::_renderLayerStates`。retired hit 只向 active 复制 Layer Variant，不复制
`SeparateLayerPayload_guess` 的 command string、颜色、viewport 或 mesh vectors。

这条 overload 的两位直接调用者为：

| 调用者 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| accurate SLA renderer | inside `0x6C7088` | `0x5910C2` | `0x10011B634` | `0x119B16` |
| sticky shared-D3D draw route | `0x6D3844` | `0x5979E2` | `0x100123E9C` | `0x123158` |

accurate renderer 在该调用后继续取得 Layer dispatch、刷新图像/尺寸和属性；没有
`createdOrChanged` Boolean。旧端口虽然 full-payload comparator 的 shipped behavior 总返回
true，通常也会每帧刷新，但仍错误地复制了整份 payload 并调用了会递增 sequence 的另一条
resolver。本轮已改为 payload-free overload。

sticky shared-D3D draw caller 还包含 target fallback、两棵树 swap/normal-tail cleanup 与 shared
D3D capture 的更大状态机。该外围状态机随后已经四端逐分支闭合并落地，详见
`motionplayer_shared_d3d_adaptor_lifecycle_four_binary_2026-08-14.md` 的第 6 节；本文件仍只把
ordinal overload 当作 factory/resolver 纵切面的直接调用者。

## 7. Player workspace materializer

四端共同状态机为：

```text
if primaryInternalLayer.Type != Void:
    return

targetAccessor = strict Object(target)
owner = targetAccessor.window

primaryInternalLayer = createLayerVariant(owner, target)
primaryAccessor = Object(primaryInternalLayer)

height = probe/get target.height; missing probe => 0
width  = probe/get target.width;  missing probe => 0
primaryAccessor.setSize(width, height)              // status ignored

workInternalLayer = createLayerVariant(owner, target)
workAccessor = Object(workInternalLayer)
workAccessor.setSize(width, height)                 // status ignored
```

primary 成员赋值早于 height/width probe 与第一次 `setSize`，work 成员赋值早于第二次
`setSize`。因此异常/自然失败留下 sticky partial state：

- primary 发布前失败：下次仍会重试；
- primary 发布后任一点失败：下次被 primary-only gate 直接挡回；
- work Void、work 未完整 sizing 或 primary 尺寸不完整都不会自动修复；
- 成功后目标尺寸变化也不会触发重新同步。

iOS armv7 的 `Player_materializeInternalRenderLayers_unwindCleanup_guess` 有 22 个 call-site case，
按构造 prefix 释放局部 Variant/accessor/dispatch owners；它从不清空已经发布的 Player 成员。
Android arm64 的显式 landing-pad 尾链、Android armv7 EHABI metadata 与 iOS arm64 Itanium cleanup
表达同一 source-level 规则：局部临时量逆序清理，persistent publication 不回滚。

## 8. 源码结构恢复

本轮修改如下：

- 在 `RuntimeSupport.h/.cpp` 建立唯一 `detail::createLayerVariant_guess` 声明/定义；
- 删除 `SourceCache.cpp`、`SeparateLayerAdaptor.cpp`、`PlayerRenderTargets.cpp` 三份完全相同的
  file-private factory 复制体；
- SourceCache constructor/load、full-payload SLA resolver、workspace primary/work 全部调用
  公共 helper；
- command-builder composed Layer 改用 owner/parent Variant 调公共 helper，移除旧
  class-dispatch 查询、HRESULT/null 保护和 raw-dispatch 二次包装；后续 V185 又恢复这两个
  Variant 只在各 group Void 门内按需执行 expression/primary GetValue 后物化；
- 新增 `SeparateLayerAdaptor::resolveLayerOrdinal_guess`，复原 Variant-only map move、factory
  miss、absolute/hitThreshold 写入与“不递增 sequence”边界；
- accurate SLA renderer 改调 payload-free resolver，删除该处原生不存在的 payload 构造、
  map payload copy 和 refresh Boolean；每个 admitted item 仍按 native 路径刷新；
- 保留 `createLayerObject` 给尚未在本纵切面完成四端映射的 Web/headless reusable-layer 路径，
  没有把它冒充成同一个 native factory。

编译源码的新注释只描述四参考共同语义，不含绝对地址。

## 9. Recovery IDB 回写

四份 recovery IDB 已完成：

- 三份 standalone helper 重命名为 `Motion_createLayerVariant_guess`；
- 四端 payload-free resolver 重命名为
  `SeparateLayerAdaptor_resolveLayerOrdinal_guess`；
- iOS armv7 两条 cleanup 分别命名为
  `Motion_createLayerVariant_unwindCleanup_guess` 与
  `Player_materializeInternalRenderLayers_unwindCleanup_guess`；
- 在 helper/inline factory、ordinal resolver、workspace materializer 与 SjLj cleanup 加入
  ownership、failure、sequence 和 sticky-publication 注释/书签；
- 四次原位 `idb_save` 均返回 `ok: true`。

## 10. 验证

- 使用现有 motionplayer Emscripten 参数对
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 syntax-only 检查，成功；
- `cmake --build --preset "Web Debug Build"` 重编 35 个步骤并成功链接
  `libmotionplayer.a`、插件库与最终 `index.html/index.wasm`；
- 构建只出现仓库既有 `_tss`、imagepacker attribute、pthread memory-growth、JSPI experimental
  与 Emscripten JS-library warnings，没有本轮新增 error；
- 文档与计划更新后另行执行 `git diff --check` 与 stale-helper scan。

## 11. 后续边界

本纵切面闭合 shared Layer factory、两个 SLA resolver 的区别、accurate 对 payload-free resolver
的使用，以及 workspace materializer 的局部异常/成员提交边界。sticky shared-D3D 的 target
fallback、map cleanup 与 capture 生命周期已在后续纵切面闭合；仍需独立纵切：

- Layer 后端方法抛出后像素缓冲是否保留部分写入；
- 外部脚本 alias 让 workspace/descriptor closure 超过 Player 生命周期时的最终释放点。

这些待办不改变本文件已经由四端共同证明的七调用点 helper 身份、raw owner 边界、map
overload 差异和 primary-only sticky publication。
