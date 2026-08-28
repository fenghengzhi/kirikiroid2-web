可以，而且这类任务必须拆。只是不能简单按“一个 `.cpp` 一个任务”或“一个类一个任务”切，因为原始实现的调用链、所有权和容器往往横跨多个文件。更合适的单位是：

> 一个小任务 = 一个可闭合的语义纵切面：从一个入口或状态变更出发，同时恢复它在四个二进制中的函数映射、共同控制流、数据流、所有权、容器行为、异常/边界行为，以及对应的本地实现和验证。

## 执行状态（2026-08-29）

这份拆分已经落地执行，权威逐任务状态在
[motionplayer_tasks_status.tsv](/Users/fenghengzhi/Developer/kirikiroid2-web/analysis/motionplayer_tasks_status.tsv)：

- 163 个唯一 ticket 已全部进入终态：146 个 `CLOSED_STATIC`、15 个 `VERIFIED`、2 个
  `EVIDENCE_BLOCKED`；139 个 MP-A/L/C/D/R/G/B 静态业务/边界任务全部闭合；
- 四参考目标、四配套 IDB、199 行 coverage、316 行 NCB 等价表、494 个注册契约和对象/容器/
  owner 分母均已建立并通过可重复生成器审计；
- MP-V06 Web Debug、MP-V07 原生/哈希回归、MP-V08 诊断、MP-V09～V15 横向审计和 MP-V16
  最终报告均已完成；
- MP-V04 已用两份 hash-pinned 15 Hz XP3 完成 Android oracle、当前 Wasmtime 和新鲜 macOS native
  三侧 25+63 帧差分，两个 port lane 均零 mismatch；MP-F08 因而完成最终状态收口；
- MP-V03 与 MP-V05 是明确的终态 `EVIDENCE_BLOCKED`，不是 PASS：前者缺当前 scalar ADB 返回值；
  后者已有完整 Wasmtime 88 帧/1472 event/176 PNG 产物，且 GitHub run 540 确有同两份 hash-pinned
  15 Hz fixture 的 Android/Wasmtime render artifact 并通过当时的 render-step compare，但该 run 位于
  `a514c688...`、未调用 draw-dispatch comparator，最新基线 run 550 也没有 render artifact。因此缺口是
  当前工作树的完整 paired render-step/draw-dispatch 复核，不是“Android partner 从未存在”。

最终证据、限制和可重复命令见
[motionplayer_final_reconstruction_report_2026-08-28.md](/Users/fenghengzhi/Developer/kirikiroid2-web/analysis/motionplayer_final_reconstruction_report_2026-08-28.md)。下面保留原任务拆分和 Definition of Done，作为覆盖分母的规范来源。

## 一、先固定任务范围

建议把“motionplayer”分成三个圈层，防止任务无限膨胀成“恢复整个 Kirikiroid2”：

1. 核心范围

   - `motionplayer.dll`
   - `emoteplayer.dll`
   - `Player`
   - `Motion.EmotePlayer`
   - `D3DEmotePlayer`
   - `EmoteEngine`
   - `ResourceManager`
   - `SourceCache`
   - `MotionNode` / `NodeTree`
   - controller、timeline、variable、particle、render pipeline
   - `SeparateLayerAdaptor`、`D3DAdaptor`、Layer 扩展

2. 依赖闭包

   只有被核心范围真实调用的共享代码才纳入，例如：

   - NCB/TJS dispatch 包装
   - `ttstr`、`tTJSVariant`
   - TJS Array/Dictionary
   - Layer/DrawDevice/Texture
   - Storage/Plugins.link
   - PSB raw-node 和资源解析
   - 与 motionplayer 渲染链直接相连的 Web/Cocos/OpenGL 适配

3. 排除范围

   FFmpeg、通用 UI、普通 movie player 等代码，除非四端调用图证明 motionplayer 的根入口确实可达，否则应放入独立恢复计划。文件或分析报告叫 `motionplayer_*` 本身不能证明它属于 motionplayer 调用闭包。

