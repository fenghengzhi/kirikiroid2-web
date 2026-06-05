# MotionPlayer P3 架构级 reframe 计划（2026-06-05）

> 来源：2026-06-05 推进 5 个登记 open 项后净剩的 2 个**架构级**偏差（commit 596b3c5）。
> 二者均非 additive，需回**阶段-2 重做数据流**（同输入→同中间变量→同计算顺序→同输出），
> 不可在现结构上打补丁（CLAUDE.md「禁止在架构不一致的基础上打补丁」）。
> 权威：libkrkr2.so 反编译；本文证据均为本 session 亲自反编译取得。

## 关键洞察：两项同源 —— 都是「ResourceManager 对象模型」未对齐

`sub_6A88CC`（EmoteObject +0 的 RM ctor，new 0xE8=232B）本 session 反编译确证 binary RM 单一对象内含：
- `sub_6A78F4` = SourceCache intrusive 双向链表（head/tail sentinel @+72/+80）—— ✅ 本地 `std::list<Entry>` 已同构
- `this+88 = new(8 * _M_next_bkt(0xA))` + `this+96` = **HashMap A（findSource 的 map）** —— ✅ **P3-A 已对齐(2026-06-05)**：fresh decompile 证实它是 **libstdc++ `std::unordered_map` + 自研 FNV functor**（非内联 KiriKiri map），本地 `_state->loadedModules` 已迁为 `unordered_map<ttstr,V,ttstr_hash,ttstr_equal>` 同选型 1:1
- `new Math.RandomGenerator()` @+144 RNG
- `+176` std::_Rb_tree（RB-tree）
- `+216 = 0x100000001` refcount/flags

而 binary RM 作为 **iTJSDispatch2\* dispatch 对象**进入 Player（`Player_ctor@0x6CED30` 单参 dispatch-in）。本地 RM 是 **native C++ 对象**（`ResourceManager` 持 `_state`），再 `CreateAdaptor` 反向包 dispatch。

→ **P3-A（容器）和 P3-B（ownership）是同一个「RM 不是 binary 对象模型的忠实复刻」问题的两个面**。建议作为一个协调 effort 分 2 阶段，A 先行（局部、可差分守护），B 后行（全局 ownership 反转，侵入最大）。

---

## P3-A：RM HashMap A —— ✅ **已完成(2026-06-05, commit 待提交)**

> **前提纠正**：本节原标题「STL → 内联 FNV bucket map」基于被证伪的前提。fresh decompile（sub_6A88CC ctor /
> sub_6EB9E4 findOrInsert / sub_6EB8F4 lookup）证实 binary HashMap A **就是 libstdc++ `std::unordered_map`**
> （`_M_next_bkt(10)` bucket-count helper + `_M_find_before_node` + wcscmp equal functor），**仅 hash 自研 FNV**——
> 与 06-05 review §四 已确证的「4 个 Player HM = libstdc++ unordered_map」同型。**没有「内联 bucket map」可迁移到**；
> 正确目标形态 = `unordered_map<ttstr,V,ttstr_hash,ttstr_equal>`，已达成。**#4 残留容器偏差完全 CLOSED（非部分）。**
>
> 落地：`_state->loadedModules` 从 `unordered_map<std::string(raw path),V>` 迁到
> `unordered_map<ttstr,tTJSVariant,ttstr_hash,ttstr_equal>`（+ `lastLoadedPath` std::string→ttstr）；所有 key 构造点
> （load/unload/findLoaded/clearCache/unloadAll）同步；大小写归一化交叉核实：binary FNV/equal 均**大小写敏感**
> （无 tolower），本地原本就 raw-path-keyed（lowercase 仅用于日志），迁移**无归一化语义丢失**。就地纠正 ResourceManager.h
> 一条被证伪注释（「binary is NOT libstdc++ unordered_map」）+ 删死字段 `_psbDictCache`。构建 web+wasmtime PASS，
> logo 差分 m2logo(93)/yuzulogo(243) 逐位 PASS（oracle-guarded，非 inert）。
>
> 原计划内容存档（已不适用，留作证据轨迹）：

