# MotionPlayer ResourceManager layer-id allocator 四参考二进制恢复（2026-08-14）

## 范围与结论

本纵切面只恢复 `motion::ResourceManager` 的 layer-id 分配器、它在对象尾部的字段布局、
NCB 暴露返回值，以及构造/析构边界。证据全部来自当前四份参考二进制；旧
`libkrkr2.so` 注释不作为裁决依据。

四端共同的源级模型是：

```cpp
std::set<std::uint32_t> usedLayerIds{0};
std::uint32_t nextLayerId = 1;
std::uint32_t layerIdState_guess = 1;
std::int32_t spec = 0;

std::int32_t requireLayerId() {
    while (usedLayerIds.find(nextLayerId) != usedLayerIds.end())
        ++nextLayerId;                    // uint32 wrap
    const std::uint32_t id = nextLayerId;
    usedLayerIds.insert(id);              // may throw before the final increment
    ++nextLayerId;
    return static_cast<std::int32_t>(id);
}

std::int32_t releaseLayerId(std::int32_t id) {
    auto first = usedLayerIds.find(static_cast<std::uint32_t>(id));
    auto count = std::distance(first, usedLayerIds.end());
    usedLayerIds.erase(first, usedLayerIds.end());
    return static_cast<std::int32_t>(count);
}
```

这推翻了两条旧注释：容器 key 不是 signed `int`；`releaseLayerId` 不是
`set.erase(id)`，也没有 `id == 0` 的 early return。它在精确命中后删除完整的有序
后缀 `[found,end)`，并向 TJS 返回删除数量。

## 函数地址

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `ResourceManager` ctor | `0x6A5CAC` | `0x57B1EC` | `0x100101158` | `0xFE254` |
| require | `0x6A8A74` | `0x57C258` | `0x100102D40` | `0x100240` |
| release | `0x6A8B30` | `0x57C2C8` | `0x100102DB8` | `0x10028A` |
| member registrar | `0x6A8C9C` | `0x57C3A8` | `0x100102E88` | `0x1002FC` |

恢复库统一把两个主体命名为
`ResourceManager_requireLayerId_guess` / `ResourceManager_releaseLayerId_guess`。

## 对象尾部布局

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `std::set<uint32_t>` | `+0xA8`, 48 B | `+0x5C`, 24 B | `+0xA0`, 24 B | `+0x58`, 12 B |
| `nextLayerId` | `+0xD8` | `+0x74` | `+0xB8` | `+0x64` |
| `layerIdState_guess` | `+0xDC` | `+0x78` | `+0xBC` | `+0x68` |
| `spec` | `+0xE0` | `+0x7C` | `+0xC0` | `+0x6C` |

构造器先完成基类、module map 和 random generator，再构造 set 并插入 key 0；插入成功后，
`nextLayerId` 与相邻未知状态槽同时写 1。`spec` 是再后一个独立槽并保持 0。Android
armv7/iOS armv7 有直接的 `spec=0` store；两个 64 位编译器把相邻零初始化折叠进更宽的
零写，不能据反编译伪代码的缺失误判为未初始化。

`layerIdState_guess` 的四端物理存在、初值与生命周期边界已确定：完整插件代码范围内只有
构造写入，没有构造后的 read/write，也没有析构清理。源码按规则保留这个 32 位 dormant
scalar 和 `_guess`，但不编造业务含义或原始字段名。

### 中间状态槽的消费者复核

本轮先对四端 ResourceManager registrar 暴露的完整原生成员表逐项复核：
`load`、`unload`、`unloadAll`、`isExistMotion`、`findMotion`、`findSource`、`random`、
`requireLayerId`、`releaseLayerId`，另含构造、析构和构造失败清理。结果是：

- ctor 是该槽目前唯一确认的写入者，四端都把它写成 1；这排除了“仅为未初始化
  alignment padding”的解释；
- require 只读写前一槽 `nextLayerId`，release 只操作 set；load 只读写后一槽
  `spec`；其余公开成员没有读写该中间槽；
- 析构不读取它，也没有单独清零；它不拥有需要释放的资源；
- ctor-only 写入分别位于 Android arm64 `0x6A5E20`、Android armv7 `0x57B26E`、
  iOS arm64 `0x1001011EC`、iOS armv7 `0xFE322`。两个 64 位目标把
  `nextLayerId/state` 合成一次 64 位 store；Android armv7 用从前一槽开始的 `STRD`；
  iOS armv7 分成两个 32 位 store。

