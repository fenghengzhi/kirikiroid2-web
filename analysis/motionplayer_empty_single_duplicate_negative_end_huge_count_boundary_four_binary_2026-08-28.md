# MotionPlayer 空 / 单元素 / 重复 / 负索引 / 末端 / 超大 count 四参考横向审计

日期：2026-08-28  
原始任务：`MP-B02`

## 1. 结论

四个参考二进制没有跨容器统一的bounds/dedup策略。横向审计后的共同规则是：

- 空输入在脚本查询、find/erase miss和多数普通循环中安全；但fixed-shape/trusted-content消费者仍可
  无条件读取element 0、最前16点或`pieces[1]/[2]`；
- 单元素通常走正常容器语义，不自动提升为scalar或特殊对象；某些算法把末元素当next-frame
  sentinel，故有效遍历范围是`[0,size-1)`；
- 重复项按容器角色分别保留全部、unique-key覆盖mapped value、collapse insertion、删第一个、删全部
  或重复执行callback/render；
- 负索引可能先保持signed、先wrap为uint32、再转size_t，或由TJS numeric lookup返回失败；不能用一条
  `index < 0` guard统一；
- one-past-end在checked TJS/`at`路径抛/失败，在raw `operator[]`、deque addressing和fixed array路径是
  trusted UB/fault边界；
- 超大count遵循32-bit arithmetic、target STL `max_size/length_error/bad_alloc`、byte narrowing和
  platform allocation strategy；不隐式clamp到“合理值”。

本地container类型、loop范围、wrap宽度、duplicate disposition和allocation/publication顺序已经匹配
四端。本轮未发现需要修改production语义的差异。

## 2. 与全容器分母的关系

既有`MP-C15`已建立完整container-role denominator：Player/node、四张HM map、parameter
vector/multimap、pending events、十类controller deque、timeline map/vectors、ResourceManager maps/set、
SourceCache list、D3D maps/list/vectors、TJS Array deque、SeparateLayer trees和global cache。

本任务不是把`MP-C15`机械映射后结束，而是按用户指定的六类输入重新审计每个archetype：

```text
empty → single → duplicate → negative → end/one-past-end → huge count
```

具体STL物理布局由`MP-C16`分类：Android reference使用old libstdc++，iOS reference使用libc++，Web
使用目标toolchain的portable STL。source-level元素序列、owner和提交点是共同合同；deque block bytes、
tree header和capacity-only失败变化不是要硬编码进C++对象的字段。

## 3. 本轮 fresh 四端证据

本轮使用原生`mcp__idalib__*`对60个独立函数范围重新执行decompile、完整disassembly和
`xrefs_to`。所有disassembly均为`truncated=false`，所有decompile无error。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | IDB 更新 |
|---|---:|---:|---:|---|
| Android arm64 | 15 | 6,948 | 101 | 15条任务注释、1个书签 |
| Android armv7 | 15 | 3,568 | 36 | 15条任务注释、1个书签 |
| iOS arm64 | 15 | 3,282 | 96 | 15条任务注释、1个书签 |
| iOS armv7 | 15 | 4,515 | 93 | 15条任务注释、1个书签 |
| 合计 | 60 | 18,313 | 326 | 60条注释、4个书签；四库原位保存 |

## 4. 15类archetype四端根