### 证据
| 项 | 地址 | 事实 |
|---|------|------|
| findSource 读 bucket map | 0x6AAB3C | `sub_6EB8F4(this+88, hash % *(this+96), name, hash)` |
| FNV 内联 hash | 0x6aac4c-0x6aac68 | `h=0; do{t=h+ch; h=(1025*t)^((1025*t)>>6);}while(ch); h=9*h; h=32769*(h^(h>>11)); if(!h)h=-1;`，缓存于 `ttstr+68` |
| bucket 数组分配 | 0x6a891c(RM ctor) | `this+88 = new(8 * _M_next_bkt(0xA))`，`this+96`=bucketCount（libstdc++ unordered_map 初始化）|
| 填充路径 | ResourceManager_loadResource → sub_6EB9E4(findOrInsert) | 按**完整 PATH ttstr** 插入，`value+16`=解析后 PSB 模块 dict |
| 本地现状 | — | `_state->loadedModules` = `std::unordered_map<std::string, tTJSVariant>`（小写 path key）；`findLoaded`/`findSource` 读它 |
| 已有设施 | — | `internal/ttstr_hash.h` 已 byte-for-byte 复刻该 FNV；4 个 Player HashMap 已用 `unordered_map<ttstr,V,ttstr_hash,ttstr_equal>` 选型 |

### 目标形态
把 `loadedModules` 从 `unordered_map<std::string,...>` 改为与 binary HashMap A 同选型的容器：
- key = **ttstr**（完整 path），复用 `ttstr_hash`/`ttstr_equal`（与 4 个 Player HM 一致）
- hash 缓存语义对齐 `ttstr+68`（若 ttstr 复刻体有该 slot；否则记为元素内部数据契约边界）
- value = PSB 模块 dict（`value+16` 对应物）

### 步骤
1. fresh decompile `sub_6EB8F4`(bucket lookup) + `sub_6EB9E4`(findOrInsert) + RM ctor 0x6A88CC 的 bucket 段，确认 bucket node 布局（next/hash/key/value 偏移）与 rehash 策略。
2. 把 `_state->loadedModules` 的 key 从 `std::string`(小写 path) 迁到 ttstr + `ttstr_hash`（容器选型对齐；先不强制内联 bucket 数组布局——按「复刻源码结构而非编译产物」方法论，`unordered_map<ttstr,...,ttstr_hash,ttstr_equal>` 已是正确选型，除非证据要求内联 bucket node 的元素数据契约）。
3. 同步所有 `loadedModules` 读者（`findLoaded`/`findSource`/load 路径 lowercase 归一化点）改用 ttstr key；确认 LoadModule 不区分大小写（CLAUDE.md）的归一化在 ttstr 路径仍正确。
4. 构建 web debug + wasmtime guest；跑 m2logo+yuzulogo 差分。

### 验证 / 风险
- **有 oracle 守护**：load 路径在 logo 渲染中被走到（loadedModules 被填充/读取）→ 差分能捕获回归（与 P3-B 不同，A **不是** oracle-inert）。
- 风险：path 大小写归一化语义若从 `std::string` tolower 迁到 ttstr 路径漏改 → 加载 miss。须 trace 所有 key 构造点。
- 范围：局限于 ResourceManager + State，不触 Player ctor。**先做**。

---

## P3-B：RM ownership —— native value → dispatch-in（+ parentPlayer 链）

### 证据
| 项 | 地址 | 事实 |
|---|------|------|
| Player ctor 签名 | 0x6CED30 | `Player_ctor(this, iTJSDispatch2* rm_dispatch)` —— **单参，无 parentPlayer** |
| RM 进入方式 | 0x6CED30 内 | `sub_A0F5E0(this+636, rm_dispatch)` / `this+656` / `this+992` —— RM **dispatch 指针**拷进 3+ 个 tTJSVariant 槽 |
| Player 工厂 | 0x6f6dc0 | `Player_factory` |
| 本地现状 | PlayerCore.cpp:90 | `Player::Player(ResourceManager rm, Player* parentPlayer)`；`_resourceManagerNative(std::move(rm))` 后 `CreateAdaptor` 反包 dispatch（**方向相反**）|
| parentPlayer 消费者 | PlayerVariable.cpp:268 | `player->_parentPlayer` 上溯遍历 |

### 偏差本质（为何禁盲改）
1. **多 parentPlayer 参**：binary ctor 无此参。本地 `_parentPlayer` 链被 var-track 上溯遍历消费——删参会断链，须先确认 binary 用什么机制承担「parent 上溯」（可能是 RM dispatch 槽或别的字段），再迁移。
2. **RM native vs dispatch-in**：binary 把 RM 作为 iTJSDispatch2* 拷进 Player 的 tTJSVariant 槽（+636/+656/+992）；本地持 native `_resourceManagerNative` 再反包。改成 dispatch-in 会**反转整个 RM ownership/lifetime 模型**，级联所有 `_resourceManagerNative` 直接消费者。

