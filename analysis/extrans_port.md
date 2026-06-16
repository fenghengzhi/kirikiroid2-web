# extrans.dll 移植分析（libkrkr2.so ↔ krkrz/SamplePlugin/extrans）

## 0. 结论先行

`libkrkr2.so` 中的 extrans.dll **就是** 上游官方示例插件
[krkrz/SamplePlugin/extrans](https://github.com/krkrz/SamplePlugin/tree/master/extrans)（W.Dee）
被 kirikiroid2 内建化（builtin）编译进二进制的产物。已用三重证据确证同源：

1. 7 个转场名（GetName 返回值）与仓库效果集合一一对应。
2. ASCII 符号 `extrans_turn_Blend`（@0x14e4b40）对应 turn.cpp。
3. 注册链顺序与仓库 `Main.cpp` 的 `V2Link` 完全一致（见 §2）。

因此移植方法论：**以仓库 `.cpp/.h` 源码为实现基底**，libkrkr2.so 反编译退为
**差分裁判**——逐函数对照二进制，抓 kirikiroid2 的 delta（像素格式、双路径渲染、
ARM/移动端适配），分歧处以二进制为准并在注释引用函数地址；一致处保留仓库源码原样
（含变量名 / 容器 / 控制流）。

## 1. 二进制侧地址索引

| 符号 | 地址 | 说明 |
|---|---|---|
| extrans 模块静态注册器 | `sub_42F224` @0x42F224 | 名 `L"extrans.dll"` 挂内建插件链表（head=`xmmword_1AB8920`），init=`sub_7C2ACC` |
| extrans init（= V2Link 体） | `sub_7C2ACC` @0x7C2ACC | 链式调 5 个 Register*（见 §2） |
| 内建插件链表消费者（loader） | `sub_548924` / `sub_548ACC` | 遍历链表注册所有 builtin |

模块描述符 vtable=`off_19FD8E8`，节点布局 `{vtable, name(L"extrans.dll"), next, initfn=sub_7C2ACC}`。
与 motionplayer / emoteplayer / textrender 同一注册表。

## 2. 注册链（sub_7C2ACC）↔ 仓库 Main.cpp::V2Link

二进制链式调用顺序 == 仓库 V2Link 调用顺序（**完全一致**）：

| 顺序 | 仓库 V2Link | 二进制 init | provider vtable | GetName 字符串 |
|---|---|---|---|---|
| 1 | RegisterWaveTransHandlerProvider | `sub_7CD05C` | `off_1A25C00` | `wave` |
| 2 | RegisterMosaicTransHandlerProvider | `sub_7C4438` | `off_1A25858` | `mosaic` |
| 3 | RegisterTurnTransHandlerProvider | `sub_7CBB5C` | `off_1A25B68` | `turn` |
| 4 | RegisterRotateTransHandlerProvider | `sub_7C9344` | `off_1A25990/1A25A30/1A25A70` | `rotatezoom` / `rotatevanish` / `rotateswap` |
| 5 | RegisterRippleTransHandlerProvider | `sub_7C7490` | `off_1A258F0` | `ripple` |

注：内建化后 `TVPInitImportStub(exporter)` 被省略（builtin 无需 tp_stub），其余逐字保留。

## 3. 项目侧接入点

- 转场接口：`cpp/core/visual/transhandler.h`（`iTVPTransHandlerProvider` /
  `iTVPDivisibleTransHandler` / `iTVPGiveUpdateTransHandler` / `tTVPDivisibleData`）。
- 注册 API：`TVPAddTransHandlerProvider` / `TVPRemoveTransHandlerProvider`
  （`TransIntf.cpp:289/307`）——**与仓库源码同名同签，可逐字移植**。
- 双路径渲染胶水参考实现：`tTVPCrossFadeTransHandler::Process` / `::Blend`
  （`TransIntf.cpp:584/614`）——软件 scanline + GPU `OperateRect` 双路。
- 纹理 scanline：`iTVPTexture2D::GetScanLineForRead/ForWrite`（`RenderManager.h:122`）。
- 模块注册：`NCB_PRE_REGIST_CALLBACK` + `#define NCB_MODULE_NAME TJS_W("extrans.dll")`
  （最小例 `cpp/plugins/fftgraph.cpp`）。**必须放进 `krkr2plugins_ncbind` 库**
  （带 `--whole-archive`/`force_load`），否则 NCB 注册被 dead-strip（textrender 教训）。

## 4. kirikiroid2 关键 delta（接口语义改写）

仓库是 Win32 plugin，直接用 `iTVPScanLineProvider::GetScanLine` 做软件 blend。
kirikiroid2 把转场接口改成 **texture-based + GPU/软件双路分发**：

1. **scanline 经 texture 取**：`data->Src->GetScanLine(...)`（仓库）→
   `data->Src->GetTexture()->GetScanLineForRead(line)`（二进制）。
   故项目 `iTVPScanLineProvider` 直接 scanline 方法被 `#if 0`。
2. **Process 双路径**：每个 handler 的 `Process` 按运行期标志（`hasGPUAccel_guess`）分发：
   - 软件路径：逐行 CPU blend（`TVPFillARGB` / `TVPConstAlphaBlend_SD[_d/_a]`），写入
     `Dest->GetTextureForRender()->GetScanLineForWrite(line)`，读
     `Src->GetTexture()->GetScanLineForRead(line)`。**完整复刻仓库逐行算法**。
   - GPU 路径：`TVPGetRenderManager()->OperateRect(method, dest, ...)` +
     `tRenderTexRectArray::Element(tex, rect)`。**位移在 fragment shader 内表达**——
     GPU 路径不退化：wave 的 GLSL（WaveTrans/_a/_d）在 shader 里
     `dx=sin(v_texCoord0.y*omega+phase)*h` 做 per-pixel 位移，效果与软件路径一致
     （证据 `sub_7CC428`）。
     ~~（早先误判"GPU 退化为平直矩形 blend"已由 wave 实现轮次证伪并纠正——见 §5。）~~
   - 分发标志：`sub_84B7FC`（旧误名 `hasGPUAccel_guess`，方向相反；真值=软件渲染器→走 CPU
     scanline）= 项目 `TVPGetRenderManager()->IsSoftware()`。IDB 已 rename 为
     `TVPIsSoftwareRenderer_guess`。
3. **颜色通道 R/B 交换**：bgcolor 存入 handler 字段时做
   `c & 0xFF00FF00 | BYTE2(c) | (byte0(c)<<16)`（交换 R/B），匹配引擎像素格式。

## 5. wave 完整映射（已 100% 取证，作为各效果的样板）

| 方法 | 二进制 | 与仓库关系 |
|---|---|---|
| Provider::StartTransition | `sub_7CD1A8` @0x7CD1A8 | 逐行一致：选项 time/maxh/maxomega/bgcolor1/bgcolor2/wavetype + 默认值（maxh=50, maxomega=0.2, time≥2）；type=ttExchange, updatetype=tutDivisible；src1==src2 尺寸校验；**+delta**：bgcolor 存入时 R/B 交换 |
| Handler::ctor | （内联于 StartTransition） | RefCount=1, LayerType/Width/Height/Time/HalfTime=time/2/MaxH/MaxOmega/WaveType, First=true（字段偏移是编译器 ABI，不复刻偏移，复刻字段语义） |
| Handler::StartProcess | `sub_7CBEDC` @0x7CBEDC | 逐行一致：First/StartTick；CurTime 钳制到 [0,Time]，t 折返 HalfTime；`tt=sin(π/2·t/HalfTime)`；CurH=tt·MaxH；WaveType switch（0:MaxOmega·tt / 1:MaxOmega·(Time-CurTime)/Time / 2:MaxOmega·CurTime/Time）；CurRadStart=-CurOmega·(Height/2)；BlendRatio=CurTime·255/Time 钳 255；CurBGColor=Blend(BG1,BG2,BlendRatio) |
| Handler::EndProcess | `sub_7CC080` | BlendRatio==255 → TJS_S_FALSE 否则 TJS_S_TRUE |
| Handler::Process | `sub_7CC094` @0x7CC094 | **双路径**（见 §4.2），分发=`IsSoftware()`。软件路径逐行：rad=Top·CurOmega+CurRadStart，d=sin(rad)·CurH，d>0/d<0 两侧填 CurBGColor，中段 Clip 后按 LayerType（ltAddAlpha=12 / ltAlpha=2 / else）选 blend 写入。GPU 路径：WaveTrans GLSL 在 shader 内做同样位移（`sub_7CC428`），**不退化** |
| Handler::MakeFinalImage | `sub_7CD118` | *dest=src2 |

## 9. mosaic 完整映射（已 100% 取证）

| 方法 | 二进制 | 与仓库关系 |
|---|---|---|
| Register | `sub_7C4438` @0x7C4438 | new Provider(0x10, vtable=off_1A25858, RefCount=1) + 全局 `qword_1AD9048` + TVPAddTransHandlerProvider |
| Provider::GetName | `sub_7C456C` @0x7C456C | `*name=L"mosaic"`（amosaic @0x14E432A，UTF-16LE 字節確認） |
| Provider::StartTransition | `sub_7C4584` @0x7C4584 | 逐行一致：type=ttExchange(1)/updatetype=tutDivisible(1)；!handler/!options→FAIL；src1==src2 尺寸校验；time 必填(PropGet 失败/tvtVoid→FAIL)钳<2→2；maxsize 默认 30→MaxBlockSize。**无颜色 delta**（区别于 wave，handler 0x50 内无 bgcolor 偏移） |
| Handler::ctor | （内联于 StartTransition） | RefCount=1, Width/Height/Time/HalfTime=Time/2/MaxBlockSize, First=true。StartTick/CurTime/CurOfs*/CurBlockSize/BlendRatio **不初始化**（忠实复刻，不补 0） |
| Handler vtable | `off_1A25800` | 槽序 AddRef/Release/SetOption/StartProcess/EndProcess/Process/MakeFinalImage/dtor-nullsub/delete |
| Handler::AddRef | 0x7C44A0 | ++RefCount |
| Handler::Release | 0x7C44B4 | RefCount==1→delete this 否则 --RefCount |
| Handler::SetOption | 0x7C44EC | no-op return TJS_S_OK |
| Handler::StartProcess | `sub_7C2AEC` @0x7C2AEC | 逐行一致：First→StartTick；CurTime 钳[0,Time]，t 折返 HalfTime；CurBlockSize=(MaxBlockSize-2)·t/HalfTime+2；BlendRatio=CurTime·255/Time 钳255；CurOfsX/CurOfsY 中心对齐（二进制用 `hw%blk-hw+(W-blk)/2` 等是上游 `Width/2/blk*blk` 的代数等价展开，>0 减一 block） |
| Handler::EndProcess | `sub_7C2BD8` @0x7C2BD8 | BlendRatio==255→TJS_S_FALSE 否则 TJS_S_TRUE |
| Handler::Process | `sub_7C2BEC` @0x7C2BEC | **双路径**，分发=`TVPIsSoftwareRenderer_guess`（mosaic 用此而非 wave 的 sub_84B7FC，本地两者同语义=IsSoftware()）。软件路径=上游 block 镶嵌（FILL_LINE switch xlen 2..7+default TVPFillARGB；FILL_ONE clip+中心像素 Blend；三段循环顶/中/底行，中段免 clip），scanline 经 GetTextureForRender/GetTexture，pitch 经纹理 GetPitch。GPU 路径=`"MosaicTrans"` GLSL（floor 网格吸附 floor((uv-offset)·tile)/tile+offset + mix；shader 串 @0x14E4204 字節確認）+ uniform opa=BlendRatio/255、tile=(Src2texW/blk,Src2texH/blk)、offset=(CurOfsX/texW,CurOfsY/texH) + OperateRect |
| Handler::MakeFinalImage | `sub_7C44F4` @0x7C44F4 | *dest=src2 |

**delta 总结**：(1) 算法 100% 对齐上游 mosaic.cpp。(2) 无颜色字段（区别于 wave 的 R/B 交换）。(3) GPU "MosaicTrans" 路径为 kirikiroid2 专属（上游 Win32 无），shader 已字節級復刻。(4) 分发函数 `TVPIsSoftwareRenderer_guess`≠wave 的 sub_84B7FC，本地统一 IsSoftware()。

## 10. turn 完整映射（已 100% 取证 + auditor 零偏差）

| 方法 | 二进制 | 与仓库关系 |
|---|---|---|
| Register | `sub_7CBB5C` @0x7CBB5C | new Provider(0x10, vtable=off_1A25B68, RefCount=1) + 全局 `qword_1AD9980` + TVPAddTransHandlerProvider |
| Provider::GetName | `sub_7CBC90` @0x7CBC90 | `*name=L"turn"`（@0x1525788，UTF-16LE 字节确认 `74 00 75 00 72 00 6E 00 00 00`） |
| Provider::StartTransition | `sub_7CBCA8` @0x7CBCA8 | 逐行一致：type=ttExchange(1)/updatetype=tutDivisible(1)；!handler/!options→FAIL；src1==src2 尺寸校验；time 必填(失败/tvtVoid→FAIL)钳<2→2(0x7cbdb8)；bgcolor 默认 0。**+delta D1（与 wave 同款，mosaic 无）**：bgcolor 存入 BGColor 字段时 R/B swizzle `c&0xFF00FF00\|BYTE2(c)\|((u8)c<<16)`（@0x7cbe84）。handler `operator new(0x40)`=64B |
| Handler::ctor | （内联于 StartTransition） | RefCount=1(+8)/Width(+40)/Height(+44)/Time(+24)/BGColor=swizzled(+48)/First=true(+56)。StartTick(+16)/CurTime(+32)/Phase(+52) **不初始化**（忠实复刻不补 0） |
| Handler vtable | `off_1A25B10`（≠ Provider 的 off_1A25B68） | AddRef/Release/SetOption/StartProcess/EndProcess/Process/MakeFinalImage/nullsub/delete |
| Handler::AddRef | 0x7CBBC4 | ++RefCount |
| Handler::Release | 0x7CBBD8 | RefCount==1→delete this 否则 --RefCount |
| Handler::SetOption | 0x7CBC10 | no-op return TJS_S_OK |
| Handler::StartProcess | `sub_7CA9CC` @0x7CA9CC | 逐行一致：First→StartTick；CurTime=min(tick-StartTick,Time)；xcount=(W-1)/64+1，ycount=(H-1)/64+1；Phase=CurTime*(64+(xcount+ycount)*2)/Time - ycount*2。**TURN_WIDTH_FACTOR=2**（乘数常数 `2*(...)+66` 确认） |
| Handler::EndProcess | `sub_7CAA54` @0x7CAA54 | CurTime==Time→TJS_S_FALSE 否则 TJS_S_TRUE |
| Handler::Process | `sub_7CAA68` @0x7CAA68 | **双路径**（分发 `TVPIsSoftwareRenderer_guess`@0x7caab4 = IsSoftware()）。**软件路径**=上游 turn.cpp table-driven per-tile 64×64 warp（两大 block 分支：边界 block 带左右 clip / 中间 block 免左右 clip；每分支 phase==0 全 src1 memcpy / phase==63 全 src2 memcpy / else `TurnTransParams[phase]` warp + `gloss[phase]` blend；中间 block gl==0 用 TVPLinTransCopy）。scanline 经 GetTexture()->GetScanLineForRead / GetTextureForRender()->GetScanLineForWrite + 纹理 GetPitch（kirikiroid2 delta D2/D3，同 wave/mosaic）。**GPU 路径**=kirikiroid2 专属（上游 Win32 无）：①初次生成 gloss 角度 double 表 `qword_1AD9150`（guard `byte_1AD9988`，0x7cb450；公式 `n*n/31`±`(int)(sin(...)*4)` 截断，存 `{a,b,64-a,64-b}`）②`"FillARGB"` builtin 填 BGColor 背景（OperateRect vt+160）③per-tile `"extrans_turn_Blend"` shader + OperateTriangles(nTriangles=2、6 顶点 dest+6 顶点 src 歪角 mesh、color=0xFFFFFFFF、opacity=gloss[phase] 整数、phase<32→src1 else src2、vt+168) |
| Handler::MakeFinalImage | `sub_7CBC18` @0x7CBC18 | *dest=src2 |

**表 .rodata 字节核对**（本轮独立 get_bytes 复核）：
- `TurnTransParams` 表基址 `dword_14E4CD4` @0x14E4CD4（[64][64]×32B=8 int32/元素）。抽样 phase=0/y=0 全 0✓；phase=32/y=31 (0x14F50B4) = `{30,2,0,3604480,4128768,327680,4128768,-3276800}` 与上游 turntrans_table.cpp 完全吻合✓。**二进制嵌入表 == 上游表**，逐字复制纳入合法（CLAUDE.md「按字节读元素内部数据格式」例外）。
- `gloss[64]` 整数表基址 `dword_14E4BD4` @0x14E4BD4。前 16 项 = `0,0,0,0,16,48,80,128,192,128,80,48,16,0,0,0` 与上游字面 `{0,0,0,0,16,48,80,128,192,128,80,48,16,0,...}` 完全吻合✓。

**delta 总结**：(1) 软件路径算法 100% 对齐上游 turn.cpp。(2) **有颜色字段 + R/B swizzle**（与 wave 同款 D1，区别于 mosaic）。(3) **Process 有 GPU 分支**（mesh-warp，上游 Win32 纯软件，kirikiroid2 专属；FillARGB 背景 + extrans_turn_Blend 6 顶点 OperateTriangles mesh blend）。(4) TurnTransParams[64][64] + gloss[64] 表 = 平台无关数据契约，逐字复制 + 字节核对吻合二进制。(5) `#pragma pack(push,4)` 的 tTurnTransParams 元素布局是数据契约（按字节读表元素），保留属允许例外。

## 11. rotate 系完整映射（已 100% 取证 + auditor 零偏差）

rotatezoom / rotatevanish / rotateswap 共享基类 `tTVPBaseRotateTransHandler`
（rotatebase.cpp/h）。ZoomHandler 被 rotatezoom 和 rotatevanish **共用同一个类**
（vtable off_1A259D0）；SwapHandler 独立（vtable off_1A25AB0）。

| 角色 | 二进制 | 说明 |
|---|---|---|
| Register | `sub_7C9344` @0x7C9344 | zoom→vanish→swap 顺序 new(0x10)+RefCount=1+TVPAddTransHandlerProvider。全局 qword_1AD9118/1AD9120/1AD9128。Provider vtable off_1A25990/1A25A30/1A25A70 |
| 基类 ctor | `sub_7C7F30` @0x7C7F30 | **DrawData = operator new[](104*Height)** 动态数组（sizeof 元素=104B：count+src1(20)+src2(20)+region[5](60)）。GPU vertex vector(+72/80/88) 三指针清零。RefCount=1/Width/Height/Time/BGColor、First=true。CurTime/StartTick/Phase 不初始化 |
| 基类 dtor | `sub_7C7F9C` @0x7C7F9C | `delete[] DrawData` + delete GPU vertex 缓冲 |
| 基类 StartProcess | `sub_7C7FEC` @0x7C7FEC | First/StartTick；CurTime 钳[0,Time]；清每行 DrawData(count=1, region[0]={0,Width,type=0})；GPU vector 清空(end=begin)；调虚 CalcPosition |
| 基类 EndProcess | `sub_7C80CC` @0x7C80CC | CurTime==Time→TJS_S_FALSE |
| 基类 Process | `sub_7C80E0` @0x7C80E0 | **双路径**（IsSoftware 分发）。软件=上游 region-walk（type0 TVPFillARGB / stepx==65536&&stepy==0 memcpy / stepy==0 TVPStretchCopy / else TVPLinTransCopy），scanline 经 GetTextureForRender/GetTexture+GetScanLineFor{Read,Write}+GetPitch（D-scanline）。GPU=kirikiroid2 专属："FillARGB" OperateRect 填 BGColor 背景 + GPUQuads 每元素 "Copy" OperateTriangles(nTriangles=2) 网格转送（dest 6 顶点 pt0/pt1/pt2/pt1/pt2/第4隅、src 6 UV=src 矩形四角） |
| 基类 AddLine | `sub_7C872C` @0x7C872C | region 四情形分割插入（完全内包→长度0 / 跨左右→分裂 count++ break / 左溢出裁右 / 右溢出裁左 + 找空位插入 i==count→count++）|
| 基类 AddSource | `sub_7C88B0` @0x7C88B0 | **双路径**。软件=四顶点 65536 定点扫描线光栅化（pd/pa/pdnext/panext ring 0..3、sdstep/sastep switch 0..3）。GPU=push 28B quad 记录{type,3 点}到 GPUQuads 即返 |
| AddRef/Release/SetOption/MakeFinalImage | 0x7C9108/0x7C911C/0x7C9154/0x7C915C | ++RefCount / ==1→delete / no-op / *dest=src2 |
| zoom Provider::StartTransition | `sub_7C94B8` @0x7C94B8 | 选项 time(钳<2→2)/factor(1)/accel(0)/twist(2)/twistaccel(-2)/centerx(w/2)/centery(h/2)。ZoomHandler new(0x98), bgcolor=0 给基类, FixSrc1=true。zoom-target=1.0 硬写 |
| vanish Provider::StartTransition | `sub_7C9D48` @0x7C9D48 | 选项 time/accel(2)/twist(2)/twistaccel(2)/cx/cy（**无 factor**）。同 ZoomHandler 类(vtable off_1A259D0), factor=0/target=1, FixSrc1=false（xmmword_14E49B0={1.0,0.0}）|
| swap Provider::StartTransition | `sub_7CA268` @0x7CA268 | 选项 time/bgcolor(0)/twist(1)。SwapHandler new(0x68)。**bgcolor 无 R/B swizzle**（D-noswizzle）。Twist=twist*π*2 存 float +96 |
| ZoomHandler::CalcPosition | `sub_7C9A14` @0x7C9A14 | accel<0 上弦/>0 下弦 pow；zm=factor+(target-factor)*v10；rad=**2π**·twist·tm；三顶点 s/c 仿射；两次 AddSource(FixSrc1?1:2 / ?2:1) |
| SwapHandler::CalcPosition | `sub_7CA57C` @0x7CA57C | switch(CurTime>=Time/2?0:1)-into-while **Duff 流**；src1 段 tm=zm²、src2 段 tm=1-(1-zm)²；sin(tm·**π**)·scx·±1.5（src1 +1.5/src2 -1.5）；两次 AddSource(1)/(2) 各 cnt-- |

**delta 总结**：
- **D-π（与样板的关键差异，证伪了"照上游笔误复刻"假设）**：二进制**修正了上游 rotatebase.cpp/rotatetrans.cpp 的 π 笔误**。上游写 `3.14159265368979`（少一位），但二进制嵌入精确 π：get_bytes 确认 qword_14E49A8=`AD 9C 47 54 FB 21 09 40`=3.141592653589793(π)、qword_14E49A0=`AD 9C 47 54 FB 21 19 40`=6.283185307179586(2π)。**本地用精确 π/2π（ROTATE_PI/ROTATE_2PI），不照抄上游笔误**——二进制是权威源。
- **D-noswizzle**：rotateswap bgcolor **不做 R/B swizzle**（原样存基类 BGColor），区别于 wave/turn 的 D1。rotatezoom/vanish 给基类传 bgcolor=0。
- **D-container**：DrawData 真实动态数组 new[Height]/delete[]（复刻）。GPU vertex 容器经构造点核对（begin/end/cap 三指针 + 手动 push/扩容 + dtor delete）= libstdc++ std::vector 内联展开，本地用 std::vector<tRotateGPUQuad> 是源码级忠实复刻非简化。
- **D-GPU**：Process/AddSource 均有 GPU 双路径（上游 Win32 纯软件，kirikiroid2 专属）。render-method = builtin "FillARGB"+"Copy"（**非自带 GLSL shader**，区别于 wave/mosaic 的自定义 shader 串），OperateRect 背景 + OperateTriangles(n=2) 网格。
- **D-scanline**：软件路径 scanline 经 texture（同 wave/mosaic/turn）。

## 12. ripple 完整映射（已 100% 取证 + auditor 单偏差修复后零偏差）

ripple 是仓库 52KB 最大效果，含 `tTVPRippleTable`（预計算波形/置換マップ）+ Handler + Provider。
实现基底 = 上游 ripple.cpp，差分裁判 = 二进制。

| 方法 | 二进制 | 与仓库关系 |
|---|---|---|
| Register | `sub_7C7490` @0x7C7490 | 缓存清零(xmmword_1AD90C8/D8)+**能力门**(见 D-cap)+new Provider(0x10,vtable=off_1A258F0,RefCount=1)+全局 qword_1AD90C0+TVPRegisterTransHandlerProvider |
| Provider::GetName | `sub_7C7920` @0x7C7920 | `*name=L"ripple"`（amultiripple+0xA @0x14CA88A UTF-16LE 确认） |
| Provider::StartTransition | `sub_7C7938` @0x7C7938 | type=ttExchange/updatetype=tutDivisible；!handler/!options→FAIL；src1==src2 校验；选项 time(必填钳≥2)/centerx(w>>1)/centery(h>>1)/rwidth(128,(rwidth-16)>>4∈{0..3}&mask 0x8B=16/32/64/128)/roundness(1.0,>0)/speed(6)/maxdrift(24,0~127且<min(w,h))。**5 条异常字符串字节确认**（含拼写 "nagative"）。new Handler(0x78)+ctor。layertype(a4) 参与 |
| Handler::ctor | `sub_7C4FB0` @0x7C4FB0（StartTransition 内 new） | RefCount=1/LayerType/Width/Height/Time/CenterX/CenterY/RippleWidth/Roundness/Speed/MaxDrift/First=true。StartTick/CurTime/BlendRatio/Phase/Drift/DriftCarePixels/CurDriftMap **不初始化**（忠实复刻不补 0）。**TVPGetRippleTable 内联**=2 OWORD pair（xmmword_1AD90C8/D8 = 4 槽指针）+ memmove LRU = 上游 4 槽 cache + TVPGetRippleTable 的编译器内联展开（本地复刻上游 4 槽源码结构）。Table refcount++ |
| Handler vtable | `off_1A25898`（≠ Provider off_1A258F0） | AddRef(0x7C7670)/Release(0x7C7684)/SetOption(0x7C76BC no-op)/StartProcess(0x7C5288)/EndProcess(0x7C53B8)/Process(0x7C53CC)/MakeFinalImage(0x7C76C4)/GetType(0x7C76D0)/dtor(0x7C7738) |
| Handler::StartProcess | `sub_7C5288` @0x7C5288 | 逐行一致：First/StartTick；CurTime=tick-StartTick；BlendRatio=CurTime*255/Time 钳255；Phase=(int)(Speed*(1/2π)*(1/1000)*CurTime*RippleWidth)%RippleWidth，<0→0，再 RippleWidth-Phase-1（二进 0.000159154943=1/2π 确认）；Drift=(int)(sin(π*CurTime/Time)*MaxDrift*4) 钳[0,MaxDrift*4-1]（二进 vcvts_n_s32_f32(...,2)=Q2 定点*4）；DriftCarePixels=Drift/4+1 奇则+1；CurDriftMap=GetDriftMap(Drift,Phase) |
| Handler::EndProcess | `sub_7C53B8` @0x7C53B8 | BlendRatio==255→TJS_S_FALSE(2) 否则 TJS_S_TRUE(1) |
| Handler::Process | `sub_7C53CC` @0x7C53CC | **双路径**（分发 (TVPIsSoftwareRenderer_guess&1)==0→GPU else→软件）。软件=上游逐行涟漪位移 scanline（经 GetTextureForRender/GetTexture，D3），行内分四水平区段：中段非折返调 off_1AA59E0(forward)/off_1AA59D8[0](backward) 函数指针，四折返边界(_f_a_e/_f_d_e/_b_a_e/_b_d_e)二进制内联展开（折返公式 2*w-1-x / 2*h+0x3FFFFFFF-y=2*h-1-y），本地复刻上游 4 独立折返函数（源码 token）。GPU=kirikiroid2 专属（上游 Win32 无）：GetOrCompileRenderMethod(**"MosaicTrans"**,ripple-GLSL,2,0)+6 uniform(opa/center/roundness/rwidth/drift/phase)+OperateRect(vtable+160) |
| Handler::MakeFinalImage | `sub_7C76C4` @0x7C76C4 | *dest=src2 |
| tTVPRippleTable::MakeTable | `sub_7C47B0` @0x7C47B0 | **运行期计算**（非静态表）：DisplaceMap(new uint16[]+atan/sqrt 二重循环)+DriftMap(new uint16[]+rippleform/cos-sin 定点表 NEON 批写)。常量 DIR_PREC=32/DRIFT_PREC=4/2048.0 定点/-2π/1.19/0.2 二进一致 |
| C transform _c_f | `sub_7C7824` @0x7C7824 | forward `*displacemap++`，ofs=(i-(s8)(n>>8))*4+(s8)n*pitch，0xFF00FF/0xFF00 双通道 blend（off_1AA59E0 默认） |
| C transform _c_b | `sub_7C77A4` @0x7C77A4 | backward `*displacemap--`，ofs=(i+(s8)(n>>8))*4+(s8)n*pitch（off_1AA59D8[0] 默认） |
| 能力门 nullsub | `nullsub_216..219` @0x7C78AC..7C78B8 | 全空 no-op |

**delta 总结**：
- **D-cap（能力门，ripple 专属重点）**：Register 内 cputype=TVPGetCPUType()（二进 sub_9162C4，0x20000=MMX/0x400000=EMMX，本地 TVP_CPU_HAS_MMX/_EMMX 值恰好吻合）按位选函数指针：`if(MMX){off_1AA59E0(f)=nullsub_216; off_1AA59D8[0](b)=nullsub_217; if(EMMX){f=nullsub_218; b=nullsub_219;}}`。**SSE2(0x800000) 分支二进制无**（上游有但 ARM build 消去）。选中的优化扫描例程在 ARM build 全是 **nullsub（空函数）**——x86 MMX/EMMX/SSE2 在 ARM 不存在，最适化路径被编空。忠实复刻=函数指针成员+TVPInitRippleTransformFuncs 能力门结构+4 个 no-op nullsub，默认指针 f=_c_f/b=_c_b。**⚠ 陷阱**：第一份 ida-deep-analyzer 报告把 nullsub f/b 映射写反（f=217/b=216），auditor 用反汇编字面三重交叉证伪并纠正为 f=216/b=217（forward=off_1AA59E0=nullsub_216，backward=off_1AA59D8[0]=nullsub_217）。已就地修正（证伪即纠正）。
- **D-GPU shader 名"MosaicTrans"**：ripple 的 GPU render-method 名在二进制是**字面 "MosaicTrans"**（@0x7C5FCC 确认，kirikiroid2 作者从 mosaic 复制粘贴的痕迹，但 shader 本体是 ripple 的 GetOffset 涟漪 per-pixel 位移）。二进制权威，名字保持 "MosaicTrans" 不改 "RippleTrans"。
- **D-phase 符号**：GPU phase uniform 用 -1/(2π)（-0.000159154943），软件 StartProcess 用 +1/(2π)，符号相反，保留。
- **D-noswizzle**：ripple **无颜色字段**（区别于 wave/turn 的 R/B swizzle D1）。
- **D-container**：缓存复刻上游 4 槽 TVPRippleTableCache[4]+memmove LRU+TVPGetRippleTable（二进制 2-OWORD-pair 是其内联展开）。tTVPRippleTable 动态对象 new/delete + AddRef/Release refcount 复刻。DisplaceMap/DriftMap = new uint16[]/delete[]。
- **D-scanline**：软件路径 scanline 经 texture（同 wave/mosaic/turn/rotate）。
- **未移植**：上游 MMX/EMMX/SSE2 inline-asm transform（_mmx_f/_sse2_f 等）是 x86 专用，ARM build 不存在，不移植（仅复刻 C 标量版 + 能力门 nullsub 结构）。TVPAddLog(L"ripple update count") 在上游 #ifdef 内，二进制全二进 0 字符串匹配（已编掉），不复刻。
- **构建**：cmake preset exit=0 + web/debug 全量 exit=0（亲自跑 emscripten）。ripple.cpp 加入 krkr2plugins_ncbind 库 + extrans.dll COMPILE_DEFINITIONS。wasmtime guest 不消费 extrans（grep 三重确认），无 CI 隐患。

## 6. 待办（拓扑序，叶子优先）

- [x] 脚手架：`cpp/plugins/extrans/`（common.h + Main.cpp 注册 + CMake 接入 ncbind 库）✅ 构建通过
- [x] wave（样板，已全取证）✅ auditor 完全对齐 + 构建通过；GPU 路径含 shader 位移不退化
- [x] mosaic（`sub_7C4438` 系）✅ auditor 完全对齐 + 构建通过；详见 §9
- [x] turn（`sub_7CBB5C` 系 + turntrans_table[64][64]）✅ auditor 完全对齐(零偏差) + web 全量构建通过；详见 §10。表逐字复制上游+字节核对吻合 dword_14E4CD4/dword_14E4BD4
- [x] rotate 系（`sub_7C9344` 系，rotatebase + rotatetrans，3 名字 rotatezoom/rotatevanish/rotateswap 共享 ZoomHandler+SwapHandler）✅ auditor 完全对齐(零偏差) + web 全量构建通过；详见 §11。**关键证伪：二进制用精确 π 非上游笔误；rotateswap bgcolor 无 R/B swizzle；DrawData=new[Height] 动态容器复刻；GPU 双路径 "FillARGB"+"Copy" OperateTriangles**
- [x] ripple（`sub_7C7490` 系，仓库 52KB 最大）✅ auditor 单偏差(能力门 f/b 映射接反)修复后零偏差 + web/debug 全量构建通过；详见 §12。**关键证伪：能力门 nullsub forward←216/218、backward←217/219（首份分析报告写反，auditor 反汇编字面纠正）；GPU shader 名字面 "MosaicTrans"（作者复制痕，非 ripple）；phase GPU -1/2π vs 软件 +1/2π；运行期建表非静态；无颜色 swizzle；4 槽 LRU cache**
- [x] 模块级集成审计 + 构建验证 ✅ 5 效果注册链(Wave→Mosaic→Turn→Rotate→Ripple)补完整对应 sub_7C2ACC；web/debug 全量 exit=0；wasmtime guest 不消费 extrans 无 CI 隐患

**extrans.dll 移植全部完成**（7 个转场名 wave/mosaic/turn/rotatezoom/rotatevanish/rotateswap/ripple 全 100% 取证 + 零偏差 + 构建通过）。

## 13. 加载触发机制（集成裁定：按需加载，忠实 libkrkr2.so）

**核心结论：extrans 经游戏 `Plugins.link("extrans.dll")` 按需加载，不在启动自动加载。这与 libkrkr2.so 一致，不需改 `TVPLoadInternalPlugins`。**

证据：libkrkr2.so 的 `TVPLoadInternalPlugins` = `sub_548924`，反编译显示它只做
`sub_548ACC(0/1/2)`（= ncbAutoRegister `AllRegist` 三条 line，把所有 builtin **索引**进
`_internal_plugins[name]`）+ 显式 `LoadModule("xp3filter.dll")`。**extrans 不在启动 LoadModule 列表**。

项目侧机制平行（`cpp/core/plugin/ncbind.hpp:2114` `AllRegist` 仅索引、`LoadModule` 才跑
`Regist()`→PreRegist 回调）。当前实现链路：
1. extrans 各 .cpp 在 `krkr2plugins_ncbind`（force_load）→ 静态 `ncbAutoRegister` 构造器把
   "extrans.dll" 注册进 `_top[PreRegist]` 链（不被 dead-strip）。
2. `TVPLoadInternalPlugins`→`AllRegist()` 把 "extrans.dll" 索引进 `_internal_plugins`。
3. 游戏 `Plugins.link("extrans.dll")`→`TVPLoadPlugin`→`TVPLoadInternalPlugin`→
   `LoadModule("extrans.dll")`→`InitPlugin_Extrans`（= sub_7C2ACC）→ 7 个 provider 注册。

→ **把 extrans 加进启动 LoadModule 列表反而会偏离 libkrkr2.so**（它只启动加载 xp3filter）。
标准 kirikiri 游戏用 ripple/turn/rotate 等 extrans 转场时本就会 `Plugins.link("extrans.dll")`，
该路径已就绪。Main.cpp 的 `NCB_MODULE_NAME` 改用 `#ifndef` 守卫（CMake COMPILE_DEFINITIONS
提供，消除重定义警告）。

## 7. 验证缺口（按项目规约标注，不从零造 fixture）

无转场 differential fixture / oracle。各效果按反编译证据忠实复刻 + 构建通过即可；
运行期可在加载游戏中触发对应 trans（`trans` KAG 标签 method=wave/mosaic/...）做尽力人工验证。
缺现成物料处不新增自动化测试。

## 8. 上游源码本地副本

`/tmp/extrans_src/`（Main.cpp / common.h / wave / mosaic / turn / rotatebase / rotatetrans /
ripple / turntrans_table.h / maketable/mkturntranstable.pl）。turntrans_table.cpp（417KB 生成数据）
按需单独拉取。
