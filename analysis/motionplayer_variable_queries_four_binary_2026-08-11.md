# MotionPlayer variable query 四参考二进制复原（2026-08-11）

## 结论

本轮从四个当前参考二进制重新定位并反编译 `getVariableRange`、
`getVariableFrameList`、Player fallback、递归折叠器和共享 child visitor，修正了
旧 `libkrkr2.so` 阶段遗留的五处源级偏差：

1. Player 范围 fallback 原版不是自行重扫 `_nodes`，而是调用共享的
   type-4/type-3 child visitor；本地现恢复为独立递归成员 + 共享 visitor 的调用链。
2. `getVariableFrameList` 对非空动态 label 会传入该 `ttstr` 自有的 TJS hash
   hint；本地原来固定传 `nullptr`。
3. 上游 `buildVariableList_guess` 对 frame Dictionary 的动态 label `PropGet` / 
   `PropSet` 也传同一 hint；本地原来两次都传 `nullptr`。
4. `getVariableFrameList` 不是让字段闭包副本跨越脚本 getter：它把字段 CopyRef
   转换为独立 retained dispatch owner，立即 Clear 闭包副本，getter 输出再显式
   CopyRef 到隐藏返回对象。
5. 两条 `getVariableRange` 命中路径都不是泛型 Dictionary initializer：EmotePlayer
   先持有局部 Dictionary，最后复制到隐藏返回对象；Player 则直接在隐藏返回对象
   中建立 Dictionary。miss 还会为 Player 的按值参数额外 CopyRef 一次 label。

HM5 命中、Player fallback、空/单点区间返回 Void、frame Array 借用/返回所有权，
以及 D3D 三个明确 TODO wrapper 的边界均未扩写为本地自创行为。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmotePlayer_getVariableRange_guess` | `0x670FCC` | `0x55AF8C` | `0x1001AE454` | `0x1ADC6C` |
| `EmotePlayer_getVariableFrameList_guess` | `0x67F67C` | `0x5622A0` | `0x1001B63C8` | `0x1B623C` |
| `Player_getVariableRange_guess` | `0x6D3970` | `0x597C00` | `0x1001241FC` | `0x1234D8` |
| `Player_foldVariableRangeRecursive_guess` | `0x6D3B4C` | `0x597D54` | `0x100124380` | `0x1236BC` |
| `Player_visitChildPlayerDispatches_guess` | `0x6B33FC` | `0x5824E4` | `0x10010A13C` | `0x107A20` |
| range child callback | `0x6F3CA8` | `0x5B05F8` | `0x100146180` | `0x146660` |
| `EmoteEngine_buildVariableList_guess` | `0x667910` | `0x555FC0` | `0x1001A73C0` | `0x1A693C` |

两类 EmotePlayer wrapper 由 UTF-16LE 成员名交叉引用回到各端 NCB 注册器后定位；
Player fallback 和递归 fold 由 wrapper 的直接调用链定位。iOS 的 range child
callback 没有普通直接 call xref：它分别从 `std::function` functor vtable 的调用槽
反查到 `0x100146180` / Thumb 函数 `0x146660`。

以上函数已经用表内语义名称写回四份 IDB；名称带 `_guess` 表示产物能证明语义，
但不能证明原始 C++ 标识符拼写。

## `getVariableRange` 数据流

四端共同伪代码为：

```cpp
Variant EmotePlayer::getVariableRange(ttstr label) {
    auto it = engine.variableRangesHM5.find(label);
    if (it != engine.variableRangesHM5.end()) {
        Dictionary result;
        result.setEnsure("min", it->second.frameMin, minHint);
        result.setEnsure("max", it->second.frameMax, maxHint);
        return result;
    }
    return engine.player->getVariableRange(label);
}

