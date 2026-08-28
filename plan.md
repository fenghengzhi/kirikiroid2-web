# Motionplayer 四参考二进制完整恢复计划

本计划的完成条件不是“本地功能可用”或“新增若干反编译报告”，而是从
`motionplayer.dll` / `emoteplayer.dll` 四端根入口建立完整覆盖分母，并让每个根可达
函数、对象、容器、owner 边、脚本成员和边界行为都有可审计的四端 disposition。

权威覆盖表：`analysis/motionplayer_coverage.tsv`。

## 当前阶段

- [x] MP-F01：固定模块根、依赖闭包与排除规则；
- [x] MP-F02：核对四个目标和四个配套 IDB；
- [x] MP-F03：NCB 316/316 与非 NCB 根可达任务分母已经按 `tasks.md` 重建；
- [x] MP-F04：16 个 MP-L 与 16 个 MP-C 对象、vtable、容器和 owner 任务全部有直接四端映射；
- [x] MP-F05：163 个原始 ticket 全部进入权威状态台账，旧聚合过度结论已被逐项账本取代；
- [x] MP-F06：逐 ticket 映射本地实现、报告和现有测试/无物料决定；
- [x] MP-F07：当前真实差异集已经按共享语义、平台、ABI/compiler、验证和证据阻塞分类；
- [x] MP-F08：163 个 ticket 已统一收口为 `CLOSED_STATIC`、`VERIFIED` 或明确的
  `EVIDENCE_BLOCKED`，不存在含糊的 partial/open 状态；
- [x] MP-A、MP-L、MP-C、MP-D、MP-R、MP-G、MP-B：139 个业务/边界原始任务全部达到
  `CLOSED_STATIC`，通过当前生成器计算的 306 个 task-slice 关联覆盖 159 个唯一四端终态 slice；
- [x] MP-V01、V02、V06、V09～V13：验证映射、fixture 决策、Web Debug 构建和横向静态审计完成；
- [x] MP-V03～V05：V04 已完成 ADB/native/Wasmtime 25+63 帧零差异；V03 当前 Wasmtime scalar
  21/21；V05 当前 Wasmtime render 88 帧/1472 event/176 PNG 已验证，GitHub run 540 同 fixture 的
  15 Hz paired render-step 也成功，但当前工作树的 draw-dispatch 跨 lane 尚未完成，两个真实缺口均以
  终态 `EVIDENCE_BLOCKED` 明确记录；
- [x] MP-V07/V08/V14：native suite 完整运行，357 个 case 中 356 pass、1 expected skip；declared
  order 23259/23259 assertions，原失败随机 seed `2862347432` 为 23260/23260；
- [x] MP-V15/V16：最终差异清单和恢复报告已经发布并更新到最终 terminal dispositions。

当前权威统计：163 个任务中 146 个 `CLOSED_STATIC`、15 个 `VERIFIED`、2 个
`EVIDENCE_BLOCKED`，无 partial/open 行。详见 `analysis/motionplayer_tasks_status.tsv`。

## 状态定义

- `UNMAPPED`：尚未建立四端函数/数据映射；
- `MAPPED_4_4`：四端均已有地址或有证据的 inline/dead-strip disposition；
- `EVIDENCED_4_4`：本轮已取得四端 fresh decompile/disasm/xref 证据并写出共同伪代码；
- `IMPLEMENTED`：本地实现已逐行对照联合证据；
- `VERIFIED`：已用当前存在的 unit/differential/runtime/build 手段验证；
- `PLATFORM_BOUNDARY`：有具体技术原因证明无法直接复刻；
- `EVIDENCE_BLOCKED`：任务要求的直接证据因明确的外部输入/执行条件而不可取得；这是终态但不是
  PASS，禁止把历史结果、自比较或间接证据冒充当前验证，也禁止据此推导产品运行语义。

只有根可达总账无未解释条目、所有六维恢复要求均有当前证据，且最终验证与平台
边界审计完成后，整个目标才能标记完成。

## NCB 注册面子台账（2026-08-27）

- [x] 生成稳定的 316 条本地候选分母；
- [x] 将现有 `Player`、`Motion`、`LayerGetter`、几何类和
  `SeparateLayerAdaptor` 四端证据合并为初始 186 条映射；
- [x] 保留注册状态与回调 body 状态的独立维度；
- [x] 闭合 `SourceCache` 4 条与 `ObjSource` 7 条，累计 197 条映射；
- [x] 闭合 `ResourceManager` 13 条，累计 210 条映射；
- [x] 逐体闭合 `ResourceManager::requireLayerId/releaseLayerId` 的 ordered uint32 set、
  sentinel/counter 生命周期、wrap、suffix erase、返回值和异常边；
