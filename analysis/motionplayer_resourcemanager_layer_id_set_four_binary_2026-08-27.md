# ResourceManager Layer ID set 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::requireLayerId` / `releaseLayerId` 共同使用一个
`std::set<std::uint32_t>`、一个单调并按 32 位自然回绕的 next counter，以及一个紧邻但不被这两个
callback 读取的 retained state word。构造器先执行 `new Math.RandomGenerator()`，再向空 set 插入
sentinel 0，最后把 next/state 连续写成 1/1。

`requireLayerId` 跳过从 next 开始的已占用 key，成功插入当前 key 后返回其旧值并 post-increment；
`releaseLayerId` 不是删除单个 key，而是精确查找 unsigned key 后删除 `[key,end)` 完整后缀，并返回
删除节点数。miss 是空 range no-op；`release(0)` 删除 sentinel 及全部更大 ID，但 counter 永不回退。

本地容器选型、unsigned 比较、suffix erase、返回转换和异常边已经匹配。本轮只把 require 中的
临时 `id` 收紧成四端共同显出的 `insert(_nextLayerId); return _nextLayerId++;` source shape；两种写法
运行结果相同，但后者更直接复原四编译产物共同的数据流。

## 2. 四端函数映射

### 2.1 callbacks

| 平台 | `requireLayerId` | 指令 | `releaseLayerId` | 指令 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6A8A74` | 47 | `0x6A8B30` | 51 |
| Android armv7 | `0x57C258` | 43 | `0x57C2C8` | 49 |
| iOS arm64 | `0x100102D40` | 30 | `0x100102DB8` | 52 |
| iOS armv7 | `0x100240` | 32 | `0x10028A` | 47 |

八个 callback 均在本轮 fresh decompile，并完整读取对应 disassembly。它们是 registrar 直接发布的
typed native method body，不存在另一个 ResourceManager-specific forwarding layer。

### 2.2 constructor / destructor 容器生命周期

| 平台 | native constructor | 指令 | native destructor | 指令 |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6A5CAC` | 177 | `0x6A5F74` | 126 |
| Android armv7 | `0x57B1EC` | 63 | `0x57B2E4` | 21 |
| iOS arm64 | `0x100101158` | 44 | `0x10010126C` | 24 |
| iOS armv7 | `0xFE254` | 93 | `0xFE3B4` | 22 |

四端 constructor/destructor 均 fresh decompile/disasm。Android arm64 destructor 与
`unloadAll` internal entry 位于同一 IDA function range，但析构入口仍清楚显示：显式清 module map，
逆序销毁 layer-id set、RandomGenerator Variant、module map storage 和 SourceCache base。其余三端为
独立紧凑 destructor。

iOS arm64 constructor unwind helper `0x10010120C` 与 iOS armv7 SjLj helper `0xFE344` 也 fresh
decompile：若 RandomGenerator evaluation 或 sentinel insertion 抛出，已构造成员按逆序销毁，
1/1 两个 state word尚未发布。

## 3. 构造、析构与内部容器

共同 source-level 构造顺序为：

```text
construct SourceCache base
default-construct loadedModules
default-construct randomGenerator Variant
default-construct usedLayerIds as empty std::set<uint32_t>

randomGenerator = eval("new Math.RandomGenerator()")
usedLayerIds.insert(0)
nextLayerId = 1
layerIdState = 1
```

关键边界：

- RandomGenerator 脚本求值先于 set 的首次节点分配；脚本异常时 set 仍为空。
- sentinel insertion 可能分配/抛异常；失败时 next/state 两个 1 都尚未写入。
- 两个 1 在 arm64 两端可合并为一个 64-bit store，在 32 位端表现为两个 word store；这是 ABI/优化
  差异，不是一个 64-bit 源字段。
- set node 在 ResourceManager 普通逆序 member destruction中释放；key是 trivial uint32，不拥有额外
  资源。

Android 两端是 libstdc++ 红黑树，iOS 两端是 libc++ `__tree`。header/root/end 的布局和 helper
名字不同，但四端都明确是 ordered unique set，不是 unordered set、vector、bitset或空闲链表。

## 4. requireLayerId 共同伪代码

```text
tjs_int requireLayerId():
    while usedLayerIds.find(nextLayerId) != usedLayerIds.end():
        ++nextLayerId
    usedLayerIds.insert(nextLayerId)
    return nextLayerId++
```

### 4.1 顺序与异常

