# MotionPlayer Eye builder：稀疏索引、deque owner 与发布顺序（四参考，2026-08-15）

## 结论

四份当前参考的 Eye metadata builder 具有同一条严格 publication 链：先按 metadata index 取元素并检查 `enabled`，再构造 BlinkController，把 raw pointer 交给 deque #4 的新 owner 元素；新元素起初 label 为空，随后才读取/赋值 metadata label，最后才用该 label 在 controller-ref map 中 insert-or-find，并写 `{type=4,index=metadataIndex}`。

这里的 index 不是 deque 的压缩索引。disabled 元素不进入 deque，但仍推进 metadata index。因此发布的索引域可以有洞；后续 type-4 reader 又直接以该 index 下标 deque，原版没有补偿映射或边界检查。合法 metadata 必须自己保持相应约束，Web 端不能“修正”为 deque size。

duplicate label 和空 label 也不被拒绝：map 命中时，后来的 enabled 元素覆盖早先 `{type,index}`，但两个 controller owner 都保留在 deque 中。

## 函数映射

| 参考 | `EmoteEngine_buildEyeControl_guess` | 大小 | BlinkController 分配大小 |
|---|---:|---:|---:|
| Android A64 | `0x669B5C` | `0x420` | `0x170` |
| Android A32 | `0x55739C` | `0x1C0` | `0xD8` |
| iOS A64 | `0x1001A91F4` | `0x204` | `0x110` |
| iOS A32 | `0x1A8800` | `0x208` | `0xA8` |

函数名仍带 `_guess`，表示 stripped binary 的语义恢复名。

## 数据流

```text
eyeControl variant
  -> retain/unbox list object
  -> metadataCount = getCount()，只取一次
  -> for metadataIndex in [0, metadataCount)
       -> element = list[metadataIndex]
       -> enabled = element["enabled"]
       -> disabled: release temporaries, continue
       -> operator new(platform BlinkController size)
       -> EmoteBlinkController(element)
            -> constructor consumes one shared canonical RNG
       -> deque #4 append { owner(raw controller), empty label }
       -> label = element["label"]
       -> deque.back().label = retained label
       -> controllerRef = refMap.getOrInsert(deque.back().label)
       -> controllerRef.type  = 4
       -> controllerRef.index = metadataIndex
```

四平台关键地址：

| 语义 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| count snapshot | `0x669BF0` | `0x5573D4` | `0x1001A924C` | `0x1A887C` |
| enabled gate | `0x669CC4` | `0x55744A` | `0x1001A92D0` | `0x1A88F2` |
| allocate/construct | `0x669CD0..0x669CDC` | `0x557456..0x55745C` | `0x1001A92DC..0x1001A92E8` | `0x1A88FE..0x1A8908` |
| deque owner append | `0x669CF4`（增长路径到 `0x669D68`） | `0x557468..0x557486` | `0x1001A92F8` | `0x1A8918` |
| label fetch/assign | `0x669D78..0x669DF0` | `0x5574B6..0x5574F0` | `0x1001A9338..0x1001A936C` | `0x1A8956..0x1A8998` |
| map insert/overwrite | `0x669E08..0x669E10` | `0x5574FE..0x557508` | `0x1001A9380..0x1001A9388` | `0x1A89A8..0x1A89B2` |
| metadata index advance | `0x669E40` | `0x557522` | `0x1001A93B0` | `0x1A89D2` |

## deque 内部实现

元素在四份参考中都是：

```cpp
struct EyeEntry {
    one_pointer_owner<EmoteBlinkController> ctl;
    retained_string label;
};
```

元素大小随 pointer width 为 16/8 字节。Android A64 把 fast append 与 libstdc++ deque 增长展开在 builder 内：当前 block 有空间时直接写 raw owner 与空 label；block 满时先扩 map/分配 0x200 字节 node，再在新 block 构造同样元素。其他产物保留 `EmoteEyeControlDeque_emplaceRaw_guess` helper 或较紧凑的等价路径。

当前 `std::deque<EmoteEyeControlEntry_Deque4>` 与 `unique_ptr + ttstr` 恢复了相同 owner/member destruction 顺序：销毁元素时先释放 label，再 delete controller。

## publication 与异常边界

按原顺序可得到这些中间态：

- BlinkController constructor 抛出：new-expression 负责释放本次 allocation，deque/map 均不变。
- controller 已构造、deque 增长分配先抛出：raw pointer 尚未进入 owner，因而泄漏；源码保留 raw emplace，而没有用临时 `unique_ptr` 改变边界。
- deque append 成功、label 读取或赋值失败：deque 中保留 controller owner，label 仍为空或保持已完成的部分状态，ref map 尚未发布。
- label 已赋值、map 分配失败：deque entry 完整存在，ref map 没有对应新 publication。
- map 命中已有 label：不增加节点，直接覆盖其 `{type,index}`；旧 deque controller 不删除。

