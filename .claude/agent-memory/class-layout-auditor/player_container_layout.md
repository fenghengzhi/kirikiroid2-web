---
name: player-container-layout
description: motion::Player (libkrkr2.so, 1384B) 内部容器选型权威表 — 4个哈希表/2个deque/2个动态数组的偏移与stride,纠正旧文档把+264/+320/+1184/+1240 误标为 hashMap-vs-vector 的歧义
metadata:
  type: project
---

# motion::Player 内部容器选型 (libkrkr2.so, ctor 0x6CED30 / dtor 0x6CFADC 验证)

对象 0x568=1384 字节。容器全部内联在对象里,按固定偏移扁平排布。

## 4 个哈希表 = 标准 libstdc++ std::unordered_map (仅 hash 自研)
> 纠正(2026-06-05 fresh ctor 0x6CED30 复核): 措辞"KiriKiri 哈希表"会误导成自研内联/开放寻址表。
> 实为 libstdc++ `std::unordered_map`: ctor 每个 HM 调 `std_Prime_rehash_policy_M_next_bkt(base,0xA)`
> + load factor dword=1065353216(1.0f) + `operator new(8*nbkt)` bucket 数组 + `_M_before_begin` 单链。
> 只有 **hash 函数**自研(ttstr UTF-16 hash)。本地 `unordered_map<ttstr,V,ttstr_hash,ttstr_equal>` 已是
> 正确 1:1 选型, **不存在** "STL→KiriKiri 内联 HM" 的 P3 重构(此前提本身错)。详见 [[motionplayer-container-audit]]。

每个哈希表布局:
- `+0`: float 负载因子 (ctor 写 1065353216 = 1.0f)
- bucket 数组 begin 指针 + bucket 计数 (sub_149EDF8 选 prime 桶数,初始 hint=10)
- chain list head: 单链节点 `[next_ptr, value_ptr]`,dtor 走 `*v6` 链表逐个 operator delete
- 内联 end-bucket 哨兵 (dtor 里 `(base+sentinel)!=ptr` 判断是否堆分配)

| 哈希表 | bucket数组ptr | bucket计数 | chain head | float负载因子 | 内联哨兵 | 节点释放 |
|---|---|---|---|---|---|---|
| HM1 | +264 | +272 | +280 | +296 | +312 | sub_6DD1A0(v+1) |
| HM2 | +320 | +328 | +336 | +352 | +368 | tTJSVariant_Release(v[1]) |
| HM3 | +1184 | +1192 | +1200 | +1216 | +1232 | sub_6DD018 链清理 |
| HM4 | +1240 | +1248 | +1256 | +1272 | +1288 | tTJSVariant_Release(v[1]) |

旧文档 (Player_Class_Layout_libkrkr2so.md) 把这4处标为 hashMap1..4 是对的;
project_player_class_layout.md 同样标 hashMap。EmotePlayer 误配报告把它们称作
"controller deque @+256/+336/+416..." 是 **EmotePlayer 内部 Player(1496B 变体)** 的偏移,
与 Motion.Player(1384B) 不是同一套偏移,不要混用。

## 2 个 deque (KiriKiri std::deque<MotionNode>, node=2632B)
- `+184` 节点 deque (sub_6F4E90 init / sub_6F436C destroy),root node 在此;`*(player+200)` = 当前块指针,所有 root 属性 getter 经此访问
- `+1296` 变量/控制器 deque (sub_6F4FD8 init / sub_6CF678 destroy, sub_6C0DE8 清理),item=160B

## 2 个动态数组 / list
- `+384`/+392 renderList: **stride 56B (7 qword)**,entry+0 = tTJSVariant*,dtor `v4+=7` 遍历释放
- `+936`/+944 variableList: **stride 44B**,entry 含 ttstr@+4 和 ttstr@+24
- `+408` someList (sub_6DD144 清理)
- `+24`/+40 节点 label map/tree (sub_6DD228),带哨兵

## 其它内联结构
- `+48`/+56 list 哨兵 (ctor `a1[6]=a1[7]=a1+4`)
- `+432`/+440 list 哨兵 (ctor `a1[54]=a1[55]=a1+52`)
- `+864` 容器 (sub_7E2344 init / sub_7E24AC destroy, 44B 区)

**How to apply:** PlayerRuntime 已删,容器内联进 Player 本体。4 HM 容器选型(unordered_map)+deque+map+
vector 全部对齐。**无需**替换成"KiriKiri 内部哈希表"(那是错的前提)。仅历史 2 处 std::string→ttstr
key retype 待办 —— 此二者(_evalResultValues HM2 / _nodeLabelMap +24)均已于 2026-06-03 完成 retype
(2026-06-05 复核: Player.h:1446 _evalResultValues=detail::LabelValueMap ttstr-key;
Player.h:1292 _nodeLabelMap=detail::NodeLabelMap ttstr-key+ttstr_utf16_less comparator)。
⇒ **容器维度无 open 偏差**。参见 [[motionplayer-container-audit]] [[player-local-vs-binary-audit]]。
