---
name: m2-emoteengine-deque4-slice
description: M2 EmoteEngine deque#4 物理/状态机子系统 — 类布局更正 + population 路径真相（setVariable 不是填充侧）+ step 拓扑
metadata:
  type: project
---

M2 EmoteEngine deque#4（"eye" 类目状态机）垂直切片调查（2026-06-03，fresh decompile）。
**Why:** 旧 memory（module_motionplayer §A/§E 与 EmoteEngine.cpp:615 注释）称"deque 由 setVariable 用 binary-typed POD 填充"——**已被反编译证伪**，就地更正。
**How to apply:** 下次推进 M2 任何 deque（#4/#5/#6/#8/#9/#10）前先读本条，避免重走 setVariable 死路。

## 类布局更正（关键）
- deque#4 @engine+256 的**元素不是 `char raw[16]` 不透明 POD**，而是 `{ EmoteVarController4* ctl @+0; ttstr key @+8 }`（16B = 8B 指针 + 8B ttstr handle，ARM64）。EmoteEngine.h:179 的 `EmoteStateMachine16B_Deque4{char raw[16]}` 在语义上是错的（应是 ptr+ttstr 对），但字节宽度巧合相同。
- progress @0x67D01C 的 6 个 step 循环全部是"指针 deque"：`v15 += 2`（#4，16B）、`v23+=2`（#5）、`v30+=3`（#6，24B=ptr+2key）、`v38+=6`（#9，48B）、`v45+=3`（#8，24B）、`v52+=2`（#10，16B）。每次 `Player_HM2_upsert_labelToValue(engine+1440, elem+1)` 用 elem+8 的 ttstr 当 key 写回 HM7。block 链表节点尾 `+8` 指向下一 block。
- step 真正操作的状态机字段（+296 phase/+300 frame/+304 target/+308 v/+312 length/+316 lengthDone/+320 speed/+324 exponent/+336 substate/+352/+356/+360）都在**被指向的 controller 堆对象**里，不在 deque 元素里。

## step 侧 sub_663BDC（deque#4 controller step，已完整反编译）
`sub_663BDC(controller* a1, float* out, float dt)`。controller 内部有两条嵌套 deque：
- deque-A @+16/+24/+32/+40/+72：12B 元素 `{float a, float b, int c}`，504B block（42 elem）——pending keyframe 队列。
- deque-B @+96/+104/+112/+120/+152：8B 元素，512B block——`{p0,p1}` 关键帧。
状态机 phase `*(a1+296)` ∈ {0,1,2}：2=插值中(pow-ease 公式 @0x663d50)，1=取下一 deque-A 项，0=取下一 deque-B 项设新段。尾部 substate `*(a1+336)` ∈ {0,0xA,0xB,0xC} 二级状态机（blink/transition 计时，含 sub_9F1A08/9F17D0 随机数）。输出写 `*out`。

## population 侧真相（setVariable 不是填充侧！）
1. **setVariable @0x671228 = 驱动侧不是填充侧**：它对 key 算 ttstr-hash → `sub_6887F4(emote+1384, hash%emote+1392, key)` 查**已存在**的 HM 项；读 `*(result+16)` 类型 tag（0/1/2/4/5/6/7/8）；case 4 → 用 `*(result+20)` 预存索引**下标进已存在的 deque#4**，对 `*v26`（controller）调 `sub_6638B0(ctl, mirror, value, transition, weight)` enqueue 一个过渡。**绝不 push 新元素**。
2. **sub_6638B0**：transition<=0 立即设 +300=value、+296=0；否则把 `{value,transition,weight}` 12B push 进 controller 的 deque-A。
3. **sub_663FC8(ctl, psbDict) = controller 反序列化器**：从 PSB dict 按 key 读 controller 字段（`phase`→+296 `frame`→+300 `v`→+308 `target`→+304 `length`→+312 `lengthDone`→+316 `exponent`→+324 `speed`→+320），`rq` 数组 → 每个 `{p0,p1}` push 进 deque-B。
4. **sub_678804(emote, eyeArray) + 兄弟 sub_678FF0/679804/67A020/67A868/67B08C/67B34C + sub_678454(timeline)**：被 **sub_678044**（reload 调度器）按类目 `eye/eyebrow/mouth/transition/selector/base/outerforce/timeline` 调用——它们都是**从已保存状态 reload**：算 hash 查已存在 HM 项 → 调 sub_663FC8 刷新 controller。**都不是初始 allocate+push 的 builder**。
5. **真正的初始 builder（allocate controller + push {ctl,key} 进 deque#4 + 写 HM@+1384 项 {type=4,index}）仍未定位**——它在 motion/character 初始加载路径（EmoteObject_init 链，读 metadata 变量定义表）。是个跨 8 类目的大子系统，喂 deque #4/#5/#6/#8/#9/#10。MCP 的 STR/STUR op_any 扫描查不到（deque 指针用 STP 写、reg+offset 寻址不被立即数匹配命中）。

## 拓扑结论 / remaining
- 叶子前置 = **初始 builder**（未定位）→ 然后才是 sub_663FC8 反序列化 → 然后 setVariable 驱动 / progress step。**没有 builder，deque#4 永远空，step 永不执行**。
- 本切片**未改任何代码**：类布局更正需要先定位 builder 才能给出 controller 的完整字段表与 HM@+1384 项布局（type@+16/index@+20/value@+0/key+ptr）；在 builder 缺位下改 EmoteStateMachine16B_Deque4 成 {ptr,ttstr} 是半成品（controller 类型尚未建模）。
- 2026-07-18 更正：上述 sibling deque/controller、hair/bust 物理与 post-loop bind 链均已实装；
  `sub_67C560` 也已迁到 Engine HM3/+1040/56B track deque。本条保留的价值是
  deque#4 的元素和 population 证据，“remaining 全部 open”不再是当前裁决。
- 验证缺口：emote 角色物理子系统无 oracle/fixture（logo 不覆盖）。
