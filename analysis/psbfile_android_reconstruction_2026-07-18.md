# Android `PSBFile.dll` 复原审计（2026-07-18）

## 结论

当前不能宣称整个 Web 项目已经“尽可能 100% 一比一复原” Android
kirikiroid2 的 PSB 数据链。

截至 2026-07-19 当前工作树，结论仍为 **NO**：Web Debug 最终链接及完整
`motionplayer-dll`、独立 `psbfile-dll` 运行测试均已通过。Player 运行时的
`_activeMotion/shared_ptr<MotionSnapshot>` 双轨 owner 已在本轮删除，live motion 的加载、
门控、路径上下文、绘制与更新现在只由 Android 对应的 +528/+1012 raw Variant 驱动；
生产 `motionplayer` target 已不再编入或链接 decoded `MotionSnapshot` loader、
`PlayerFrameStep/PlayerFrameStepping` 兼容测试模型和 `psbfile_decoded_compat`；snapshot 类型、
decoded 帧/曲线/像素/字典 helper 也已整体移入 offline-only 头文件，生产头文件不再暴露
这张 eager 对象图。live `MotionNode::ClipSlot` 的 `src/icon` 已收束为 Android 同形的两只
`ttstr` owner；slot 的 `motionDtgt/action` 与 event deque 也已收束为原生
`ttstr/tTJSVariant` owner；CurveData 已从生产 slot/evaluator 删除，曲线求值改为即时读取
raw dispatch。节点级 `interpolatedCache`、生产 `FrameContentState` 与
`MotionNode::localState` 已整体删除；live evaluator 现按 `0x699AE4` 直接写
`accumulated/colorBytes/particleInterp`，slot opacity 也恢复为二进制的整数 `0..255`，并
复刻无 clamp ratio、`ti` unsigned-trunc 量化、双阈值缓存判断和 parameterEntry 直读。
source render cache 已删除，节点 label/path 字符串链已经改为原生 `ttstr` owner 和键空间。
`0x699AE4` 的 mesh 交叉帧、nodeType 5/10 类型专用输出及其 camera/anchor 消费链已在
live evaluator 中闭合；mesh 点容器也已依据 `0x692AB0/0x6996E8/0x69AC4C/0x69B1E8`
及几何、render-item、TJS serializer consumers，恢复为
`std::vector<{float x,float y}>` 的 8B 元素拓扑。当前结论仍为 NO 的含义是“尚不能证明
整个端到端链已经 100%”：MDF 失败路径和损坏 packed-table 分支缺现成 Android runtime
oracle；测试通过只能证明现有覆盖未回归，不能把未覆盖边界宣称为完全相等。

- 独立的 `psbfile` target 已从原先的 eager `PSBValue` 对象树改造成与
  `libkrkr2.so` 一致的 raw-buffer owner、二指针 node view、惰性 TJS dispatch
  和 `psb:` storage media；插件主体的函数覆盖已经接近完整。
- Web `motionplayer` 的 `ResourceManager` HashMap A 已改为 raw `PSBFile` holder，
  cache hit/miss、严格元数据校验、filter、unload 和每次新建 root dispatch 的主链已经
  复刻。raw load 后再次构造 eager `MotionSnapshot`、按 dispatch/path 登记两只全局
  强引用表的旁路已经删除；`findMotion` 的 `[raw motion, matchedKey]` 现在直接进入
  Player 的 +528/+1012 对应 owner。`psbfile_decoded_compat` 现在只由离线工具和显式测试
  target 读取，不再进入 Web 最终依赖图；
  Player 内只保留 +528 motion content 与 +1012 matched resource key 两个 raw owner，
  `_activeMotion`、`_motionsByKey`、路径候选回退和 native 直读旁路均已删除；节点树的形状、字段
  初始化、两槽帧求值及 stencil 后置 pass 已迁到 raw layer dispatch，Engine 时间线已迁回
  HM3/+1040/56B track deque 的 raw 状态机。先前额外持有 dispatch 的
  `lastLoadedModule/Path` 已删除。因此，插件模块主体已接近完整，但当前证据仍不足以把
  “插件模块自身”和“当前 Web 端到端运行链”合并成一个已证明的 100% 结论。
- `ResourceManager::setEmotePSBDecryptFunc` 的 callable 转换、共享闭包控制块、
  `CBinaryAccessor(data,size)` 参数、全局 filter 替换与旧闭包释放已经复刻；两个
  decrypt setter 也已从 ResourceManager 的 12-member registrar 移到
  `emoteplayer_entry` 对应的动态注入链。Player 活动路径标记及相关 render/timeline gate
  已闭合；`FrameContentState/localState` evaluator 栈中转也已删除；mesh 8B point 元素
  拓扑、crossfade 行为和 nodeType 5/10 类型输出亦已闭合。当前剩余的是现有真实资产/
  Android oracle 的验证缺口，而非已确认的 mesh 源码结构偏差。阻塞项不是 `interpolatedCache`、source cache、曲线解码镜像、节点 label/path 镜像、CMake 源传播、
  `src/icon` 双 owner、生产头文件泄漏、
  decrypt callable 桩、per-Player 缓存或全局 registry。
- `EmoteObject_init@0x67DBAC` 到 `EmoteObject_destroy@0x67F420` 的 RM 所有权也已
  从“值拷贝 RM + 另建一个共享 State 的 adaptor RM”纠正为单一堆分配 RM；sticky
  adaptor 指向同一对象，析构顺序恢复为 Engine → RM → path vector。RM 类内部的
  `shared_ptr<State>` 间接层也已删除；HashMap A、layer-id set/counter 现为 RM 内联成员。
  D3DEmotePlayer 壳层错误的 by-value RM 已改为二进制要求的 raw D3DImage owner；
  `0x530DA4/0x530DE8/0x533244/0x533CBC/0x533D4C/0x6D5C68` 又证明该链是
  listener list 的 add/remove、update 与 draw 两个虚槽。本地现在按“构造先注册、
  析构先拆两只 EmoteObject 再解注册”的顺序复原，owner 仍是不加引用的裸指针。
- Player 的 `motionKey`/`project` 已按 NCB 注册点 `0x6D6F58..0x6D7020` 纠正为
  同一 `player+1012` variant 的两个别名；getter `0x695BE0`、setter `0x6B4978`
  都只 CopyRef 这一格。Web 先前另造 `_project` 并把对象 dispatch 当作 snapshot
  激活入口的旁路已删除。该格现在只承担 `findMotion/findSource` 的匹配模块路径上下文；
  实际请求运动名仍由 binary `player+976` 对应状态承担。
- Player 的只读 `tags` 已按 `sub_6D9618@0x6D9618` 纠正为对
  `player+1072` raw `motion["tag"]` variant 的 CopyRef；独立 `_tags` 不再作为
  NCB 数据源。`Player_skipToSync@0x6D3504` 也已恢复为仅在
  `playing && loopTime < 0` 时枚举 raw tag 帧并执行原始 dead reads，然后写
  `queuing/firstFrame/frameTickCount/clampedEvalTime`；已删除该入口对 Web
  `_timelines` map、playing labels、syncWaiting 和 allplaying 的伪造改写。
- `Player_initNonEmoteMotion@0x6B365C` 的参数表也已从
  `MotionClip::motionObject` eager dictionary 迁回 `player+528` raw dispatch。
  `0x6B1718/0x6B202C/0x6B1ECC/0x6B1ABC` 证明参数项的 `id` 是
  `ttstr` owner，`parameterize` 仅有 Object 单项和 Integer 索引两条分支，
  `division` 使用 strict property-miss fallback，而 +408 multimap 在 play/init
  caller 中不会预清。本地已删除 `MotionClip::motionObject/contentObject`
  两只 decoded dictionary owner，以及 snapshot activation 中额外的参数容器预清。
- `EmoteObject_init@0x67DBAC` 的顶层初始化顺序已恢复为：顺序 load 全部路径、从最后
  一个 raw module 读取 `metadata/base/chara/motion`、把最后一个输入路径写入
  `player+1012`、应用 chara、以 Force=1 调 `Player_play`、最后应用完整 metadata。
  `Player::ensureMotionLoaded` 也先消费 `ResourceManager_findMotion@0x6A9ED4` 的真实
  `[raw motion dispatch, matched HashMap-A key]` 结果；`Player_playImpl@0x6B2284` 的
  `result[0]` 现由独立 `tTJSVariant` 按 +528 语义持有，`result[1]` 继续由 +1012
  对应格持有。`initVariables` 以及 `loopTime/lastTime` 已改从该 raw content dispatch
  读取，`tag/priority/priority[0].content` 也已建立三只独立 raw variant owner。
  tag/priority 的全部前进、后退与重定位消费现已直接读取这三只 raw owner，旧
  `MotionSnapshot::tagFrames/priorityFrames` 也已删除。全局 dispatch/path 映射和 eager
  二次解码已删除；活动 motion 不再生成 `MotionSnapshot` 标记，节点 `frameList` 求值及
  synthetic root 已经切断 snapshot。剩余 `MotionSnapshot` API 只服务离线 decoded 工具/
  兼容模型；其声明、实现及 decoded frame/resource helper 均已移入
  `OfflineMotionSnapshot.h/.cpp`，不再出现在生产公共头文件；source texture 的
  Win/spec=2 与 KRKR/spec=1 像素链已经全部切到 mapped record raw nodes；
  `SourceCache` 的非 atlas 路径也已切到 `ResourceManager.findSource → ObjSource →
  drawLayer(bufLayer)`，并删除 `_activeMotion`/`sourceCandidates` 像素旁路；
  D3DEmotePlayer 的五个变量枚举接口已恢复为 Android 的精确 TODO 异常边界，
  `EmotePlayer.getVariableRange/getVariableFrameList` 已分别切回 Engine HM5（miss 时递归
  扫 Player+384 参数表）与 Engine+1248 Dictionary，删除对应的 snapshot 查询面；
  `Motion.Player.variableKeys@0x6D139C` 也已改为每次从 Player+1296
  `std::deque<VariableLabelScope>` 的 `cascadeKey` 新建 TJS Array，删除 snapshot label
  缓存、伪 setter 及其额外 owner；`Player_updateLayers@0x6BB33C` 的变量阶段也已恢复
  对 `Player_interpolateVarTrackValues@0x6BBE20` 的无条件调用，不再从 snapshot
  `variableFrames` 取首帧；已无消费者的 snapshot frame/range 派生表已删除；
  metadata controller builder 与 Engine timeline 已不再读 snapshot，顶层也不再直接
  `lookupModuleSnapshot(lastLoaded)`。
- `Player_play@0x6B21E8`、`Player_playImpl@0x6B2284`、chara 四只 NCB
  accessor 与 child/particle caller 的联合复核已恢复六个独立 string-value owner：
  live chara/stealthChara/motion/stealthMotion 对应 +960/+968/+976/+984，pending
  stealthMotion/stealthChara 对应 +768/+776；primary chara 双写、same-motion gate、
  `AsCan && playing`、Join、失败清理及两个 pending flush 均走二进制同形调用链。
  +1099 已由 `Player_getPlaying@0x6D9794` 确认为 playing，不再沿用旧文档的
  motion-loaded 命名。
- `Player+1092` 的全函数区 AArch64 位移扫描枚举出 23 个直接访问点：只有
  `Player_ctor@0x6CF0A4` 与 `Player_setPreview@0x6D9640` 写入，
  `Player_getPreview@0x6D9638` 及节点初始化、帧求值、相机、可见性、child-motion、
  粒子、渲染列表和 bounds 路径全部读取。它是 NCB `preview` 属性，不是
  motion `type` 或 emote-mode 字节。本地已删除由 decoded
  `MotionSnapshot::root["type"]` 驱动的伪 `_isEmoteMode` owner；所有 +1092
  消费者统一读取 `_preview`，而 `Player_updateLayers_childMotionPass@0x6BEA90`
  对子 Player 的独立 `+482` 条件恢复为 `_directEdit`。这同时纠正了旧
  cluster-L 审计中 “+1092=isEmoteMode-ish” 的错误记录。
- `Player_playImpl@0x6B2284` 的 raw `motion["type"]` 三分支及
  `Player_initEmoteMotion@0x6B2E90` 已恢复：type 1 保存/清零 root angle、持有
  `division`(+484) 与 `motionList`(+508)、以 +504 记录选中项并加载二级 motion；
  type 0 恢复 root angle 后进入 non-emote init；其他非零 type 不初始化。
  `setAngleDeg/setAngleRad`、progress、child-motion、particle transform/spawn/step 的
  9 个 binary caller 也已逐个接回。此前 child-motion 把 +472 cameraAngle 传播误写成
  zFactor 的实现与注释已纠正。
- `Player_loadMotion@0x6B0F10`、`Player_playImpl@0x6B2284`、
  `Player_initNonEmoteMotion@0x6B365C`、`Player_buildNodeTree@0x6B51F0`
  的联合复核确认：Android 全链只持有并消费 +528 raw TJS dispatch，不构造文件级
  `MotionClip`、decoded `layerList` 或递归 `sourceCandidates` 图。本地已删除
  `MotionClip` 类型、clip 两张索引表、三组 decoded vectors、`_activeClip`、
  `selectActiveClip/activeSourceCandidates/activeClipTime` 及相关诊断；
  `lastTime/loopTime` 只保留 Player 自身的原版标量，不再写入 decoded timeline
  兼容表。历史 DRACU 的
  `(owner,label)` clip-key 修补已被 raw `motion/<chara>/<motion>` 路径导航取代。
- TJS typed class 注册、`PSBFile` 脚本构造和 `psb:` media 缓存路径已用现有真实
  `ezsave.pimg` 覆盖：测试走 `AllRegist → LoadModule("PSBFile.dll") → initPsbFile →
  singleton register → psb:// storage gateway`，验证真实 resource exists/open、32 项 array
  listing、miss、异常、缓存复用和空 local-name。本机 `reference` 工作树另有 142 个
  `mdf\0` 资产，但它们不在子模块 HEAD，不能作为仓库 fixture；其中最小场景文件已手动
  覆盖 Web storage/octet，并通过 Android oracle 直调 `0x598268/0x598960` 确认 MDF
  成功解压、owner 布局和 strict refresh。MDF zlib 失败分支仍只有 fresh 反编译与构建
  证据；带 filter 的 header 刷新已用现有加密 motion PSB 与真实 seed 覆盖，但仍不得把
  未覆盖的损坏输入边界写成行为已经完全相等。

## 审计范围与权威来源

权威来源是 Android `libkrkr2.so` 的 IDA 反编译。PSB 插件主体连续函数区间为
`0x59641C..0x59AA84`，共枚举到 90 个函数；其中包含 `std::vector` 实例化和
NCB 通用模板函数，不能全部按业务方法计数。

模块注册点 `0x42CF28` 给出二进制字面名字：

- module: `PSBFile.dll`
- class: `PSBFile`
- pre-register callback: `0x59849C`
- unregister callback: `nullptr`

## 反编译覆盖矩阵

