# MotionPlayer NodeTree `ncbPropAccessor` 源码身份与 transform 双读边界（四参考二进制，2026-08-16）

## 结论

四份参考二进制的 NodeTree 路径并不是一组以裸 `iTJSDispatch2 *` 为参数的本地
`dispatchGet/dispatchInt/dispatchBool` 包装器。其源码结构是连续嵌套的
`ncbPropAccessor`：

- `Player::buildNodeTree` 在旧树 teardown 之前，从复制的 motion-content Variant 构造
  一个 accessor；
- 每层 recursive walker 从 raw layers Variant 构造一个 accessor，并在整个本层循环中
  复用；
- 每个 raw layer 另构造一个 accessor，用于 raw-label 和 children；
- `Player_initNodeFields_guess` 又从同一个 raw-layer Variant 构造一个独立 accessor，按
  固定顺序读取全部字段；
- `transformOrder`、type-12 stencil mask list 分别构造自己的 accessor；
- ResourceManager 是刻意不同的一条所有权路径：复制 Variant、`AsObject()`、销毁临时
  Variant，然后以 raw retained dispatch 跨越整层循环和递归回调，不带
  `ncbPropAccessor` vptr。

端口原先把上述 layer/accessor 路径压平成若干 raw-dispatch helper。大多数单次读取结果
相同，但这抹掉了真实对象结构，并且把 `transformOrder[index]` 错写为单次普通
`PropGetByNum`。参考实现调用 `ncbPropAccessor::getIntValue(index, 0)`：先探测，存在时再读
一次；这是 getter 次数、flags、失败处理和最终值都可观察的语义差异。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| node 字段初始化 | `0x6B1058` | `0x580FA4` | `0x100108720` | `0x105E70` |
| recursive walker | `0x6B1E4C` | `0x5818B0` | `0x100109328` | `0x106BDC` |
| 完整 builder | `0x6B25D0` | `0x581CC8` | `0x1001097C8` | `0x107060` |
| indexed `getIntValue` | caller 内联 | `0x4C7834` | `0x100100DF8` | `0xFDF84` |
| indexed `GetValue<tjs_int>` | `0x660B9C` | `0x4C7970` | `0x100069180` | `0xEF730` |
| stencil indexed `GetValue<ttstr>` | `0x621CA8` | `0x52E2C4` | `0x100108690` | `0x105DAC` |

## accessor 的可识别对象形状

`ncbPropAccessor` 因虚析构而是一个 two-word 对象：第一字是 vptr，第二字是持有的
dispatch。三个未完全内联的平台在完整 builder 中都显示相同构造序列：

| 平台 | motion Variant copy | accessor vptr | `AsObject` | temporary Variant dtor |
|---|---:|---:|---:|---:|
| Android armv7 | `0x581CE4` | `0x581CEE` | `0x581CF4` | `0x581CF8` |
| iOS arm64 | `0x1001097F4` | `0x100109804` | `0x100109810` | `0x100109818` |
| iOS armv7 | `0x107086` | `0x1070A8` | `0x1070CE` | `0x1070D2` |

随后才调用旧树 reset，再通过同一个 accessor 读取 `layer`。Android arm64 的优化器将
这个短生命周期中的显式 vptr store 消去，但仍在 `0x6B2608..0x6B2648` 完成同一份
copy/AsObject/temporary-dtor 所有权建立，并在 `0x6B267C` 内联同构的 named Variant
读取。这里的源码身份由另外三端的显式 vptr、四端相同异常清理和后续调用序列共同确认；
不是只凭一个地址猜测。

字段初始化中的 accessor 在四端全部显式存在：

- Android armv7：copy `0x580FC2`，vptr `0x580FCE`，AsObject `0x580FD4`，temporary dtor
  `0x580FD8`；
- iOS arm64：copy `0x100108748`，vptr `0x100108758`，AsObject `0x100108764`，temporary
  dtor `0x10010876C`；
- iOS armv7：copy `0x105E96`，vptr `0x105EBA`，AsObject `0x105EE2`，temporary dtor
  `0x105EE6`；
- Android arm64 在 `0x6B108C` 起复制 layer Variant，并在紧随的构造区写
  `ncbPropAccessor` vptr/dispatch；所有后续 named helper 都接收该栈对象地址。