- 每个 occupied candidate 都重新执行 tree find；不是一次 lower_bound 后扫连续节点。
- counter 是 unsigned 32-bit；`++` 在 `0xFFFFFFFF` 后自然回绕到 0。
- insert发生在 post-increment前。node allocation/insert抛异常时 counter仍指向 candidate，set未增加
  新 key，异常直接向 typed invoker传播。
- insert成功后 counter一定递增，即使返回值在脚本侧按 signed `tjs_int` 显示为负数。
- 初始 sentinel只保证 counter第一次不会发出0。`release(0)`移除它后，counter不会立即变0；只有将来
  自然回绕且0仍空闲时才会发出0。
- 如果理论上全部 `2^32` 个 key均被占用，while永不终止；没有 full-set检测。

### 4.2 并发边界

函数无 mutex/atomic。并发 require/release 或脚本重入修改同一 native对象会形成标准容器和counter
data race；参考实现没有用“更安全”的锁、CAS或重复插入结果检查来修复。

## 5. releaseLayerId 共同伪代码

```text
tjs_int releaseLayerId(tjs_int id):
    first = usedLayerIds.find(static_cast<uint32_t>(id))
    erased = distance(first, usedLayerIds.end())
    usedLayerIds.erase(first, usedLayerIds.end())
    return static_cast<tjs_int>(erased)
```

### 5.1 精确边界

- lookup使用输入 32-bit 位模式的 unsigned解释；负 `tjs_int`可命中 `>=0x80000000` 的 key。
- 命中时删除目标 key本身及全部更大 key。释放早期 ID 会级联失效后来分配的 ID。
- miss 时 first=end、distance为0、erase(end,end)为空操作，返回0。
- `release(0)`清空整个 set，包括 sentinel；不会重新插入0，也不会改 next/state。
- erased count先按 iterator difference累计，再窄化为 `tjs_int`。极端超过 signed 32-bit 的结果保留
  低32位边界；没有饱和或异常。
- key和比较器析构均不抛；正常erase只进行tree rebalance、node delete和size更新，不分配。

### 5.2 STL 差异

- Android libstdc++ 先走 exact find/lower-bound展开，遍历 successor计算distance，然后调用
  `_M_erase_aux(first,end)`。
- iOS libc++ 先调用独立 find helper，遍历 successor计数，再循环调用 single-node erase直到end。
- iOS miss被优化为直接返回0，Android仍可调用空 range helper；共享源码仍是同一
  `distance + erase(first,end)`。

## 6. 本地逐行对照

### 6.1 已匹配

`ResourceManager.h` 当前保留：

```text
tTJSVariant randomGenerator
std::set<tjs_uint32> usedLayerIds
tjs_uint32 nextLayerId
tjs_uint32 layerIdState
```

`ResourceManager.cpp` 的两个 constructor 都先初始化 RandomGenerator，再 insert(0)，最后写1/1；
destructor显式清 module map，set/Variant/map/base随后按声明逆序自动析构。该生命周期与四端一致。

`releaseLayerId` 当前逐行对应 exact unsigned find、distance、suffix erase和signed return cast，无需
修改。它没有把release简化成 `erase(id)`，也没有重置counter。

### 6.2 require source-shape 收紧

修正前：

```cpp
const auto id = _nextLayerId;
_usedLayerIds.insert(id);
++_nextLayerId;
return static_cast<tjs_int>(id);
```

目标：

```cpp
_usedLayerIds.insert(_nextLayerId);
return static_cast<tjs_int>(_nextLayerId++);
```

两者保持相同的 find、insert、throw frontier、wrap和return位模式；目标写法直接还原四端都呈现的
“insert member lvalue -> load old member -> store old+1 -> return old”数据流，移除不必要的本地
snapshot变量。

## 7. 验证边界

- 本轮完成八个 callback和四套 constructor/destructor的fresh evidence；`git diff --check`已通过；
  NCB生成器通过`py_compile`，正式输出和独立临时目录重生成逐字节一致；主台账/原生证据TSV
  分别严格为18/12列，覆盖表所有非空行严格为12列。两个callback状态已把全局分布从
  pending/implemented `110/20`推进到`108/22`。
- 当前环境缺少CMake、Ninja、Emscripten与既有build输出；standalone syntax check被依赖头缺失
  阻断，不能宣称正式unit/Web build。
- 无需构造新fixture：现有语义改动运行等价，set的极端2^32边界也不能靠伪造物料代表真实oracle。
