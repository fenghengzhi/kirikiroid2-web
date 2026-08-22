# MotionPlayer layer query、`onFindMotion` 与 shape-anchor 四端生命周期对照（2026-08-12）

## 1. 结论

本轮对 `reference/binaries/` 中四个当前参考二进制重新定位、重新反编译并核对 NCB
参数转换器后，四个 script-facing API 的源级签名可以收敛为：

```cpp
tTJSVariant Player::getLayerMotion(tTJSVariant name);
tTJSVariant Player::getLayerGetter(ttstr name);
tTJSVariant Player::getLayerGetterList();
tTJSVariant Player::onFindMotion(tTJSVariant request);
```

其中两个看似可能是 `const &` 的参数实际都是值传递：`getLayerMotion`/`onFindMotion`
共享按值构造完整 `tTJSVariant` 临时量的 NCB 特化，`getLayerGetter` 使用按值构造完整
`ttstr` 临时量的另一特化。ARM64 的 invisible-reference ABI lowering 只改变机器级传法，
不改变源级值语义。

语义上，`getLayerMotion` 返回节点保存的 child-player Variant；`getLayerGetter` 返回借用
原始 `MotionNode *` 的 live facade；`getLayerGetterList` 每次新建 Array，按本 Player 的扁平
节点 deque 顺序枚举全部非根节点；默认 `onFindMotion` 只是把输入 Variant 原样复制返回，
不是内部播放入口。

物理 shape-anchor helper 只调用 `getLayerGetter`，再读 facade 的 `shape`。旧本地实现调用
`getLayerMotion` 后尝试读取 `.shape`，与四端数据类型都矛盾，因为 `getLayerMotion` 返回的
是 child-player Variant。

## 2. 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `Player_getLayerGetter_guess` | `0x6D0CD4` / `0xA4` | `0x595EF4` / `0x2C` | `0x100121D64` / `0x4C` | `0x120B2C` / `0x2C` |
| `Player_getLayerMotion_guess` | `0x6D0D78` / `0xB4` | `0x595F74` / `0x60` | `0x100121E38` / `0x64` | `0x120C2C` / `0xA0` |
| `Player_getLayerGetterList_guess` | `0x6D2368` / `0x1DC` | `0x596CD4` / `0xD2` | `0x100122DC0` / `0xCC` | `0x121E18` / `0xF6` |
| `Player_onFindMotion_default_guess` | `0x6D6E40` / `0x8` | `0x599152` / `0xC` | `0x10012596C` / `0x8` | `0x124B6C` / `0xC` |
| `EmoteEngine_resolveShapeAnchor_guess` | `0x678D50` / `0x378` | `0x55F098` / `0x192` | `0x1001B2C60` / `0x214` | `0x1B2774` / `0x238` |

表中每格为“入口 / IDA 函数大小”。最终写入四份 IDB 的归一化 prototype 为：

```text
tTJSVariant_guess Player_getLayerGetter_guess(void *self, ttstr_guess label)
tTJSVariant_guess Player_getLayerMotion_guess(void *self,
                                               tTJSVariant_guess name)
tTJSVariant_guess Player_getLayerGetterList_guess(void *self)
tTJSVariant_guess Player_onFindMotion_default_guess(
    void *self, tTJSVariant_guess request)
bool EmoteEngine_resolveShapeAnchor_guess(
    void *self, const ttstr_guess *label, float *outX, float *outY)
```

`ttstr_guess *` 在最后一个机器级 prototype 中表示源级 `const ttstr&`；第 7 节给出调用点
直接传字段地址的证据。

## 3. 注册表与宽字符串证据

四个成员名在各端的 UTF-16LE 字面量位置为：

| 名称 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `getLayerMotion` | `0x14D664E` | `0xD85F10` | `0x10195CF3C` | `0x174F2A0` |
| `getLayerGetter` | `0x14D666C` | `0xD85F2E` | `0x10195CF5A` | `0x174F2BE` |
| `getLayerGetterList` | `0x14D668A` | `0xD85F4C` | `0x10195CF78` | `0x174F2DC` |
| `onFindMotion` | `0x14D5CC4` | `0xD8572C` | `0x10195C1CC` | `0x174E530` |

普通文本搜索对这些 TJS 宽字面量返回空；按 UTF-16LE bytes 搜索后可跨四库稳定定位。
ARMv7/iOS 的成员描述符可以直接看到 callback 与 NCB helper：

- Android ARMv7：`0x598782` -> `getLayerMotion`，`0x598798` ->
  `getLayerGetter`，`0x5987AE` -> `getLayerGetterList`，`0x59881C` ->
  `onFindMotion`；
- iOS ARM64：`0x1001251EC`、`0x10012520C`、`0x10012522C`、
  `0x1001252CC`；
- iOS ARMv7：`0x12442E`、`0x12444C`、`0x12446A`、`0x124500`。

