# Motion.ObjSource 完整 NCB 注册面、双构造路径与 facade 边界四参考审计（2026-08-14）

## 结论

四份当前参考二进制共同给出同一份 `Motion.ObjSource` 发布面：

- 一个零参数 generated typed constructor；
- 随后恰好六个成员，顺序为 `originX`、`originY`、`width`、`height`、`clip`、
  `drawLayer`；
- 前五项全是 getter-only typed property，最后一项是 ordinary typed method；
- 没有常量、setter、raw callback 或 constructor overload；
- 脚本 constructor 对所有非负 surplus 参数全部忽略，实际执行 `new ObjSource()`；
- 默认 native facade 的 PSB owner、raw node 和 lazy texture 三个 pointer slot 都为 null；
- 一个 Void 参数仍是 ncbind 的空-adaptor shell sentinel，不分配 native record；
- `ObjSource(const PSBRawNode&)` 是 `ResourceManager::findSource` 的 native factory 路径，
  不是脚本 overload；它复制/retain PSB owner、借用 owner 内 raw node，并把 texture 置 null；
- 默认 constructor attach 失败会执行 native destructor + free；而 `findSource` 已构造 native 后
  若 `CreateAdaptor` 正常返回 null，caller 不回收 native。这两条失败边界不能合并；
- native/adaptor destruction 先释放 retained texture，再释放 PSB owner，最后 free facade。

现有 `main.cpp` 的六项注册顺序和 `SourceCache.h/.cpp` 的 native 实现与四端证据一致，
本纵切面没有要求新的 production 语义修改。本轮新增真实 class-object 回归，覆盖零参数 native
publication、单 Void shell、surplus ignore、read-only descriptor、默认 width/height/clip 和
non-dictionary `drawLayer` 早退。

## 四端 registrar 与 constructor 链映射

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `ObjSource` member registrar | `0x69A098` | `0x575028` | `0x1000F8D30` | `0xF5C48` |
| registrar 大小 | `0x35C` | `0x9C` | `0x108` | `0xDC` |
| constructor descriptor register | registrar inline | `0x5750F4` | `0x1000F8E38` | `0xF5D24` |
| constructor Function factory | registrar inline | `0x5A1C68` | `0x1001326A4` | `0x1316A0` |
| constructor descriptor install | registrar inline | `0x5A1CC4` | `0x10013272C` | `0x13179C` |
| constructor `FuncCall` | `0x6E1208` | `0x5A1DAC` | `0x10013287C` | `0x131908` |
| allocate + adaptor attach | `0x6E12DC` | `0x5A1E3C` | `0x10013291C` | `0x131974` |
| native destructor | `0x6E145C` | `0x5A1EE8` | `0x100132A60` | `0x131AF8` |
| native allocation size | `0x18` | `0x0C` | `0x18` | `0x0C` |

Android ARM64 把 `0x38` 字节 constructor Function descriptor 的 factory/install 展开进
registrar；Android ARMv7/iOS ARMv7 factory 分配 `0x20`，iOS ARM64 分配 `0x38`。这些是
NCB Function dispatch object 尺寸，不是 ObjSource native record 尺寸。

constructor outer Function vtable 与 `FuncCall` 槽为：

| ABI | outer vptr | `FuncCall` slot | slot target |
|---|---:|---:|---:|
| Android ARM64 | `0x1A19328` | `0x1A19338` | `0x6E1208` |
| Android ARMv7 | `0x10BA3A0` | `0x10BA3A8` | Thumb `0x5A1DAD` |
| iOS ARM64 | `0x101AE1420` | `0x101AE1430` | `0x10013287C` |
| iOS ARMv7 | `0x18326A0` | `0x18326A8` | Thumb `0x131909` |

两个 32 位表中最低位 `1` 是 Thumb ISA tag；本文函数地址均使用去 tag 后的偶数地址。

## 精确六项发布顺序与 native target

| # | 脚本名 | descriptor | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---|---|---:|---:|---:|---:|
| 1 | `originX` | typed RO property, `tjs_int` | `0x69A3F4` | `0x57511C` | `0x1000F8E88` | `0xF5D4C` |
| 2 | `originY` | typed RO property, `tjs_int` | `0x69A4B8` | `0x575180` | `0x1000F8EEC` | `0xF5E04` |
| 3 | `width` | typed RO property, `tjs_int` | `0x69A57C` | `0x5751E4` | `0x1000F8F50` | `0xF5EBC` |
| 4 | `height` | typed RO property, `tjs_int` | `0x69A65C` | `0x575258` | `0x1000F8FD0` | `0xF5F8C` |
| 5 | `clip` | typed RO property, `Variant` | `0x69A73C` | `0x5752CC` | `0x1000F9050` | `0xF605C` |
| 6 | `drawLayer` | typed method `(Variant)->void` | `0x69AAB8` | `0x5754E4` | `0x1000F930C` | `0xF63C0` |