- [x] 闭合 `ResourceManager::random` 的持久 TJS RandomGenerator receiver、零参数 dispatch、
  ignored status、Variant Real转换、hint与异常owner；
- [x] 闭合 `ResourceManager::unloadAll` 的纯 module-map clear、outer bucket 保留、
  `KRKR -> Win -> PSB` value owner逆序析构和inner texture Release链；
- [x] 闭合 `ResourceManager::unload` 的 placed-path normalization、ttstr hash/equality miss、
  单module node unlink、owner析构、bucket保留和STL异常次序；
- [x] 闭合 `ResourceManager::load` 的cache hit/miss、storage/MDF/Adopt数据流、严格
  `id/spec/version` 校验、粘滞spec、`operator[]` 异常owner和每次fresh dispatch生命周期；
- [x] 闭合 `ResourceManager::isExistMotion` 的未检查query split、UTF-8动态key、String project
  定向查找、全map回退、strict/contains raw PSB导航、临时owner与异常边；
- [x] 闭合 `ResourceManager::findMotion` 的同构direct/full-scan、final raw node、fresh dispatch、
  `[dispatch, actual module key]` Array、deque实现和first-emplace前泄漏边；
- [x] 闭合 `ResourceManager::findSource` 的`src/blank`精确分流、UTF-16键核验、raw source/icon
  导航、texture-null ObjSource adaptor、Dictionary String/Integer字段和失败泄漏边；
- [x] 闭合 `SourceCache::loadSource/clearCache/bufLayer` 的复合identity、命中/变色/miss、
  pre-insert trim、bake/packed tint、persistent scratch Layer、`std::list<Entry>` owner、
  public clear partial commit 和 ResourceManager 同址callback复用；
- [x] 闭合 `ObjSource` 六个 getter/draw callback 的 strict/category 边界、fresh clip
  Dictionary、完整 UTF-16LE `left/top/right/bottom`、lazy texture、RL8/RL32、palette/BGRA、
  Bitmap/Texture 发布、异常泄漏和 adaptor/PSB owner 析构次序；
- [x] 闭合 Motion namespace `doAlphaMaskOperation/getD3DAvailable` 的 11 参数 wrapper、
  clip/空交集惰性源对象、software/GPU alpha/threshold 操作矩阵、四 strip 清零、静态
  render-method owner、非空无效 mode/op 的 update 边界和 software-renderer 逻辑取反；
- [x] 逐体闭合 `BezierPatch` 八个静态 callback 的 unsigned pair walk、probe/strict
  coordinate 读取、fresh Array/Dictionary、10×10 tessellation、重复 left 写、U/V
  浮点结合顺序、原版未初始化 accumulator、反向 triangle/affine 扫描和退化/NaN owner 边；
- [x] 逐体闭合 `MotionLayerExtensions_guess` 九个 attached callback 的 per-Layer lazy
  payload、debug Variant CopyRef/assignment、face/type/mode 矩阵、clear、plain/Bezier
  submit/update/debug 顺序、nested Array/frame orientation、负 division 和异常 partial commit；
- [x] 闭合 `D3DAdaptor` 16 条，累计 226 条映射；
- [x] 逐体闭合 `D3DAdaptor` 14 条简单状态、兼容空操作和纹理 map clear；仅
  `captureCanvas` 保留为独立 body slice；
- [x] 闭合 `D3DAdaptor::captureCanvas` software row-copy、GPU texture-swap、Layer helper
  和异常所有权边界；D3DAdaptor 15 个非 Factory callback body 全部闭合；
- [x] 闭合 `D3DAdaptor` 唯一参数构造、构造失败泄漏边、析构 reverse release、进程
  shared raw owner 与 clear 的双静态 guard；删除非参考 default/initialize API；
- [x] 恢复 Motion 独立 guarded `opengl` manager root，修正 D3DAdaptor、D3D batch、
  stencil、method selector 与 Private GLL 的默认/私有 renderer 误路由；闭合 render envelope；
- [x] 闭合 D3D source getter 的 atlas/fallback/software 分流、raw-key/intrusive-holder
  红黑树 lookup/emplace、Android libstdc++ 与 iOS libc++ 节点构造差异，以及命中、清空、
  分配失败和 null factory 返回的引用边界；
- [x] 闭合 shared D3D deep renderer 的 stencil 编号/clip 预处理、逐 item admission、
  两个 type-erased texture getter、method cache、TriangleBatch key/flush、GL stencil 状态与
  异常 owner；恢复 live target-pair getter、overflow message box 和 D3DLayer owner 边；
- [x] 闭合公共 mesh submit 的手工 source 引用、software repeat bitmap、source 网格、
  outer/cell admission、selected-cell 容器、六顶点绕序、callback envelope和异常泄漏边；
