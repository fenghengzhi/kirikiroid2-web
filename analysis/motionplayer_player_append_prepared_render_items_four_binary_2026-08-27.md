# Player::appendPreparedRenderItems 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

`Player::appendPreparedRenderItems` 是 prepared-render 数据流的核心递归 builder。四端共同源码
并不是“遍历 node 然后 push”这么简单，而是：

1. 先通过进程全局单项 cache 合成递归继承色；
2. 从 retained priority-content dispatch 反向读取 node index；
3. 对选中的 node 先清 `drawnThisFrame`，再按 particle/type-3/ordinary 三条路径递归或填充；
4. 每个 MotionNode 延迟拥有并跨帧复用一个 PreparedRenderItem；
5. ordinary item 完整覆盖到 affine/rect 阶段后才 append mainList；
6. 第一遍结束后按原始 node 顺序做独立 type-12 stencil-composite aux/child post-pass。

本地核心 node 分流、字段发布顺序、共享 list 递归、持久 item owner、mesh stale-field 边、affine
数值顺序、post-pass 和 partial-commit 行为与四端一致。本轮删除了生产 builder 中不属于任何
参考端的 logo/path/snapshot sidecar，并从 portable derived item 移除了只为该 sidecar 服务、会在
mainList commit 前额外分配的 `std::string sourceKey`。

至此 outer `prepareRenderItems` 的内容 gate、递归构造、main-only stable-sort 以及递归 builder
主体全部闭合。PreparedRenderItem 最终随 MotionNode 销毁的完整 deque/element/destructor owner 链
随后由
`analysis/motionplayer_motionnode_prepared_item_deque_lifetime_four_binary_2026-08-27.md`
独立闭合：普通树替换的后缀 erase 销毁非根 item，Player 析构体的显式 nodes.clear 销毁 root
item，最终 deque member destructor只释放空容器的残留存储。

## 2. 四端主体、cleanup 与辅助函数

### 2.1 主体完整指令

| 平台 | builder | body 指令 | CFG blocks |
|---|---:|---:|---:|
| Android arm64 | `0x6BF714` | 1507 | 216 |
| Android armv7 | `0x58B178` | 944 | 约百级 |
| iOS arm64 | `0x1001148F8` | 820 | 约百级 |
| iOS armv7 | `0x1123D8` | 1034 | 139 |

四端均 fresh decompile；合计 4305 条主体指令通过 native IDA MCP pagination 全部读取。
Android arm64 的单次 pseudocode 响应因函数约 55k 字符被服务端截断，结论以完整 1507 条
disassembly、其余三端完整 pseudocode 和四端逐块交叉对齐为依据，没有把截断反编译当完整证据。

iOS armv7 另有 `0x112F0C` 的 111 条 SjLj cleanup dispatcher，覆盖 44 个 call-site selector；
已 fresh decompile 并完整读取。

### 2.2 packed-color helper

| 平台 | helper | 指令 |
|---|---:|---:|
| Android arm64 | builder 内联 | 主体计数内 |
| Android armv7 | `0x58BD1C` | 71 |
| iOS arm64 | `0x1001156D8` | 55 |
| iOS armv7 | `0x113040` | 65 |

### 2.3 lazy item ensure/constructor

| 平台 | helper | 指令 | native item size |
|---|---:|---:|---:|
| Android arm64 | builder 多处内联 | 主体计数内 | `0x1B0` |
| Android armv7 | `0x58BDF0` | 24 | `0x148` |
| iOS arm64 | `0x1001157BC` | 33 | `0x1B0` |
| iOS armv7 | `0x113108` | 29 | `0x148` |

### 2.4 affine rect helper

| 平台 | bounds transform | rounding | 指令 |
|---|---:|---:|---:|
| Android arm64 | builder 内联 | builder 内联 | 主体计数内 |
| Android armv7 | `0x5902B0` | 同一 helper | 119 |
| iOS arm64 | `0x10011A8D8` | `0x100115840` | 68 + 9 |
| iOS armv7 | `0x118BE8` | `0x113160` | 104 + 17 |

