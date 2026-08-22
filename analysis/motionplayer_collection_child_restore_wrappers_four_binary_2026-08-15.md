# Collection child restore wrappers 四端复原（2026-08-15）

## 范围与结论

本纵切面闭合 `EmoteEngine` 的五个 controller-collection restore wrapper：Eye、
Eyebrow、Mouth、Transition 和 Selector。重点不是各 controller 字典内部字段，而是外层
Array 的识别、item/accessor 生命周期、label 读取、对应 controller deque 的线性查找、
Variant 传参方式，以及 unknown-label 边界。

五类共同流程是：只接受 native TJS Array；顺序跳过非 Object item；为每个 Object item
建立 retained accessor；用共享 `engineLabel` hint 执行 `TJS_MEMBERMUSTEXIST` label probe，
成功后直接转换并提交为 `ttstr`；在线性 deque 中寻找第一个同名 entry；随即恢复该
controller。没有整批 staging 或回滚，前面 item 已提交的 controller 状态在后面 getter、
转换或 restore 抛异常时保留。

四端共同保留两个不对称边界：

- Eye 是唯一在查找后比较 `end()` 的类别，unknown label 被静默跳过。
- Eyebrow、Mouth、Transition、Selector 都无条件解引用查找结果；unknown label（包括空
  controller deque）进入 native end-iterator undefined behavior，而不是“忽略未知项”。

调用 controller restore 时还有一处容易被统一循环抹掉的 ABI/源结构差异：Eye、Eyebrow
和 Transition 先复制 raw Array-item Variant，再按值传给 controller restore；Mouth 和
Selector 直接把 raw item 借给 const-reference 参数，第一份 Variant copy 发生在被调
controller restore 内部构造 accessor 时。

## 四端入口映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| restore Eye collection | `0x675BE4` | `0x55D398` | `0x1001B16E0` | `0x1B11CC` |
| restore Eyebrow collection | `0x6763D0` | `0x55D7D8` | `0x1001B19A4` | `0x1B1484` |
| restore Mouth collection | `0x676BE4` | `0x55DBF4` | `0x1001B1C68` | `0x1B1734` |
| restore Transition collection | `0x677400` | `0x55E13C` | `0x1001B1F2C` | `0x1B19F0` |
| restore Selector collection | `0x677C48` | `0x55E578` | `0x1001B2218` | `0x1B1CD4` |
| strict named `ttstr` getter | `0x679718` | `0x55F730` | `0x1001B348C` | `0x1B2FB8` |

对应 controller restore 入口为：

| controller | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| Blink / Eye | `0x6613A8` | `0x552820` | `0x1001A2BD8` | `0x1A1DB8` |
| Eyebrow | `0x662C24` | `0x55343C` | `0x1001A3AB8` | `0x1A2E38` |
| Mouth | `0x663588` | `0x553910` | `0x1001A40EC` | `0x1A3504` |
| Var / Transition | `0x664EBC` | `0x554618` | `0x1001A50C0` | `0x1A45EC` |
| Selector | `0x665950` | `0x554C68` | `0x1001A588C` | `0x1A4DD0` |

## 严格 label getter 与共享 hint

四端的 `VariantObject_tryGetStringByName_guess` 具有同一事务边界：

```text
probe = Void Variant
hr = accessor.dispatch.PropGet(TJS_MEMBERMUSTEXIST,
                               name, hint, &probe, accessor.dispatch)
if hr failed:
    destroy probe
    return false                         // destination ttstr unchanged

converted = ttstr(probe)                 // conversion may throw
destination = converted                  // commit only after successful conversion
destroy converted
destroy probe
return true
```

它不是“先读到 caller Variant，再由 wrapper 转成 string”。因此 label getter 的成功提交
目标本来就是 `ttstr`；这也解释了反编译中输出槽只有一个 ref-counted string 指针，而不是
完整 Variant。源码新增对应 helper，并同步修正前一 timeline restore 纵切面，使两条路径
复用相同严格字符串行为。

`label` 字面量和共享 member-hint 地址如下：

| 项 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| UTF-16 `label` | `0x14BF74C` | `0xD772AC` | `0x10195FD00` | `0x1752064` |
| shared hint | `0x1AB4F18` | `0x11114B0` | `0x101B69FC8` | `0x187D9E8` |

同一 hint 还被 controller serializer、metadata builder、timeline 和其它 Engine label
路径复用；这里不能为五个 wrapper 各造独立静态 hint。

## 共同 item 生命周期与可重入边界

五类 wrapper 的四端共同伪代码（保留后述差异）是：

```text
if value.Type != Object: return
items = force Object + query native Array instance
if items == null: return

for rawItem in native Items order:
    if rawItem.Type != Object: continue

    temporary = copy rawItem
    force/check Object
    itemAccessor = AddRef(temporary.Object dispatch)
    destroy temporary before first label probe

    label = empty ttstr
    if !strictGetString(itemAccessor, "label", label, sharedHint):
        continue

    found = linear first-match scan(controllerDeque, entry.label == label)
    apply category-specific end check and Variant passing mode

    destroy label
    Release itemAccessor
```

accessor 活到 controller restore 返回之后，因而 label getter 或 controller getter 可重入时，
item 的 Object dispatch 至少由 wrapper 持有一份引用。与此同时，native 循环仍保留指向
Array `Items` 内部 Variant 的 iterator/reference；脚本 getter 若直接改写同一 Array，可能
使 raw item 或 iterator 失效。四个参考实现没有复制整个 Array，也没有为迭代做防重入快照，
源码保留这一边界。

label 缺失时 strict getter 返回 false 并只跳过当前项；label getter 成功但 string 转换抛出
时，controller 查找尚未开始。重复 label 使用 first match，后续同名 entry 不会被恢复。