随后对以下完整 motionplayer 核心/NCB 代码范围做原始指令级复扫：

| 目标 | 扫描范围 | 精确成员位移 | 同时覆盖的 packed 形式 |
|---|---:|---:|---|
| Android arm64 | `0x690000..0x700000` | `+0xDC` | 从 `+0xD8` 开始的 64 位 load/store、pair load/store |
| Android armv7 | `0x560000..0x5C0000` | `+0x78` | 从 `+0x74` 开始的 `LDRD/STRD` |
| iOS arm64 | `0x1000F0000..0x100150000` | `+0xBC` | 从 `+0xB8` 开始的 64 位 load/store、pair load/store |
| iOS armv7 | `0xE0000..0x160000` | `+0x68` | 从 `+0x64` 开始的 `LDRD/STRD` |

扫描不仅匹配独立 32 位 load/store，还匹配 `ADD/ADD.W` 地址形成、AArch64 unscaled/
pair 形式、Thumb 16 位 `LDR/STR`、Thumb-2 `LDR.W/STR.W` 与相邻槽起始的双字访问。
所有非 ctor 候选逐条回到所属函数和基址：它们是 Player 自身从 `+0xD8/+0xB8`
开始的容器/渲染字段、MotionNode/EmoteEngine 字段、栈槽、20 字节数组元素或数据区误命中，
都不持有 ResourceManager 实例。

作为独立交叉检查，四端所有已恢复的 `Player_*`、`MotionNode_*`、`SourceCache_*` 主体也
重新反编译并按 typed field 名检索，覆盖数量依次为 227、237、235、236，四端
`layerIdState_guess` 命中数均为 0。加上 ResourceManager 主体/NCB wrapper 的完整复核，
当前四份参考足以把该槽收口为“构造期写 1、随后在完整插件代码范围 dormant、析构不处理”。
这仍不是对链接外未知代码的数学证明，原始字段名也仍未知；因此保留
`layerIdState_guess`，不根据旧 `libkrkr2.so` 记忆猜成 refcount、第二个 id counter 或
version。

Player 的 source 查找函数分别从 ResourceManager 的 `+0xE0/+0x7C/+0xC0/+0x6C`
读 spec：

- Android arm64 `MotionNode_findSource_guess @ 0x691CC8`；
- Android armv7 `0x570500`；
- iOS arm64 `0x1000F316C`；
- iOS armv7 `0xEF97C`。

因此中间初值 1 的槽绝不能误命名为 `_spec`。

## 内部容器 ABI

Android 两端使用旧 libstdc++ `_Rb_tree<uint32_t>`：

- arm64 node-base 32 B，value 位于 node `+0x20`，完整 node 40 B；set 48 B，inline
  header 位于 set `+0x08`，node count 位于 `+0x28`；
- armv7 node-base 16 B，value 位于 `+0x10`，完整 node 20 B；set 24 B，header 位于
  set `+0x04`，node count 位于 `+0x14`。

iOS 两端使用 libc++ tree：

- arm64 node 32 B，left/right/parent 为 `+0/+8/+0x10`，color 位于 `+0x18`，value
  位于 `+0x1C`；set/tree 24 B；
- armv7 node 20 B，left/right/parent 为 `+0/+4/+8`，color 位于 `+0x0C`，value
  位于 `+0x10`；set/tree 12 B。

四套 ABI 类型已写入相应 recovery IDB：`RbTreeUint*_*_guess` 或
`LibcppTreeUint*_*_guess`，并组成 `ResourceManager_{A64,A32,I64,I32}_guess` 尾布局。

## require 数据流与异常边界

四端都按 unsigned 32 位比较 set key。若当前 counter 已占用就递增并重查；溢出按
`uint32_t` 自然回绕。初始 `{0}` 只保证自然回绕时 0 会被跳过；它不是硬编码保留值。

选中空闲 key 后先执行 unique insertion，再读取 counter、加一、写回并返回旧值。因此：