五个 property descriptor 的 setter/member-adjustment slots 四端都为零。`drawLayer` method
descriptor 的 member adjustment 也为零；ObjSource 没有多重继承所需的 this correction。

部分 recovery decompile 把 UTF-16 名称误渲染成一字符 ASCII：iOS registrar 中可见伪
`"o"/"c"`，Android ARM64 尾项甚至显示伪 `"d"`。这不是原版短别名。对四份输入分别执行
UTF-16LE byte search，完整字符串均唯一命中：

| name | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `originX` | `0x14D5318` | `0xD84EC0` | `0x10195B5F2` | `0x174D956` |
| `drawLayer` | `0x14D540A` | `0xD84FBE` | `0x10195B74C` | `0x174DAB0` |
| `ObjSource` | `0x14D69DA` | `0x599474` | `0x10195D366` | `0x174F6CA` |

完整 byte string、registrar 内连续六次 descriptor install、native target 和本地 NCB macro
共同消除了 IDA string-rendering 噪声。不能把伪代码中的单字母提升成脚本 API。

## Zero-argument constructor 的精确脚本边界

四端 constructor `FuncCall` 共同为：

```text
if membername != null:
    return TJS_E_MEMBERNOTFOUND       // -1001; result untouched

if numparams == 1 && param[0].Type == Void:
    return TJS_S_OK                   // adaptor shell; result untouched

if result != null:
    result.Clear()

if numparams < 0:
    return TJS_E_BADPARAMCOUNT        // -1004

native = new ObjSource()              // does not inspect params

if objthis/adaptor attach fails:
    native.~ObjSource()
    operator delete(native)
    return TJS_E_NATIVECLASSCRASH     // -1008

return TJS_S_OK                       // result remains Void
```

observable 结果为：

- `ObjSource()` 正常创建带 native record 的 script instance；
- `ObjSource(void)` 是空 adaptor shell，不创建 native；
- 任意一个或多个普通 surplus 参数全部忽略，不触发 Variant conversion，也不会构造
  `PSBRawNode`；
- generated lower-bound 仍存在，但 requiredCount 为零，因此只有内部负数 `numparams` 才返回
  BADPARAMCOUNT；
- membername 和 Void sentinel 分支都早于 result clear；普通路径先清 result；
- bridge 没有 upfront `objthis` null/adaptor gate。它先完成 native allocation/default
  construction，attach 阶段失败才析构/free；
- attach 成功只把 native pointer 写入 adaptor metadata，不把 native pointer写入 result。

这与 `ObjSource(const PSBRawNode&)` 的存在不矛盾：该 constructor 没有任何 NCB descriptor、
Function vtable target 或 bridge xref，是 C++ `ResourceManager::findSource` 的专用 native path。

## 默认 native record 与对象布局

四端 default allocation helper 直接证明自然布局：

| source member/slot | 64-bit ABIs | 32-bit ABIs |
|---|---:|---:|
| `_source.owner` | `+0x00`, pointer | `+0x00`, pointer |
| `_source.node` | `+0x08`, pointer | `+0x04`, pointer |
| `_texture` | `+0x10`, pointer | `+0x08`, pointer |
| total | `0x18` | `0x0C` |

allocation helper 在查询 script native-instance metadata 之前就把三个 slot 全部写零。成功时，
native pointer 写入 64 位 adaptor `+8` 或 32 位 adaptor `+4`。metadata query 要求：

1. `objthis` 非 null；
2. `NativeInstanceSupport(TJS_NIS_GETINSTANCE, ObjSourceClassId, &adaptor)` 成功；
3. 返回 adaptor 非 null。

任一条件失败，helper 都执行完整 `ObjSource` destructor、`operator delete` 并返回 `-1008`。
空 record 的 destructor 不调用 texture Release，也没有 PSB owner refcount 需要降低。

本地 `ObjSource() = default` 是类内首声明，`PSBRawNode` 默认构造 owner/node 为 null，
`_texture = nullptr` 是显式 default member initializer；它与四端 `new ObjSource()` 三槽零状态
一致。

## `ResourceManager::findSource` 的 PSBRawNode constructor 路径

source-hit path 在严格导航 `root["source"][group]["icon"][icon]` 后执行：

