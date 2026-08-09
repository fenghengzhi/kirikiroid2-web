---
name: player-value-structs-spec
description: Phase B Step 2 — motion::Player 三个内嵌容器 value 类型完整字段反编译规格 (HM1 EvalCascadeState 72B, HM3 PerNodeLayerState 688B, ControllerEntry @+1296 160B)，配套 ctor/dtor 路径与 MotionNode 映射
metadata:
  type: project
---

# motion::Player Value Structs Spec (Phase B Step 2)

完整字段表来自反编译，可直接驱动 C++ struct 编写。前置：[[player-containers-libstdcxx-spec]]、[[project_player_four_hashmaps]]、[[project_player_class_layout]]。

## 关键发现
1. **HM1 value 实际为 "evaluated PropGet result cache"**（不是 EvalCascadeState 之类的复杂状态）。dtor 揭示 V+48 是 `operator new` 分配的堆指针、V+0/+8 是 ttstr+dispatch、V+16/+24 是 `iTJSDispatch2**` 动态数组。**`memset(v18+2, 0, 0x48u)` 证明 V+16..+88 在 insert 时全清零**，仅 V+0..+16 (entry+16..+32) 初始化为 keyRef + dispatchRef。
2. **HM3 value 双向 init/restore 路径已锁定**。`Player_HM3_initValueFromNode @0x699510` (从 Node→snapshot) 与 `sub_6997F0 @0x6997F0` (从 snapshot→Node) 对偶。`a2` 在两函数中都是 `int*` 单位，与 dtor 的 `_QWORD*` 单位单位字段表需统一为字节偏移。
3. **ControllerEntry @+1296 = "VariableLabelScope" entries**（不是 controller animator）。sub_6CD750 (= `Player_initVariables`@0x6CD750) 遍历 player+528 的 "variable" 属性，每个 variable 的每条 "label" 子项 push 一条到 +1296 deque。160B/条远大于普通 label，因为它内嵌完整的 scope ttstr + 多 dispatch 解析缓存。
4. **`byte_1AB84A8` = `Player::defaultSyncActive`** (全局静态 bool)，仅 `Player_getDefaultSyncActive @0x6D93F8` / `Player_setDefaultSyncActive @0x6D9404` 访问；Player_ctor @0x6CED30 在 +0xCB0 区域读取它作为新实例默认值。

## EvalCascadeState (HM1 value, V=72B, entry=96B)

V 是 `entry + 16` 偏移。

| V offset | size | 类型 | 用途 | 证据来源 |
|---|---|---|---|---|
| +0 | 8 | ttstr (key copy) | 缓存 key 副本，hash 提取见 entry+88 | dtor 0x6DD200 `tTJSVariant_Release(*a1)` |
| +8 | 8 | iTJSDispatch2* | 主结果 dispatch（来自 a2[0] = 参数 dispatch） | upsert 0x6F5378 引用计数+1，dtor 0x6DD1F4 release |
| +16 | 8 | iTJSDispatch2** | dispatch 数组 begin | dtor 0x6DD1C0 + loop release |
| +24 | 8 | iTJSDispatch2** | dispatch 数组 end | dtor 0x6DD1C0 |
| +32 | 8 | iTJSDispatch2** | dispatch 数组 capacity_end | 推断（std::vector 第三指针，dtor 未直接读但 +16/+24 形成区间） |
| +40 | 8 | unused/padding | 0 (memset 清零保留) | upsert `memset(v18+2, 0, 0x48)` |
| +48 | 8 | void* (operator new heap) | 缓存的 PropGet 结果块/long double 值容器 | dtor 0x6DD1B4 `operator delete(v2)`（注意：直接 delete 不是 release，故非 dispatch） |
| +56 | 8 | unused/padding | 0 | memset |
| +64 | 8 | unused/padding | 0 | memset |

**Entry 控制字段** (entry+0..+16):
- entry+0: `_Hash_node*` next
- entry+8: 未使用（part of libstdc++ node layout）