- 普通路径从 1、2、3……递增；release 不回退 counter，不主动复用已删除 id；
- 在碰撞跳过期间 counter 已就地推进；若随后分配 node 抛异常，它停在已选中的空闲值；
- 若插入成功，最后一次 counter 加一按 32 位回绕；返回值保留同一 32 位 bit pattern，
  NCB 以 signed `tjs_int` 发布；
- 没有锁、原子或重入保护，容器和 counter 是单对象内的普通可变状态。

构造期 sentinel insertion 自身也可能分配失败。iOS 显式 ctor unwind 与 Android EH tail
都在失败时逆序销毁已完成的 random generator、module map 和 SourceCache 基类；只有
sentinel insertion 成功后才发布两个值为 1 的尾部状态。

## release 数据流与边界

四个 release 主体都先做精确 unsigned lookup。Android 端随后遍历 iterator 到 header
计算距离，再调用 `_M_erase_aux(first,end)`；iOS 端同样先遍历到 end 计数，然后从 first
重复 erase-at 直到 end。两套 STL ABI 的不同代码形状恢复到同一源级算法。

边界行为：

- miss：`first == end`，距离 0，erase 空范围，返回 0；
- 命中中间 key：删除该 key 以及所有更大的 key；
- `releaseLayerId(0)`：命中初始 sentinel 时清掉 sentinel 和当前所有正 id；
- 删除不会修改 `nextLayerId` 或 `layerIdState_guess`；
- signed 负参数先按 32 位 bit pattern 转成 unsigned key，例如 `-1` 查找
  `0xFFFFFFFF`；
- 计数器/返回计数都是 32 位；理论上极端容器规模的计数也按 32 位回绕。

## NCB 返回协议

四个 registrar 都把 require/release 的原生成员函数指针直接登记到 `Function` 描述符，
没有另设 raw callback。release 的 typed invoker：

1. 要求至少一个参数，额外参数不参与转换；
2. 将第一个 Variant 按普通 numeric conversion 取 32 位值；
3. 调用 native release；
4. 把 native `R0/W0` 作为 signed 32 位整数写入结果 Variant。

Android arm64 的公共一参数 invoker `0x6EBB14` 在返回处明确 `SXTW`；iOS/32 位端具有相同
typed-wrapper 实例化结果。这证明源级返回类型是 `tjs_int`，不是 `void`，也不是 64 位
`size_t`。Player 内部 release dispatch 传空结果槽，所以内部清理链会忽略该值；脚本直接
调用 `Motion.ResourceManager.releaseLayerId` 则能观察删除数量。

## 源码修复与测试

本轮修改：

- `ResourceManager.h`：set/counter 改成 `tjs_uint32`；补
  `_layerIdState_guess = 1`；release 返回 `tjs_int`；
- `ResourceManager.cpp`：保留 require 的 unsigned wrapping；release 恢复
  `distance + erase(first,end) + count return`；
- `motionplayer-dll.cpp`：新增确定性边界用例，覆盖连续分配、后缀删除与返回计数、miss、
  signed `-1` lookup、counter 不回退、`release(0)` 清 sentinel/all 与后续继续分配。

验证结果：

- Web Debug 全量目标重编译并链接成功；第二次构建为 `ninja: no work to do.`；
- 完整 `motionplayer-dll.cpp` 复用 Web Debug 的真实 Emscripten defines/includes/ABI
  参数做 `-fsyntax-only` 成功，唯一诊断为仓库既有 `_tss` deprecated warning；
- 四份 recovery IDB 在函数重命名、原型、ABI 类型、注释和反编译复核后均成功原位保存。

当前 Web preset 关闭 `ENABLE_TESTS`，因此没有可直接执行的 wasm Catch2 runner；这里如实记录
全目标编译/链接与完整测试翻译单元编译，不把 `-fsyntax-only` 冒充运行时执行。

## 仍未闭合

- `layerIdState_guess` 的原始字段名未知；四份参考的完整插件范围已证明它在构造写 1 后
  dormant，但不能用“无 consumer”反推一个具体业务名，继续保持 `_guess`。
- 析构树清理的各 STL helper 已确认，但本纵切面没有尝试把编译器私有模板 helper 全部恢复为
  原始 libstdc++/libc++ 符号。
- 本纵切面闭合不代表 motionplayer 整体完成；后续对象、容器、异常路径和平台分支继续逐项
  对四参考二进制恢复。