| archetype | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| node deque append | `0x6B1E4C`，397 | `0x5818B0`，230 | `0x100109328`，182 | `0x106BDC`，268 |
| parameter duplicate range | `0x6C1A48`，490 | `0x58C4D8`，231 | `0x100116410`，188 | `0x113D54`，291 |
| event live vector | `0x6C1870`，118 | `0x58C3A8`，90 | `0x10011622C`，97 | `0x113B64`，145 |
| selector publication | `0x66ACDC`，593 | `0x557E04`，331 | `0x1001AA030`，412 | `0x1A96D8`，626 |
| timeline first/all erase | `0x679680`，38 | `0x55F6E4`，28 | `0x1001B341C`，28 | `0x1B2F70`，26 |
| layer-id set suffix erase | `0x6A8B30`，51 | `0x57C2C8`，49 | `0x100102DB8`，52 | `0x10028A`，47 |
| SourceCache list trim | `0x6A3EE0`，55 | `0x57A106`，37 | `0x1000FFA1C`，39 | `0xFCCD2`，37 |
| SeparateLayer tree clear | `0x6C46C4`，87 | `0x58E174`，109 | `0x10011844C`，130 | `0x116280`，190 |
| listener duplicate remove | `0x5311C8`，25 | `0x4952AC`，24 | `0x100233720`，11 | `0x23259A`，9 |
| TJS Array deque grow | `0x6DFC90`，47 | `0x5A099C`，40 | `0x1000FAED8`，47 | `0xF7F90`，54 |
| prepared raw priority index | `0x6BF714`，1,507 | `0x58B178`，944 | `0x1001148F8`，820 | `0x1123D8`，1,034 |
| resource split/result Array | `0x6A72B4`，791 | `0x57B9F8`，262 | `0x100101E84`，255 | `0xFF11C`，396 |
| mesh division/index/count | `0x69AFE4`，1,829 | `0x575800`，871 | `0x1000F974C`，787 | `0xF685C`，1,035 |
| global basis count/cache | `0x69DE30`，167 | `0x576C7C`，142 | `0x1000FB4A8`，107 | `0xF854C`，124 |
| geometry point Array | range `0x6A264C`，753 | `0x579258`，180 | `0x1000FE804`，127 | `0xFB868`，233 |

Android arm64最后一项是编译器合并的多个Bezier callback range；本轮保留已有合法range，不人为切成
重叠函数。

## 5. 空容器/空输入矩阵

| family | empty行为 |
|---|---|
| constructor-created Player node deque | 合法最小状态含一个synthetic root；普通non-root循环0次；直接私有消费者仍信任root存在 |
| fresh TJS Array getters | 返回独立owning empty Array，不返回Void、不复用前一次Array |
| Dictionary/map find | find miss按call site返回Void/false/default；`at` stale key抛；`operator[]`可物化default mapped value |
| parameter multimap range | `[end,end)`，不修改任何entry |
| pending events | 立即返回，不AddRef dispatch；这使Engine传null receiver在empty时安全 |
| controller queues | 多数step no-op并保持current；不同controller state byte不统一重置 |
| timeline label | empty stop label清全部active；empty query label可能表示“是否任一active” |
| layer-id set/list/tree | empty erase/clear/trim正常no-op；回调不触发 |
| prepared main/aux | motion tag为Void时prepare false；tag存在但无item仍true并stable-sort empty |
| mesh selected cells | empty最终Release source并false return |
| mesh bounds points | **不安全**：公共mesh helper无条件读取`boundsPoints[0]` |
| calcBounds point choice | composite empty且transformed empty才fallback四corners；transformed非空则固定读16点 |
| Bezier basis division -1 | mapped vector可以empty；division 0产生一行并保留0/0 NaN权重 |
| Resource path pieces | unknown/empty prefix可早Void；进入motion route后`pieces[1]/[2]`仍unchecked |

“空查询返回empty Array”不能推广到raw render/PSB结构消费者；后者保留crash/UB边界。

## 6. 单元素和末元素sentinel

单元素容器仍保留容器identity：

- one-item TJS Array仍是Array，typed getter按index 0读取；
- one prepared item stable-sort不需要comparator交换，item仍是node-owned persistent对象；
- one SourceCache/listener/tree node按普通erase/clear释放；
- one timeline active label按loop/flags决定保留或erase；
- one selector option可成为index 0 target，仍通过完整controller publication；
- one mesh cell必须有`(divX+1)*(divY+1)`点，不能因cellCount=1只提供一个point。

部分timeline/frame vector把最后元素当`next-time` sentinel，实际扫描`[0,size-1)`；size 0/1时主体可
零次。其他vector遍历完整`[begin,end)`。不能抽取一个“所有frame vector跳过last”的helper。