**Entry hash 缓存** (entry+88..+96, 8B):
- entry+88: `size_t cached_hash` — 节点保存最终 hash。注意它与 key backing
  `tTJSVariantString::Hint@+68` 是两个不同缓存槽：先按
  `Ptr==null ? 0 : (Hint!=0 ? Hint : compute-and-store)` 得到 hash，非 null
  计算结果为 0 时改为 `0xFFFFFFFF`，再把该最终值写入 node+88。
- entry+92: 4B padding

### Ctor 序列 (Player_HM1_upsert_evalCascade @0x6F52AC)
1. 读 `key` 的 `ttstr+68` 缓存 hash；若 0 → 重算 1025-rolling hash
2. `bucket = hash % a1[1]`
3. `sub_6F51BC (Player_HM1_find_node)` 找现有节点
4. 未命中：`operator new(0x60)` 96B → 写 entry+0=0, entry+8=keyRef++ → `memset(entry+16, 0, 0x48)` → `sub_6F53C8 (Player_HM1_insert_node)` 链入桶 + 写 entry+88=hash
5. 返回 `entry+16` 即 V 起始，调用者继续 init V+8/V+16

### Dtor 序列 (Player_HM1_value_destroy @0x6DD1A0)
1. V+48 (=a1+56)：`operator delete`
2. V+16..+24 (=a1+16..24)：迭代 dispatch**，`tTJSVariant_Release(*v3)`，结束后 `operator delete(v3)` 释放数组
3. V+8 (=a1+8)：`tTJSVariant_Release` (主 dispatch)
4. V+0 (=a1+0)：`tTJSVariant_Release` (key ttstr)

## PerNodeLayerState (HM3 value, V=688B, entry=720B)

V 是 `entry + 16`。**统一用字节偏移**（init 用 `int* a2`，每个索引 ×4；dtor 用 `_QWORD* a2`，每个索引 ×8）。下表用字节偏移，并标注两路引用。

### MotionNode → snapshot 字段映射（关键 P0）

`a1` = MotionNode 基址；`v5 = *(int*)(a1+1392)` 是当前 frame index；`a1 + 536*v5` 是 per-frame slot。