这些 helper 均 fresh decompile/full disassembly。函数、关键 inline region、cleanup 和误并到相邻
Bezier cache 名下的 iOS armv7 三个 packed-color cache globals 已在 IDB 中命名、注释、bookmark
并保存。

## 3. 入口、继承色与早退顺序

共同签名为：

```text
void Player::appendPreparedRenderItems(
    vector<PreparedRenderItem*>& mainList,
    vector<PreparedRenderItem*>& auxList,
    uint32 inheritedColor,
    bool inheritedDrawFlag19,
    bool inheritedFlag18)
```

函数先调用：

```text
effectiveColor = multiplyPackedColorWeights(
    inheritedColor, player.colorWeightPacked)
```

然后才检查 node deque 的逻辑 size；size 小于 2 时直接返回。也就是说空/root-only Player 仍读
颜色字段并触碰 packed-color cache，但不复制 root-content Variant、不取 dispatch、不读取 priority
Array。原本本地在颜色合成前做 motion-path/string/log gate，本轮已删除，恢复这一可观察顺序。

packed-color helper 的共同语义为：

```text
neutral = 0xFF808080
if rhs == neutral: return lhs
if lhs == neutral: return rhs
if cache == (lhs,rhs) or cache == (rhs,lhs): return cachedResult
for each byte channel:
    value = (lhsByte * rhsByte) >> 7
    if value > 254: value = 255
publish cache operands and packed result
return result
```

identity fast path不更新 cache。cache 只有一对无序输入和一个结果，进程全局、无锁、无
thread-local；并发调用形成原生 data-race/撕裂 sharp boundary。iOS armv7 的三个 dword 只因 IDA
最近符号规则被显示成 Bezier map 后的大偏移，本轮已在真实地址建立独立符号，二者并非同一容器。

## 4. priority-content owner 与 node 选择

node 数量至少 2 后，builder：

1. CopyRef Player 持久 `_rootContentVariant` 到临时 Variant；
2. 对临时值执行 `AsObject()`，得到独立 retained dispatch；
3. 立即清理临时 Variant；
4. 整个两遍 builder 生命周期内持有该 dispatch；每次 priority 数字读取都使用同一 receiver。

每个 logical non-root iteration 计算：

```text
priorityPosition = nodes.size - logicalIndex - 1
rawIndex = priorityContent.PropGetByNum(priorityPosition).AsInteger()
nodeIndex = uint32(rawIndex) + 1       // 32-bit wrap
node = nodes[nodeIndex]                // unchecked deque indexing
node.drawnThisFrame = false
```

PropGetByNum 的返回码被忽略；临时 result Variant每次迭代构造/析构。priority dispatch 为 null、
返回不可转 integer、nodeIndex 越界或 deque 元数据畸形均没有修复 gate。

这一顺序产生几个容易被“安全化”破坏的边界：

- duplicate priority index 会重复选择、重复清零并可能重复 build 同一 node；
- priority list 漏掉的 node 在第一遍不会清 `drawnThisFrame`，可能把上一帧 true 留给第二遍
  type-12 post-pass；
- `rawIndex == 0xFFFFFFFF` 经 `+1` 变 0，可选中 canonical root；
- 其他负数先按 32-bit wrap，再进入无检查 deque addressing；
- builder不按 deque 顺序渲染普通 node；只有第二遍 post-pass 使用原始 `1..size-1` 顺序。

## 5. 第一遍三条 node 路径

### 5.1 particle node（non-preview type 4）

particle 分支发生在 selected node 的 accumulated-active gate之前。builder从 node Variant复制并
retain一个 Array dispatch；同一 dispatch覆盖 count 读取、全部按数字索引取 child，以及所有递归
调用。re-entrant child调用即使替换原 node Variant，也不会切换当前 receiver。

count转换为有符号 int后用 `0 <= i < count` 迭代；负数等价于零次。每个 child native pointer
不判空，直接递归，并把同一 mainList/auxList传下去：

