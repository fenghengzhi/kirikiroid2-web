# Player 帧推进相关内部容器结构（权威）

来源（mcp__ida-pro-mcp__decompile，唯一权威）：
- `Player_progress_inner` @ **0x6C106C**
- `Player_advanceRootAndNodes` @ **0x6B6ADC**
- `Player_reseekTimelineCursors` @ **0x6B86C8** ← variable-track deque 的**完整 80B 控制结构索引公式**全部来自这里
- `sub_6B786C` @ **0x6B786C**（variable-track slot 写入：seekToFrameIndex）
- `sub_6B7A70` @ **0x6B7A70**（variable-track slot 写入：mergeFrameContent，含 type/interval/value/easing）

目标：把本地 std::deque / std::unordered_map 换成与 libkrkr2.so 完全一致的 inline 容器。本文档只列有反编译证据的字段；未确认项单列。所有偏移均为 `player`（Player 对象基址，1384B=0x568 布局）相对偏移。

> 已知背景（来自 agent-memory player_containers_libstdcxx_spec）：Player 的 6 个内嵌容器全部是 **GCC libstdc++ STL 实例化**（4 unordered_map + 2 deque），不是 KiriKiri 自定义。本文档专注 2 个 deque 的"帧推进"使用面 + 2 个 event-stream cursor + 标量 flag。

---

## 0. libstdc++ std::deque ABI（解释下面所有魔数）

`std::deque<T>` 控制结构 = **80 字节**（10 qword）：

```
+0   T**           _M_map        // node-map（block 指针数组）基址
+8   size_t        _M_map_size   // map 容量（可放多少 block 指针）
+16  iterator(32B) _M_start      // begin 游标 {cur, first, last, node}
+48  iterator(32B) _M_finish     // end 游标   {cur, first, last, node}
```

每个 `_Deque_iterator<T>` = 32B：`{ T* _M_cur; T* _M_first; T* _M_last; T** _M_node; }`。

block 字节容量 = `__deque_buf_size(sizeof(T))` = `sizeof(T) < 512 ? 512 : sizeof(T)`，每 block 元素数 = `max(1, 512/sizeof(T))`：
- node 元素 2632B → block=2632B，**1 元素/block**
- variable-track 元素 160B → block=480B（=3×160），**3 元素/block**

这两个数值用 ctor 处的 `std_deque_initBlocks_2632 @0x6F4F5C` / `std_deque_initBlocks_160 @0x6F50D8`（agent-memory）二次确认。

---

## 1. node deque（player +200 .. +280，控制结构 80B）

### 1.1 字段映射

deque 控制结构起于 **player+200**。把 progress_inner / advanceRootAndNodes / reseek 三处用到的 qword 对回标准 80B 布局（三处用法字节一致）：

| player 偏移 | deque 字段 | 反编译用法 |
|------|-----------|-------------|
| **+200** | `_M_map`（T**） | `v16/v39/v57 = *(a1+200)`；首块定址基址 `v16 + 2632*i` |
| **+208** | `_M_start._M_cur` | `(_M_map - +208)>>3` 算首块起点残差：`*0x18E6.. * ((v16 - *(a1+208))>>3)` |
| **+216** | `_M_start._M_node`（T**） | size 计算项 `*0x18E6.. * ((*(a1+216) - _M_map)>>3)` |
| **+224** | `_M_finish._M_cur`（末块当前元素，=末块 node-map 段基址） | `v14/v37/v55 = *(a1+224)`；`*(v14 + 8*v17)` 取块指针 |
| **+232** | `_M_finish._M_last` | size 项 `(*(a1+232) - *(a1+240))>>3` |
| **+240** | `_M_finish._M_first` | 同上配对 |
| **+248** | `_M_finish._M_node`? | 未在三函数出现（标准布局应是 finish.node；见 §6） |
| **+256** | `_M_finish._M_node`（实际被读的 end node 槽 T**） | size 项 `(*(a1+256) - v14)>>3` |
| **+264..+280** | 控制结构尾部 | 见 §6 未确认 |

> 注：reseek 末尾 `for(n = *(a1+280); n; n = *n)` 把 **+280** 当成另一条链表头（`sub_6B9650` aux list），不是 node-deque 字段。即 node-deque 控制结构实际只占 +200..+264，+280 起是别的成员（aux composite list）。这与"标准 80B=+200..+280"冲突，说明 IDA 反编译中 node-deque 的 80B 控制结构可能只用了前 8 个 qword（+200..+264），尾部 2 qword 被编译器省略/复用。**精确边界见 §6 未确认项。**

