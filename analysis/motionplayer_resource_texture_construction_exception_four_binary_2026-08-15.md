# MotionPlayer ResourceManager 页面纹理构造引用异常矩阵（四参考，2026-08-15）

## 目标与结论

本纵切面专门闭合 `ResourceManager` 旧审计中最后一个未命名边界：Win source 页面和 KRKR
atlas 页面创建出的 `iTVPTexture2D *`，在 nested `operator[]`、旧纹理 `Release`、元数据
getter/conversion 或 `Update` 抛异常时，是否存在隐藏的 LSDA/SjLj cleanup owner。

四份完整函数与异常尾给出的共同源码结论是：**没有**。页面 texture 在
`CreateTexture2D` 返回后只是 raw local；持久 map 的 `setTexture` 会为每个成功发布的 mapped
value 取得独立引用，但 construction reference 只在正常 Win 尾或正常 atlas 页尾显式
`Release`。任何在该显式调用前逃出作用域的异常都会泄漏 construction reference；已经发布
的 map node、entry texture 引用和字段前缀不回滚。

这不是建议修复的健壮性问题，而是四参考共同的源级边界。Web 恢复源码因此继续使用 raw
pointer 和显式正常尾 `Release`，不添加 `unique_ptr`、scope guard 或 catch/release。

## 目标映射

| 路径 | A64 | A32 | I64 | I32 |
| --- | --- | --- | --- | --- |
| Win caller / 内联 loader | `MotionNode_findSource_guess` `0x691CC8` | `0x570500` | `0x1000F316C` | `0xEF97C` |
| KRKR atlas caller / 内联 builder | `Player_loadKrkrAtlasSource_guess` `0x6931C8` | `0x570F54` | `0x1000F4098` | `0xF0BE4` |
| I32 Win SjLj cleanup dispatcher | — | — | — | `0xF0394` (`MotionNode_findSource_unwindCleanup_guess`) |

这里的 `I64/I32` 是本仓库 recovery IDB 对 iOS arm64/armv7 的既有简称；所有无原始符号的
私有恢复名继续保留 `_guess`。

## Win 页面：正常引用交接

| 阶段 | A64 | A32 | I64 | I32 |
| --- | --- | --- | --- | --- |
| `CreateTexture2D` 返回 | `0x69233C` | `0x5707F6` | `0x1000F35B0` | `0xEFDBC` |
| BGRA dealloc | `0x692344` | `0x5707FA` | `0x1000F35B8` | `0xEFDC4` |
| nested `operator[]` | `0x692358` | `0x570806` | `0x1000F35C4` | `0xEFDD0` |
| Release old / store / AddRef new | `0x692370..0x69238C` | `0x570814..0x570824` | `0x1000F35DC..0x1000F35F8` | `0xEFDE2..0xEFDF8` |
| Release construction ref | `0x69239C` | `0x57082C` | `0x1000F3608` | `0xEFE04` |
| publish result pointer | `0x6923A0` | `0x57082E` | `0x1000F360C` | `0xEFE0A` |

64 位 texture `Release` 使用虚表槽 `+0x10`，32 位使用 `+8`。该槽位判据同时用于区分
TJS/dispatch 临时量的 64 位 `+8`、32 位 `+4` 清理，避免把后者误报为 texture owner。

Win 路径的顺序是：Create 返回 raw pointer，先释放 BGRA，再让 nested `operator[]` 插入或
找到 mapped slot；`setTexture` 按 Release-old -> store-new -> AddRef-new 执行；随后 caller
显式 Release construction reference，最后才把 pointer 写入 source result。

## Win 页面：四端异常路由

### A64

完整 native unwind tail 最终到 `0x692FBC` 的 `_Unwind_Resume`。其中仅有两组接口临时量的
虚表 `+8` 调用（`0x692D88..0x692D90`、`0x692F4C..0x692F54`），没有任何虚表 `+0x10`
texture `Release`。因此 nested insertion、old Release 或正常 construction Release 本身抛出
时，都没有补偿性的 texture cleanup。

### A32

完整函数到 `0x570C20`。正常 texture Release 使用虚表 `+8`；后部间接清理只使用虚表
`+4`（`0x570BC2..0x570BC6`、`0x570C18..0x570C1C`），且函数没有本地
`_Unwind_Resume` cleanup body。没有隐藏 texture owner。

### I64

完整尾部到 `0x1000F3B44`。后部两组接口清理都使用虚表 `+8`
（`0x1000F3AE8..0x1000F3AF0`、`0x1000F3B30..0x1000F3B38`）；不存在虚表 `+0x10`
texture `Release`。这与 A64 的槽位分类一致。

### I32 SjLj

Win caller 在 nested `operator[]`、Release-old、Release-construction 前分别写入 call-site
`0x20`、`0x21`、`0x22`。相邻 dispatcher `0xF0394` 的这些 case 只路由到 ttstr、Variant、
PSB raw-node 等临时量清理，所有间接析构都是虚表 `+4`；整个 dispatcher 没有 texture 的
虚表 `+8` 调用，最后恢复原异常。因此三个阶段都不补做 texture Release。

