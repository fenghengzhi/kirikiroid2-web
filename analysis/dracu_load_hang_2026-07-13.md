# DRACU-RIOT! LOAD 路径分析（2026-07-13）

## 已确认的运行时现场

- 测试包：`reference/xp3/dracu/dracu.zip`，入口 `data.xp3`。
- 点击 LOAD 后在存档槽绘制阶段抛出
  `load.ks(40) sysexec: Cannot convert the variable type (() to Object)`。
- 完整脚本调用链：
  `load.ks:40 -> SystemHook.kagExec -> pending/_exec -> execDelay ->
  Current.action("onStopMotion") -> CustomSaveLoad.onStopMotion -> update ->
  drawItemView -> drawItemText`。
- 异常点是 `custom.tjs:1645 tmp.setImageSize(lay.width, lay.height)`。最终的
  VM 值追踪证明 `tmp = kag.temporaryLayer` 是有效对象；为 `void` 的是函数
  参数 `lay`，其来源为 `drawItemText(target.info, ...)` 中的 `target.info`。
- `target.info` 来自 `getSlotAreaLayer(i, "infoarea")`；同一路径的
  `thumbarea`、`textarea` 也都返回 `void`。
- `onStopMotion` 已经实际进入（日志为 `onStopMotion, show`），所以不是
  `SystemHook` 回调、`Current` 或其 closure context 丢失。

## libkrkr2.so 反编译证据

| 地址 | 对应实现 | 关键结论 |
|---|---|---|
| `0x9D8C28` | `tTJSInterCodeContext::PropGet` | property 默认成员调用 `PropGetter->FuncCall`，原样传递 `result` 与 `objthis` |
| `0x9D89CC` | `tTJSInterCodeContext::FuncCall` | getter/function context 进入 `ExecuteAsFunction` |
| `0x9CF1E0` | `tTJSInterCodeContext::ExecuteAsFunction` | 寄存器区、this/global proxy、参数和 result 数据流与本地结构一致 |
| `0x9CF960` | `tTJSInterCodeContext::ExecuteCode` | `VM_CHGTHIS`（case 123）无条件要求源/目标为 object 并改写 closure `ObjThis` |
| `0x9FA230` | `tTJSCustomObject::PropGet` | 找到成员后进入 `TJSDefaultPropGet` |
| `0x9F9AAC` | `TJSDefaultPropGet` | closure 自带 `ObjThis` 优先，否则使用调用方 `objthis` |
| `0x6B5AD8` | `Player_findNodeByRawLabel` | 先查当前 Player+24 raw-label map；未命中且递归标志为 1 时遍历子 Player |
| `0x6B601C` | `Player_visitChildPlayerDispatches` | node 顺序遍历；type 4 按 Array index 遍历 particle Player，type 3 遍历单个 child Player；回调 false 时停止 |
| `0x6F230C` | `0x6B5AD8` 的 visitor callback | 对每个子 Player 以原 key/递归标志再次调用 `0x6B5AD8`，找到后停止 |
| `0x6D38F4` | `getLayerGetter` | 用递归标志 1 调 `0x6B5AD8`，找到 node 后创建 LayerGetter |
| `0x6D3998` | `getLayerMotion` | 用递归标志 1 调 `0x6B5AD8`，找到 node 后复制 node+1912 child-Player variant |

## 已纠正的架构偏差

本地 `VM_CHGTHIS` 曾包含 2026-03-29 加入的 workaround：当源寄存器为
`void` 时静默跳过 closure 重绑定。`libkrkr2.so @ 0x9CF960 case 123`
不存在该分支；它会走 `AsObjectNoAddRef` 的类型错误路径。当前已恢复 Android
数据流，避免把真实的首个 void context 延迟成后续无关位置的 void 属性。

本地 `getLayerGetter`/`getLayerMotion` 曾只查当前 Player 的
`_nodeLabelMap`。运行时确认存档槽对象为 `slot/normal`，直接标签表只有
`base,tumb,clear,edit,new`；目标 `thumbarea/textarea/infoarea` 位于其 type-3
子 Player。Android 包装始终给 `0x6B5AD8` 传递递归标志 1，因此本地缺少的
`0x6B601C` 子遍历才是三个 area 全部变成 `void` 的根因。

当前已新增 `findNodeByRawLabelLike_0x6B5AD8`，顺序复刻直接 map 查询、
type-4 particle Array 遍历、type-3 child Player 遍历及首个命中停止；
`getLayerGetter`、`getLayerMotion`、`hitTestLayer` 共用该路径。

## 验证状态

- 普通 Web Debug 构建通过。
- 修复后每个槽位均递归解析到：`thumbarea=index4`、`textarea=index5`、
  `infoarea=index6`，三者都是 `nodeType=1, shapeType=2`。
- LOAD 界面完成绘制；退出回标题后再次进入 LOAD 仍正常。
- 两次进入/一次退出过程中均未出现 `Cannot convert`、`RuntimeError` 或
  `memory access out of bounds`。
- `TEMPTRACE`/`TEMPVM`/`TEMPGETTER`/`AREATRACE` 临时日志已全部删除。
