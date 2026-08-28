# MotionNode / PreparedRenderItem / deque 生命周期四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制共同证明，`PreparedRenderItem` 不是 prepared-list 的所有物，而是每个
`MotionNode` 独占、延迟创建并跨帧复用的裸指针对象。main/aux/child list 只借用该指针；唯一
释放点是 `MotionNode::~MotionNode()`。普通旧树替换通过
`nodes.erase(next(nodes.begin()), nodes.end())` 销毁所有非根节点，随后清 raw-label map；
构造时保留的 synthetic root 则在 `Player::~Player()` 的显式 `nodes.clear()` 中销毁。

本轮纠正两条旧生命周期结论：

1. `Player` 析构不是在普通成员逆序析构的最后阶段才销毁 root。它在删除
   `SeparateLayerAdaptor` 并清空裸指针槽后，立刻显式 `nodes.clear()`；稍后的 deque member
   destructor 只面对空容器并释放残留 block/map storage。
2. `Player` 在参数 vector 清空后、旧树 reset 前，还显式
   `variableLabelScopes.clear()`；该 clear 销毁元素但保留实现定义的 deque 容量，最终 member
   destructor 再释放底层存储。

本地 `MotionNode` 的唯一 owner、PreparedRenderItem 成员声明顺序、后缀 erase 和 label-map clear
已经与四端一致；`Player::~Player()` 缺少上述两个显式 clear，且注释把 root 销毁时点写反。本轮
应补齐这两步并就地纠正旧注释。

## 2. 四端函数映射

### 2.1 Player 析构入口

| 平台 | Player destructor | 完整指令 | 说明 |
|---|---:|---:|---|
| Android arm64 | `0x6CCEBC` | 311 | 主体含 landing/cleanup 链 |
| Android armv7 | `0x593C24` | 99 | libstdc++ deque helpers |
| iOS arm64 | `0x10011F2A0` | 101 | libc++ deque helpers |
| iOS armv7 | `0x11DCC4` | 175 | SjLj 主体；cleanup `0x11DED6`，108 条 |

四端均在本轮 fresh decompile，并完整读取 311/99/101/175 条 disassembly。iOS armv7 的
108 条 cleanup dispatcher 也 fresh decompile；它按 call-site selector 执行尚未销毁成员的
逆序清理，最终进入 `clang_call_terminate`，符合 destructor 的 noexcept 异常边界。

### 2.2 旧树 reset 与非根后缀 erase

| 平台 | reset | 指令 | range erase |
|---|---:|---:|---:|
| Android arm64 | `0x6B2AD8` | 244 | `0x6F11EC`，274 条 |
| Android armv7 | `0x581F3C` | 212 | wrapper `0x592F18` -> `0x5AE7A8`，221 条 core |
| iOS arm64 | `0x100109ACC` | 221 | `0x10011DDB8`，303 条 |
| iOS armv7 | `0x107358` | 312 | `0x11C6B4`，337 条；reset cleanup `0x1076DA`，64 条 |

四端 reset 和 range-erase instantiation 均 fresh decompile；对应指令总数通过 fresh full-disasm
计数核对。`releaseLayerId` 的普通字符串索引在四库均为空；按宽字符串流程补做 UTF-8、
UTF-16LE、UTF-32LE 原始字节搜索后，四端各得到唯一 UTF-16LE 字面量：

| 平台 | UTF-16LE `releaseLayerId` | reset xref |
|---|---:|---:|
| Android arm64 | `0x14D5A72` | `0x6B2AD8` |
| Android armv7 | `0xD85592` | `0x581F3C` |
| iOS arm64 | `0x10195BF20` | `0x100109ACC` |
| iOS armv7 | `0x174E284` | `0x107358` |

每个命中都读取前后原始字节，确认前一字符串终止符、28-byte UTF-16LE 内容和双零终止符；
ASCII/UTF-8 与 UTF-32LE 均无命中，所有分页 cursor 均完成。该结果说明原先空 `find` 只是
字符串列表/编码差异，不是 release 机制缺失。

### 2.3 显式 clear、最终 deque destructor 与 MotionNode destructor

