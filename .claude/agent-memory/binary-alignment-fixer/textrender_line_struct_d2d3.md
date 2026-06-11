---
name: textrender-line-struct-d2d3
description: textrender 批1-4 偏差全清 (2026-06-11)——D2 嵌套 Line 112B/D3 clear 双零/D1/Q1-Q3/R1-R3/N1/D4/抛错组均 PASS；Line 同型证据三件套 + sub_A0E48C 抛错模式
metadata:
  type: project
---

textrender D2+D3 已修复（2026-06-11，一轮审计 ✅ PASS，构建通过，无运行时验证手段——textrender 无差分/fixture 覆盖）。

**D2**：pending 行缓冲（对象+320）与 lineList 元素同型 112B `Line { deque<charItem> chars(+0..79); float lineBottom/lineHeight/bbL/bbT/bbR/bbB(+80..103); int wordBreakRun(+104); bool prevWasSpace(+108) }`。本地已删 LineItem+_pendingChars+8 个摊平 metric 字段，改 `Line _pendingLine` + `vector<Line> _lineList`，`Line::clear()` 复刻 0x5A1E68（deque clear + 零化相对 +80..+108）。
同型三重证据：① 0x5A1E68 取 +320 指针零化相对 +80..+108（STR/STP/STUR 三条，+101 处 8B STUR 覆盖 +104/+108）；② finishLine push（0x5a3758/0x5a3768）与扩容 sub_5A43E8 拷 a2+80/a2+93 两 OWORD = +80..+108 全范围（含 wordBreakRun/prevWasSpace）→ 源码层 = `lineList.push_back(pendingLine)`；③ 0x5A1B24 dtor 共用（函数体只触 deque 控制块，Line 尾部 POD，~Line≡~deque；kinsoku 80B 裸临时 deque 也复用它，不与同型冲突）。
关键顺序契约：finishLine push 后 Line::clear 再读 +408（`if(bboxLeft>penX)` 左操作数恒 0）；clear@0x59EC6C Line::clear 在前、竖排分支随后写 +416/+408=renderSizeW（横排不写保持 0）。

**D3**：clear@0x59EC6C `STR XZR,[#0x118]` 8B=+280 renderPos+284 renderPosSnap、`STUR XZR,[#0xBC]` 8B=+188 renderDelayAccum+192 charDelayStep——本地 clearImpl 已补 `_renderPosSnap=0`/`_charDelayStep=0`，旧注释"clear 不重置 +192"已证伪改正（代码 + analysis §3b/§7.6 同步）。

**D1/Q1/R1/R2 已修复（2026-06-11，一轮审计 ✅ PASS，构建通过，仍无运行时验证手段）**：
- D1 kinsoku used<1 边界补 `--_renderCount`（0x5a50a0/0x5a50a8 两路与 LABEL_94 tail-merge 共享尾 0x5a52cc/0x5a52d8；仅 --(+84) 不动 +108；leading 未命中 0x5a5078 直接 LABEL_107 不减）。
- Q1 CharItem+40 真名 graph（getCharacters@0x5a081c dict **首字段** L"graph" 先于 text，串 @0x14CA19A；sub_5A2160 byte→Integer）——rubyFlag 已全局改名 + dict 最前插入 + analysis §9.1/§3b-1 同步。
- R1 render `$` 展开全成功（++v64>=v50 @0x5a295c → LABEL_319）补 v17=false；空结果 LABEL_320 不动 v17。
- R2 renderBalancedChar 配对前置门 `_begin.GetLen()==_end.GetLen()`（0x5a2a5c/0x5a2a68 两处 BL _ZdlPvm_22 = IDA 误标的 tTJSVariantString 长度 getter（读 +60），CMP @0x5a2a6c，B.NE → LABEL_319）。
**模式**：`_ZdlPvm_22`（"operator delete"）在 textrender 全文 = ttstr length getter，凡见返回值参与 CMP/循环上界即按长度处理。

**第四批（2026-06-11，一轮审计 ✅ 7/7 PASS，构建通过）边界抛错/结构细节组全部关闭**：
- R3 evalDollarTag@0x5A4148：onEval 返回 object/octet/int/real → TJSThrowVariantConvertError(String)（octet/int/real 走 `(u)(ty-3)<3`，object 单判同抛）；string 取值/void 空串；FuncCall 返回码不检查。
- N1 ncb_render@0x59FC28：a2>=4 时 `(void)param[3]->AsReal()` 强制后丢弃——本地 tTJSVariant::AsReal（tjsVariant.h:937）逐 case 与二进制 switch 同构（object/octet 抛 Real、string 解析、void 0.0 不抛），可直接用作 sub_A0E48C(,5u) 强制组的等价物。
- Q2/Q3+vDict：getKeyWait add-arg/result 与 onStyleChanged onFontChange arg 三处 variant ObjThis 槽均 null（(obj,nullptr)）；**getCharacters result 是 (obj,obj)——二进制两函数自身不一致，各自照抄，勿"统一"**。
- D4 countdown：`v21=(+88)-1; if(v21>=0){回写;return}` 先算后条件回写，负值不回写（+88 永不为负，非无条件 `--`）。
- 抛错组：paramAsDict type!=object → throw(Object)；setOptionStr 四串键 + setDefault/setFont face 键 object/octet/int/real → throw(String)（string 取值/void 空串）。
**模式**：textrender dict/参数层的 sub_A0E48C(v, N) = TJSThrowVariantConvertError(v, (tTJSVariantType)N)（1=Object,2=String,4=Integer,5=Real）；real/int 强制组直接等价 AsReal/AsInteger，但 string 强制组**不是** ttstr(v)——非 string/void 必须显式 throw。
textrender 三簇（dict 解析层/render 状态机+查询层/落字层）已知偏差全部清零；仍 open 仅注册/生命周期审计簇（auditor 称 ctor@0x5A111C 默认值群，需与 C1 批已修内容核对是否 stale）。

**模式沉淀**：摊平字段 vs 嵌套结构体的判别证据 = ①专用 clear/dtor helper 接收子对象指针并按相对偏移零化、②push/拷贝按整段 OWORD 范围搬运（含"看似无关"的尾字段）、③同一 dtor 被两个容器位置共用。命中任一即应怀疑源码是嵌套 struct 而非对象级摊平字段。
