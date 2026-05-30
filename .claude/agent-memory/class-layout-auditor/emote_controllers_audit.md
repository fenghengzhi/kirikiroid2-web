---
name: emote-controllers-audit
description: EmoteVarController(0x80) + EmoteAngleController(0x70) POD 实测布局审计 — ctor 0x667030/0x6867B0, step 0x666BF8/0x666634 字节级偏移表 + 两类 4 大本地偏差(var 堆数组超额分配 4x / var element 字段偏移注释错 / pack 强制不必要 / ctor 多清零)
metadata:
  type: project
---

# Emote Controllers 实测布局审计（2026-05-30, 反编译权威）

两类都是 EmoteEngine 内 `operator new(0x80)`/`operator new(0x70)` 堆分配的独立 POD（指针存 EmoteEngine+1072..+1120）。**无 vtable**：ctor +0 写的是 `std::deque._M_map`，不是 vptr。

## EmoteVarController（0x80=128B）
- ctor: `EmoteVarController_ctor_20Bdeque` @ **0x667030**
- step: `EmoteVarController_step` @ **0x666BF8**
- 字段（step 实测偏移）: +0..79 deque(libstdc++) | +80 int count | +84 int state | +88/+96/+104 三个 float* | +112 int powCount | +116 float phase | +120 float invDuration | +124 pad
- **deque element=20B**：step 读 `duration@+12`、`powCount@+16`（**不是 +4/+8**）。endValue/+0..+11 语义未追（method body）
- **关键**：三堆数组 = `operator new[](4*count)` 字节 = **count 个 float**（is_mul_ok(count,4)→4*count 字节），memset 4*count 字节；step 所有循环 `i<count`（非 count*4）；写 out 仅 count 个 float

## EmoteAngleController（0x70=112B）
- ctor: `EmoteAngleController_ctor_12Bdeque` @ **0x6867B0**（只初始化 deque header +0..79，**不碰 +80 之后**）
- step: `EmoteAngleController_step` @ **0x666634**
- 字段（step 实测）: +0..79 deque | +80 int state | +84 currentRad | +88 targetRad | +92 startRad | +96 invDuration | +100 int powCount | +104 phase | +108 pad
- **deque element=12B**：endRad@0, duration@4, powCount@8（本地正确）
- 无堆数组（单标量通道）
- EmoteEngine_ctor 对它：`memset(0x50)` → ctor → 仅写 `*(QWORD)(+80)=0` + `*(DWORD)(+88)=0`（即只清 +80..+91）。**+92..+111 在 operator new(0x70) 后是垃圾值，ctor 不清**。reset 路径无 memcpy seed（var 有 `memcpy(+88,&seed,4*count)`，angle 没有，special path）

## 本地实现 4 大偏差（cpp/plugins/motionplayer/）

1. **[严重] EmoteVarController.cpp:27-29 堆数组超额 4 倍**：本地 `new float[count*4]`（=16*count 字节），二进制 `new[](4*count)` 字节（=count 个 float）。且 step.cpp 循环用 `channelCount=count*4` 操作 4 倍数据 + 写 out 也 count*4 起点错。二进制 step 只操作 count 个 float、写 count 个 out
2. **[中] EmoteVarController.h element 字段偏移注释错**：标 duration@+4/powCount@+8，二进制 step 读 @+12/@+16。pad(uint64)@+12 实际占了 duration 的位置。20B 总大小对，但字段映射错
3. **[低] #pragma pack(1) 不必要**：{float,float,uint32,uint64} 自然对齐=24B，强制 pack 到 20B。二进制确为 20B（deque advance +20），故 pack 大小结果对，但应注释清楚 +0..+11 真实字段
4. **[低] ctor 多余清零**：本地两 ctor 显式清零所有 +80 后字段；二进制 angle ctor 不清 +92..+111（依赖 step 首次写）。功能更安全但非字节对齐。var ctor 二进制写 count@+80、state@+84=0 + 3 指针，其余 powCount/phase/invDuration/pad 未显式清（new(0x80) 不清零）— 本地全清

## 结论
- EmoteAngleController: ✅ 布局/vtable/字段全对齐（仅 ctor 多清零，无害）
- EmoteVarController: ⚠️ 布局字段偏移对，但**堆数组大小 4 倍偏差**（堆对象生命周期错）+ element 内部字段注释错
