# 四端注册字符串、参数默认值与绑定目标最终对账

## 结论

本报告建立 `MP-A32` 的最终注册契约分母。可重生成主表
`analysis/motionplayer_registration_contracts.tsv` 当前包含 494 个唯一条目，全部为
`EVIDENCED_4_4`，没有 `UNMAPPED`：

| 来源 | 行数 | 分母意义 |
|---|---:|---|
| `motionplayer_ncb_equivalence.tsv` | 316 | `motionplayer.dll` 的原有 NCB candidate/member 分母 |
| DrawDeviceD3D 七类 | 167 | 7 class name、6 factory + 1 constructor、4 constants 和全部成员 |
| 四个模块根 | 4 | `motionplayer.dll`、`emoteplayer.dll`、`DrawDeviceD3D.dll`、`DrawDeviceD3DZ.dll` |
| 非普通 registrar 特殊发布 | 4 | 两个 decrypt setter、`D3DLayerBase`、`D3DLayerObjectNativeInstance` |
| 漏在 member inventory 之外的 class name | 3 | `Motion`、attached `BezierPatch`、动态 `Motion.EmotePlayer` |
| 合计 | 494 | ID 唯一、四端字段非空、参数契约非空 |

联合四个参考二进制，本地注册字符串的大小写、对象内顺序、descriptor kind、绑定
目标和所有 script-side 可选参数默认值已经对账完成。本任务没有发现需要修改的 C++
注册语义；新增的是可重复审计生成器和最终契约表。

这项结论只闭合“发布什么、绑定到哪里、调用参数如何解释”。它不拿注册表替代
callback body、对象生命周期、容器或正式构建验证；这些仍由各语义切面和
`MP-F03..F07`、`MP-V01..V16` 独立跟踪。

## 可重生成分母

生成链为：

```text
cpp/plugins/motionplayer/main.cpp
    -> generate_local_ncb_inventory.py
    -> motionplayer_local_ncb_inventory.tsv                (316)

four-binary surface reports + address TSVs
    -> generate_ncb_equivalence_ledger.py
    -> motionplayer_ncb_native_evidence.tsv                (316)
    -> motionplayer_ncb_equivalence.tsv                    (316 / 316)

316-row ledger + MP-A30/MP-A31 + module-root evidence
    -> generate_registration_contracts.py
    -> motionplayer_registration_contracts.tsv             (494 / 494)
```

最终表字段为：稳定 ID、module、owner、sequence、kind、精确 script name、binding、
argument contract、四个平台证据、registration status 和 evidence report。

生成器的硬失败条件包括：

- 原 NCB candidate / evidence 不再恰好为 316 行；
- 316 行中任一不是 `EVIDENCED_4_4`；
- 15 个原 NCB raw method 的 owner/name 集合与默认契约表不完全相等；
- 10 个原 constructor owner 或 2 个原 factory owner 集合发生漂移；
- 最终总数不为 494、ID 重复、任何必填字段为空；
- 字段含嵌入的 tab/newline/carriage return。

因此新增一个 raw callback、constructor、factory、D3D member 或 module root 时，不能
在不更新证据分母的情况下静默通过。

## 精确字符串与大小写

### 316 行 `motionplayer.dll` NCB 分母

现有 equivalence generator 不是用本地名字直接宣称参考事实。它以本地注册块生成
candidate，再要求 native evidence 的 `(owner, sequence, script_name)` 与候选逐字相等，
并要求四个平台地址字段全部存在。当前 316 个候选和 316 个 native evidence 一一
合并，大小写不同、历史错拼不同或顺序移动都会使生成器失败。

这个分母包含：

- 顶层 `Motion` 的 23 constants、11 subclasses 和 2 namespace methods；
- `Player` 93 行；
- `EmotePlayer` 73 行；
- `LayerGetter` 30 行；
- `D3DAdaptor` 16 行；
- `ResourceManager` 13 行；
- 两套 Layer attached surfaces 共 17 行；
- `BezierPatch` 8 行；
- geometry、SourceCache、ObjSource、SeparateLayerAdaptor 等剩余行。

需要原样保留的例子包括 `CoordinateRecutangularXY/XZ` 的历史错拼、
`defaultSyncActive/defaultTransformOrder` 的精确 camelCase、
`setEmotePSBDecryptSeed/Func` 的 `PSB` 大写，以及不同 facade 中看似相似但并非同一
拼写/绑定的名字。

### DrawDeviceD3D 七类

MP-A31 对前六类执行了 24 个完整 registrar 审计，并对 68 个相关 UTF-16LE 名字做
四库含终止符原始字节矩阵；每个 pattern 都有完整命中，所有 cursor 为
`done=true`。MP-A30 另行闭合 `D3DEmotePlayer` 的 4 constants + 54 members。

七个 class name 的顺序为：

```text
DrawDeviceD3D
D3D
D3DLayer
D3DImage
D3DPicture
D3DEmoteModule
D3DEmotePlayer
```

