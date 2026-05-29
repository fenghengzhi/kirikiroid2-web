---
name: EmoteEngine 7-controller + 10-deque reverse-engineering spec
description: Byte-verified spec for EmoteEngine 1496B object — 10 internal std::deque at offsets 0/80/.../720 + 7 heap-allocated controller objects (a1[134..140]) at +1072..+1120. No vtable on controllers. Maps each controller to its semantic role (position/scale/color/angle/hair/bust×2) and its step function.
type: project
---

# EmoteEngine 7-Controller Reverse Engineering 报告

## 关键背景修正

之前 `module_motionplayer.md` 写"5 个 deque 在 EmoteEngine，1 个 unordered_map 在 EmoteEngine+1384"。**实际情况**：
- EmoteEngine 内嵌 **10** 个 std::deque @ offsets 0/80/160/240/320/400/480/560/640/720（每个 80B）
- EmoteEngine+1064 (a1[133]) = Player\*（Player_ctor 分配 0x568=1384B 独立堆对象）
- EmoteEngine+1072..+1120 (a1[134..140]) = **7 个独立堆 controller**
- EmoteEngine+1440 = HM2 锚点（labelToValue），不是 Player+1440。Player size 1384 < 1440，HM2 必然在 EmoteEngine

sub_530A5C (`D3DEmotePlayer_progress` @ 0x530A5C) 取 `v13 = *(QWORD*)(*(QWORD*)(D3DEmotePlayer+24)+8)` = **EmoteEngine\***（不是 Player）。所有 v13+256/336/416/576/656/736 都是 EmoteEngine 内部 deque 的 begin._M_cur 字段。

## EmoteEngine_ctor 总览 (0x67E38C, size 0x5D8 = 1496B)

```c
void EmoteEngine_ctor(EmoteEngine* a1, ResourceManager* a2):
    // === 10 个 std::deque @ offsets 0..720 ===
    memset(a1+0,   0, 0x50); sub_684A50(a1+0,   0);  // #1  @ 0   element=48B (sub_684B50 init)
    memset(a1+80,  0, 0x50); sub_684BCC(a1+80,  0);  // #2  @ 80  element=56B
    memset(a1+160, 0, 0x50); sub_684BCC(a1+160, 0);  // #3  @ 160 element=56B
    memset(a1+240, 0, 0x50); sub_684D58(a1+240, 0);  // #4  @ 240 element=16B
    memset(a1+320, 0, 0x50); sub_684EAC(a1+320, 0);  // #5  @ 320 element=16B
    memset(a1+400, 0, 0x50); sub_685000(a1+400, 0);  // #6  @ 400 element=24B
    memset(a1+480, 0, 0x50); sub_685198(a1+480, 0);  // #7  @ 480 element=40B
    memset(a1+560, 0, 0x50); sub_685314(a1+560, 0);  // #8  @ 560 element=24B
    memset(a1+640, 0, 0x50); sub_6854AC(a1+640, 0);  // #9  @ 640 element=48B
    memset(a1+720, 0, 0x50); sub_685628(a1+720, 0);  // #10 @ 720 element=16B

    // === scalar/state fields @ 800..928 ===
    a1[102]=0; a1[105]=0; a1[106]=0;            // offsets 816 / 840 / 848
    *((OWORD*)a1 + 50) = 0;                     // offsets 800..815 zeroed
    *((DWORD*)a1 + 214) = 1.0f (1065353216);    // offset 856 = float 1.0
    a1[108] = 0;                                // offset 864 = 0

    // === 4 个 std::vector reserve(10) inline @ offsets 856..1023 ===
    // Pattern (repeats 4x): cap=sub_149EDF8(...,10); ptr=new(8*cap); set ptr@a1[103]/a1[110]/a1[117]/...
    // These store flat dynamic-array bookkeeping (not deque). Used by Player_ctor pattern at this+296/352/1216/1272.
    // Pattern A (offsets 856..887): vector#1 with cap@a1[104], data@a1[103], size@a1[105], +1.0f field @ a1[214]
    // Pattern B (offsets 888..919): vector#2 same shape
    // Pattern C (offsets 952..983): vector#3
    //   (additional state slots at a1[124..130] memset to 0)

    // === offset 1064 ===
    v13 = operator new(0x568);                  // 1384B
    Player_ctor(v13, a2);
    a1[133] = v13;                              // EmoteEngine+1064 = Player*

    // === 7 controllers @ 1072..1120 ===
    a1[134] = operator new(0x80); EmoteVarController_ctor_20Bdeque(a1[134], 2);
    a1[135] = operator new(0x80); EmoteVarController_ctor_20Bdeque(a1[135], 1);
    a1[136] = operator new(0x80); EmoteVarController_ctor_20Bdeque(a1[136], 4);
    a1[137] = operator new(0x70);
        memset(a1[137], 0, 0x50);
        EmoteAngleController_ctor_12Bdeque(a1[137], 0);
        *(QWORD*)(a1[137] + 80) = 0;            // ptr field
        *(DWORD*)(a1[137] + 88) = 0;            // int field
    a1[138] = operator new(0x80); EmoteVarController_ctor_20Bdeque(a1[138], 2);
    a1[139] = operator new(0x80); EmoteVarController_ctor_20Bdeque(a1[139], 2);
    a1[140] = operator new(0x80); EmoteVarController_ctor_20Bdeque(a1[140], 2);

    // === scalar matrix/state fields @ 1128..1456 ===
    *((OWORD*)a1+71)=0; *((OWORD*)a1+72)=0;     // 1136..1167 zeroed
    a1[150] = 1.0 (double, 0x3FF0000000000000); // +1200 = double 1.0  (read by stepBust as +1184/+1192/+1200 doubles)
    *((DWORD*)a1+290) = 1;                      // +1160 = 1
    *((OWORD*)a1+73)=0; *((OWORD*)a1+74)=0;     // 1168..1183 zeroed (later become +1184,+1192 doubles)
    *((BYTE*)a1+1162) = 1;                      // physics-dirty flag (set in EmoteEngine_progress)
    // ...4 more vector reserve(10) blocks @ 1272..1488...

    // === reset 4 controllers (134,135,137,136 — note order!) ===
    // After ctor, immediately reset controller's internal deque begin/end pointers, seeding with
    // identity value (xmmword_14D68D0 = identity matrix-like for color=white #FFFFFFFF? scale=1.0?).
    reset_controller_deque(a1[134], seed=0x0_0_0_0);     // pos defaults (0,0)
    reset_controller_deque(a1[135], seed=1.0f);          // scale default 1.0
    reset_controller_deque(a1[137], seed=...);           // angle, special path (no memcpy at end)
    reset_controller_deque(a1[136], seed=xmmword_14D68D0); // color default (4 floats)
```

