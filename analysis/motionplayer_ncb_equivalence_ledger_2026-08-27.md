# Motionplayer NCB 四参考二进制等价台账

日期：2026-08-27

## 1. 目的和边界

这份台账解决 MP-F03 / MP-A 的第一个覆盖分母问题：当前本地
`motionplayer.dll` 与 `emoteplayer.dll` 注册代码究竟暴露了多少个 NCB 候选，哪些已经
具有四个参考二进制的原生注册证据，哪些仍然只是本地候选、不能当作参考实现事实。

它不把本地 `main.cpp` 当成权威来源，也不声称已覆盖所有根可达 native helper。注册
函数往往通过函数指针、静态对象和 TJS/NCB 运行库间接发布成员，单独从模块根跑直接
callgraph 会漏掉数据引用，同时会把大量通用 TJS 运行库扇出纳入分母。因此这里采用：

1. 本地注册块只生成候选分母；
2. 已完成的四二进制 registrar / callback / constructor 报告提供原生证据；
3. 只有四个平台字段全部存在且对象内顺序和脚本名都吻合，候选才能离开
   `UNMAPPED`；
4. 注册面已映射不等于回调 body 已恢复，两个状态独立记录。

## 2. 生成物

- `analysis/motionplayer_local_ncb_inventory.tsv`：316 个本地候选。候选 ID 由
  module / owner / kind / script name 构成，不再使用会因前置插入而整体漂移的全局序号；
- `analysis/motionplayer_ncb_native_evidence.tsv`：316 个已有四端原生证据的对象成员；
- `analysis/motionplayer_ncb_equivalence.tsv`：把 316 个候选与 316 个证据逐项合并后的
  主台账；
- `tools/motionsim/generate_local_ncb_inventory.py`：候选扫描器；
- `tools/motionsim/generate_ncb_equivalence_ledger.py`：证据抽取、完整性断言和合并器。

生成器当前有意锁定 316 个候选，并断言 candidate ID 唯一、原生证据无孤儿、每个证据
具备四个平台字段、对象内序号与脚本名完全相同。注册代码发生变化时，失败是覆盖分母
需要重新审计的信号，不应静默接受。

## 3. 当前覆盖数字

| 状态 | 数量 | 含义 |
|---|---:|---|
| 本地候选总数 | 316 | 当前两个模块注册块中的 NCB 候选分母 |
| `EVIDENCED_4_4` | 316 | 四参考二进制均已有 registrar / callback / constructor 证据 |
| `UNMAPPED` | 0 | 当前 NCB 候选分母中没有缺失四端注册面证据的行 |

已经映射的对象：

| owner | 已映射 | 当前本地总数 |
|---|---:|---:|
| `Player` | 93 | 93 |
| `EmotePlayer` | 73 | 73 |
| `Motion` | 36 | 36 |
| `LayerGetter` | 30 | 30 |
| `D3DAdaptor` | 16 | 16 |
| `ResourceManager` | 13 | 13 |
| `MotionLayerExtensions_guess` | 9 | 9 |
| `BezierPatch` | 8 | 8 |
| `Rect` | 7 | 7 |
| `ObjSource` | 7 | 7 |
| `Circle` | 6 | 6 |
| `Point` | 5 | 5 |
| `SeparateLayerAdaptor` | 5 | 5 |
| `Quad` | 4 | 4 |
| `SourceCache` | 4 | 4 |

当前没有尚未映射的 owner：

| owner | 待映射 |
|---|---:|
| — | 0 |

## 4. body 状态不能由注册状态替代

316 个已映射成员的 `body_status` 分布为：

| body 状态 | 数量 |
|---|---:|
| `OUT_OF_SCOPE_FOR_SURFACE_SLICE` | 78 |
| `BODY_PENDING_SEPARATE_SLICE` | 0 |
| `BODY_EVIDENCED_4_4` | 0 |
| `NOT_APPLICABLE_CONSTANT` | 25 |
| `SEE_SUBCLASS_LEDGER` | 11 |
| `CONSTRUCTOR_EVIDENCED_4_4` | 5 |
| `FACTORY_EVIDENCED_4_4` | 2 |
| `IMPLEMENTED` | 195 |

因此“316/316 注册面已映射”不能解释为“316 个实现已经逐行恢复”。`Player` 的
`getLayerMotion` 已由 raw-label resolver 全 caller 闭包提升为 `IMPLEMENTED`；
`colorWeight`、`independentLayerInherit`、`zFactor`、`processedMeshVerticesNum` 与
`contains` 又由共享 child-visitor capture/EH 前沿提升为 `IMPLEMENTED`；
`variableKeys`、`cameraTarget`、`cameraPosition`、`bounds`、`modifyRoot`、
`getLayerNames`、`setCameraOffset` 与 `getCameraOffset` 又由 TJS Array/Dictionary
容器和 caller EH 前沿闭包提升为 `IMPLEMENTED`。四个 geometry constructor、18 个
geometry callback以及 LayerGetter 29 个 property又根据已闭合的
constructor/scalar/contains/container/adaptor EH报告提升为 `IMPLEMENTED`；最后两个
`LayerGetter.label/src` 已在 MotionNode source-order owner 闭包后从
`BODY_EVIDENCED_4_4` 升级为 `IMPLEMENTED`。
其余 78 个公开
成员虽然已有精确发布顺序和 callback 地址，回调 body 仍必须由后续函数等价类和语义
slice 独立闭合。

## 5. 原生证据来源

当前 316 条来自以下已经完成的四二进制报告和地址表：

- `analysis/motionplayer_player_ncb_surface_and_constructor_four_binary_2026-08-26.md`
  与 `analysis/motionplayer_player_ncb_surface.tsv`；