recursive walker 也不是借用裸 layers dispatch。显式 vptr store 分别位于
`0x6B1EA0`、`0x5818D6`、`0x100109368`、`0x106C26`；count getter 接着在
`0x6B1EE8`、`0x5818EA`、`0x100109388`、`0x106C60` 读取 `"count"`。每个 raw layer
又在 `0x6B202C`、`0x5819B8`、`0x10010945C`、`0x106D0E` 建立独立 accessor。

## named/indexed 模板身份

字段 helper 的 ABI 形状与 `ncbind.hpp` 模板完全对应：self 不是 dispatch 本身，而是
`{vptr, _obj}` accessor；virtual call 从 `self->_obj` 发出，并把同一个 `_obj` 同时作为
receiver 与 `objthis`。临时 Variant 在转换后析构，PropGet HRESULT 不参与
`GetValue<T>` 的返回决策。

本轮把四库中旧的 `VariantObject_*` 假名更新为实际源码身份：

- `ncbPropAccessor_GetArrayCount_guess`；
- `ncbPropAccessor_GetValueNamedString_guess`；
- `ncbPropAccessor_GetValueNamedBool_guess`；
- `ncbPropAccessor_GetValueNamedReal_guess`；
- `ncbPropAccessor_GetValueArrayVariant_guess`（ARM64 对应 caller 内联）；
- `ncbPropAccessor_GetValueArrayInteger_NodeTreeEmote_guess`；
- `ncbPropAccessor_GetValueArrayString_NodeTree_guess`；
- `ncbPropAccessor_getIntValueArray_NodeTree_guess`（ARM64 caller 内联）。

未知的上层函数名仍保留 `_guess`。2026-08-16 后续四端纵切面证明同一个 emitted clone 还
服务 Eye/Blink 与 Eyebrow 构造器，因此后缀扩为 `NodeTreeEmote`；它只用于区分同模板在
其他翻译单元产生的重复实例，不表示源码中存在这个后缀。续证见
`analysis/motionplayer_emote_controller_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

## `transformOrder` 的准确双读

共同源码等价于：

```text
transformValue = layer.GetValue<tTJSVariant>("transformOrder", hint)
transform = ncbPropAccessor(transformValue)
for index = 0..3:
    node.transformOrder[index] = transform.getIntValue(index, 0)

getIntValue(index, defaultValue):
    probe = Void
    status = _obj.PropGetByNum(
        TJS_MEMBERMUSTEXIST, index, &probe, _obj)
    destroy(probe)
    if status < 0:
        return defaultValue
    value = Void
    _obj.PropGetByNum(0, index, &value, _obj)  // status ignored
    result = value.AsInteger()
    destroy(value)
    return result
```

四端 caller 边界：

| 平台 | transform loop/calls |
|---|---|
| Android arm64 | probes `0x6B142C/0x6B1480/0x6B14D4/...`；成功分支第二读 `0x6B144C/0x6B14A0/0x6B14F4/0x6B1548` |
| Android armv7 | loop call `0x58117E` → helper `0x4C7834` |
| iOS arm64 | loop call `0x100108988` → helper `0x100100DF8` |
| iOS armv7 | loop call `0x10612E` → helper `0xFDF84` |

三个 standalone helper 都直接调用 `HasValueByNum`；成功时尾调/调用 indexed integer
`GetValue`，失败时返回第三参数 default。Android arm64 将 `getIntValue` 展开到 caller，
但保留完全相同的分支：负状态写 0，非负状态执行第二次普通读取。由此得到四个不能被
“一次读取后顺手复用 probe 值”替代的边界：

1. `TJS_S_TRUE` 等任意非负状态都算存在，不要求精确 `TJS_S_OK`；
2. 存在项的自定义 getter 被调用两次，第一次 Variant 被销毁而不转换；
3. 第二次 getter 的 HRESULT 被忽略，只要它写了 Variant，该值就参与转换；
4. 缺项只调用一次并返回调用方提供的 0。

## recursive walker 与 ResourceManager 的类型分界

准确的对象图是：

```text
layersAccessor (owns layers dispatch for this recursion)
├─ rawLayer Variant
│  ├─ layerAccessor (raw label + children; owns one dispatch ref)
│  └─ initNodeFields
│     └─ second layerAccessor (all node fields; owns another dispatch ref)
└─ resourceManagerDispatchOwner (raw retained dispatch, no accessor vptr)
   ├─ requireLayerId #1
   ├─ requireLayerId #2
   └─ remains alive across getters and recursive child build