## Controller 字段布局 (0x80 / 0x70 variants)

### Variant A: EmoteVarController (0x80, from EmoteVarController_ctor_20Bdeque @ 0x667030)

```c
struct EmoteVarController_80 {
    // +0..+79: std::deque<KeyValue20B>  (libstdc++)
    //   element type: struct { float endValue; float duration; uint32_t powCount; uint64_t pad; } size=20
    //   block: 25 elements × 20B = 500B per chunk
    +0:  T**       map_ptr;          // sub_6878D8 calls operator new(8 * map_size)
    +8:  size_t    map_size;         // = max(deque_size/25 + 3, 8)
    +16: T*        begin._M_cur;     // first valid element ptr
    +24: T*        begin._M_first;   // first block start
    +32: T*        begin._M_last;    // first block end (start + 500)
    +40: T**       begin._M_node;
    +48: T*        end._M_cur;
    +56: T*        end._M_first;
    +64: T*        end._M_last;
    +72: T**       end._M_node;
    // +80..+127: animation state (step function = EmoteVarController_step @ 0x666BF8)
    +80: int32_t   count;            // number of float channels (2,1,4 = pos/scale/color)
    +84: int32_t   state;            // 0=idle, 1=animating
    +88: float[]   currentValue;     // new[count*4] heap array (zero-init)
    +96: float[]   targetValue;      // new[count*4] heap array
    +104: float[]  startValue;       // new[count*4] heap array (used as src for lerp)
    +112: int32_t  powCount;         // = element.powCount (degree of power curve)
    +116: float    phase;            // 0..1 ramp; advances by (1/duration)*dt
    +120: float    invDuration;      // = 1/element.duration
    +124: int32_t  pad;
};
```

Step function `EmoteVarController_step(this, float* output, float dt)`:
1. If state==0: pop deque head, set targetValue=elem.endValue, invDuration=1/elem.duration, powCount=elem.powCount, copy currentValue→startValue, advance state→1
2. If state==1: phase += invDuration*dt; if phase≥1 commit final value, state→0; else compute `f = powf(phase, powCount)`; SIMD-lerp `current[i] = start[i] + f*(target[i]-start[i])` for i=0..count
3. Copy currentValue → output[count]

