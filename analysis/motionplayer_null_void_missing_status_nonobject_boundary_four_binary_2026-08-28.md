# MotionPlayer Null / Void / 缺失属性 / TJS status / 非对象 Variant 四参考横向审计

日期：2026-08-28  
原始任务：`MP-B01`

## 1. 结论

四个参考二进制没有一个统一的“空值或失败”规则。横向审计后，必须保留至少六种彼此不可替换的
状态：

1. `Variant.Type() == tvtVoid`：脚本值sentinel；
2. `Variant.Type() == tvtObject`但Object/ObjThis dispatch为null：typed-null Object；
3. 非Object、非Void的普通Variant：Integer/Real/String/Octet等；
4. raw C++ pointer为null：native instance、Player、Layer、texture、argv元素等；
5. TJS调用返回失败status但result仍为Void；
6. TJS调用返回失败status但callee已经写result或产生副作用。

reference对它们按call site分别处理：有的strict转换并抛异常，有的把native-instance失败当普通miss，
有的只检查exact Void，有的允许任意Variant原样保存，有的以`MEMBERMUSTEXIST` status决定分支，有的
完全忽略status并继续转换result。把这些路径统一成`if (!value) return`、`optional`或“所有TJS失败都
抛”都会偏离原版。

本地316/316条NCB公开候选已有四端注册证据；本轮又对动态property/call/conversion边界做第二遍
交叉分类。没有发现production语义差异，现有strict/borrowed conversion、exact-Void gate、status
branch和result默认值均与联合证据一致。

## 2. 审计分母

公开脚本面分母来自确定性NCB ledger：

| 项目 | 数量 |
|---|---:|
| NCB候选 | 316 |
| `EVIDENCED_4_4` | 316 |
| method / raw method / detail method | 126 |
| property / read-only property | 142 |
| constructor / factory | 12 |
| constants / subclass rows | 36 |

本地motionplayer源码的动态边界词法复核另得到：86处`PropGet`、20处`PropSet`、57处`FuncCall`、
28处owning `AsObject`、67处borrowed `AsObjectNoAddRef`以及154处显式Variant type/TJS status/
`MEMBERMUSTEXIST` gate。这些词法数字用于防漏检，不把helper封装下的多次调用冒充新的公开分母。

纵向body证据由Player、EmotePlayer、ResourceManager、SourceCache/ObjSource、D3D/SLA、geometry、
LayerGetter和Layer extension报告闭合；本轮横向审计检查同一种失败输入是否在不同纵切面被错误
“安全化”。

## 3. 本轮 fresh 四端证据

本轮使用原生`mcp__idalib__*`对40个代表全部边界家族的独立函数范围重新执行decompile、完整
disassembly和`xrefs_to`。所有disassembly均为`truncated=false`，所有decompile无error。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 10 | 3,177 | 24 | 10条任务注释、1个书签 |
| Android armv7 | 10 | 1,676 | 7 | 10条任务注释、1个书签 |
| iOS arm64 | 10 | 1,462 | 14 | 10条任务注释、1个书签 |
| iOS armv7 | 10 | 2,295 | 9 | 10条任务注释、1个书签 |
| 合计 | 40 | 8,610 | 54 | 40条注释、4个书签；四库原位保存 |

| 边界代表 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| SLA typed ctor / Void sentinel | `0x6EBECC`，45 | `0x5AA1C8`，48 | `0x10013D3E8`，35 | `0x13DE14`，35 |
| SLA arbitrary-Variant property | `0x6EC600`，74 | `0x5AA8EC`，56 | `0x10013DC58`，62 | `0x13E812`，44 |
| strict draw target conversion | `0x6D3398`，371 | `0x597864`，293 | `0x100123C84`，270 | `0x122F28`，423 |
| playback status/Void/missing | `0x6AF664`，459 | `0x580158`，281 | `0x100107540`，236 | `0x104AE8`，386 |
| parameter presence probe | `0x6AEAF8`，230 | `0x57FA14`，152 | `0x100106D00`，116 | `0x104168`，217 |
| ignored-status random | `0x6A894C`，73 | `0x57C1CC`，48 | `0x100102C90`，37 | `0x1000F0`，72 |
| exact-Void one-shot Layer gate | `0x6CB57C`，398 | `0x592F7C`，212 | `0x10011E2BC`，178 | `0x11CAC8`，298 |
| findMotion Void/non-String/status | `0x6A72B4`，791 | `0x57B9F8`，262 | `0x100101E84`，255 | `0xFF11C`，396 |
| findSource Void/strict nested access | `0x6A7F1C`，646 | `0x57BDE0`，262 | `0x100102594`，227 | `0xFF890`，374 |
| raw callback receiver/result split | `0x6CFE78`，90 | `0x595598`，62 | `0x100121204`，46 | `0x11FFB4`，50 |

## 4. “Null”必须先说明是哪一种null

### 4.1 null objthis