| V offset | size | 类型 | init 来源 (Player_HM3_initValueFromNode @0x699510) | dtor 路径 (Player_HM3_value_destroy @0x6DD06C) |
|---|---|---|---|---|
| +0 | 4 | int nodeType | `*a2 = *(int*)(a1+28)` | — |
| +4 | 4 | padding | — | — |
| +8 | 8 | iTJSDispatch2* | dtor `a1[1]` release; init via 嵌套 sub_6DD06C(a2+3) 路径 | 0x6DD104 release |
| +16..+27 | 12 | (sub_A0F778 cleared) | dtor a1+21 → 推测 ttstr | 0x6DD0F8 (offset 188 in `int` = bytes 188 from V) |
| +28 | 4 | int (Node+340) | `a2[7] = *(_DWORD*)(v13+340)` | — |
| +32..+44 | — | ttstr area | dtor 0x6DD0E8/0x6DD0E0 (offsets 188/124 字节) | — |
| +44 | 8 | iTJSDispatch2* | `*(_QWORD*)(a2+11) = *(Node+356)` + refcount++ | 0x6DD10C release |
| +52 | 4 | int (Node+364) | `a2[13] = *(int*)(v13+364)` | — |
| +64 | 16 | OWORD (Node+376) | `*((_OWORD*)a2+4) = *(_OWORD*)(v13+376)` | — |
| +80 | 4 | int sourceRect.x (Player+100) | `a2[20] = *(int*)(a1+100)` | — |
| +84 | 4 | int sourceRect.y (Player+104) | `a2[21]` | — |
| +88 | 4 | int sourceRect.w (Player+108) | `a2[22]` | — |
| +92 | 4 | int sourceRect.h (Player+112) | `a2[23]` | — |
| +96 | 4 | int (Node+408) | `a2[24] = *(int*)(v13+408)` | — |
| +104 | 16 | OWORD (Player+1512) | `*((_OWORD*)a2+9)` 注 a2+26 in int 等于 +104 字节 | — |
| +120 | 8 | QWORD (Player+1528) | `*((_QWORD*)a2+15) = *(_QWORD*)(a1+1528)` | — |
| +128 | 1 | byte skipFlag (Node+344) | `*((byte*)a2+32) = *(byte*)(a1+536*v5+344)` | — |
| +129 | 1 | byte (Player+1508) | `*((byte*)a2+129)` | — |
| +136 | 16 | OWORD (Player+1544) | `*((_OWORD*)a2+9) = *(_OWORD*)(a1+1544)` (a2+34 in int = +136) | — |
| +152 | 16 | long double (Player+1560) | `*((long double*)a2+10) = *(long double*)(a1+1560)` | — |
| +168 | 8 | QWORD (Player+1536) | `*((_QWORD*)a2+17) = *(_QWORD*)(a1+1536)` (a2+21 in QWORD = +168) | — |
| +188 | 16 | ttstr (sub_A0F778-managed) | conditional via Node+1912 if nodeType==3 (`sub_A0FB64(a2+136, a1+1912)`, a2+136 in int = +544 — **see below**) | 0x6DD0F8 ttstr_clear (offset 188 字节) |
| +228 | 16 | ttstr | — | 0x6DD0E8 (offset 228 字节 = a1+228) |
| +268 | 16 | ttstr | — | 0x6DD0D8 (offset 268 字节) |
| +288 | 8 | iTJSDispatch2* | — | 0x6DD0CC `a1[36]` release |
| +296 | 16 | ttstr | — | 0x6DD0C4 `sub_A0F778(a1+37)` (37*8=296) |
| +320 | 8 | void* heap | — | 0x6DD0B4 `a1[40]` operator delete (40*8=320) |
| +364 | 16 | ttstr | — | 0x6DD0A8 (offset 364 字节) |
| +392 | 8 | iTJSDispatch2* | — | 0x6DD098 `a1[49]` release (49*8=392) |
| +504 | 8 | iTJSDispatch2* | — | 0x6DD08C `a1[63]` release (63*8=504) |
| +516 | 16 | ttstr | — | 0x6DD080 (offset 516 字节) |
| +544 | 16 | ttstr (Node+1912 snapshot) | `sub_A0FB64(a2+136, a1+1912)` (a2+136 in int = +544) | 0x6DD040 `sub_A0F778(a2+70)` (70*8=560? — 注：用 _QWORD*；a2+70 in QWORD = +560；与 +544 有 16B 错位，疑似 ttstr 占两个 QWORD slot) |
| +584 | 8 | void* heap | `a1[73]` (73*8=584) | 0x6DD030 `a1[73]` operator delete |
| +672 | 16 | ttstr (Node+2296 snapshot) | `sub_A0FB64(a2+168, a1+2296)` (a2+168 in int = +672); also when nodeType==4 写 OWORD×4 at +600..+664 from a1+2224..+2272 | 0x6DD02C `sub_A0F778(a2+86)` (86*8=688) |

**关键缺口**：dtor 与 init 偏移有 ~8B 错位（ttstr 在 libstdc++ 下是 `{header_ptr, size_or_inline}` 16B 结构，dtor 调 sub_A0F778 时传起始地址，是 ttstr 头）。复刻时按"V+544..+560 = ttstr(VariableTransformName_snapshot)"理解，下游 C++ 用 `ttstr` 类型字段即可。

### Ctor (Player_HM3_initValueFromNode @0x699510)
1. `*a2 = nodeType` (来自 Node+28)
2. 若 Player+2000 == 1 → `sub_6996E8(a2+142, Player+2024)` 拷贝 std::vector<QWORD>（labelIndices？）
3. nodeType==3 → snapshot Node+1912 ttstr 到 +544
4. nodeType==4 → snapshot Node+2296 ttstr 到 +672；若 Node+344(skipFlag)==0 额外拷贝 16B×4 transform 到 +600..+664
5. 引用计数 Node+356 dispatch，写 +44
6. 批量拷贝 Node+340/+364/+376/+408 + Player 全局 +100..+112/+1512/+1528/+1544/+1560 等 28 字段

