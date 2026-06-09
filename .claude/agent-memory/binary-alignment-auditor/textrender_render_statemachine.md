---
name: textrender-render-statemachine
description: TextRenderBase::render 状态机 @0x5A228C 对齐结论 + curRubyText(+528) 数据流穷尽证据(两项强断言均证本地正确)
metadata:
  type: project
---

TextRenderBase render 状态机 @0x5A228C (NCB wrapper @0x59FC28) 经 2026-06-09 逐分支审计 = ✅ 完全对齐。本地 cpp/plugins/textrender/TextRender.cpp renderImpl(1836)/renderPercentTag(1464)/renderBalancedChar(1746)/render-wrapper(1796) 1:1。

**强断言1 — ruby `[...]` 不写 +528：本地正确，非缺口。**
- '[' case @0x5A2878: scanTagUntil(delim=']'=93) → 若 !+56(ignoreRuby) `__ldaxr/__stlxr`AddRef 后 LABEL_114 `tTJSVariant_Release` = 净空操作。**全函数无 +528 store**。
- 穷尽证据：TextRenderBase 簇 0x59E000-0x5A6000(8074 insn) `STR/STUR/STP @528 与 @520 全=0`；唯一 `ADD #0x210` 在 appendChar@0x5A3A58(消费者)。
- +528(curRubyText) 在二进制只有3处接触，**全是消费/清零，无 producer**：
  - appendChar sub_5A3880：0x5A3A4C 读 → 渲染 ruby 字形(rubyVec 20B 元素) → 0x5A3B48 Release → 0x5A3B4C 置 null。
  - finishLine sub_5A34B8：LABEL_56 @0x5A37F0 读 → Release → 0x5A3800 置 0。
- 结论：render 调用链内 curRubyText 永无写入。本地 _curRubyText 只清不赋与二进制一致。真正 producer(若存在)是 TextRenderBase 之外的 setter，非 render 审计范围。**勿据此判 render 有缺口。**

**强断言2 — %C/%R/%L align cascade → +76(curAlign)=-1：本地正确，真 fall-through。**
- 'C'@0x5A2C50: !+59→`+76=0`→fall LABEL_226`+76=1`→fall LABEL_227`+76=-1`→LABEL_228。终值 -1。
- 'R'@0x5A2D88: !+59→`goto LABEL_226`(1→-1)。终值 -1。
- 'L'@0x5A2D7C: !+59→`goto LABEL_227`(-1)。终值 -1。
- 是真实 switch fall-through 编译产物，非 case 标签误读。本地三连写 1:1 复刻终值 -1。

**字段偏移速查(a1+N)**：+48=vertical, +50=ignoreColor, +51=ignoreSize, +52=ignoreDelay, +56=ignoreRuby, +57=ignoreType, +58=ignoreFace, +59=ignoreStyle, +62..69=cur/default bold/italic/edge/shadow, +72=faceIndex, +76=curAlign, +84=renderCount, +88=charLimit, +96=defaultFace, +116=curFontSize, +148=defaultFontSize, +152=bigFont, +156=smallFont, +188=renderDelayAccum, +192=charDelayStep(=a4), +196=lineStartX, +200=curChColor, +216=defaultChColor, +232=penX, +236=penY, +280=renderPos, +432/440/448=lineList(112B元素), +480/488/496=keyWaitList, +504/512/520=charBuf(UTF16), +528=curRubyText(owning tTJSVariant*)。
**render 参数语义(BLOCKING,非坐标)**：x(a3)=begin/end 平衡集启用标志(`if(!a3)`门控)；y(a4)=charDelayStep(+192)初值。NCB wrapper param[3]=size 仅类型校验后丢弃。
**子函数**：finishLine sub_5A34B8, appendChar sub_5A3880, scanTagUntil sub_5A3CE4, scanDigits sub_5A3F18, parseInt10 sub_9B111C, evalDollarTag sub_5A4148, onStyleChanged sub_5A1F28, resetFont sub_59EEE0, keyWait push sub_5A5874。