## KRKR atlas：正常页面与 entry 发布

| 阶段 | A64 | A32 | I64 | I32 |
| --- | --- | --- | --- | --- |
| page `CreateTexture2D` | `0x693CD4` | `0x5717D2` | `0x1000F4C6C` | `0xF1634` |
| nested `operator[]` | `0x693D58` | `0x571818` | `0x1000F4CD8` | `0xF168A` |
| Release old / store / AddRef page | `0x693D80..0x693DA0` | `0x57183A..0x571850` | `0x1000F4CF8..0x1000F4D18` | `0xF16A6..0xF16C4` |
| metadata writes | `0x693DB4..0x693FBC` | `0x57185C..0x57196C` | `0x1000F4D2C..0x1000F4E74` | `0xF16CA..0xF183C` |
| optional Update / BGRA free | `0x693FC0..0x693FEC` | `0x57196E..0x571984` | `0x1000F4E78..0x1000F4EA0` | `0xF1840..0xF1864` |
| page construction Release | `0x694000..0x69400C` | `0x571998..0x57199E` | `0x1000F4EB0..0x1000F4EBC` | `0xF1874..0xF1880` |

每个成功执行 `entry.setTexture(page)` 的 mapped descriptor 都持有自己的 AddRef。页级 raw
construction reference 与这些 entry 引用是并行 owner，不是 move/transfer。metadata 按
`originX`、`originY`、rect、clip 前缀逐步写入，Update 之后才释放该 rect 的 BGRA；没有事务
对象把这些动作组合回滚。

## KRKR atlas：四端异常路由

### A64

native landing/catch 区从 `0x694894` 延伸到函数尾 `0x695110`。该整段没有任何 `BLR`
texture Release；出现的清理均为 direct string/raw-node/vector/delete/catch helper。页正常尾
`0x69400C` 是函数中 construction reference 的唯一释放点。

### A32

完整函数在 `0x5719EA` 结束，正常页尾 `0x57199E` 后只有 vector、string、raw-node 正常
清理和 length-error helper，没有本地 landing-pad/`_Unwind_Resume` body，也没有第二个
texture `Release`。因此异常直接越过 raw page local。

### I64

native exception tail 从 `0x1000F4FBC` 延伸到 `0x1000F52CC` 的 `__Unwind_Resume`。其中
没有间接虚调用；只清理 string/raw-node/vector/allocation owner。虚表 `+0x10` 的 page
Release 只存在于正常页尾 `0x1000F4EBC`。

### I32 SjLj

I32 在调用前写入的关键 call-site 为：Create `0x3A`、page Release `0x3B`、key conversion
`0x3C`、nested `operator[]` `0x3D`、Release-old `0x3E`、metadata `0x3F..0x4B`、Update
`0x4C`、BGRA dealloc `0x4D`。`0xF193E` 的 78-case dispatcher 对这些 case 只做 ttstr、
PSB raw-node、vector/bin storage 和像素记录容器清理；相关路径在
`0xF1B9A..0xF1BF8` 汇合，随后继续外层容器清理并在 `0xF1D12` 恢复异常。dispatcher 中
不存在虚表 `+8` 的 texture Release。

## 精确失败矩阵

| 抛出点 | construction reference | 持久 map / entry | 已写字段 | raw BGRA |
| --- | --- | --- | --- | --- |
| `CreateTexture2D` 内部 | 未返回，无可释放 texture | 未改变 | 未改变 | Win BGRA；atlas records 仍按各 ABI 外层 cleanup 边界处理 |
| nested `operator[]` allocation/rehash | 泄漏 | candidate node 由 map 自身强回滚 | 无新字段 | 已释放的 Win BGRA不受影响；atlas record BGRA 仍在 raw record 中 |
| Release-old | 新 page/Win construction ref 泄漏 | 原 mapped pointer 尚未替换 | 原字段保留 | atlas 同上 |
| metadata getter/conversion | page construction ref 泄漏 | 当前及先前 entry 保留各自 AddRef | 已提交前缀保留，后缀保持旧值/默认值 | 当前及未访问 record 可保持非 null |
| `Update` | page construction ref 泄漏 | entry 保留 AddRef | metadata 已完整提交 | 当前 rect 尚未执行正常 dealloc |
| 显式 construction `Release` 本身 | 不重试；按该实现的实际副作用保留 | 已发布 map 引用不回滚 | 已提交 | 已正常释放到该点者保持释放 |

## 源码与旧待办迁移

- `PlayerResource.cpp` 继续保留 raw `iTVPTexture2D *` 和正常尾显式 `Release`，只补充四端
  exception-boundary 语义注释；不添加会改变原版泄漏与部分发布行为的 RAII。
- `motionplayer_resource_manager_module_map_lifecycle_four_binary_2026-08-14.md` 中原先
  “LSDA 尚未逐 landing-pad 命名”的待办已迁移为本矩阵的闭合结论。
- 四份 recovery IDB 在 Create、nested publish、正常 construction Release 和 exception
  cleanup 汇合处加入同一语义注释；I32 Win dispatcher 恢复为
  `MotionNode_findSource_unwindCleanup_guess`。