| 领域 | Android 地址 | 已复原结构/行为 |
| --- | --- | --- |
| 名字查找 | `0x59641C` | double-array trie，按 UTF-8 byte 逐步转移 |
| 字典查找 | `0x59659C` | packed name-index 数组上的 lower-bound 二分查找 |
| 惰性值转换 | `0x59673C` | null/bool/int/real/string/octet 惰性转换；array/dictionary 创建共享 owner 的新 dispatch |
| 字符串/资源 | `0x596BC4`, `0x596C70` | 返回 raw buffer 内借用字符串指针；按 chunk offset/length 返回资源视图 |
| dispatch ABI | `0x596D78..0x597AD4` | 直接双继承 `iTJSDispatch2`/`iTJSNativeInstance`，独立 intrusive refcount、owner、node、valid byte；完整 vtable 默认值 |
| 成员枚举 | `0x596F50` | array 使用十进制下标名，dictionary 使用 packed 顺序；`TJS_ENUM_NO_VALUE` 控制回调参数个数 |
| count/index/property | `0x5975E0`, `0x5976C4`, `0x597854` | array count、负下标、dictionary 属性、`TJS_MEMBERMUSTEXIST` 边界 |
| name decode | `0x5975C0`, `0x597B1C` | 先经 `namesData[nameIndexes[index]]` 找 terminal，再沿 parent 回溯并 reverse |
| NCB 注册 | `0x597E98..0x5980F4`, `0x59AA84` | typed class state、factory、`root` property、`load` method；本地由 ncbind 模板承接通用注册机制 |
| root/load | `0x5981F8`, `0x598268`, `0x598538` | root 每次返回新 dispatch；string/octet 分流；小写 `mdf` 解压及失败 fallback；原始错误文本 |
| owner | `0x598708`, `0x598960`, `0x598A64`, `0x598AAC`, `0x598B3C` | 一个 owner 独占一个 raw allocation；intrusive ref；替换时释放旧 owner；move 保留零引用删除分支；filter 后刷新 header view |
| node helper | `0x598A3C..0x5996E4` | move、字符串、strict/try lookup、bool、keys、int/double、category、contains、resource |
| media 生命周期 | `0x59849C`, `0x5997F0..0x5998A8` | function-local static 指针由 `__cxa_guard` 构造一次；process-lifetime singleton、初始 ref=1、名字 `psb`、不注销 |
| media 访问 | `0x5998BC..0x59A4B0` | normalize no-op、exists/open/list/local-name、按首段缓存一个 PSBFile TJS object、contains→strict 逐段遍历 |
| motionplayer 原始加载链 | `0x6A8D8C`, `0x6A87D0`, `0x685D30`, `0x6863CC` | 规范化路径→缓存 raw owner→全局 `std::function` filter→严格读取 id/spec/version→每次新建 root dispatch；seed setter 接受至少一个可转整数的 TJS 参数 |
| callable 解密/注册 | `0x682528`, `0x685E60`, `0x6864C0`, `0x6864C8`, `0x6865B4`, `0x62C808` | emoteplayer entry 动态注入两个 static method；callable 由 `tRefHolder` 形状的 pointer+refcount 控制块共享；每次以同一个 `CBinaryAccessor` 类型传 `(whole-file view,size)`，返回值忽略；替换 filter 释放旧 Object/ObjThis |
| motionplayer 缓存生命周期 | `0x6A8438`, `0x6A8B94`, `0x6A8CF8`, `0x6A959C` | `clearCache` 只清 SourceCache 图层链；析构依次销毁 raw owner map、layer-id set、random variant 和基类状态；`unloadAll` 只清 raw owner map；`unload` 规范化路径后按 key 擦除 |
| motionplayer dispatch helper | `0x662668`, `0x6635DC`, `0x6636D4`, `0x529524`, `0x56C694`, `0x6695BC`, `0x6637BC` | 统一按 holder dispatch + 同一 objthis 调用 `PropGet/PropGetByNum`，再做普通 TJS real/int/bool/string 转换；count 来自 `PropGet("count")` |
| 变量轨道所有权/读取 | `0x6CD750`, `0x6B786C`, `0x6B7A70`, `0x69A754` | `frameSource`、`easing` 都是 `tTJSVariant` CopyRef；step/merge 通过 dispatch 读帧；`interval/value` 来自 `content`，`easing` 来自帧对象；Bezier 的 x/y 也经 dispatch 逐项读取 |
| 变量查询边界 | `0x53041C`, `0x530530`, `0x530568`, `0x530588`, `0x5305A8`, `0x673BEC`, `0x68229C`, `0x6D6590`, `0x6D676C` | D3D 五接口无条件抛精确 TODO `eTJSError`；Emote range 先查 HM5 的 frameMin/frameMax，miss 后递归折叠当前/子 Player +384 参数表；frameList CopyRef +1248 Dictionary 后 PropGet label |
| Player variableKeys | `0x6D139C`, `0x6D69C8` | NCB RO 属性；每次新建 TJS Array，按 Player+1296 var-track deque 顺序复制每项 item+0 `cascadeKey`，空 deque 仍返回新空 Array |
| Player tags/skipToSync | `0x6D9618`, `0x6D3504`, `0x6D69C8` | `tags` 直接 CopyRef Player+1072 raw tag variant；skip 的 playing/loopTime gate、tag-frame dead reads、时间 clamp 与两只连续 flag byte 均按函数体复原，不经 snapshot timeline |
| 粒子 source list | `0x6BF0DC`, `0x697D34` | node+2200 `particleMotionList` 保持独立 `tTJSVariant` owner；发射时直接 `PropGet("count")`、`PropGetByNum(randomIndex)` 后拆路径，不再经 ClipSlot/FrameContentState/interpolatedCache 的三层 `vector<string>` 镜像 |
| ClipSlot source 所有权/消费 | `0x69260C`, `0x692AB0`, `0x699510`, `0x6997F0`, `0x6948E8`, `0x6B64AC`, `0x6BE0C0`, `0x6BEDD0`, `0x6C2334` | slot+28 icon 与 slot+36 src 由两只 `ttstr` 直接持有；reset 只释放 src，HM3 value+44 CopyRef src 但 restore 不写回；init/findSource、child、particle-emitter 与 render-item 均直接消费 slot owner。child 单段路径 `setChara(src)+play(icon)`，多段路径固定取 `[1]/[2]`，无二段兜底 |
| source render 写回链 | `0x699AE4`, `0x6C2334` | timeline evaluator 只写节点变换/颜色/opacity/类型专用标量，不复制或插值 source；render-list builder 从 active slot+36 直接 AddRef 到 render item。生产 `FrameContentState/interpolatedCache` 的 `icon/src` owner 与 fallback 已删除，离线 decoded 字符串仅留 `OfflineFrameContentState` |
| 节点运行态标量与 mesh owner | `0x692AB0`, `0x6996E8`, `0x699AE4`, `0x69AC4C`, `0x699510`, `0x6997F0`, `0x69B1E8`, `0x6BC4F0`, `0x6C2334`, `0x6C715C`, `0x6D5264` | `interpolatedCache`、生产 `FrameContentState` 与 `localState` 整体删除；evaluator 直接写 `accumulated/colorBytes/particleInterp`，opacity 为整数 0..255，HM3/geometry 直接从 active slot 与 node runtime 取值。普通 transform/type-4、mesh crossfade、type-5 camera.fov 与 type-10 feedback.timespan 写回及消费链均已闭合；node/slot/HM3/render-item 的 mesh 容器已统一为 8B `{float x,float y}` 元素，只有 TJS/Layer 平台边界展开为标量/`tTVPPointD`。 |
| action/dtgt 与事件队列 | `0x692AB0`, `0x6B638C`, `0x6B6ADC`, `0x6B9A3C`, `0x6C4490`, `0x6BE0C0`, `0x6BEDD0` | slot action/dtgt 由 `ttstr` 持有；44B 事件源码形状恢复为 `int + tTJSVariant + tTJSVariant`，layer action 保留 void param1，node action 保留 label/action 两只 String variant，dispatch 前 CopyRef 后直接 `onAction`；dtgt 直接进入 Player+24 `map<ttstr,int>` 查找 |
| 节点 label/path 所有权与键空间 | `0x6B3C78`, `0x6B4A6C`, `0x6B51F0`, `0x6B5C1C`, `0x6B2D3C`, `0x6B826C`, `0x6B638C` | node+0 直接持有 PSB `label` 的 `ttstr`；Player+24 是 raw-label `map<ttstr,int>`；HM3 路径由每级 `L"/" + label` 前插构造为独立 `ttstr` key，两个键空间不再 narrow/widen；node action 从同一 label 构造 String variant |
| live 曲线所有权/求值 | `0x692AB0`, `0x699AE4`, `0x69A754`, `0x698454`, `0x69A4D4` | `ccc/cp/acc/zcc/scc` 只由活动 ClipSlot 的 raw `tTJSVariant` 持有；每次求值即时 `PropGet x/y/t/s` 及 `PropGetByNum`，不再解码到 `vector<double>`；decoded Bezier/spline 结构和回退算法仅留在 `OfflineMotionSnapshot.h`，生产 include graph 搜索为零 |
| 静态插件模块边界 | Android 单体 `libkrkr2.so` 的 NCB 静态注册链；Web/macOS 静态归档平台边界 | 所有插件 target 的源码均改为 `PRIVATE`，`libmotionplayer.a` 与 `libkrkr2plugin.a` 的 Ninja 输入只含各自编译单元；最终 executable 通过 force-load/whole-archive 保留 registrar-only 对象，不再用 `PUBLIC target_sources` 把 scriptsEx/psdfile/layerExDraw/fstat 对象重复归档进 motionplayer |
| Player parameter table | `0x6B365C`, `0x6B1718`, `0x6B202C`, `0x6B1ECC`, `0x6B1ABC` | `parameterize/parameter` 全部从 Player+528 raw dispatch 读取；56B vector 项使用 `ttstr id`，严格 Object/Integer 分支，+408 multimap 保留重复注册和父链 owner 形状 |
| Player preview/directEdit 字段拓扑 | `0x6CF0A4`, `0x6D9638`, `0x6D9640`, `0x6BC000`, `0x6BC4F0`, `0x6BD8DC`, `0x6BE0C0`, `0x6BEDD0`, `0x6BF0DC`, `0x6C2334` | +1092 是 `preview` 的单一 owner，负责节点类型 mask/整段 pass gate；子 Player 角度重初始化仍读取独立 +482 `directEdit`；已删除 snapshot root type 派生的第二模式字节 |
| Emote 状态持久化 | `0x675E40`, `0x678044`, `0x6767E4..0x677E28`, `0x678454..0x67B34C` | `EmotePlayer.serialize/unserialize` 直接操作 Engine 的 timeline/controller/base/outerforce raw 容器；固定八键 Dictionary、请求队列与 angle shipped quirk 均按二进制复原 |

## 六维对照

### 1. 源代码结构

`PSBRawOwner`、`PSBRawNode`、一指针 `PSBFile` holder、直接双接口
`PSBValueDispatch`、`PSBMedia` 已分层。ARM64 字节偏移只记录为反编译证据，
没有用 padding 或 packing 强行污染 wasm32 ABI。

旧 eager decoder 已隔离到 `psbfile_decoded_compat`；`loadMotionSnapshot` 实现与
`PlayerFrameStep.cpp`/`PlayerFrameStepping.cpp` 现在只属于 `motionplayer_offline`，Web
最终目标的 `ninja -t query` 中没有这两个兼容库和三个离线编译单元。fresh decompile
`Player_ctor@0x6CED30`、`Player_dtor@0x6CFADC`、`Player_loadMotion@0x6B0F10` 也确认
Android Player 没有 decoded owner；`motionplayer_ncb_register@0x6D9B08` 的注册链没有
DecodedPSB/TypeHandler，故生产 `main.cpp` 已去掉 `PSBFile.h` 的静态注册副作用。

`MotionSnapshot`、`loadMotionSnapshot` 及 decoded 帧/曲线/资源 helper 已集中到
`OfflineMotionSnapshot.h/.cpp`；`RuntimeSupport.h`、`PlayerInternal.h`、`Player.h` 的静态
交叉核实已无 `PSB::`、`PSBValue`、`MotionSnapshot`、`DecodedPSB` 或 loader 引用，
`PlayerCore.cpp` 的一条未使用 `PSBValue.h` include 也已删除。live `ClipSlot` 的
`src/icon` double owner 已删除，直接由两只 `ttstr` 驱动原生消费链；`motionDtgt/action`
与 event deque 也已恢复 `ttstr/tTJSVariant` owner。slot 内 decoded CurveData 已删除，
`0x69A754/0x698454/0x69A4D4` 运行链改为即时读取 raw dispatch；节点级
`interpolatedCache`、生产 `FrameContentState` 与 `localState` 已整体删除；HM3/geometry/render
现在直接读取 active slot、`colorBytes`、`accumulated` 与 `particleInterp`，`0x699AE4`
直接写 node runtime。source cache 已按 `0x699AE4/0x6C2334` 删除：render 直接读活动 slot 的 `ttstr`。
节点 label/path 已改为单一 `ttstr` owner：Player+24
保留 raw-label map，HM3 使用独立 path key，不再存在 `std::string` mirror 或
narrow/widen 回转。
插件 target 的源码传播边界也已闭合：`krkr2plugin`、`motionplayer`、`psdfile`、
`layerExDraw`、`fstat` 均使用 `PRIVATE target_sources`。Mac/Web 的 `ninja -t query`
确认 `libmotionplayer.a` 与 `libkrkr2plugin.a` 只归档各自对象；最终 executable 在静态链接
平台边界通过 `-force_load`/`--whole-archive` 保留 Android 单体 so 中必然存在的 NCB
registrar-only 编译单元。这样既恢复 `LoadModule("motionplayer.dll")`，也不再让源码跨库复制。

### 2. 数据流

插件 target 的主数据流已经是：

`storage/octet -> optional mdf -> one raw allocation -> owner header view -> raw node -> lazy TJS value`

不再预先构造递归 `PSBValue` 树。motionplayer 的 HashMap A 和变量轨道也已不再持有
eager `tTJSVariant`/`shared_ptr<IPSBValue>`；HashMap A 的 value 是一指针语义的
`PSBFile` holder，命中时从同一 owner 创建新的 root dispatch。

raw 主链后的 `attachDecodedSnapshotCompatibility` 已删除：cache hit/miss 都只从
HashMap A 的 `PSBFile` holder 创建 fresh root dispatch，不再第二次读取/解码文件。
`findMotion` 也只返回 `[raw motion dispatch, matched HashMap-A key]`，不再执行
path→snapshot 或 dispatch-pointer→snapshot 映射。Player 也不再维护 `_motionsByKey`、
尝试路径候选/native 直读或创建 `_activeMotion`。诊断路径由 +1012 matched-key Variant
即时转换，loaded gate 直接读取 +528 Variant type。

### 3. 调用链

模块名、typed NCB factory/property/method、pre-register media singleton、
`psb:` resolve/open/list 链已按二进制复原。NCB 通用模板由仓库现有 ncbind
实现承接。旧分析曾把 `PSBFile.load` 误判为 `const tTJSVariant&` 且补了
borrow-only converter；后续 fresh decompile `0x59B570/0x59B708/0x5980F4` 已证伪：
原版按值构造参数并经历明确的 Variant AddRef/Release 临时生命周期，本地已同步纠正。
motionplayer 侧新增共享的 `Motion_propGet*` 层，`ResourceManager` 与变量轨道均复用
同一调用形状；变量轨道不再为前进、后退、重定位各复制一套 eager 读取器，而是调用
Android 中独立存在的 step `0x6B786C` 与 merge `0x6B7A70` 对应 helper。
`ResourceManager::load` 则按 `0x6A8D8C` 执行路径规范化、raw cache lookup/load、
`id/spec/version` strict lookup、copy-insert 与 fresh root dispatch common tail。cache hit
在 `0x6A8E94..0x6A8EB8` 复制一指针 holder 并 AddRef；miss 在
`0x6A926C..0x6A92A8` 对 map record 执行 Release-old/copy/AddRef，随后仍由 local holder
进入公共 dispatch 构造链。本地先前使用 move-insert、直接借用 record 的结论已由这些
指令证伪并纠正。
`setEmotePSBDecryptSeed/Func` 不再作为 ResourceManager registrar 的本地附加项；
`emoteplayer.dll` 后置入口按 `0x682528` 查找 `Motion.ResourceManager`，创建 native class
method，并以 `TJS_MEMBERENSURE | TJS_STATICMEMBER` 动态注入。
`EmoteObject_init@0x67DBAC` 现已按 raw dispatch 调用顺序读取 metadata/base，随后通过
`Player_playImpl@0x6B2284` 的本地入口进入加载；`ensureMotionLoaded` 首先按
`Player_loadMotion@0x6B0F10` 拼出 `motion/<chara>/<motion>`，并调用
`ResourceManager_findMotion@0x6A9ED4`。二进制结果 element 0 原样写入 +528 对应 raw
owner，element 1 原样回写 motionKey/project 单槽；不再经过全局 snapshot 查询。

### 4. 对象生命周期

- owner intrusive refcount 管理 raw allocation；holder、node view 和 dispatch 共享它。
- `PSBFile` holder 可复制：copy construction 对 owner AddRef；copy assignment 先释放旧
  owner，再保存并 AddRef 新 owner。ResourceManager 命中与插入均保留原版临时 holder
  的 AddRef/Release 生命周期，不用 move 消去中间步骤；assignment 不额外加入二进制
  该调用点不存在的 self-assignment 安全分支。
- holder 替换文件只释放自己的 owner 引用，旧 node/dispatch 仍保持 allocation 存活。
- holder move 与 `0x598A64` 一致地保留“非空且 refcount==0 时直接删除 owner”的边界分支。
- 每个 collection dispatch 有独立 `valid` byte；invalidate 父 dispatch 不会使子 dispatch 失效。
- TJS object closure 的 Object/ObjThis 各持一份 dispatch 引用，测试中的稳定 refcount 为 2。
- media 通过带 `__cxa_guard` 的 function-local static 指针只分配一次，保留
  process-lifetime 初始引用，模块无 unregister callback。
- `PSBValueClass` 的 native class ID 则不是 guarded static：`0x596D90` 显式检查
  零值后调用 `TJSRegisterNativeClass`，本地保留了这个生命周期差异。
- ResourceManager map erase 只释放 holder 自己的 owner 引用；测试确认外部保留的两个
  fresh root dispatch 在 `unload` 后仍能读取同一 raw allocation。
- callable filter 使用 `TJS::tRefHolder` 的 pointer+RefCount 控制块共享一份 owning
  closure；最终销毁依次 Release Object/ObjThis。真实加密 PSB 测试确认 callable 参数
  在输入 variant 清空后仍存活，并在 seed filter 替换时恰好析构一次。
- `0x6865B4` 为 `CBinaryAccessor` 构造后的 object variant 执行 AddRef，却没有像 XP3
  wrapper 那样平衡 constructor initial ref；本地保留了这个每次 callable 调用泄漏一份
  accessor 初始引用的边界，未擅自“修安全”。
- Android 不存在的两只 function-static snapshot 强引用表已删除；RM `unload/unloadAll`
  现在只需管理 raw HashMap A，外部 dispatch 按 intrusive owner 独立续命。Player 的
  `_motionsByKey` 与 `_activeMotion` 均已删除；析构只销毁 Android 对应的 +1012/+528
  Variant owner，不再额外销毁一只 `shared_ptr` 文件标记。
- ResourceManager 现在内联拥有 map/set/counter，构造时真实插入 set sentinel `{0}`，
  析构体先清 raw map，再由成员逆序销毁 set/random/map；不再存在共享 State 拓扑。

### 5. 内部容器实现

PSB packed integer arrays、name trie、sorted dictionary index 和临时
`std::vector<std::string>` key list 已按反编译结构复原。packed count 只接受
`0x0D..0x10`，packed value 则按二进制的 unsigned range 接受 `0x0D..0x11`；
value stride 保留 `tag - 0x0C` 的有符号结果，不能被通用安全读取器归一化。旧 decoder
的 map/vector 对象树不属于新的 `psbfile` target，只保留在 compatibility target。
ResourceManager HashMap A 使用 `std::unordered_map<ttstr, PSBFile,
ttstr_hash, ttstr_equal>`，对应 ctor `0x6A88CC` 的 libstdc++ bucket/node-chain 拓扑；
layer id 使用 `std::set<tjs_int>`，没有用 Web 自造容器替代。
`sub_597B1C@0x597B1C` 的 name decode 直接写调用者提供的 `std::string&`；
`sub_598E64@0x598E64` 在整个 dictionary 枚举期间只复用一只 string，并把它逐次复制进
预留容量的 vector。本地旧实现的按值返回、逐轮新建 string 和 move-emplace 已纠正，
保留原版 COW string copy/refcount 与临时对象生命周期。
trie 查找 `sub_59641C@0x59641C` 只接收裸 UTF-8 指针；packed dictionary 二分
`sub_59659C@0x59659C` 是独立 helper，并通过 out 参数返回相对 `node+1` 的 32-bit
value offset。本地旧实现曾让前者持有 `std::string&`、把后者内联成直接指针结果；现已
恢复 `trie → packed offset → node+1+offset` 的函数边界和数据流。
`sub_598D58@0x598D58` 的 try-get 在 hit 时按 Release-old→copy owner→AddRef→write node
顺序更新 out 参数，miss 保持 out 不变；`sub_5995D8@0x5995D8` 的 contains 对 dictionary
创建临时 raw node 并调用 try-get，返回前保留一对 AddRef/Release no-op。本地旧实现曾以
retain-first 临时 move 改变 alias 边界，并把 contains 简化成裸指针查找；现已纠正。
typed root getter `sub_5981F8@0x5981F8` 在 holder 为空时返回 null；raw root helper
`sub_598A3C@0x598A3C` 则无 guard，直接解引用 owner/header 后复制两指针 node；typed
getter 也不调用该 helper，而是在 guard 后直接构造 dispatch 内的 owner/node。本地旧实现
曾把 guard 下沉到 raw helper并让 typed getter绕经它，掩盖直接调用空 holder 的崩溃边界、
增加额外调用层；现已恢复两层职责。
dispatch ctor `sub_597AD4@0x597AD4` 直接接收 source owner/node，复制字段并在 ctor 内
AddRef owner；不是先构造 retained `PSBRawNode` 值再 move 进成员。本地 constructor 已改为
两裸字段输入，typed root getter 与通用 factory 均由该 ctor 自身建立 dispatch owner 引用。
raw node validity `sub_598E44@0x598E44` 独立检查 `owner && node`；类型消费者
`sub_599554@0x599554`、`sub_5995D8@0x5995D8` 直接读取 `node[0]`。本地 `GetType()` 旧有的
null→tag0 安全归一化已删除，未先做显式 validity 检查的调用保留原始空指针边界。

