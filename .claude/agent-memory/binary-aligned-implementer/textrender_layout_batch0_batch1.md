---
name: textrender-layout-batch0-batch5-DONE
description: textrender.dll TextRenderBase 全 50 成员忠实对齐 DONE(批0-5)：charItem 80B POD/pending deque/face hash/render 状态机/查询层+属性核对+集成审计
metadata:
  type: project
---

textrender.dll TextRenderBase 移植（cpp/plugins/textrender/TextRender.cpp，分批工程）**全 50 成员 DONE 2026-06-09**（批0-5）。

**批5 收尾（查询层+属性核对+集成审计）DONE 2026-06-09**：
- getCharacters@0x5A0694：count==0→renderCount(+84)-start（**非到末尾**，纠正旧 §4）；dict key 表见 analysis §9.1
  （text+0/x+8/y+12/cw+16/size+20/face=缓存名/color+28/bold+41/.../shadowDiff+48(sub_5A6020)/ruby(仅+56!=+64,sub_5A6240 子Array)/vertical+45/delay=**+24 renderPos**）；落数组=PropSetByNum(index)。face 缓存 v15 上一 faceIndex。
- getKeyWait@0x5A02DC：**keyWait 元素是 8B 双 int {index;time}**（纠正旧 "4B=renderPos float bits"）。\k push {renderCount,0}→done 重写 time(高 int)=charList[index].renderPos bits→**getKeyWait disasm 0x5a036c LDRSW 读低 int=index，pos=time=index**；time(高int) dead-for-getKeyWait（dead-but-faithful）。本地改 std::vector<KeyWaitItem>。
- onEval@0x5A0294：result.Clear()(type=Void)+sub_8E3FA4=**TVPExecuteExpression(param[0],objthis,result)**（字符串 ref ScriptMgnIntf.cpp 确证）。纠正旧桩。新增 #include "ScriptMgnIntf.h"。
- **属性核对**：renderDelay/maxScrollOffset/renderLines ✓已对齐；maxScrollLine@0x5A1080 批5 实装（原桩 0.0；视口起向前减 lineHeight+84 统计可容纳行）；**defaultFace@0x5A0DA8/0E0C 修正=INDEX-based**（get 查 _faceTable[+96] 越界空串，set=resolveFaceIndex→+96；删幻影 _defaultFace ttstr 字段）。
- **line ~1820 clang-tidy 半个分号**=误导性死代码（parseHexColor 恒 true），非真 bug，已改 if/else 1:1 复刻。
- **集成审计**：数据流贯通；对象级 ruby bbox +264/268/272 = dead-but-faithful（appendChar 写，clear 不 init/无 reader 交叉核实确认）；charItem 级 ruby vec(+56) 是 LIVE 消费端(getCharacters)；resolveFaceIndex faceTable.push 不变量维护正常。
- **验证**：web/debug 构建+链接通过。headless 千恋万花 textrender.dll Success + TextRender.tjs 加载成功，零 textrender abort/exception/No-method。游戏停在 custom.tjs(218) movieQualitySelectMenuItem 缺失=游戏菜单配置 quirk（**非 textrender 回归**）。

详见 analysis/textrender_textrenderbase_registration.md §9（getCharacters key 表/getKeyWait 解码/属性公式核对/集成审计/50 成员总表）。下面是批0+批1 地基（仍有效）：

**Why:** 系统设定颜色选择器/鉴赏模式需 textrender.dll；权威反编译归档 analysis/textrender_textrenderbase_registration.md（50 成员表 + §3b 字段 + §3b-1 charItem + §3b-2 deque + §6 rename）。
**How to apply:** 续批（批2 = setFont/setStyle/setDefault/setOption dict 解析；批3 = render 状态机 0x5A228C；done/newline）直接复用下列布局，勿重新推。

批0 地基（charItem 80B POD，三处反编译交叉确认）:
- charItem +0 ttstr text / +8 x / +12 y / +16 cw(onGetTextWidth) / +20 size(fontScale*curFontSize) / +24 renderPos(落字累积位置,calcShowCount 倒扫读) / +28 chColor / +32 shadowColor / +36 edgeColor / +40 rubyFlag(byte) / +41 bold / +42 italic / +43 shadow / +44 edge / +45 vertical / +48 shadowDiff / +52 faceIndex / +56 ruby vector(20B elem {ttstr*@0,x@8?,y,int@16}).
- 证据: charItem_copy@0x5A4838(incref +0,OWORD +8/+24/+40,deep-copy +56 vec/5=20B) + appendChar@0x5A3880(栈 v48..v64 蓝图,v49=width@+16 v50=size@+20 v51=chColor@+28...) + appendChar_kinsoku@0x5A4A7C(LABEL_10 回填 +8/+12/+24) + calcShowCount@0x5A0644(读 +24).
- **旧 §3b 注释字段错位(把 text 放 +40 实际 +0)已纠正**——勿信旧 "+8 x,+16 cw,+24 lineY,+40 text" 排布。

批0 pending-char deque(真对象 +320):
- std::deque<charItem>(80B elem, 480B node = 6 elem/node). 8 控制指针 +320 map/+328 mapsize/+336 start.cur/+344 start.first/+352 start.last/+360 start.node/+368 finish.cur/+376 finish.first/+384 finish.last/+392 finish.node. 证据 pendingDeque_init@0x5A15B0 + pushNode@0x5A57CC(new 0x1E0=480B).
- 用途: kinsoku(禁则)回退的尾部字符暂存, 换行后 appendChar_kinsoku 自递归 drain(LABEL_107 while v79!=v76). +424=行内 pending count(倒扫上界).
- dtor sub_5A1B24 释放 +320 = deque 析构(不是 "accumulated text buffer", 旧标注已纠正).
- size 公式里的 -6/+6/-80 是 libstdc++ deque size() 对 6/node 的内联展开, 非源码 token.

批1 四叶子函数(已实现, 构建通过):
- resolveFaceIndex@0x5A14DC: face 名→稳定 idx intern. 命中返回旧 idx; 未命中 idx=faceTable.size()((+464-+456)>>3)并存 hash, **不 push faceTable**(push 在 setFont/setDefault). hash 算法字节复刻 FaceNameHash: 逐 UTF16 码元 t=1025*(acc+ch); acc=t^(t>>6); 末 *9; v=32769*(h^(h>>11)); 0→0xFFFFFFFF; 空串→0. 本地 unordered_map<ttstr,int,FaceNameHash,FaceNameEq>.
- onStyleChanged@0x5A1F28: build dict{face=_faceTable[_curFaceIndex 越界空串],bold=+62,italic=+65} → objthis FuncCall(L"onFontChange",dict). 纯 TJS dispatch. 当前无调用者(setFont 桩),oracle-inert 但有反编译证据照实现.
- calcLineOffset@0x5A05FC: 行列表 stride 112 offset@+80; 越界(unsigned count<=（u32)idx)返回 +260 bottom.
- calcShowCount@0x5A0644: char 列表(+296,8B 指针 elem)倒扫; count<=1→0; while(charItem->renderPos(+24)*timeScale(+180)>width)--; 到头(v6<=1)→0.

下批前置依赖: dict 解析(批2)需 PropGet 逐 key(face→resolveFaceIndex 已就绪/bold/fontsize/color...写当前样式或 default* 字段); render 状态机(批3,0x5A228C)是 KAG 转义标签状态机, 落字走 appendChar@0x5A3880→appendChar_kinsoku@0x5A4A7C(已分析未移植), 字宽走 sub_5A426C FuncCall(L"onGetTextWidth") 回调脚本(非平台边界, 可移植).