```text
childColor = ownerNode.inheritFlags & 0x200
    ? effectiveColor : 0xFF808080
childDrawFlag19 = caller.inheritedDrawFlag19
childFlag18 = caller.inheritedFlag18 || ownerNode.priorDraw
```

particle分支完成后 `continue`，不再检查 active，也不为 particle node 创建普通 item。preview 模式
完全跳过这条 particle recursion，随后按普通路径处理可见类型。

### 5.2 nested motion（non-preview type 3）

active false先过滤。active type-3 从 child Variant取得 raw native Player，无 null/class/generation
gate。

当 `drawFlag == false && stencilCompositeMaskReferenced == false` 时，child直接向调用者当前
mainList/auxList递归，不创建 wrapper，不做 child-local sort。

任一 flag 为真时走持久 wrapper path：

1. 先置 owner node `drawnThisFrame = true`；
2. ensure node-owned persistent item；
3. 每次刷新 ownerLabel、portable nodeIndex，置 hasOwnSource=false；
4. 写 `drawFlag=false`、stencil byte/int、raw paintBox 和 clip viewport；
5. root draw-affine 非 identity 时按 native helper变换有效 viewport和 paintBox；
6. 只有 node.drawFlag 为真才先把 wrapper加入 auxList，再解析/ensure visible ancestor item；仅
   maskReferenced为真时不加入 aux，parent为 null；
7. parentItem store发生在 aux growth和ancestor ensure之后；
8. `childItems.clear()` 只把 end退回 begin，保留容量；
9. 以 wrapper.childItems作为 child mainList递归，强制 childDrawFlag19=true；
10. 最后把完成的 child pointer range插入调用者 mainList，wrapper自身不进入 mainList。

因此 aux push或ancestor allocation失败时，wrapper可能已部分刷新；child递归失败时 childItems
保留已提交前缀；main range insert失败时完整 childItems仍留在 wrapper，但调用者 mainList未得到
该 range。

### 5.3 ordinary source item

其 admission 顺序为：

```text
if !forceVisible and !(preview-specific nodeType bit): continue
if !source.valid: continue
node.drawnThisFrame = true
ensure node.preparedRenderItem
populate persistent item in place
mainList.push_back(item)
```

bitmask 是 non-preview `0x1441`、preview `0x1449`；skipFlag0另用 non-preview `0x441`、preview
`0x449`。source.valid是最终必需条件，forceVisible不能绕过它。drawn byte在 allocation/string/vector
操作之前发布；后续任意异常都可能留下 drawn=true而 mainList尚无该 item。

## 6. persistent item 构造、复用与字段发布顺序

每个 MotionNode只有一个 raw PreparedRenderItem owner。ensure只在 pointer为 null时执行
`new T()`；构造完整成功后才把 pointer写回 node。operator new失败保持 node pointer为 null。

native构造不是整对象 memset。它只初始化拥有资源所需的三个 string backing、四个 pointer
vector begin/end/cap、三个 Variant tag，以及少量显式默认字段（rawFlag16、drawFlag、rawFlag20、
stencilComposite、commandPatchDivision等）。其他 trivial字段保持未初始化，直到某条 admitted path
写到它。64位大小为`0x1B0`，32位为`0x148`。本地用 user-provided derived constructor避免
`new T()` value-initialize整个对象，保留这一 source-level形状。

ordinary path在复用同一 item时不重构对象，按以下可观察顺序覆盖：

1. ownerLabel；
2. portable nodeIndex sidecar；
3. skipFlag0、rawFlag16、`inheritedFlag18 || node.priorDraw`；
4. `_findMotionContextVariant -> temporary ttstr -> commandKey`；
5. layerId1/layerId2、sortKey=accumulated.posZ、coordinateMode、objTriPriority；
6. commandCoord `{posX, zFactor*posZ + posY}`；
7. source origin + active-slot ox/oy；
8. accumulated 2x2 command matrix；
9. 四个 packed colors逐个乘 effectiveColor，再用 source clip remapper改写；
10. 八个 raw corners；
11. active-slot commandSrc、blendMode，accumulated opacity；
12. borrowed `sourceState=&node.source`、stencil、三种 draw cause的 OR、portable hasOwnSource；
13. visible ancestor item ensure与 parentItem，随后 portable ancestor index；
14. raw paintBox；clip pointer copy、ordered validity和 null sentinel；
15. mesh type/division与三个 mesh vector分支；
16. root affine in-place geometry与 rect transform；
17. mainList push。

