# MotionPlayer P3 架构级 reframe 计划（2026-06-05）

> 来源：2026-06-05 推进 5 个登记 open 项后净剩的 2 个**架构级**偏差（commit 596b3c5）。
> 二者均非 additive，需回**阶段-2 重做数据流**（同输入→同中间变量→同计算顺序→同输出），
> 不可在现结构上打补丁（CLAUDE.md「禁止在架构不一致的基础上打补丁」）。
> 权威：libkrkr2.so 反编译；本文证据均为本 session 亲自反编译取得。

## 关键洞察：两项同源 —— 都是「ResourceManager 对象模型」未对齐

`sub_6A88CC`（EmoteObject +0 的 RM ctor，new 0xE8=232B）本 session 反编译确证 binary RM 单一对象内含：
- `sub_6A78F4` = SourceCache intrusive 双向链表（head/tail sentinel @+72/+80）—— ✅ 本地 `std::list<Entry>` 已同构
- `this+88 = new(8 * _M_next_bkt(0xA))` + `this+96` = **HashMap A（findSource 的 FNV bucket map）** —— ❌ 本地是 `std::unordered_map<std::string,...> _state->loadedModules`
- `new Math.RandomGenerator()` @+144 RNG
- `+176` std::_Rb_tree（RB-tree）
- `+216 = 0x100000001` refcount/flags

而 binary RM 作为 **iTJSDispatch2\* dispatch 对象**进入 Player（`Player_ctor@0x6CED30` 单参 dispatch-in）。本地 RM 是 **native C++ 对象**（`ResourceManager` 持 `_state`），再 `CreateAdaptor` 反向包 dispatch。

→ **P3-A（容器）和 P3-B（ownership）是同一个「RM 不是 binary 对象模型的忠实复刻」问题的两个面**。建议作为一个协调 effort 分 2 阶段，A 先行（局部、可差分守护），B 后行（全局 ownership 反转，侵入最大）。

---

## P3-A：RM HashMap A —— STL `unordered_map` → 内联 FNV bucket map

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

---

## 执行顺序与挂载

| 阶段 | 项 | 侵入度 | oracle | 前置 | 建议 session |
|------|----|--------|--------|------|------------|
| 1 | **P3-A** RM HashMap A 容器对齐 | 局部 | ✅ 有差分守护 | 无 | 可立即排期（module-alignment-driver 或 binary-aligned-implementer，限定 ResourceManager+State 范围）|
| 2 | **P3-B** RM ownership dispatch-in + parent 链 | 全局 | ⚠️ 多为 inert | P3-A 完成 | 独立 session，class-layout-auditor 全程守护，先做证据交叉核实再动 |

两项完成后，binary RM 对象模型（dispatch facade + 内联 FNV bucket map + SourceCache list + RNG + RB-tree）即与本地 1:1，配合本轮已对齐的 `ResourceManager::findSource` 函数体，关闭维度 ④（对象生命周期）与 ⑤（容器实现）在 RM 上的最后两处系统性偏差。