## 7. duplicate disposition矩阵

| 容器角色 | duplicate结果 |
|---|---|
| unique map/unordered map resolver | later write覆盖mapped locator/value；最初key backing通常保留；旧deque owner不删除 |
| multimap parameter ramps | 每个duplicate独立node；`equal_range`全部消费，析构purge全部exact pointer |
| set / layer IDs | equality-equal插入折叠；suffix erase从唯一hit开始 |
| vectors/deques of events/items/controllers | duplicate按物理顺序全部保留，可重复callback、bind、render或mask |
| prepared priority indexes | duplicate index重复选择同一node、重复清/填item，可能重复向list发布pointer |
| timeline active labels | malformed duplicates保留；named stop只erase第一个；empty stop清全部 |
| listener list | duplicate保留并重复notify；remove删除**所有**相等payload |
| node label/resolver map | 后写index覆盖；旧node仍在deque并最终正常析构 |
| SourceCache identity | same key/color直接hit；color不同先push-front copy再erase old，不留两个persistent nodes |
| SeparateLayer active/retired maps | ordinal key各树unique，pass间move/reuse；不长期双own同一node |
| basis cache | division keyunique；hit复用table，miss先发布一个mapped vector |

所以禁止在input parse阶段统一dedup；它会改变resolver覆盖、owner数量、callback次数和render顺序。

## 8. 负索引的四种宽度路径

### 8.1 先uint32 wrap

prepared builder从priority Array取得Integer，先转32-bit word再`+1`。raw `-1`变
`0xFFFFFFFF + 1 == 0`，可选中synthetic root；其他负数wrap后进入unchecked deque addressing。

frame cursor、selector index、layer IDs和某些division也先做32-bit word算术。不能在wrap之前加signed
negative rejection。

### 8.2 signed比较后跳过

很多count loop是`for(int i=0; i<count; ++i)`；negative count等价0次。mesh division则每轴只有
`>=0`才生成row/column vector，但cellCount仍可由signed multiply形成负word，随后转size_t reserve。

### 8.3 转size_t后成为巨大值

negative frame/index若在比较前转size_t，会成为巨大无符号数并跳过/越界，具体由源表达式决定。
Resource path和fixed vector `operator[]`没有恢复性exception。

### 8.4 TJS numeric/property lookup

script-visible Array/manager getter可把negative index交给TJS PropGet/checked wrapper并得到失败或异常；
这些public边界不能用来证明内部raw index安全。

## 9. 末端与one-past-end

- STL `find==end`、`equal_range(end,end)`和`erase(end,end)`是安全no-op；
- `vector::at`或TJS property lookup可失败/抛；
- raw `vector[index]`、deque block address、fixed Array和PSB node lookup不判`index == size`；
- `end` iterator只能比较/作为range sentinel，不能解引用；
- event dispatcher保留raw cursor并每轮重读live end，callback append不realloc时新tail可被同轮消费；
  realloc后旧cursor和old end同时失效，原版不修复；
- listener live list callback自删current/future node同样保留iterator invalidation边界；
- nested motion、particle child、prepared ancestor/source vectors信任已建拓扑，不增加one-past gate。

## 10. 超大count、overflow和allocation

### 10.1 Bezier/basis

`division + 1`先做signed int addition再转vector size。`INT_MAX + 1`是native signed-overflow/target
compiler边界；negative division可产生empty table；0 division产生一行NaN basis。cache key在resize/
fill前已经发布，异常留下empty/partial mapped vector。

### 10.2 mesh

`cellCount = int32_word(divisionX * divisionY)`，再转size_t传`selected.reserve`。负/overflow product可
成为巨大reserve并抛length_error/bad_alloc。point indices用`divisionX + 1`和row-major signed算术；
过短mesh vector直接越界。selected size乘6用于两个vertex vector reserve/append，也保留size/word边界。

### 10.3 TJS Array/deque和普通vectors