需要特别保留：

- `DrawDeviceD3D` 与 `D3D` 各自发布相同的 33-name surface，但 concrete
  class identity 和 factory 不同；
- `D3DEmotePlayer` 的成员名是历史拼写 `queing`，不是 `queuing`；
- `D3DLayer` constants 是 `DrawPlaneFront/Back/Both`，值分别为 1/2/3；
- `D3DEmotePlayer` constants 的 `TimelinePlayFlagParallel/Difference` 大小写和值
  分别为 1/2；
- `D3DLayerBase` 与 `D3DLayerObjectNativeInstance` 是两个内部 native identity，
  不发布为 global script class，但仍属于注册字符串分母。

### member inventory 之外的发布

仅扫描普通 NCB member block 会漏掉以下名字，所以最终表显式加入：

- 四个 module name；
- 顶层 `Motion` class name；
- attached `BezierPatch` class identity；
- `emoteplayer.dll` callback 动态发布的 `Motion.EmotePlayer`；
- 动态注入 `ResourceManager` 的 `setEmotePSBDecryptSeed` 和
  `setEmotePSBDecryptFunc`；
- 两个 D3D 内部 native identity。

`MotionLayerExtensions_guess` 没有作为脚本 class name 发布；它只是本地 C++ owner
标签，九个成员直接附加在 `Layer` 上。因此最终表包含它的 9 个实际脚本 member，
不把 `_guess` 标签伪造成额外注册字符串。

## descriptor kind 与绑定目标

494 行的 kind 分布为：

| kind | 数量 |
|---|---:|
| method | 184 |
| property | 117 |
| property_ro | 93 |
| variant | 32 |
| method_raw | 18 |
| constructor | 11 |
| subclass | 11 |
| class | 9 |
| factory | 8 |
| method_detail | 4 |
| module_root | 4 |
| native_identity | 2 |
| attached_class | 1 |

普通 NCB 316 行保留本地 macro binding 和四端 registrar/callback 地址；D3D 167 行
保留 class/factory/member binding 与各端 registrar/entry；特殊行保留 callback、
publisher 或 setup 地址。所有四平台字段非空。

关键非一一同名绑定也已显式记录：

- `Player.clear` → `drawToLayerRecursive_guess(Variant, Variant)`；
- `Player.draw` → `draw(Variant)` detail wrapper；
- `D3DEmotePlayer.setTimelineBlendRatio` → 五参数 `setTimeline`；
- `D3DEmotePlayer.pass` → `passTimelines_guess()`；
- `D3DEmotePlayer.load` → raw `loadCompat`；
- `DrawDeviceD3D.interface` → read-only `getInterface`；
- `motionKey/project`、`x/left`、`y/top` 等有意复用 callback pair 的别名仍是两条
  独立 script registration。

Android arm64 的少量 callback 是 combined function 的内部 entry，表中记录 entry 而
不伪造重叠函数；Android 的 identical-code folding 也不会把独立 NCB identity 合并。

## 参数与默认值最终矩阵

### 普通 typed member

所有普通 typed method / method-detail 都按绑定的成员函数签名要求每个参数；缺少
required argument 返回 `TJS_E_BADPARAMCOUNT`，surplus 不转换并忽略。它们没有
script-side 可选默认值。

唯一容易误判的 C++ 默认声明是 `EmotePlayer::play(label, flags=0)`：成员函数指针的
typed NCB descriptor 仍要求 label 和 flags 两项；`flags=0` 只服务直接 C++ 调用，
不是脚本默认参数。最终表对此有独立 contract 文本。

### 18 个 raw method

| owner / method | minimum argc | 可选默认值 / surplus |
|---|---:|---|
| `Player.setVariable` | 2 | `mode=0`；argv3+ 忽略 |
| `Player.play` | 2 | 无；argv2+ 忽略 |
| `Player.progress` | 1 | 无；argv1+ 忽略 |
| `EmotePlayer.setVariable` | 2 | `transition=0, ease=0`；argv4+ 忽略 |
| `EmotePlayer.setCoord` | 2 | `transition=0, ease=0`；argv4+ 忽略 |
| `EmotePlayer.setScale` | 1 | `transition=0, ease=0`；argv3+ 忽略 |
| `EmotePlayer.setRotate` | 1 | `transition=0, ease=0`；argv3+ 忽略 |
| `EmotePlayer.setColor` | 1 | `transition=0, ease=0`；argv3+ 忽略 |
| `EmotePlayer.setOuterForce` | 3 | `transition=0, ease=0`；argv5+ 忽略 |
| `EmotePlayer.playTimeline` | 1 | `flags=0`；argv2+ 忽略 |
| `EmotePlayer.stopTimeline` | 0 | label 为 allocated-empty string；argv1+ 忽略 |
| `EmotePlayer.getTimelinePlaying` | 0 | label 为 allocated-empty string；argv1+ 忽略 |
| `EmotePlayer.setTimelineBlendRatio` | 1 | `duration=0, ease=1, autoStop=false`；argv4+ 忽略 |
| `EmotePlayer.fadeInTimeline` | 1 | `duration=0, ease=1`；argv3+ 忽略 |
| `EmotePlayer.fadeOutTimeline` | 1 | `duration=0, ease=1`；argv3+ 忽略 |
| `D3DEmotePlayer.load` | 0 | 无默认；所有 supplied argv 都按顺序转 ttstr |
| `setEmotePSBDecryptSeed` | 1 | 无；argv1+ 忽略 |
| `setEmotePSBDecryptFunc` | 1 | 无；argv1+ 忽略 |