### 6. 边界行为

已覆盖：最小 0x40 字节、`PSB\0` signature、offset 的严格/非严格比较、
MDF 解压失败 fallback、storage invalid-buffer 的原始泄漏边界、unknown tag 抛错、
known non-dictionary contains=false、strict missing-key 抛错、负数组下标、
`TJS_MEMBERMUSTEXIST`、dispatch invalidate、no-op normalize、空 locally-accessible name、
packed value tag `0x11` 与有符号 stride，以及真实加密 motion PSB 的 xorshift filter。

尚不能封口：没有现成 fixture/oracle 覆盖 filter 后 offset 验证失败、MDF zlib 失败的
runtime 路径，以及损坏 packed table 的实际越界/崩溃表现。NCB typed class 的真实 TJS
构造与 media 缓存切换已经由现有 PIMG 资产覆盖。

## 本轮纠正的既有误差

1. 原实现把 `PSBFile` 做成 eager decoder；现已改名为 `DecodedPSBFile` 并隔离。
2. name decode 曾漏掉 `namesData[nameIndexes[index]]` 的第一次间接寻址；已由
   `0x597B1C` 纠正，并用日文 key/value 测试覆盖。
3. dispatch 曾继承 `tTJSDispatch`；vtable 与构造函数 `0x597AD4` 证明原实现是
   直接 `iTJSDispatch2 + iTJSNativeInstance` 双基类，现已纠正。
4. strict/try dictionary lookup 曾在 helper 内额外检查 dictionary tag；
   `0x598C58/0x598D58` 证明查找本体不门控，类型门控位于上层调用者，现已纠正。
5. 删除了无对应 Android 函数、仅供早期测试使用的 `GetInteger/GetReal` 公开便利接口；
   数值访问只保留 `0x599438/0x5992E8` 证实的 `GetInt/GetDouble`。
6. 原 packed-array 读取器把 count/value tag 合并成通用安全逻辑；`0x59641C` 与
   `0x59659C` 证明两者接受范围不同，且 stride 是有符号 `tag-0x0C`，现已纠正。
7. `setEmotePSBDecryptSeed` 曾要求参数数目恰好为 1 且预先限制为整数；`0x685D30`
   证明它只拒绝零参数，并使用普通 TJS integer conversion，现已纠正。
8. IDA 曾把 `0x6A8CF8` 的独立 `unloadAll` 合并进 `0x6A8B94` 析构函数，并把错误名字
   赋给析构函数；独立 `SUB SP` 序言及注册点 `0x6AB8BC` 的字面 `unloadAll` 绑定证明
   两者不同。现已拆分函数边界，析构函数改为带 `_guess` 名，`0x6A8CF8` 使用注册名，
   添加证据注释并保存 IDB。
9. 变量轨道曾把 `frameSource/easing` 建模为 eager `shared_ptr<IPSBValue>`，并误从
   `content["easing"]` 取值；`0x6CD750/0x6B7A70` 证明两者均为 `tTJSVariant`，且
   `easing` 的 PropGet 接收者是帧对象。现已纠正字段生命周期、访问器和 Bezier 链。
10. ResourceManager HashMap A 曾缓存 eager `tTJSVariant`；`0x6A8D8C`、`0x6A8CF8`、
    `0x6A959C` 和 ctor `0x6A88CC` 证明 mapped value 是可 move 的一指针 `PSBFile`
    holder，cache hit/miss 共用 fresh-dispatch tail。现已纠正 value 类型、filter、严格
    校验、插入/擦除和 owner 生命周期；旧 snapshot side graph 已在后续阶段整体删除。
11. `setEmotePSBDecryptSeed/Func` 曾被记为 ResourceManager 的“port extras”，且 callable
    是未实现桩；UTF-16 字符串 xref 与 `emoteplayer_entry@0x682528` 证明它们由 Android
    emoteplayer 模块动态注入。`0x685E60/0x6864C8/0x6865B4` 又证明闭包控制块、调用参数
    与释放顺序。现已纠正注册链、共用 `CBinaryAccessor`、callable filter 与旧闭包释放；
    错误 memory 同步就地修正。
12. EmoteObject 曾以 by-value `ResourceManager` 持有 `shared_ptr<State>`，再为 NCB adaptor
    复制出第二个 RM；`0x67DBAC/0x67E20C/0x67F420` 证明 Android 只有一个堆分配 RM，
    sticky adaptor 指向同一对象，且 paths 在 Engine 构造后才赋值。现已纠正对象数、
    refcount/异常顺序和 Engine → RM → vector 析构链，并同步修正旧 memory/analysis。
13. D3DEmotePlayer 曾把构造参数和壳字段误建模为 by-value ResourceManager；
    `sub_542764/sub_5428D8/sub_42C7F8` 证明参数的二进制字面类型是 `D3DImage`，壳只保存
    native owner。现已删除壳 RM，factory 校验 D3DImage，clone 传递同一 owner。
    `0x530DA4/0x530DE8` 证明 owner 维护允许重复的 raw listener list；
    `0x533244/0x533CBC/0x533D4C` 证明 update/draw 虚槽与注册、解注册时序。
    本地已恢复 add/remove-all、scale-X 更新、TransformPoint(0,0) 与原生 target 纹理绘制链。
14. ResourceManager 曾通过 `shared_ptr<State>` 共享 HashMap A、set 和 counter；
    ctor `0x6A88CC`、dtor `0x6A8B94` 证明这些是对象内联成员，且 ctor 确实向 set 插入
    `{0}`。现已内联容器、禁止复制、恢复 sentinel 与“析构体先清 map”的顺序，并纠正
    旧注释中“插入 0 是 port invention”的错误。
15. ResourceManager/Player 曾维护 `_lastLoadedPath/_lastLoadedModule` 并让
    `ensureMotionLoaded()`从该状态兜底；`0x6A8D8C` 只返回 fresh dispatch，
    `0x6A959C/0x6A8CF8` 只操作 HashMap A，而 `EmoteObject_init@0x67DBAC` 将每次 load
    返回值覆盖到调用栈局部变量并在循环后消费。现已删除字段、getter、维护分支与兜底链。
16. Player 曾把 NCB `motionKey` 错绑到实际运动名 `_motionKey`，又另造 `_project`
    保存 module dispatch 并在 play/ensureMotionLoaded 中直接激活 snapshot。注册点
    `0x6D6F58..0x6D7020` 证明 `motionKey` 与 `project` 复用完全相同的 getter/setter；
    `0x695BE0/0x6B4978` 证明二者只读写唯一的 `player+1012` variant，而
    `Player_loadMotion@0x6B0F10` 将其作为 `ResourceManager.findMotion` 参数 0。
    现已删除 `_project` 与对象快照旁路，两个属性统一绑定 `_findMotionContextVariant`。
17. `Player::modifyRoot` 曾被本地误造为接收 module variant 并把它存进 `_project` 的
    snapshot 激活入口；NCB 注册点 `0x6D87C4` 证明它是无参方法，函数体
    `0x6CD0B0` 只把 `*(player+200)+1584` 的 root delta dirty byte 置 1。
    现已改成无参 `_nodes[0].delta.dirty = true`，没有保留兼容对象旁路；外层
    `EmotePlayer_modifyRoot@0x681F0C` 的同形 `engine→player→root` 桩也已改为直接转发。
18. `EmoteObject` 构造曾在 load 循环后直接以最后一个返回 dispatch 查 snapshot，再由
    `Player::loadFromSnapshot` 同时激活 motion 和构建 controller；这与
    `EmoteObject_init@0x67DBAC`、`Player_loadMotion@0x6B0F10`、
    `Player_playImpl@0x6B2284` 的 raw 数据流相反。现已恢复 raw `metadata/base` 读取、
    最后输入路径写入 +1012、chara→Force play→metadata 的顺序；
    `ResourceManager_findMotion@0x6A9ED4` 的 `[motionValue,matchedKey]` 成为激活输入。
    原 `loadFromSnapshot` 已删除，controller builder 已恢复为
    `EmoteEngine::applyMetadataLike_0x67D4D0`。接收者从 Player 迁回 `EmoteEngine`，并按
    `0x67D4D0` CopyRef 构造函数传来的
    raw metadata variant；`mirror` 的三状态字段、root flip、0-frame progress，以及
    `scale`→scale-controller 当前值→倒数有效比例的数据流也已直接消费 raw metadata。
    `EmoteEngine_buildLoopControl@0x66E480` 也已从 eager
    `PSBList/PSBDictionary/PSBNumber` 完整迁到 `PropGet/PropGetByNum`：enabled gate、
    transitionList 三元帧、12B float keyframe、deque#10 与 HM6 `{type=3,index}` 均直接
    消费 raw dispatch。`EmoteEngine_buildTransitionControl@0x66D4C4` 同样已迁到 raw
    dispatch，并保留“先 push 空 label、再 CopyRef label”、flag=1、raw controller
    所有权、跳过项仍使用原始 loop index 以及 HM6 `{type=7,index}`；调用顺序恢复为
    variableList → raw bust/hair/parts → raw eye/eyebrow/mouth →
    transition → selector → loop。其余 controller list 解码仍代理给 Player 侧兼容中段。
19. selector 的旧注释曾声称 transition deque 尚未移植、因此 refCtl 必为空；本轮
    `0x66D4C4` raw builder 已使该前提失效。fresh decompile 的
    `EmoteEngine_buildSelectorControl@0x66D8FC` 又显示 selector 在 disabled 项和 transition
    匹配项上都会调用 `sub_66E248`。该 helper 并非本地 map erase：它 CopyRef
    `EmoteEngine+1228` 并调用 TJS Array 的 `remove(label)`；数组由
    `EmoteEngine_buildVariableList@0x66A530` 创建，+1248 则是 per-label frame-array
    Dictionary。现已把 +1208/+1228/+1248 从错误的 opaque filler 纠正为三个 owning
    `tTJSVariant` 字段，并直接恢复 reset 尾部 `sub_669798@0x669798` 的 Array 创建、
    CopyRef 别名与 Dictionary 创建。`buildVariableList` 已按 raw `PropGet` 链构造 label
    Array、per-label frame Array/Dictionary，并把原始 frame variant 追加到 native TJS
    Array；`buildSelectorControl` 也已完整迁到 raw dispatch，两处 `sub_66E248` 均实际
    调用 Array `remove`，不再以本地 map erase 或空对象检查替代。
20. HM#5 曾被错误占位为 `unordered_map<ttstr,double>`。交叉反编译其插入
    `sub_6880A8@0x6880A8`、链表挂接 `sub_688260@0x688260`、查询
    `sub_687FB8@0x687FB8`、清理 `sub_68577C@0x68577C` 和消费者
    `buildVariableList@0x66A530` 后，确认它使用独有 64B 节点：next@0、key ttstr@8、
    value ttstr@16、double@24/@32/@40/@48、cached hash@56；清理会释放两只 ttstr。
    本地已改为具名 value struct 与 KiriKiri ttstr hash map；首对 double 按二进制初始化
    为 DBL_MAX/-DBL_MAX，frameMin/frameMax 忠实保留二进制的未初始化边界行为。
21. `resetMetadataState` 已按 `sub_669928@0x669928` 恢复：先清 HM6 与十组 controller
    deque/所有权，再清 +800 vector、HM1/HM2/HM3，最后进入 `sub_669798` 重建上述三只
    TJS 容器并清 HM4/HM5。2026-07-19 fresh decompile `sub_6696B8@0x6696B8`、
    `vector<ttstr> assign@0x67F0CC` 与 `EmoteEngine_dtor@0x67F4B8` 后，已纠正此前
    “+800 元素类型未知”的错误：+800/+992/+1016/+1040 均为 `vector<ttstr>`，clear/dtor
    逐元素 Release string handle 后保留 capacity 或释放 buffer。本地四只
    `std::vector<ttstr>` 已由普通成员生命周期完整复刻，不再列为生命周期缺口。
22. `EmotePlayer_getVariableKeys@0x681FA0` 只 CopyRef `engine+1208`。旧实现从 snapshot
    字典拼装 keys，现已删除并直接返回 `_variableLabelsBase`，保留 +1208 与 +1228
    经 reset CopyRef 后、variableList 再替换 +1228 的原始别名变化。
23. `eyeControl` 的 builder `0x66C77C` 与 controller ctor `0x662968` 已作为一个不可拆的
    raw vertical 迁移：外层直接按数值索引取 elem、执行 enabled gate、push deque#4 并按
    原循环索引写 HM6 `{4,index}`；内层直接从同一 elem dispatch 读取六个 blink scalar、
    `edge` 二元表和 `node` 行向量，不再经过 `PSBList/PSBDictionary/PSBNumber`。为了不因
    单项迁移破坏 `0x67D4D0` 顺序，decoded 兼容段曾拆在 eye 前后，随后 eyebrow/mouth
    完成 raw 化后又删除了后半段。fresh decompile 同时确认 loop 后还有 clampControl、
    mirrorControl、可选 instantVariableList、timelineControl 与 `sub_670D1C`；这些 raw
    builder 及 selector-sync 尾链现已全部依序接入，剩余的是相关 consumer ownership。
24. `eyebrowControl` 的 builder `0x66CB9C` 与 slim ctor `0x66480C` 已整体 raw 化。slim
    ctor 忠实保持其独立类边界：只读取 beginFrame 与 edge/node 表，不读取 eye 专属的
    blink/endFrame/interval 字段，也不调用 RNG；builder 以原循环索引写 HM6 `{5,index}`。
25. `mouthControl` 的 builder `0x66CFBC` 与 ctor `0x665C98` 已整体 raw 化。0x70-byte
    controller 只含一只 12B-keyframe deque、紧凑 ramp 状态和 raw beginFrame；builder
    保持 24B deque 元素的 label/talkLabel 双 ttstr 所有权，并对两只 key 分别写入同一个
    `{type=6,index=原循环索引}`。原 Player 侧 mouth snapshot 消费入口已随之删除。
26. `clampControl` builder `0x66EE5C` 已直接接到 `0x67D4D0` 的 loop 后原始位置：按 raw
    数值索引读取、以 enabled=false 为默认 gate、先追加全零 40B typed entry，再按
    type→var_lr→var_ud→min→max 的二进制顺序填充；两只 ttstr 生命周期由 deque#7
    直接持有，不分配 controller，也不写 HM6。消费端 `0x67C8A8` 现已归回
    `EmoteEngine`，直接遍历该 raw deque、读取 Engine HM7 并使用 Engine raw mirror；
    callee `0x67C560` 也已迁回 Engine HM3/+1040/nested 56B track deque，不再委托
    Player decoded timeline model。
27. `mirrorControl` builder `0x66F364` 已恢复 Engine+800 的真实容器类型
    `std::vector<ttstr>`，并从 raw `variableMatchList` 按原索引逐项追加。fresh decompile
    明确显示这里没有 enabled gate、空字符串过滤、去重或 builder-local clear；因此不再
    把 decoded collector 的 `appendUnique`/`!empty` 行为误当 Android 边界语义。
28. `instantVariableList` builder `0x66F64C` 与插入 helper `0x689760` 的交叉反编译确认
    Engine+1272 是 `unordered_set<ttstr>`，不是原先占位的 `unordered_map<ttstr,double>`；
    节点仅 24B `{next,key,cached-hash}`。现已恢复外层 optional gate 与逐项 raw ttstr
    插入，且不再添加 decoded collector 独有的空值过滤、字典 key fallback、去重前置或
    variableLabels 旁写（set 本身按原容器语义处理重复 key）。
29. `timelineControl` builder `0x66F80C`、HM3 find/insert `0x687C80`、dtor `0x683E40`
    与消费者 `0x67C560` 已交叉核实：+992/+1016/+1040 全是 `vector<ttstr>`；builder
    只清 normal/diff 两只，按 `diff` 属性“先 MEMBERMUSTEXIST probe、销毁 probe、再读
    bool”的两步副作用选择 vector，保留空 label 与重复项，并将完整 raw elem CopyRef 到
    HM3[label]。HM3 node 是 0x88B，其中 mapped value 为 112B，不是旧 `opaque[120]`；
    本地已恢复 owning raw variant、`EmoteTimelineData80B`、其 56B track deque/
    24B frame vector、blendWeight=1 与 heap handle 语义；play/stop/seek/query/fade/blend 与
    `0x67C560` 消费链均已转到这一 raw HM3 状态机。
30. mirror consumer `sub_67C6B0` 与 set 插入 `0x68BF40` 证明 Engine+824/+880 是正/负
    `unordered_set<ttstr>` cache，而非 scalar map。现已迁回 Engine：先受 mirrorChanged
    gate，再查正/负 cache，miss 时按 raw variableMatchList 顺序执行
    `label.IndexOf(pattern,0) >= 1`（位置 0 明确不匹配），最后写对应 cache。旧 Player
    decoded exact-match、activeMotion/empty guard 已删除；Player 壳仅转发给 Engine。
31. `sub_670D1C` fresh decompile 证明 selector-sync 会创建 TJS Array 并 CopyRef 到
    +1208、读取 +1228 ArrayNI 并复制 Items、置 dirty、逐 entry 写 +16 gate；enabled
    分支清 selector command deque、置 selState=0 并 applySelection(0)，disabled 分支调用
    `std::remove` 却故意忽略返回 end、不 erase。随后按 +24/+32/+40 的非 owning
    `vector<transition-entry*>` 分别 remove label 或清 controller queue/state/value。
    helper `0x704CB8`、`0x670F6C`、`0x68B898` 已交叉取证；本地现已完整恢复该函数并在
    metadata 尾部 `0x67DA8C` 和 selectorEnabled setter `0x681F94` 接回真实调用链。