### Variant B: EmoteAngleController (0x70, from EmoteAngleController_ctor_12Bdeque @ 0x6867B0)

```c
struct EmoteAngleController_70 {
    // +0..+79: std::deque<KeyValue12B>
    //   element type: struct { float endRad; float duration; uint32_t powCount; } size=12
    //   block: 42 elements × 12B = 504B per chunk
    +0..+79: std::deque<KeyValue12B>;  // same shape as Variant A but 12B/element, 42/block
    // +80..+111: animation state (step function = EmoteAngleController_step @ 0x666634)
    +80: int32_t   state;            // 0=idle, 1=animating
    +84: float     currentRad;       // current output angle (radians)
    +88: float     targetRad;        // = elem.endRad (after shortest-path wrap)
    +92: float     startRad;         // = currentRad at start of segment
    +96: float     invDuration;
    +100: int32_t  powCount;
    +104: float    phase;            // 0..1
    +108: int32_t  pad;
};
```

Step function `EmoteAngleController_step(this, float* outRad, float dt)`:
- Pop deque, but compute shortest-path: if `|target - current| > π`, add/subtract 2π so the rotation goes the short way around
- Power-curve interp like Variant A but on 1 scalar
- Wraps result into [0, 2π)

## 7 Controllers (byte-verified semantic mapping)

证据来自 `EmoteEngine_applyVarControllers_pos_scale_color_angle` @ 0x6766E0 + `EmoteEngine_progress` @ 0x67D01C tail.

| # | Offset | Address | Class | Count | Role | Output consumer |
|---|--------|---------|-------|-------|------|-----------------|
| 1 | a1[134] = +1072 | `EmoteVarController_ctor_20Bdeque(.,2)` | VarController 0x80 | 2 | **Position (x,y)** | `Player_setCoord(player, v[0], v[1])` |
| 2 | a1[135] = +1080 | `EmoteVarController_ctor_20Bdeque(.,1)` | VarController 0x80 | 1 | **Scale (uniform)** | `Player_setSlant(player, v[0], v[0])` + `this+1176 = 1.0/(this+1168 * v[0])` |
| 3 | a1[136] = +1088 | `EmoteVarController_ctor_20Bdeque(.,4)` | VarController 0x80 | 4 | **Color RGBA** | `sub_6CD724(player, byte(v[0])\|byte(v[1])<<8\|byte(v[2])<<16\|byte(v[3])<<24)` |
| 4 | a1[137] = +1096 | `EmoteAngleController_ctor_12Bdeque(.,0)` | AngleController 0x70 | 1 | **Angle/Rotation** | `Player_setAngleDeg(player, v[0])` |
| 5 | a1[138] = +1104 | `EmoteVarController_ctor_20Bdeque(.,2)` | VarController 0x80 | 2 | **Hair/Parts physics target** | `EmoteEngine_stepHairParts(this, dt)` reads `*(this+1104)`, iterates **deque #1 (48B element)** running `EmotePhysics_springStep` per node |
| 6 | a1[139] = +1112 | `EmoteVarController_ctor_20Bdeque(.,2)` | VarController 0x80 | 2 | **Bust #1 physics target** | `EmoteEngine_stepBust(this, *(this+1112), this+80, *(double*)(this+1184), dt)` over **deque #2 (56B element)** |
| 7 | a1[140] = +1120 | `EmoteVarController_ctor_20Bdeque(.,2)` | VarController 0x80 | 2 | **Bust #2 physics target** | `EmoteEngine_stepBust(this, *(this+1120), this+160, *(double*)(this+1192), dt)` over **deque #3 (56B element)** |

### 没有 vtable

`EmoteVarController_ctor_20Bdeque` 和 `EmoteAngleController_ctor_12Bdeque` 都**不写 +0 字段为 vtable ptr**——+0 是 `std::deque._M_map`（指向 block ptr 数组）。这些 controllers **不是多态对象**。EmoteEngine 通过具体函数指针直接调用 step 函数：
- 4 个直接 controller（pos/scale/color/angle）通过 `EmoteEngine_applyVarControllers_pos_scale_color_angle` 调度
- 3 个物理 controller 通过 `EmoteEngine_stepHairParts` / `EmoteEngine_stepBust` 调度

### dtor