ancestor只有一个 null sentinel（portable index `-1`对应 native null pointer）。其余 index包括
self和越界均不检查；ancestor item可能仅被 ensure而本帧从不进入 main/aux。

## 7. mesh 容器与 stale-field 边

`commandCompositeMeshPoints` 每次都 copy-assign，因此空输入也会清空它。若复制后非空，
`meshType=2`，不更新 Bezier division/raw/processed vectors。

当 composite为空且 node.meshType==1：

- raw `meshControlPoints`为空时只把 item.meshType改为0；
- 非空时计算 capped patch division，再分别 copy-assign processed
  `transformedMeshControlPoints -> meshPoints` 和 raw `meshControlPoints -> commandBezierPatchPoints`。

其他 meshType不清 meshPoints、raw Bezier vector或旧 commandPatchDivision。这是 persistent item
复用的刻意 stale-field边；下游必须按 meshType判读。更尖锐的是 affine阶段无条件遍历 item的
processed meshPoints vector，而不是再次检查当前 meshType：若它是历史残留，仍会被再次变换。
本地保持这一行为。

vector copy-assign/growth使用各端标准库 owner。pointer vector增长失败不会构造 item owner；
MeshPoint vector在复用容量时可先覆盖已有元素，再在实现规定点提交 size。源码无 catch、rollback
或手工恢复旧 mesh payload。

## 8. root draw-affine、rect 与数值边界

递归 child读取 canonical root Player的 draw-affine owner，而不是 child自己的构造 identity矩阵。
root pointer无 null gate。只有 root的 non-identity byte为真时才变换：

- 固定四个 corners；
- commandCompositeMeshPoints 全部点；
- item.meshPoints全部点（包括上述 stale vector边）；
- viewport仅在 ordered-valid时；
- paintBox无条件。

point公式在 double域按独立 multiply/add执行，输入float先提升，结果每个坐标立即窄化float；没有
FMA。raw commandBezierPatchPoints不变换。

rect helper把 `{left,top,right,bottom}` 的四角全部变换、分别窄化float，然后用固定嵌套
`lhs < rhs ? lhs : rhs` / `lhs > rhs ? lhs : rhs` reduce。相等（包括 signed zero）和 unordered
都选择右 operand，因此不能换成可能具有不同NaN/signed-zero规则的`std::min/max`。最后对
left/top执行floorf，对right/bottom执行ceilf。

clip pointer非 null时先无条件读取四个float，再以 `right >= left && bottom >= top`判有效；NaN
使 validity false但复制值仍已写入 item。null installs `{1,1,-1,-1}`并跳过 affine viewport helper。
paintBox即使反向或NaN仍在 non-identity时无条件进入 helper。

## 9. 第二遍 type-12 stencil-composite post-pass

第一遍结束后，builder按 raw node order从1遍历，不再读取 priority content。admission为：

```text
nodeType == 12
&& (stencilType & 4) != 0
&& drawnThisFrame
```

通过后：

1. ensure parent item；
2. 把同一 pointer append auxList；
3. `parent.childItems.clear()`；
4. 先 append parent自身；
5. 按 node.stencilCompositeMaskNodes raw pointer-vector顺序遍历；
6. 只保留 drawn且type 0/3的 mask node；
7. ensure mask item；
8. type0或preview直接 append mask item；
9. non-preview type3则 range-insert maskItem.childItems，不 append type3 item本身。

重复 mask pointer保留；null pointer立即解引用；循环/自引用不做检测。aux push成功后，child clear/
self push/mask append失败会留下已提交的 aux pointer与 child prefix。若 priority第一遍漏掉某个
type12，上一帧残留 drawn=true可让它在本帧进入该 post-pass，这也是第一遍只清 selected nodes的
直接后果。