整体依赖关系可以压缩为：

```text
范围与覆盖总账
    ↓
模块注册与脚本 API
    ↓
对象模型、生命周期、容器
    ↓
资源加载与运行时状态机
    ↓
updateLayers、几何与渲染
    ↓
平台边界与端到端验证
    ↓
四端完整性审计
```

边界行为、异常回滚和验证不是最后再补的阶段，而是每个纵切面的必查维度。

## 二、基础设施任务：先建立“还原分母”

这些是第一优先级。没有它们，后面即使继续增加 V362、V363，也无法证明距离 100% 还有多远。

| ID | 小任务 | 产物与验收 |
|---|---|---|
| MP-F01 | 固定 motionplayer 根入口和范围边界 | 列出两个 NCB 模块的注册入口、静态初始化入口、类注册入口；明确依赖闭包规则和排除规则 |
| MP-F02 | 核对四端目标与 IDB 身份 | 四个二进制、四个 `.i64`、架构、imagebase、Hex-Rays 状态一一对应；任何缺失都阻塞后续取证 |
| MP-F03 | 建立四端函数等价类总账 | 每一行是一个语义实体，包含四端函数名/地址/状态：已定位、内联、dead-strip、缺失或待定位 |
| MP-F04 | 建立对象与类型总账 | 类、基类、vtable、构造/析构、字段族、容器、全局静态对象、owner/borrowed 边全部入账 |
| MP-F05 | 索引现有 649 份分析报告 | 把现有报告映射到函数/对象总账；区分已闭合、部分闭合、被后续证据纠正、重复和仅导航性报告 |
| MP-F06 | 建立本地实现映射 | 每个语义实体映射到本地文件、函数、测试和分析报告；没有对应实现时只能标为“待核实”，不能根据一次空搜索断言缺失 |
| MP-F07 | 建立差距生成规则 | 从总账生成待办：四端未定位、只取证未实现、实现无四端证据、生命周期不完整、容器选型不确定、边界未覆盖等 |
| MP-F08 | 清理完成状态定义 | 统一状态：`UNMAPPED`、`MAPPED_4_4`、`EVIDENCED_4_4`、`IMPLEMENTED`、`VERIFIED`、`PLATFORM_BOUNDARY`、`EVIDENCE_BLOCKED` |

建议总账至少包含这些列：

```text
slice_id
semantic_entity
root_reachability
android_arm64_function/address/status
android_armv7_function/address/status
ios_arm64_function/address/status
ios_armv7_function/address/status
common_source_pseudocode
difference_class
object/container/owner_edges
local_file/function
implementation_status
analysis_report
test_oracle
platform_boundary
idb_changes_saved
remaining_gap
```

`V361` 这种序号只能表示工作历史；完成率应由这张总账计算。

## 三、模块注册和 TJS/NCB 脚本表面

本地 [main.cpp](/Users/fenghengzhi/Developer/kirikiroid2-web/cpp/plugins/motionplayer/main.cpp:46) 中 `Player` 有约 92 个注册成员，`EmotePlayer` 也有大量兼容入口。不能把“检查 Player 注册”作为一个任务，应按成员家族拆开。

### 模块级任务

| ID | 小任务 |
|---|---|
| MP-A01 | 恢复 `motionplayer.dll` 静态 registrar、模块注册、重复注册和卸载边界 |
| MP-A02 | 恢复 `emoteplayer.dll` 预注册回调、模块依赖、注册顺序和失败边界 |
| MP-A03 | 恢复模块名大小写处理、`Plugins.link`、autoload 和已注册模块集合关系 |
| MP-A04 | 穷举二进制中两个模块实际注册的类、常量、属性、方法及精确字符串 |
| MP-A05 | 对比本地注册表，找出多注册、少注册、错误绑定、顺序差异和 dead-strip 成员 |

