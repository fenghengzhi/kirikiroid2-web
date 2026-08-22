# MotionPlayer Engine 变量容器尾部语义命名（四参考，2026-08-15）

## 结论

`EmoteEngine` 中三个发布用 `tTJSVariant` 之后的四个 unordered 容器，依次是：

1. `_instantVariableLabels`：时间线 Track 构造时查询的“即时变量”标签集合；
2. `_variableRanges`：变量标签到 metadata 帧极值记录的 map；
3. `_variableControllerRefs`：变量标签到 `{controller type, metadata index}` 的路由
   map；
4. `_variableValues`：控制器输出、直接写入和时间线贡献共同使用的长期标量值 map。

旧名 `_instantVariableSetHM4_1272`、`_variableRangesHM5_1328`、
`_scalarHM6_1384`、`_labelToValueHM7` 混合了调查期编号、单一 Android arm64
偏移和错误的标量推断。四份参考的声明顺序与用途一致，但实际偏移随指针宽度及
STL ABI 改变，因此源码和测试改用上述语义名；容器类型、声明次序和行为不变。

## 四 ABI 字段布局

| 容器 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| instant-variable 标签集合 | `+1272` | `+676` | `+904` | `+488` |
| variable-range map | `+1328` | `+704` | `+944` | `+508` |
| controller-ref map | `+1384` | `+732` | `+984` | `+528` |
| variable-value map | `+1440` | `+760` | `+1024` | `+548` |

instant-variable 容器是 `unordered_set<ttstr>`；其余三个分别是：

```cpp
unordered_map<ttstr, EmoteVariableRange, ttstr_hash, ttstr_equal>
unordered_map<ttstr, EmoteVarRef,       ttstr_hash, ttstr_equal>
unordered_map<ttstr, double,            ttstr_hash, ttstr_equal>
```

Android old-libstdc++ 使用 56/28 字节的 64/32 位 unordered 容器头，默认构造
请求十个 bucket 并落到十一素数 bucket；iOS libc++ 使用 40/20 字节容器头并
保持 bucketless/lazy。Android hash node 采用旧 libstdc++ 的 next/key/value/hash
顺序，iOS node 采用 libc++ 的 next/hash/key/value 顺序。这些都是同一源级容器的
ABI 差异，Web 端不硬编码任一参考的物理 header 或 node 布局。

## 关键函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildVariableList_guess` | `0x667910` | `0x555FC0` | `0x1001A73C0` | `0x1A693C` |
| `EmoteEngine_buildInstantVariableList_guess` | `0x66CA2C` | `0x558DBC` | `0x1001AB6E4` | `0x1AAE18` |
| `EmoteEngine_setVariable_guess` | `0x66E608` | `0x559D84` | `0x1001ACDBC` | `0x1AC5F4` |
| variable-range `find` | `0x685398` | `0x566C72` | `0x1001BDF24` | `0x1BC9EC` |
| controller-ref `operator[]` | `0x6859AC` | `0x56719C` | `0x1001A8574` | `0x1A7B48` |
| variable-range facade getter | `0x670FCC` | `0x55AF8C` | `0x1001AE454` | `0x1ADC6C` |
| timeline-state decoded initializer | `0x66D03C` | `0x5590E8` | `0x1001ABD5C` | `0x1AB4B0` |
| shared label-value get/insert | `0x683D24` | `0x56559C` | `0x10010BD28` | `0x1096A4` |

这些是 stripped 参考上的恢复语义，无法证明原始 C++ 符号拼写，所以恢复 IDB 和
源码函数继续遵守 `_guess` 命名边界。

## instant-variable 集合与 Track 快照

专用 builder 取得 raw array 的 count，按升序索引读取每个 Variant，将其直接转成
`ttstr` 后插入集合。builder 自身没有 `enabled` 门、空串过滤、预清空或额外
去重逻辑；重复值只受 `unordered_set` 自身唯一键规则约束。

重复插入的分配时机是可观察的 STL ABI 边界：Android 的旧 libstdc++ 路径可能先
分配候选 node 再发现重复并释放，iOS libc++ 先查找、只在 miss 时分配。两者最终
集合内容相同，不能为了统一分配轨迹而改写源级算法。

时间线 decoded initializer 在完成 Track 的 label 赋值后立即查询该集合，把结果
复制到 Track 的 `instantVariable` bool：该 bool 位于 64 位 Track 的 `+8`、32 位
Track 的 `+4`。这是一次性构造快照；后来插入或删除集合标签，不会追溯修改已经
存在的 Track。即时 Track 和普通 Track 后续走不同的时间线变量派发路径，所以把
查询推迟到播放期会改变行为。

变量 metadata builder 自身每次调用还会重新发布 fresh `_variableLabels` Array 与 fresh
`_variableFrameLists` Dictionary；后者不是只由 metadata reset 创建一次。每个 label 在
Dictionary strict lookup 前先分配 candidate Array，hit 再做第二次普通 getter并丢弃
candidate，miss 才发布。完整 retained-accessor、双 getter 与异常前缀见
`analysis/motionplayer_build_variable_list_owner_pipeline_four_binary_2026-08-15.md`。

## variable-range map 的非直觉边界

变量 metadata builder 对每个标签执行 find/try-emplace。miss 时，node 同时拥有
两份同标签 `ttstr` 引用：一份 map key，一份 mapped value 内的
`labelCopy_guess`。hit 时直接复用原 node，不覆盖 mapped value。

