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

- `findSource @0x6AAB3C` 是 **ResourceManager 类** 的成员（仅被 RM_ncb_registerMembers 注册），与 SourceCache 无关。它的 a1+88/+96 FNV bucket map 是 **PSB 资源注册表**（key=完整 source 名 ttstr，hash 缓存在 ttstr+68），命中后 `v28=*(*v27+16); v29=*(v28[1]+64)` 走到第二层 dict，把 `dict["source"][group]["icon"]` 解析成 **ObjSource**（operator new 0x18 facade，对应本地 ObjSource 类）。它产出的是 ObjSource 对象，不是 layer/bitmap source 缓存。
- FNV inline @0x6aac4c-0x6aac68 已与本地 `internal/ttstr_hash.h::ttstr_hash_utf16` 逐字节一致（h=0; do{t=h+ch; h=(1025t)^((1025t)>>6);}while(ch); h=9h; h=32769*(h^(h>>11)); if(!h)h=-1）。

**How to apply:** 若再被要求把 SourceCache 容器改 hashmap，直接拒绝并指此条。真要复刻 FNV bucket map，对象是 ResourceManager.findSource→ObjSource 这条链（本地 ObjSource 当前 dead，未被 findSource 构造），那是独立工作项，不能拿 SourceCache 的 list 开刀。本地 SourceCache.h 头注释把 0x6A7BA8 说成 "scans a list cache" 是对的；不要改。
