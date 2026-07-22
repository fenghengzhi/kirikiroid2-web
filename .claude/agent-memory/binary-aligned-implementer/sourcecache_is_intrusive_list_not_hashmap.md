---
name: sourcecache-is-intrusive-list-not-hashmap
description: SourceCache binary container = intrusive doubly-linked LIST (0x6A7BA8), NOT the FNV bucket map; that hashmap belongs to a DIFFERENT class (ResourceManager.findSource 0x6AAB3C)
metadata:
  type: project
---

motion::SourceCache 的二进制容器是 INTRUSIVE 双向链表，本地 `std::list<Entry>` 已是忠实选型 — 不要改成 FNV bucket map。

**Why:** 2026-06-05 收到任务要把 SourceCache 的 `std::list<Entry>` 重写为「KiriKiri inline FNV-hash bucket map（key=name）+ 嵌套 per-group hashmap」，线索指向 findSource@0x6AAB3C 的 a1+88/+96 bucket。fresh decompile 证伪该前提：

- NCB 注册铁证：`SourceCache_ncb_registerMembers @0x6A85A8` 只注册 4 个成员 = ctor(0x6A78F4) + `loadSource→sub_6A7BA8` + `clearCache→sub_6A8438` + `bufLayer`(prop)→sub_6A84FC。**SourceCache 根本没有 findSource 成员。**
- SourceCache 真正的容器在 `Motion_ResourceManager_loadSource @0x6A7BA8`（被 SourceCache 和 RM 两个类共同注册），ctor `sub_6A78F4 @0x6A7944`：`*(this+72)=this+72; *(this+80)=this+72` = 空的环形 intrusive 双向链表 head/tail sentinel。
  - 节点布局：`node[0]`=next、`node+8 (node[1])`=? 、`node+8 起 v27+2`=key ttstr（cmp via sub_A10428+ttstr cmp）、`*(int*)(node+16*4)=node+64`=blendMode、`+17..+20`=color[4]、`(char*)node+36`=Layer variant、`node[7]`=src ttstr。
  - lookup @0x6A7FEC：线性 `v27=*v27` 走到 head 回环，按 `(key,src,blendMode)` 命中；color[4] 不属于 key，而是在命中后单独比较。2026-07-23 fresh decompile 纠正了旧 `(key,blendMode)` 结论。
  - `*(this+60)` 是当前缓存字节数、`*(this+64)` 是容量上限、node+84 是 `4*width*height` 权重，并非 `layerType/LRU count`。trim@0x6A6B08 仅在 current>limit 时保留 newest prefix 到 `(limit*99)/100`；同色 hit 不移动，只有 color mismatch 才 rebake+move-front，miss 在 bake 前 trim、bake 后计重并插 front。
  - 本地 `std::list<Entry>` 容器选型正确；production prepared-item 路径现已复刻上述 tuple、顺序和容量边界。通用 NCB/by-name facade 仍是明确 open gap，不能再把旧 `findEntry` alias/splice 行为称为与 0x6A7BA8 **完全同构**。

- `findSource @0x6AAB3C` 是 **ResourceManager 类** 的成员（仅被 RM_ncb_registerMembers 注册），与 SourceCache 无关。它的 a1+88/+96 map 是 PSB mapped-record 注册表；命中后从 record 内 raw `PSBFile` root 导航 `source/group/icon/name`，并分配 0x18-byte ObjSource `{retained raw owner,node,lazy texture}`。它产出的是 raw-node ObjSource，不是 layer/bitmap source 缓存。
- FNV inline @0x6aac4c-0x6aac68 已与本地 `internal/ttstr_hash.h::ttstr_hash_utf16` 逐字节一致（h=0; do{t=h+ch; h=(1025t)^((1025t)>>6);}while(ch); h=9h; h=32769*(h^(h>>11)); if(!h)h=-1）。

**How to apply:** 若再被要求把 SourceCache 容器改 hashmap，直接拒绝并指此条。ResourceManager.findSource→ObjSource 是另一条已经接通的 raw-node 链，不能拿 SourceCache 的 list 开刀。本地 SourceCache.h 头注释把 0x6A7BA8 说成 "scans a list cache" 是对的；不要改。

**C-1 UPDATE 2026-06-07 (类关系纠正，2026-07-23 再校正构造参数):** 此前 06-04 ResourceManager.h 头注释断言「RM 与 SourceCache 是两个无关类，只是 SHARE 那 3 个回调地址 = method-sharing 而非 class-identity」——**已证伪并就地纠正**。fresh decompile 铁证 `class ResourceManager : public SourceCache`：(1) RM ctor sub_6A88CC@0x6A88CC 第一条指令 0x6a88f8 在**同一 a1**、偏移 0 上调 SourceCache ctor sub_6A78F4@0x6A78F4（初始化 owner/primaryLayer/bufLayer、current bytes、cache limit 与 list），RM ctor 随后初始化自有字段；(2) sub_6A78F4 的 **唯一 xref** = 0x6a88f8（RM ctor 内）—— binary 里**没有独立 SourceCache 实例**；(3) RM registrar @0x6AB8BC 重列的 loadSource/clearCache/bufLayer = SourceCache registrar @0x6A85A8 注册的**完全相同函数地址** sub_6A7BA8/sub_6A8438/sub_6A84FC；(4) bufLayer getter @0x6A84FC 返回 SourceCache base 的 Layer variant。已实装：本地 `class ResourceManager : public SourceCache`，NCB 的三成员解析到继承实现；EmoteObject_init@0x67DBAC 现按原顺序求值 `global.kag` 并把字面 20 MiB budget 传入 RM/Base ctor，旧“默认 ctor 导致 owner/bufLayer 缺失”和 `+64 layerType` 结论均已证伪删除。Player_ctor@0x6CED30 的同一 RM dispatch 三次独立 CopyRef 也已分别保留，恢复各自 AddRef/Release。`Player::unloadAll` 走 `nativeRM()->unloadAll()`；binary clearCache 只清 base list，模块清理是 unloadAll@0x6A8CF8。RM NCB 继承成员仍无 Android runtime fixture 覆盖，不能把本地构建/既有 logo 守护冒充 oracle。
