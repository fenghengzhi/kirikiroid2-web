---
name: m9-source-subsystem
description: motion source/resource subsystem (cluster K/M9) — ResourceManager public-inherits SourceCache; outer unordered_map mapped record = PSBFile + Win texture map + KRKR descriptor map; raw pixel navigation remains open
metadata:
  type: project
---

# M9 source resource subsystem (RE 2026-06-03)

**纠正结论：二进制中是 `class ResourceManager : public SourceCache`，不是两个完全无关的类，也不是“同一个 NCB 类”。** `ResourceManager_ctor @0x6A88CC` 在同一 `this` 上先调 `SourceCache_ctor @0x6A78F4`，再构造派生字段。本地现已恢复 public 继承。

## RM/SourceCache 实例字段布局(单一类)
- +40  ttstr  bufLayer (NCB property `bufLayer`, getter @0x6A84FC 直接返回 ttstr at +40)
- +60  int    layer-list LRU/size counter
- +72/+80  intrusive **doubly-linked list head** (空时 *(this+72)==this+72)。SourceCache layer-image 缓存。node 布局:[0]next [1]prev,[+8](=node+2 qword) key ttstr,[+16]blendMode,[+17..+20]color[4],[+36]Layer variant,[+56](=node[7])src ttstr,[+84]width-like。匹配键=(key ttstr, blendMode)。loadSource/clearCache/evictLRU 操作此 list
- +88/+96  **outer std::unordered_map**: key=模块 context/path `ttstr`；mapped record 声明顺序为 `PSBFile` + Win 纹理表 + KRKR descriptor 表。构造证据 `sub_6EBCFC @0x6EBCFC`，析构证据 `sub_6DB3E8 @0x6DB3E8`。
- +104/+112 是同一 libstdc++ unordered_map 的 global node chain/element count，不是独立第二容器。
- +136  HashMap A 的 inline first-bucket sentinel(`if(v7 && this+136!=v7) delete`)
- +144  ttstr(unloadAll 里 sub_A0F778 释放;random 在 +144 上 PropGet "random")
- +160  int(random 的 spec-ish check)
- +168..+184 std::_Rb_tree<uint>(usedLayerIds 集合,unloadAll 调 _M_erase(this+168, *(this+184)))
- +224  int **spec**(1=krkr, 2=win)。loadResource 从 PSB "spec" 字段写入

## FNV-variant hash(全子系统统一)
ttstr 缓存的 hash 在 ttstr 偏移 +68(字节)/ +17(uint下标) 处。算法:
```
h=0; for ch in str: h = ((1025*(h+ch)) ^ ((1025*(h+ch))>>6))
h = 9*h; h = 32769*(h ^ (h>>11)); if(h==0) h=-1
bucket = h % bucketCount
```

## RM 方法地址(全部重命名完毕)
- load(loadResource)  `ResourceManager_loadResource`(原名保留) — HashMap A 命中即返回;miss 则 sub_598538 打开 PSB → 校验 id=="motion"/spec(krkr|win)/version<=3.03 → 存入 HashMap A
- loadSource(SourceCache) `Motion_ResourceManager_loadSource@0x6A7BA8` — 读 source-dispatch 的 key/src/blendMode/color[4],在 +72 layer-list 找 (key,blendMode) 命中复用 Layer,miss 则 Motion.Layer 构造新 Layer,LRU 用 +60
- clearCache  `@0x6A8438` — 只清 +72 layer-list(每个 Layer vtable+112 释放image),不碰 hashmap
- bufLayer getter `@0x6A84FC` — return ttstr@+40
- unload      `@0x6A959C` — HashMap A erase(sub_6EBF2C)
- unloadAll   `@0x6A8B94`(IDA误并入 loc_6A8CF8) — 清 +104 list、memset HashMap A buckets、Rb_tree erase、+144 ttstr、+72 layer-list 全清
- findSource  `@0x6AAB3C` — split name by "/",前缀须=="src";dict["source"][layerName]["icon"][colorName] 链;blank→构造空白 Layer dict;命中→构造 ObjSource(operator new 0x18,3字段:[0]dict variant,[1]?,[2]=0)
- findMotion  `@0x6A9ED4` — HashMap A by motion-name → dict["object"][name]["motion"][label],拷进 caller 的 vector(20B元素)。也 fallback 走 +104 list
- isExistMotion `@0x6A96F8` — 同 findMotion 的存在性版本,先 HashMap A 后 +104 list
- random      `@0x6AB56C` — 无关(KAG.random PropGet)
- evictLRU    `@0x6A6B00`(guess) — loadSource 调,按 99%/lru 清 layer-list