### `motionplayer.dll` 类表面任务

| ID | 小任务 |
|---|---|
| MP-A06 | `BezierPatch` 附着到 `Layer` 的方法表和包装 ABI |
| MP-A07 | `MotionLayerExtensions` 的注册、hook、附着对象与生命周期 |
| MP-A08 | `SourceCache`、`ObjSource` 的类注册、工厂和返回对象所有权 |
| MP-A09 | `Point`、`Circle`、`Rect`、`Quad` 的构造、属性和 `contains` 边界 |
| MP-A10 | `LayerGetter` 的完整只读属性表、Variant/Array 返回所有权 |
| MP-A11 | `SeparateLayerAdaptor` 的注册、构造参数、属性和 `clear` |
| MP-A12 | `D3DAdaptor` 的公开方法、属性、画布捕获和纹理缓存入口 |
| MP-A13 | `ResourceManager` 的 load/unload/find/cache/layer-id API |
| MP-A14 | `Motion` 类常量、静态入口、工厂以及命名空间层级 |

### `Player` 表面任务

按 [main.cpp 的注册顺序](/Users/fenghengzhi/Developer/kirikiroid2-web/cpp/plugins/motionplayer/main.cpp:269) 切成以下任务：

| ID | 成员族 |
|---|---|
| MP-A15 | 默认配置、`resourceManager`、时间和 `variableKeys`，约 #1–#6 |
| MP-A16 | `chara`、`motion`、stealth 槽、tags、project、completion/preview/priorDraw，约 #7–#17 |
| MP-A17 | mesh、speed、sync、tick、camera、outline、mask、color、transform 配置，约 #18–#45 |
| MP-A18 | 坐标、flip、opacity、visible、slant、zoom、D3D 和 pixelate，约 #46–#65 |
| MP-A19 | variable、play/progress/clear/stop/draw 的 raw/typed wrapper，约 #66–#79 |
| MP-A20 | contains、view、command/layer 查询、sync/action callbacks、motion 查找，约 #80–#92 |
| MP-A21 | `Player` 构造包装：参数数量、默认值、receiver、native-instance publication 和失败回滚 |
| MP-A22 | `Player` 所有属性 setter 的精确 TJS 状态返回、Void/Null 转换和异常前缀 |
| MP-A23 | `Player` raw callback 的 `objthis`、`result`、arity、hint、临时 Variant 生命周期 |

### `Motion.EmotePlayer` 和 D3D 表面任务

| ID | 小任务 |
|---|---|
| MP-A24 | progress/draw/pass/play/clear/contains/wind 基础入口 |
| MP-A25 | setVariable/setCoord/setScale/setRotate/setColor/setOuterForce 兼容 callback |
| MP-A26 | motion/project/mask/mesh/outline/priorDraw 和时间/边界属性 |
| MP-A27 | camera offset、root modify、hair/parts/bust scale |
| MP-A28 | debug/queuing/directEdit/selector 和 `variableKeys` |
| MP-A29 | controller、timeline、variable range/frame list、command list 后半表面 |
| MP-A30 | `D3DEmotePlayer` 独立类层级、typed factory、clone 和原版故意保留的 TODO 行为 |
| MP-A31 | D3DLayer、D3DImage、D3DPicture、DrawDeviceD3D 等相关 NCB 类表面 |
| MP-A32 | 四端所有注册字符串、精确大小写、默认参数和绑定目标最终对账 |

## 四、对象结构和生命周期任务

这里恢复的是共享源码中的字段顺序、继承和 owner 关系，不是用 padding 强行对齐某个 ABI 的字节偏移。