```text
native = operator new(sizeof(ObjSource))
native.source.owner = iconEntry.owner
if owner != null:
    ++owner.refcount
native.source.node = iconEntry.node
native.texture = null
dispatch = ObjSourceAdaptor::CreateAdaptor(native, sticky=false, throw=false)
```

对应 allocation/publication 点为：

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `ResourceManager::findSource` | `0x6A7F1C` | `0x57BDE0` | `0x100102594` | `0xFF890` |
| source-hit native allocation | `0x6A83A0` | `0x57BED0` | `0x10010270C` | `0xFFA44` |
| `CreateAdaptor` helper | `0x6E9504` | `0x5A7A04` | `0x10013A190` | `0x13A274` |

`source.node` 是 owner 内存中的 borrowed raw address；owner refcount 保证该地址的存储寿命。
texture 由 ObjSource 自己持有一次 retained reference，初始为 null，首次 dictionary
`drawLayer` 才 materialize。

需要保持的异常/失败差异：

- NCB script constructor 的 allocate/attach helper在 attach 失败时明确 dtor+delete；
- `findSource` 构造 native 后调用 `CreateAdaptor`。若 helper **正常返回 null**，caller 只返回
  Void 并清理栈临时，不 dtor/delete刚分配的 facade，因此泄漏 native、PSB owner retain；
- 不能用 `unique_ptr` 临时把该 `findSource` null-return branch“修好”，否则会改变参考边界；
- 正常 `CreateAdaptor(..., sticky=false)` 后，script adaptor 成为 native record 的 owner。

## 五个 read-only property 的数据流与边界

### `originX` / `originY`

两者四端都没有 source-category guard：

```text
temporary = source.GetDictionaryValueStrict("originX" or "originY")
result = temporary.GetInt()
destroy temporary raw-node owner
```

因此默认空 ObjSource 或任意非-dictionary source 不返回 `0/32/Void`，而是进入 strict raw
lookup 的失败路径；dictionary 缺键同样不提供默认。返回为 ordinary 32-bit `tjs_int`，没有
clamp 或浮点中转。

### `width` / `height`

四端共同为：

```text
if source.GetTypeCategory() != 7:
    return 32
return source.GetDictionaryValueStrict("width" or "height").GetInt()
```

默认 `32` 只属于“source 不是 dictionary”。一旦 category 为 7，missing key 仍由 strict
getter失败；不能写成 `Get(key, default=32)`。

### `clip`

四端共同顺序为：

```text
clip = empty raw-node owner
if source.category != 7 || !source.TryGet("clip", clip):
    return Void

dictionary = new property object
dictionary["left"]   = clip.Strict("left").GetDouble()
dictionary["top"]    = clip.Strict("top").GetDouble()
dictionary["right"]  = clip.Strict("right").GetDouble()
dictionary["bottom"] = clip.Strict("bottom").GetDouble()
return owning object closure(dictionary, dictionary)
```

四次 property set 都使用 `TJS_MEMBERENSURE` (`0x200`) 和各自静态 member hint，顺序固定
`left -> top -> right -> bottom`。只有 outer `clip` lookup 是 optional；clip object存在后，
四个 child 都是 strict read，任一缺失不会降级成 Void/0。每次成功 getter 都新建一个
dictionary dispatch，并不是对 raw PSB node 的 live proxy。

## `drawLayer` 早退与 retained texture publication

四端 native target 首先检查 `source.GetTypeCategory()`：

```text
if source.category != 7:
    return
ensureTexture()
layer = strict target-Layer native conversion
layer.AssignTexture(texture)
layer.SetSize(texture.width, texture.height)
```

因此脚本默认 ObjSource 的 `drawLayer(任意值)` 在 target Variant conversion 之前就成功早退。
dictionary path 则不会把无效 target 静默忽略。

lazy materialization 会把 `CreateTexture2D(bitmap)` 的 retained return **直接发布**到
`_texture`，再释放 bitmap 和 BGRA 临时量；没有临时 texture smart owner 或尾部 commit。
后续 AssignTexture/SetSize 失败不回滚 `_texture`。`CreateTexture2D` 正常返回 null 时，null
也原样写入，随后 draw path 自然解引用；没有 null guard。

pixel/RL/palette 的完整数据流与异常矩阵不在本 NCB surface 文件重复展开；相邻 texture-owner
纵切面已闭合 retained slot、publish timing 和明确失败边界。

## Adaptor 与 native destruction