### 1.2 元素 stride 2632B + 块内布局

`v16 + 2632*v15`（0x6C1258）、`v39 + 2632*v38`（0x6B73D0）、`v57 + 2632*v56`（0x6B9220）：node 元素 **sizeof = 2632B**，**1 元素/block**。

### 1.3 size() 与 index→element 完整公式（progress_inner 0x6C1208..0x6C1258；reseek 0x6B91CC..0x6B9220 同构）

```c
// 1-based 元素索引 i，循环 for(i=1; ; i++)
v14 = *(a1+224);   // finish._M_cur
v16 = *(a1+200);   // _M_map
size_minus_1 =
    0xE719AD850EC8C0F9 * ((finishLast(+232) - finishFirst(+240)) >> 3)   // 末块元素数项
  + ((endNode(+256) - v14) >> 3)                                          // node 槽偏移
  - 0x18E6527AF1373F07 * ((startNode(+216) - v16) >> 3)                   // 首块起点项
  - 1;
if (size_minus_1 <= i) break/return;     // 越界停

v17 = i - 0x18E6527AF1373F07 * ((v16 - startCur(+208)) >> 3);  // i 在块序列里的残差
if (v17 != 0)  elem = *(v14 + 8*v17);    // 非首块：node-map 槽直接取块指针（1元素/block）
else           elem = v16 + 2632*i;      // 首块：基址 + 2632*i 直接定址
```

两个魔数都是 ÷(2632/8)=÷329 的有符号 magic-division 变体（`0x18E6527AF1373F07` / `0xE719AD850EC8C0F9`）。

### 1.4 遍历后处理（advanceRootAndNodes 0x6B73B4 起）

每元素 `v41`：`if(*(v41+8))` 有 child → `Player_advanceNodeFrames(v41,a1)`；否则内联 2-slot frame seek（§1.5）。

### 1.5 node 元素 2632B 关键内部字段（0x6B744C..0x6B7338）

| 元素内偏移 | 含义 |
|-----------|------|
| +8 | child 指针（非 0 走递归 advanceNodeFrames） |
| +28 | nodeType（`(6145/6153 & (1<<type))` mask gate；6153 当 +1092!=0 否则 6145） |
| +44 | dirtyFlag（seek 后置 1） |
| +64 | frameList dispatch source holder（propGetCount） |
| +320 + 536*slot | slot[0/1]（536B/slot 双缓冲） |
| slot+8 | slot.time（double 边界） |
| slot+0 | slot.frameIndex（int，与 count-2 上界比较） |
| slot+22 | bit2(&4)=action 标志 → `sub_6B638C(a1,.,slot+288)` 派发 |
| slot+288 | action variant |
| +346 | mergeGate（==0 才 `Player_mergeFrameContent(+320,type,+64)`） |
| +856 | 第二 merge 目标 |
| +882 | 第二 mergeGate（==0 才 merge +856） |
| +1392 | activeSlotIndex（int，每 seek 步 `^=1`） |
| +1996 | forceFind 标志（非 0 强制 findSource） |
| +536*activeSlot + 348/356 | findSource 输出指针对 |

`Player_parseFrame`(=sub_6926B4 单帧解析)，`Player_mergeFrameContent`(=sub_692AB0 mask-gated 合并)，`Motion_Player_findSource`(0x6B7338 解析贴图源)。

---

## 2. variable-track deque（player +1312 .. +1392，控制结构 80B）

### 2.1 字段映射（reseek 0x6B8F8C..0x6B9030 给出全部 7 个 qword，**这是权威**）

deque 控制结构起于 **player+1312**。reseek 的索引公式用到 +1312/+1320/+1328/+1336/+1344/+1352/+1368：

