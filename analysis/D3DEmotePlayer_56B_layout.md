# D3DEmotePlayer native instance 内存布局 (56B / 0x38, libkrkr2.so ARM64)

> 权威来源：`sub_52FFBC` (0x52FFBC, "clone" 回调 = 真正的实例构造器) /
> `sub_533C00` (0x533C00, 实例析构器) 反编译。
>
> **归属说明**：libkrkr2.so（NDK clang/ARM64）字节几何，仅作反编译对照用，**不约束 wasm 本地实现**。
> wasm32 ABI 下偏移必然不同（可接受）。复刻目标是源代码结构/对象生命周期，不是字节偏移。
> 本地 `D3DEmotePlayer` 应是**普通 C++ 类**（带继承 vtable、`EmoteObject* primary/secondary` 字段、
> clear/load 方法），让编译器算偏移——这正是 libkrkr2.so 源码的样子（56B 是其 ARM64 编译产物）。

## 对象大小

- **56 字节 (0x38)**：`sub_52FFBC` / `sub_533C00` 均 `operator new(0x38)` / 按此大小析构。
- 是个**有虚函数的 C++ 类**：+0 是 vtable，构造中途 vtable 从 off_19FE050 切到 off_19FE020
  （基类 ctor → 派生类 ctor 的编译器行为，0x52fff4 / 0x53002c）。

## 字段表（构造 sub_52FFBC 写入 + 析构 sub_533C00 释放，双向确认）

| 偏移 | 类型 | 语义 | 证据 |
|---|---|---|---|
| +0  | ptr | **vtable**：off_19FE020(active) / off_19FE050(base) | 0x52fff4 / 0x53002c |
| +8  | ptr | **parentDispatch** (a2 传入的 parent dispatch) | 0x52fff4 |
| +16 | u64 | **packedTypeTag** = 0xBF00000000000008（低 32 位 = 类型标记 8） | 0x52fff8 |
| +24 | ptr | **primarySlot** EmoteObject\*：load/clone 写，clear 拆 | 构造 0x53003c = sub_67F978(...)；析构 a1[3] |
| +32 | ptr | **secondarySlot** EmoteObject\*：保留槽，生命周期主链恒 null | 构造 0x530024 清零；析构 a1[4] 0x533c14 |
| +40 | float | **scaleX** = 1.0f (0x3F800000) | 0x530028 (0x3F8000003F800000 低半) |
| +44 | float | **scaleY** = 1.0f (0x3F800000) | 0x530028 (高半) |
| +48 | word | **flags**，init 0 | 0x530030 |

## +32 次槽裁定（此前阻塞对象生命周期对齐的开放问题）

次槽是**真实字段**（析构 sub_533C00 销毁 a1[4]），但在**全部已反编译生命周期路径**中只被写 0：
- 构造 sub_52FFBC：`+32 = 0`（0x530024），此后只 `+24 = new`（0x53003c），**不写 +32**
- load (0x52FDD4)：拆 +24/+32，只重建 +24
- create/"clear" (0x52FD84)：拆 +24/+32，置 null
- destroy (0x533C00)：只读 +24/+32 销毁

结论：logo 用例下为**二进制保留但不激活的退化单实例**。本地建模为真实槽位默认 null（非删除、非急建）。
范围诚实：覆盖脊柱全部入口（construct/load/clear/destroy）；脊柱外的隐藏写入者未排除，但不在此链。

## NCB 成员名/回调错位（D3DEmotePlayer_ncb_registerMembers 0x52E504）

二进制故意的 name↔callback 别名（非 bug）：
- `clear` → D3DEmotePlayer_create（0x52e680）
- `queing` → setBustScale/getBustScale
- `bustScale` → setBodyScale/getBodyScale
- `setTimelineBlendRatio` → D3DEmotePlayer_setTimeline
- `modified` → getPlayCallback（RO prop）
- `pass` → addPlayCallback

## 对象生命周期脊柱

```
D3DEmotePlayer 56B  +24/+32 → EmoteObject 40B  +8 → EmoteEngine 1496B → Player 1384B
                                                          node-deque(2632B/elem) → ClipSlot[2](536B)
```
EmoteObject/Player/node/slot 各层布局见 EmoteObject_40B_layout.md / MotionNode_2632B_layout.md /
ClipSlot_536B_layout.md。