### Dtor (Player_HM3_value_destroy @0x6DD06C)
顺序：高偏移到低 — V+516 ttstr → V+504 dispatch → V+392 dispatch → V+364 ttstr → V+320 delete → V+296 ttstr → V+288 dispatch → V+268/+228/+188 ttstr → V+8/+0 dispatch+key 释放。

### Entry 控制 / hash
- entry+0: next ptr
- entry+8: 8B padding (key 不在 entry，key 在 V+8 dispatch 内部或 V+x ttstr — 这是 `unordered_map<ttstr, V>`)，hash 缓存在 entry+712 (= 720-8)

实际上 HM3 的 key 是 `Player_buildNodePathKey @0x6B5C1C` 构造的 ttstr，**key 存储在 V 内**而非 entry 顶部（前 4 hashmap 之一布局，[[player-containers-libstdcxx-spec]] 标记"node-path key"）。Pruner @0x6B826C 通过 `*(_BYTE*)(v14+56*v16+68)` (V+56*nodeType+68) 读 skipFlag 决定是否清理 — 表明 V 内嵌一段 56B×N 的 per-nodeType-frame 数组（推测 V+? 起的多帧 cache），与本文 +0..+170 单帧 snapshot 不同。这区段超出 P0 必需深度。

## ControllerEntry (+1296 deque element, 160B)

deque 元素由 `Player_initVariables @0x6CD750`（即 sub_6CD750）push_back，每条对应一个 `(variable, label)` pair。

| 字节 offset | size | 类型 | 用途 | 证据 |
|---|---|---|---|---|
| +0 | 8 | ttstr (scope/cascade key) | join "::" 后的 cascade key (e.g. "groupA::varX") | 0x6CDBB4 写入 (`v14-160 = *combined ttstr*`) |
| +8 | 8 | unused / padding | memset 0 | 0x6CD9C0 `memset(v12, 0, 0xA0)` |
| +16 | 16 | ttstr labelName | 来自 v47 (dispatch) PropGet("label") 通过 enum index | 0x6CDA98 `sub_A0FB64(v14-136, v39)` (v14-136 = +24？修正：v14 是新 element 末尾 = base+160，故 v14-136=base+24，对齐 ttstr 16B 字段→实为 +16 ttstr 头) |
| +56 | 4 | int (cleared) | 0x6CDA50 `*(_DWORD*)(v14-152) = 0` → +8 (?)；重审：v14-152 = base+8 已被 memset 覆盖 | — |
| +64 | 16 | ttstr scope | sub_A0BAF4 → +672 区域（scope 解析） | 0x6CDAEC |
| +68 | 1 | byte 1 (always 1) | 0x6CDA48 `*(_BYTE*)(v14-92) = 1` → base+68 | flag (active?) |
| +108 | 1 | byte 1 | 0x6CDA4C `*(_BYTE*)(v14-36) = 1` → base+124（v14-36=base+124 if v14=base+160） | flag (validated?) |
| +124 | 1 | byte 1 | 同上重新计算：v14-36 实际是 base+124 | — |
| +0..+160 | 160 | 剩余区域 | 大部分为 0 (memset 0xA0=160 全清零) | — |

**注**：deque 元素 sizeof = 160B；block size 480 = 3 × 160（[[player-containers-libstdcxx-spec]] 已确认）。push 路径：
- 若 `tail == back_block_end - 160` → 末块满，可能扩展 `map`：`operator new(0x1E0=480)` 新块 → push 进 +1368 (back map ptr) → 更新 +1344/+1352/+1360
- 否则 `memset(tail, 0, 0xA0)` 清零 → tail += 160 → 写入字段

### 用途判定 — 不是 controller animator

