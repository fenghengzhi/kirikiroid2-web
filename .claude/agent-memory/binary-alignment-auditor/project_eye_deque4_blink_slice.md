---
name: eye-deque4-blink-slice
description: 2026-06-03 独立审计 EmoteEngine eye/deque#4 blink 垂直切片对齐结论（ctor/step/RNG/buildEyeControl 全 ✅）
metadata:
  type: project
---

EmoteEngine eye/deque#4 (TYPE 4) blink 物理垂直切片 — 2026-06-03 独立 fresh-decompile 审计结论：✅ 逐行架构对齐。

**已 fresh-decompile 确认对齐的二进制↔本地映射：**
- `EmoteBlinkController_ctor` @0x662968 ↔ EmoteBlinkController.cpp:89 — 标量字段 +328..+360 全对齐；blinkTimer=min+(max-min)*rand；edge/node 表用 (float)(int)sub_6637BC coerce
- `sub_663BDC` step @0x663BDC ↔ EmoteBlinkController.cpp:198 — 值轨道+blink(case 0/10/11/12)+末端 remap 三段 1:1
- `sub_9F1A08` RNG init @0x9F1A08 ↔ EmoteBlinkRng.cpp:84 — MT19937 quirk 递推 (v2+1)+1812433253*(...)-3
- `sub_9F17D0` RNG next @0x9F17D0 ↔ EmoteBlinkRng.cpp:113 — 单调用消费2 word, left L→L-2, canonical-real low|((high>>18^high)&0xFFFFF)<<32|0x3FF..0, -1.0
- `EmoteEngine_buildEyeControl` @0x66C77C ↔ EmoteEngine.cpp:583 — enabled&1 门, new(0x170), push 16B{ctl,label}, HM6[label]={type=4,index=v5}
- progress deque#4 step+HM7 @0x67d0a4 ↔ EmoteEngine.cpp:717 — 16B 步幅, label@+8, HM7[label]=(double)out

**经反编译确认的关键架构事实（防未来误判）：**
- 末端 remap (0x663fa0): 分子 (float), 除数 (double)(endFrame-beginFrame) — float/double 混用是真实的, 不可全 float 简化
- RNG lowerMask = 0x7FFFFFFE (非 0x7FFFFFFF) — 二进制 AND #0x7FFFFFFE 确认
- HM6 (+1384) value = {int32 type@+0; int32 index@+4}, NOT double; index 是 loop 变量 v5 (enabled 跳过仍递增), NOT deque.size()
- sub_6637BC 返回 i32 (case4 int, case5 (int)double), 二进制随后 (float)(int)
- RNG seed = steady_clock::now()/1000000 (sub_A2BDBC) → 非确定性是设计使然, 解释无 fixture

**诚实的 open 边界（不阻塞 blink 对齐）：**
- sub_661F7C→sub_660028 (1925行 mesh resolver) 未移植: 重建 8B 值轨道。轨道空时 step 走「empty→state0/跳 blink」路径, 与二进制空轨道路径一致, blink 状态机+remap 仅依赖 +328..+360 标量(ctor 填好), 不受影响。+288 trackResolvedSpan 由 resolver 写, 未移植时保持 0 = ctor 默认。

---
**CORRECTION (2026-06-03, commit 2316276):** this slice was NOT zero-deviation. trackPow(+324) was ported as int32_t + static_cast<float>, but the binary loads it as *(float*) (raw float bits, DWORD copy from keyframe[+8], no SCVTF). The mouth slice disasm audit caught it; fixed to float + memcpy. Lesson: scrutinize int-vs-float-bits on every pow/curve field (LDR S without SCVTF = raw bits).