## Motion_Player_findSource @0x6948E8(Player 上的 findSource,非 RM 的)
- 签名:`(out* a1, Player* a2, ttstr** a3=name, ttstr** a4=icon/2nd-name)`
- a1 输出结构:+0 found bool,+1 blank bool,+4 source variant,+20 ?,+24 **texture handle**,+32 width,+40 height,+48 originX,+56 originY,+64..88 clip(left/top/right/bottom doubles),+96..108 截断rect int,+112 source variant cache
- 流程:PropGet(player+636 RM-self, idx dword_1AB8098) → v10=RM native。spec(+224): ==2 走 PSB texture 上传路径;==1 走 KAG.findSource 回调路径;其它/blank → 走 player-self dispatch "findSource" 脚本回调(LABEL_142)
- spec==2 路径:outer map 按模块 context 取 mapped record，再读 raw dict["source"][name]。Win 内表按 name 缓存 texture。miss 严格读 texture{truncated_width/height(丢弃),width,height,type,pixel}；RGBA8 做 TVPReverseRGB(count=pixelBytes/4)，A8L8 按 `[alpha,luminance]→[luminance,luminance,luminance,alpha]` 展开，其他格式抛异常。本地该 Win raw 链已对齐；KRKR/spec=1 atlas 仍 open。

### nested texture-cache(D1 关键)
- **缓存键=纯 name ttstr**(a3),不含 blendMode/color。findNode/findOrInsert 第三参都只传 a3,node[1] 只存这一个 ttstr。证据确凿
- node 布局(operator new 0x20=32B):[0]+0 next,[1]+8 key ttstr(AddRef),[2]+16 value=裸 iTVPTexture2D* handle(findOrInsert 返回&node[2]),[3]+24 keyhash(findNode 比 node+24==hash)
- **CPU bitmap upload 完即弃**:v65=sub_A0DE48 分配的解码buf,upload 后立刻 sub_A0DE90(v65) 释放。nested map 只存 GPU handle,不缓存像素。每次 miss 重新解码+alloc/free
- handle Release/AddRef = vtable+16,refcount 在 handle+8(v74[2])
- **缓存键分离**:blendMode/color 缓存属于 Player+72 layer-list(loadSource 路径),与本 by-name texture nested-map(entry+24)物理分离,findSource 全程不碰 +72。D1 改 hashmap 时:键=纯 name,value=裸 GPU handle,CPU bitmap 不缓存
- by-name HashMap A(sub_6EB8F4) 的 node key-hash 在 node+136;nested map(sub_6E2060) 的 node key-hash 在 node+24。同算法不同 node 布局,两份 findNode

## ObjSource(0x69CCB8 register)= TJS dict facade,非 struct
ObjSource 实例 = operator new(0x18) 3 qword:[0]=tTJSVariant 持 PSB source dict,[1]=?,[2]=0。所有 NCB getter(originX/originY/width/height/clip + drawLayer method)都是 `dict[key]` 读取(sub_598C58 取字典项),**没有 _key/_src/_blendMode/_color 结构字段**。本地 SourceCache.h:116 ObjSource 的 4 个私有字段是 port 发明;它"缺的6个成员"实际上不存在——真实 ObjSource 只有 1 个 dict variant backing。getters: originX@0x69D014 originY@0x69D0D8 width@0x69D19C(需变体type==7 dict,否则返回32) height@0x69D27C clip@0x69D35C(构造Rect对象) drawLayer@0x69D6D8(type==7时 SetSize+绘制)

## 本地 vs 二进制差异（2026-07-18 纠正）
1. RM public-inherits SourceCache 已恢复；旧“两个无关类”结论已过期。
2. Player_findSource 的 Win/KRKR 纹理表已移入 outer map 的 `LoadedResourceRecord`，并恢复 AddRef/Release 与 unload 生命期。它与 SourceCache layer-list 是两条不同缓存链，不得再用 `std::list/shared_ptr` 概括 Player_findSource 的当前差异。
3. ObjSource 已恢复为单 `tTJSVariant` dict facade 并注册 6 个成员；`clip/drawLayer` 方法体仍是独立 open 项。
4. RM 继承面、unloadAll/isExistMotion/findMotion/findSource/random NCB 表面已恢复；各方法体继续按独立证据审计。
5. 2026-07-18 后续纠正：Win/spec=2 与 KRKR/spec=1 都已直接消费 `LoadedResourceRecord::file` raw nodes；KRKR 恢复 all-group 枚举、两种 RL、palette expand 与透明 2x2。source 像素导航已关闭，整页上传是单独的 Web API 边界。