NCB method/property外层通常先检查member-name和receiver。`objthis == nullptr`返回
`TJS_E_NATIVECLASSCRASH`；在许多descriptor里它发生在result Clear之前，所以旧result可保留。
receiver存在后才Clear result、做argc gate和native instance解析。

raw callback自身不一定Clear result。`Player.progress` raw body只解析receiver/argc、转换dt并调用
bridge；“脚本方法对象先Clear”属于外层descriptor，不能把两层合并到raw callback。

### 4.2 typed-null Object

Object-null与Void不同。`Player.draw`对Object-null的两次strict Variant-to-Object转换都成功，两个
class-ID probe普通miss，然后仍构造main/aux并进入prepare；Void/Integer/String则在第一个probe前
抛转换异常。

同一Object-null在需要直接解引用dispatch的call site可崩溃；reference没有全局null policy。

### 4.3 raw native pointer null

native-instance probe可把失败status/null payload分类为普通miss，也可由typed descriptor升级为
`NATIVECLASSCRASH`。一旦private helper已经取得borrowed native pointer，后续通常不再判null：
child Player、source texture、Layer target、prepared item、argv/param元素等保持trusted-pointer边界。

不能因公开入口通常保证nonnull，就在深层所有call site增加silent return；这会改变private malformed
call和callback重入后的行为。

## 5. Void的五种角色

### 5.1 exact-one-Void constructor sentinel

SeparateLayerAdaptor、EmotePlayer/D3DEmotePlayer等NCB生成descriptor在“argc恰好1且唯一参数是Void”
时创建empty adaptor/shell。普通0参数可能是BADPARAMCOUNT；`Void + surplus`离开sentinel并进入正常
arg0 conversion。exact-one-Void不是“缺少参数”的同义词。

### 5.2 dynamic miss返回值

ResourceManager `findMotion`/`findSource`的普通miss返回Void，不返回null Object、空Array或空Dictionary。
`findMotion`成功返回fresh两元素Array；`findSource`成功返回ObjSource/blank Dictionary。

### 5.3 optional field absence

很多motion content字段只以exact Void判缺失：curve、mesh points、parameterize、project key、appearance、
internal Layer等。非Void但类型错误不会自动归为missing，而是进入strict conversion、后续raw use或
“non-Void residue blocks retry”。

### 5.4 one-shot publication gate

internal render Layer materializer只在persistent primary Variant是Void时创建。第一次Layer已经发布后，
work creation/setSize失败可留下`primary != Void && work == Void`；后续立即返回，不修复。Integer、String
或typed-null Object residue同样阻止重试。

### 5.5 successful void method result

typed void method在receiver/argc通过后通常保持result为Void。raw callback直调时result可保持调用前值，
因为Clear属于descriptor。这两种测试必须分层。

## 6. 非对象Variant的三种处理

| call-site合同 | 非对象行为 | 代表 |
|---|---|---|
| strict Object conversion | 抛Variant conversion异常 | Player.draw、source/target Layer accessor、metadata restore |
| type test后分流 | 作为missing/default/no-op或不同route | optional curve/appearance、fill value、project key Void |
| arbitrary Variant storage | 完整CopyRef保存，成功 | `SeparateLayerAdaptor.targetLayer` setter、若干state Variants |

`targetLayer` property刻意接受Integer、String、Void和Object closure，setter只做Variant copy-assign，不
验证Layer class、不更新owner/private/absolute。之后真正消费target的call site才可能抛错。

`findMotion/isExistMotion`的project key只有Void跳过direct lookup；非Void必须已经是String。Integer/
Real/Object/Octet不做格式化，直接抛strict string conversion。

## 7. 缺失属性与`MEMBERMUSTEXIST`

presence probe是一次真实TJS调用，不等于在C++ Dictionary上查key。共同家族有：

1. 先用`TJS_MEMBERMUSTEXIST`；status失败时走missing/default；
2. probe成功后再用flags=0读取；第二次status常被忽略，再对result做转换；
3. 某些probe失败时必须忽略callee恶意写入的result，使用range-derived/default值；
4. 某些字段不用probe，直接PropGet并让Void/default conversion决定结果；
5. nested raw PSB getter不是TJS presence probe，缺失可直接抛/越界。

Parameter `division`是典型：probe失败走range-derived division，即使callee在失败时写了99也忽略；
explicit present的0/negative/NaN/infinity则保留，不重新归为missing。

Layer尺寸helper的missing width/height通常提供Integer 0；“存在但转换失败”传播异常。这与“missing
property必抛”不同。

## 8. TJS status处理矩阵

### 8.1 status决定presence或route

- `MEMBERMUSTEXIST` probe以`TJS_FAILED`/`TJS_SUCCEEDED`决定是否读取/默认；
- native-instance support可把失败当class miss；
- TJS Array native Items取得要求status**精确等于**`TJS_S_OK == 0`，不是任意非负success；
- `Player.findSourceForNode`同样对关键find status做exact-zero判断并结合Void result。

### 8.2 status完全忽略，随后转换result