## 稀疏与 duplicate 边界

示例 metadata：

```text
0 disabled "disabled-zero"
1 enabled  "duplicate-eye"
2 disabled "disabled-two"
3 enabled  "duplicate-eye"
4 enabled  ""
```

构建结果：

```text
deque labels = ["duplicate-eye", "duplicate-eye", ""]
ref["duplicate-eye"] = {4,3}
ref[""]              = {4,4}
disabled labels absent
```

也就是说，map index 3/4 并不等于 deque index 1/2。builder 不规范化这个状态。新测试直接锁定该行为，但不调用后续无边界检查的 type-4 下标 reader，以免把参考的 malformed-metadata UB 变成测试进程崩溃。

## 源码迁移与验证

修改：

- `cpp/plugins/motionplayer/EmoteEngine.cpp`
- `cpp/plugins/motionplayer/EmoteEngine.h`
- `tests/unit-tests/plugins/motionplayer-dll.cpp`

迁移内容：

- 删除 `v5/v7/v17` 伪代码注释和局部命名；
- 使用 `metadataCount/metadataIndex/element/controller/controllerRef`；
- 修正 header 中只写 Android A64 `0x170` 的旧平台单值注释；
- 明确 raw-owner append、label-after-append、map-last publication；
- 新增 disabled hole、duplicate label overwrite、empty label map key 的构建测试。

验证：

- Emscripten 完整单元测试翻译单元语法检查通过，仅有既有 `_tss` warning；
- `Web Debug Build` 完整编译/链接通过；
- 四份恢复 IDB 已把核心 locals 改为语义名，并写入边界注释/书签。

## Eyebrow builder 独立复核

Eye 完成后又对 deque #5 的 Eyebrow builder 做了独立四参考反编译，没有仅凭相似源码类推：

| 参考 | `EmoteEngine_buildEyebrowControl_guess` | 大小 | controller allocation |
|---|---:|---:|---:|
| Android A64 | `0x669F7C` | `0x420` | `0x150` |
| Android A32 | `0x557618` | `0x1C0` | `0xB8` |
| iOS A64 | `0x1001A9540` | `0x204` | `0xF0` |
| iOS A32 | `0x1A8B68` | `0x208` | `0x88` |

其 publication 次序与 Eye 相同，但使用独立的 `EmoteEyebrowController`、deque #5 和 type 5：

| 语义 | Android A64 | Android A32 | iOS A64 | iOS A32 |
|---|---:|---:|---:|---:|
| count | `0x66A010` | `0x557650` | `0x1001A9598` | `0x1A8BE4` |
| enabled | `0x66A0E4` | `0x5576C6` | `0x1001A961C` | `0x1A8C5A` |
| allocate/ctor | `0x66A0F0..0x66A0FC` | `0x5576D2..0x5576D8` | `0x1001A9628..0x1001A9634` | `0x1A8C66..0x1A8C70` |
| raw owner append | `0x66A114` | `0x5576E4..0x557702` | `0x1001A9644` | `0x1A8C80` |
| label after append | `0x66A198` | `0x557732` | `0x1001A9684` | `0x1A8CBE` |
| type-5 map publication | `0x66A228..0x66A230` | `0x55777A..0x557784` | `0x1001A96CC..0x1001A96D4` | `0x1A8D10..0x1A8D1A` |

源码和 IDB locals 同样迁移为 `metadataCount/metadataIndex/controller/controllerRef`，并新增独立测试：disabled index 0/3 不进入 deque，两个同名 enabled 条目都保留 owner，而 map 中 type-5 ref 被后一个 metadata index 2 覆盖。Eyebrow 修改后的 Emscripten 测试 TU 语法检查和最终 `Web Debug Build` 完整编译/链接均通过；四恢复库已再次保存。

## 2026-08-16 accessor/source owner 补完

本页原有 sparse index、raw deque ownership 与 publication 顺序结论保持不变。随后对四参考做的
fresh source-identity 复核进一步确认 Eye 与 Eyebrow builder 并非调用裸 Variant getter：两者都
由 copied control Variant 构造 root `ncbPropAccessor`，每轮保留 indexed getter 返回的 source
element Variant，再由第二份 copy 构造 element accessor。公共尾部严格先释放 element accessor、
再析构 source element，循环尾才释放 root accessor；`enabled` 与 `label` 还分别复用跨 builder
的全局 hint。完整地址表、helper HRESULT 边界、IDB 写回和失败但写值的回归见
`analysis/motionplayer_leaf_controller_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`。