Android ARM64 把描述符构造大幅内联；相邻 store 仍分别落到 `getLayerMotion`、
`getLayerGetter`、list callback 和 `Player_onFindMotion_default_guess`，其后才是
`isExistMotion` 描述符。四端成员顺序与 callback 身份一致。

## 4. NCB 参数类型：为什么不是 `const &`

### 4.1 `tTJSVariant` 值传递

Android ARMv7 的 `getLayerMotion` 和 `onFindMotion` 注册项共享 helper `sub_5B49B4`：

```text
sub_5B49B4
  -> sub_5B49E8
    -> sub_5B4A24              构造 adaptor，安装 off_10BCD98
       vtable FuncCall = sub_5B4A8C
         inner invoke = sub_5B4B4C
           converter = sub_5B4C00
```

`sub_5B4C00` 从脚本参数构造完整 12-byte `tTJSVariant` 临时量；member pointer 以该对象
作为值参数调用，回调完成后 `sub_760238` 析构临时量。若参数是
`const tTJSVariant&`，ncbind 的 `TypeWrap<const T&>` 会走指针转换，不会拥有这个完整临时
对象。因此这里是值特化。

### 4.2 `ttstr` 值传递

`getLayerGetter` 使用另一条 helper 链：

```text
sub_5B375C
  -> sub_5B3790
    -> sub_5B37CC              安装 off_10BC9A8
       vtable FuncCall = sub_5B3834
         inner invoke = sub_5B38F4
           converter = sub_5B39A8
```

`sub_5B39A8` 把 Variant 转成完整 `ttstr` 临时量，调用 member pointer 后由
`sub_48E9E6` 析构。结合仓库内 ncbind `paramsFunctor::TypeWrap<T>` 的值类型规则，源级
签名只能是 `ttstr` by value。

## 5. 四个 API 的共同伪代码

### 5.1 `getLayerMotion`

```text
getLayerMotion(player, Variant name by value) -> Variant:
  node = null
  {
    ttstr label(name)
    node = findNodeByRawLabel(player, label, recursive=true)
  } // label 在这里析构

  if node == null:
    return Void
  return copy(node.childPlayerVariant)
```

关键顺序是：Variant 到字符串的转换临时量在 child-player Variant CopyRef 之前析构。
命中节点时不检查 node type，也不要求 child Variant 为 Object；节点里是 integer、Void 或
其他 Variant 时也照样复制。未命中返回 Void。

### 5.2 `getLayerGetter`

```text
getLayerGetter(player, ttstr label by value) -> Variant:
  node = findNodeByRawLabel(player, label, recursive=true)
  if node == null:
    return Void

  wrapper = new LayerGetter(node)       // 唯一字段是 MotionNode *
  dispatch = NCB_CreateAdaptor(wrapper)
  if dispatch == null:
    return Void                         // wrapper 不 delete，保留泄漏边界

  result = ObjectVariant(dispatch)
  dispatch.Release()                    // Variant 保留正常引用
  return result
```

`LayerGetter`/adaptor 不拥有 `MotionNode`。所有属性访问都在访问时重新读这个 raw pointer，
所以 facade 能观察节点后续变化；反过来，节点树重建或 Player 析构后继续使用旧 facade 的
悬空风险也是原始生命周期边界，不能用 shared ownership 静默改变。

### 5.3 `getLayerGetterList`

```text
getLayerGetterList(player) -> Variant:
  result = new TJS Array
  for nodeIndex = 1 .. flatNodeDeque.size - 1:
    result.push(buildLayerGetterVariant(flatNodeDeque[nodeIndex]))
  return result
```

每次调用都创建不同 Array；只枚举当前 Player 的扁平 deque，不递归 child Player；跳过
constructor root；按稳定节点顺序输出；重复 label 不折叠。某一项 adaptor 构造失败时把
Void 元素 append 到原位置，不过滤也不中止。

### 5.4 默认 `onFindMotion`

```text
onFindMotion(player, Variant request by value) -> Variant:
  return copy(request)
```

四端 body 只有 Variant copy-return。它的用途是 script override hook：motion property/load
路径通过当前 TJS dispatch 调 `objthis.onFindMotion(request)`，默认实现保持 request Object
身份和内容。内部 child/particle 播放路径直接进入共同 play state machine，不调用这个默认
identity member。

## 6. 查找范围与容器边界

`getLayerMotion` 与 `getLayerGetter` 共享 recursive raw-label lookup：先查当前 Player 的有序
`ttstr -> flat node index` map，当前层未命中后按 child-player visitor 顺序递归。首次命中
停止。它们不使用 hierarchical per-node path map。

list API 则完全不使用 label map：它线性遍历本 Player 的 flat node deque。因此：

- label map 的重复键会指向一个最终节点，但 list 仍为每个节点生成 facade；
- recursive 单项查询可以命中 child Player，list 不会把 child 的节点摊平；
- list 返回顺序由 node-build deque 顺序决定，不是 label map 的字典序。