34. selector-target 三入口也已闭合：`is@0x6823FC` 每遍历一个 selector 先把 +1160
    byte 写入 entry gate，再扫描其非 owning target vector；activate@0x67581C 与
    deactivate@0x675BF4 找到 target 索引后，均清 selector command deque、重置 selState、
    `applySelection(index,0,0)`，分别写 gate=0/1，再以 dt=0 重步进所有 selector 与
    transition controller 回写 HM7。旧 decoded selector registry、disabled-map 和 warning
    桩已删除。注册点 `0x6814D8` 及 UTF-16 `0x14D7796` 还证明 activate 是真实第 68 个
    NCB 成员；本地“仅 69 成员、无 activate”的旧结论已纠正为 70 成员 + 2 常量。
32. `bustControl` builder `sub_66B018@0x66B018`、spring ctor
    `sub_662448@0x662448`与 raw vec3 reader `sub_66B83C@0x66B83C` 已作为一个整体
    迁移。外层现在直接按数值索引取 raw element，执行 `enabled=false` gate，
    从 `param` 中按 `op`→`p`→`pv`→`ofs` 的原始顺序覆盖 spring 状态，再先追加
    pointer/init/zero-anchor node，后写 `baseLayer`、`var_lr`、`var_ud`；HM6 两个 key
    都指向 `{type=0,index=原始循环索引}`。ctor 只写二进制实际写入的 first flag、
    三组零向量与 gravity/spring/friction/scale_x/scale_y，不再防御性初始化
    `prevDeltaX/prevDeltaY/biasY`。这一 vertical 已完全脱离 decoded snapshot；
    metadata 的 bust/hair/parts 三路 spring builder 都已不再依赖 decoded snapshot。
33. `hairControl`/`partsControl` 的共用 builder `sub_66B9D0@0x66B9D0` 与 176B
    chain-spring ctor `sub_668EF8@0x668EF8` 已整体 raw 化。ctor 按原始顺序读取
    八个 scalar 与 length/scale_x/scale_y 三组二元表，再以静态向量
    `(0,1,0)` 派生两段初始位置/速度。builder 直接消费 raw element/param，
    保留 enabled gate、原循环索引和三 key HM6 映射；同时纠正了旧实现的
    所有者错误：`bp` 的 dispatch 来自 `param`，不是 elem。deque node 的 +8
    保持不写，两只 anchor 按二进制显式清零；先 emplace 节点再写四只
    ttstr，避免把未初始化字节通过临时节点 move 读出。`0x67D4D0` 现在以
    raw metadata 依次取 bust→hair→parts→eye，Player 侧的 decoded root 重遍历与其
    整段兼容 helper 已删除。

## `MotionSnapshot` side graph 闭合审计

2026-07-18 重新反编译 `Player_loadMotion@0x6B0F10`、
`Player_playImpl@0x6B2284`、`Player_initNonEmoteMotion@0x6B365C`、
`Player_buildNodeTree@0x6B51F0`、`Motion_Player_findSource@0x6948E8` 和
`sub_695DE8@0x695DE8` 后，确认 side graph 不是平台边界，而是错误的双轨架构；
下表记录其删除后的闭合状态：

| 数据组 | Android owner/流转 | 当前 Web owner/流转 | 裁决 |
|---|---|---|---|
| motion/content | `findMotion` 返回 raw motion dispatch；`playImpl` 将 result[0] 写 +528、result[1] 写 +1012；type 1 另持有 +484 division/+508 motionList 并以 +504 选择二级 motion；ctor/dtor 不存在额外活动文件 owner | +528/+1012 raw `tTJSVariant`、六个 chara/motion/pending string-value owner、type 0/1/其他分支及 +484/+504/+508 Emote owner/选择链均已建立；重复解码、全局注册表、`_motionsByKey`、路径回退和 `_activeMotion` 均已删除 | CLOSED |
| tag/priority | `initNonEmoteMotion` 直接 `PropGet` 并 CopyRef 到 +1072/+548，priority[0].content 到 +616；前进、后退和完整重定位均通过这些 Array dispatch 读取帧 | 三只 raw `tTJSVariant` owner 已建立；四条帧游标路径均改为 TJS dispatch 读取，旧 snapshot 帧表字段已删除 | CLOSED |
| layer/node tree | `buildNodeTree` 从 +528 dispatch 取 `layer` TJS Array，递归 helper 直接消费各 raw layer dispatch；节点独立 CopyRef `frameList/emoteEdit/particleMotionList/stencilCompositeMaskLayerList` | 树形、节点数、label map、标量字段、type 分支及 stencil 指针 vector 均由 raw dispatch 驱动；`snapshotCompatibility` 递归参数、decoded `psbNode/emoteEditDict` owner 已删除 | CLOSED |
| node label/path | node+0 持有 raw `label` ttstr；Player+24 raw-label map 与 HM3 slash-path map 是两个独立 `ttstr` 键空间；action event 从 node+0 构造 String variant | `MotionNode::layerName`、Player+24 map、HM3 path builder、layer getter、事件和 child/render consumers 均直接传递 `ttstr`；只在日志/JSON 边界 narrow | CLOSED |
| frame slot/evaluator | `parseFrame@0x6926B4` 接收 raw `frameList+index` 且只 parse；`mergeFrameContent@0x692AB0` 再按 slot index 取 content，保留 raw 字符串/variant、32 数值 mesh 与两槽 merged 状态 | live 节点已按 raw `frameListVariant` 实现 selective reset、parse/merge 分离、raw owner、mesh 32 数值、init/reseek/forward/back/modified 重建；node 0 按 `0x6BB4D4` 始终是 synthetic root 并直接复制 delta。decoded helpers 仅为无调用者的 legacy/test model | CLOSED |
| source texture | `0x6948E8/0x695DE8` 从 RM HashMap A 的 record.root 导航；record 内含 Win `group->texture` 与 KRKR `src/group/icon->descriptor` 两张 map；非 atlas 路径经 `RM_findSource@0x6AAB3C → ObjSource_drawLayer@0x69D6D8 → ensureTexture@0x6DA454 → SourceCache_loadSource@0x6A7BA8`；删除外层节点时按 KRKR→Win→PSBFile 析构 | mapped record、两表拓扑、AddRef/Release 与 unload 生命周期已复原；Win/KRKR 均从 raw `PSBRawNode` 导航，KRKR 恢复 all-group 枚举、raw/RL/palette/透明 2x2 分支；`ObjSource` 现独占一只惰性 texture 并在析构 Release，`SourceCache` 不再遍历 `_activeMotion`/`MotionSnapshot::sourceCandidates`。整页 CPU 合成后一次 Update 是 Web API 边界 | CLOSED + PLATFORM BOUNDARY |
| variable query/interpolation | D3D 五个枚举方法是无条件 TODO throw；Emote range/frameList 读取 Engine HM5/+1248，HM5 miss 才递归 Player+384 参数表和所有子 Player；updateLayers 无条件调用 `0x6BBE20` 遍历 +1296 var-track deque | 五个 D3D wrapper、range/frameList 与 updateLayers live 插值均已切到 raw Engine/Player owner；旧 snapshot frame/range 查询及首帧旁路已删除 | CLOSED |
| Player variableKeys | getter 直接遍历 Player+1296 `std::deque<VariableLabelScope>`，每次分配并返回一个新 Array；没有 setter、Player 缓存或 motion-load 副作用 | getter 已直接读取 `_variableLabelScopes[].cascadeKey`；旧 `_variableKeys`、RW setter、`ensureMotionLoaded()` 与 snapshot-label 同步写入均已删除 | CLOSED |
| Player parameter table | `initNonEmoteMotion` 从 +528 读 `parameterize/parameter`；+384 是 56B vector，+408 是 `multimap<ttstr,entry*>`，并注册到当前 Player 及父链 | helper 已改为 raw `tTJSVariant` 输入，`MotionParameterEntry::id` 已改为 `ttstr`，decoded `motionObject/contentObject` 与其额外 owner 已删除 | CLOSED |
| controller/timeline | Engine raw metadata、HM3/+1040、typed deques、selector gate/vector 与 mirror vector/cache | Engine live builder/consumer 链已 raw 化；重复的 selector、mirror、通用 controller 与 fixed-output snapshot side table 已删除；Player NCB 注册表无 timeline API，本地 Player `_timelines/_playingTimelineLabels`、decoded timelineControl/label/loop/total 表和无效直接调用测试均已删除；全仓无 snapshot↔timeline 交叉消费 | CLOSED |

35. `Player_loadMotion@0x6B0F10` 字面调用 `Player+992` ResourceManager dispatch
    的 `findMotion(projectKey,"motion/"+chara+"/"+motion)`，返回值直接传给
    `playImpl`。Android 没有 path→snapshot 或 dispatch-pointer→snapshot 注册表。
    Web 原先在 cache hit 和 miss 两路都再调一次 `loadMotionSnapshot`，导致同一文件
    同时存在 raw `PSBFile` holder 和 eager `DecodedPSBFile` owner；该双解析链及其
    path/dispatch 注册步骤现已删除。
36. `Player_playImpl@0x6B2284` 以 string-value owner identity/type/name 判断是否 reload，成功后
    把 motion AddRef/store 到 +976/+984，再对返回 content 的 `type` 分流到 emote/
    non-emote init。本地现已把 `findMotion` 的 result[0]/result[1] 分别 CopyRef 到
    `_motionContentVariant`（+528 语义）与 `_findMotionContextVariant`（+1012 语义），
    `onFindMotion` 的入口也已统一到 `Player_play@0x6B21E8` →
    `Player_playImpl@0x6B2284`。同运动 gate 直接比较 `_motionKey`/`_stealthMotion`，
    成功路径总写 stealthMotion、非 stealth 再写 motion；`AsCan && playing`、Join、
    load 失败清 content/context/playing，以及 +768 pending flush 均已复刻。成功后的
    raw `type` 分支也已复原：1 进入 `Player_initEmoteMotion@0x6B2E90`，0 进入
    `Player_initNonEmoteMotion@0x6B365C`，其他非零值不初始化。2026-07-19 再联合
    `Player_ctor@0x6CED30`、dtor `0x6CFADC`、`Player_initEmoteMotion@0x6B2E90`、
    draw gate `0x6D5164/0x6D2D80` 与 `findSource@0x6948E8` 复核后，成功/失败、二级
    motion 覆写、析构和全部 live gate 均只消费 +528/+1012；`_activeMotion` 已删除。
37. `Player_initNonEmoteMotion@0x6B365C` 对活动 +528 dispatch 做两次
    `PropGet`：`tag`→+1072、`priority`→+548，并把 priority[0].content CopyRef
    到 +616；`loopTime/lastTime` 也从同一 dispatch 成对读取。本地现已按同样顺序
    建立三个独立 raw variant owner，变量初始化及两只时间标量也优先直接读 +528。
    `Player_advanceRootAndNodes@0x6B6ADC` 与
    `Player_reseekTimelineCursors@0x6B86C8` 对 +1072/+548 的直接 Array dispatch 读取也已
    逐路复原，覆盖前进、后退和完整重定位；旧 snapshot 帧表字段已删除。仅
    snapshot-only 测试/兼容入口仍保留 decoded 时间标量 fallback，该 fallback 归入
    motion/content side graph 缺口，不再属于 tag/priority live 消费链。
38. `Player_buildNodeTree@0x6B51F0` 先 reset node deque，再从 +528 直接取
    `layer` Array 并调 `Player_buildNodeTree_recursive`；`Player_initNodeFields@0x6B3C78`
    复制四只节点级 variant，后置 stencil pass 从 raw mask Array 导航 label map，并把
    type-0/type-3 目标指针压入 type-12 节点的 vector。本地现已按这条 raw 主链构树，
    decoded `layerList/clipList` 不再决定树形、节点数或字段。本轮进一步删除
    `snapshotCompatibility/clipOwner/clipLabel` 构树参数、decoded 递归 helper 及
    `psbNode/emoteEditDict` 节点 owner；节点构造链现不再持有 snapshot side graph。
39. 全仓交叉核实曾确认 `snapshotRegistry/snapshotPathRegistryCompatibility` 只有注册与
    查询、没有 erase/unregister/clear，导致 decoded file/resource tree 可活到进程结束。
    两只 function-static map 及全部注册/查询 API 现已删除；atlas texture 归 RM mapped
    record，离线 `loadMotionSnapshot` 只把 decoded owner 返回给直接调用者，不再写全局状态。
40. `Player_parseFrame@0x6926B4` 与 `Player_mergeFrameContent@0x692AB0` 的完整
    反编译确认旧注释中的“无 live dispatch，因此 decoded PSB 是平台边界”为错误：节点
    现已有 `frameListVariant`。Android 的 parse 只 reset 槽、按 `frameList[index]` 读取
    `time/type/content.mask/act`，不调用 merge；merge 的第三参仍是原始 frameList，并按
    槽内 index 重新取得 content。本轮 live 非根节点已改为 raw `frameListVariant`：
    `reset@0x69260C` 只清 Android 实际清理的前缀/variant/vector size，parse 不再内联 merge，
    merge 保存 `ttstr`/`tTJSVariant` owner并按 32 个数值构造 mesh；`init@0x6B64AC`、
    `advance@0x6B7E44`、reseek/rewind 与 `modified@0x6B6878` 均接入同一两槽链。
    `Player_updateLayers@0x6BB4D4` 又证明 node 0 始终直接复制 delta block，非 PSB frame；
    本地已删除根节点 decoded 分支。`PlayerInternal::parseFrame/evaluateLayerContent` 与
    `PlayerFrameStep/Stepping` 只剩无 live 调用者的 legacy/独立测试模型，不能把它们冒充
    Android 实现，但它们也不再构成 live frame slot 数据流，故该项记为 CLOSED。
41. `Player_updateNodeGeometry@0x6BC4F0` 证明 forceVisible 分支 CopyRef 节点 +1980
    `emoteEdit` variant，经 TJS BOOL getter 读取 `priorDraw` 并 `&1` 写入 node+48，否则写 0。
    本地几何阶段已从 decoded `emoteEditDict` 改用同一 raw variant，删除 Android 不存在的
    并行 decoded owner；此前“node+48 是 `&5` raw bitfield”的错误注释也已就地纠正。
42. `Player_buildNodeTree_recursive@0x6B4A6C` 的每层输入、元素和 `children` 都是
    TJS dispatch variant；`Player_buildNodeTree@0x6B51F0` 只从 +528 读取 `layer` 后递归，
    `Player_updateLayers@0x6BB4D4` 则在进入 index=1 循环前直接把 root delta 复制到
    accumulated。三处交叉证明 decoded 节点同步遍历与 root frame 分支都不是 Android
    数据流；本地已删除这整条 side ownership，而非仅把它标为 inert。
43. `ResourceManager_loadResource@0x6A8D8C`、mapped-record ctor
    `sub_6EBCFC@0x6EBCFC`、node dtor `sub_6DB3E8@0x6DB3E8` 与
    `unload/unloadAll@0x6A959C/0x6A8CF8` 交叉确认：外层 map 的 mapped value 不是单独
    `PSBFile`，而是按 `PSBFile -> Win texture map -> KRKR source-entry map` 声明的记录；
    普通 C++ 逆序析构正好是 KRKR→Win→PSBFile。Win map 按 group 缓存一只 owning texture；
    KRKR map 按完整 `src/group/icon` 缓存 88B 语义 descriptor，atlas miss 会把整组 icon
    直接写入这一张平表。Web 已将两表从 `MotionSnapshot` 移入 `_loadedModules` mapped
    record，三表均按二进制构造时 `rehash(10)`，纹理赋值 AddRef、创建引用随后 Release；
    `unload/unloadAll` 删除记录即同步释放两表。Win/spec=2 纹理现已直接
    从 `record.file` 的 raw `PSBRawNode` 严格导航 `source/group/texture/icon`，
    并按 `0x6948E8` 恢复 `truncated_*` 读取但丢弃、RGBA8 ReverseRGB、
    A8L8 `[alpha,luminance]→[luminance,luminance,luminance,alpha]`、异常与
    icon 几何边界。KRKR/spec=1 也已从 `record.file` 的 raw node 恢复
    all-group/icon 枚举、严格 width/height、raw/RL 4-byte 解码、RL 1-byte+
    palette expand、全透明 2x2 与 descriptor 写表。仅“CPU 组整页后一次
    Update”是已注明的 Web 纹理 API 边界；这关闭 source 像素数据流，不代表
    整个 psbfile/motionplayer 已 CLOSED。
44. 联合反编译 `Player_findSource@0x6948E8`、
    `ResourceManager_findSource@0x6AAB3C`、`SourceCache_loadSource@0x6A7BA8`、
    `ObjSource_getClip@0x69D35C`、`ObjSource_drawLayer@0x69D6D8` 与
    `ObjSource_ensureTexture@0x6DA454`，确认非 atlas source 不从 decoded motion
    side graph 取像素：RM 直接包装 raw icon dict，ObjSource 惰性读取 width/height/
    pixel/compress/pal，执行 RL8/RL32、palette expand 或 ReverseRGB，仅持有一只 texture；
    SourceCache 只调用 `drawLayer(bufLayer)` 后进入颜色 bake。本地已按同一链补齐
    `getClip/drawLayer/ensureTexture`、texture 析构 Release，删除 SourceCache 中的
    `loadPsbBitmap/loadPsbSourceFacade/resolveMotionSourcePath/buildSourceCandidates` 旁路。
    `SourceCache.cpp` 现无 `_activeMotion` 或 `MotionSnapshot` 类型消费；source 数据流与
    ObjSource 生命周期项由 PARTIAL 改为 CLOSED。