Variant Player::getVariableRange(ttstr label) {
    double minValue = DBL_MAX;
    double maxValue = -DBL_MAX;
    foldVariableRangeRecursive(label, minValue, maxValue);
    if (!(minValue < maxValue))
        return Void;
    return Dictionary{{"min", minValue}, {"max", maxValue}};
}
```

HM5 命中每次都创建新的 Dictionary；不会返回 map node、缓存 Dictionary 或共享
mutable wrapper。其精确 owner 流程是：factory dispatch 进入一个局部 owning
Variant，释放 factory 原始引用；再 CopyRef/ToObject 建立 retained accessor 并
early Clear accessor 输入副本；依次写 `min`、`max`；最后才 CopyRef 局部 owning
Variant 到隐藏返回对象，随后释放 accessor 并析构原 owner。两个属性 setter 的
布尔结果均被忽略，因此单个写失败不会回滚另一个写，仍会正常发布当时的
Dictionary 内容。

miss 分支不是把当前 `label` 借给 Player：wrapper 先 CopyRef 一份 `ttstr`，以按值
参数调用 `Player::getVariableRange(ttstr label)`，返回后再释放该副本。Player 的
递归 fold 仅借用这份按值参数。

Player 有效区间路径直接把 fresh Dictionary 建立在 ABI 隐藏返回 Variant 中，再
用一个短命 CopyRef/ToObject/accessor 写入两个属性；它没有 EmotePlayer 命中路径
末尾的第二次返回 CopyRef。EmotePlayer 与 Player 各有一对独立的 `min` / `max`
进程级 TJS hint，共四个 mutable hint，不能合并成泛型 helper 的一对 hint。各 ABI 的 HM5
STL/value 布局不同，查询所读的最终 frame extrema 偏移如下：

| ABI | Engine HM5 起点 | node value 中的 `frameMin/frameMax` |
|---|---:|---:|
| Android ARM64 | `+1328` | `+40/+48` |
| Android ARMv7 | `+704` | `+40/+48` |
| iOS ARM64 | `+944` | `+48/+56` |
| iOS ARMv7 | `+508` | `+32/+40` |

这些差异来自 STL、字符串和对齐 ABI，不代表四端算法分叉。

### Player parameter vector 与折叠规则

递归 fold 先线性扫描 Player 的 `vector<MotionParameterEntry>`：

| ABI | vector begin/end | entry stride |
|---|---:|---:|
| Android ARM64 | `+384/+392` | `56` |
| Android ARMv7 | `+252/+256` | `48` |
| iOS ARM64 | `+296/+304` | `56` |
| iOS ARMv7 | `+204/+208` | `44` |

每个 `entry.id == label` 的项贡献规范化区间：

```cpp
entryMin = entry.rangeEnd < entry.rangeBegin
         ? entry.rangeEnd : entry.rangeBegin;
minValue = minValue < entryMin ? minValue : entryMin;

entryMax = entry.rangeBegin < entry.rangeEnd
         ? entry.rangeEnd : entry.rangeBegin;
maxValue = entryMax < maxValue ? maxValue : entryMax;
```

2026-08-16 的指令级复核确认，内层 endpoint 选择在相等/unordered 时保留
`rangeBegin`，但外层 accumulator 在相等/unordered 时选择本条记录的新候选。它们不能
折叠成嵌套 `std::min/std::max`：后者会在外层保留旧累计值。最终发布 gate 也不是
对 NaN 不成立的普通 `min >= max`，而是只在有序 `min < max` 时创建 Dictionary。
signed-zero、NaN 与遍历顺序矩阵见
`motionplayer_variable_range_accumulator_operand_identity_four_binary_2026-08-16.md`。

随后 fold 构造一个捕获 `label/minValue/maxValue` 的 callback，调用共享 child
visitor。callback 的四端共同边界是：

```cpp
if (child != nullptr)
    child->foldVariableRangeRecursive(label, minValue, maxValue);