## 7. shape-anchor 调用链与 `const ttstr&` 边界

resolver 只有两类调用者：hair/parts spring pass 与 bust-chain pass。

| 目标 | hair/parts caller / call | bust caller / call |
| --- | ---: | ---: |
| Android ARM64 | `0x678B28` / `0x678BE4` | `0x6790C8` / `0x679278` |
| Android ARMv7 | `0x55EE98` / `0x55EF18` | `0x55F2F4` / `0x55F382` |
| iOS ARM64 | `0x1001B29D0` / `0x1001B2AD8` | `0x1001B2F2C` / `0x1001B3048` |
| iOS ARMv7 | `0x1B24D8` / `0x1B25BC` | `0x1B2ABC` / `0x1B2B84` |

八个调用点全部直接把 deque entry 内嵌 label 字段地址放入第二参数寄存器：64-bit
entry 为 `entry + 12`，32-bit entry 为 `entry + 8`。调用前没有 label CopyRef/AddRef，调用
后没有临时 ttstr destructor，所以 resolver 的源级参数是 `const ttstr&`。resolver 内部再
调用按值 `getLayerGetter` 时才产生那一次必要字符串副本。

共同伪代码为：

```text
resolveShapeAnchor(engine, const ttstr& label, float *outX, float *outY):
  getter = engine.player.getLayerGetter(copy(label))
  if getter is not Object: return false

  shape = getter.shape
  if shape is not Object: return false
  if int(shape.type) != 0: return false

  shapeX = real(shape.x)
  shapeY = real(shape.y)
  rootX = engine.player.x
  rootY = engine.player.y
  ratio = engine.meshDivisionRatioDup

  *outX = rootY + (shapeY - rootY) * ratio
  *outY = rootX + (shapeX - rootX) * ratio
  return true
```

所有 false 路径都不写任一输出。成功路径故意交叉 X/Y：第一个输出使用 rootY/shapeY，
第二个输出使用 rootX/shapeX。只有 shape type 0（Point）被接受。

## 8. 本地差异与恢复

本轮修复以下结构性偏差：

1. `Player` 四个成员签名改成 NCB 证明的值参数；`onFindMotion` 用 const-lvalue
   copy-return，防止 C++ 从 by-value 参数隐式 move 而改变 native CopyRef 边界；
2. `getLayerMotion` 显式缩短本地 `ttstr` scope，使其析构先于 child Variant 返回拷贝；
3. `getLayerGetterList` 恢复 flat deque `[1,end)` 顺序、重复保留和 Void item 边界；
4. child-motion 与 particle child 的旧代码不再把 `onFindMotion` 当播放 helper，改为直接
   进入共享 play state machine；
5. shape-anchor 从错误的 `getLayerMotion` 改为 `getLayerGetter`，并恢复为从 Engine 自身
   取得 Player 的四参数 member helper；hair/bust caller 直接传 entry label 引用；
6. 补充成功、miss、fresh Array、默认 identity、Variant-to-ttstr 转换、非 Point 失败且输出
   保持不变，以及 crossed anchor 的 Catch 回归；
7. 删除本纵向依赖旧单一 `libkrkr2.so` 地址身份的源码注释，地址表只保留在本文。

## 9. IDB 改进

四份 IDB 均完成并保存：

- 定义 ABI 对应的 `tTJSVariant_guess` 与 `ttstr_guess`；
- 重命名并设置第 2 节五个函数的 prototype；
- 为 layer-query 函数补入临时量、返回拷贝、raw-node 借用、Array 顺序与 failure leak 注释；
- 为 resolver 补入 `getLayerGetter`、Point-only、crossed outputs 和失败不写输出注释；
- 在八个 caller 中标记 resolver 调用，并在 resolver 函数注释中记录 direct embedded-label
  `const ttstr&` 证据；
- force recompile 后重新检查 typed pseudocode，最后四库 `idb_save` 均返回 `ok=true`。

## 10. 验证

- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用当前 Web Debug 的真实
  Emscripten 参数执行 `-fsyntax-only` 成功，仅报告仓库既有 `_tss` 弃用警告；
- Web Debug 与 Wasmtime Debug 的 `motionplayer` 静态库均重新编译并链接成功；
- Web Debug 完整目标成功链接 `index.html`/Wasm 并同步 shell 预分配内存；
- Wasmtime Debug `krkr2_wasmtime_guest` 成功重编 guest objects、链接 wasm 并完成
  exnref exception 转换；
- 两个完整目标与两个静态库目标的第二次增量构建均为
  `ninja: no work to do.`；
- 构建输出中的 `_tss`、imagepacker `nodiscard`、pthread/memory-growth、JSPI 与 JS
  library 信息均为仓库既有 warning；
- 当前环境没有可直接运行该 Catch 翻译单元的 native 测试目标，因此上述回归只报告为
  已进入完整翻译单元并通过编译，不声称运行时执行。