| ID | 小任务 |
|---|---|
| MP-L01 | `Player` 构造函数：成员初始化顺序、默认值、故意未初始化字段 |
| MP-L02 | `Player` 普通析构：owner 逆序、容器析构、外部 listener/cache 解绑 |
| MP-L03 | `Player` 构造失败：每个 publication frontier 的异常回滚 |
| MP-L04 | `Player` vtable、虚函数表面、deleting destructor 和可能的多继承 thunk |
| MP-L05 | `EmoteObject → EmoteEngine → Player` 完整 owner 链 |
| MP-L06 | `Motion.EmotePlayer` facade 与 `D3DEmotePlayer` shell 的不同 owner 拓扑 |
| MP-L07 | `EmoteEngine` 七个 direct-controller owner 的构造、替换和析构 |
| MP-L08 | controller 容器元素内部的 heap owner、borrowed resolver 和 publication 顺序 |
| MP-L09 | `MotionNode` 的构造、浅/深复制、赋值、erase 和 type-3 child owner |
| MP-L10 | `NodeTree` 新树构造、旧树退休、替换、遍历和失败回滚 |
| MP-L11 | `ResourceManager`、`SourceCache`、`ObjSource` 的 owner/cache/raw holder 关系 |
| MP-L12 | `SeparateLayerAdaptor` 的 target owner、pass 状态和 teardown |
| MP-L13 | D3D adaptor、D3D layer listener、texture map 和 managed set 生命周期 |
| MP-L14 | clone/serialize/unserialize/assignState 的临时 owner 和部分提交边界 |
| MP-L15 | global RNG、static method cache、texture cache、registrar/static guard 的生命周期 |
| MP-L16 | AddRef/Release、TJS native instance、borrowed pointer 与 deleting destructor 总审计 |

每个生命周期任务都要输出一张 owner 图，例如：

```text
D3DEmotePlayer shell
    owns → EmoteObject
        owns → EmoteEngine
            owns → Player
            owns → controller family
            borrows/uses → ResourceManager / render adaptor
```

每条边必须标明：

- raw owner、unique owner、refcount owner 还是 borrowed；
- 在什么语句后正式发布；
- 构造失败由谁清理；
- 普通析构顺序；
- clone/copy 是否共享；
- reentrant callback 时是否仍然有效。

## 五、内部容器恢复任务

容器不能只在对象布局任务里顺带写一句。必须分别恢复构造点、元素类型、增长/删除、异常和边界，因为消费循环里的 `size()-1` 可能只是 STL 内联展开。

| ID | 小任务 |
|---|---|
| MP-C01 | `Player` node 容器、node-index 辅助结构和遍历顺序 |
| MP-C02 | `Player` variable、parameter、range 和 frame-list 容器 |
| MP-C03 | timeline/track/cursor/play-log/state-map 容器 |
| MP-C04 | pending callback、action、sync、child-event 队列 |
| MP-C05 | particle、emitter、join/retired-child 容器 |
| MP-C06 | angle/var/loop controller 的 deque/vector 元素类型 |
| MP-C07 | eye/eyebrow/mouth 的 primary/secondary track 容器 |
| MP-C08 | selector、transition、spring、bust/hair/parts/wind 容器 |
| MP-C09 | prepared render item、render command、target 和 triangle batch 容器 |
| MP-C10 | `ResourceManager` motion/source/layer-id 容器 |
| MP-C11 | `SourceCache` path/source/texture/atlas cache 容器 |
| MP-C12 | D3D texture/background/caption/listener/manager 容器 |
| MP-C13 | TJS Array/Dictionary 创建、items 获取、push/erase 和 owner handoff |
| MP-C14 | `ttstr` hash/equality、null 与 allocated-empty key、duplicate-key 行为 |
| MP-C15 | 所有容器的 empty、duplicate、erase、迭代器失效、分配异常和部分提交审计 |
| MP-C16 | Android libstdc++ 与 iOS libc++ 展开的差异归因，反推共同源码容器选型 |

任务验收不能只写“参考二进制看起来像 deque”。至少要确认：