| player 偏移 | deque 字段 | 反编译用法 |
|------|-----------|-------------|
| **+1312** | `_M_start._M_cur`（begin 当前元素） | `v44 = *(a1+1312)`；首块定址 `v44 + 160*i`；advance 中 `v19 = *(a1+1312)` 步进基址 |
| **+1320** | `_M_start._M_first`（begin 块首） | reseek 残差项 `((v44 - *(a1+1320))>>5)`（>>5 = ÷32 配合 ÷3 → ÷96? 见下）|
| **+1328** | `_M_start._M_last`（begin 块尾哨兵） | advance `v20 = *(a1+1328)`；`if(v19==v20)` 跨块；size 项 `((*(a1+1328) - v44)>>5)` |
| **+1336** | `_M_start._M_node`（begin node 槽 T**） | advance `v21 = *(a1+1336)`；reseek `v42 = *(a1+1336)`，`*(v42 + 8*v47)` 取块指针 |
| **+1344** | `_M_finish._M_cur`（end 当前元素） | advance `if(*(a1+1344) > v19) goto LABEL_77`；size 项 `((*(a1+1344) - *(a1+1352))>>5)` |
| **+1352** | `_M_finish._M_first`（end 块首） | size 项配对 +1344 |
| **+1360** | `_M_finish._M_last`? | 未在 progress 路径出现（见 §6） |
| **+1368** | `_M_finish._M_node`（end node 槽 T**） | advance `v26 = *(a1+1368); if(v26==v21) break`；size 项 `((*(a1+1368) - +1336)>>2)+((..)>>3)` |

> +1376/+1384 是 tail padding（agent-memory：ctor memset 0x50 + 写 10 qword，+1376..+1384 对齐字节）。

### 2.2 deque 形态：block=480B=3 元素（advance 0x6B716C 跨块逻辑权威）

元素 stride **160B**。跨 block（advanceRootAndNodes 0x6B7160..0x6B7170）：
```c
v19 += 160;
if (v19 == v20) {            // v20 = begin._M_last (+1328)
    v25 = *(v21 + 8);        // v21 = node 槽 (T**)，取下一 block 指针
    v21 += 8;
    v19 = v25;
    v20 = v25 + 480;        // ★ block 字节容量 = 480 = 3 元素
}
```

### 2.3 reseek 随机访问 index→element 公式（0x6B8FD0..0x6B9030，÷3 magic）

```c
// v44 = begin._M_cur(+1312), v42 = begin._M_node(+1336)
size_minus_3 =                       // libstdc++ size() for 3-elem-block deque
    ((endNode(+1368) - v42) >> 2) + ((endNode(+1368) - v42) >> 3)            // node 槽差 ×(map 段) 
  - 0x3333..3 * ((finishCur(+1344) - finishFirst(+1352)) >> 5)              // 末块元素数 (÷32 then ×magic = ÷3)
  - 0x3333..3 * ((beginLast(+1328) - v44) >> 5)                             // 首块剩余
  - 3;
if (size_minus_3 <= i) break;        // i = 0-based track index

v45 = i - 0x3333..3 * ((v44 - beginFirst(+1320)) >> 5);   // i 相对首块起点的块内残差
if (v45 < 0)        v47 = ~(~v45 / 3);          // 负残差向下取整除 3 → 块号
else if (v45 <= 2)  { elem = v44 + 160*i; goto have; }     // 首块内：直接 +160*i
else                v47 = v45 / 3;              // 块号
elem = *(node[+1336] + 8*v47) + 160*(v45 - 3*v47);   // 块指针 + 块内 160*(残差%3)
have:
```

`0x3333333333333333 * (x>>5)` = `(x/32)/?`... 实测语义：先 `>>5`（÷32，因为 32 是块内某对齐？），但 element=160B、block=480B；`(beginLast - beginCur)>>5` 给出的是 `(480-offset)/32`，再 `×0x3333..`(÷5 的 magic? 实为 ÷3 的另一种写法因 160/32=5, 480/32=15=3×5)。**关键结论：每 block 3 元素、stride 160B 被这套 ÷3 + >>5 公式精确编码，inline 容器必须复刻 `512/160=3` 的块元素数。**

### 2.4 variable-track 元素 160B 字段表（advance 0x6B71F4..；reseek 0x6B8F30..0x6B8F60；0x6B786C/0x6B7A70 写入证据）

元素基址 = `v46`（reseek）/ `v19`（advance）。

