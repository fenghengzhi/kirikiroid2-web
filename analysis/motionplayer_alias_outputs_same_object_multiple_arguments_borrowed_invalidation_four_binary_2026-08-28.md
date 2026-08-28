# MotionPlayer alias 输出、重复对象参数与 borrowed reference 失效四参考横向审计

日期：2026-08-28  
原始任务：`MP-B08`

## 1. 结论

四个参考二进制允许多种彼此不同的alias，不能用统一的deep-copy、self-assignment guard或dedupe
处理：

1. storage alias：两个公开属性名访问同一persistent Variant slot；
2. closure alias：Object与ObjThis指向同一dispatch，但各自拥有一份独立引用；
3. argument alias：同一个Variant/object可同时作为owner、parent、source、target、receiver、objthis
   或多个argv元素；
4. result/output alias：dispatch result out-param可直接指向persistent field，native output pointers也可
   指向同一scalar/state field；
5. container alias：多个parent/child/stencil entry可重复引用同一persistent command Variant；
6. borrowed subobject alias：native Array Items、PSB interior node、PreparedRenderItem sourceState和selected
   parameter pointer只借用其owner/container内部地址。

不同owner类型的self-assignment规则也不同：`tTJSVariant` copy assignment先retain新closure并有self
guard；`PSBFile` holder则按Release-old→copy pointer→AddRef且无self guard，最后owner的自赋值可先删掉
pointee。SeparateLayer assign明确允许`source == target`且入口tree swap会改变随后遍历的source。

现有production实现已经保持这些非统一规则。本轮没有发现task-local语义偏差，也不新增production
guard或copy。

## 2. 本轮 fresh 四端证据

本轮用原生`mcp__idalib__*`对64个alias代表范围重新执行decompile、完整disassembly、strings/
constants/callees和`xrefs_to/from`。所有decompile成功，所有disassembly均未截断。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | `xrefs_from` | IDB 更新 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 16 | 6,499 | 178 | 16 | 16条任务注释、1个书签 |
| Android armv7 | 16 | 3,834 | 166 | 16 | 16条任务注释、1个书签 |
| iOS arm64 | 16 | 3,190 | 176 | 16 | 16条任务注释、1个书签 |
| iOS armv7 | 16 | 4,897 | 168 | 16 | 16条任务注释、1个书签 |
| 合计 | 64 | 18,420 | 688 | 64 | 64条注释、4个书签；四库原位保存 |

共享PSB raw getter、Array factory和Layer texture helper的xref数较大；它们用来证明alias primitive被
多个上层consumer复用，不把xref数量冒充motionplayer公开API数量。

## 3. 四端函数映射

| alias范围 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| SeparateLayer assign | secondary `0x6A97F0`，完整assign段537；resolved合并range 729 | `0x57C814`，288 | `0x10010347C`，247 | `0x100874`，381 |
| PSB raw strict getter/holder publication | `0x599038`，63 | `0x4DD49C`，61 | `0x1000EDA48`，42 | `0xE9D10`，82 |
| simple spring output | `0x65FB48`，128 | `0x551910`，139 | `0x1001A1A8C`，127 | `0x1A0BE0`，159 |
| chain/post-bend output | `0x665D84`，289 | `0x555010`，267 | `0x1001A5BDC`，277 | `0x1A51CC`，302 |
| Player isExistMotion argv alias | `0x6CDBD4`，172 | `0x5942F4`，91 | `0x10011F558`，80 | `0x11E054`，133 |
| findSource persistent result alias | `0x691CC8`，1,191 | `0x570500`，676 | `0x1000F316C`，586 | `0xEF97C`，952 |
| getCommandList closure aliases | `0x6D0E2C`，1,315 | `0x595FF0`，838 | `0x100121EB0`，596 | `0x120CF8`，1,032 |
| calcView shared separator/Items | `0x6CE908`，1,349 | `0x594958`，798 | `0x1001201CC`，613 | `0x11EED4`，977 |
| targetLayer Variant setter | `0x6EC600`，74 | `0x5AA8EC`，56 | `0x10013DC58`，62 | `0x13E812`，44 |
| shared Layer factory | inline site `0x6CB660` in 398 body | `0x57AC1C`，61 | `0x1001008A8`，58 | `0xFDA14`，104 |
| SourceCache bufLayer getter | `0x6A58DC`，3 | `0x57B060`，5 | `0x100100F84`，3 | `0xFE11A`，5 |
| TJS Array + borrowed Items factory | `0x702098`，63 | `0x5BAA70`，60 | `0x10029FF58`，49 | `0x2A4A80`，91 |
| D3D captureCanvas source/target | `0x6AAD0C`，111 | `0x57CF94`，107 | `0x100103DBC`，115 | `0x10116E`，109 |
| Player draw target identity | `0x6D3398`，371 | `0x597864`，293 | `0x100123C84`，270 | `0x122F28`，423 |
| motionKey/project shared slot setter | internal `0x67C6C0` in 169 body | `0x560FD4`，36 | `0x1001B4F68`，23 | `0x1B4B38`，53 |
| Layer AssignTexture identity branch | `0x8071A0`，74 | `0x6308A8`，58 | `0x10007A164`，42 | `0x772FC`，50 |

