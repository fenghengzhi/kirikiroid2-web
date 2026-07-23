---
name: m9-source-subsystem
description: motion source/resource subsystem (cluster K/M9) — ResourceManager public-inherits SourceCache; mapped record = PSBFile + Win/KRKR maps; 2026-07-23 corrected shared atlas caller/SourceState alias gaps after the earlier raw-chain closure claim
metadata:
  type: project
---

# M9 source resource subsystem (RE 2026-06-03)

**纠正结论：二进制中是 `class ResourceManager : public SourceCache`，不是两个完全无关的类，也不是“同一个 NCB 类”。** `ResourceManager_ctor @0x6A88CC` 在同一 `this` 上先调 `SourceCache_ctor @0x6A78F4`，再构造派生字段。本地现已恢复 public 继承。

## RM/SourceCache 实例字段布局(单一类)
- +40  tTJSVariant bufLayer (NCB property `bufLayer`, getter @0x6A84FC CopyRef)
- +60  uint32 current cache bytes；+64 uint32 cache byte limit
- +72/+80 are the libstdc++ sentinel links of SourceCache's source-level
  `std::list<Entry>`, not a hand-written intrusive container. Entry semantic
  order is full Variant key, Layer Variant, src ttstr, blendMode, color[4],
  byteWeight. Match identity is strict `(full Variant key, src, blendMode)`;
  color is compared separately.
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
- loadSource(SourceCache) `Motion_ResourceManager_loadSource@0x6A7BA8` — 读 descriptor-dispatch 的 full-Variant key/src/blendMode/color[4]，在 +72 std::list 按 `(key,src,blendMode)` 命中复用 Layer；color mismatch 或 miss 才把借用 source object 交给 `sub_6A6BE0` 重烘焙，+60 是 current byte count（不是 LRU counter）
- clearCache  `@0x6A8438` — 每个 Layer dispatch `Invalidate(self)`，清 list，current bytes=0；不碰 hashmap
- bufLayer getter `@0x6A84FC` — CopyRef Layer Variant@+40
- unload      `@0x6A959C` — HashMap A erase(sub_6EBF2C)
- destructor `@0x6A8B94` — 依次销毁 raw-owner map、layer-id set、random Variant 和 SourceCache 基类状态；它不是 `unloadAll`。
- unloadAll   `@0x6A8CF8`（独立序言，注册点 `0x6AB8BC` 字面绑定 `unloadAll`）— 只沿同一 unordered_map 的 +104 global node chain 删除 mapped records、memset +88 buckets，并清零 +104/+112；不清 Rb_tree、random 或 +72 layer-list。
- findSource  `@0x6AAB3C` — split name by "/",前缀须=="src"；从 mapped record 的 raw root 导航 `source/group/icon/name`，fixed key strict、dynamic key has+strict；命中后构造 ObjSource(operator new 0x18，字段为 raw owner/node pair + lazy texture)，再以 sticky=false/err=false 创建 adaptor；adaptor 失败只返回 void，不回收新 ObjSource
- findMotion  `@0x6A9ED4` — HashMap A by motion-name → dict["object"][name]["motion"][label],拷进 caller 的 vector(20B元素)。后备扫描走同一 unordered_map 的 +104 global node chain，不是独立 list。
- isExistMotion `@0x6A96F8` — 同 findMotion 的存在性版本；bucket 路径后的 +104 扫描仍属于同一 unordered_map，不是第二容器。
- random      `@0x6AB56C` — 无关(KAG.random PropGet)
- trim        `@0x6A6B08` — current>limit 时按 99% 阈值做 greedy subsequence 扫描，不是连续 prefix/LRU cut

## Motion_Player_findSource @0x6948E8(Player 上的 findSource,非 RM 的)
- 签名:`(out* a1, Player* a2, ttstr** a3=name, ttstr** a4=icon/2nd-name)`
- a1 输出结构:+0 found bool,+1 blank bool,+4 source variant,+20 ?,+24 **texture handle**,+32 width,+40 height,+48 originX,+56 originY,+64..88 clip(left/top/right/bottom doubles),+96..108 截断rect int,+112 source variant cache
- 流程:PropGet(player+636 RM-self, idx dword_1AB8098) → v10=RM native。spec(+224): ==2 走 PSB texture 上传路径;==1 走 KAG.findSource 回调路径;其它/blank → 走 player-self dispatch "findSource" 脚本回调(LABEL_142)
- spec==2 路径:outer map 按模块 context 取 mapped record，再读 raw dict["source"][name]。Win 内表按 name 缓存 texture。miss 严格读 texture{truncated_width/height(丢弃),width,height,type,pixel}；RGBA8 做 TVPReverseRGB(count=pixelBytes/4)，A8L8 按 `[alpha,luminance]→[luminance,luminance,luminance,alpha]` 展开，其他格式抛异常。2026-07-23 纠正：raw owner/map 拓扑虽已对齐，旧结论遗漏了 Win texture-before-icon 顺序、逐字段写序及 spec2 不写 path；KRKR 又遗漏共享第二调用者、SourceState alias 和 branch-local resource lookup。当前这些审计站点已补齐，但不得再把“raw 链闭合”写成未经全调用者复核的全局 100% 证明。