| 元素内偏移 | 类型 | 含义 | 证据 |
|-----------|------|------|------|
| **+0** | ttstr | cascadeKey（`scope` 存在时为 `scope+"::"+label`，否则为 label） | Player_initVariables 0x6CD9F0..0x6CDBB4 |
| **+8** | int | activeSlotIndex（活动 slot 0/1） | advance `v27=*(int*)(v19+8)`；翻转 `*(v19+8)=(*(v19+8)&1)==0`；reseek 末 `*(v46+8)=0` 重置 |
| **+16** | double | 当前插值变量值（HM4 快照读取） | Player_interpolateVarTrackValues 0x6BBF54 |
| **+24** | tTJSVariant | frameList dispatch source（传 0x6B786C/0x6B7A70 第二参） | Player_initVariables 0x6CDA58..0x6CDA98 CopyRef |
| **+48 + 56*slot** | 56B | slot[0]/slot[1]（双缓冲，每 slot 56B） | advance `v24=v19+48+56*active`；reseek `sub_6B786C(v46+48,..)` 与 `sub_6B786C(v46+104,..)`（104=48+56） |

**slot 内 56B 子结构（来自 sub_6B786C 0x6B7898/0x6B7988/0x6B798C + sub_6B7A70 0x6B7a9c..0x6B7c88）**，slot 基址 = `v46+48`（slot0）/ `v46+104`（slot1）：

| slot 内偏移 | 类型 | 含义 | 写入函数 |
|-----------|------|------|---------|
| +0 | int | frameIndex（该 slot 当前帧号） | sub_6B786C 写 a3 |
| +8 | double | time（`propGetDouble("time")`） | sub_6B786C 写 |
| +20 | byte | isHold/typeZeroFlag（type==0 时置 1） | sub_6B7A70 `*(a1+20)=1` |
| +21 | byte | easingKind（type==2→0，type==3→1） | sub_6B7A70 `*(a1+21)=v7` |
| +22 | byte | **mergedFlag**（sub_6B786C 置 0 = 未 merge；sub_6B7A70 置 1 = 已 merge） | 两函数共写 +22 |
| +16(=a1[4]) | int | interval（`propGetInt("interval")`） | sub_6B7A70 0x6B7c68 |
| +24(=a1[3]) | double | value（`propGetDouble("value")`） | sub_6B7A70 0x6B7c88 |
| +32 | tTJSVariant | `frame["easing"]` 的独立 CopyRef | 0x6B7C90..0x6B7CD4 |

> 勘误（2026-07-18）：旧记录把 easing 误写成 slot+8、与 time 重叠。完整反编译证明 0x6B7CD4 的 CopyRef 目标是 slot+32；time 始终在 +8。interval/value 的 holder 是 `frame["content"]`，easing 的 holder 则是 frame 本身。

**元素级 mergedFlag（+70/+126）的来历**：advance 中 `if(!*(v19+70)) sub_6B7A70(v19+48,..)` 与 `if(!*(v19+126)) sub_6B7A70(...)`。+70 = (+48)+22 = slot0 的 +22；+126 = (+104)+22 = slot1 的 +22。即 **+70/+126 就是两个 slot 各自的 mergedFlag**（不是独立元素字段）。

### 2.5 元素遍历/seek 结构

advance（0x6B7274 循环）：
```c
active = v19+48+56*(v19+8);  other = v19+48+56*((v19+8&1)==0);
count2 = propGetCount(v19+24) - 2;
while (*active < count2 && +456 >= *(double*)(other+8)) {
    *(v19+8) = (*(v19+8)&1)==0;             // 翻转 activeSlotIndex
    sub_6B786C(active, v19+24, *(int*)other + 1);  // 下一帧 → active slot
    swap(other, active);
}
if (!*(v19+70))  sub_6B7A70(v19+48, v19+24);   // slot0 未merge → merge
if (!*(v19+126)) sub_6B7A70(v19+48, v19+24);   // slot1 未merge → merge
```

reseek（0x6B8F30）对每个 track 元素：`sub_6B786C(v46+48, v46+24, idx)` + `sub_6B7A70(v46+48, v46+24)` + `sub_6B786C(v46+104, v46+24, idx+1)` + `sub_6B7A70(v46+104, v46+24)` + `*(v46+8)=0`。即 reseek 把 slot0=idx、slot1=idx+1、activeSlot=0 重新种子化。

### 2.6 子函数语义（已反编译，不再"未确认"）