Android arm64 SeparateLayer assign是clear-rooted合并range中的secondary entry；motionKey setter也是
containing-function internal entry。报告使用真实入口和完整resolved body，不创建重叠IDA函数。

## 4. Variant assignment与shared slot alias

motionKey/project是同一persistent Variant的两个脚本名字；outline/meshline/targetLayer也使用普通完整
Variant copy assignment。共同语义：

```text
copy_assign(dst, src):
    if &dst == &src: return
    retain src.Object
    retain src.ObjThis
    or retain src String/Octet owner
    release old dst content
    publish copied payload/type
```

因此getter产生的owning alias再通过另一个属性名写回同一slot不会提前删除dispatch。setter的by-value
参数还拥有自己的引用；old owner最后一次Release可同步重入，但new owner已经retain。

这条规则不能外推到所有holder。PSBFile自赋值故意没有self guard，见下一节。

## 5. PSB holder与interior node alias

PSBRawNode由`PSBFile file_` owner holder加`node_` interior pointer组成。holder assignment顺序是：

```text
release(dst.owner)
dst.owner = src.owner
addref(dst.owner)
```

没有self-assignment test。当src/dst是同一最后owner holder时，Release可删除owner/data，随后copy/AddRef
使用悬空地址。非throwing dictionary getter先算child，再赋值`output.file_`，最后写`output.node_`；
`output`与source raw node alias时保留同一release-before-retain风险。

strict getter的fresh四端owner构造/cleanup与D05的70-range全生命周期审计共同证明该primitive；本地
`PSBRawFile.h`刻意保留无self guard，不能改成copy-and-swap。

## 6. native scalar output alias

simple spring solver先写X output，再读取Y路径的`biasY/leverY`等live state。若outX指针alias这些state
field，第一store会改变稍后的Y结果；reference没有把全部inputs预读到locals。

chain post-bend按`seg1 += bend`后`seg0 -= bend`写回。两个output pointer相同时，第二store消费第一次
更新后的同一slot，最终结果由顺序决定。已有unit alias test锁定该行为；不能用两个temporary后同时
assign，因为那会消除alias观察点。

## 7. TJS argv/result与persistent field alias

`Player::isExistMotion`把`&_findMotionContextVariant`本身作为argv0传给dispatch，不先构造copy。
callee可在调用中直接替换persistent context slot；argv1 path和result是独立Variants。callback status被
忽略，result总是转bool。

findSourceForNode则把`dst.object`直接作为findSource result out-param。callee可写该persistent field，
即使最终status非0；caller随后按`status==0 && object!=Void`决定valid。成功后accessor从`dst.object`
取得独立Object-only owner，因此后续getter重入替换persistent slot不会重定向当前receiver。

result slot与argument storage可能在恶意直接ABI调用中同址；reference raw/typed wrappers只保留各自
明确的Clear/conversion顺序，不额外检测range overlap。

## 8. Object/ObjThis双alias

TJS Array、Dictionary、Layer和PSB facade通常把同一dispatch同时保存为Object与ObjThis。两者即使地址
相同也各AddRef一次、各Release一次；这不是重复释放bug。

shared Layer factory直接把caller的owner/parent Variant地址放入`argv[2]`，不copy或Object-convert；
owner与parent可同一个Variant，或两个closure可指向同一dispatch。factory结果又把created作为
Object/ObjThis self-closure。参数identity没有dedupe，也不把两owner压成一份ref。

Player.draw的两个target参数各自strict conversion；它们可解析到同一dispatch/native Layer，router仍按
参数角色执行，不因identity相同跳过prepare/render/post。

## 9. Array Items与container alias