| 平台 | `nodes.clear()` | 最终 deque dtor/storage | `MotionNode::~MotionNode()` | PreparedRenderItem dtor |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6F174C`，65 | `0x6CCD94`，67 | `0x6F206C`，92 | `0x6F21DC`，37 |
| Android armv7 | `0x593EFC`，24 -> `0x5AEAAC` | `0x593BA8`，38 | `0x5AF220`，50 | `0x5AF2D0`，28 |
| iOS arm64 | `0x10012A38C`，64 | `0x10012A344`，18 | `0x10012A48C`，69 | inline in node dtor |
| iOS armv7 | `0x129004`，65 | `0x128FDC`，15 | `0x1290A6`，68 | inline in node dtor |

以上函数均 fresh decompile/disasm。Android armv7 的 24 条 wrapper 把当前 end iterator复制到栈上，
再调用 `0x5AEAAC` 销毁 `[begin,end)` 并更新 finish；不能因 Hex-Rays 丢失 wrapper 参数就把它
误判为空操作。iOS 两端的最终 deque destructor 都先再次调用 clear，再释放保留的 block 和
map storage；Android 两端由 libstdc++ 的 element destroy 与 deque-base storage destructor分工。

## 3. 四端共同源码伪代码

### 3.1 Player 析构

```text
Player::~Player():
    purgeParameterRampMap()
    parameterEntries.clear()
    variableLabelScopes.clear()

    resetAndReleaseOldNodeTree()

    if renderSeparateLayerAdaptor != null:
        renderSeparateLayerAdaptor->~SeparateLayerAdaptor()
        operator delete(renderSeparateLayerAdaptor)
        renderSeparateLayerAdaptor = null

    nodes.clear()

    // compiler-generated reverse member destruction starts here
    // variableLabelScopes dtor sees an empty container
    // ... later post-node maps/vectors are destroyed ...
    // nodes dtor sees an empty container and releases retained storage
    // nodeLabelMap is destroyed last among these early members
```

`variableLabelScopes.clear()` 与 `nodes.clear()` 都在 explicit destructor body；它们出现在任何
later-declared member 的普通逆序析构之前，不能解释成编译器自动析构。四端相同的相对调用顺序
提供了共享源码结构证据。

### 3.2 reset 与非根后缀擦除

```text
resetAndReleaseOldNodeTree():
    retain one ResourceManager dispatch for the entire operation
    visit node-owned type-4/type-3 child variants and Invalidate each
    reset every eval-cascade live value and cached node-pointer vector

    for index in [1, nodes.size()):
        releaseLayerId(nodes[index].layerId1)
        releaseLayerId(nodes[index].layerId2)
        item = nodes[index].preparedRenderItem
        if item != null and item.rawFlag20:
            releaseLayerId(item.renderLayerId)

    if nodes.size() > 1:
        nodes.erase(next(nodes.begin()), nodes.end())
    nodeLabelMap.clear()
```

每次 `releaseLayerId` 都通过 retained receiver 调用 TJS，返回码被忽略。第三次 release 只由
`item != null && rawFlag20` 门控；不检查 layer-id 数值，也不检查另一个 draw byte。任一 dispatch
异常发生在 suffix erase 前，旧树和 item 仍保持发布状态，供异常处理/重试边界观察。

### 3.3 MotionNode 与 PreparedRenderItem 销毁

```text
MotionNode::~MotionNode():
    if preparedRenderItem != null:
        destroy PreparedRenderItem members in reverse declaration order:
            meshPoints
            commandBezierPatchPoints
            commandCompositeMeshPoints
            composedLayer
            leafLayer
            commandVariant
            commandKey
            childItems                 // pointer elements are borrowed
            commandSrc
            ownerLabel
        operator delete(preparedRenderItem)
        preparedRenderItem = null

    destroy MotionNode suffix members in reverse declaration order
