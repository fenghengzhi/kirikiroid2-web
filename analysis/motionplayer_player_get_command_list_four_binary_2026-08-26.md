# Player.getCommandList command 容器（四参考二进制，2026-08-26）

## 1. endpoint 与完整指令覆盖

| 端 | 入口 | 完整 disassembly 指令数 | ABI 形状 |
|---|---:|---:|---|
| Android arm64 | shared tail `0x6D0E2C` | 1313（另加 2 条 EmotePlayer thunk） | libstdc++ vector/sort；共享尾块 |
| Android armv7 | `0x595FF0` | 838 | libstdc++ vector/sort |
| iOS arm64 | `0x100121EB0` | 596 | libc++ vector/sort |
| iOS armv7 | `0x120CF8` | 1032 | libc++ vector/sort + SjLj EH |

四端均已 fresh 全函数 decompile，并分页读取全部 3781 条函数指令。Android arm64
的 `0x67F900` 是 EmotePlayer thunk：加载 embedded Player `+0x428` 后 branch 到
shared tail；Player registrar 直接进入 `0x6D0E2C`。IDA 将 tail chunks 归到 thunk，
因此保留真实共享形状，用 target line comment/bookmark 标记 Player 入口，没有伪造
第二份函数。其独立 sort comparator `0x6D22E0` 也已反编译、命名：只执行
`lhs.sortKey < rhs.sortKey`，unordered 返回 false。四份 IDB 均已保存。

## 2. 顶层两遍数据流

共同结构：

```cpp
vector<PreparedRenderItem *> main;
vector<PreparedRenderItem *> aux;
prepareRenderItems(main, aux);
sort(main, sortKeyLess);

for (item : main)
    item->commandVariant = buildFreshCommand(*item);

Array result;
for (item : main) {
    if (item->skipFlag0 || item->rawFlag16 || item->opacity == 0)
        continue;
    item->commandVariant.stencilChain = buildStencilChain(item->parentItem);
    result.Items.push_back(item->commandVariant);
}
return result;
```

- `PreparedRenderItem` 是 node-owned persistent object；main/aux 只是本次调用的 borrowed
  pointer vectors；
- 只有 main 排序和序列化，aux 从不进入 command Array；
- 第一遍在过滤之前给每个 main item 建 fresh Dictionary，并原地替换 persistent
  `commandVariant`；因此被过滤的 stencil parent 仍能被 child link 引用；
- 第二遍过滤后才补 `stencilChain` 并 CopyRef 到 fresh result Array；
- 第二次调用复用 PreparedRenderItem 地址，但所有 command Dictionary 和 result Array
  都换成新 owner；旧返回值仍独立存活；
- sort comparator 是 ordered binary64 `<`。equal 与 unordered 都返回 false；不同
  STL 的等价项排列不承诺稳定，属于容器/平台边界，不应加 port-only tie breaker。

## 3. command Dictionary 的固定发布顺序

第一遍每项按以下顺序执行 `SetValue(TJS_MEMBERENSURE, sharedHint)`：

1. `key`：item 的 ttstr command key；
2. `id`：layer id；
3. `src`：active clip-slot ttstr；
4. `coordinate`；
5. `opacity`；
6. `blendMode`；
7. `coord`：fresh Array `[x, y, sortKey]`，三项 Real；
8. `mtx`：fresh Array `[m11, m12, m21, m22]`，四项 Real；
9. `color`：fresh Array，四个 uint32 零扩展到 Integer64；
10. `originX`；
11. `originY`；
12. `triPriority`；
13. `clipRect`：有效 Dictionary 或 Void；
14. `meshTransform`；
15. mesh payload（条件分支）；
16. 将 command dispatch 同时作为 Object/ObjThis 写回 item.commandVariant。

所有 SetValue 返回 bool 都忽略；script dispatch 异常传播，先前发布字段保留。数组
直接 append 到 native Array Items，不经过脚本数值 setter。

## 4. PreparedRenderItem 共同布局投影