45. D3DEmotePlayer 的 `countVariables`、`getVariableLabelAt`、
    `countVariableFrameAt`、`getVariableFrameLabelAt`、
    `getVariableFrameValueAt` 曾错误转发到 Player snapshot 查询。
    `0x53041C/0x530530/0x530568/0x530588/0x5305A8` 证明五者均无条件调用
    `sub_95440C` 抛出各自精确 TODO 文本。旧转发和 Player 上不存在于该 NCB 调用面的
    snapshot 查询现已删除。`EmotePlayer_getVariableRange@0x673BEC` 则先查 Engine HM5，
    hit 时用 value+40/+48 新建 `{min,max}` Dictionary；miss 调 `sub_6D6590`，由
    `sub_6D676C` 扫 Player+384 的 56B 参数条目、对 rangeBegin/rangeEnd 排序折叠，并经
    `Player_visitChildPlayerDispatches@0x6B601C` 递归所有子 Player；只有 min<max 才返回
    Dictionary。`getVariableFrameList@0x68229C` CopyRef Engine+1248 Dictionary 后直接
    PropGet label。本地已按这三条 raw owner/生命周期链复原，不再读取
    `MotionSnapshot::variableLabels/variableFrames/variableRanges`。
46. `Player_ncb_registerMembers@0x6D69C8` 在 `0x6D6CEC` 将
    `Player_getVariableKeys@0x6D139C` 只读绑定到字面量 `L"variableKeys"`。
    getter 每次创建一只新 TJS Array，遍历 Player+1296 的
    `std::deque<VariableLabelScope>`，把每个 160B 语义元素 item+0 的 `cascadeKey`
    作为 string variant 按 deque 顺序追加；空 deque 也返回独立的新空 Array。
    本地旧实现却先 `ensureMotionLoaded()`，再返回由
    `MotionSnapshot::variableLabels` 维护的 `_variableKeys` 持久 Array，数据源、调用链、
    所有权和空状态行为均不一致。现已改为直接遍历 `_variableLabelScopes` 并每次
    `makeArray`；删除 `_variableKeys` 字段、伪 setter、clear 与 snapshot 同步函数，
    原同步函数中无关的 selector 兼容调用仍从原激活站点直接执行。
47. `Player_updateLayers@0x6BB33C` 在 `0x6BB4E0` 复制 root delta、清 dirty 后，
    于 `0x6BB4EC` 无条件调用独立的
    `Player_interpolateVarTrackValues@0x6BBE20`。该函数逐项遍历 Player+1296 的
    160B 语义 var-track deque：按 cursor 选择 active/other 两槽；active type-zero
    时跳过，否则按 interp/type-zero 选择 HOLD 或 LERP；LERP 对 interval 做 floor
    量化，可经 `Player_applyBezierEasing@0x69A754` 修正比例；最后写 item+16 并调用
    `Player_bindParameterValue`。本地这套 raw 实现原已存在，但 updateLayers 的同位置
    却读取 `MotionSnapshot::variableFrames`、只取每条首帧并重放 HM2，形成完全不同的
    数据流。本轮已在同一阶段直接调用 `interpolateVarTrackValuesLike_0x6BBE20`，删除
    snapshot 首帧循环；随后交叉搜索确认 `variableFrames/variableRanges` 仅剩构建端，
    已连同专用 `VariableFrameInfo` 一并删除。当时关于
    `MotionSnapshot::variableLabels` 仍被 serialize/selector 消费的记录已被后续证据
    证伪：Android serialize/unserialize 不经过 Player snapshot，selector 也直接消费
    Engine raw deque。换搜索词并读取调用链后确认该字段只剩 parser 写入和诊断计数，
    因此已同步删除字段、收集函数、写入点与诊断输出，避免保留无消费者的 side graph。
48. `EmotePlayer_ncb_registerMembers@0x67FAC8` 以二进制字面量把
    `serialize`/`unserialize` 分别绑定到 `EmoteEngine_serialize@0x675E40` 与
    `EmoteEngine_unserialize@0x678044`；IDB 中旧的
    `EmoteEngine_reloadVarsDispatch_guess` 已据此纠正并保存。serialize 先调用
    `preProgress(true,0)`，再以 `dt=0` step eye/eyebrow/mouth/selector/transition deque、
    更新 HM7 并应用 base controller，最后返回固定八键 Dictionary：`timeline`、`eye`、
    `eyebrow`、`mouth`、`transition`、`selector`、`base`、`outerforce`。
    `0x66767C` 的通用 controller schema 为
    `phase/tick/speed/exponent/frame/prev/target`，后三项各是逐 channel 的 Array；
    `0x67C094` 的请求队列是 `{p0,p1}` Dictionary Array，只有输入 `rq` 确为 Array
    才清空并重建。unserialize 按同一八键顺序 PropGet；timeline 先无条件
    `stopTimeline("")`，只接受 Array、只恢复 HM3 中已存在 label，之后执行
    play→inclusive window→stopWhenBlendDone/blendRatioCtrl；其余 controller 也只恢复
    当前 deque 中 label 相等的对象，未知 label 忽略。angle restore 保留原版边界：
    `prev` 与 `target` 都写 startRad，targetRad 不变。旧 Player 自造的
    `chara/motion/tickcount/speed/outline/variables/timelines` snapshot schema 及对应方法已删除。
49. 重新反编译 `Player_ncb_registerMembers@0x6D69C8` 后确认，Motion.Player 注册面只有
    `loopTime` 标量与 `variableKeys@0x6D139C`，不存在 selector 属性；`sub_670D1C`
    的全部 xref 也只有 metadata 尾部 `0x67DA8C` 和 selectorEnabled setter
    `sub_681F94@0x681F94`。该函数直接遍历 Engine selector deque、写 entry gate、
    reset/apply 或 restore controller 状态，并对 option 做 removeVariableLabel/reset。
    因此 Player 激活/find/reset 阶段的 selector snapshot 同步、Player 自造 gate，及
    `MotionSnapshot::selectorControls` 解析图均为 Android 不存在的旁路，现已整体删除；
    唯一 owner/调用链回归 Engine raw deque 与上述两个原始调用点。
50. `EmoteEngine_buildMirrorControl@0x66F364` 只把 raw `variableMatchList` 的每个 ttstr
    CopyRef 追加到 Engine+800 vector；`shouldMirror@0x67C6B0` 则受 +1158 gate 控制，
    依次查询 +824/+880 正负 cache，miss 时扫描该 vector，并按结果写回对应 cache。
    交叉搜索确认 snapshot 的 `mirrorVariableMatchList` 只有 parser 写入与诊断计数，
    没有运行时消费者，现已连同 collector 和诊断字段删除，mirror 只保留 Engine 原生
    vector/HM owner、substring 边界和 cache 生命周期。
51. `EmoteEngine_applyMetadata_buildControllers@0x67D4D0` 直接按
    variableList→bust→hair→parts→eye→eyebrow→mouth→transition→selector→loop→
    clamp→mirror→instantVariable→timeline 的顺序调用各 typed builder，最后进入
    `sub_670D1C`；二进制中不存在 generic `VariableControllerBinding` 或再次排序生成
    fixed output 的中间层。交叉搜索确认 Web 的两张表只有互相构建和诊断计数、没有
    live reader，现已删除结构、字段、collector、排序函数与诊断输出。Engine typed deque、
    HM6/HM7/HM3 和 TJS 容器成为 controller metadata 的唯一 live 数据流。
52. `Player_initEmoteMotion@0x6B2E90` 现按原始顺序将 cameraAngle(+472) 与
    emoteAngle(+464) 相加并循环归一化，在 +484 division 的相邻区间中选择
    `index % count`，以 +504 去重，从 +508 motionList 取字符串、用共用
    `sub_697D34@0x697D34` 语义拆分 `/`，再以第三段调用 `Player_loadMotion@0x6B0F10`；
    成功覆盖 +528/+1012 并进入 non-emote init，失败清空二者。+484/+508 是
    `tTJSVariant` owner，+504 按 binary 保持 ctor 不初始化、由 type-1 play 先写 -1。
    对 `Player_initEmoteMotion` 的 9 个 code xref 已全部复核并接入；调用可能重建节点树，
    因此 child/particle caller 在调用后重新取得 root 节点，不保留失效引用。

## 验证

- 当前 macOS Release `psbfile-dll`：5 test cases、437 assertions 全部通过。测试 target
  已链接 `krkr2plugins_ncbind`，补齐 `TVPGetD3DImageNative`/
  `TVPGetD3DImageScaleX` 所属 D3D bridge；这是测试构建接线修复，不改变产品实现。
- 当前 macOS Release `motionplayer-dll`：14 test cases、398 assertions 全部通过。
- 当前 Web Debug：曲线 raw-owner/离线类型剥离后的增量目标完成，`index.html` 最终链接通过。
- Web Debug：本轮完成 217/217 targets，`index.html` 完整链接通过；
  `ResourceManager` 内联容器、D3DImage owner 与 last-loaded 状态删除均进入最终 wasm 链接。
- 已覆盖 owner 替换/析构后 node 生存、raw dictionary/array、日文 name decode、
  TJS property/negative index、双接口 native instance、refcount、invalidate 独立性、
  真实加密 motion PSB filter。
- `setEmotePSBDecryptSeed` 聚焦测试覆盖零参数报错、额外参数接受、real/string 到整数
  的 TJS 转换；`setEmotePSBDecryptFunc` 聚焦测试 1 case / 12 assertions 全部通过，覆盖
  零参数、普通对象转换、额外参数、闭包持有、真实加密 PSB 的二参数调用与旧闭包释放。
  完整 motionplayer suite 仍有 4 个既有资源/运行时失败，因此不记为全绿。
- 新增 ResourceManager raw 生命周期聚焦测试：23 assertions 全部通过，覆盖同一路径
  返回不同 dispatch、native instance、snapshot 过渡映射、unload 后外部引用存活。
- macOS Debug `motionplayer-dll` 完整重编译/链接通过；运行结果为 12 cases 中
  8 通过、4 个既有资源/运行时失败，188 assertions 中 184 通过；单一 RM 生命周期
  以及本轮 RM 内联容器、D3DImage owner、last-loaded 删除都没有新增失败或崩溃。
- macOS Release 在重新配置后完成 753/753 全量编译和 `motionplayer-dll` 链接；
  `motionKey/project` 单槽合并后的运行结果仍为 12 cases 中 8 通过、4 个同样的既有
  资源/运行时失败，188 assertions 中 184 通过，没有新增失败。
- Web Debug 在同一修正后重新配置并完成 231/231 全量编译，`index.html` 最终链接通过。
- raw EmoteObject 初始化链修正后，Web Debug 再次完成 217/217 增量 targets 并链接
  `index.html`；macOS Release 的 `motionplayer` 静态库与 `krkr2` app 最终链接通过。
  `motionplayer-dll` 重新链接后仍为 12 cases 中 8 通过、4 个相同既有失败，
  188 assertions 中 184 通过，没有新增失败。当时阻断 macOS `all` target 的
  `motionplayer-ttstr-hash-test.cpp` 陈旧断言已在本轮按已证实的 HM1/HM3/var-track
  字段同步修正，不再引用 `DispatchAliasMap` 或旧 `EvalCascadeState` 字段。
- metadata 接收者归位后，macOS Release 指定目标 65/65、Web Debug 56/56 均完成最终
  链接；`motionplayer-dll` 结果仍为 8/12 cases、184/188 assertions，无新增失败。
- raw mirror/scale 分支恢复后两端再次完成同样的 65/65 与 56/56 链接；测试基线仍为
  8/12 cases、184/188 assertions，退出码 42 来自相同四个既有失败。
- `loopControl@0x66E480` raw 迁移后两端再次完成 65/65 与 56/56 链接；完整
  `motionplayer-dll` 仍为同一 8/12、184/188 基线。
- `transitionControl@0x66D4C4` raw 迁移后，macOS Release 指定目标 65/65、Web Debug
  56/56 均完成最终链接。完整 `motionplayer-dll` 连续两次随机 seed 运行均为 8/12
  cases、退出码 42；一次因资源用例更早在 `isExistMotion` 的 `REQUIRE` 退出而得到
  173/177 assertions，另一次运行到后续 `layerNames` 断言而得到 184/188。稳定的四类
  既有失败是 timeline internal error、logo layers 0/15、resource layerNames 为空和
  `findSource` 返回 void；断言总数受随机测试顺序与共享状态影响，不能当成固定基线。
- variableList/selector/reset 容器拓扑迁移后，macOS Release 指定目标完成 81/81，
  Web Debug 完成 70/70 并最终链接；`git diff --check` 通过。完整
  `motionplayer-dll` 仍为 8/12 cases、184/188 assertions、退出码 42，四类失败与上述
  基线完全相同，没有新增失败或崩溃。现有 36 个 mtn/e-mote PSB 资产均不覆盖
  variableList/selector，因此遵守物料约束，没有伪造新 fixture。
- raw eye builder/controller 迁移后，macOS Release 完成 249/249（含 app 与
  `motionplayer-dll`），Web Debug 完成 166/166 并链接 `index.html`；完整套件仍为
  8/12 cases、184/188 assertions、退出码 42，失败仍仅是 findSource、logo layers、
  resource layerNames 与 timeline Internal error，未新增失败或崩溃。
- raw eyebrow builder/slim-controller 迁移后，macOS Release 完成 249/249，Web Debug
  完成 194/194 并链接；完整套件仍为 8/12 cases、173/177 assertions、退出码 42。
  断言数变化来自已知随机顺序/资源用例提前退出，失败类别未变。
- raw mouth builder/controller 迁移后，macOS Release 完成 249/249，Web Debug 完成
  193/193 并链接 `index.html`；完整套件仍为 8/12 cases、184/188 assertions、退出码
  42。失败仍是相同四类既有资源/运行时问题，没有新增失败或崩溃。
- clamp/mirror/instant/timeline raw builder、mirror raw consumer 与 selector partial
  命名纠正合并后，Web Debug 完成 188/188 并链接 `index.html`；macOS Release
  `motionplayer-dll` 指定目标重新链接通过。`motionplayer-ttstr-hash-test` 同步到当前
  HM1/HM3/var-track 模型后为 22 cases、100 assertions 全绿。完整
  `motionplayer-dll` 仍为 8/12 cases、173/177 assertions、退出码 42，失败类别仍仅是
  findSource void、logo layers 0/15、resource isExistMotion/shared-state 与 timeline
  Internal error；`git diff --check` 通过。macOS 全 `all` target 当前另被既有 `tjs`
  链接配置缺少 expat（`_XML_Parse` 等未定义）阻断，与目标插件编译无关。
- raw `bustControl`/spring ctor/vec3 reader 迁移后，macOS Release
  `motionplayer-dll` 指定目标完成 218/218，Web Debug 完成 195/195 并链接
  `index.html`。完整 `motionplayer-dll` 仍为 8/12 cases、173/177 assertions、
  退出码 42；失败类别仍是 logo layers 0/15、findSource void、resource
  isExistMotion/shared-state 与 timeline Internal error，没有新失败或崩溃；
  `git diff --check` 通过。
- raw hair/parts chain builder 与 176B ctor 迁移后，macOS Release
  `motionplayer-dll` 指定目标完成 218/218，Web Debug 完成 195/195 并链接
  `index.html`。完整套件为 8/12 cases、184/188 assertions、退出码 42；
  失败仍仅是 logo layers 0/15、timeline Internal error、findSource void 和
  resource layerNames 为空，没有新失败或崩溃。
- 完整 selector-sync、三只 selector-target 入口及 NCB activate 注册恢复后，macOS
  Release `motionplayer-dll` 完成 211/211 并链接。此前仅 sync 阶段两端已通过，完整
  target 恢复后的 Web Debug 完成 188/188 并链接 `index.html`；完整套件仍为 8/12
  cases、184/188 assertions、退出码 42。失败仍是既有 findSource、timeline、
  resource layerNames 与 logo 0/15 四类，没有新增失败或崩溃；`git diff --check` 通过。
- clamp live consumer `sub_67C8A8` 归回 Engine raw deque/HM7 后，macOS Release
  `motionplayer-dll` 完成 57/57，Web Debug 完成 54/54 并链接 `index.html`。完整套件
  仍为 8/12 cases；本次随机顺序为 173/177 assertions、退出码 42，失败仍是既有
  resource isExistMotion/shared-state、timeline Internal error、findSource void 与
  logo layers 0/15 四类，没有新增失败或崩溃；`git diff --check` 通过。
- Engine HM3 timeline 消费/query 迁移与 D3DImage listener/update/draw bridge 恢复后，
  macOS Release `motionplayer-dll` 成功链接；完整套件仍为 8/12 cases、
  173/177 assertions，仍是 timeline Internal error、logo layers 0/15、resource
  isExistMotion/shared-state 和 findSource void 四类既有失败。Web Debug 随后全量
  完成 191/191 并链接 `index.html`；新的 raw listener vtable、原生 target 纹理
  与 HM3/+1040 时间线都已进入最终 wasm，只有既有编译警告。
- raw tag/priority 帧流与 raw NodeTree 构造链迁移后，macOS Release
  `motionplayer-dll` 再次成功链接；完整套件仍为 8/12 cases、184/188 assertions，
  失败仍归于 timeline Internal error、logo layers 0/15、resource shared-state/layers
  与 findSource void 四类既有入口，未新增崩溃。Web Debug 随后完成 65/65 增量目标并
  重新链接 `index.html`；最终一次 `git diff --check` 通过。
- live raw frame slot、raw priorDraw 及 NodeTree snapshot side graph 删除后，macOS Release
  `motionplayer-dll` 完成 218/218 并链接，Web Debug 完成 202/202 并链接 `index.html`；
  完整套件仍为 8/12 cases、184/188 assertions、退出码 42，失败仍是既有
  findSource void、timeline Internal error、resource layerNames 为空与 logo layers 0/15。
  `git diff --check` 通过，全仓已无 `psbNode/snapshotCompatibility/emoteEditDict` 节点引用。
- source mapped-record 与两张内层纹理表迁移后，macOS Release `motionplayer-dll` 完成
  218/218 并链接，Web Debug 完成 203/203 并链接 `index.html`。完整
  `motionplayer-dll` 仍为 8/12 cases、184/188 assertions、退出码 42；四类失败
  仍是 timeline Internal error、logo layers 0/15、resource layerNames 为空与
  findSource void，没有新失败或崩溃。纹理不再由 `MotionSnapshot` 析构；
  Win/KRKR 表随 RM 外层节点释放。该阶段尚保留 decoded snapshot 像素导航，
  因此当时只验证容器/所有权迁移的非回归，未把 source 数据流记为关闭；
  `git diff --check` 通过。
- Win/spec=2 source 像素链迁到 raw `PSBRawNode` 后，macOS Release
  `motionplayer-dll` 与 Web Debug 均完成 14/14 增量目标并链接；完整套件
  仍为 8/12 cases、184/188 assertions、退出码 42，失败类别未变。
  `git diff --check` 通过。当时 KRKR/spec=1 atlas 仍待 raw 迁移。
