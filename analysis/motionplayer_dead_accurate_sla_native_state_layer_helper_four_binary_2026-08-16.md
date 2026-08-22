# accurate SLA dead native-state Layer helper 清理（四参考，2026-08-16）

## 1. 结论

`PlayerRenderTargets.cpp` 中的 `ensureAccurateSlaStateLayer_guess` 是旧 accurate-SLA 端口
留下的不可达源码抽取：全仓只有定义，没有声明、调用、函数指针引用、测试入口或字符串
引用。它实现的行为包括：

- Void/null slot 时通过 owner/parent 创建 Layer；
- 把 TJS object 解包成 `tTJSNI_BaseLayer`；
- 失败时返回 null；
- 直接修补 native parent、type 与 absolute-order mode。

四份当前参考的完整 `Player_renderAccurateSeparateLayerAdaptor_guess` 都没有这条函数边界或
数据流。item Layer 由 payload-aware TJS Variant resolver 建立 owner，随后经 TJS
`setSize`、copy-family、`setPos` 与 property setters 完成渲染和发布；accurate renderer
没有 `tTJSNI_BaseLayer` source/item 下钻，也没有由 native-instance query 控制的 null
返回门槛。因此该零 caller helper 不是应保留的“未来 native fallback”，而是与已恢复
source structure 冲突的死代码。

## 2. 四端完整函数边界

以 target Variant 的第一处读取重新归属 enclosing function，四端入口为：

| 目标 | accurate renderer entry | size |
|---|---:|---:|
| Android arm64 | `0x6C7088` | `0x203C` |
| Android armv7 | `0x590468` | `0x1494` |
| iOS arm64 | `0x10011A9E8` | `0x1590` |
| iOS armv7 | `0x118D70` | `0x1788` |

四端完整函数的已闭合证据共同显示：

- target owner 由 `SLA.targetLayer` 的 Variant copy/严格 Object conversion 建立；
- width、height 按 TJS PropGet 顺序读取；
- item Layer 的 size 与三类 geometry copy 都是 TJS FuncCall；
- final position/type/visible/opacity 是 TJS `setPos` 与 PropSet；
- unknown geometry 仍发布 Layer，不经 native-state helper admission；
- `TJSNI_Layer_FromVariant_guess` 的四端完整 xref 集均不包含 accurate renderer。

详细指令映射分别保存在
`motionplayer_accurate_sla_admission_clip_pass_four_binary_2026-08-16.md`、
`motionplayer_accurate_sla_tjs_copy_chain_four_binary_2026-08-16.md` 与
`motionplayer_accurate_sla_target_publication_four_binary_2026-08-16.md`。

## 3. 旧记录为何失效

`motionplayer_render_helper_identity_four_binary_2026-08-14.md` 当时的范围是把旧
`libkrkr2.so` 地址型函数名迁移为 `_guess` 语义名，并明确没有修改函数体。它把
`ensureAccurateSlaStateLayer_guess` 与 active helpers 一起列出，但没有做 caller census，
也早于完整 accurate renderer 的 owner、copy 和 publication 纵切面。

后续恢复已经把曾经依赖该思路的 active accurate 路径改为 TJS owner 链，唯独零 caller
定义仍留在文件中。本轮删除它并给旧文档加 supersession note，避免未来维护者把死代码
误认为参考 renderer 的可选 native fast path。

## 4. 源码与验证边界

本轮只删除不可达的 anonymous-namespace helper，不改变任何 active call expression、
Variant owner、member hint、container、branch 或渲染结果。定向检索应确认生产源码和测试
均无 `ensureAccurateSlaStateLayer_guess`；绝对地址仅保存在本文与 recovery IDB。