- [x] 闭合相邻 Bezier basis cache 与 patch tessellation helper 的四端容器、边界和owner；
- [x] 闭合 direct D3D/canvas 共用的 prepared-item camera offset 与 stereovision projection；
- [x] 闭合 `prepareRenderItems` wrapper、递归builder调用边界与 stable-sort owner/EH；
- [x] 深闭合 `appendPreparedRenderItems` 递归builder主体的node分流、owner/container与partial-commit边；
- [x] 闭合 MotionNode/PreparedRenderItem 最终析构、deque erase/replacement 与唯一释放生命周期；
- [x] 闭合 `BezierPatch` 8 条与 `MotionLayerExtensions_guess` 9 条，累计 243 条映射；
- [x] 闭合 `EmotePlayer` 73 条，累计 316 条映射，NCB 注册候选全部闭合。

明细见 `analysis/motionplayer_ncb_equivalence.tsv` 和
`analysis/motionplayer_ncb_equivalence_ledger_2026-08-27.md`。NCB 文件本身仍刻意把
注册面状态与 body slice 状态分开；其余函数、对象、容器和 owner 分母已经由 coverage
companion rows与最终根可达审计闭合。

## 阶段性审计与纠正（2026-08-27）

- [x] 闭合 `frameProgress`、四流 cursor、variable/node 双槽与 event dispatcher；
- [x] 由 UTF-16LE 类名和 vtable data pointer补齐私有 `__Private_Motion_GLLayer`
  registrar、factory、六个 callback 与 `Draw_GPU`；
- [x] 枚举共享 Layer factory的全部 SourceCache/SLA/materializer/composed-group caller；
- [x] 闭合 SLA Void shell和任意 `targetLayer` 的全部 native consumer与尖锐边界；
- [x] 将现有coverage逐项映射到 `tasks.md` 163项，不再以聚合slice替代原始分母；
- [x] 保留 `MP-B11-PLAYER-CURSOR-FP` 与 `MP-B11-PLAYER-SKIP-FP` 两个已知
  四端指令级平台边界，但不再声称它们是唯一剩余项；
- [x] 建立 `analysis/motionplayer_tasks_status.tsv` 和可重复生成器；
- [x] 就地纠正原“最终根可达闭合”过度结论。

## 最终验证与证据阻塞（2026-08-29）

- Web Debug 已完成正式 CMake configure、全量 compile 和 final link，五个固定产物已记录
  size 与 SHA-256；
- macOS native `motionplayer-dll` 已完成最终重编译、签名和全 suite 执行：357 cases，356 pass，
  1 expected live-OpenGL skip；declared order 23259/23259 assertions，原失败随机 seed
  `2862347432` 为 23260/23260；
- 最终重编译的 `motionplayer-ttstr-hash-test` 为 23 cases / 150 assertions 全过；
- 原生 IDA transport 已恢复；DrawDeviceD3D bool result、EmotePlayer Boolean fixture 前置状态和
  `Motion.EmotePlayer` Factory arg0 gate 均完成四端 fresh decompile/disassembly、IDB 注释/书签/保存、
  实现或 fixture 纠偏和最终回归；
- 两份 hash-pinned 15 Hz XP3 已取得并校验；当前 Wasmtime 与新鲜 macOS native 均对 Android
  playback oracle 完成 m2logo 25 帧、yuzulogo 63 帧零差异，MP-V04 为 `VERIFIED`；
- 当前 Wasmtime render-stage 两 case 共 88 帧、1472 event、176 PNG，manifest/schema/frame/hash
  审计全部通过；GitHub run 540 在 `a514c688...` 使用相同两份 hash-pinned 15 Hz fixture，保留了
  Android/Wasmtime render artifact 且当时的 render-step compare 成功。它未调用 draw-dispatch comparator，
  最新基线 run 550 又没有 render artifact，所以当前工作树的完整 paired 结果仍为 `EVIDENCE_BLOCKED`；
- scalar Wasmtime 21/21 继续通过；当前 scalar ADB 0/21 因同一 Android 条件明确为
  `EVIDENCE_BLOCKED`，没有把历史 ADB 结果提升为当前 PASS；
- MP-V09～V15 的可重复横向审计见
  `analysis/motionplayer_v09_v15_crosscutting_final_audit_2026-08-28.md`；
- MP-V07/V08 的当前执行与诊断账本见
  `analysis/motionplayer_v07_v08_native_runtime_diagnostics_2026-08-28.md`。
- MP-V16 最终报告已发布并更新为
  `analysis/motionplayer_final_reconstruction_report_2026-08-28.md`；163 个任务全部是终态，两个
  Android 直接证据缺口保留为 `EVIDENCE_BLOCKED` 而非伪造的验证成功。