ResourceManager `random`调用`random()`时忽略FuncCall status。result初始Void：失败且未写result时
`AsReal()`得到0；失败但写了Real时仍转换该Real。没有native RNG fallback或retry。

playback的`onFindMotion`和ResourceManager `findMotion`调用也可忽略普通status；真正route看result是否
Void或strict result shape。普通失败与异常是两条不同路径。

### 8.3 status忽略，因为只关心副作用

大量Layer `setSize/setClip/setPos/PropSet/copy/operate/fill/update/Invalidate`忽略HRESULT。普通非零
status不停止后续publication；C++/TJS抛出的异常才unwind。给这些call site统一加
`if (TJS_FAILED) throw`会改变partial commit和后续调用次数。

### 8.4 status失败后显式default

playback的chara/motion presence getter失败产生empty string；ResourceManager `KAG`/filter/optional
property也有各自default。default是call-site决定，不应放进一个全局PropGet wrapper。

## 9. result槽和恶意dispatch

result在TJS ABI中是caller提供的可写Variant。reference经常先构造Void result，再调用可能失败的
dispatch；callee即使返回失败仍可能写值。因此必须分别记录：

- result何时Clear/构造；
- status是否检查；
- 失败时是否忽略已写值；
- 后续是否strict转换；
- result owner何时析构或发布到persistent字段。

Player playback复用一个hidden result槽跨callback与ResourceManager load。callback可写非Void，下一
阶段是否Clear由精确控制流决定；非Void route还会先提交label，再验证Array/Object内容。这里不能
用`std::optional<Result>`把“status失败”与“Void result”合并。

## 10. 公开NCB边界统一项与例外

316行公开面共同核验了script name、kind、arity、receiver、result和default，但descriptor家族不同：

- typed method/property：required argv先转换为按值owner，surplus不访问；
- raw legacy callback：body自行receiver/argc gate，外层负责member/objthis/result；
- native-instance raw callback：外层先解析ClassID，再进body自己的argc/default；
- typed constructor/factory：exact-one-Void sentinel、ordinary argc和arg conversion顺序各自固定；
- constants/subclasses没有普通call body，不能套method规则。

特别是“result一定先Clear”“wrong receiver一定优先于argc”“Void一定等于缺参”都不是跨316行成立的
命题；现有ledger和真实descriptor tests按家族记录。

## 11. owner、异常和partial commit

普通failed HRESULT通常不触发C++ unwind；已写result/Layer/object状态和后续call都保留。抛异常则只
销毁当前call site已经构造的Variant/string/accessor owner，不撤销之前persistent store。

代表边界：

- internal Layer primary先发布、work后失败，后续Void gate不再重试；
- findMotion fresh Array第一次object emplace后第二次string emplace失败，fresh object未发布给script，
  但内部owner/leak disposition保持目标实现；
- findSource创建raw ObjSource后adaptor attach/Dictionary property发布失败可留下owner前缀；
- draw direct D3D先写sticky，再prepare/render；non-object则在sticky前strict转换失败；
- targetLayer setter CopyRef先acquire新owner，再释放旧owner，self-alias安全；回调重入仍按Variant规则。

更完整的setter/push/publication间异常由`MP-B06`继续做横向时序审计；本任务只确认空值/status/type不会
错误改变这些提交点。

## 12. 本地实现与回归资产

对应实现集中在：

- `cpp/plugins/motionplayer/main.cpp`、`EmotePlayer.cpp`：NCB descriptor/raw callback家族；
- `cpp/plugins/motionplayer/PlayerDrawDispatch.cpp:25`：strict target与Object-null；
- `cpp/plugins/motionplayer/PlayerTimeline.cpp:95`、`PlayerCore.cpp:442`：playback hidden result/Void；
- `cpp/plugins/motionplayer/PlayerVariable.cpp:66`：presence probe/default/conversion；
- `cpp/plugins/motionplayer/ResourceManager.cpp:406`、`:591`、`:666`：Void misses、fresh result、ignored status；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp:455`：exact-Void one-shot publication；
- `cpp/plugins/motionplayer/SeparateLayerAdaptor.h:84`：arbitrary Variant targetLayer；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:468`：Array native exact-zero gate。

既有unit资产覆盖exact-one-Void与surplus、wrong receiver/result Clear、Object-null vs non-object draw、
missing-property malicious result、random failed-status result conversion、playback missing/status/Void、
targetLayer任意Variant、internal Layer partial publication和ResourceManager miss/non-String project。
本轮没有发现新的可执行语义缺口，故不新增重复测试。

## 13. 验证状态

本轮完成8,610条完整指令、54个`xrefs_to`、40条任务注释、4个书签和四库保存。316行NCB ledger
仍为316/316 `EVIDENCED_4_4`，候选ID/四端字段/脚本名/sequence由生成器严格断言。

coverage与163-ticket映射随后重生成并执行严格列数、重复ID和`git diff --check`检查。正式native
unit、Web Debug和恶意dispatch/failure-injection runtime执行归`MP-V`；静态闭合不伪称这些运行通过。

`MP-B01`没有剩余task-local静态差异。
