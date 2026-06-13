---
name: textrender-kinsoku-placeline
description: textrender 落字层 appendChar/kinsoku/finishLine/done 二进制地址↔本地映射; 2026-06-13 十审(11项inert微差修复逐项独立重反编译验证)全✅零新偏差
metadata:
  type: project
---

# textrender 落字层对齐 (2026-06-12 六审独立复核, 全部 11 函数重新 decompile, 结论=完全对齐)

六审独立验证补充证据: word_14CA1EE get_bytes=0x3000 UTF-16LE 确证; introsort@0x5A59E8+insertion@0x5A5C34 比较键均为 *(elem+24) float 升序(=std::sort by renderPos, 非稳定)双双 decompile 确证; charItem_copy@0x5A4838(text incref+3 OWORD+ruby 20B 深拷逐项 incref)/destroy@0x5A5760(ruby release→delete buf→text release)=本地隐式 copy/dtor 1:1; Line::clear@0x5A1E68 保留首 node+删多余 node=std::deque::clear 语义; Line dtor@0x5A1B24=deque dtor。
六审本地行号(TextRender.cpp 2439 行版): clearImpl 465-521 / onGetTextWidth 603-623 / appendChar 629-701 / kinsoku 708-814 / placeChar 818-871 / updateWordBreakState 875-888 / advanceLineVertical 891-897 / finishLine 903-989 / newlineImpl 996-999 / doneImpl 1022-1080 / Line::clear 153-163。
残留 7 项全为已自承注释的 inert 微差(非偏差): measure objthis null guard+TJS_FAILED 早退(结果等价 void→0)/FuncCall hint nullptr vs byte_1AB51A0 缓存/appendChar 单 ttstr 复用 vs 二进制双构造(v65+v48)/done keyWait 本地 bounds guard(二进制无检查=UB)/_hasCurRubyText 平行哨兵/!word_break 循环 run 缓存 vs 每轮重读+424(循环无写)/drain 下标 vs 迭代器。

# (五审历史记录 2026-06-11)

地址映射 (libkrkr2.so ↔ cpp/plugins/textrender/TextRender.cpp):
- appendChar @0x5A3880 ↔ appendChar(560-626)
- measureTextWidth @0x5A426C ↔ onGetTextWidth(534-554) ✅(throw 已修)
- appendChar_kinsoku @0x5A4A7C ↔ kinsoku(633-733); placeChar=LABEL_10 ↔ 737-790
- finishLine @0x5A34B8 ↔ finishLine(822-905); newline @0x59FECC ↔ 926; done @0x59FEE4 ↔ 952
- charItem_copy @0x5A4838 / charItem_destroy @0x5A5760 / rubyVec_defaultAppend @0x5A5374
- pendingDeque_init @0x5A15B0 / pushNode @0x5A57CC / deque_pushFrontNode @0x5A5548
- **pendingLine_clear @0x5A1E68**(原误名 pendingDeque_clear, 已 rename+comment)
- pendingLine_dtor @0x5A1B24 / lineItem_copyFromPending @0x5A4588 / deque_copyElems @0x5A46C0
- lineList growPushBack @0x5A43E8 / introsort @0x5A59E8 + insertion @0x5A5C34 (=std::sort, 键 char+24 renderPos float, 非稳定)
- wcscmp_utf16 @0x9B1ED0; ttstr_indexOf @0xA0CBEC(任一 Ptr null→-1, 空集→-1 ✅ 同本地 tTJSString::IndexOf)
- 全角空格 word_14CA1EE = 0x3000 ✅