每个 raw callback 的转换顺序和异常 partial-conversion owner 由其 companion report
闭合；这里的 contract 只汇总脚本边界。

### constructor / factory

| family | 参数契约 |
|---|---|
| `SourceCache/ObjSource/Point/Circle/Rect/Quad/LayerGetter/D3DEmoteModule` | ordinary argc 0 或任意 surplus 均可，argv 全忽略；exact-one-Void 是 empty adaptor |
| `SeparateLayerAdaptor/Player/EmotePlayer factory` | 至少 1 个 Variant，只消费 arg0；surplus 忽略；exact-one-Void sentinel |
| `ResourceManager` | 至少 2 项：Variant + int32；surplus 忽略；exact-one-Void sentinel |
| `D3DAdaptor` | 至少 5 项：Window + 4 个 int32；surplus 忽略；exact-one-Void sentinel |
| `DrawDeviceD3D/D3D` | raw factory 至少 width/height；surplus 忽略；exact-one-Void sentinel |
| `D3DLayer/D3DImage` | raw factory 至少一个 `D3DLayerBase` Object；surplus 忽略；exact-one-Void sentinel |
| `D3DPicture` | typed factory 至少 `D3DLayer,D3DImage`；surplus 忽略；exact-one-Void sentinel |
| `D3DEmotePlayer` | typed factory 至少一个 `D3DLayer`；surplus 忽略；exact-one-Void sentinel |

sentinel 一律要求 argc 恰好为 1 且 arg0 为 Void；Void+surplus 离开 sentinel，按该
family 的 ordinary 参数规则处理。descriptor 是在 result clear 前还是后执行 gate、attach
失败是否删除 fresh native、constructor re-entry 是否覆盖并泄漏旧 native，仍由各 factory
报告保留，最终表不把它们压平成一个不存在的统一 wrapper。

## 四端差异 disposition

本任务中观察到的差异均不改变注册契约：

- LP64/ILP32 descriptor/adaptor 大小和 slot 宽度；
- Android libstdc++ 与 iOS libc++ helper 展开；
- ARM/Thumb function pointer 低位和部分 xref 恢复不足；
- Android arm64 大量 template inline、combined internal entry；
- linker ICF/dead-strip 对字节相同 body 的折叠；
- Hex-Rays 把未类型化 UTF-16LE 字面量显示为首字符。

精确字符串以原始 UTF-16LE bytes + registrar operation 为准，不能用 Hex-Rays 的
单字符显示或一次 negative search 改写源码名字。

## 本地映射与执行校验

本地实现来源：

- `cpp/plugins/motionplayer/main.cpp`：316-row candidate 注册块；
- `cpp/plugins/DrawDeviceD3D.cpp`：七类 D3D 注册块和 module callbacks；
- `cpp/plugins/motionplayer/EmotePlayer.cpp`：raw/default callback bodies；
- `cpp/plugins/motionplayer/Player*.cpp`：Player raw callbacks；
- `tools/motionsim/generate_local_ncb_inventory.py`；
- `tools/motionsim/generate_ncb_equivalence_ledger.py`；
- `tools/motionsim/generate_registration_contracts.py`。

本轮执行：

```sh
python3 -m py_compile \
  tools/motionsim/generate_local_ncb_inventory.py \
  tools/motionsim/generate_ncb_equivalence_ledger.py \
  tools/motionsim/generate_registration_contracts.py
python3 tools/motionsim/generate_local_ncb_inventory.py
python3 tools/motionsim/generate_ncb_equivalence_ledger.py
python3 tools/motionsim/generate_registration_contracts.py
```

结果为 316 local candidates、316 native evidence、316 merged rows（0 unmapped）、
494 final contracts（494 unique、494 `EVIDENCED_4_4`、无空字段）。
`git diff --check` 通过。

## disposition

- 原始任务：`MP-A32`
- 静态状态：`CLOSED_STATIC`
- 覆盖切面：`MP-A32-REGISTRATION-STRINGS-ARGUMENTS-BINDINGS`
- 本任务局部剩余差异：无
- 独立剩余工作：模块加载集合/大小写机制由 `MP-A03` 跟踪；全函数、全对象和正式
  runtime/build verification 仍由 `MP-F03..F07`、`MP-V01..V16` 跟踪。