- 元素生产点；
- `push/emplace/insert/erase/clear` 的调用链；
- 元素复制/移动/析构；
- 空容器和越界路径；
- 分配失败时已经提交了什么；
- 四端差异是不是编译器/STL 展开，而不是源码差异。

## 六、资源与节点构建数据流

| ID | 小任务 |
|---|---|
| MP-D01 | storage 输入、placed path、project/motion key 和规范化规则 |
| MP-D02 | `ResourceManager::load/unload/unloadAll` 的完整状态转换 |
| MP-D03 | `findMotion` 的查询、fallback、结果 Variant 与 receiver |
| MP-D04 | `findSource` 的直接路径、fallback accessor、context 和部分提交 |
| MP-D05 | PSB/MTN 输入、filter/decrypt、raw-node 借用指针和 buffer 生命周期 |
| MP-D06 | PSB metadata 读取的 mask gate、默认值、类型转换和错误状态 |
| MP-D07 | source descriptor、path snapshot、source-state 的构造/复制/析构 |
| MP-D08 | image format、palette、R/B 交换、atlas 和 texture materialization |
| MP-D09 | source cache key、颜色/尺寸维度、命中与失效规则 |
| MP-D10 | raw motion → variable/timeline/node builders 的入口与 owner 传递 |
| MP-D11 | node tree build 的逐节点 publication、旧树退休和异常回滚 |
| MP-D12 | reload/clear/unload 与仍在使用的 node/render source 之间的边界 |
| MP-D13 | layer-id require/release 的后缀、重复、溢出和无效输入行为 |

## 七、运行时状态机任务

| ID | 小任务 |
|---|---|
| MP-R01 | `setMotion`、`setChara` 和 stealth pending/live 槽状态机 |
| MP-R02 | `load → play → stop → clear` 的完整主调用链 |
| MP-R03 | `frameProgress` 的时间采样、speed、loop、lastTime 和 sync 状态 |
| MP-R04 | ordinary/stealth play 的查找、发布、递归和失败前缀 |
| MP-R05 | variable-list 和 parameter 初始化 |
| MP-R06 | variable setter/getter/range/frame-list 脚本边界 |
| MP-R07 | variable track 的 absolute reseed、incremental step 和 merge |
| MP-R08 | timeline 构造、label/index 查询和初始化 commit |
| MP-R09 | timeline play/seek/pass/fade/stop/cursor 状态机 |
| MP-R10 | timeline 与 variable/opacity/transform payload 的求值边界 |
| MP-R11 | angle、var、loop controller 的构造、step、reset 和完成判定 |
| MP-R12 | blink/eyebrow/mouth controller 的 track、RNG 和 overshoot 行为 |
| MP-R13 | selector/transition 的借用关系、索引转换和持久状态 |
| MP-R14 | spring、bust、hair、parts scale 和外力状态 |
| MP-R15 | wind emitter、MT19937、宽度差异和停止条件 |
| MP-R16 | child-motion type-3 bridge、pending callback 合并和失效 |
| MP-R17 | action/sync/ground-correction 回调队列、顺序和重入 |
| MP-R18 | particle source、spawn、count trigger、child opacity 和回收 |
| MP-R19 | camera target/position/FOV/alive、camera offset 和 velocity |
| MP-R20 | root transform、visible、opacity、flip、slant、zoom 和 coordinate |
| MP-R21 | state serialize/restore、clone 和 incomplete-state 边界 |

例如 `MP-R03` 不只是“实现 frameProgress”，而应完整覆盖：

```text
TJS progress wrapper
  → Player::frameProgress
  → tick/speed/sync time calculation
  → timeline/variable/controller stepping
  → pending child/action/sync event publication
  → updateLayers gate
  → lastTime/loopTime/final state commit
```

若这条链太大，就按每个会抛异常或发布状态的 frontier 再拆成 `MP-R03a`、`MP-R03b`、`MP-R03c`。

