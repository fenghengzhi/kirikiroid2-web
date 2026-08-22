# MotionPlayer compiled-source coordinate comment cleanup follow-up（2026-08-16）

## 目的

在 controller/header 与 host-object 两轮注释迁移之后，compiled motionplayer 源码仍
残留少量旧式反编译坐标。它们描述的运行语义大多正确，但把 vtable slot、native
subobject 地址、hash-node 内位移、Frida 字段坐标或某个 ABI 的 record offset 放在
可编译源旁边，容易被误读成四端共享的原始 C++ declaration。

本轮只迁移这些注释，不改变字段、类型、表达式、分支、所有权或构建结果。固定 POD
（例如 12B keyframe、20B var keyframe、wind emitter）用于验证真实 ABI 的
`static_assert`/`offsetof` 不在清理范围。

## 清理项

| 文件 | 移除的坐标式描述 | 保留的源级语义 |
| --- | --- | --- |
| `EmoteBlinkRng.cpp` | vptr 位于 object offset zero | virtual destructor 建立 polymorphic prefix；seed 求值顺序不变 |
| `EmoteEngine.cpp` | controller deque element 的 `16B/24B ptr@...`、hash node 中 `index +20` | owner pointer、category-specific labels、`EmoteVarRef::{type,index}` 路由 |
| `MotionTraceWeb.cpp` | Frida `node+52` | trace 的 `blendMode` 键实际发布 persistent `stencilType`；transform 无 accumulated blend field |
| `PlayerCore.cpp` | `*(vtbl+40)` | `PropGetByNum(MEMBERMUSTEXIST, i, ..., ObjThis=array)` 与 incremental partial commit |
| `PlayerResource.cpp` | atlas record 的 `+0x10/+0x08` | raw-node owner 后嵌 packer rect，rect 后 back-pointer 恢复 record，tail 延迟初始化 |
| `RuntimeSupport.cpp` | `native+8/+16` | `NativeInstanceSupport` 仅在 exact success 发布 `tTJSArrayNI::Items` subobject |

## 边界

- 没有把二进制坐标删除出分析记录；各纵切面的绝对地址、ABI offsets 和 native sizes
  继续保存在 `analysis/` 与四份 recovery IDB。
- 没有修改 `EmoteVarRef` 的两个 int32 字段、controller deque declaration order、
  atlas back-pointer、TJS Array owner/handoff 或 trace JSON schema。
- 没有更新 IDB；本轮没有产生新的反编译事实或符号，只把已经恢复的源语义与证据
  坐标重新分层。
