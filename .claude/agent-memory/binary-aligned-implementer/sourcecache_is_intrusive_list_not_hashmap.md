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
  - lookup @0x6A7FEC：线性 `v27=*v27` 走到 head 回环，按 (key, blendMode) 命中；命中后比 color[4]，不同则重建。`*(this+60)`=LRU 计数（evict via Motion_ResourceManager_evictLRU_guess）。
  - 这与本地 `std::list<Entry>` + (key,blendMode,packedColors) 匹配 + findEntry 的 LRU splice **完全同构**。本地选型正确。

- `findSource @0x6AAB3C` 是 **ResourceManager 类** 的成员（仅被 RM_ncb_registerMembers 注册），与 SourceCache 无关。它的 a1+88/+96 map 是 PSB mapped-record 注册表；命中后从 record 内 raw `PSBFile` root 导航 `source/group/icon/name`，并分配 0x18-byte ObjSource `{retained raw owner,node,lazy texture}`。它产出的是 raw-node ObjSource，不是 layer/bitmap source 缓存。
- FNV inline @0x6aac4c-0x6aac68 已与本地 `internal/ttstr_hash.h::ttstr_hash_utf16` 逐字节一致（h=0; do{t=h+ch; h=(1025t)^((1025t)>>6);}while(ch); h=9h; h=32769*(h^(h>>11)); if(!h)h=-1）。

**How to apply:** 若再被要求把 SourceCache 容器改 hashmap，直接拒绝并指此条。ResourceManager.findSource→ObjSource 是另一条已经接通的 raw-node 链，不能拿 SourceCache 的 list 开刀。本地 SourceCache.h 头注释把 0x6A7BA8 说成 "scans a list cache" 是对的；不要改。

**C-1 UPDATE 2026-06-07 (类关系纠正):** 此前 06-04 ResourceManager.h 头注释断言「RM 与 SourceCache 是两个无关类，只是 SHARE 那 3 个回调地址 = method-sharing 而非 class-identity」——**已证伪并就地纠正**。fresh decompile 铁证 `class ResourceManager : public SourceCache`：(1) RM ctor sub_6A88CC@0x6A88CC 第一条指令 0x6a88f8 在**同一 a1**、偏移 0 上调 SourceCache ctor sub_6A78F4@0x6A78F4（初始化 +20/+40/+64/+72/+80 base 字段），RM ctor 随后从 +88 起初始化自有字段；(2) sub_6A78F4 的 **唯一 xref** = 0x6a88f8（RM ctor 内）—— binary 里**没有独立 SourceCache 实例**，SourceCache 只作 RM 的 [0,88) base subobject 存在；(3) RM registrar @0x6AB8BC 重列的 loadSource/clearCache/bufLayer = SourceCache registrar @0x6A85A8 注册的**完全相同函数地址** sub_6A7BA8/sub_6A8438/sub_6A84FC = 继承基类成员在派生类 NCB 表上的重列（C++ 继承签名，非巧合共享）；(4) bufLayer getter @0x6A84FC 读 a1+40 = SourceCache base 的 Layer variant（tTJSVariant，非 ttstr name）。已实装：本地 `class ResourceManager : public SourceCache`，删除 RM 自有 `_bufLayer`(ttstr)/`_sourceCacheList`/`SourceCacheEntry`/loadSource(ttstr)/clearCache()const/getBufLayer()→ttstr（都是继承前的 two-class artifact），NCB 的 loadSource/clearCache/bufLayer 现解析到继承的 SourceCache base 成员。Player+656 ≠ 独立 SourceCache：Player ctor @0x6CED30 把**同一 RM dispatch** 拷进 +636/+656/+992（sub_A0F5E0 各一份）。本地 Player 独立 `_sourceCacheNative`（live render-source workhorse）**保留不动**——把它并入 RM-as-SourceCache 是更大的 P3-B ownership 重构，超出 C-1 范围。`Player::unloadAll` 里 `nativeRM()->clearCache()` 改为 `nativeRM()->unloadAll()`（binary clearCache 只清 base +72 list 不清 HashMap A；模块清理是 unloadAll@0x6A8CF8 的活）。2026-07-18 纠正：`0x6A8B94` 是析构函数并清 layer-id set，`unloadAll@0x6A8CF8` 只清 HashMap A。GAP（oracle-inert 诚实记录）：RM base ctor 用默认 ctor，未把 RM dispatch 喂进 base _owner/+40 bufLayer（binary base ctor 取 (this,rmDispatch,0)）——属 P3-B；RM NCB 实例的继承成员无 fixture 覆盖（live 路径走 Player._sourceCacheNative）。web/debug + krkr2_wasmtime_guest 构建 clean，motion_playback m2logo 93f + yuzulogo 243f PASS bit-identical。