- KRKR/spec=1 source 像素链迁到 raw `PSBRawNode` 后，macOS Release
  `motionplayer-dll` 完成 218/218 并链接，Web Debug 完成 203/203 并链接
  `index.html`；完整套件仍为 8/12 cases、184/188 assertions、退出码 42，
  失败类别未变。`PlayerResource.cpp` 已无 `MotionSnapshot`、decoded dictionary、
  `findPSBResourceBySourceName` 或 `navigatePSBPath` 依赖；`git diff --check` 通过。
- Player 六槽/两个 pending owner 及 child/particle flush 调用链复原后，macOS Release
  `motionplayer-dll` 完成 218/218 并链接；`psbfile-dll` 仍为 377/377，完整
  `motionplayer-dll` 仍为 8/12 cases、184/188 assertions、退出码 42，失败仍是
  findSource void、logo 0/15、resource layerNames 为空与 timeline Internal error
  四类既有问题。Web Debug 完成 140/140 并重新链接 `index.html`。
- SourceCache/ObjSource raw source 链复原后，macOS Release `motionplayer-dll`
  完成 218/218 并链接，Web Debug 完成 142/142 并链接 `index.html`；
  `psbfile-dll` 仍为 377/377。完整 `motionplayer-dll` 仍为 8/12 cases、
  184/188 assertions、退出码 42，失败仍是既有 findSource void、logo 0/15、
  resource layerNames 为空与 timeline Internal error 四类，没有新增失败或崩溃；
  `git diff --check` 通过。
- 变量查询边界复原后，macOS Release `motionplayer-dll` 完成 211/211 并链接；独立
  D3D TODO 用例 1 case / 10 assertions 全绿。完整套件为 9/13 cases、194/198
  assertions、退出码 42，仍只有既有 findSource void、logo 0/15、resource layerNames
  为空与 timeline Internal error 四类失败。Web Debug 完成 147/147 并链接
  `index.html`，wasm32 异常、HM5、+1248 Dictionary 与参数表递归链均进入最终产物。
- Player `variableKeys@0x6D139C` 迁回 var-track deque 后，macOS Release
  `motionplayer-dll` 完成 211/211 并链接；独立生命周期/空 deque 用例为 1 case、
  11 assertions 全绿。完整套件为 10/14 cases、205/209 assertions、退出码 42，
  仍只有既有 findSource void、logo 0/15、resource layerNames 为空与 timeline
  Internal error 四类失败。Web Debug 完成 148/148 并链接 `index.html`，最终
  `index.wasm` 通过 `wasm-validate --enable-all`；`git diff --check` 通过。
- `Player_updateLayers@0x6BB33C → Player_interpolateVarTrackValues@0x6BBE20`
  调用链恢复并删除 snapshot frame/range 派生表后，macOS Release
  `motionplayer-dll` 完成 218/218 并链接；独立 variableKeys 用例仍为 11/11，完整
  套件仍是 10/14 cases、205/209 assertions、同四类既有失败。Web Debug 完成
  157/157 并链接 `index.html`，`index.wasm` 通过
  `wasm-validate --enable-all`；`git diff --check` 通过。
- `EmotePlayer.serialize/unserialize` 迁回 Engine raw 状态链、并删除最后的 snapshot
  `variableLabels` 派生表后，macOS Release `motionplayer-dll` 完成 218/218 并链接；
  新增固定八键 schema、controller/rq round-trip 与 angle quirk 聚焦用例为
  1 case / 88 assertions 全绿。完整套件为 11/15 cases、293/297 assertions、退出码 42，
  仍只有既有 findSource void、logo layers 0/15、resource layerNames 为空与 timeline
  Internal error 四类失败。Web Debug 完成 158/158 并链接 `index.html`，最终
  `index.wasm` 通过 `wasm-validate --enable-all`；`git diff --check` 通过。
- selector/mirror/generic-controller 重复 side graph 删除后，macOS Release
  `motionplayer-dll` 完成 218/218 并链接；完整套件为 11/15 cases、293/297
  assertions、退出码 42，失败仍只有既有 findSource void、logo layers 0/15、
  resource layerNames 为空与 timeline Internal error。Web Debug 完成 162/162 并链接
  `index.html`，最终 `index.wasm` 通过 `wasm-validate --enable-all`；残留符号扫描无
  代码引用，`git diff --check` 通过。
- clamp owner 最终收口后，fresh xrefs 证明 `sub_67C8A8@0x67C8A8` 的 live caller
  只有 `EmoteEngine_progress@0x67D01C`；`sub_67CC9C@0x67CC9C` 无任何 caller。
  因此已删除 caller-less 的本地 Player 包装、Player mirror helper 与
  `MotionSnapshot::clampControls` 派生表，不再以“保留死函数模型”为由制造 Android
  不存在的对象状态。
- `skip@0x66EB8C` 与 `setMirror@0x671DB0` 的 fresh decompile 推翻了本地“Player
  stopTimeline/轻量 reset”模型：`skip` 会提交或结束活动 HM3 timeline，并依次复位
  var、spring、eye、eyebrow、mouth、selector、transition、position、scale、angle、
  color 控制器；`setMirror` 先计算 requested/base 差异、更新 Player root flip，再走
  完整同一复位链。对应 `0x669D10/0x66713C/0x663AA0/0x6654C4/0x668394/
  0x66A42C/0x6CD068` 已逐个反编译并复刻，metadata 的 `mirror` 也纠正为 base 值而非
  requested 值。macOS Release `motionplayer-dll` 完成 246/246 并链接；完整套件为
  11/15 cases、282/286 assertions、退出码 42，仍只有既有 logo layers 0/15、
  resource isExistMotion、findSource void 与 timeline Internal error 四类失败，没有
  新失败或崩溃；`git diff --check` 通过。
- `D3DEmotePlayer_setTimeline@0x5308A4` 的四条 ARM64 指令证明它不是
  `(label,bool) -> Player::playTimeline` 的兼容入口，而是把 receiver 改写为 Engine、
  mask 第二参数，并把 `(value,transition,easingWeight)` 三只 FP 参数原样尾调用
  `sub_6735AC@0x6735AC`。本地已将 NCB 故意错配名 `setTimelineBlendRatio` 的 callback
  恢复为五参数 Engine HM3 blend-controller 直通，删除该入口最后一条 snapshot
  timeline 调用链。macOS Release `motionplayer-dll` 完成 57/57；完整套件仍为
  11/15 cases、282/286 assertions，仅四类既有失败。Web Debug 完成 190/190 并链接
  `index.html`，最终 `index.wasm` 通过 `wasm-validate --enable-all`；
  `git diff --check` 通过。
- 独立 `psbfile-dll` 本轮重链被测试 target 缺少 `TVPGetD3DImageNative` 与
  `TVPGetD3DImageScaleX` 两个 D3D bridge 符号阻断；这是测试链接配置缺口，不据此
  推断插件行为。此前已有的 377/377 独立 PSB 断言仍保留为历史验证，本轮不伪称
  得到了新的独立全绿结果。
- `Player_skipToSync@0x6D3504` 与 `tags@0x6D9618` 复原后，macOS
  Release `motionplayer-dll` 完成 211/211 并链接；完整套件仍为
  11/15 cases、282/286 assertions、退出码 42，仅有既有 logo 0/15、
  resource `isExistMotion`、`findSource` void 与 timeline Internal error 四类失败。
  Web Debug 完成 159/159 并链接 `index.html`，最终 `index.wasm`
  通过 `wasm-validate --enable-all`；`git diff --check` 通过。不会用缺少
  直接 tag fixture 来阻止这段有反编译证据的忠实复刻。
- `Player_initNonEmoteMotion@0x6B365C` 参数表 raw 迁移后，macOS
  Release `motionplayer-dll` 完成 218/218 并链接；完整套件仍为
  11/15 cases、282/286 assertions、退出码 42，只有原四类失败。
  Web Debug 完成 162/162 并链接 `index.html`，`index.wasm` 通过
  `wasm-validate --enable-all`，`git diff --check` 通过。聚焦符号扫描已无
  `motionObject/contentObject` 或 decoded parameter helper 引用；剩余
  `MotionSnapshot/_activeMotion` 引用由 237 降至 235，仍分布于 25 个文件。
- `Player+1092 preview` / `Player+482 directEdit` 拆分纠正后，macOS Release
  `motionplayer-dll` 完成 211/211 并链接；完整套件为 11/15 cases、293/297
  assertions，仍只有既有 logo layers 0/15、resource layerNames 为空、timeline
  Internal error 与 findSource void 四类失败。Web Debug 完成 166/166 并链接
  `index.html`，最终 `index.wasm` 通过 `wasm-validate --enable-all`；
  `git diff --check` 通过。`MotionSnapshot/_activeMotion` 残留仍为 235 处/25 文件，
  说明本轮删除的是错误模式 owner，而不是尚未关闭的 snapshot 主旁路。
- raw type/Emote 二级 motion 链及全部 9 个 caller 接入后，macOS Release
  `motionplayer-dll` 增量构建完成 42/42；完整套件仍为 11/15 cases、293/297
  assertions，仅有既有 logo layers 0/15、resource layerNames 为空、timeline
  Internal error 与 findSource void 四类失败。Web Debug 同样完成 42/42 并链接
  `index.html`，`index.wasm` 通过 `wasm-validate --enable-all`，`git diff --check`
  通过。`MotionSnapshot/_activeMotion` 残留为 236 处/25 文件；新增的一处是二级
  motion 加载仍需经过兼容 snapshot owner，故不能把本 vertical 记为端到端 CLOSED。
- 删除 decoded clip/layer/source side graph 后，`MotionSnapshot/_activeMotion` 残留降为
  223 处/25 文件；`MotionClip/sourceCandidates/clipList/clipIndex/layerList/_activeClip`
  在 motionplayer 与单测目标中均为零引用。后续 fresh decompile 确认 `0x66F80C`
  实为 `EmoteEngine_buildTimelineControl`，唯一 caller 是 `0x67D4D0`；本地 Engine
  +992/+1016/HM3 实现已经复刻，故删除了错误挂在 Player 上且读取 snapshot labels 的
  `primaryTimelineStateLike_0x66F80C` 重复实现。macOS Release `motionplayer-dll` 成功链接，
  完整套件仍为 11/15 cases、293/297 assertions，失败集合仍是既有 logo layers
  0/15、resource layerNames 为空、timeline Internal error 与 findSource void 四类；
  Web Debug 完成 173/173 并链接 `index.html`，`index.wasm` 通过
  `wasm-validate --enable-all`，`git diff --check` 通过。
- 继续 fresh decompile `Player_renderToCanvas@0x6C7440` 后确认：目标宽高只从传入
  Layer dispatch 的 `width`/`height` 属性读取，Android 路径不存在“扫描整份 decoded
  PSB 后取首个 width/height”这一 fallback。故删除 `MotionSnapshot::width/height`、
  decoded 扫描赋值、两处 render fallback 及无调用的 Player getter；无效目标尺寸仍沿用
  本地既有的失败返回边界。同时 fresh decompile
  `EmoteEngine_buildTimelineControl_guess@0x66F80C` 确认它清空并重建的是 Engine
  +992/+1016 label vectors 与 +936 HM3，唯一 caller 为
  `EmoteEngine_applyMetadata_buildControllers@0x67D4D0`；本地 Engine 实现已经承担该
  调用链，因此删除错误挂在 Player、读取 snapshot label 表且无 caller 的重复函数。
  本轮后 `MotionSnapshot/_activeMotion` 残留为 211 处/25 文件。macOS Release
  `motionplayer-dll` 完成 218/218 并成功链接；完整套件仍为 11/15 cases、293/297
  assertions、退出码 42，失败集合仍是既有 logo layers 0/15、resource layerNames
  为空、timeline Internal error 与 findSource void 四类。Web Debug 完成 174/174 并
  链接 `index.html`，最终 `index.wasm` 通过 `wasm-validate --enable-all`；
  `git diff --check` 通过。

- fresh decompile `Player_ncb_registerMembers@0x6D69C8`、
  `EmotePlayer_ncb_registerMembers@0x67FAC8`、`EmoteEngine_playTimeline@0x672F70`、
  `EmoteEngine_stopTimeline@0x67C2A0`、`EmoteEngine_progress@0x67D01C`、
  `EmoteEngine_buildTimelineControl@0x66F80C` 与 metadata caller `0x67D4D0` 后，确认
  timeline 的公开 API、HM3、active-label vector 和 controller 生命周期全部归属
  EmoteEngine；Motion.Player 的 92 项注册表没有 timeline 方法。本轮删除 Player
  `_timelines/_playingTimelineLabels`、`TimelineState`/control animator 图、decoded
  timelineControl/label/loop/total 派生表及全部 Player timeline 查询/播放面；
  `Player_progressCompat@0x6D2A98` 不再伪造返回进度，`Player_updateLayers@0x6BBB74`
  的位移门控改回 `_queuing`。`sub_6C0DE8` 又确认 child 完成时清的是 +1296 var-track
  deque，本地已将错误的 timeline clear 改为 `_variableLabelScopes.clear()`。
  macOS Release `motionplayer-dll` 完成 218/218 并链接；完整套件为 11/14 cases、
  251/254 assertions，剩余三类既有失败是 PIMG motion 判定、EmotePlayer timeline
  fixture 的 Internal error 与 findSource void。被删除的第 15 个 case 直接调用 Android
  Motion.Player 不存在的 timeline API，不能作为有效 oracle。Web Debug 完成 177/177
  并链接 `index.html`，`index.wasm` 通过 `wasm-validate --enable-all`，
  `git diff --check` 通过。`MotionSnapshot/_activeMotion` 残留由 211 降为 166 处、仍为
  25 个文件；全仓已无 snapshot↔timeline 交叉消费。

- 2026-07-19 继续 fresh decompile `ResourceManager_loadResource@0x6A8D98`、
  `EmoteEngine` instantVariableList builder `0x66F64C`、
  `Motion_Player_findSource@0x6948E8` 与 `SourceCache_loadSource@0x6A7BA8`：Android
  的 `spec` 只写入并读取 `ResourceManager+224`，instant-variable 项逐条进入
  `EmoteEngine+1272`，source 解析直接消费 RM HashMap-A raw root 及 record 内嵌纹理
  map；不存在 snapshot 级 `sourceSpec/instantVariableLabels/resourceAliases` 副本，
  也不存在 `psb://` 别名候选或全资源表后缀扫描。故删除上述三字段、无效果的 eager
  `scanValue` 全树遍历，以及无 caller 的
  `resolveMotionSourcePath/pushGraphicCandidates/appendEmbeddedSourceCandidates` 链；
  `resourcesByPath/root/file` 暂保留给离线 `mtndump/motionsim`，不伪称整个 decoded
  snapshot 已关闭。`MotionSnapshot/_activeMotion` 残留降至 155 行/23 文件。
  macOS Release `motionplayer-dll` 完成 218/218，Web Debug 完成 181/181 并链接
  `index.html`；完整单测为 11/14 cases、262/265 assertions，仍是既有 PIMG motion
  判定、findSource void 与 EmotePlayer timeline fixture Internal error 三类失败。
  本机当前无 `wasm-tools/wasm-validate`，因此本轮只记录 Web 链接成功，不冒充完成
  独立 wasm validator 校验。`mtndump` 已改为从真实
  `source/<group>/icon/<name>/pixel` 资源节点枚举，不再依赖已删除的
  `sourceCandidates`；目标构建通过，并用现有加密 fixture（seed `742877301`）成功导出
  114 张源图、0 skipped。联合工具构建还揭示 `motionsim` 仍引用已删除的
  `layerList/clipList/tagFrames` 等十处 eager snapshot 字段，这是独立的工具迁移缺口，
  不能通过恢复 Android 不存在的 snapshot side graph 修补。`git diff --check` 通过。

- 2026-07-19 fresh decompile `ResourceManager_loadResource@0x6A8D8C`、
  `ResourceManager_findMotion@0x6A9ED4`、`ResourceManager_unload@0x6A959C`、
  `ResourceManager_unloadAll@0x6A8CF8`、`Player_loadMotion@0x6B0F10`、
  `Player_playImpl@0x6B2284` 与 `Player_isExistMotion@0x6D07F4` 后，删除 raw load 的
  二次 decoded parse、path/dispatch 两只全局 snapshot registry 及其测试契约。
  `ensureMotionLoaded` 直接保存 `[motionValue,matchedKey]`，只为尚未迁移的下游建立
  不持有 file/root/resource 的路径标记。该阶段 macOS Release 完成 218/218，Web Debug
  完成 181/181；完整单测为 11/14。该阶段运行时 `MotionSnapshot/_activeMotion` 残留为
  142 行/22 文件；两只全局 registry 已归零。

- 2026-07-19 继续 fresh decompile `Player_findMotion@0x6D004C`、
  `Player_isExistMotion@0x6D07F4`、`Player_loadMotion@0x6B0F10`、
  `Motion_propGetBool@0x6636D4` 与 `Player_initNodeFields@0x6B3C78`，并用全编码
  字符串/Xref 交叉检查 NCB 注册表。Player 的三条 motion 查询均已恢复为通过
  `Player+992` ResourceManager dispatch 调用 `findMotion/isExistMotion`；本地不再从
  native RM 直读或尝试扩展名/路径候选。二进制注册证据还确认 `unload/unloadAll` 属于
  ResourceManager，`motionList` 只是 PSB key，`debugPrint` 不是 Player 接口，因此删除
  Player 上这些 port-only 成员以及错误的单参数 `findMotion`。`_motionsByKey`、
  `cacheMotion/resolveMotion` 与全部路径候选 helper 已归零。bool 属性读取也恢复为
  `Motion_propGetBool` 的 variant truth 转换，`meshCombine` 保留一次 MEMBERMUSTEXIST 探测
  后再执行第二次 PropGet。macOS Release `motionplayer-dll`、Web Debug 214/214 和
  `mtndump` 均构建成功；资源链聚焦用例 110 assertions、绘制/播放聚焦用例
  20 assertions 全部通过。当阶段完整套件为 13/14 cases、388/389 assertions，唯一失败落在
  `D3DEmotePlayer::pass → Player::progressFramesLike_0x6D2A54 →
  Player::advanceRootAndNodes_0x6B6ADC → motionPropGetDouble`，真实事件流值为 void 时触发
  `Cannot convert the variable type (() to Object)`。这条失败在当时是不能给出 100% 结论的
  直接运行时证据，不能在未完整反编译事件流链前猜测修补；后续条目已按完整事件流
  反编译证据关闭它。motionplayer 目录内
  `_activeMotion` 仍有 95 行、分布 15 个文件；连同其 `MotionSnapshot` 类型消费者合计
  126 行、分布 21 个文件；`_motionsByKey` 已为零。