## 八、updateLayers、几何和渲染任务

这部分应该严格按阶段和数据产品切，不要按本地拆分文件直接推断原始 translation unit。

| ID | 小任务 |
|---|---|
| MP-G01 | `updateLayers` dispatcher 的精确阶段顺序、早退和状态 reset |
| MP-G02 | phase 1：raw node/property/frame 求值 |
| MP-G03 | phase 2：父子传播、join、active/retired tree 处理 |
| MP-G04 | phase 3：camera constraint 和 camera node |
| MP-G05 | phase 3：root/owner/primary/local transform 组合顺序 |
| MP-G06 | phase 3：anchor feedback 和尺寸反馈 |
| MP-G07 | phase 3：particle/emitter 更新 |
| MP-G08 | mesh point、vertex mesh 和 ancestor deformation |
| MP-G09 | Bezier basis、patch tessellation、bounds 和 reverse calculation |
| MP-G10 | shape、AABB、circle/rect/quad 和 `contains` |
| MP-G11 | clip、mask、stencil、opacity、color-weight 和可见性裁剪 |
| MP-G12 | `calcBounds` 的各 node type、child 和 particle 分支 |
| MP-G13 | prepared render item 的构造、优先级、duplicate 和 stencil 关系 |
| MP-G14 | render command list 的生成、排序、target 和 clip |
| MP-G15 | render source 查找、texture key、cache 命中和 materialization |
| MP-G16 | primary layer target 的按需创建和发布 |
| MP-G17 | Separate Layer Adaptor 的 accurate/non-accurate 两条路径 |
| MP-G18 | D3D adaptor target、texture map、canvas capture 和清理 |
| MP-G19 | software/GL backend 的 mesh、Bezier、texture 和 blend 调用 |
| MP-G20 | stencil composite、alpha mask、clear target 和缓存方法 |
| MP-G21 | canvas submit、post-draw update、retired item 和异常清理 |
| MP-G22 | 完整坐标链：PSB → ownerLayer → primaryLayer → paintBox → screen |
| MP-G23 | Web/Cocos 与参考渲染栈之间不可避免的平台边界 |
| MP-G24 | 一帧完整调用链与数据产品快照：input state → final draw calls |

`MP-G23` 只有明确的技术不可能性才能标为平台边界，例如本地渲染 API 确实无法表达参考实现的 per-vertex 状态；“现有 oracle 看不到”不能成为平台边界。

## 九、横向边界审计任务

这些任务在各纵切面完成后做第二遍交叉审计，专门防止“正常路径一致，但边界被 C++ 简化了”。

| ID | 小任务 |
|---|---|
| MP-B01 | Null、Void、缺失属性、失败 TJS status 和非对象 Variant |
| MP-B02 | 空容器、单元素、重复元素、负索引、末端索引和超大 count |
| MP-B03 | NaN、±Inf、`-0.0`、subnormal、除零和 unordered compare |
| MP-B04 | float/double → signed/unsigned int 的饱和、截断、wrap 和舍入 |
| MP-B05 | 字符串 null、allocated-empty、大小写、UTF-16LE 截断和 hash |
| MP-B06 | 异常发生在每个 setter/store/push/publication 之间时的部分提交 |
| MP-B07 | callback 重入导致 owner clear、容器增长或对象替换 |
| MP-B08 | alias 输出、同一对象作多个参数、borrowed reference 失效 |
| MP-B09 | static guard、全局 cache、RNG 和首次初始化并发 |
| MP-B10 | 构造失败、析构重入、double release、zero-ref 和删除 thunk |
| MP-B11 | Android/iOS、arm64/armv7 的差异分类：平台、ABI、STL、编译器或未知 |
| MP-B12 | dead value、no-op AddRef/Release、未初始化局部和 inactive tail 复刻审计 |

## 十、验证与最终收口任务

证据是实现的阻塞项；现成验证物料不足不是拒绝忠实复刻的理由。