ControllerEntry @+1296 装的是 **variable scope binding entries**，每条记录"某个 variable 在某 scope 下绑定到哪个 label"。具体使用模式：
1. PSB 加载时 setMotion 通过 Player_initVariables 解析 motion 的 "variable" 数组
2. 每个 variable 包含一组 (name, label, scope) 三元组
3. push 进 +1296 deque 后，evalKey 路径（[[project_player_four_hashmaps]] HM2/HM4）通过 cascade key lookup 找到对应 label，再用 label 找到 Node 上的 animator/value 槽

### 与 EmotePlayer 5 个 deque 的关系

EmotePlayer 5 个 controller deque (+256/+336/+416/+576/+656) 是**真正的 animator state**容器（per-nodeType 分组）：
- +256 (16B 元素): animator type 4
- +336 (16B): animator type 5
- +416 (24B): animator type 6
- +576 (24B): animator type 7
- +656 (48B): animator type 8

与 Player+1296 (160B) 容器**不同元素类型**，**不同用途**。1296 deque 是 lookup 表，5 个 deque 是状态机。

setVariable @0x671228 通过 PropGet 拿到 HM2/HM4 缓存里的 `{type, animatorIndex}` 后跳转到对应 deque 的元素 — Player+1296 deque 不在 setVariable 直接访问路径上，仅在初始化阶段 populate。

## 附加发现

### +864 sub_7E2344 容器 (Player_1384B_flat_spec.md 标"未确认 44B 容器")

`sub_7E2344` 反编译揭示：仅清零 32B（OWORD×2）+ 8B QWORD = **40B + 1B = 41B 数据 + 3B 对齐 = 44B**。这是简单的零初始化 helper，结构体本身在调用方 alloc。需进一步看 dtor/use 确定语义；此次未深挖。

### +760 owned 对象 (`sub_6CFFB8` dtor)

sub_6CFFB8 调用：
- `Player_resetRenderState_guess(a1)` — 清渲染状态
- `sub_6DB7B8(a1+112, *(QWORD*)(a1+128))` — std::vector dtor (头/尾指针对)
- `sub_6DB7B8(a1+64, *(QWORD*)(a1+80))` — 另一 vector
- `sub_A0F778(a1+40)` / `(a1+20)` / `(a1)` — 3 个 ttstr

**结论**：+760 指向一个 ~144B 的 "RenderStateBundle" struct（含 3 ttstr + 2 std::vector），属于渲染状态归档对象。不直接影响 1384B Player struct 重构。

### `byte_1AB84A8` 语义确认

= `motion::Player::defaultSyncActive` 静态成员，bool 类型。
- 读：Player_getDefaultSyncActive @0x6D93F8（返回值）；Player_ctor @0x6CED30 内 0x6CF0B0/0x6CF0C8 在初始化新实例的 syncActive 字段时读取
- 写：Player_setDefaultSyncActive @0x6D9404（单 STR 写入）

## Phase A 落地的关键风险

1. **HM3 value 688B 含两段不同布局**：前 ~170B 是 init 路径填充的"current frame snapshot"，+188..+688 含多个 ttstr/dispatch slot（pruner 用 V+56*N+68 访问 N-frame skip 数组）。**P0 阶段如仅实现 ttstr key + small fixed struct，会丢失 dtor 释放的 ≥8 个 ttstr/dispatch 引用**，导致内存泄漏。建议先按 dtor 列表保留所有字段（即使语义不明），用 placeholder bytes 占位。
2. **HM1 value 72B 含一个 vector<iTJSDispatch2*>**（V+16/+24/+32）：本地若用 `std::vector` 实现是兼容的，但需保证 iTJSDispatch2* refcount 管理与 dtor 一致。
3. **ControllerEntry 160B 中 ~120B 是 padding/未识别字段**：先按完整 160B placeholder 占位，避免 deque block stride 不匹配。
4. **+1296 deque 不应与 EmotePlayer 5 deque 合并**：用途不同；本地若已合并需拆分回 motion::Player 单独字段。
5. **HM3 entry hash 在 entry+712 而非 +88**：与 HM1/HM2/HM4 (entry+88) 不同，因为 HM3 value 是 688B，hash cache 后置避免与 V 字段冲突。