## 10. owner、异常与 partial-commit

mainList、auxList、wrapper.childItems都只拥有连续 pointer slots，不拥有指向的 item。item由
MotionNode持久 raw pointer持有并跨多次prepare/getCommandList/draw复用。builder本身不 delete
item，也不清未选中node的缓存。

函数无事务边界。典型commit点如下：

- selected node先清 drawn；ordinary/type3 admission再先置true；
- persistent item字段逐个原地覆盖；string/vector assignment之后的失败不恢复此前字段；
- wrapper aux append早于 parentItem store；
- ordinary main append是最后一步，失败时完整/部分 item仍持久存在；
- post-pass aux append早于 child vector重建；
- recursive child共享调用者 auxList，且可能直接共享 mainList；子异常保留所有先前append。

递归图没有visited set或depth limit；child/particle循环最终stack overflow。

Android arm64 landing paths会清当前 Variant/dispatch临时 owner后resume。iOS armv7的44-way SjLj
dispatcher精确区分：临时 context Variant、particle Variant、particle Array dispatch、全函数
priority-content dispatch；cleanup destructor自身再抛时转 terminate，非法selector trap/abort。
Android armv7和iOS arm64主体text中没有独立landing block；普通路径仍明确release owner，源码无
catch或list/item rollback。这里记录的是可见目标实现差异，不把某一端的EH代码生成强行移植成
第二套motion算法。

## 11. 本地偏差与本轮恢复

原本生产 builder额外执行：

1. effectiveColor和node-count gate之前查询logo trace/snapshot与matched motion path；
2. child递归前后记录main/aux size、读取child motion、格式化并输出日志/快照；
3. ordinary item affine完成后、mainList push之前把wide source path窄化到新
   `std::string sourceKey`；
4. 构造多组expected geometry、packed-color展开、`fmt::format`字符串与stderr快照。

四端4305条主体中都没有这些gate、string、日志、null容错或分配。尤其 sourceKey assignment会
把一个新bad_alloc点插在native“item完整填充”和“main pointer commit”之间，改变
drawn/item/mainList的partial-commit边。本轮删除全部生产sidecar、辅助unpack函数和derived item的
sourceKey字段。`KRKR2_WASMTIME_HEADLESS`下的enter/leave仍作为差分测试instrumentation保留，
不属于正常plugin/Web source path语义。

portable nodeIndex、hasOwnSource和hasViewport sidecar仍保留。后续visibility producer、motion-sub、
particle-system与本builder consumer的fresh四端联合审计证明MotionNode visible ancestor必须是可跨
Player的borrowed raw pointer；因此local已恢复pointer并删除PreparedRenderItem上没有runtime消费者的
visibleAncestorIndex sidecar。builder现在与本报告原本记录的native raw-pointer消费完全一致。

## 12. 验证与剩余范围

本轮完成：

- 四端fresh decompile；
- 4305条builder主体完整pagination；
- iOS armv7 111条cleanup完整审计；
- packed-color、lazy item constructor和rect helper的四端内联/独立实现交叉对齐；
- 本地逐字段/逐commit点比较；
- IDB命名、注释、bookmark和保存；
- `git diff --check`与coverage非空行12列检查。

正式CMake/unit/Web build仍无法运行：当前环境只有系统clang，缺少CMake、Ninja、Emscripten，
也没有可复用build/out；单文件语法检查又被缺失的`boost/locale.hpp`阻断。因此本报告不把静态
验证表述为正式测试通过。

相邻 MotionNode/PreparedRenderItem 最终 destructor、deque erase/replacement 和唯一释放生命周期已由
`analysis/motionplayer_motionnode_prepared_item_deque_lifetime_four_binary_2026-08-27.md`闭合，
并纠正 Player 析构缺失的 variable-track/node 两次显式 clear。下一步继续完整 root-reachable
body/container ledger。