- **Motion_VarTrackSlot_step_guess(slot, frameListHolder, frameIndex)** @0x6B786C：`slot[0]=frameIndex`；从 frameList dispatch 取第 frameIndex 项；`slot+8 = propGetDouble("time")`；`slot+22 = 0`（清 mergedFlag）。
- **Motion_VarTrackSlot_merge_guess(slot, frameListHolder)** @0x6B7A70：`slot+22 = 1`；取帧 `type`，type==0 时写 `slot+20=1` 并早退；否则从 `frame["content"]` 读 interval/value 到 +16/+24，再把 `frame["easing"]` CopyRef 到 slot+32。

---

## 3. layer / root event-stream cursor（不是 deque，是 dispatch+int+2double）

### 3.1 layer event stream（+1072 source / +916 cursor / +920 curTime / +928 nextTime）

advanceRootAndNodes 0x6B6B14..；reseek 0x6B8700..：

| 偏移 | 类型 | 含义 |
|------|------|------|
| **+1072** | dispatch holder（≈20B） | layer stream **dispatch source**（事件数组 owner）。`sub_A0F5E0(.,a1+1072)` 取出 → propGetCount |
| **+916** | int | layer **cursor**（已消费事件索引，0-based） |
| **+920** | double | 当前事件 time |
| **+928** | double | 下一事件 time（前看；`+456 < +928` 停止消费） |

advance 循环 `for(i = count-2; cursor < i; )`，`+456 < +928` break。type==1 帧 stop-gate（受 +1093 门控；reseek 用 `align`/advance 用 propGetBool[7]）：置 +483=1 或 +1098=1，并 `+456=+1120=+920`。`action` 非空 → `sub_6B638C`。

### 3.2 root event stream（+548 source / +568 cursor / +576 curTime / +584 nextTime / +616 content buf）

advanceRootAndNodes 0x6B6EE4..；reseek 0x6B8BC4..：

| 偏移 | 类型 | 含义 |
|------|------|------|
| **+548** | dispatch holder（≈20B） | root stream **dispatch source** |
| **+568** | int | root **cursor** |
| **+576** | double | 当前事件 time（推进时 `+576 = +584`） |
| **+584** | double | 下一事件 time（前看；`+456 < +584` 停止） |
| **+616** | content buffer | `sub_A0FB64(a1+616, content)` 把当前 root 事件 content 拷入 |

advance 循环 `for(j = count-2; cursor < j; )`，`+456 < +584` break。每步 `sub_A0FB64` 复制 content 到 +616 并 `+576 = +584`。

### 3.3 两 stream 对照

| | dispatch source | cursor(int) | curTime(double) | nextTime(double) | content buf |
|---|---|---|---|---|---|
| **layer** | +1072 | +916 | +920 | +928 | 无（触发 action/sync/align） |
| **root**  | +548  | +568 | +576 | +584 | +616（sub_A0FB64） |

两者都 propGetCount 取事件数，`for(cursor; cursor < count-2; )`，nextTime 与 +456 比较决定是否消费。dispatch holder = ttstr/variant 持有 + 解析出的 iTJSDispatch2*（sub_A0F5E0 解析 / sub_A0F778 释放 / off_19FD968 vtable）。holder 精确字节布局见 §6。

---

## 4. 帧推进标量/flag 字段表（progress_inner 0x6C106C / reseek 0x6B86C8 确认）

| 偏移 | 类型 | 含义 |
|------|------|------|
| +456 | double | **clampedEvalTime**（求值时间 = min(frameTick,totalFrames)；所有 stream/seek 的比较基准） |
| +480 | byte | progressFlags/gate（==0 才 `+1120+=+592; +456=min(+1120,+1128)`） |
| +481 | byte | firstFrame（一次性，seed +1120/+456 from activeTimeline+40 或 totalFrames） |
| +482 | byte | emoteMode（非 0 → `Player_initEmoteMotion(a1,2)`） |
| +483 | byte | motionCompleted（每帧入口清 0；stop-gate 置 1） |
| +548/+568/+576/+584/+616 | — | root stream（§3.2） |
| +592 | double | **deltaTime** = speedMul(+1168) × dtFrames(a2)；符号定 forward/reverse |
| +609 | byte | reverseSeekFlag（一次性反向 seek） |
| +916/+920/+928/+1072 | — | layer stream（§3.1） |
| +1092 | byte | preview（node mask 6145 vs 6153；completionType 是 +1144 int） |
| +1093 | byte | syncActive（layer/reseek type==1 事件 align/sync 门控；非 +1168 speed 乘子） |
| +1098 | byte | syncWaiting（置 1 后多处提前 return） |
| +1099 | byte | playing（本地 `_allplaying`） |
| +1120 | double | **frameTickCount**（主时间游标，无 clamp 累加） |
| +1128 | double | **totalFrames**（cachedTotalFrames，clamp 上界 / loop 边界） |
| +1136 | double | **loopTime**（>=0 forward-loop wrap，<0 reset-to-0） |
| +1152 | int | 每帧入口清 0（用途未确认） |
| +1168 | double | speedMul（首读，×dt → +592） |
| +200..+264 | deque | node deque（§1） |
| +280 | ptr | aux composite list 头（reseek `for(n=*(a1+280);n;n=*n) sub_6B9650`），非 node-deque |
| +1312..+1368 | deque | variable-track deque（§2） |