| ID | 小任务 |
|---|---|
| MP-V01 | 给每个已闭合纵切面映射现有 unit test |
| MP-V02 | 只用现有 fixture 补可表达的正常/边界测试；没有物料时记录验证缺口，不从零捏造 fixture |
| MP-V03 | 几何、Bezier、position、hit-test 的 Wasmtime/ADB 差分 |
| MP-V04 | motion playback 的 native/ADB/Wasmtime trace 差分 |
| MP-V05 | render-stage、draw-dispatch、render-step trace 差分 |
| MP-V06 | Web Debug 配置与构建 |
| MP-V07 | motionplayer 相关 unit-test TU 和运行时检查 |
| MP-V08 | `git diff --check`、诊断日志和非回归检查 |
| MP-V09 | `_guess` 全量审计：有二进制名称证据则纠正，没有则保留 |
| MP-V10 | 本地旧单目标裸地址、`Like_0x...`、旧 helper 名和过时注释审计 |
| MP-V11 | 四端 dead-strip/inline/缺失项审计，禁止把单次 negative search 当结论 |
| MP-V12 | 全函数覆盖审计：每个根可达函数都有四端 disposition |
| MP-V13 | 全对象覆盖审计：每个构造、析构、owner 边、容器和 vtable 都有 disposition |
| MP-V14 | 全脚本表面审计：注册字符串、arity、receiver、result、默认值和异常一致 |
| MP-V15 | 最终差异清单：共享源码、平台差异、ABI/编译器差异、明确平台边界、证据阻塞项 |
| MP-V16 | 发布最终恢复报告和可重复检查命令 |

## 十一、每个小任务统一的完成标准

每个会影响 C++ 语义的小任务必须满足以下 Definition of Done：

1. 有四行映射表

   - Android arm64-v8a
   - Android armv7
   - iOS arm64
   - iOS armv7

2. 本轮对四端重新取证

   - 已定位函数分别 fresh decompile；
   - 内联、dead-strip 或暂未定位时，有本轮 `find/find_bytes/xrefs/disasm` 证据；
   - 地址始终带所属二进制，不能写裸地址。

3. 写出共同源码伪代码

   - 控制流；
   - 数据流；
   - 所有中间变量和计算顺序；
   - 默认值；
   - 条件分支；
   - 容器操作；
   - owner/publication/cleanup。

4. 单列四端差异

   差异只能分类为：

   - 平台；
   - ABI；
   - STL/编译器；
   - 版本；
   - 尚未解释。

5. 与本地实现逐行对照

   不能从本地代码反推二进制；必须先写联合证据，再说明本地每一行如何对应。

6. 只实现证据支持的结构

   - 不用 `std::vector` 代替原版 TJS Array；
   - 不用 `shared_ptr` 简化原版 raw/refcount owner；
   - 不为“更安全”补初始化、回滚或范围检查；
   - 不用 padding/packing 硬对齐某个 ABI 的对象字节布局。

7. 写回证据

   - `analysis/*.md` 保存四端地址和差异；
   - IDB 中纠正类型、名称、局部变量和注释；
   - 四个 IDB 分别保存；
   - 编译源码中不写反编译地址。

8. 尽力验证

   - 相关 unit test；
   - 已有差分 fixture/oracle；
   - Web build；
   - `git diff --check`；
   - 无现成物料时明确记录验证缺口。

9. 没有未说明的剩余项

   任务可以以 `EVIDENCE_BLOCKED` 或 `PLATFORM_BOUNDARY` 结束，但不能用含糊的“以后再看”冒充完成。

## 十二、控制单个任务大小

为了让上面的任务真正“小”，建议加硬限制：