return true;
```

因此：

- type-4 particle child 按 Array 索引顺序递归；type-3 单 child 按 node 顺序递归；
- 子 dispatch 不是 Player 或 native instance 解析失败时，visitor 仍会把
  `nullptr` 交给 callback，但 range callback 会忽略它；
- callback 永远返回 true，所以范围查询不会主动早停；
- 没有匹配项、所有匹配项合并后为单点、或最终 extrema unordered 时，
  `!(minValue < maxValue)`，返回 Void；不会返回空 Dictionary、`[0,0]`、单点或
  NaN Dictionary；
- 反向端点会先正规化；没有额外 finite/NaN 防护。相等/unordered 时的新候选选择
  是数据流自身的一部分。

本地先前手写的 `_nodes` 扫描在正常 child 上能得到相同数值，但绕开了共享
visitor 的 dispatch 解析、顺序和 callback 边界，也没有复原原始源代码调用链；
现已拆成 `foldVariableRangeRecursive_guess` 并复用该 visitor。

## `getVariableFrameList` 所有权与 hint

四端共同流程为：

1. CopyRef Engine 的 `_variableFrameLists` Variant；
2. 对副本执行 `ToObject`，构造一个 retained dispatch accessor；
3. 在脚本 getter 之前立即 Clear 第 1 步的闭包副本，只让 accessor 的单个 dispatch
   引用跨越调用；
4. 用 `flags=0`、动态 label、label hint、局部 result、retained dispatch 自身作为
   `objthis` 调用 `PropGet`；
5. CopyRef result 到 ABI 返回 Variant；
6. 析构 result，再释放 retained dispatch。

Engine 字段偏移为 Android `+1248/+664`、iOS `+880/+476`。非空 `ttstr`
使用字符串对象中的 embedded hint 槽：64 位为 string object `+68`，32 位为
`+60`。null `ttstr` 改用进程全局空 UTF-16 字符串，并传 `nullptr` hint。

函数不检查 `PropGet` 的 HRESULT，也不克隆命中的 Array。失败时局部 result
保持/变为 Void；成功时返回值与 Dictionary 中的 frame Array 共享 dispatch
所有权。函数也没有 `_variableFrameLists` 非对象/null 防护：metadata reset/ctor
建立 Dictionary 是原版依赖的不变量。

## variable-list builder 的动态 label

重新检查四端已定位的 `buildVariableList_guess`，动态 frame Dictionary 路径均为：

```cpp
tjs_uint32 *hint = label.GetHint(); // null label 时为 nullptr
if (frameDictionary.PropGet(TJS_MEMBERMUSTEXIST,
                            label.c_str(), hint, &frames, frameDictionary)
    failed) {
    frames = new Array;
    labels.add(label);
    frameDictionary.PropSet(TJS_MEMBERENSURE,
                            label.c_str(), hint, &frames, frameDictionary);
}
```

同一次循环的 `PropGet` 与 miss 后 `PropSet` 复用 label 的同一 embedded hint。
四端汇编分别显式形成 `label object + 68/+60/+68/+60`；这不是 TJS 内部最终
可能重新计算 hash 后才能观察到的优化细节，而是调用 ABI 的一部分。

### 后续 HM5 节点生命周期补充

对同一 builder 的 node constructor/clear 路径继续追踪后，又确认 mapped value
自身持有第二份相同 label CopyRef，并确认 frame 极值在相等/NaN 比较时选择新
frame 值。本地已把插入改为 `try_emplace(label, label)`，同时用显式比较替换
`std::min/std::max`。四端节点布局、析构顺序与 signed-zero/NaN 边界详见
`analysis/motionplayer_variable_range_hm5_four_binary_2026-08-11.md`。

## D3D wrapper 边界

`D3DEmotePlayer::countVariableFrameAt`、`getVariableFrameLabelAt` 和
`getVariableFrameValueAt` 仍保持四端参考中明确存在的 TODO 抛错行为。本轮没有
根据 EmotePlayer 的 frame Dictionary 猜测并实现这三个不同 API。其四端证据见
`analysis/motionplayer_d3d_todo_boundaries_four_binary_2026-08-11.md`。

## 本地修改与验证

源代码修改：

- `Player::getParameterRangeLike_0x6D6590` 重命名为
  `Player::getVariableRange_guess`；
- `Player::getVariableRange_guess` 恢复为 `ttstr` 按值参数；
- 新增 `Player::foldVariableRangeRecursive_guess`，并复用
  `visitChildPlayerDispatches_guess`；
- `EmotePlayer::getVariableRange` 与 Player fallback 分别恢复各自的 fresh
  Dictionary owner/accessor/返回 handoff 管线和独立 `min/max` hint 对；
- `EmotePlayer::getVariableFrameList` 恢复 copy/force/accessor/early-Clear、
  `label.GetHint()` 与显式返回 CopyRef 顺序；
- `EmoteEngine::buildVariableList_guess` 的动态 label `PropGet/PropSet` 改为
  共享 `label.GetHint()`；
- 删除触及范围内的旧 `libkrkr2.so` 地址注释，把精确地址集中保存在本文；
- 单元测试新增 HM5 fresh Dictionary、HM5 miss -> Void、非空 label embedded
  hint、空 label null hint、`objthis == frameDictionary` 契约。

验证结果：

- Web Debug 增量编译并完整链接通过；
- Wasmtime Debug 增量构建通过（最终 `ninja: no work to do`）；
- `motionplayer-dll.cpp` 使用 Web `compile_commands.json` 的真实 Emscripten
  编译参数执行 `-fsyntax-only` 通过，仅有仓库既有 `_tss` literal operator
  deprecated warning；
- 首次同时触发两套构建时，Web 后链接阶段曾在 Emscripten 内部 metadata
  assertion 失败；隔离重跑同一 Web link 成功，未出现 C++ 编译或符号错误；
- 四份 IDB 完成语义重命名并保存。

2026-08-15 对 owner/return handoff 的再次四端复核还通过了真实 Emscripten
syntax-only 与完整 Web Debug 最终链接；精确异常前缀和两条 Range Dictionary
管线另见
`analysis/motionplayer_variable_range_dictionary_owner_handoff_four_binary_2026-08-15.md`
及
`analysis/motionplayer_variable_frame_list_query_owner_return_four_binary_2026-08-15.md`。