loop-wrap（progress_inner）：forward `frameTick += loopTime - totalFrames` while `totalFrames<=frameTick`；reverse `frameTick += totalFrames - loopTime` while `loopTime>frameTick`。

---

## 5. inline 容器替换建议

| libkrkr2.so | 替换目标 |
|---|---|
| node deque @+200（80B 控制结构，element 2632B，1 元素/block，block=2632B） | inline libstdc++-ABI deque<MotionNode>，1 元素/block；index→addr 用 §1.3 公式 |
| variable-track deque @+1312（80B 控制结构，element 160B，3 元素/block，block=480B） | inline deque<VarTrackEntry160B>，3 元素/block；index→addr 用 §2.3 ÷3 公式；element 含 +0 cascadeKey、+8 activeSlot、+16 当前值、+24 frameSource Variant、+48/+104 两 56B slot（slot: +0 frameIndex/+8 time/+16 interval/+20 hold/+21 easingKind/+22 mergedFlag/+24 value/+32 easing Variant） |
| layer stream @+1072/+916/+920/+928 | dispatch holder + int cursor + 2 double（无 content buf） |
| root stream @+548/+568/+576/+584/+616 | dispatch holder + int cursor + 2 double + content buf @+616 |

**关键不可省略点**：两个 deque 的块元素数不同（node=1、var-track=3）。inline 实现必须各按 `max(1,512/sizeof(T))` 算块元素数，否则 `v19==v20` 跨块判定、`+2632*i` / ÷3-`+160` 随机访问、reseek 的 size() magic 全错。

---

## 6. 未确认项

1. **node deque 控制结构精确边界**：三函数只用 +200/+208/+216/+224/+232/+240/+256；标准 80B 应到 +280，但 reseek 把 +280 当 aux list 头。需另找 node-deque ctor（`Player_nodesDeque_init @0x6F4E90`）/push（`0x6F1914`）确认 +248/+264/+272 是否属控制结构，以及 finish iterator 4 字段精确排布。
2. **variable-track deque +1360**：reseek size 公式未直接读 +1360（标准应为 finish._M_last）；需 ctor `Player_controllerDeque_init @0x6F4FD8` 确认。
3. **dispatch holder（+1072 / +548 / element+24）精确字节布局**：~20B ttstr/variant + 解析出的 iTJSDispatch2*；off_19FD968 vtable 成员。sub_A0F5E0/sub_A0E48C/sub_A0F778 是其 ctor/detach/dtor。
4. ~~slot+8 time/easing 冲突~~：已由 0x6B7CD4 完整反编译证伪；easing Variant 位于 slot+32，不与 +8 time 重叠。
5. **player+1152 用途**（每帧清 0 的 int）。
6. **+456 在 reseek 里被 int 截断**（`(double)(int)propGetInt("time")` 写 +920/+928 是整数化）：layer stream 在 reseek 路径 curTime/nextTime 被 int 截断，advance 路径用 propGetDouble 不截断——两路径精度不同，inline 移植须保留此差异。

> §1（node deque 形态+公式）、§2（var-track 形态+÷3 公式+元素字段+两 slot writer）、§3（两 stream cursor）、§4（标量 flag）均有 0x6C106C / 0x6B6ADC / 0x6B86C8 / 0x6B786C / 0x6B7A70 直接反编译证据。§6 为补充细节，不影响容器主体替换。