adaptor 条件销毁点：

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| class registration wrapper | `0x6FB9F0` | `0x59977C` | `0x1001260DC` | `0x125194` |
| adaptor conditional destroy | `0x6FBD70` | `0x5B6EA8` | `0x10014E588` | `0x150350` |

adaptor 关键布局为 64 位 `native@+8, sticky@+16`，32 位
`native@+4, sticky@+8`。四端 cleanup 等价于：

```text
native = adaptor.native
if native != null && adaptor.sticky == false:
    native.~ObjSource()
    operator delete(native)
adaptor.native = null
adaptor.sticky = false
```

sticky adaptor 不删除外部 C++ owner 持有的 native，但仍断开 pointer并清 sticky state。
普通 script constructor 与 `findSource` publication 都使用 non-sticky ownership。

native destructor 的成员顺序为：

```text
if texture != null:
    texture.Release()
source.~PSBRawNode()     // release retained owner; raw node has no own refcount
```

这是显式 destructor body 释放 `_texture`，body 返回后由 C++ 隐式析构 `_source` 的直接结果。
不能把 texture 改成借用 pointer，也不能让 PSB owner 先释放；否则 lazy texture release 若仍
间接依赖 PSB 数据，生命周期边界会变化。

## 本地实现与回归

production source 本轮无需语义改动：

- `main.cpp` 已是零参数 constructor、五个 RO property、一个 method 的精确顺序；
- `ObjSource()` 与 `ObjSource(const PSBRawNode&)` 清楚分离 script default/native factory；
- 三 pointer 自然布局、owner retain、texture direct publication 与 destructor order 已对齐；
- getters 保持 strict/category/optional 的不同边界；
- `drawLayer` category gate 位于 target conversion 之前；
- `ResourceManager::findSource` 保留 adaptor-null 时不 reclaim native 的参考行为。

新增 `Motion.ObjSource NCB constructor publishes the empty raw-node facade` 回归：

- 从真实 `Motion.ObjSource` class object零参数 `CreateNew`，确认 native publication；
- 验证默认 native `width=height=32`、`clip=Void`；
- 经 script dispatch读取 `width/height/clip`；
- 对 `originX/originY` 执行 `PropSet`，确认 getter-only descriptor，而不在本 constructor 测试中
  强制触发默认 raw source 的 strict getter exception；
- 调 `drawLayer(integer)`，确认非-dictionary source 在 target conversion 前成功早退；
- 验证单 Void 创建无 native 的 adaptor shell；
- 验证两个普通 surplus integer 全部忽略并仍创建默认 facade。

## Recovery IDB 改进

四份 recovery IDB 已原位保存：

- member registrar、constructor register/factory/install、八参数 `FuncCall`、allocate/attach、
  五个 getter和 class registration wrapper 统一按 `_guess` 规则命名；
- constructor `FuncCall` 四端应用统一 primitive dispatch prototype；
- registrar、bridge、allocation、五个 getter、drawLayer 和 adaptor destruction写入四端共识
  注释与 bookmark；
- 对 UTF-16 `originX/drawLayer/ObjSource` 逐字节搜索，排除 decompiler 单字母误渲染；
- rename/type/comment 后 fresh decompile 四个 registrar 与四个 constructor bridge；除 Android
  ARMv7 既有 local-variable-allocation warning 外，全部 gate、target 和返回码可读；
- 四份 IDB 均保存成功。

## 验证

- 整份 `motionplayer-dll.cpp` Emscripten TU syntax check 通过；只保留仓库既有 `_tss`
  literal-operator deprecation warning。
- `cmake --build --preset "Web Debug Build"` 成功；SourceCache/ObjSource production objects
  已由前一轮 32/32 全量构建链接，当前源码无新的 production 变化，因此 Ninja 正确报告
  `no work to do`。
- 精确 source scan 确认 ObjSource block 恰好一项 `NCB_CONSTRUCTOR(())`、五项
  `NCB_PROPERTY_RO`、一项 `NCB_METHOD`、零 typed constructor overload、零 raw callback，
  且七项严格按参考顺序排列。
- 声明扫描确认类内 default constructor 与独立 `PSBRawNode` constructor 同时存在；新增
  回归、分析文档和 plan link 均存在。
- `git diff --check` 通过；仓库既有 LF/CRLF 提示不视为内容错误。
- 当前 CMake 没有配置可直接运行该 Catch2 motionplayer TU 的 native executable；只记录
  真实编译验证，不伪造运行结论。

相邻证据：

- `analysis/motionplayer_objsource_texture_owner_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_module_map_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_source_cache_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/psbfile_four_binary_audit_2026-08-10.md`
