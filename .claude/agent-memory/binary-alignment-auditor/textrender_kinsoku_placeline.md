---
name: textrender-kinsoku-placeline
description: textrender 落字层 appendChar/kinsoku/placeChar/onGetTextWidth 二进制地址↔本地映射 + 已确认偏差(following used<max 分支)
metadata:
  type: project
---

# textrender 落字层对齐 (2026-06-09 审计 TextRender.cpp 460-748)

地址映射 (libkrkr2.so, 本地 cpp/plugins/textrender/TextRender.cpp):
- TextRenderBase_appendChar @0x5A3880 ↔ appendChar (501-567) ✅
- TextRenderBase_appendChar_kinsoku @0x5A4A7C ↔ kinsoku (574-665) ⚠ 一处偏差
- placeChar = kinsoku LABEL_10 @0x5A4B14 ↔ placeChar (669-722) ✅
- updateWordBreakState = LABEL_16 @0x5A4B8C ↔ (726-739) ✅
- advanceLineVertical = LABEL_26 @0x5A4C8C ↔ (742-748) ✅
- TextRenderBase_measureTextWidth(onGetTextWidth) @0x5A426C ↔ (472-495) ⚠ throw vs 0.0

字段(a1+off): +8=following set(ttstr), +16=leading set, +48=vertical, +49=wordBreak,
+53=ignoreOverX, +54=ignoreOverY, +55=widthTimeScale, +84=renderCount, +88=charBufCountdown,
+108=kinsokuUsed, +112=kinsokuMax, +116=curFontSize, +184=fontScale, +188=delayAccum,
+192=charDelayStep, +232=penX, +236=penY, +240=renderSizeW, +244=renderSizeH, +280=renderPos,
+284=renderPosSnap, +320=pending deque(80B elem/6 per node/480B node), +416=lineBboxRight,
+420=lineBboxBottom, +424=wordBreakRun, +428=prevWasSpace, +504/512/520=accum UTF16 buf, +528=ruby.

## 已确认偏差 #1 (🔧 真逻辑偏离, 0x5A4D90-0x5A4DE8): following 集 used<max 路径
二进制(a2 在 following 集 a1+8, 且 used<max):
  ++kinsokuUsed;
  if (deque 空: +368==+336) -> LABEL_107 (finishLine);
  else { back = deque 末元素(+368 -0x50, 含 wrap);
         if (indexOf(following=a1+8, back) != -1) -> LABEL_107 (finishLine, 无下移);
         else -> 0x5A5338 直接 placeChar (无 finishLine, 不换行!); }
本地(620-629): 仅 _kinsokuUsed=used+1 + 空 no-op 注释, 然后无条件 fall 到 655 finishLine。
两处错: (1) 检查的是 deque **末/back** 字符对 **following** 集(非"首字符"非 leading);
(2) back 不在 following 集时二进制 **不 finishLine, 直接落字**, 本地却总是换行。
本地 624-627 注释"仅做首字符检查后落入 LABEL_107 无下移"是误判, 需重构该分支数据流。

## 已确认偏差 #2 (⚠ 平台边界候选/功能差, onGetTextWidth switch): Object/Octet 抛错
0x5A426C switch(variant.type): case1(Object) fall→case2, case3(Octet) fall→case4,
都先调 sub_A0E48C(__noreturn, 抛 TJS 类型转换异常) 再到 AsReal/(double)。
故 Object/Octet 二进制是 **throw**(中断脚本), 本地以 0.0 守护吞掉。功能不等价(漏抛)。
case2=String→sub_A133A8(AsReal), case4=Integer→(double), case5=Real→raw, default(void/0)=0.0 ✅。

## 已核实正确 (无偏差):
- appendChar: +88 倒计数(--后>=0 return true), buffer!=2字节 return false, effSize=+184*+116,
  ruby 分支(!vertical && +528) bbox 累加 + 消费后 release +528, +528 无 producer(死支但复刻忠实) ✅
- over 检测门控 (vertical: +244<=0||>penY+size||+54; horizontal: +240<=0||>penX+cw||+53) ✅
- placeChar: char.renderPos(+24)=renderPos(+280) 始终(不取max); delayAccum(+188)=max(+188,+280)
  via v9 指针(默认指 char+24, +188>renderPos 时指 +188) — 本地 674-681 语义正确 ✅
- following used>=max drain: size>=2 守卫; used<1 边界(查 leading=a1+16 末字符, 条件单次 push+pop,
  无 --used/--renderCount, break); 正常路径 push+pop + --kinsokuUsed + --renderCount ✅
- !wordBreak trailing-run: run=+424>=1 时 while(size>run) push末→tmp前 + pop + --renderCount ✅
- leading 分支(a2 不在 following): size>=3 查倒数第2(非leading)且末(在leading)→下移; LABEL_94 size>=2 末在leading→再下移 ✅
- finishLine 失败→sub_5A1B24(tmp)+return false; 成功→drain tmp 自递归 kinsoku; 末 placeChar(v6=!vertical) ✅
- updateWordBreakState/advanceLineVertical 1:1 ✅
- deque size 表达式 0xCCC...CD*((cur-first)>>4)+6*((node-map)>>3)-0x333...*(...)-6 = std::deque::size()
  的 libstdc++ 内联(80B elem>=512? 否, 但 6/node 块); "-6" 是块容量项, 非 sentinel
