# Player colorWeight / independentLayerInherit / zFactor（四参考二进制，2026-08-26）

## 1. 范围

闭合 Player 表面 #28、#29、#32 的六个主 callback，并继续下钻：

- independentLayerInherit mismatch 的 iOS out-of-line deque body；
- zFactor 的共享 child-dispatch visitor；
- zFactor 捕获 double 的 child callback；
- Android 与 iOS 的 `std::function` capture/cleanup 形状。

本地实现与共同证据一致，本纵切面不修改运行 C++。

## 2. 主 callback 与字段

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| getColorWeight | `0x6CAAF0` | `0x5928F4` | `0x10011D4E4` | `0x11BE94` |
| setColorWeight | `0x6CAB04` | `0x59290C` | `0x10011D4F8` | `0x11BEAC` |
| getIndependentLayerInherit | `0x6D6B48` | `0x598FC2` | `0x100125680` | `0x1248A6` |
| setIndependentLayerInherit | `0x6C9DB4` | `0x5921E8` | `0x10011CB18 -> 0x10011CB2C` | `0x11B498 -> 0x11B4A6` |
| getZFactor | `0x6D6B5C` | `0x598FD0` | `0x1001256B0` | `0x1248D2` |
| setZFactor | `0x6B1D6C` | `0x5817B4` | `0x100109198` | `0x1069C4` |

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| independentLayerInherit byte | `Player+0x449` | `+0x2ED` | `+0x3D9` | `+0x2AD` |
| zFactor double | `Player+0x458` | `+0x300` | `+0x3E8` | `+0x2BC` |
| native packed colorWeight | `Player+0x484` | `+0x32C` | `+0x414` | `+0x2E8` |

## 3. colorWeight

getter 与 setter 都应用同一个 involution：

```cpp
uint32_t swapRedBlue(uint32_t v) {
    return (v & 0xFF00FF00)
         | ((v >> 16) & 0x000000FF)
         | ((v & 0x000000FF) << 16);
}
```

即交换 byte 0 与 byte 2，保留 byte 1 与 byte 3。脚本 word 与 renderer-native
packed word 跨过 R/B 交换边界；连续调用两次恢复原 bit pattern。

边界：

- 完整 32 位都保留，包括最高位和脚本侧负 Int32；
- setter 不 dirty、不触发 renderer、不验证 alpha/color 范围；
- getter 返回同一 32 位的 signed script Int32 解释；
- native 构造默认 `0xFF808080`，因 R/B 相等，脚本 getter 也返回同值。

## 4. independentLayerInherit

共同伪代码：

```cpp
bool getIndependentLayerInherit() const {
    return independentLayerInherit;
}

void setIndependentLayerInherit(bool requested) {
    if (requested == independentLayerInherit)
        return;

    for (MotionNode &node : flatNodes)
        node.delta.dirty = true;

    // deliberate: never writes independentLayerInherit
}
```

精确边界：

- public setter 不保存新值，所以不相等请求可重复 dirty 全部节点；
- 遍历包含 synthetic root 和每个 flat node；
- 只 dirty 当前 Player 的节点，不 unwrap/recurse child Player；
- 空 deque 安全 no-op；正常构造总有 synthetic root；
- 迭代使用每个 ABI 的真实 segmented deque end；
- 类型 3 child 初始化有另一条内部 compare/dirty/store 路径；那条路径才会更新 flag，
  不能据 public setter 反推 flag 永远不变。

## 5. zFactor 本地提交与传播

共同伪代码：

```cpp
void setZFactor(double value) {
    if (zFactor == value)
        return;

    zFactor = value;
    flatNodes[0].delta.dirty = true;

    visitChildPlayerDispatches([value](Player *child) {
        child->setZFactor(value);
        return true;
    });
}
```

共同 child visitor：

- 按当前 Player flat deque 顺序扫描所有节点；
- type 3：从 persistent child Variant 查询 Player adaptor/native；
- type 4：先读 particle Array count，但循环每次都取 numeric index 0，而不是 loop index；
- callback false 才停止；zFactor callback 固定返回 true；
- 没有 child null guard、cycle guard 或递归深度限制；
- 非 type 3/4 节点跳过。

因此第二个及后续 particle child 不会收到 zFactor；index 0 child 会被调用 count 次。
有限相同值在第一次递归后让后续重复调用快速返回；NaN 因 `NaN != NaN`，会让同一
index-0 child 及其子树被完整重复传播 count 次。带环 child 图会无限递归。

+0/-0 比较相等：若当前为 +0，set(-0) 是 no-op，字段 sign bit 和 child 状态均不变。
NaN 每次都提交、dirty 和传播。

## 6. owner 与异常前沿

四端在遍历前都已：

1. 写本 Player zFactor；
2. dirty synthetic root。

这些写入永不 rollback。

Android 两端的 type-erased callback 显式 `operator new(8)` 保存 captured double，
manager callback 在退出时 scalar-delete；capture allocation 失败发生在本地提交后、
任何 child 更新前。iOS libc++ 两端把 vtable pointer + double 放进
`std::function` small buffer，没有对应 8-byte heap allocation。

visitor 中任一 Variant→Object、adaptor lookup、Array access 或递归 child setter
抛异常时：

- parent 本地提交保留；
- 已访问 child 的提交保留；
- 当前/后续 child 不保证更新；
- type-erased callback 仍沿 ABI 对应 RAII cleanup 销毁。

四端 child callback 地址：

`0x6F0F60 / 0x5AE580 / 0x100143550 / 0x14441C`，均执行
`child->setZFactor(captured)` 后返回 true。

## 7. 本地与验证

本地：

- `swapPackedRedBlue_guess` 与位运算完全一致；
- public independent setter 保留“不写 flag”的反常边界；
- `setZFactor` 先本地提交，再复用同一个
  `visitChildPlayerDispatches_guess`；
- visitor 已保留 type-4 重复 index 0 bug。

现有单元用例覆盖 packed color words、independent 重复 dirty、type-3 内部 commit、
zFactor equality/-0/NaN，以及 particle index-0 传播。当前工具链缺失，未在本轮实际
执行。2026-08-27 已由
`motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md` 补齐
capture allocation、四端 manager/vtable、normal destroy 与 local landing 差异；本项现为
`IMPLEMENTED`。