mapped value 的四个 double 不能被压缩成常见的 `{min,max}`：

```cpp
struct EmoteVariableRange {
    ttstr labelCopy_guess;
    double unusedMinSeed_guess; // DBL_MAX
    double unusedMaxSeed_guess; // -DBL_MAX
    double frameMin;             // 构造器不初始化
    double frameMax;             // 构造器不初始化
};
```

四份构造路径只初始化前一对 seed；builder 实际读取并更新后一对 `frameMin` /
`frameMax`。因此首帧计算保留原版的 indeterminate-input 边界，不能补一个防御性
`DBL_MAX/-DBL_MAX` 初始化。每帧使用的比较等价于：

```cpp
frameMin = frameMin < value ? frameMin : value;
frameMax = value < frameMax ? frameMax : value;
```

相等或 unordered 时都会选择新值；这与 `std::min/std::max` 在 `+0.0/-0.0` 和
NaN 上并不等价。业务 getter 命中时从最后一对字段创建新的 Dictionary，miss 时
委托内层 `Player` 的递归参数范围查询；它不会把 miss 伪造为 `[0,0]`。

## controller-ref 路由

controller-ref mapped value 是无所有权的 8 字节 POD：

```cpp
struct EmoteVarRef {
    int32_t type;
    int32_t index;
};
```

`operator[]` miss 默认产生 `{0,0}`；hit 保留已有值。各 metadata builder 随后写入
控制器类别与原始 metadata 循环索引。这里的 index 不是“成功追加后的 deque
下标”：被 `enabled` 门跳过的 element 仍消耗 metadata index，所以数值域允许有
洞。眼、眉、嘴、transition、selector、loop 以及两类 spring builder 都向同一张
路由表登记；嘴控制器会为两个输出标签登记同一个 `{type,index}`。

`setVariable` 先用 `find` 查询该 map，然后按 type `0..8` 选择对应 controller
deque，并使用保存的 index。controller-ref miss，或 type `0/1/2` 且 `_directEdit`
打开时，流程落入 `_variableValues[key] = value`。这也意味着空字符串仍是普通键，
而默认构造出来但尚未由 builder 覆盖的 `{0,0}` 具有真实的 type-0 路由语义，不能
用额外 validity flag 改写。

## variable-value map 的数据流与生命周期

`_variableValues` 是长期 `ttstr -> double` 所有权表，至少承接四类写入：

1. `setVariable` 的 controller-ref miss/direct-edit fallback；
2. 每个 progress slice 中 controller deque 的输出；
3. reset/flush 类零步长 controller 采样；
4. bind 阶段的 active timeline contribution 原地累加。

progress 随后遍历该 map，对每个值先叠加时间线贡献，再根据镜像缓存作变换，最后
写入嵌套 Player 的两张查询 map。Engine map 与 Player map 是不同对象；没有一次
progress bind，Engine 侧刚写的值不会自动出现在 Player getter 中。unordered-map
的 node-chain 次序受 hash、碰撞和 rehash 控制，不是源级插入顺序；循环对不同标签
独立发布，因此四个 STL 实现次序不同而没有跨标签数据依赖。

## reset、clear 与析构顺序

完整 metadata reset 的关键顺序是：

1. 先清 `_variableControllerRefs`；
2. 清控制器/物理 node 容器并重建三个发布 Variant；
3. 清 `_instantVariableLabels`；
4. 清 `_variableRanges`；
5. **不清** `_variableValues`。

三个发布 Variant 的精确 owner 顺序是：fresh Array copy-assign
`_variableLabelsBase` 后立即析构 factory helper 临时量，再从 base 成员 CopyRef 到
`_variableLabels`；随后 fresh Dictionary Object Variant copy-assign
`_variableFrameLists` 并立即析构临时量。之后才执行第 3、4 步。四端 fresh refcount 与
异常前缀见
`analysis/motionplayer_variable_publication_variant_reset_lifecycle_four_binary_2026-08-15.md`。

上述 clear 都释放键、mapped value 自有字符串和 node，但保留当前 bucket 分配、
bucket count、负载因子及 rehash policy。variable values 刻意跨 metadata replacement
存活，之后由新控制器 step 或 `setVariable` fallback 惰性覆盖。若在 reset 中顺手
清掉它，会改变旧值可见期、Player bind 输入和 node/bucket 生命周期。

正常 Engine 析构按成员逆声明顺序释放：variable values、controller refs、variable
ranges、instant-variable labels，随后才是三个发布 Variant。range node 先释放 mapped
value 的标签副本，再释放 key；controller-ref 和 double mapped value 无独立资源；
set/map node 最终各自按目标 STL ABI 的 node chain 销毁。

## 源码迁移

| 旧名 | 新名 |
| --- | --- |
| `_instantVariableSetHM4_1272` | `_instantVariableLabels` |
| `_variableRangesHM5_1328` | `_variableRanges` |
| `_scalarHM6_1384` | `_variableControllerRefs` |
| `_labelToValueHM7` | `_variableValues` |

本轮同时把 Engine 专属的 HM4–HM7 注释改成用途名，并保留 Player 自己独立的
HM1/HM2/HM4 调查标识，避免把两组物理不同的容器混为一谈。绝对地址、ABI 偏移
和调查编号只保留在本文及恢复 IDB；编译源码只描述四参考共同成立的源级语义。