### nested texture-cache(D1 关键)
- **缓存键=纯 name ttstr**(a3),不含 blendMode/color。findNode/findOrInsert 第三参都只传 a3,node[1] 只存这一个 ttstr。证据确凿
- node 布局(operator new 0x20=32B):[0]+0 next,[1]+8 key ttstr(AddRef),[2]+16 value=裸 iTVPTexture2D* handle(findOrInsert 返回&node[2]),[3]+24 keyhash(findNode 比 node+24==hash)
- **CPU bitmap upload 完即弃**:v65=sub_A0DE48 分配的解码buf,upload 后立刻 sub_A0DE90(v65) 释放。nested map 只存 GPU handle,不缓存像素。每次 miss 重新解码+alloc/free
- handle Release/AddRef = vtable+16,refcount 在 handle+8(v74[2])
- **缓存键分离**:blendMode/color 缓存属于 Player+72 layer-list(loadSource 路径),与本 by-name texture nested-map(entry+24)物理分离,findSource 全程不碰 +72。D1 改 hashmap 时:键=纯 name,value=裸 GPU handle,CPU bitmap 不缓存
- by-name HashMap A(sub_6EB8F4) 的 node key-hash 在 node+136;nested map(sub_6E2060) 的 node key-hash 在 node+24。同算法不同 node 布局,两份 findNode

## ObjSource(0x69CCB8 register)= raw node facade（2026-07-19 纠错）
旧“单 tTJSVariant dict facade”结论被构造/析构与 consumer fresh decompile 证伪。ObjSource 实例 = operator new(0x18) 3 qword：`[0..1]=PSBRawOwner*/node*`（构造时 owner AddRef），`[2]=lazy texture*`。析构 `0x6E407C` 先 Release texture，再递减 raw owner。originX/originY `0x69D014/0x69D0D8` 直接 strict raw read；width/height `0x69D19C/0x69D27C` 仅在非-dictionary 时返回 32；clip `0x69D35C` try-gate `clip` 后 strict 读四边并顺序写新 Dictionary；ensureTexture `0x6DA454` 直接读取 raw pixel/compress/pal，恢复重复 pal gate、RL8/RL32、palette、aligned buffer、pitch-copy 和 `tTVPBitmap→texture`；drawLayer `0x69D6D8` 严格取 Layer native instance，以 texture 自身宽高 SetSize。

## 本地 vs 二进制差异（2026-07-18 纠正）
1. RM public-inherits SourceCache 已恢复；旧“两个无关类”结论已过期。
2. Player_findSource 的 Win/KRKR 纹理表已移入 outer map 的 `LoadedResourceRecord`，并恢复 AddRef/Release 与 unload 生命期。它与 SourceCache layer-list 是两条不同缓存链，不得再用 `std::list/shared_ptr` 概括 Player_findSource 的当前差异。
3. 2026-07-19 纠正：ObjSource 已恢复为 `PSBRawNode + texture*`，六个注册成员、clip、ensureTexture、drawLayer、adaptor 失败泄漏与析构顺序均已按上述地址闭合；旧 dict-facade/open 结论不得继续使用。
4. RM 继承面、unloadAll/isExistMotion/findMotion/findSource/random NCB 表面已恢复；各方法体继续按独立证据审计。
5. 2026-07-19 后续纠正：Win/spec=2、KRKR/spec=1 以及非 atlas ObjSource 均已直接消费 `LoadedResourceRecord::file` raw nodes；这些 named raw 导航站点与 ObjSource 生命周期已对齐，整页上传是单独的 Web API 边界，但该结论不再外推为完整 source 调用链关闭。
6. 2026-07-23 再纠正：`sub_695DE8@0x695DE8` 有 Player 与 `sub_6F1060@0x6F1060` 两个调用者；prepared item 在 `sub_6C2334@0x6C360C` 保存持久 `SourceState*`，`sub_6ADFBC@0x6AE154..0x6AE188` 在纹理 getter 后从该 alias 重读 rect。本地旧快照链被证伪，现恢复共享 helper、直接 alias、现场 rect、atlas `{x,y,right,bottom}`、right-left/bottom-top 尺寸、持久 `var_B0` + per-record `p` 两只 raw node、未初始化 size 槽及 pal/non-pal 分支内重复 lookup。旧“Win/KRKR 整链 CLOSED”只能说明 raw owner/map 迁移阶段，不再作为全局裁决。
7. 同轮继续区分两个 type-erased getter：`0x6D5C68` 用 `sub_6F67CC` 只读现有 texture，`0x6ADE24` 才用 `sub_6F1060`；`0x6F1060` 与 Private `0x6DE738` 的 fallback 均把调用后的 `SourceState.object` 直接交给 `0x6C1B70`。2026-07-23 后续已恢复 Player 常驻 descriptor/color Dictionaries 与继承 NCB `(source,descriptor)` 调用链；Web-only `Player.loadSource(name)` 是独立额外兼容 helper，不再被误称为 NCB alias 实现。