| 语义 | 64 位四端 | 32 位四端 |
|---|---:|---:|
| `rawFlag16 / skipFlag0` | `+0x10/+0x11` | `+0x08/+0x09` |
| blend / id | `+0x30/+0x34` | `+0x1C/+0x20` |
| sort key | `+0x40` | `+0x28` |
| matrix | `+0x48..+0x60` | `+0x30..+0x48` |
| coord x/y | `+0x68/+0x70` | `+0x50/+0x58` |
| origin x/y | `+0x78/+0x80` | `+0x60/+0x68` |
| packed colors | `+0xA8..+0xB4` | `+0x90..+0x9C` |
| viewport | `+0xC8..+0xD4` | `+0xB0..+0xBC` |
| opacity / coordinate / triPriority | `+0xE8/+0xEC/+0xF0` | `+0xD0/+0xD4/+0xD8` |
| stencilComposite | `+0xF4` | `+0xDC` |
| command key | `+0xF8` | `+0xE0` |
| parentItem | `+0x108` | `+0xE8` |
| mesh div x/y / mesh type | `+0x110/+0x114/+0x118` | `+0xEC/+0xF0/+0xF4` |
| commandVariant | `+0x11C` | `+0xF8` |

随后 vector 的偏移因指针宽度继续分叉；四端语义字段次序相同。本表是跨函数结构
恢复投影，不把 STL ABI padding 复制进 portable C++ declaration。

## 5. clipRect 与 IEEE-754 边界

```cpp
valid = viewport.right >= viewport.left &&
        viewport.bottom >= viewport.top;
```

比较是 ordered float compare；任一参与值为 NaN 即 invalid。valid 时 fresh Dictionary
严格发布 `left, top, right, bottom, width, height` 六个 Real，其中 width/height 在
float 精度先做减法再转为 Variant Real；随后把 Dictionary dispatch 同时作为
Object/ObjThis 写入 command。invalid 时仍发布 `clipRect`，值为 Void。

局部 clip owner 在 `clipRect` SetValue 后、`meshTransform` 之前释放；command 留有
自己的 owner。当前本地分支、计算精度和生命周期与四端一致。

## 6. mesh payload

`meshTransform` 是 signed int：

- `<= 1`：发布 `bezierPatch` Dictionary。
  - `patch` 是 command Bezier points 展平为 `[x0,y0,x1,y1,...]` 的 fresh Real Array；
  - `division` 先以 binary64 计算 `meshDivisionRatio * int32 commandPatchDivision`，
    再 signed-truncate；ordered product `< 50.0` 时发布转换值，否则（含 NaN）发布
    50。超出 int64 范围的 target-instruction 饱和边界由
    `serializeBezierPatchDivision_guess` 显式承载。
- `== 2`：发布 `compositeMesh` Dictionary：`vtx` 展平 Real Array，随后 `divx`、
  `divy`。
- 其他值：不发布 `bezierPatch` 或 `compositeMesh`。

patch/vtx 的 translation offset 固定为零；它们读取 command snapshot points，不重算
render geometry。现有数值测试覆盖 signed zero、NaN、正负 infinity、int64 边缘和
50 cap。

## 7. filter 与 stencil alias 容器

第二遍 filter 为：`skipFlag0 || rawFlag16 || opacity == 0`。被过滤项的 fresh
commandVariant 仍留在 persistent item。

有 parent 时，从直接 parent 沿 `parentItem` 单链向根遍历，每层 append 一个 fresh
link Dictionary：

- `type = parent.stencilComposite`；
- 若 `type & 4`，`mesh` 是 fresh Array，按 parent.childItems 原始 vector 顺序
  CopyRef 每个 child 的 persistent commandVariant；重复指针不去重；
- 否则 `mesh` 直接别名 parent.commandVariant；
- link 以 Object/ObjThis 同一 dispatch append 到 chain。

无 parent 时 `stencilChain` 仍发布为 Void；有 parent 时发布 fresh Array。最后
command accessor 从 persistent Variant CopyRef/strict AsObject，写入 stencilChain，
再把同一 commandVariant CopyRef 到 result native Items。两遍结构保证 childItems 中
所有 command alias 已完成第一遍重建。

## 8. 本地对照与验证状态