## 关键架构发现: 嵌套 Line 结构体 (2026-06-11 确证)
二进制源码层 pending 行缓冲(+320, 112B)与 lineList 元素**同型**:
`Line { deque<charItem> chars(80B); float lineBottom,lineHeight,bbL,bbT,bbR,bbB(+80..+103); int wordBreakRun(+104); bool prevWasSpace(+108) }`
证据: ①sub_5A1E68(Line::clear) 取 +320 指针却零化相对 +80..+108(disasm STR XZR[#0x50]+STP XZR[#0x58]+STUR XZR[#0x65] = 对象 +400..+428)
②finishLine push 与 sub_5A43E8 都把 a2+80/a2+93 两 OWORD(=+400..+428 含 wordBreakRun/prevWasSpace)拷入 lineItem+80/+93
③sub_5A1B24 同为 pending Line 与 lineList 元素的 dtor。
~~本地把累加器摊平为 TextRenderBase 字段~~ → 2026-06-11 三审: 本地已实装 `struct Line{deque<CharItem>;6 float;int wordBreakRun;bool prevWasSpace}` + `Line _pendingLine`/`vector<Line> _lineList`, 摊平字段/LineItem/clearPendingDeque 全删(双 grep 复核无残留)。

## 2026-06-11 四审后开放偏差(D1/D2/D3 已修复)
1. **D1 ✅已修复(四审验证, 2026-06-11)**: kinsoku used<1 边界(back in leading 下移一个)二进制**有** `--renderCount(+84)`@0x5a52d8
   (0x5a50a0→LABEL_102 / 0x5a50a8→LABEL_101 两路皆汇入含 destroy+--84 的共享 tail)。本地 kinsoku ~739-744 已补
   `--_renderCount` 后 break; 复核三点全合: (a)该 tail 只 --(+84) 不动 +108(+108 减仅在 used>=1 路 0x5a4f84);
   (b)leading 未命中 0x5a5078 indexOf==-1 → 直接 LABEL_107 无 pop 无减(本地 break 前 if 包住 pop); (c)break 后
   finishLine+drain tmp 自递归+placeChar(!vertical) ≡ LABEL_107 序列。
2. **D2 ✅已修复(三审验证)**: `Line::clear()`(本地151-161) 1:1 复刻 sub_5A1E68(chars.clear()=deque::clear 语义 + 9 字段全零化;
   二进制 STR[#0x50]+STP[#0x58]+STUR[#0x65] 覆盖相对 +80..+108 全部);3 调用点(clearImpl 466/over-fail 904/push 后 961)全到位;
   finishLine push 改 `_lineList.push_back(_pendingLine)`(≡sub_5A4588+两 OWORD +80/+93 全范围拷, grow 路 sub_5A43E8 同);
   push 后 clear 再读 +408(行 967 左操作数恒 0)顺序对齐 0x5a378c→0x5a37c8。
3. **D3 ✅已修复(三审验证)**: clearImpl 499-500 `_renderPos=0;_renderPosSnap=0`(=STR XZR 8B@+280)、
   502-503 `_renderDelayAccum=0;_charDelayStep=0`(=STUR XZR 8B@+188); 字段注释已改正。
4. **D4 ✅已修复(2026-06-11 五审验证)**: appendChar 倒计数本地已改 `int v21=_charBufCountdown-1; if(v21>=0){回写;return true;}`(TextRender.cpp 634-640) 1:1 复刻 0x5a3970..0x5a3984(SUBS+B.LT, 负值不回写)。落字层零开放偏差。

## D2 重构三审逐项(2026-06-11, 全✅)
- clearImpl 竖排分支写序 +232/+248/+256/+416/+408 与 0x59eca8..0x59ecb8 一致; 横排不写 bboxL/R(保持 clear 后 0)✅
- finishLine metric 写序 +404/+400/+408/+416/+236/条件+420 = 0x5a3724..0x5a373c 逐条一致(含 v31/v32/v34/v33 先读后写)✅
- kinsoku 全部 +424/+428 读写改走 _pendingLine.wordBreakRun/.prevWasSpace; placeChar +416→.bboxRight;
  advanceLineVertical +420→.bboxBottom; updateWordBreakState run=chars.size()(push 后)✅
- 消费端 done(bbox float[22..25]/valign char.y/铺 charList)/calcLineOffset(+80 lineBottom)/maxScrollLine(+84 lineHeight)/
  renderImpl 入口 _lineList.clear() 字段名未变仅类型改 Line, 全部仍正确✅
- !wordBreak trailing-run 循环本地缓存 run vs 二进制每轮重读 +424(循环内无写)= inert 等价✅

## 已核实对齐(2026-06-09 两旧偏差均已修复)
- following used<max: ++used 后查 back∈following → finishLine, 否则直接落字不换行 ✅(本地 686-696 已修)
- onGetTextWidth Object/Octet 经 AsReal throw ✅(已修); String→AsReal/Int→(double)/Real→raw/void及FuncCall失败→0.0 ✅
- over 门控/!word_break trailing-run/leading 双 pop(size>=3 查倒数第2∉leading 且 back∈leading; LABEL_94 size>=2 back∈leading)/
  LABEL_107 drain tmp 自递归/placeChar(renderPos 恒=+280, delayAccum=max)/updateWordBreakState/advanceLineVertical 全 ✅
- finishLine: 行高 max/over(+60=1,!+54→clear+false)/align(1→right,0→center*0.5,其它0)/缩进唯一门控=pending 非空/
  v16=(v14+first.x)/v15(0→fontsize)/落字 concat/metric 写序/push/\n/penX=+196/+236+=+136/竖排直跳 LABEL_56 ✅
- done: bbox float[22..25]/valign(1→H-bottom,0→center,其它0; ±v12 恒执行)/charList 铺行内 deque 元素指针/
  keyWait v39[1]=charList[v39[0]]+24 bits(本地多边界守护,自承)/std::sort ✅
- 容器选型: deque<CharItem>/vector<Line>/vector<charItem*>/vector<RubyItem>(20B)/vector<KeyWaitItem>(8B) 全一致 ✅
- charItem copy(text incref+OWORD+ruby 深拷)/destroy(ruby release→delete→text release) 生命周期 ✅
- inert 微差: text 空判定(二进制裸 Ptr null vs 本地 c_str() 恒非null, concat""=no-op)/appendChar 双 ttstr 构造 vs 复用/
  measureTextWidth 多 TJS_FAILED 早退/_hasCurRubyText 平行哨兵(恒 false, +528 无 producer 死支)

# (2026-06-13 七审独立复核：落字/禁则链零开放偏差✅ 再确认)
独立重反编译 appendChar@0x5A3880/kinsoku@0x5A4A7C/finishLine@0x5A34B8/measure@0x5A426C/ctor@0x5A111C，全链 1:1 复核成立（over 门控/三分支禁则/used<1 边界 --renderCount/LABEL_107 drain 自递归/placeChar pen 推进/word_break 状态/finishLine align+缩进+metric 写序）。新增证据：四禁则集 get_bytes 逐字✓——following 68cp@0x14C9DF8 / leading 19cp@0x14C9E82（首码点 U+005C）/ begin 10cp@0x14C9EAA / end 10cp@0x14C9EC0，与本地 ctor 字面量（hexdump 验证 U+3000=e3 80 80）逐码点一致；ctor@0x5A111C 确证 +8/+16/+24/+32 灌入这 4 地址。本地文件已变 2140 行（d83b1191 重构），六审行号作废：ctor 281-311/clear 429-484/newline 528/done 535-591/calc* 594-616/getKeyWait 862-884/getCharacters 889-952/helpers 953-987/onGetTextWidth 1042/appendChar 1059-1130/kinsoku 1133-1232/placeChar 1235-1286/finishLine 1316-1396。finishLine LABEL_56 内 +108=0 与 +528 release 顺序本地互换=inert（独立字段）。查询层 2 项新偏差归属 render_statemachine 记忆，非本层。

# (2026-06-13 十审：用户裁定 11 项 inert 微差全部按偏差修正，逐项独立重反编译验证全✅)
本层相关修复点验证：①finishLine LABEL_56 尾序已修为二进制序（+108=0 store @0x5a37f4 在 Release call @0x5a37fc 之前，+528 的 LDR @0x5a37f0 仅是调度提前，opaque call 不可越过前置 store）→ 本地 `_kinsokuUsed=0; releaseCurRubyText(); _accumBuf.clear()` 1:1。②kinsoku !wordBreak 追い出し循环已删局部 run 缓存：入口 v33 读一次 @0x5a4dec 做 >=1 门控，循环条件每迭代重读 +424 @0x5a4e14（destroy 为 opaque call 强制重读 → 源码 token = 循环条件直读成员）→ 本地 `while((int)chars.size() > _pendingLine.wordBreakRun)` 1:1。③appendChar 双 ttstr（v65 @0x5a39a8 度量 / v48 @0x5a39d8 charItem）+ `_fontScale*_curFontSize` 两处重算（@0x5a39c4 / +184 reload @0x5a39e8 → @0x5a39f0）已复刻。④_hasCurRubyText 哨兵已删，ruby 门 = `!_curRubyText.IsEmpty()` ≡ `*(+528)!=0`（ttstr 空串 Ptr==null）；ctor @0x5A111C 字段集复核无多余成员（对象 0x250=592B）。⑤done keyWait 回填守护已删（@0x5a01d4/@0x5a0200 无界，二进制循环为编译器 2 路展开，本地朴素循环=源码形）；std::sort 注释成立（callsite depth=2*(63-clz) @0x5a0240；sub_5A5C34 129B/16 元素阈值+unguarded；sub_5A59E8 median-of-3 introsort；比较键 *(elem+24) float 升序，本会话双双重反编译确证）。⑥calcLineOffset 越界判定 `(unsigned __int64)a2`（int 符扩）= 源码 `size() <= (size_t)lineIdx` ✅。
~~残留已知 inert（下轮可选）：ruby PropSet 折叠 / kinsoku drain 下标 / renderImpl c_str-length 次序 / appendChar text P3~~ **十一审(2026-06-13)全部消除已对齐**：①appendChar P3 已改 `CharItem v{ ttstr((tjs_char)ch) };` prvalue 原位构造 +0（无具名局部，零额外 AddRef/Release，disasm v48=createFromWide @0x5a39d8 直存字段、kinsoku 实参 &v48 @0x5a3bbc、尾释 @0x5a3bfc 全对位）；②kinsoku drain 已改 `for(CharItem &item:tmp)`(tmp=std::deque<CharItem>) = 二进制 @0x5a52e8 deque 迭代器遍历(元素步进 80B @0x5a5318 + 节点 hop `LDR X22,[X24,#8]!;ADD X26,X22,#0x1E0` @0x5a52f8 = operator++)，自递归 kinsoku @0x5a5310 / fail return false @0x5a5314 对位；③ruby PropSet 拆分 + ④renderImpl 次序见 [[textrender-render-statemachine]] §十一审。全 4 项 analysis §11.1 记录。~~getKeyWait count 提升~~ 已于十审修复。

# (2026-06-13 十审独立复核：第二独立审计会话全量重反编译 finishLine/kinsoku/appendChar/calcLineOffset/done/measure 等 17 函数，11/11 PASS 确认零新偏差；kinsoku `>=1` 门=二进制真实控制流 @0x5a4df4 非多余门；详录 analysis §11)

# (2026-06-13 十二审后独立评估审：render+落字+容器全量重反编译。逻辑/分支/容器拓扑零偏差，但新发现 6 项此前各轮未曾登记的 token 级微差，全部 OPEN)
1. **finishLine renderText 拼接 token**：二进制三处 concat 全是 `operator+=`（Independ 双分支 @0x5a36b4/0x5a3638/0x5a3798 + sub_A13ABC=TJSAppendVariantString(旧Ptr, wchar*) 返回新指针回存）；本地三处全写 `_renderText = _renderText + ttstr(...)`（operator+ 新建串+临时 ttstr）。缩进空格 @0x5a365c / 逐字 @0x5a36f0（实参=c_str(text) 裸 wchar*）/ "\n" @0x5a37c0。
2. **kinsoku tmp 作用域序**：二进制 tmp deque dtor @0x5a5338 在 LABEL_10 placeChar **之前**（fast-path 直接跳 LABEL_10 不经 dtor → 源码内层作用域先闭合再 placeChar，C++ 语义证明非编译器重排）；本地 `return placeChar(...)` 在 tmp 作用域内 → 析构在 placeChar 之后。
3. **updateWordBreakState 空格判定**：二进制 wcscmp_utf16(c_str(text), L" ")==0 @0x5a4ba8 零分配；本地 `c.text == ttstr(TJS_W(" "))` 每次堆分配临时。
4. **appendChar ruby 槽形态**：二进制 = vector resize（sub_5A5374 错误串 "vector::_M_default_append" 实证）+ back() 就地赋值（x@v28-3/y@-2/text=操作符赋值/span@-1）；本地 = 具名 RubyItem 局部 + push_back 拷贝（多一对 AddRef/Release，生长走 _M_realloc_insert 非 default_append）。
5/6 见 [[textrender-render-statemachine]]（renderBalancedChar 裸扫描、resolveFaceIndex by-value）。
判定：全部 inert（无可观察行为差），但按本仓「inert 微差也按偏差消除」先例（十~十二审）应列 OPEN 修复清单。

# (2026-06-13 十三审：上条 6 项 token 级微差中的 5 项 render 链修复已落地并逐项独立重反编译核实 = 全 PASS，CLOSED)
本轮独立反编译 finishLine@0x5A34B8 / appendChar_kinsoku@0x5A4A7C / appendChar@0x5A3880 / rubyVec_defaultAppend@0x5A5374 / render@0x5A228C + disasm @0x5a3a78/@0x5a4da0 交叉确认，5 项修复全部与二进制一致：
1. **finishLine renderText 三处拼接已改 `operator+=`（in-place）** ✅：全角空格 @0x5a365c(word_14CA1EE)/char.text @0x5a36f0(c_str)/换行 @0x5a37c0(L"\n") 每处前置 atomic_load→sub_A0BC58(Independ)/ttstr_c_str + sub_A13ABC 写回 *(a1+40)。本地 `_renderText += (tjs_char)0x3000 / += ci.text / += TJS_W("\n")`。
2. **kinsoku tmp deque 作用域已闭合于 placeChar 之前** ✅：二进制 dtor 双出口 LABEL_113 @0x5a5328(fail return 0)/正常 @0x5a5338 均在 LABEL_10(place)前。本地 `{...}` 块包重排，块尾 dtor 后 `return placeChar(c,!_vertical)`；fail/drain return false 在块内=RAII LABEL_113。placeWithoutFinish 分支三向已 disasm 核实(@0x5a4db4 pending空→LABEL_107 / @0x5a4de4 back∈following→LABEL_107 / @0x5a4de8 back∉following→`B loc_5A5338` 跳 finishLine 直接落字)。
3. **updateWordBreakState 空格判定已改裸 wcscmp** ✅：@0x5a4ba8 wcscmp_utf16(ttstr_c_str,L" ")==0；本地 TJS_strcmp(c.text.c_str(),TJS_W(" "))==0（tjsConfig.cpp:551 逐 tjs_char 比较）。无临时 ttstr。
4. **appendChar ruby 槽已改 resize+back 就地赋值** ✅：rubyVec_defaultAppend@0x5A5374=std::vector<RubyItem(20B)>::_M_default_append；写序 x@v28-3→y@-2→text@-20→span@-1 @0x5a3b34..0x5a3b3c；本地 resize(size+1)→back()→x/y/text/span。`==-20` 快路径=STL resize 内联展开非源码 token，resize() 覆盖。
5. **renderBalancedChar 三处已改裸 c_str 指针扫描+-1 哨兵**（scanCharIndex helper）✅：begin@0x5a2640/end@0x5a29d8/begin 二扫@0x5a30d8 全 `p=c_str-1;do{ch=p[1];++p;}while(ch!=target&&ch)`；长度门 _begin.GetLen()==_end.GetLen()@0x5a2a5c(IDA 误标 operator delete=GetLen)；--v133 仅 end 命中时。详见 [[textrender-render-statemachine]]。
回归核实：P1 颜色 4 属性 TR_RW(tjs_int)+SXTW@0x5a91ec / P2 objthis 裸指针回指+dict 解析层 / P3 resolveFaceIndex@0x5A14DC 按值 ttstr 全未碰坏。整体=完全对齐。