`createTJSArrayWithItems_guess`返回owning Array closure，同时暴露native Items deque pointer。Items是
borrowed，不单独AddRef Array；只有closure owner活着时才有效。把Items存到比Array Variant更长的对象
会悬空。

calcView创建一个separator Dictionary/Array dispatch，并以同一Object/ObjThis closure复用于多个
separator entry。getCommandList第一遍重建每个persistent command Variant，第二遍才构建parent chain：

- non-composite parent的mesh直接CopyRef parent command closure；
- composite parent的mesh Array按childItems原始顺序CopyRef，每个duplicate pointer都保留；
- output Array再CopyRef同一persistent command closure。

这些是有意的identity graph，不是应deep clone的树。

## 10. SeparateLayer source==target

assign入口先swap target active/retired tree，然后遍历`source._activeLayers`。当`&source == this`时，
source active已经因swap变成入口前的retired tree；原active则成为target retired。reference没有：

- `if(this == &source) return`；
- source tree snapshot；
- copy entire adaptor后再assign。

resolver可从target retired复用原active nodes，同时遍历swap后的source active。结果由真实tree identity
和callback mutation决定。现有实现保持同一swap-before-loop结构。

## 11. source==target texture/Layer

D3D captureCanvas GPU路径先从Layer旧texture选择candidate并AddRef，再把adaptor target交给Layer，
然后Release adaptor原target并发布candidate/new texture。source Layer、target Layer和texture identity
可以相同。

`AssignTexture`即使发现传入texture已经安装，也仍执行image size、dirty、clip reset和Layer update副作用；
identity fast path不是整个函数no-op。为了“避免self assignment”提前return会漏掉这些副作用。

candidate AddRef、Layer接管、old target Release和replacement publication的顺序保证正常identity路径的
引用平衡；中途throw的额外ref泄漏属于B06已记录边界。

## 12. persistent外部alias与lifetime

SourceCache `bufLayer` getter、Player tags/resourceManager/outline/motionKey、Layer debug fields等都返回
ordinary Variant CopyRef。外部alias可越过下一次setter、cache clear甚至owner对象析构继续保活dispatch。

但alias只保活dispatch/closure，不自动保活其native borrowed view或container interior。例如D3DLayer
script shell可继续存在而native borrowed Player/Layer pointer已悬空；Array closure可保活Items，单独
缓存Items pointer则不行。

## 13. borrowed reference失效矩阵

| borrowed alias | owner | 稳定条件 | 失效条件 |
|---|---|---|---|
| TJS Array Items | Array dispatch/closure | closure仍live且native class未被替换 | Array invalidation/destruction/native slot replacement |
| PSB node interior | PSBRawOwner via PSBFile | 任一holder/dispatch/ObjSource仍retain owner | 最后owner Release；holder self-alias可提前触发 |
| selected parameter pointer | Player parameter vector | vector不clear/reallocate | playback rebuild、clear或growth reallocation |
| PreparedRenderItem sourceState | owning MotionNode | node deque element仍live | tree clear/erase；owner closure不等于node owner |
| command child/parent pointer | persistent item storage | item未destroy/rebuild | prepared tree reset/reentrant clear |
| D3D borrowed native view | external native owner | pointee先于shell存活 | pointee destructor；shell alias不retain pointee |

“有一个Variant owner”只证明dispatch活着，不能据此证明其内部native/container borrow有效。

## 14. 本地与测试对照

本地关键对应包括`PSBRawFile.h`、`SeparateLayerAdaptor.cpp`、`EmoteSpring.cpp`、
`PlayerResource.cpp`、`PlayerLayerQuery.cpp`、`RuntimeSupport.cpp`、`SourceCache.cpp`、
`D3DAdaptor.cpp`、`PlayerDrawDispatch.cpp`和core `tjsVariant.cpp`。

现有tests已覆盖spring output alias、motionKey/project getter-produced self alias、targetLayer arbitrary
Variant owner、getCommandList persistent/duplicate aliases、bufLayer外部alias、source==target texture、
Array Object/ObjThis refs和ResourceManager aliased argv。PSB最后owner self-assignment属于故意的UAF尖锐
边界，不制造会执行未定义行为的portable unit expectation。

## 15. 最终判定

`MP-B08`没有剩余task-local静态差异。storage/closure/argument/result/container/borrowed alias以及各自
self-assignment和lifetime规则已完成四端映射；64条IDA任务注释、4个书签已写入并保存到四库。

正式native unit、Web Debug、malicious-dispatch overlap和cross-reference alias differential属于`MP-V`。