本地 `PlayerLayerQuery.cpp:788` 的两遍结构、18 个字段发布、三种 mesh 分支、filter、
parent chain、native Items append、persistent alias 和 owner 释放顺序均与四端 fresh
证据一致；本切片无需源码修正。现有测试覆盖：

- fresh empty Array 与 typed zero-arg/surplus/negative-argc wrapper；
- prepare builder 的 priority/particle retained owners；
- filtered parent command、reverse/duplicate childItems、输出排序和第二次调用 owner；
- coord/mtx/color/clip/bezier/composite 字段类型与内容；
- division 的全部有序/无序数值边界。

因此本切片记为 `IMPLEMENTED`（完成四端逐行对照，而非仅有地址）。正式
CMake/unit/Web build 仍因工具链缺失未执行；每个 Dictionary/Array allocation 与
SetValue 抛出的精确 Android EH table / iOS LSDA/SjLj cleanup case 留在最终 ABI
exception ledger。

## 9. 2026-08-27 allocation/SetValue EH 闭包

最终账本再次 fresh decompile 四端主体，并按 300-instruction 页读取完整
1313/838/596/1032 条指令。Android arm64 的 1313 条包含 2 条 EmotePlayer forward chunk、
远端 shared Player body 和主体内 landing region。另完整读取 iOS arm64
`0x100122928` 的 175 条 LSDA cold cleanup、两个 terminate thunk，以及 iOS armv7
`0x121820` 的 368 条、84-case SjLj cleanup。

逐 call-site 路径可以归并为以下 source owner 前缀，而不丢失可观察行为：

1. `main`/`aux` 是 borrowed `PreparedRenderItem *` vector。任何后续异常最终只析构
   vector backing，不析构 element，也不触碰 node-owned item；
2. command Dictionary 尚未写回 `item.commandVariant` 时，异常销毁 fresh local Dictionary
   及当前已经构造的 coord/mtx/color/clip/mesh nested owners；写回成功后该 command 已成为
   persistent owner，unwind 不恢复旧 command；
3. nested Array/Dictionary 一旦被 SetValue/Items append CopyRef，局部 owner 的逆序析构只
   减去局部引用，已发布字段仍存活。SetValue status failure不抛时继续执行；dispatch
   真正抛异常时停止在当前字段；
4. 第二遍的 stencil link、optional mesh Array、chain Array 和 result Array 遵循同一
   prefix-commit：已 append 的 link/command 保留，尚未完成构造的当前 owner 被清理；
5. result 尚未复制给返回槽时，异常销毁其本地 Array owner；已经写入 persistent
   `item.commandVariant` 的第一遍前缀仍不回滚；
6. cleanup destructor/Release 自身抛出进入 terminate，不能 catch 后继续其它 cleanup。

目标差异：

- Android arm64 的 landing 全在 shared function chunk `0x6D2000..0x6D22D4`。不同入口先
  销毁当前 clip/mesh/stencil nested owner 前缀，汇合后清 result/local command owners和
  两个 pointer-vector backing，再 `_Unwind_Resume`；
- Android armv7 完整 838 条主体没有本帧 unwind landing，只有正常路径 cleanup；
- iOS arm64 的 LSDA 直接落入 175 条 cold body 的多个入口。它按 live set 释放当前
  Variant/accessor、必要的 temporary Array dispatch，最后释放两份 pointer-vector backing
  并 `__Unwind_Resume`；两个相邻 thunk处理 cleanup throw；
- iOS armv7 的 call-site 0..83 由 84-case SjLj switch 精确选择 cleanup prefix。cases
  4..60覆盖第一遍各字段的 nested owner，69..81 覆盖 stencil/result 阶段；case 61 等
  cleanup-throw case进入 terminate。共同尾释放两个 vector backing并
  `__Unwind_SjLj_Resume`。

这些路径与本地 `std::vector`、`tTJSVariant`、Array/Dictionary helper 和 accessor 的
词法 RAII 相符；没有 native rollback、catch 或额外 release 需要移植。四个 IDB 已补充
landing/cold/SjLj 命名、注释、书签并保存。因此早期留下的“每个 allocation/SetValue
精确 ABI cleanup”已闭合，正式构建仍只是环境限制。