未单独反编译 `EmoteEngine_dtor`，但可推断：EmoteEngine destruction 应：
1. delete 7 个 controller heap object（每个 80B 或 112B）
2. 销毁 10 个 std::deque（释放 map_ptr + 各 block）
3. delete Player\* (1384B)

## EmoteEngine 10-deque 完整规格

| # | Offset | begin._M_cur | Element | Block | progress 用？ | Step 函数 | 角色推断 |
|---|--------|------|---------|-------|------|------|------|
| 1 | 0   | (n/a—Hair/Parts 通过 stepHairParts 间接)  | 48B | 10×48=480 | Indirect | `EmoteEngine_stepHairParts` 用 deque#1.begin@+16 | **Hair/Parts spring nodes** (48B/node: position + velocity + stiffness) |
| 2 | 80  | (n/a)  | 56B | 9×56=504 | Indirect | `EmoteEngine_stepBust(this+80,...)` | **Bust chain #1 nodes** (56B/node) |
| 3 | 160 | (n/a)  | 56B | 9×56=504 | Indirect | `EmoteEngine_stepBust(this+160,...)` | **Bust chain #2 nodes** |
| 4 | 240 | v13+256 | 16B | 32×16=512 | YES | `sub_663BDC` (state machine, eye-blink/mouth) | **Categorical/state-machine variable** (e.g. eye, mouth, brow phase) — output 1 value to HM2 |
| 5 | 320 | v13+336 | 16B | 32×16=512 | YES | `sub_665600` | **Variable #2** (similar to #4, single value output) |
| 6 | 400 | v13+416 | 24B | 21×24=504 | YES | `sub_666068` (writes 2 outputs per element) | **Composite variable** (pair value: e.g. blush x+y, breath in+out) |
| 7 | 480 | (NOT iterated) | 40B | 12×40=480 | NO | (storage pool) | **Setup table** (likely "frame keyframe pool" for #4..#10) |
| 8 | 560 | v13+576 | 24B | 21×24=504 | YES | `sub_666BF8` (same as VarController_step but on deque element) | **Aux value controller** (single output) |
| 9 | 640 | v13+656 | 48B | 10×48=480 | YES | `sub_668470` (likely vector op) | **Vector variable** — 48B element, 6-QWORD iter step |
| 10| 720 | v13+736 | 16B | 32×16=512 | YES | inline table lookup + lerp | **Pre-baked curve/lookup table** (uses table data at element[0][1] as float[3] frames with `(index, value, duration)`) |

### EmoteEngine_progress (sub_67D01C) 主循环骨架

```c
EmoteEngine_progress(this, dt):
    v13 = this;  // EmoteEngine*
    Player_preProgress();
    while (dt > 0 || dirty_flag@1162):
        step = fmin(dt, 1.1);  // physics step cap
        dirty_flag@1162 = 0;

        // iterate 6 active deques, each step writes 1-2 floats to HM2@+1440:
        for each elem in deque#4 (16B): out=sub_663BDC(*elem, step); HM2.upsert(elem.label1, out)
        for each elem in deque#5 (16B): out=sub_665600(*elem, step); HM2.upsert(elem.label1, out)
        for each elem in deque#6 (24B): (o0,o1)=sub_666068(*elem, step); HM2.upsert(elem.label1,o0); HM2.upsert(elem.label2,o1)
        for each elem in deque#9 (48B): out=sub_668470(*elem, step); HM2.upsert(elem.label1, out)
        for each elem in deque#8 (24B): out=sub_666BF8(*elem, step); HM2.upsert(elem.label1, out)
        for each elem in deque#10 (16B): table-driven lerp; HM2.upsert(elem.label1, value)

        // apply 4 direct controllers + hair/bust (with same step):
        EmoteEngine_applyVarControllers_pos_scale_color_angle(this, step);
        if (player@1128 && player+1544 flag) sub_6687E8(step);

        dt -= step;

    // === post-loop ===
    for (entry = this+1456; entry; entry = entry->next): {  // linked list of pending evals
        sub_67C560(this, ...);
        sub_67C6B0(this, ...);
        Player_bindParameterValue_writesHM1_HM2(...);
    }
    sub_67C8A8(this);
    sub_6D2A54(player, 0, dt);

    if (dt!=0 && !syncWaiting@1159):
        // physics-only pass (no animation evaluation)
        EmoteVarController_step(*(this+1104), v71, dt);
        EmoteVarController_step(*(this+1112), v71, dt);
        EmoteVarController_step(*(this+1120), v71, dt);
        EmoteEngine_stepHairParts(this, dt);
        EmoteEngine_stepBust(this, *(this+1112), this+80,  *(double*)(this+1184), dt);
        EmoteEngine_stepBust(this, *(this+1120), this+160, *(double*)(this+1192), dt);
```

## 物理 step 函数细节

### EmoteEngine_stepHairParts (0x67B748)
- Reads controller#5 (this+1104) → memcpy其 currentValue@+88 into local v32 (target position)
- Iterates **deque#1** (48B element, `v9 += 6`)
- Each element has fields: `+0=spring_obj_ptr (sub-controller)`, `+8=byte init flag`, `+12=position/anchor`, `+20=label1`, `+28=label2`, `+36=int state`
- For each element: call `EmotePhysics_springStep(*v9, &outX, &outY, ...)` with target, current state, dt
- Write outputs to HM2 @ labels `+20`, `+28`

### EmoteEngine_stepBust (0x67BCE8)
- Same shape as stepHairParts but on **deque#2 or #3** (56B element, `v15 += 7`)
- Calls `sub_6689A4` (different physics op — bust simulation with sinusoidal damping)
- Uses **Player_getAngleDeg(player)** to apply gravity direction
- Element fields: `+0=node_ptr`, `+8=init_flag`, `+12=anchor`, `+20=label1 (e.g. bustL)`, `+28=label2 (bustR)`, `+36=label3`, `+44=int state`
- Sinusoidal damping: `sinf(phase) * amplitude_decay * amplitude_field` writes paired symmetric values (one + one -)

### EmotePhysics_springStep (0x662768)
- Reads sub-controller fields: `+4=stiffnessY, +8=stiffnessX, +12=damping, +20=scale1, +24=scale2`
- State: `+28/+32=prev_target_delta, +36/+40=last_target, +44/+48/+52/+56=position(x,y,z?), +60/+64/+68=velocity`
- Computes `F = stiffness * (target - position)` + rotated gravity vector
- `velocity = velocity * (1-damping) + F * dt`
- `position += velocity * dt`
- Output: `atanf(F_x * scale) / 0.0392699`, `atanf(F_y * scale) / 0.0392699` (radians per 2.25° step?)

## 与本地 cpp/plugins/motionplayer 的缺失差距

本地 `cpp/plugins/motionplayer/EmoteEngine.h` 当前声明：
- 5 个 std::deque<VariableAnimatorState> @ +256/+336/+416/+576/+656（实际应该是 EmoteEngine offsets 240/320/400/560/640）
- 1 个 std::unordered_map<std::string,VariableAnimatorState> @ +1384（实际应该是 HM2 = libstdc++ unordered_map@ +1440）

### 缺失项

1. **缺 5 个 deque**：本地只声明 5 个，二进制有 10 个。缺：
   - deque #1 (offset 0, 48B/elem) — Hair/Parts spring node container
   - deque #2 (offset 80, 56B/elem) — Bust chain #1
   - deque #3 (offset 160, 56B/elem) — Bust chain #2
   - deque #7 (offset 480, 40B/elem) — Setup/storage pool
   - deque #10 (offset 720, 16B/elem) — Lookup table curves

2. **缺 7 个 controller heap object**：完全没有对应本地类。需要新增 2 个本地类：
   - `EmoteVarController`（0x80 字节，6 个实例：pos/scale/color + 3 个 physics target）
   - `EmoteAngleController`（0x70 字节，1 个实例：rotation）

3. **缺 4 个 std::vector reserve(10) 内嵌块**（offsets 856..887, 888..919, 952..983, 1272.., 1328..） — 这些是 setVariable 内联 vector，目前未做完整反编译。

4. **缺物理引擎完整实现**：
   - `EmoteEngine_stepHairParts` (0x67B748)
   - `EmoteEngine_stepBust` (0x67BCE8) ×2 调用
   - `EmotePhysics_springStep` (0x662768)
   - `sub_6689A4` (bust sinusoidal physics — 未细看)
   - `sub_663BDC` / `sub_665600` / `sub_666068` / `sub_668470` (deque #4-#9 step functions)

5. **缺 HM2 锚点位置**：本地放 +1384，实际应该是 **+1440**（在 EmoteEngine，不是 Player）。Player 大小 1384，1440 > 1384，HM2 不可能在 Player 内。

## 不确定项

- **deque #7 (40B element)** 用途未确认（无 step 函数调用）。最可能是"frame keyframe pool"，被 setVariable 用作 deque#4..#10 element 内的 indirect pointer 池。需要追 setVariable 类型分发到这个 offset 的写入路径才能确认。
- **xmmword_14D68D0** 的值未读取，但用于 color controller (a1[136]) 的 reset 种子。十有八九是 `(1.0, 1.0, 1.0, 1.0)` (white RGBA) 或 `(255,255,255,255)` packed floats。
- **+1168 (double, EmoteEngine field)** 用作 scale controller 的分母，可能是"reference scale" / "designed bust scale"。需要看 setBustScale 才能确认。
- **+1184 / +1192 (double)** 是 bust spring constants（传给 stepBust 的"strength" 参数），具体语义未追。
- Controller heap 对象**没有 vtable**，因此**不可能存在继承层次**。它们是纯 POD/struct。本地实现也应该用 plain struct 而非 polymorphic class。

## IDA 命名修正记录（2026-05-30）

- `sub_667030` → `EmoteVarController_ctor_20Bdeque`
- `sub_6867B0` → `EmoteAngleController_ctor_12Bdeque`
- `sub_666BF8` → `EmoteVarController_step`
- `sub_666634` → `EmoteAngleController_step`
- `sub_67B748` → `EmoteEngine_stepHairParts`
- `sub_67BCE8` → `EmoteEngine_stepBust`
- `sub_662768` → `EmotePhysics_springStep`
- `sub_6766E0` → `EmoteEngine_applyVarControllers_pos_scale_color_angle`
- `sub_67D01C` → `EmoteEngine_progress`
- EmoteEngine_ctor +0x67E658..+0x67E718 已加 7 个 controller 创建点注释

## 后续工作（class-layout-auditor / binary-aligned-implementer 优先级）

P0 — **新增 2 个本地 controller 类**：
```cpp
struct EmoteVarController {  // 0x80 = 128B
    std::deque<KeyValue20B> queue;          // 80B (libstdc++ layout)
    int32_t count;
    int32_t state;
    float* currentValue;                    // new[count*4]
    float* targetValue;                     // new[count*4]
    float* startValue;                      // new[count*4]
    int32_t powCount;
    float   phase;
    float   invDuration;
    int32_t pad;
};

struct EmoteAngleController {  // 0x70 = 112B
    std::deque<KeyValue12B> queue;          // 80B
    int32_t state;
    float   currentRad;
    float   targetRad;
    float   startRad;
    float   invDuration;
    int32_t powCount;
    float   phase;
    int32_t pad;
};
```

P0 — **扩 EmoteEngine.h 加 5 个缺失 deque** + 7 个 controller 指针字段：
```cpp
class EmoteEngine {
    // existing 5 deque already there (rename their semantics per table above)
    std::deque<HairPartsNode>   _hairPartsNodes;       // NEW @ offset 0
    std::deque<BustChain1Node>  _bustChain1Nodes;      // NEW @ offset 80
    std::deque<BustChain2Node>  _bustChain2Nodes;      // NEW @ offset 160
    // ... existing 5 deque ...
    std::deque<SetupEntry40B>   _setupPool;            // NEW @ offset 480
    // ...
    std::deque<LookupCurve16B>  _lookupCurves;         // NEW @ offset 720

    // 4x vector reserve(10) inline (TBD)
    Player*                     _player;               // @+1064
    EmoteVarController*         _ctlPosition;          // @+1072 (count=2)
    EmoteVarController*         _ctlScale;             // @+1080 (count=1)
    EmoteVarController*         _ctlColor;             // @+1088 (count=4)
    EmoteAngleController*       _ctlAngle;             // @+1096
    EmoteVarController*         _ctlHairPartsTarget;   // @+1104 (count=2)
    EmoteVarController*         _ctlBust1Target;       // @+1112 (count=2)
    EmoteVarController*         _ctlBust2Target;       // @+1120 (count=2)

    // ...remaining scalar state (matrix, doubles, bytes)...
    // HM2 (label→value) at +1440 (NOT in Player!)
    libstdcxx_unordered_map<ttstr, double> _labelValues;  // @+1440
};
```

P1 — 在 progress() 中按 EmoteEngine_progress 骨架实现 6-deque + 4-controller + 物理调度循环。

P2 — 实装 EmotePhysics_springStep / sub_6689A4 物理算子。