## 五类差异矩阵

| 类别 | owner deque / entry | 查找后检查 `end()` | controller 入参 | controller 调用前额外 Variant copy |
|---|---|---|---|---|
| Eye | deque #4, `{ctl,label}` | 是 | by value | 是 |
| Eyebrow | deque #5, `{ctl,label}` | 否 | by value | 是 |
| Mouth | deque #6, `{ctl,label,talkLabel}` | 否 | const reference | 否 |
| Transition | deque #8, `{ctl,label,flag+padding}` | 否 | by value | 是 |
| Selector | deque #9, `{ctl,label,flag+padding,targets vector}` | 否 | const reference | 否 |

Eye 的四端控制流都在 controller 调用前重新计算/构造 deque end iterator 并比较；其余四类
在 linear scan 返回 end 后直接从该 iterator 读 `ctl`。这是源级真实差异，不是某一端
decompiler 丢失条件。Mouth/Selector 则在调用点直接传 `rawItem` 地址；Eye/Eyebrow/
Transition 的调用点可见 `tTJSVariant` copy constructor 和调用后 destructor。

因此 Mouth/Selector 的 controller restore 自身仍会为输入构造 retained accessor，但 outer
wrapper 不先造 by-value 参数副本。Web 源码此前把两者也声明成 by-value，导致额外一次
可观察的 closure AddRef/Release；本次已改回 const-reference 边界。

## 两层 deque ABI

外层 state Array 的 `tTJSArrayNI::Items` 与目标 controller deque 是两套独立 deque。Array
item 块布局与 timeline restore 相同：

| 目标 | Variant 大小 | Array block payload / 元素数 | 实现族 |
|---|---:|---:|---|
| Android ARM64 | 20B | 500B / 25 | 旧 libstdc++，`512 / sizeof(T)` |
| Android ARMv7 | 12B | 504B / 42 | 旧 libstdc++，`512 / sizeof(T)` |
| iOS ARM64 | 20B | 4080B / 204 | libc++，约 4KiB block |
| iOS ARMv7 | 12B | 4092B / 341 | libc++，约 4KiB block |

controller deque 的 record stride / block payload 则由 entry 大小决定：

| 目标 | Eye / Eyebrow | Mouth / Transition | Selector |
|---|---|---|---|
| Android ARM64 | 16B × 32 = 512B | 24B × 21 = 504B | 48B × 10 = 480B |
| Android ARMv7 | 8B × 64 = 512B | 12B × 42 = 504B | 24B × 21 = 504B |
| iOS ARM64 | 16B × 256 = 4096B | 24B × 170 = 4080B | 48B × 85 = 4080B |
| iOS ARMv7 | 8B × 512 = 4096B | 12B × 341 = 4092B | 24B × 170 = 4080B |

这些数字同时验证当前 entry 成员结构：Eye/Eyebrow 是两个 pointer-width 字段；Mouth
多一个 `ttstr`；Transition 的 byte gate 加 padding 后也是三个 pointer-width words；
Selector 再包含一个三指针 vector，总计六个 pointer-width words。源码应继续使用标准
`std::deque`/entry 类型表达共同结构，不把任一 ABI 的 map-node/block 算术硬编码进去。

## 异常、提交与边界行为

- 输入不是 Object，或 Object 不是 native Array：没有 controller 副作用，立即返回。
- 非 Object item：跳过，不构造 accessor。
- Object item 但缺 `label`：strict getter 失败，输出 label 不变，跳过该项。
- label 类型转换、entry 比较、controller restore 任一处抛异常：RAII 释放当前 label、accessor
  和已产生的 by-value Variant；异常继续向顶层 unserialize 传播。
- 已完成的前序 item 不回滚；当前 controller restore 内部也沿用各自纵切面已记录的字段级
  前缀提交行为。
- Eye unknown label：安全跳过。
- 其它四类 unknown label：unchecked end dereference，结果属于 native UB；不能承诺固定
  crash、固定异常或静默返回。
- label getter 可以执行脚本并重入。accessor 固定 Object dispatch 寿命，但不固定原 Array
  的 deque storage，也不固定 engine controller deque；参考实现没有 reentrancy guard。

## 本地修正、IDB 回写与验证

- 五个 wrapper 都恢复 item copy/force/accessor-retain/temporary early-destroy 生命周期，并
  在 label probe 中传入共享 `engineLabelHint_guess`。
- 新增 strict `ttstr` getter，成功转换后才 copy-assign 输出；timeline restore 的 label
  读取同步从中间 Variant 改为同一直接 string helper。
- Eye/Eyebrow/Transition 保持 by-value controller 输入；Mouth/Selector 恢复 const-reference
  输入，去掉 wrapper 层额外 Variant copy。
- Eye 保持 end check；其余四类保留 unchecked end dereference，未用“安全化”分支改变边界。
- 四份 recovery IDB 将 strict string helper 命名为
  `VariantObject_tryGetStringByName_guess`；五个 wrapper、共享 hint 与两层 deque 布局已补注释
  和 bookmark，并已原位保存。
- 真实 response-file `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有 `_tss`
  warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤：重新编译
  `EmoteEngine.cpp`、生成 `libmotionplayer.a` 并成功链接最终 `index.html`；仅有既有
  `_tss`、pthread memory-growth 与 Emscripten/JSPI warnings。
- 定向 `git diff --check` 通过；换行转换提示不属于 whitespace error。

本页只闭合 collection wrapper；Eye/Eyebrow/Mouth/Var/Selector 字典内部字段、request queue
与逐字段异常前缀由 `motionplayer_controller_state_restore_family_four_binary_2026-08-15.md`
单独记录。