- 一个任务最多处理一个公开入口族、一个 owner 链或一个容器族。
- 最多包含 1–4 个非平凡共享源码函数体；简单 getter/setter wrapper 可以按同一字段家族合并。
- 每个参考二进制超过约 4 个主要函数体时，继续拆成 `a/b/c`。
- 一个任务中如果同时出现两个互不依赖的异常回滚 frontier，应拆开。
- 一个任务只产生一份主分析报告。
- 一次代码改动只闭合一个语义纵切面。
- “恢复整个 `Player`”“恢复全部 rendering”“恢复所有容器”都不能作为单个任务。
- 只做证据盘点、不改代码也是合法且完整的小任务。
- 构建通过不等于该纵切面完成；oracle 不可观察也不等于任务价值低。

## 十三、推荐实际执行顺序

第一波先做：

1. MP-F01：固定范围。
2. MP-F03：创建函数等价类总账。
3. MP-F04：创建对象/容器/owner 总账。
4. MP-F05：把现有分析报告导入总账。
5. MP-F06：映射本地代码和测试。
6. MP-A01～MP-A05：完成两个模块的注册表分母。

第二波做：

1. MP-A15～MP-A32：脚本表面完整性。
2. MP-L01～MP-L16：对象和生命周期。
3. MP-C01～MP-C16：容器生产/消费链。

第三波做：

1. MP-D01～MP-D13：资源链。
2. MP-R01～MP-R21：运行时状态机。
3. MP-G01～MP-G24：update/render 链。

最后做：

1. MP-B01～MP-B12：横向边界审计。
2. MP-V09～MP-V15：名称、地址、覆盖率和最终差异审计。
3. MP-V16：最终报告。

基础总账完成后，脚本 API、资源链、controller、几何和渲染各组可以独立推进；但任何实现任务都必须仍然满足四文件 fresh 取证门槛。

## 十四、一个实际小任务的写法示例

例如不要写：

> 恢复 `frameProgress`。

应写成：

> `MP-R03a Player::frameProgress 的输入时间、speed/sync gate 和本地时间提交`

范围：

- 只恢复 wrapper 进入后，到 timeline/controller stepping 前的时间计算。
- 包含 tickCount、frameTickCount、speed、sync wait、lastTime/loopTime。
- 不包含 timeline、controller 和 render。

验收：

- 四端 wrapper 与 native body 映射齐全；
- 共同伪代码完整；
- 精确记录 float/double、整数转换、NaN、负时间和 wrap；
- 明确哪些字段在异常前已提交；
- 本地 [PlayerFrameProgress.cpp](/Users/fenghengzhi/Developer/kirikiroid2-web/cpp/plugins/motionplayer/PlayerFrameProgress.cpp:1) 逐行对照；
- 相关测试落在 [motionplayer-dll.cpp](/Users/fenghengzhi/Developer/kirikiroid2-web/tests/unit-tests/plugins/motionplayer-dll.cpp:1)；
- 四端 IDB 保存；
- Web build 或现有可运行检查完成。

后续再单独建立：

- `MP-R03b`：timeline/variable/controller stepping 顺序；
- `MP-R03c`：pending event、action/sync callback publication；
- `MP-R03d`：updateLayers/render gate 与最终状态提交；
- `MP-R03e`：异常、重入和边界矩阵。

这样每个任务既足够小，又不会丢掉“源代码结构、数据流、调用链、对象生命周期、内部容器和边界行为”中的任何一维。

这套拆分遵循了 `ida-decompile` 的四参考纵切面规则。执行后，MP-F01～F08 的覆盖、差距和最终
状态总账以及 MP-V16 最终报告都已落地；后续不应继续盲目追加新的 V 编号。MP-V03 仍需外部 Android
执行条件；MP-V05 应先通过已认证 GitHub 下载 run 540 的 15 Hz artifact，校验 schema 后与当前 capture
补跑 render-step/draw-dispatch comparator，若不兼容再重跑当前 pipeline。只有这些直接证据完成后才能
把对应 `EVIDENCE_BLOCKED` 提升为 `VERIFIED`。