- 2026-07-19 fresh decompile `Player_advanceRootAndNodes@0x6B6ADC`、
  `Player_rewindRootAndNodes@0x6B9A3C`、`Player_reseekTimelineCursors@0x6B86C8`、
  `Player_initNonEmoteMotion@0x6B365C` 与 `Player_ctor@0x6CED30` 后，确认 Android 将
  tag/root 前进与后退实现为四段方向独立的增量流；前进入口不预读 `frame[0]/frame[1]`，
  也不存在按 dispatch 指针身份重置 cursor 的两个额外 owner。Web 已拆分四个 helper，
  删除 `_layerStreamSource/_rootStreamSource` 和 reseek 的 `_activeMotion` 守卫，raw count/
  property 读取恢复 TJS 原始转换边界。原 `void → Object` 失败消失。又由
  `fadeOutTimeline@0x6739F4`、`isTimelinePlaying@0x673558`、
  `EmoteEngine_preProgress_guess@0x671764` 和 `Player_getAllplaying@0x6CCE34` 纠正两条
  陈旧测试契约：fadeOut 是异步 auto-stop，且 `getAnimating` 是 Player motion 状态，
  不是 Engine 活动 timeline 向量。macOS Release `motionplayer-dll` 当前为
  **14/14 cases、395/395 assertions**；独立 macOS Release `psbfile-dll` 已修复测试
  target 的 D3D bridge 链接接线，当前为 **5/5 cases、437/437 assertions**。fresh
  decompile `Player_getLayerNames@0x6D10E0`、`getLayerGetterList@0x6D4F88`、
  `Player_hitTestLayer@0x681B0C` 与 `getCommandList@0x6D3A4C` 确认四个 Android 入口都
  不存在 motion/snapshot gate；Web 已删除 `PlayerLayerQuery.cpp` 对应四个
  `_activeMotion` 早退，空 map/deque 自然生成空结果。静态复核当前仍有
  `MotionSnapshot/_activeMotion` 121 处、20 个 motionplayer 文件。fresh decompile
  `EmotePlayer_ncb_registerMembers@0x67FAC8` 纠正了 `initPhysics` 的旧误判：二进制字面
  名称直接绑定 `EmoteEngine_applyMetadata_buildControllers@0x67D4D0`，不是尚未移植的
  五标量 physics helper；Web 已将该 NCB 入口接到完整 raw metadata builder，并删除
  D3D/Player 两个无二进制成员证据的空桩。fresh decompile
  `Player_rewindRootAndNodes@0x6B9A3C`、`Player_advanceNodeFrames@0x6B7E44`、
  `Player_initNodeTimeline@0x6B64AC` 与 `Player_reseekTimelineCursors@0x6B86C8` 又证明
  反向 node-layer 帧槽路径已经闭合：reverse 四流按 layer→root→var-track→node 执行，
  非参数节点进入 `0x6BA1CC` 单向后退，参数节点进入 `0x6B7E44`，reseek 对所有非 root
  node 先做双槽绝对播种。Web 已删除二进制不存在的“slot 均未初始化时 lazy seed”兜底；
  fresh decompile `D3DEmotePlayer::assignState@0x530150` 与异常 helper
  `sub_95440C@0x95440C` 又纠正了“assignState 仅记 TODO 日志并返回”的旧实现：Android
  先强制 Object 转换，非空时以 D3DEmotePlayer class id 调用 `NativeInstanceSupport`，
  随后始终抛出精确 TODO `eTJSError`。Web 已恢复同一参数、原生实例探测和异常边界，
  对应回归测试进入当前 **14/14 cases、397/397 assertions**。本轮 `Player.h` 触发的
  Web Debug 完整重编译随后完成 **179/179** 并链接 `index.html`。这些事实继续
  阻止 100% 结论。

- 2026-07-19 fresh decompile `Player_loadMotion@0x6B0F10`、
  `Player_playImpl@0x6B2284`、`Player_ctor@0x6CED30`、dtor `0x6CFADC`、共享 chara
  writer `0x6B29C0`、`Player_initEmoteMotion@0x6B2E90`、draw/build gate
  `0x6D5164/0x6D2D80/0x6D5FB8/0x6D5B90/0x6D5C68` 及
  `Player_findSource@0x6948E8` 后，确认 Android Player 没有额外活动文件 owner：
  result[0]/result[1] 的唯一 owner 分别是 +528/+1012，所有 live loaded gate 读
  +528 type，路径/资源导航读 +1012。本地据此删除 `_activeMotion`、`activateMotion`
  及全部 lazy snapshot gate/reset，显式 load 始终调用 RM；`chara` 修改也不再错误清
  +528/+1012。静态复核 Player live 路径 `_activeMotion` 为 **0**；剩余
  `MotionSnapshot` 为 **22 处/9 文件**，均属离线 decoder/helper 或 legacy test model。
  macOS Release `motionplayer-dll` 为 **398/398**、`psbfile-dll` 为 **437/437**，Web
  Debug 完整链接通过。另 fresh decompile `ResourceManager_random@0x6AB56C` 与
  `D3DEmotePlayer_getOuterForce@0x530B28`，纠正 random 的非 Object 转换边界及
  getOuterForce 的无条件 TODO 异常。该阶段的 NO 结论不再由 runtime snapshot 双轨导致，
  而由当时尚存的生产头文件 decoded compatibility 模型、live slot mirror 等更下层结构偏差
  导致；生产头文件泄漏已由后续 offline-only 拆分闭合。

- 2026-07-19 fresh decompile `Player_ctor@0x6CED30`、`Player_dtor@0x6CFADC`、
  `Player_loadMotion@0x6B0F10` 与 `motionplayer_ncb_register@0x6D9B08` 后，将 eager
  snapshot loader、`PlayerFrameStep`、`PlayerFrameStepping` 拆入 EXCLUDE_FROM_ALL 的
  `motionplayer_offline`，并将 `psbfile_decoded_compat` 改为 EXCLUDE_FROM_ALL；只有
  `mtndump`、`motionsim` 和显式单测链接它们。生产 `main.cpp` 不再 include `PSBFile.h`，
  避免 Android 注册链中不存在的 DecodedPSB TypeHandler 静态构造。清理旧对象后 Web Debug
  全量 **339/339** 链接通过；最终依赖图只含 `libmotionplayer.a` 与 raw `libpsbfile.a`，
  不含两个 offline/compat 库或其三个编译单元。`motionplayer-dll` **398/398**、
  `psbfile-dll` **437/437** 继续通过。`mtndump` 构建通过；当时 `motionsim` 仍引用已删除的
  `MotionSnapshot::clipIndexByLabel/clipList/layerList/tagFrames`，该缺口已在下一阶段修复，
  不再是当前阻塞项。

- 2026-07-19 fresh decompile `parseFrame@0x6926B4`、
  `mergeFrameContent@0x692AB0`、`evaluateTimeline@0x699AE4`，并与同轮的
  `Player_ctor@0x6CED30`、`Player_dtor@0x6CFADC`、`Player_loadMotion@0x6B0F10`、
  `motionplayer_ncb_register@0x6D9B08` 交叉确认：Android live 链只从 raw frame dispatch
  写入既有 536B slot，再在两槽之间插值写回 node runtime；不存在文件级
  `PSBDictionary/shared_ptr/MotionSnapshot` owner。Web 据此新建 offline-only
  `OfflineMotionSnapshot.h`，把 snapshot 类型、loader 及 decoded 帧/曲线/资源 helper
  全部移出 `PlayerInternal.h`/`RuntimeSupport.h`/`Player.h`；`motionsim` 改为直接从离线
  decoded root 选择 `motion` dictionary，不再恢复已删除的 snapshot 字段。生产
  `PlayerCore.cpp` 最后一条未使用 `PSBValue.h` include 也已删除。Web Debug 完整链接、
  macOS Release `motionplayer-dll` **398/398**、`psbfile-dll` **437/437** 通过，
  `mtndump` 与 `motionsim` 均可构建；最终 `index.html` 依赖查询仍不含 offline/compat
  编译单元。该阶段仍待核的 live slot/container 镜像已在后续缩小为
  `FrameContentState/localState/accumulated` evaluator 中转，插件 target 边界也已在后续
  用 PRIVATE 源 + final-link force-load 闭合；当前 NO 结论不再由 snapshot 头文件泄漏、
  工具断编或插件对象串库支撑。

- 2026-07-19 fresh decompile `Player_particleEmitterPass@0x6BF0DC` 与
  `Player_onFindMotion_splitCharaMotionPath@0x697D34`，确认粒子发射器对 source list 的唯一
  owner 是 node+2200 raw Variant：每次 CopyRef 后读取 TJS `count`，以
  `int(random()*count)` 做 `PropGetByNum`，再拆路径并构造 child Player。本地此前虽已在
  `NodeTree.cpp` 保存 `particleMotionListVariant`，消费端却绕到
  `activeSlot().srcList`，并经 `FrameContentState`、`interpolatedCache` 复制三次。现已改为
  直接消费 raw dispatch，删除三层 `vector<string>` mirror；Mac 两套测试仍为
  **398/398**、**437/437**，Web Debug 完整 **43/43** 链接通过。

- 2026-07-19 fresh decompile `Player_resetFrameSlot@0x69260C`、
  `Player_mergeFrameContent@0x692AB0`、`Player_HM3_initValueFromNode@0x699510`、
  `Player_HM3_restoreValueToNode@0x6997F0`、`Motion_Player_findSource@0x6948E8`、
  `Player_initNodeTimeline@0x6B64AC`、`Player_updateLayers@0x6BB33C`、child pass
  `0x6BE0C0`、particle-emitter pass `0x6BEDD0` 与 render-item path `0x6C2334`，确认
  slot+28/+36 是 icon/src 的独立 `ttstr` owner，transform 求值阶段不会复制 source；
  HM3 value+44 只保留 src 生命周期，restore 不回写。Web 删除 `ClipSlot` 的两只
  `std::string` owner，child pass 恢复单段 `play(icon)` 与多段固定 `[1]/[2]` 边界，
  emitter dtgt 也改为 `ttstr`。Mac `motionplayer-dll` **398/398**、`psbfile-dll`
  **437/437**，`mtndump`/`motionsim` 构建及 Web Debug 完整链接通过；最终 Web 依赖图仍
  不含 offline/compat 编译单元。

- 2026-07-19 fresh decompile `Player_pushActionEvent@0x6B638C`、
  `Player_advanceRootAndNodes@0x6B6ADC`、`Player_rewindRootAndNodes@0x6B9A3C`、
  `Player_dispatchEvents@0x6C4490`，并与同轮 child `0x6BE0C0`、emitter `0x6BEDD0`
  交叉确认：事件 deque 的源码元素是 `int type` 加两只独立 `tTJSVariant`，layer action
  的 param1 为 void，node action 的 param1/param2 为 label/action String variant；
  slot+712 dtgt 则直接进入 Player+24 `map<ttstr,int>`。Web 删除 ClipSlot、
  FrameContentState 与 interpolatedCache 的 `action/motionDtgt` string mirror，事件参数恢复
  variant CopyRef，dtgt 查找不再 narrow/widen。Mac 两套测试 **398/398**、**437/437**，
  两个离线工具与 Web Debug 完整链接通过。

- 2026-07-19 fresh decompile `Player_applyBezierEasing@0x69A754`、
  control-point evaluator `sub_698454@0x698454`、position interpolation
  `sub_69A4D4@0x69A4D4`，并与 `Player_evaluateTimeline@0x699AE4` 交叉确认：Android
  每次求值都从活动 slot 的 `tTJSVariant` CopyRef 后即时读取 `x/y/t/s` 和嵌套
  `s[index].x/y/p`，不存在 `vector<double>` 解码缓存。Web 删除 live ClipSlot、
  `FrameContentState`、`interpolatedCache` 的 decoded 曲线字段及 merge-time decode，生产
  `interpolateSlots` 只接受 raw curve variant；离线工具所需 Bezier/spline 模型及算法集中到
  `OfflineMotionSnapshot.h`。生产目录排除离线头后的 `BezierCurve/ControlPointCurve` 搜索
  为零；Mac 两套测试 **398/398**、**437/437**，`mtndump`/`motionsim` 构建及 Web Debug
  最终链接均通过。

- 2026-07-19 fresh decompile `Player_initNodeFields@0x6B3C78`、
  `Player_buildNodeTree_recursive@0x6B4A6C`、`Player_buildNodeTree@0x6B51F0`、
  `Player_buildNodePathKey@0x6B5C1C`，并以 `Player_resetMotionState@0x6B2D3C`、
  `Player_pruneHM3ByNodeIdentity@0x6B826C`、`Player_pushActionEvent@0x6B638C` 交叉确认：
  node+0 是 PSB `label` 的直接 `ttstr` owner；Player+24 以 raw label 建
  `map<ttstr,int>`；HM3 的 key 则沿父链把 `L"/" + label` 前插成独立 path `ttstr`。
  Web 据此删除 `MotionNode::layerName` 的 `std::string` mirror、树构建和 HM3 的
  narrow/widen 回转；layer getter、action event、child/render consumers 均直接传递同一
  `ttstr`。Mac `motionplayer-dll` **398/398**、`psbfile-dll` **437/437**，
  `mtndump`/`motionsim` 构建及 Web Debug **43/43** 最终链接通过。

- 2026-07-19 fresh decompile `Player_evaluateTimeline@0x699AE4` 与
  `Player_buildRenderList@0x6C2334`，确认 evaluator 只把两槽的 transform、packed colors、
  opacity 和 type-specific 标量写入 node runtime；source 不参与插值，也没有节点级 source
  cache。render-list 在 `0x6C35D8` 对 `node + 320 + 536*activeSlot + 36` 的 `ttstr`
  直接 AddRef，释放 render item 旧 owner 后写入新 owner。Web 删除生产
  `FrameContentState`/`interpolatedCache` 的 `icon/src`、source fallback 和逐层复制；trace/log
  也直接读取活动 `ClipSlot::srcValue`。离线 decoded 模型把两只 `std::string` 下沉到
  `OfflineFrameContentState`。Mac 两套测试 **398/398**、**437/437**，离线工具构建和
  Web Debug **43/43** 最终链接通过。

- 2026-07-19 fresh decompile `Player_HM3_initValueFromNode@0x699510` 与
  geometry pass `0x6BC4F0`，确认 HM3 的 slot 字段直接来自 active ClipSlot，颜色和 transform
  来自 evaluator 写入的 node runtime，type-4 数据来自 `node+2224..2288`；geometry 的
  origin 则是 node persistent origin 加 active-slot ox/oy。Web 据此整体删除节点级
  `interpolatedCache`，HM3 改读 `activeSlot/colorBytes/accumulated/particleInterp`，几何改读
  active slot。生产搜索 `interpolatedCache|InterpolatedCache` 为零；Mac 两套测试
  **398/398**、**437/437**，`mtndump`/`motionsim` 构建及 Web Debug **43/43** 最终链接通过。
  该阶段尚存的 `FrameContentState` 栈中转已在后续直接写回阶段删除。

- 2026-07-19 fresh decompile `Player_evaluateTimeline@0x699AE4`、
  `Player_updateLayers@0x6BB33C`、`Player_resetMotionState@0x6B2B7C`、
  `Player_resetFrameSlot@0x69260C`、`Player_mergeFrameContent@0x692AB0` 与
  `Player_initNodeFields@0x6B3C78`，并用 `0x699C94..0x699CCC` 原始 ARM64 指令确认 `ti`
  为 `ti * uint(elapsed/ti)`。Web 删除生产 `FrameContentState`、`localState`、
  `hasTimelineEvalRatio` 与 parameter override 镜像；evaluator 改为 bool 返回并直接写 node
  runtime，恢复无 clamp ratio、`1e-7/DBL_EPSILON` 双阈值、整数 opacity 默认/解析/插值，
  node+8 也只在 `parameterize` 为 Integer 时持有参数项。离线工具保留独立
  `OfflineFrameContentState`，不进入生产数据流。Mac 四目标、Web Debug 最终链接通过；
  `motionplayer-dll` **398/398**、`psbfile-dll` **437/437**。该时点仍 open 的 mesh
  crossfade 与 nodeType 5/10 类型专用输出已由下一阶段闭合。

- 2026-07-19 fresh decompile `Player_evaluateTimeline@0x699AE4`、
  `sub_69AC4C@0x69AC4C`、`Player_mergeFrameContent@0x692AB0`、
  `Player_processCameraNode@0x6BDA28`、`Player_evaluateAnchorNodes_type10@0x6C0528`、
  `MotionNode_initFields@0x6F19B4` 与 `Player_ctor@0x6CED30`。Web 现恢复 mesh crossfade
  的 raw float ratio、可选 slot curve、`unmatched point list`、clear/保留容量、精确扩容与
  双空不写 node 分支；type-5 `camera.fov` 与 type-10 `feedback.timespan` 按 copy/linear
  crossfade 写入未初始化 node 输出通道，camera phase 在 stereovision 分支再写 Player
  `+1104`，其构造默认恢复为 `0.2`。旧 `anchorDamping` 命名已证伪并同步纠正 memory。
  Mac 四目标、Web Debug 最终链接通过；`motionplayer-dll` **398/398**、`psbfile-dll`
  **437/437**。行为链已闭合；随后一轮已继续恢复 mesh 的 8B point 元素拓扑。