### 步骤（高侵入，依赖 P3-A 先完成）
1. fresh decompile Player ctor 0x6CED30 全体 + 工厂 0x6f6dc0 + 这 3 个 RM 槽（+636/+656/+992）各自的消费者（xref），搞清 binary 里「Player 如何持有/调用 RM」的完整数据流。
2. 反编译确认 binary 的「parent 上溯」机制（grep 本地 `_parentPlayer` 全部消费者，逐个在 binary 找对应——确认是否真有独立 parentPlayer 字段，还是本地发明）。**这是强「本地发明 vs binary 缺失」断言，须独立交叉核实**（CLAUDE.md M7 教训）。
3. 若证实 RM 应 dispatch-in：把 `_resourceManagerNative` 模型整体迁为 tTJSVariant(dispatch) 槽，所有 native 直调改经 dispatch FuncCall；ctor 签名收敛到单参。
4. 构建 + 差分。

### 验证 / 风险
- **oracle-inert 风险高**：ctor 路径在 logo 启动时走到，但 RM ownership 模型差异未必在 logo 差分中可观察（logo 不重度用 RM dispatch 路径）。按 CLAUDE.md「oracle-inert 不是 defer 理由」仍应做，但验证主要靠反编译逐行对照 + 构建，差分仅非回归守护。
- 风险**最高**：反转 ownership 触及对象生命周期（维度④）+ 字段顺序（维度①根因）+ 所有 RM 消费者。须在 P3-A 稳定后单独 session 做，class-layout-auditor 全程守护。
- **依赖**：P3-A 先行（HashMap A 对齐后，RM 对象内部已更接近 binary，再处理 RM 如何被 Player 持有更安全）。

### ✅ 证据已备齐（2026-06-05 只读核实 → analysis/MotionPlayer_P3B_Evidence_2026-06-05.md）
**关键交叉核实结论（缩小 P3-B 范围）**：
- **`_parentPlayer` 不是 port 发明 = binary `Player+8`**（写 `Player_initNodeFields@0x6b43dc` stencilType==3 child `*(child+8)=parent`；读 `sub_6B1ABC@0x6b1bb8` label-miss 上溯）。**→ parent 链已忠实，P3-B 不再含「parentPlayer 链迁移」，只剩 RM ownership 一项。删 `_parentPlayer` 是错的。**
- ctor 单参确认；同一 RM dispatch 拷三槽（各 AddRef）：**+636**=findSource self（**dispatch-facade-over-native 混合**：解包回 native 直读 +224/+88/+96，即 P3-A 已对齐的 map）、**+656**=渲染期 RM、**+992**=规范 RM（NCB getter / findMotion / loadMotion / child 继承源）。
- **混合模型警告**：findSource 迁移时**勿全改纯 dispatch**（binary 自己就解包回 native 直读 map）。
- 14 处 `_resourceManagerNative` 消费者 + binary 证据 + 6 步迁移面见 dossier 块 3。
- **正交新缺口（与 RM 无关，可独立补）**：本地上溯每层只查 HM2，省略 binary 每层 `parent+280` 的 type3/type4 node-list 扫描（PlayerVariable.cpp:267 缺口）。
- **待决疑点**（动手前需补证据）：(1) layer-id RM method(requireLayerIdForName/releaseLayerId) binary 对应名未定位；(2) +656 渲染槽消费未逐行展开；(3) +636 解包 PropGet 属性名(dword_1AB8098) UTF-16 未解。

---

## 执行顺序与挂载

| 阶段 | 项 | 侵入度 | oracle | 前置 | 状态 |
|------|----|--------|--------|------|------|
| 1 | **P3-A** RM HashMap A 容器对齐 | 局部 | ✅ 有差分守护 | 无 | ✅ **完成(2026-06-05)**：选型 1:1（libstdc++ unordered_map + FNV functor）；前提「内联 bucket map」证伪 |
| 2 | **P3-B** RM ownership dispatch-in（~~+ parent 链~~：parent 链已证忠实，移出范围）| 全局 | ⚠️ 多为 inert | P3-A 完成 | ✅ **第一轮完成(2026-06-05)**：ctor 单参 dispatch-in + RM 持有 native value→dispatch variant + nativeRM() 解包 + child 继承 parent dispatch + parent 设置移出 ctor。class-layout-auditor 裁决 0 真实 bug；web debug 240/240 + wasmtime guest 276/276 + logo 差分 m2logo(93)/yuzulogo(243) 逐位 PASS。**defer**：findMotion/loadMotion +992 FuncCall 化、+656 bufLayer 渲染分支、layer-id 容器/无 name 签名（见 dossier 块4）|

两项完成后，binary RM 对象模型（dispatch facade + 内联 FNV bucket map + SourceCache list + RNG + RB-tree）即与本地 1:1，配合本轮已对齐的 `ResourceManager::findSource` 函数体，关闭维度 ④（对象生命周期）与 ⑤（容器实现）在 RM 上的最后两处系统性偏差。