```

64 位 item 的三个 vector 位于 `+0x190/+0x178/+0x158`，三个 Variant 位于
`+0x144/+0x130/+0x11C`；32 位对应 `+0x138/+0x12C/+0x11C` 和
`+0x110/+0x104/+0xF8`。这些地址只用于证据对照。四端共同的 reverse order 与本地
`NativePreparedRenderItemState` 声明顺序一致。`childItems` 的元素从不递归 delete；它借用其他
MotionNode 持有的 item。

节点槽只在完整 item member destruction 和 `operator delete` 之后清零。随后才销毁节点自己的
stencil mask vector、child Variant、字符串/Variants、mesh vectors、双 frame slot 等成员。

## 4. range erase 的搬移分支与实际后缀边界

四套都是标准库的通用 `deque::erase(first,last)` instantiation，因此二进制中确实同时存在：

- 根据 erased range 前后元素数选择靠前或靠后搬移；
- 对 MotionNode 执行 compiler-generated memberwise copy assignment；
- 销毁被缩短一侧的元素并回收多余 block。

这解释了为什么 native MotionNode 类型必须保持可复制，以及裸 `preparedRenderItem` 会被默认浅拷贝。
把 copy/assignment 删除，或改成 `unique_ptr`，都会改变模板可实例化性和通用 erase 的所有权边界。

但 reset 的实际参数恒为 `[begin+1,end)`：

```text
erasedCount = size - 1
elementsBefore = 1
elementsAfter = 0
choose back/suffix branch
move(last, end, first) has an empty source range
destroy [begin+1, end)
new size = 1
```

因此这条正常路径不会执行任何 MotionNode copy assignment，不会把 root 的裸 item owner复制到别的
节点，也不会产生双重释放。通用 helper 的其他调用若传入中间区间，浅拷贝 owner 的危险仍是原生
sharp boundary；本地不应为“安全”而改变类型。

## 5. 平台/标准库差异

### 5.1 Android libstdc++

- MotionNode 大于 512 bytes，因此 deque block capacity 是 1：arm64 stride 2632，armv7 stride
  2272，每个 map slot 指向一个 node block。
- suffix erase 逐节点调用 MotionNode dtor，并释放 start/finish 之间多余 block。
- explicit `clear()` 保留 start block；最终 deque/base destructor 在普通成员逆序阶段释放剩余 block
  与 map storage。
- arm64 destructor 的 311 条流包含异常 landing cleanup；armv7 生成更紧凑的 helper 调用链。

### 5.2 iOS libc++

- block capacity 都是 16：arm64 `2648 * 16 = 42368`，armv7
  `2228 * 16 = 35648`。
- clear 先逐元素析构并把 size 置零，然后从前端释放 block，直到 map 中只保留实现允许的一到两个
  spare block；当剩两个 block 时 start offset 写 16，剩一个时写 8。
- 最终 deque destructor 对空容器再次 clear，释放全部保留 block 和 map allocation。
- armv7 使用 SjLj。reset cleanup 会销毁当前临时 Variant/函数对象并 release retained RM 后 resume；
  destructor cleanup 继续清理尚存成员，最后 terminate。

这些是 ABI/STL 产物差异；共享源码仍是相同的 `clear`、suffix `erase` 和普通 deque destructor。

## 6. 本地逐行对照与本轮修正

### 6.1 已匹配

- `PlayerMotionLoad.cpp::resetAndReleaseOldNodeTree_guess`：retain RM、child invalidation、cascade reset、
  两个必选 release、item/rawFlag20 第三 release、最后 suffix erase + label-map clear，与共同伪代码一致。
- `RuntimeSupport.cpp::eraseNonRootNodesAndClearLabelMap_guess`：精确使用
  `erase(next(begin), end)`，不是逐个 pop 或交换新 deque。
- `MotionNode.h`：保留 user-declared destructor 与 default copy/copy-assignment，且注释明确正常 suffix
  erase 不走 relocation branch。
- `RuntimeSupport.cpp::MotionNode::~MotionNode`：`delete preparedRenderItem; preparedRenderItem=nullptr;`
  让 PreparedRenderItem 逆序 member destruction和 free先发生，再清节点槽，再自动销毁节点 suffix。
- `RuntimeSupport.h::NativePreparedRenderItemState`：拥有成员声明顺序使默认析构严格产生四端共同
  reverse order；portable derived sidecar均为 trivial value，不增加所有权。

### 6.2 修正前偏差

`PlayerCore.cpp::Player::~Player` 修正前在 `_parameterEntries.clear()` 后直接 reset，缺少
`_variableLabelScopes.clear()`；删除 adaptor 后直接退出析构体，缺少 `_nodes.clear()`。同一处注释还称
root“稍后由 deque member destructor 销毁”，被四端显式 clear 调用顺序证伪。

### 6.3 目标实现

```cpp
_parameterEntries.clear();
_variableLabelScopes.clear();
resetAndReleaseOldNodeTree_guess();
delete _renderSeparateLayerAdaptor;
_renderSeparateLayerAdaptor = nullptr;
_nodes.clear();
```

该改动恢复 source-level 调用顺序，不复刻任何平台字节 offset，也不手工模拟 deque block 策略。
标准库会按当前 wasm/libc++/libstdc++ 实现处理容量，这属于不可要求跨 ABI 相同的实现细节；容器选型、
元素销毁时点、唯一 owner 和异常边界则保持与四端共享源码一致。

## 7. 验证边界

- `git diff --check` 已通过；coverage TSV 所有非空行均为严格 12 列；静态源码核对确认
  `parameterEntries.clear -> variableLabelScopes.clear -> reset -> adaptor delete/null -> nodes.clear`
  的顺序。
- 当前环境只有 `/usr/bin/clang++`，缺少 CMake/Ninja/Emscripten，且单文件语法检查被仓库依赖
  头阻断（本轮最先缺失 `spdlog/spdlog.h`；此前更完整 include path 仍缺 `boost/locale.hpp`）；
  不存在既有 `out/web/debug` 或 `build`，不能把静态检查包装成正式构建或测试。
- 没有现成 fixture 能单独观察 destructor 内部 deque spare-block 状态；按项目规则不捏造物料。
- 语义修正仍有四端 fresh evidence 支撑，不因 oracle-inert 而延后。