- 2026-07-19 fresh decompile `Player_mergeFrameContent@0x692AB0`、
  `Player_evaluateTimeline@0x699AE4`、`sub_69AC4C@0x69AC4C`、
  `Player_HM3_initValueFromNode@0x699510`、restore `0x6997F0`、精确 vector copy
  `0x6996E8`、Bezier evaluator `0x69B1E8`、geometry `0x6BC4F0`、child deform
  `0x69AE74`、render-item builder `0x6C2334`、TJS point serializer `0x6C715C`、
  command-list `0x6D3A4C`、GLL queue/render `0x6DE738/0x6DD56C`、render emit
  `0x6C4E28`、renderToCanvas `0x6C7440`、SLA `0x6C9CA8` 与 translate
  `0x6D5264`。`0x6996E8` 以 `>>3` 计数并逐个复制 8B 元素，`0x69B1E8`
  要求 128B 即 16 点；几何、child、render-item 与 TJS serializer 均以 8B stride
  读取同一 `{float x,float y}`。Web 生产链现统一使用 `MeshPoint`，解析时每对实数
  push 一个点，插值/几何/渲染均按点处理，只在 TJS Array、Layer mesh 与 Emscripten
  SIMD 边界展开。Mac 四目标与 Web Debug 最终链接通过；`motionplayer-dll`
  **398/398**、`psbfile-dll` **437/437**。该元素拓扑差异已闭合；该时点仍缺 MDF、
  media/TJS 与 packed-table 损坏输入覆盖，其中 media/TJS 已由下一阶段闭合。

- 2026-07-19 fresh decompile media singleton/register `0x59849C`、析构/ref/name
  `0x5997F0..0x5998A8`、exists/open/list/local-name `0x5998C4..0x599DD8`、container
  cache `0x599E04`、resource `0x59A0B4`、factory `0x59A330` 与 resolve `0x59A4B0`。
  本地字段顺序、逆序析构、process-lifetime singleton、首段缓存、contains→strict 导航、
  array/dictionary listing 与 miss 边界均同形。新增测试使用既有 `ezsave.pimg`，实际走
  `AllRegist → LoadModule("PSBFile.dll") → new PSBFile() → psb://`，覆盖 typed class、
  native adaptor、真实 resource 内存流、32 项 array listing、miss/异常、缓存复用和
  local-name 清空；`psbfile-dll` 现为 **484/484 assertions（6 cases）**。
  同轮 fresh decompile MDF/storage owner 链 `0x598538/0x598708/0x598960/0x598AAC`
  与 `0x598B3C`，确认 zlib 失败保留原 buffer、成功替换、无效 storage leak、filter 后 refresh
  及 owner 替换顺序均已静态复原。当时按扩展名搜索误判为无现成 MDF fixture；下一阶段
  已按魔数扫描纠正该结论。

- 2026-07-19 按文件头而非扩展名扫描本机 `reference/` 与 `tests/` 工作树，发现 142 个
  `mdf\0` 包装资产（均藏在 `.ks.scn` 路径）；批量 zlib 校验结果为 142/142 解压成功、
  142/142 声明尺寸相等、142/142 内层为 `PSB\0`，没有天然失败样本。随后用
  `git ls-tree HEAD` 复核发现这些 DRACU 资产只存在于 `reference` 子模块的本地
  staged/untracked 状态，子模块 HEAD 和主仓库已跟踪测试物料中的 MDF 数量都是 0；因此
  删除了依赖固定本地路径、不可在干净 checkout 复现的单元测试，`psbfile-dll` 保持
  **484/484 assertions（6 cases）**，并纠正“仓库已有 MDF fixture”的错误记录。

- 2026-07-19 启动本机 `oracle-arm64-31` AVD，部署仓库 harness 后，既有 Android
  `geometry_hit_test` oracle 10/10 通过，证明真实 `libkrkr2.so` 调用链可用。新增
  `run_psbfile_load_adb.py --input <existing-mdf>`，由操作者提供现有 MDF，不把资产或损坏
  fixture 写入仓库。以本机最小 12,729 字节 MDF 直调 `PSBFile.load(octet)@0x598268`：
  Android 返回 true，生成 refcount=1、inline header 的 0x68 owner，raw buffer 为
  163,214 字节 `PSB\0`，再调 `PSBRawOwner::Refresh(strict)@0x598960` 返回 true。
  Frida 实测序列为 `0x598268 enter → 0x598708 enter/true → 0x598268 true →
  0x598960 enter/true`。同一 runner 又覆盖 25,160→48,011 字节和最大
  300,816→5,806,932 字节样本；为容纳后者，修复了 oracle `_rpc_write` 超过 harness
  128 KiB line buffer 时的截断，改为 60 KiB 分块。三只样本的 owner size、magic、refcount、
  inline header 和 strict refresh 全部一致。这提供了 MDF 成功链的 Android runtime
  oracle。runner 的 `--storage` 又把同一文件临时推至 `/data/local/tmp`，直调
  `PSBFile.loadStorage@0x598538`；Frida 得到 `0x598538 → 0x598708(true) →
  0x598538(true)`，随后显式 `0x598960(strict=true)` 亦返回 true，故 Android 的 octet 与
  storage 两条 MDF 成功入口均已有运行时证据。已提交的 64,585 字节 `ezsave.pimg` 也在
  同一 runner 的 raw-PSB octet/storage 模式全部通过，因此干净 checkout 至少有一条完全
  可复现的 Android PSB owner/call-chain oracle。

- 2026-07-19 fresh decompile `EmotePlayer_setEmotePSBDecryptSeed_callback@0x685D30`
  与 filter call operator `0x6863CC`，确认闭包只持有 32-bit seed，call operator 从
  `owner.header.encryptData` 到 `chunkOffsets` 原地执行四状态 xorshift，每个 word 按低字节
  起依次 XOR。oracle 的 READ/WRITE 均补齐 64/60 KiB 分块后，用已提交的 5,366,313 字节
  加密 motion PSB 和 seed `742877301` 直调 Android：encrypt 区间精确为 203,302 字节，
  Android 输出与独立 host xorshift 逐字节相等，`0x598960(strict=true)` 返回 true，root
  tag 为 `0x21`；Frida 序列包含 `0x598268 → 0x598708 → 0x6863CC → 0x598960`。这闭合了
  合法 seed-filter 成功路径的 Android runtime 对照。zlib 失败、filter 后 offset 校验失败
  与损坏 packed table 仍无天然输入，按规则未人为构造 fixture。

- 2026-07-19 对 `reference` 子模块 HEAD 的 8 个已提交 XP3 全量解包到临时目录，共扫描
  4,407 个内部文件：发现 12 个 `PSB\0`、0 个 `mdf\0`。因此“已提交归档内可能藏有 MDF”
  已被独立排除；现有 MDF runtime 证据依赖操作者本地资产，raw PSB/filter 证据则使用主仓库
  已提交的 `ezsave.pimg` 与加密 motion PSB，可在干净 checkout 重现。

- 2026-07-19 最终回归：Android raw PSB 的 octet/storage、真实 MDF 的 octet/storage、
  203,302 字节 seed-filter 全字节对照及 `geometry_hit_test` 10/10 均通过；Mac
  `motionplayer-dll` 为 398/398 assertions（14 cases），`psbfile-dll` 为 484/484 assertions
  （6 cases），Web Debug 完整链接成功。上述结果证明本轮 oracle/协议分块和证据修正未引入
  已知回归，但不替代下述损坏输入边界缺口。

- 2026-07-19 再次 fresh decompile `PSBFile::LoadStorage@0x598538` 发现第二次
  `stream->GetSize()` 明确写入 32-bit `w22/v6`，随后该截断值贯穿 allocation、ReadBuffer、
  MDF 输入长度和 `PSBFile::Adopt@0x598708`；第一次 `<9` 检查仍直接使用 64-bit 返回值。
  本地此前把第二次结果保存为 `size_t`，会让超过 4 GiB 的 storage 边界偏离 Android。
  现已恢复为 `std::uint32_t` 局部量，同时保留先做 64-bit 最小尺寸检查的调用顺序。

- 2026-07-19 fresh decompile 并核对 ARM64 指令
  `PSBValueDispatch` 惰性转换 `0x59673C`、node `GetDouble@0x5992E8` 与
  `GetInt@0x599438`：整数 tag `0x05..0x0A` 分别执行 signed 8/16/24/32/40/48-bit
  扩展，tag `0x0C` 原样读取 64-bit；但 tag `0x0B` 的 7 字节路径只拼接低 56 位，
  **不**把 bit55 扩展到最高字节。`GetInt` 最终只消费低 32 位，因此同一 quirk 对其
  结果无影响；惰性 TJS Integer 与 double 转换则会观察到差异。本地通用 reader 此前把
  7 字节也按有符号数扩展，现已恢复 Android 的 7-byte zero-extension 边界。仓库没有
  天然 tag `0x0B` 物料，按规则未构造 fixture。

- 2026-07-19 fresh decompile `resource` helpers `0x596C70/0x5996E4`、惰性 octet
  转换 `0x59673C`、media resource `0x59A0B4`，并复核 motion callers
  `0x694E10/0x696CC8`。二进制接口返回 borrowed chunk pointer，只有 length 是 out 参数；
  `chunkData == nullptr` 时立即返回 null 且不写 length。本地此前把 pointer 也改成引用输出，
  并无条件做 pointer arithmetic。现已恢复 `const uint8_t *GetResource(uint32_t &size)`
  接口、null early-return 与所有生产 caller 的直接返回值数据流。

- 上述三项修正完成后，Mac Release 的 `psbfile-dll` **484/484**（6 cases）、
  `motionplayer-dll` **398/398**（14 cases）全部通过；`motionplayer-dll`、`psbfile-dll`、
  `mtndump`、`motionsim` 均构建成功，Web Debug 也完成最终 `index.html` 链接。现有验证
  未发现回归；tag `0x0B` 和 >4 GiB storage 仍属于无天然物料的边界证据缺口。

- 2026-07-19 继续 fresh decompile `sub_597B1C@0x597B1C` 与
  `sub_59659C@0x59659C`，纠正两处内部结构：名称 trie 反向解码的临时容器是
  `std::vector<char>`，完成后 reverse 并构造 `std::string`；字典索引搜索在当前
  midpoint 等于目标时立即停止，不是继续收缩到第一个相等项的 `lower_bound`。本地此前
  分别用 `std::string` 直接 push 和 lower-bound 循环简化；现已恢复临时容器的分配/
  扩容/析构生命周期及重复损坏键时的 midpoint 命中边界。

- `sub_59641C@0x59641C`、`sub_59659C@0x59659C` 的 ARM64 `MADD W`/`MUL W`
  证明 packed value stride 是 `int`，与 `uint32_t count/index` 运算后在 32-bit 中间值
  上 wrap，再用于指针偏移。本地此前把 stride 提升为 `ptrdiff_t`，损坏 tag 或乘积溢出
  时会走指针宽度有符号算术；现已恢复原始通常算术转换。正常 `0x0D..0x10` table 不变。

- `PropGetByNum@0x5976C4`（指令 `0x59777C..0x59778C`）、
  `PSBValueDispatch_EnumMembers_guess@0x596F50` 与 media `GetListAt@0x5999F4`
  交叉确认：数组 count 在 TJS numeric 访问和枚举调用面均折叠成 signed 32-bit，负索引
  加 count 也执行 32-bit ADD；dictionary count 的循环仍保持 unsigned。本地此前统一使用
  `uint32_t`/`int64_t`，规避了原版高位 count 和负索引溢出边界；现已按三个调用面分别
  恢复 signed/unsigned 数据流。

- media 链 fresh decompile `GetResourceData@0x59A0B4`、
  `CheckExistentStorage@0x5998C4`、`Open@0x59993C`：中间 helper 直接返回 borrowed
  chunk pointer，只有 size 是未预初始化的 out 参数；不是 `bool` 加 pointer/size 双 out。
  本地接口和两个 caller 已恢复直接返回值数据流。owner 仍由 `_file` adaptor 缓存持有，
  `tTVPMemoryStream` 继续借用 chunk 地址，因此跨 container replacement 的原始悬挂边界未被
  shared ownership 隐藏。

- `Load(octet)@0x598268` 与 `LoadStorage@0x598538` 再次核对显示：输入长度、MDF
  expected length 以及成功后从 `uLongf actual` 写回的长度都经过 `uint32_t`，只在传入
  owner 的 64-bit size 字段时零扩展。本地已把 octet helper、pair 返回长度及 storage
  `dataSize` 从 `size_t` 收束为同一 32-bit 链；zlib 失败回退、storage Adopt 失败泄漏和
  octet Adopt 失败释放顺序保持不变。

- 本轮修正后重新构建 Mac Release 的 `motionplayer-dll`、`psbfile-dll`、`mtndump`、
  `motionsim`，`psbfile-dll` **484/484**、`motionplayer-dll` **398/398** 全绿；Web
  Debug 完成最终 `index.html` 链接。上述损坏 table/high-bit count 边界没有天然物料，
  因此这些结果是正常资产非回归守护，不替代反编译边界证据，也未新增人工 fixture。

- 2026-07-19 修正静态插件 target 边界：将根插件、motionplayer、psdfile、layerExDraw、
  fstat 的 `PUBLIC target_sources` 收束为 `PRIVATE`。第一次只改可见性后，
  `motionplayer-dll` 的三个 `LoadModule("motionplayer.dll")` 检查失败，证明 registrar-only
  `motionplayer/main.cpp` 会被普通静态归档抽取规则丢弃；最终在 `krkr2plugin` 的链接接口对
  六只已分离插件归档使用 macOS `-force_load` / Web `--whole-archive`。Mac/Web Ninja
  归档依赖图确认没有对象串库，最终链接命令确认六只归档均被强制加载；Mac
  `motionplayer-dll` **398/398**、`psbfile-dll` **437/437**，`mtndump`/`motionsim`
  与 Web Debug 最终链接全部通过。该项不再是 100% 结论的阻塞项。

- 2026-07-19 fresh decompile typed NCB 链 `0x597E98..0x5980F4`、参数构造
  `0x59B570/0x59B708` 与 module/media callback `0x42CF28/0x59849C`：`PSBFile.load`
  的 Variant 参数是按值复制并经历 AddRef/Release，不是 borrow-only `const&`；pre-register
  descriptor 也直接指向包含 function-local media singleton 的 callback，中间不存在额外
  `initPSBMedia` 转发层。本地已删除错误 converter/转发函数并恢复直接调用形状。

- 同轮 fresh decompile/disasm `ResourceManager_loadResource@0x6A8D8C` 进一步确认 holder
  生命周期：cache hit 在 `0x6A8E94..0x6A8EB8` copy/AddRef 临时 holder；miss 在
  `0x6A926C..0x6A92A8` 对 map record 执行 Release-old/copy/AddRef；公共返回块从 local
  holder 创建 dispatch 后再 Release 临时引用。本地原先的 deleted-copy、move-insert 和
  record 直借用均已纠正。Mac 四目标构建、`psbfile-dll` **484/484**、
  `motionplayer-dll` **398/398**、Web Debug 最终链接全部通过；在线 Android AVD 的既有
  `ezsave.pimg` octet/storage oracle 及 5,366,313 字节 motion PSB octet/seed-filter oracle
  也全部 `status=ok`，filter 的 203,302 字节逐字节一致。

- 2026-07-19 对 PSB 主区 90 个函数重新枚举后，fresh decompile
  `0x59641C/0x59659C/0x597B1C/0x597AD4/0x5981F8/0x598A3C/0x598D58/
  0x598E44/0x598E64/0x599554/0x5995D8`，闭合 packed-name/dictionary 的函数边界、
  输出参数、复用 string/vector copy 生命周期，恢复 try-get 的 Release-old→copy→AddRef
  顺序与 contains 临时 node 的引用计数 no-op，并纠正 typed/raw root guard、dispatch ctor
  自持有 AddRef 及空 raw node 直接解引用边界。Mac 四目标构建成功，`psbfile-dll`
  **484/484**、`motionplayer-dll` **398/398**，Web Debug 最终链接通过；在线 Android AVD
  的既有 PIMG octet/storage 与 motion octet/seed-filter oracle 全部 `status=ok`。

- 2026-07-19 继续逐函数覆盖审计，fresh decompile/disasm
  `sub_598AAC@0x598AAC`、`sub_598960@0x598960`、`sub_598B58@0x598B58`、
  `sub_599554@0x599554`，纠正两处仍属“行为近似但源码调用链不一致”的实现：raw owner
  ctor 现在自行按原顺序建立 inline header view，不再伪调用后续 refresh helper；当输入
  指针为空时，header view 与二进制一样保持未初始化。raw string getter 也恢复自己的完整
  tag switch，不再绕经独立的 type-category helper；五个 string tag 返回 owner 内借用
  指针、全部已知非 string tag 返回空、未知 tag 抛精确异常。Mac 四目标构建成功，
  `psbfile-dll` **484/484**、`motionplayer-dll` **398/398**，Web Debug 最终
  `index.html` 链接通过；在线 Android AVD 的 `ezsave.pimg` octet/storage 与 motion
  octet/seed-filter oracle 全部 `status=ok`，filter 仍为 203,302 字节逐字节一致。上述
  Android oracle 证明正常资产的原版构造边界未变，但不替代无天然资产的损坏输入验证。

- 2026-07-19 fresh decompile `sub_598D58@0x598D58`、
  `PSBMedia::Resolve@0x59A4B0`，并以 `sub_598A64@0x598A64` 的独立 move ctor
  交叉区分两类生命周期：try-get 命中及 Resolve 逐段替换实际走
  Release-old→copy owner→AddRef→write node；strict getter 的返回临时量随后在末段状态
  检查前析构，形成原版 copy assignment AddRef + temporary destructor Release 的 no-op，
  不是 raw-node move 的清零分支。审计中最初的 move 假设已在修改前按指令流纠正；本地
  copy assignment 与 Resolve 临时量作用域现已恢复上述顺序和 zero-ref 删除边界。Mac
  四目标构建成功，`psbfile-dll` **484/484**、`motionplayer-dll` **398/398**，Web Debug
  最终链接通过。本轮 Android oracle 未复跑：`.claude.local.md` 指定的
  `emulator-5554` 已从 ADB 列表消失，runner 停在 `wait-for-device`，未产生可归因于实现
  的失败结果；此前同资产的在线 AVD oracle 结果仍为 `status=ok`。

## 后续闭合条件

要对“当前 Web 项目”给出 100% 结论，至少还需要：

1. 使用现有天然损坏资产覆盖 MDF zlib 失败及 filter 后 offset 校验失败；MDF 成功路径已
   用本机现有 `.ks.scn` 在 Android oracle 验证，但仓库没有已提交 MDF fixture；media/TJS
   注册路径已由现有 PIMG 闭合。没有现成物料时记录验证缺口，不从零伪造 fixture。
2. packed-table 的 tag/width/stride 分支已逐项反编译；剩余工作是用现有损坏资产或
   Android oracle 核对真实越界/崩溃表现，不能为了测试主动构造新 fixture。