- `analysis/motionplayer_layergetter_ncb_surface_and_constructor_four_binary_2026-08-26.md`；
- `analysis/motionplayer_geometry_ncb_registration_surface_four_binary_2026-08-26.md`；
- `analysis/motionplayer_motion_class_registration_surface_four_binary_2026-08-26.md`；
- `analysis/motionplayer_motion_alpha_mask_d3d_available_four_binary_2026-08-27.md`；
- `analysis/motionplayer_separate_layer_ncb_surface_four_binary_2026-08-27.md`；
- `analysis/motionplayer_separate_layer_target_layer_property_four_binary_2026-08-27.md`；
- `analysis/motionplayer_separate_layer_clear_destructor_four_binary_2026-08-27.md`；
- `analysis/motionplayer_separate_layer_assign_four_binary_2026-08-27.md`；
- `analysis/motionplayer_sourcecache_objsource_ncb_surface_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_ncb_surface_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_layer_id_set_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_random_dispatch_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_unload_all_map_clear_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_unload_single_node_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_load_cache_validate_dispatch_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_is_exist_motion_direct_fallback_scan_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_find_motion_dispatch_array_owner_four_binary_2026-08-27.md`；
- `analysis/motionplayer_resourcemanager_find_source_blank_objsource_owner_four_binary_2026-08-27.md`；
- `analysis/motionplayer_sourcecache_load_clear_buflayer_four_binary_2026-08-27.md`；
- `analysis/motionplayer_objsource_getters_clip_draw_decode_texture_lifetime_four_binary_2026-08-27.md`；
- `analysis/motionplayer_d3dadaptor_ncb_surface_four_binary_2026-08-27.md`；
- `analysis/motionplayer_d3dadaptor_simple_state_map_clear_four_binary_2026-08-27.md`；
- `analysis/motionplayer_d3dadaptor_capture_canvas_four_binary_2026-08-27.md`；
- `analysis/motionplayer_bezier_layer_extensions_ncb_surface_four_binary_2026-08-27.md`；
- `analysis/motionplayer_bezierpatch_methods_geometry_inverse_four_binary_2026-08-27.md`；
- `analysis/motionplayer_layer_extensions_callbacks_lifetime_render_four_binary_2026-08-27.md`；
- `analysis/motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-27.md` 与
  `analysis/motionplayer_emoteplayer_ncb_surface.tsv`。
- `analysis/motionplayer_emoteplayer_scale_trigger_variablekeys_animating_four_binary_2026-08-27.md`。
- `analysis/motionplayer_emoteplayer_player_facade_properties_methods_four_binary_2026-08-27.md`。
- `analysis/motionplayer_emoteplayer_timeline_selector_queries_four_binary_2026-08-27.md`。
- `analysis/motionplayer_emoteplayer_primary_flow_raw_setters_four_binary_2026-08-27.md`。
- `analysis/motionplayer_raw_label_resolver_caller_closure_four_binary_2026-08-27.md`。
- `analysis/motionplayer_player_child_visitor_exception_frontiers_four_binary_2026-08-27.md`。
- `analysis/motionplayer_tjs_array_dictionary_exception_frontiers_four_binary_2026-08-27.md`。
- `analysis/motionplayer_layergetter_quad_exception_frontiers_four_binary_2026-08-27.md`。
- `analysis/motionplayer_motionnode_source_order_four_binary_2026-08-27.md` 与
  `analysis/motionplayer_layergetter_scalar_string_getters_four_binary_2026-08-26.md`。
- `analysis/motionplayer_geometry_default_constructors_four_binary_2026-08-26.md`。
- `analysis/motionplayer_geometry_scalar_getters_four_binary_2026-08-26.md`。
- `analysis/motionplayer_geometry_contains_boundary_four_binary_2026-08-26.md`。

生成器从这些已经审计过的证据源抽取地址，而不是猜测缺失类的 native 顺序。四端
IDB 仍然是最终权威；报告只承担可复查的证据索引。

## 6. 注册面闭合后的最终 disposition

NCB 候选注册面没有剩余 owner，`BODY_PENDING_SEPARATE_SLICE` 为零。表内 78 个
`OUT_OF_SCOPE_FOR_SURFACE_SLICE` 是生成器刻意保留的职责分离：它表示注册面报告本身不冒充
callback body证据，不表示这些 body仍未完成。它们已经由 Player C11-C32、EmotePlayer L14、
Resource/Source L12-L13、D3D/renderer R14/G11和各对象生命周期 companion rows闭合。

NCB 之外由模块回调、vtable、函数指针、静态析构和 renderer/resource 根可达的 helper，
以及对象 owner、容器、异常清理、目标差异和输入边界，也已经由
`motionplayer_root_reachable_denominator_final_audit_2026-08-27.md` 完成最终分母审计。
生成账本仍保留 surface/body 两个维度，不把历史分类机械重写成一张失去证据作用域的平面表。

## 7. 校验结果

- 两个生成器通过 `python3 -m py_compile`；
- TSV 解析得到 316 / 316 条数据行，字段数分别为 18 / 12；
- candidate ID 唯一，原生证据无孤儿；
- 所有 316 条证据均有四个平台字段；
- 合并时对象内 sequence 与 script name 完全相同；
- 所有字段均无嵌入换行、回车或制表符；
- 从临时输出重生成后可与受控生成物逐字节比较；
- `git diff --check` 通过。

当前环境缺少 CMake、Ninja 和 Emscripten，且单头文件语法检查被缺失的
`boost/locale.hpp` 阻塞，因此本 slice 不宣称完成正式 native/Web 构建。
