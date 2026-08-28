# Player vertex / mesh phase 四参考二进制联合恢复

日期：2026-08-27

## 1. 完整函数映射

本报告闭合 `updateLayers` 的第二个 phase3 helper：parent mesh chain、dirty gate、raw/combined patch、
affine vertex与inverse、inherited mesh grid、forced-visible镜像、processed count以及delta-position尾部。

| 平台 | 函数 | 完整指令 |
|---|---:|---:|
| Android arm64 | `0x6B98D0` | 1265 |
| Android armv7 | `0x5866F8` | 1108 |
| iOS arm64 | `0x10010F6AC` | 961 |
| iOS armv7 | `0x10CE30` | 1297 |

四端均 fresh decompile，并从offset 0完整读取disassembly；四个cursor均 `done=true`。A64 NEON展开、
32位VFP/deque addressing和不同STL vector helper解释指令数差异，共同控制流一致。

## 2. function-level临时容器

函数建立一个 `vector<MeshPoint> combinedPatch`，在全部nonroot nodes之间复用。`clear()`只把end移回
begin，保留allocation；copy assignment按当前node raw patch大小复用或扩容。正常函数尾销毁一次，
异常时由ABI landing/SjLj cleanup销毁。

参考实现没有motion path string、logger状态或第二个diagnostic vector。local修改前在临时vector之前
读取trace开关并可能物化path，使只有synthetic root的Player也发生native不存在的conversion/allocation。

## 3. 每节点前半段

共同顺序：

```text
for each nonroot node:
    parent = nodes[node.parentIndex]               // unchecked

    if node.forceVisible:
        emoteOwner = copy(node.emoteEdit)
        node.priorDraw = emoteOwner.getBool("priorDraw")
    else:
        node.priorDraw = false

    node.meshAncestor =
        (parent.hasMeshData || parent.separator)
            ? &parent : parent.meshAncestor
    node.meshPassDirty = node.accumulated.dirty
        || (node.meshAncestor && node.meshAncestor.meshPassDirty)
```

`priorDraw` callback发生在读取parent mesh state之前，copy owner跨getter保持receiver。异常会保留本node
此前状态，尚未写meshAncestor/dirty。

dirty=false时仍有一个真实side effect：若raw `meshCombine && hasMeshData`，沿raw parent chain跳过
与当前runtime ancestor重合的combined mesh，更新`meshAncestor`；随后continue。不会重算其它mesh bytes。

dirty=true时提交：

```text
hasMeshData = !active.done && meshType!=0 && rawPatch nonempty
              && source.valid && (meshFlags & 8)
separator = meshAncestor != null && !(inheritFlags & 0x02000000)
```

node type 1/5先把accumulated x/y沿每个live mesh ancestor映射，逐次增加processed count，并发布
vertexPosX/Y；Z直接来自accumulated posZ。shift直接消费nodeType，无range guard。

## 4. patch组合和derived vector publication

active slot done会跳过后续source/geometry。其余node由forceVisible或mask `0x1C41`（normal）/
`0x1C49`（preview）门控，并要求source.valid。

effective patch初始借用raw `meshControlPoints`：

- `meshCombine && hasMeshData`：copy当前raw patch到function-level combinedPatch，沿raw parent chain对每个
  live mesh执行delta叠加；若vector尺寸不同，抛精确原生 `mesh size is different.`；遇到当前runtime
  ancestor时先把node.meshAncestor提升到其ancestor；
- `meshCombine && !hasMeshData`：只clear combinedPatch；
- 非combine：继续借用raw vector。

source origin把 `source.origin + activeSlot.ox/oy` 经accumulated affine转换为orgX/orgY。随后先clear
`transformedMeshControlPoints`和`compositeMeshPoints`，保留capacity并使异常后的partial state可见。

`meshType==1 && effectivePatch nonempty`时resize transformed vector到16，并无条件读取前16项生成own-
affine control points；长度1..15保持native越界boundary。若当前hasMeshData，再以width/height缩放后的
matrix计算inverse；det为0也直接IEEE除法，无singular guard，offset先缩窄为float再取负。

## 5. 四角、division与ancestor映射

独立quad materialization gate通过后，四个float点按 TL/TR/BR/BL严格写入。所有表达式先double计算，
每项各自缩窄float。参考root在这8个store后直接继续，没有重算同一表达式做self-check。

存在meshAncestor时：

- meshType1+effective patch：用unit patch求own division，width/height转uint32，乘加均按W32回绕；
- 其它mesh：从最近live ancestor取得division与source extent，继承缩放后cap 50；
- denominator为0仍执行ISA unsigned divide，无source-level guard；
- `meshDivX=splitX+1`、`meshDivY=division-splitX+1`按32位counter word发布；
- build bilinear grid；unit patch路径再经16-point Bezier evaluator；
- separator之前的live ancestors逐点map整个composite vector并映射position，processed count增加
  `vector.size()+1`；separator之后只继续映射position并每ancestor加1；
- position前后有差异才把float delta平移到全部composite points。

没有meshAncestor而meshType1时不在此创建composite vector，但仍按division grid规模增加processed
count。所有processed stores是uint32自然回绕，不饱和。

## 6. forced-visible写回与函数尾部

eligible geometry完成后，forceVisible node把coord、matrix、width/height、origin、flip、zoom、slant、
angle写回其retained emoteEdit object。每层Variant/dispatch有独立owner；indexed/property setter按native
顺序逐项commit，异常不回滚node geometry或已写object字段。四端vertex helper在主循环和临时vector
析构后直接返回，不包含position-delta发布。

后续C29对四端updateLayers root重新切分边界，证明delta-position pass内联在root的phase2之后、
camera-constraint之前，且覆盖全部node包括synthetic root。修改前本地误把它放在本helper尾部并排除
root；现已移回`PlayerUpdateLayers.cpp`。该纠正不改变本报告其余vertex/mesh主体证据。

## 7. ABI/浮点边界

四端node stride与deque物理实现仍为2632/2272/2648/2228和libstdc++ single-node blocks对libc++
16-node blocks。portable C++只保留deque/vector与source-level member顺序。

double到uint32的mesh extent conversion、A64 profile divide、NaN/infinity/out-of-range、W32乘法回绕与
signed counter reinterpretation由专门helper表达；不能用host `size_t`或高精度中间值“修正”。

## 8. 本地差异与证据后实施

完整vertex/mesh主状态机逐项匹配。确认的不匹配是：

1. 入口trace开关与motion path物化；
2. 四角store后重新构造同源8-float `expectedVertices`；
3. 逐项fabs比较、narrow live src、构造两条fmt string并调用logo checker；
4. port误把updateLayers root的all-node delta-position pass附在vertex helper尾部。

这组旁路永远只拿刚才同一表达式写出的结果自比，却增加allocation、string conversion、logger与
异常前沿；它还可能在node geometry已commit而ancestor/grid尚未处理时抛出。四端完整函数无对应
调用。证据固化后已删除入口状态和整个self-check block，保留一次native四角publication；后续root
边界证据又把delta-position pass移回phase2与camera-constraint之间。

四个IDB已统一命名 `Player_updateLayersPhase3_VertexComputation_guess`，添加注释/bookmark并保存。

## 9. 验证限制

实施后执行 `git diff --check`、coverage严格12列、duplicate-ID和diagnostic residual搜索。当前环境
缺CMake/Ninja/Emscripten正式工具链，不能声称unit/Web build通过。