目标STL在超过`max_size`时抛length_error，在allocation失败时抛bad_alloc。Android libstdc++ deque可
先成功recenter/replace map、再因element block失败，仅留下capacity/map内部变化；iOS libc++先在
temporary split-buffer staging，成功后才swap live map。portable source不伪造两套内部header。

### 10.4 count narrowing

- particle/Array count可窄到signed int；超范围按目标conversion profile；
- stencil count保持int增长，但写8-bit ref在256处wrap为0，overflow提示只一次；
- opacity/color/flags按各自byte/word宽度截断；
- typed NCB的`numparams`是signed：negative内部count走家族特定BADPARAMCOUNT/clear边界，ordinary
  nonnegative surplus通常不转换。

## 11. erase/clear与capacity

- vector clear销毁elements但通常保留capacity；deque/list/tree clear按元素物理顺序销毁owner；
- timeline named erase只删一个duplicate，listener `remove`删全部；
- layer-id `release(id)`从hit到end删suffix，miss不动，release 0若sentinel存在可清整suffix；
- SourceCache color-change hit是copy-then-erase，不是splice；新owner先AddRef；
- SeparateLayer invalidate callback先于node clear，异常留下invalidated prefix和unprocessed tail；
- parameter vector销毁前先从multimap按exact borrowed pointer purge，避免留下dangling ramp；
- main/aux pointer vectors clear不deletepersistent PreparedRenderItem。

capacity/bucket/block是否保留只在allocation/reentry/observable API能看到时记录；普通script getter不暴露
STL capacity。

## 12. callback/reentry影响

本任务只按index/count角度记录reentry：

- event/listener live traversal可因append/erase改变本轮end和pointer validity；
- property getter可在count读取后替换Array，部分call site先retain dispatch snapshot，部分继续用live
  persistent field；
- SourceCache/SLA invalidation回调可改变list/tree，但reference不统一snapshot；
- prepared/resource builder的dynamic getter可在index计算后reload/clear owner，raw borrow可能失效。

完整callback owner/reentry审计由`MP-B07`；这里确认不能用“先复制整个container再遍历”修复
negative/end/duplicate问题。

## 13. 本地实现与测试资产

主要实现位置：

- `cpp/plugins/motionplayer/NodeTree.cpp:245`：append-before-read node；
- `cpp/plugins/motionplayer/PlayerVariable.cpp:176`：duplicate multimap和purge；
- `cpp/plugins/motionplayer/PlayerFrameProgress.cpp:1240`：live event vector；
- `cpp/plugins/motionplayer/EmoteEngine.cpp:2955`：timeline clear-vs-first erase；
- `cpp/plugins/motionplayer/ResourceManager.cpp:513`：layer-id set suffix erase；
- `cpp/plugins/motionplayer/SourceCache.cpp:815`：list trim；
- `cpp/plugins/motionplayer/PlayerRenderItems.cpp:442`：priority wrap/duplicate builder；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:194`：basis key/count；
- `cpp/plugins/motionplayer/MotionRenderBackend.cpp:507`：mesh division/index/reserve；
- `cpp/plugins/motionplayer/RuntimeSupport.cpp:449`：TJS Array Items owner。

既有unit资产覆盖fresh empty Arrays、duplicate resolver/controller/listener/prepared indices、negative public
indexes、priority `-1` wrap、timeline first/all erase、zero/negative/overflow division、empty selected cells、
TJS deque grow owner和one-past/malformed shape boundaries。本轮没有发现新的语义缺口，不新增重复测试。

## 14. 验证状态

本轮完成18,313条完整指令、326个`xrefs_to`、60条任务注释、4个书签和四库保存。结果与既有
`MP-C15-ALL-CONTAINER-BOUNDARY-AUDIT`及`MP-C16-STL-CONTAINER-SOURCE-ATTRIBUTION`相容。

coverage与163-ticket映射随后重生成并执行严格列数、重复ID和`git diff --check`检查。正式native
unit、Web Debug、allocator fault injection和malformed-count runtime执行归`MP-V`；静态闭合不伪称
这些运行通过。

`MP-B02`没有剩余task-local静态差异。
