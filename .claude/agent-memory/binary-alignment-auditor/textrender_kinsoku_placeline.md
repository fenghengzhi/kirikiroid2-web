---
name: textrender-kinsoku-placeline
description: textrender 落字层 appendChar/kinsoku/finishLine/done 二进制地址↔本地映射; 2026-06-12 六审(独立全量重反编译11函数)确认零开放偏差✅
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