```

`layersAccessor.GetArrayCount()` 仍然是 `PropGet("count")`，不是 dispatch 的虚
`GetCount()`。即使 count 为 0，ResourceManager raw owner 也在 count 读取之后建立，并于
本递归层退出/异常展开时释放。恢复 accessor 不能顺手把 ResourceManager 改成 accessor，
两条来源的对象布局和销毁路径在四端都不同。

raw-label map 使用 `layerAccessor.GetValue<ttstr>("label")`；节点初始化再通过第二个
accessor 独立读取 `label`，所以 side-effect getter 可以产生不同字符串。children 使用
named `GetValue<tTJSVariant>`，下一递归层再从返回 Variant 构造自己的 accessor。

## stencil post-pass

type-12 且 `stencilType & 4` 的节点从保存的 mask-list Variant 构造 accessor，调用
`GetArrayCount()`，随后每个索引通过 `GetValue<ttstr>` 直接转换，不是先返回一个长期保存的
raw Variant 再在调用方模拟转换。四端 indexed string helper 见首表。

查找仍然使用 raw-label map 且 `recursive=false`。只有 type 0/type 3 目标会被追加到
`stencilCompositeMaskNodes`，并在目标上写 referenced 标志。

## 源码修正

`cpp/plugins/motionplayer/NodeTree.cpp`：

- 删除 plugin-local `dispatchGet_guess`、`dispatchTryGet_guess`、`dispatchGetByNum_guess`、
  `dispatchCount_guess`、`dispatchInt_guess`、`dispatchReal_guess`、`dispatchBool_guess`；
- motion layer、layers、transform 和 mask-list 全部改用真实 `ncbPropAccessor` API；
- optional 字段恢复 `HasValue` + typed `GetValue`；
- scalar/Variant/string 字段恢复相应 `GetValue<T>` 模板；
- transform 四项恢复 `getIntValue(index, 0)` 双读；
- recursive ResourceManager 继续使用独立 raw retained-dispatch owner。

`cpp/plugins/motionplayer/PlayerMotionLoad.cpp`、`NodeTree.h`、`Player.h`：

- build 入口在 reset 前从复制的 `_motionContentVariant` 构造 accessor；
- detail builder 接收 accessor 引用，避免 reset 后重新取 Player 字段，也避免额外构造一个
  非参考实现的 owner。

`tests/unit-tests/plugins/motionplayer-dll.cpp`：

- 真实 NodeTree 构建中的 transform probe 对索引 0 返回 `TJS_S_TRUE`，验证非零成功状态；
- 索引 2 返回 `TJS_E_MEMBERNOTFOUND`，验证单读和 default 0；
- 其余存在项第二读返回 `TJS_E_FAIL` 但写入 700+index，验证 HRESULT 被忽略且第二值胜出；
- 完整 flags/index 序列固定为
  `{MUST,0, MUST,0, MUST, MUST,0}` / `{0,0,1,1,2,3,3}`；
- 每次 receiver/objthis 都必须是同一个 transform dispatch；最终 node transform 为
  `{700, 701, 0, 703}`。

## IDB 更新

四份 recovery IDB 已完成：

- 上述 accessor helper 语义重命名与强制 readback；
- 三个 standalone `getIntValue` helper 的三参数 prototype；
- node initializer、recursive walker、完整 builder、transform callsite 和 stencil indexed
  string helper 的 source-identity/owner-scope 注释；
- 每个平台 node initializer 的 V130 bookmark；
- 原位保存到 `out/ida-recovery/` 对应目录。

## 验证

- ordinary Emscripten 单元测试翻译单元 syntax-only：通过；
- `KRKR2_WASMTIME_HEADLESS=1` 同一翻译单元 syntax-only：通过；
- Web Debug 完整增量构建：通过，复跑为 `ninja: no work to do.`；
- Wasmtime Headless Debug 完整增量构建：通过，复跑为 `ninja: no work to do.`；
- 警告仅为项目既有 `_tss`、`imagepacker.h` `nodiscard` 与 Emscripten 链接警告。
