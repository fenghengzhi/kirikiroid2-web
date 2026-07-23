# Android `PSBFile.dll` 复原审计（2026-07-18）

## 2026-07-23：raw-node alias、数值 decoder 与 media 可达性复核

- `sub_598D58@0x598D58` 的真实 caller `sub_695DE8@0x696A84..0x696A90`
  把同一 `&v278` 同时放入 X0（source raw node）和 X2（out raw node），即
  `sub_598D58(&v278, "clip", &v278)`。这证伪了本文旧有的“没有 alias caller 证据”。
  callee 不做 self guard，命中后仍按 Release-old→从同一 source 槽重读 owner→AddRef→
  写 child node 执行；本地 `GetDictionaryValue` 已是同一危险顺序。新增 `ezsave.pimg`
  回归用例先以 trie 中存在、但 root dictionary 中不存在的 `"name"` 守护第二段查找的
  alias miss 完全不改 owner/node，再以
  `node.GetDictionaryValue("layers", node)` 守护 alias hit 的原位下降。该证据只属于
  try-get 函数本体，仍不能证明通用 `PSBRawNode::operator=` 是否存在或是否带 self guard。
- caller 本身此前尚未复刻这条 alias 数据流。`sub_695DE8` 在 `0x6960D4` 唯一初始化
  raw-node scratch，请求探测、icon 枚举与 packed-record loop 持续复用；
  `0x696914..0x696960` 每轮 Release-old→copy owner/node→AddRef，随后
  `0x696A90` 以同一 scratch 原位查 `clip`，inner/outer backedge 均不析构。本地
  `PlayerResource.cpp` 现也用一只持久 `iconNode` 贯穿这几段，不再直接从 record 读 origin
  后另建空 `clipNode`；scratch 声明在 `sourceRoot` 之前，对齐 `0x6960D4→0x6960E8`
  的构造顺序，以及正常路径 `sourceRoot@0x697358→scratch@0x697380` 的反向析构顺序。
  请求 icon 的第二次 lookup 还在 `0x69612C..0x696154` 先释放
  temporary icon-root owner、后判断 lookup 结果；本地现以显式 scope + bool 恢复相同顺序，
  不再把该临时 owner 保活到 helper 返回。Android scratch 还跨越该本地 helper 之外的后续阶段并到
  `0x697380` 才统一 Release；本地仍拆分 `sub_695DE8`，这是尚未闭合的源码边界。
- `GetInt@0x599438` 与 `GetDouble@0x5992E8` 都呈 outer tag switch 加
  32-bit integer / 64-bit integer / float / double 四组 nested decoder 的形状。本地此前
  各写一只 monolithic switch，现抽出四个共享 `_guess` decoder，再由两只 outer wrapper
  分派。精确 helper 名、member/free 身份以及“inline helper”还是源码显式 nested switch
  仍不能仅由优化后二进制唯一判定；`_guess` 保留该不确定性。tag `0x0B` 在 64-bit decoder
  中读取完整 56 位且不扩展 bit55；`GetInt` 机器码只读低 32 位是 wrapper 截断后的优化结果，
  不再被误记成 decoder 源码只读四字节。
- fresh decompile `EnsureContainer@0x599E04`、`Resolve@0x59A4B0`、
  `GetListAt@0x5999F4` 与 `ContainsDictionaryKey@0x5995D8` 未发现新的本地差异。
  对现有资产的 packed table 做独立只读解析：`ezsave.pimg` root 是 11-key Dictionary，
  直属八个 `.tlg` 为 Resource、`height/width` 为 Integer、`layers` 为 Array，没有直属
  Dictionary；未过滤 motion root 是 Resource `0x1A`。Resolve 不暴露 root、每段只允许
  Dictionary key，且不能以数字段穿过 Array。因此当前两只天然资产均无法通过 PSBMedia
  public path 到达 dictionary listing 分支；这是一条由函数链和资产结构共同证明的 negative，
  不是一次空 grep。

## 2026-07-23：六维逐函数复审纠正

- `PSBValueDispatch::PropGet@0x597854` 的 array `count` 分支在
  `0x5979F8` 调用 `tTJSVariant::operator=(tjs_int32)@0xA0FF28`；该 helper 在
  `0xA0FF44` 以 `SXTW` 写入 Integer。此前本地把 `uint32_t count` 直接提升为
  `tjs_int64`，高位 count 会零扩展；现已恢复 signed 32-bit 赋值边界。
- `assign@0x59673C` 的 String 分支与 `EnumMembers@0x596F50` 的 dictionary name
  都直接调用 narrow `tTJSVariant::operator=(const char *)@0xA0FEB4`。此前本地先构造
  `ttstr`，多出宽字符串 owner、AddRef/Release 和“先分配临时量、后释放旧 result”的
  异常生命周期；现已恢复直接 narrow 赋值。
- 同一 `assign@0x59673C` 的入口 `0x596764` 把 destination 保存到 `X19`；各 tag
  正常/throw-helper-return 分支最终汇入 `0x596B88 MOV X0,X19`，返回同一 destination
  地址。四个直接 caller `0x5971B4/0x5973A4/0x597848/0x5979B4` 都忽略该返回值。
  本地此前误写为 `void`，现恢复返回现有 destination pointer；pointer/reference 的原始
  源码拼写在 ARM64 ABI 上不可区分，未强行宣称其中一种为唯一事实。
- `EnumMembers@0x596F50` 按顺序 default-construct name、flags、value，随后执行
  `flags = tjs_int32(0)`，再 default-construct callback result。此前 flags 直接由零构造，
  少了一次与 Android 相同的赋值调用和旧内容释放边界；现已恢复四只 Variant 的精确时序。
- `assign@0x59673C`、`getResource_guess@0x596C70`、
  `PSBRawNode::GetResource@0x5996E4` 与 `PSBMedia::GetResourceData@0x59A0B4`
  四条 resource 路径都先取得 chunk-offset view、再取得 chunk-length view，索引 entry 时
  则先读 `lengths[index]`、后读 `offsets[index]`。三个 helper 原本已符合两层顺序；只有
  `assign` 的 entry 访问反了，现已纠正，保留损坏表输入下的首个越界访问顺序。
- `PSBFile::Load@0x598268` 对既非 String 也非 Octet 的参数调用异常 helper 后，
  `0x5983B0` 仍显式返回 true；此前本地在 helper 意外返回时继续执行 `AsOctet()`，现已恢复
  throw-helper continuation 的精确返回值。
- `GetDictionaryKeys@0x598E64` 的 full-vector 分支在 `0x598FFC` 经 PLT
  `0x423250/0x42325C` 调到
  `std::vector<std::string>::_M_emplace_back_aux<std::string &>@0x59B7E8`；此前仅看
  真实函数的 direct xref 而把它排除在 PSBFile coverage 外，是错误的 negative 结论。
  manifest 已从 111 个业务/NCB入口纠正为 **112 个相关函数**（额外 1 个本源码触发的
  vector 扩容慢路径），最后一只函数的 exclusive end 为 `0x59B9C8`。该地址的独立
  `SUB SP,#0x30` 序言及 `PackinOne.dll` 注册 callback xref 又证明，后续代码不属于
  vector helper；IDB 已同步拆分并保存。
- 既有加密 motion PSB 可作为第二只有效 raw container。新增测试按
  `ezsave.pimg → encrypted motion PSB → ezsave.pimg` 驱动 `EnsureContainer@0x599E04`，
  验证跨-container 替换确实发生、旧 `tTVPMemoryStream` 的自身 metadata/析构在替换后仍可用，
  以及切回首容器。stream 不保活 owner、block 仅为 borrowed 的事实来自
  `tTVPMemoryStream` ctor/dtor 反编译证据；测试不读取悬挂 block，也不冒领这项证明。
  因此本文此前“只有一只可加载 container、不能覆盖 replacement/borrowed stream”的断言
  已被证伪并就地纠正；该测试是本地生命周期复刻验证，不冒充 Android runtime oracle。
- 2026-07-23 新增 `run_psbfile_load_adb.py --media-lifecycle`，把同两只 tracked 资产
  以 ASCII alias 推入 Android，并直接驱动 process-lifetime PSBMedia singleton：
  `Open(ezsave/2036.tlg) → CheckExistentStorage(raw-motion/2036.tlg) →
  delete old stream → Open(ezsave/2036.tlg)`。它同时核对 `_file/_container` 确实替换、
  adaptor 地址变化、旧 stream 的 Block 指针值/Reference/Size/AllocSize/CurrentPos 不变，
  且绝不读取 replacement 后的悬挂 Block。离线 fake-engine/RPC 协议测试已通过；它没有
  伪造 `adb` 可执行文件、没有启动 Android，也没有执行 `libkrkr2.so`。本轮
  `adb devices -l` 无连接设备，因此尚不把真实 Android 执行记为通过。
- fresh IDA 曾把 `0x8F7D04..0x8F7DC0` 合成一只函数；`0x8F7D68` 有独立 ARM64 序言，
  并在同一 `Block && !Reference` 清理后 tail-call `operator delete(self)`。IDB 已拆为
  complete destructor `0x8F7D04..0x8F7D68` 与 deleting destructor
  `0x8F7D68..0x8F7DC0`、补 `_guess` 名称/类型/注释并保存。Android oracle 调用
  deleting entry `0x8F7D68`，与源码 `delete stream` 的完整对象生命周期一致。
- 当前验证：macOS Release 五个相关目标构建成功，`psbfile-dll` **575/575**（10 cases）、
  `motionplayer-dll` **1212/1212**（16 cases）、`motionplayer-ttstr-hash-test`
  **100/100**（22 cases）；Web Debug 最终链接与显式 Wasmtime
  `krkr2_wasmtime_guest` 目标均通过。现成 motion playback runner 在执行 guest 前因
  当前 checkout 缺少 `reference/xp3/logo_test_oracle.xp3` 退出；不制造 fixture，记录为
  运行时差分验证缺口，不冒充 guest 已执行。

## 2026-07-23：local→Android 调用边界复核

- 已消除三类 macOS Release 本地产物中的额外调用边界：
  1. `GetCount@0x5975E0`、`PropGetByNum@0x5976C4` 的两个站点及
     `PropGet@0x597854` 共四个 count 解码站点，均在调用者函数体内展开 tag switch，
     不再直接调用 `ReadPackedCount_guess`。
  2. `PSBMedia::Resolve@0x59A4B0` 直接从 native `PSBFile` 的 owner/header 建立 root
     raw node，不再绕经 `PSBFile::GetRoot@0x598A3C`。
  3. `PSBMedia::GetResourceData@0x59A0B4` 直接展开 resource-index 与 chunk-table
     解码，不再绕经 `PSBRawNode::GetResource@0x5996E4`。
- macOS Release 的 `main.cpp.o` / `PSBMedia.cpp.o` 符号和 relocation 扫描已确认不再
  引用上述三个本地边界。该证据只说明本地产物不再多出 Android 二进制中未观察到的
  调用；优化后的 Android 二进制仍不能唯一排除原始源码曾使用等价 inline helper，
  因而不得把当前展开写法声明为唯一源码拼写。
- 修改后 `psbfile-dll` 为 **554/554**（8 cases），`motionplayer-dll` 为
  **1197/1197**（15 cases），Web Debug 最终链接通过，`git diff --check` 通过。

## 2026-07-22：eager compatibility 子系统已删除

此前隔离的 `DecodedPSBFile` / `PSBValue` / type handlers / image metadata、
`OfflineMotionSnapshot`、`PlayerFrameStep`、`PlayerFrameStepping` 以及
`psbfile_decoded_compat` / `motionplayer_offline` targets 已全部删除。
`mtndump` 现直接读取 `PSBRawNode`；`motionsim` 的字典、数组、枚举及帧内容读取全部通过
生产 `PSBValueDispatch`。wasmtime 与 native differential 的显式 source list 也已改用
`psbfile/main.cpp + PSBRawFile.cpp`。下文早期章节对这些 compatibility 文件的描述只记录
历史审计过程，不再代表当前工作树状态。

## 2026-07-22：当前复核增量

- 对 `0x59641C..0x59B708` 的 111 个 PSBFile.dll 业务/NCB入口重新枚举异常 helper
  continuation，补齐 `assign@0x59673C`、`IsInstanceOf@0x596E24` 与
  `GetDictionaryKeys@0x598E64` 的精确默认值。该轮曾误记 `PSBFile::Load@0x598268`
  已闭合；后续逐指令复核 `0x5983AC..0x5983B0` 发现 throw helper 返回后必须显式 true，
  本轮已纠正代码和本文结论。
- ABI 与 AArch64 最小样本证明 `0x598A64` 是带隐藏返回槽、消费 source/this 的按值
  transfer helper，而不是 move constructor 函数本体；但不能证明 helper 的源名字、
  member/free 身份，也不能证明内部复制来自某个用户声明的 move constructor。
  `PSBFile::Transfer_guess` 保留可证的 incoming zero-ref 删除与清 source 净行为，
  `ResourceManager_loadResource@0x6A9204..0x6A92F8` 的 selected/loaded/root 生命周期按
  callsite 证据复原，不把本地 special-member 写法升格为二进制事实。
- 删除无 Android 独立入口的 `CreatePSBValueDispatch/CreatePSBValueVariant`；
  `PSBValueDispatch` 的完整类声明位于 `PSBDispatch.h`，35 个方法仍在 `main.cpp`
  out-of-line 定义。`load@0x6A92FC..0x6A9358` 直接从 selected owner/root 构造 dispatch，
  不再生成 retained `PSBRawNode` 临时量。
- fresh decompile `isExistMotion@0x6A96F8` 与 `findMotion@0x6A9ED4` 纠正了旧的 TJS root
  导航：前者全程 raw-node；后者只在命中最终 motion node 后构造 dispatch。direct/fallback
  两段均在函数体内展开；macOS Release 对象恰有 3 个 constructor relocation，对应 Android
  的 `load@0x6A931C`、`findMotion@0x6AA124/0x6AA424` 三处 xref。
- 删除 Android 12-member registrar 和函数区均不存在的本地 `ResourceManager::findLoaded`，
  以及六个无构建/源码消费者的旧 eager compatibility 头文件：`BitConverter.h`、
  `Consts.h`、`EMoteCTX.h`、`PSBEnums.h`、`PSBExtension.h`、`PSBHeader.h`。
- raw node 已删除无 Android 独立/内联调用证据的 array/dictionary convenience API；
  `PSBMedia::GetListAt@0x5999F4`、`GetString@0x598B58`、`GetInt@0x599438`、
  `GetResource@0x5996E4` 直接展开自身 packed 分支；`GetInt@0x599438` 与
  `GetDouble@0x5992E8` 则恢复为 outer dispatcher 加四类共享 `_guess` decoder 的
  高置信源码形状，Release 构建仍由优化器内联这些边界。
  `PSBFile::Load@0x598268` 也已合并 octet/MDF 裸指针生命周期，不再经过本地 `LoadOctet`。
- `ResourceManager_loadResource@0x6A8D8C` 已恢复完整异常字符串拼接、
  `unordered_map::operator[]@0x6EB9E4` 默认构造 mapped record，以及 selected/loaded
  common tail；`findSource@0x6AAB3C` 已恢复 unchecked split 边界、String 型 blank
  dimensions、`ncbDictionaryAccessor::SetValue`、五个 hint 槽及 raw group 临时生命周期。
- 2026-07-22 再次 fresh decompile `Player_getVariableKeys@0x6D139C` 与
  `sub_704CB8@0x704CB8`，并交叉核对当前源码：`variableKeys`、`transformOrder` 与
  `defaultTransformOrder` 已通过同一 `createTJSArrayWithItems_guess` 返回 Array Variant
  及其 `tTJSArrayNI::Items` deque，随后直接 `emplace_back`，不存在中间 vector 或
  `FuncCall("add")`。`0x1AB820C..0x1AB821C` 等 member-hint 也已提升为
  `MotionDispatch.h/RuntimeSupport.cpp` 中的进程级共享槽，由 `findSource`、
  `PlayerResource`、`PlayerLayerQuery` 等 caller 共用。本文旧版将这两项列为 OPEN 的
  结论已被当前源码和 fresh 反编译证伪，现就地纠正为 CLOSED。
- 当前验证：macOS Release `psbfile-dll` 为 575/575（10 cases），完整
  `motionplayer-dll` 为 1212/1212（16 cases），`motionplayer-ttstr-hash-test` 为
  100/100（22 cases）；显式 `krkr2_wasmtime_guest` 目标与 Web Debug 最终链接均通过，
  `git diff --check` 通过。
  guest harness 的 `clipRect` 参数类型已随生产字段一并改为 `std::array<float,4>`。

## 结论

当前不能宣称整个 Web 项目已经“尽可能 100% 一比一复原” Android
kirikiroid2 的 PSB 数据链。

截至 2026-07-23 当前工作树，结论仍为 **NO**：Web Debug 最终链接及完整
`motionplayer-dll`、独立 `psbfile-dll` 运行测试均已通过。Player 运行时的
`_activeMotion/shared_ptr<MotionSnapshot>` 双轨 owner 已在本轮删除，live motion 的加载、
门控、路径上下文、绘制与更新现在只由 Android 对应的 +528/+1012 raw Variant 驱动；
生产 `motionplayer` target 已不再编入或链接 decoded `MotionSnapshot` loader、
`PlayerFrameStep/PlayerFrameStepping` 兼容测试模型和 `psbfile_decoded_compat`；snapshot 类型、
decoded 帧/曲线/像素/字典 helper 及原先的 offline-only 文件也已从仓库整体删除，
不再存在这张 eager 对象图。live `MotionNode::ClipSlot` 的 `src/icon` 已收束为 Android 同形的两只
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
- Web `motionplayer` 的 `ResourceManager` HashMap A 已改为
  `LoadedResourceRecord{PSBFile, Win texture map, KRKR source-entry map}`，
  cache hit/miss、严格元数据校验、filter、unload 和每次新建 root dispatch 的主链已经
  复刻。raw load 后再次构造 eager `MotionSnapshot`、按 dispatch/path 登记两只全局
  强引用表的旁路已经删除；`findMotion` 的 `[raw motion, matchedKey]` 现在直接进入
  Player 的 +528/+1012 对应 owner。旧 `psbfile_decoded_compat` target 已全部删除；
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
  拓扑、crossfade 行为和 nodeType 5/10 类型输出亦已闭合。`sub_704CB8` direct-deque
  caller 与跨函数 hint 共享拓扑也已在 2026-07-22 的复核中闭合。总体仍不能宣称 100%：
  除现有真实资产/Android oracle 的验证缺口外，render-list/build/execute 端到端链仍在
  继续按 `0x6C2334/0x6C4E28/0x6C7440` 审计，不能把局部闭合外推为全链闭合。已排除的
  结构偏差仍包括
  `interpolatedCache`、source cache、曲线解码镜像、节点 label/path 镜像、CMake 源传播、
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
  synthetic root 已经切断 snapshot。`MotionSnapshot` API、离线 decoded 工具/兼容模型、
  其声明、实现及 decoded frame/resource helper 已从仓库整体删除；source texture 的
  Win/spec=2 与 KRKR/spec=1 像素链已经全部切到 mapped record raw nodes；
  `SourceCache` 的非 atlas 路径也已切到 `ResourceManager.findSource → ObjSource →
  drawLayer(entry.layer)`，并删除 `_activeMotion`/`sourceCandidates` 像素旁路；
  D3DEmotePlayer 的五个变量枚举接口已恢复为 Android 的精确 TODO 异常边界，
  `EmotePlayer.getVariableRange/getVariableFrameList` 已分别切回 Engine HM5（miss 时递归
  扫 Player+384 参数表）与 Engine+1248 Dictionary，删除对应的 snapshot 查询面；
  `Motion.Player.variableKeys@0x6D139C` 已改为每次从 Player+1296
  `std::deque<VariableLabelScope>` 的 `cascadeKey` 新建 TJS Array，删除 snapshot label
  缓存、伪 setter 及其额外 owner；当前已通过 `sub_704CB8` 对应 helper 直接写
  `tTJSArrayNI::Items` deque，不再经过中间 vector 或 `FuncCall("add")`。
  `Player_updateLayers@0x6BB33C` 的变量阶段已恢复
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

权威来源是 Android `libkrkr2.so` 的 IDA 反编译。2026-07-19 后续边界复核将 PSB
插件相关实现从原先截断的 `0x59641C..0x59AA84` 扩展到
`0x59641C..0x59B708`；2026-07-22 又拆开 IDA 错并的 `0x59A8D8`、`0x59A968`、
`0x59B14C` 三个独立序言，得到 **111** 个业务/NCB函数。`0x59A8D8/0x59A968`
分别是 typed NCB 自动注册/反注册入口；`0x59AA84` 是注册尾链的起点而非终点，
其后实际有 20 个入口覆盖 native-instance holder、成员注册、raw factory、root 属性 wrapper、
load 方法 wrapper 和首参 Variant 转换。2026-07-23 继续沿
`GetDictionaryKeys@0x598E64 → PLT 0x423250/0x42325C` 追踪，确认下一函数
`0x59B7E8` 正是该源码触发的 `std::vector<std::string>::_M_emplace_back_aux`
扩容慢路径；完整集合因此为 **112** 个相关函数，覆盖到该函数的 exclusive end
`0x59B9C8`。紧随的 `sub_59B9C8` 被 `sub_42CFA0` 作为字面 `PackinOne.dll`
callback 注册，且从 `fstat.dll` 起加载子插件，属于下一模块。完整分组
manifest 见 [psbfile_function_coverage_2026-07-19.md](psbfile_function_coverage_2026-07-19.md)。

模块注册点 `0x42CF28` 给出二进制字面名字：

- module: `PSBFile.dll`
- class: `PSBFile`
- pre-register callback: `0x59849C`
- unregister callback: `nullptr`

## 反编译覆盖矩阵

| 领域 | Android 地址 | 已复原结构/行为 |
| --- | --- | --- |
| 名字查找 | `0x59641C` | double-array trie，按 UTF-8 byte 逐步转移 |
| 字典查找 | `0x59659C` | packed name-index 数组上的二分查找；命中任一相等 midpoint 即停止，不继续收缩到第一个相等项 |
| 惰性值转换 | `0x59673C` | null/bool/int/real/string/octet 惰性转换；array/dictionary 创建共享 owner 的新 dispatch |
| 字符串/资源 | `0x596BC4`, `0x596C70` | 返回 raw buffer 内借用字符串指针；按 chunk offset/length 返回资源视图 |
| dispatch ABI | `0x596D78..0x597AD4` | 直接双继承 `iTJSDispatch2`/`iTJSNativeInstance`，独立 intrusive refcount、owner、node、valid byte；完整 vtable 默认值；PropGet/PropGetByNum 的成功与非 throwing miss 均无条件解引用 result |
| 成员枚举 | `0x596F50` | array 使用十进制下标名，dictionary 使用 packed 顺序；`TJS_ENUM_NO_VALUE` 控制回调参数个数 |
| count/index/property | `0x5975E0`, `0x5976C4`, `0x597854` | array count、负下标、dictionary 属性、`TJS_MEMBERMUSTEXIST` 边界 |
| name decode | `0x5975C0`, `0x597B1C` | 先经 `namesData[nameIndexes[index]]` 找 terminal，再沿 parent 回溯并 reverse |
| NCB 注册 | `0x597E98..0x5981F8`, `0x59A8D8`, `0x59A968`, `0x59AA84..0x59B708` | typed class state、自动注册/反注册、factory、native holder、`root` property、`load` method；本地由 ncbind 模板承接通用注册机制；factory 在 load 抛异常时析构已写入 result 的 native holder、保留悬挂 result slot 后原样重抛 |
| root/load | `0x5981F8`, `0x598268`, `0x598538` | root 每次返回新 dispatch；string/octet 分流；小写 `mdf` 解压及失败 fallback；storage 数据 buffer 使用裸指针，异常清理只析构 stream |
| owner | `0x598708`, `0x598960`, `0x598A64`, `0x598AAC`, `0x598B3C` | 一个 owner 独占一个 raw allocation；intrusive ref；替换时释放旧 owner；`0x598A64` 是按值返回并消费 source 的 transfer helper，但内部源级 special member 不可辨识；Adopt/transfer 均保留零引用删除分支；filter 后刷新 header view |
| node helper | `0x598A3C..0x5996E4` | raw-pair 构造/复制/消费的净语义、字符串、strict/try lookup、bool、keys、int/double、category、contains、resource；strict miss 的异常 helper 若返回则输出空 owner/node；是否存在用户声明的 copy/move special member 不可由优化后二进制唯一判定 |
| media 生命周期 | `0x59849C`, `0x5997F0..0x5998A8` | function-local static 指针由 `__cxa_guard` 构造一次；process-lifetime singleton、初始 ref=1、名字 `psb`、不注销 |
| media 访问 | `0x5998BC..0x59A4B0` | normalize no-op、exists/open/list/local-name、按首段缓存一个 PSBFile TJS object、contains→strict 逐段遍历；strict 返回 pair 只净更新局部 current，失败保持 caller out 不变，成功尾块才 copy/AddRef 写回 out；循环内源码是 move 还是 copy+临时析构不可辨识 |
| motionplayer 原始加载链 | `0x6A8D8C`, `0x6A87D0`, `0x685D30`, `0x6863CC` | 规范化路径→缓存 raw owner→全局 `std::function` filter→严格读取 id/spec/version→每次新建 root dispatch；seed setter 接受至少一个可转整数的 TJS 参数 |
| motionplayer motion 查找 | `0x6A96F8`, `0x6A9ED4` | direct map hit 与 fallback node-chain 分别展开；`isExistMotion` 全程 raw-node，`findMotion` 只把最终命中 node 包成 dispatch，并返回 `[motion, matchedKey]`；三处 dispatch ctor xref 与 Release 对象一致 |
| callable 解密/注册 | `0x682528`, `0x685E60`, `0x6864C0`, `0x6864C8`, `0x6865B4`, `0x62C808` | emoteplayer entry 动态注入两个 static method；callable 由 `tRefHolder` 形状的 pointer+refcount 控制块共享；每次以同一个 `CBinaryAccessor` 类型传 `(whole-file view,size)`，返回值忽略；替换 filter 释放旧 Object/ObjThis |
| motionplayer 缓存生命周期 | `0x6A8438`, `0x6A8B94`, `0x6A8CF8`, `0x6A959C` | `clearCache` 只清 SourceCache 图层链；析构依次销毁 raw owner map、layer-id set、random variant 和基类状态；`unloadAll` 只清 raw owner map；`unload` 规范化路径后按 key 擦除 |
| motionplayer dispatch helper | `0x662668`, `0x6635DC`, `0x6636D4`, `0x529524`, `0x56C694`, `0x6695BC`, `0x6637BC` | 统一按 holder dispatch + 同一 objthis 调用 `PropGet/PropGetByNum`，再做普通 TJS real/int/bool/string 转换；count 来自 `PropGet("count")` |
| 变量轨道所有权/读取 | `0x6CD750`, `0x6B786C`, `0x6B7A70`, `0x69A754` | `frameSource`、`easing` 都是 `tTJSVariant` CopyRef；step/merge 通过 dispatch 读帧；`interval/value` 来自 `content`，`easing` 来自帧对象；Bezier 的 x/y 也经 dispatch 逐项读取 |
| 变量查询边界 | `0x53041C`, `0x530530`, `0x530568`, `0x530588`, `0x5305A8`, `0x673BEC`, `0x68229C`, `0x6D6590`, `0x6D676C` | D3D 五接口无条件抛精确 TODO `eTJSError`；Emote range 先查 HM5 的 frameMin/frameMax，miss 后递归折叠当前/子 Player +384 参数表；frameList CopyRef +1248 Dictionary 后 PropGet label |
| Player variableKeys | `0x6D139C`, `0x704CB8`, `0x6D69C8` | NCB RO 属性、数据源、每次新建 Array 的 owner，以及 `sub_704CB8` 后直接向 `tTJSArrayNI::Items` deque emplace 的写入路径均已对齐；无中间 vector/`FuncCall("add")`；CLOSED |
| Player tags/skipToSync | `0x6D9618`, `0x6D3504`, `0x6D69C8` | `tags` 直接 CopyRef Player+1072 raw tag variant；skip 的 playing/loopTime gate、tag-frame dead reads、时间 clamp 与两只连续 flag byte 均按函数体复原，不经 snapshot timeline |
| 粒子 source list | `0x6BF0DC`, `0x697D34` | node+2200 `particleMotionList` 保持独立 `tTJSVariant` owner；发射时直接 `PropGet("count")`、`PropGetByNum(randomIndex)` 后拆路径，不再经 ClipSlot/FrameContentState/interpolatedCache 的三层 `vector<string>` 镜像 |
| ClipSlot source 所有权/消费 | `0x69260C`, `0x692AB0`, `0x699510`, `0x6997F0`, `0x6948E8`, `0x6B64AC`, `0x6BE0C0`, `0x6BEDD0`, `0x6C2334` | slot+28 icon 与 slot+36 src 由两只 `ttstr` 直接持有；reset 只释放 src，HM3 value+44 CopyRef src 但 restore 不写回；init/findSource、child、particle-emitter 与 render-item 均直接消费 slot owner。child 单段路径 `setChara(src)+play(icon)`，多段路径固定取 `[1]/[2]`，无二段兜底 |
| source render 写回链 | `0x699AE4`, `0x6C2334` | timeline evaluator 只写节点变换/颜色/opacity/类型专用标量，不复制或插值 source；render-list builder 从 active slot+36 直接 AddRef 到 render item。生产 `FrameContentState/interpolatedCache` 的 `icon/src` owner 与 fallback 已删除，离线 decoded 字符串仅留 `OfflineFrameContentState` |
| 节点运行态标量与 mesh owner | `0x692AB0`, `0x6996E8`, `0x699AE4`, `0x69AC4C`, `0x699510`, `0x6997F0`, `0x69B1E8`, `0x6BC4F0`, `0x6C2334`, `0x6C715C`, `0x6D5264` | `interpolatedCache`、生产 `FrameContentState` 与 `localState` 整体删除；evaluator 直接写 `accumulated/colorBytes/particleInterp`，opacity 为整数 0..255，HM3/geometry 直接从 active slot 与 node runtime 取值。普通 transform/type-4、mesh crossfade、type-5 camera.fov 与 type-10 feedback.timespan 写回及消费链均已闭合；node/slot/HM3/render-item 的 mesh 容器已统一为 8B `{float x,float y}` 元素，只有 TJS/Layer 平台边界展开为标量/`tTVPPointD`。 |
| action/dtgt 与事件队列 | `0x692AB0`, `0x6B638C`, `0x6B6ADC`, `0x6B9A3C`, `0x6C4490`, `0x6BE0C0`, `0x6BEDD0` | slot action/dtgt 由 `ttstr` 持有；44B 事件源码形状恢复为 `int + tTJSVariant + tTJSVariant`，layer action 保留 void param1，node action 保留 label/action 两只 String variant，dispatch 前 CopyRef 后直接 `onAction`；dtgt 直接进入 Player+24 `map<ttstr,int>` 查找 |
| 节点 label/path 所有权与键空间 | `0x6B3C78`, `0x6B4A6C`, `0x6B51F0`, `0x6B5C1C`, `0x6B2D3C`, `0x6B826C`, `0x6B638C` | node+0 直接持有 PSB `label` 的 `ttstr`；Player+24 是 raw-label `map<ttstr,int>`；HM3 路径由每级 `L"/" + label` 前插构造为独立 `ttstr` key，两个键空间不再 narrow/widen；node action 从同一 label 构造 String variant |
| live 曲线所有权/求值 | `0x692AB0`, `0x699AE4`, `0x69A754`, `0x698454`, `0x69A4D4` | `ccc/cp/acc/zcc/scc` 只由活动 ClipSlot 的 raw `tTJSVariant` 持有；每次求值即时 `PropGet x/y/t/s` 及 `PropGetByNum`，不再解码到 `vector<double>`；decoded Bezier/spline 结构、回退算法及 offline 文件已整体删除 |
| 静态插件模块边界 | Android 单体 `libkrkr2.so` 的 NCB 静态注册链；Web/macOS 静态归档平台边界 | 所有插件 target 的源码均改为 `PRIVATE`，`libmotionplayer.a` 与 `libkrkr2plugin.a` 的 Ninja 输入只含各自编译单元；最终 executable 通过 force-load/whole-archive 保留 registrar-only 对象，不再用 `PUBLIC target_sources` 把 scriptsEx/psdfile/layerExDraw/fstat 对象重复归档进 motionplayer |
| Player parameter table | `0x6B365C`, `0x6B1718`, `0x6B202C`, `0x6B1ECC`, `0x6B1ABC` | `parameterize/parameter` 全部从 Player+528 raw dispatch 读取；56B vector 项使用 `ttstr id`，严格 Object/Integer 分支，+408 multimap 保留重复注册和父链 owner 形状 |
| Player preview/directEdit 字段拓扑 | `0x6CF0A4`, `0x6D9638`, `0x6D9640`, `0x6BC000`, `0x6BC4F0`, `0x6BD8DC`, `0x6BE0C0`, `0x6BEDD0`, `0x6BF0DC`, `0x6C2334` | +1092 是 `preview` 的单一 owner，负责节点类型 mask/整段 pass gate；子 Player 角度重初始化仍读取独立 +482 `directEdit`；已删除 snapshot root type 派生的第二模式字节 |
| Emote 状态持久化 | `0x675E40`, `0x678044`, `0x6767E4..0x677E28`, `0x678454..0x67B34C` | `EmotePlayer.serialize/unserialize` 直接操作 Engine 的 timeline/controller/base/outerforce raw 容器；固定八键 Dictionary、请求队列与 angle shipped quirk 均按二进制复原 |

## 六维对照

### 1. 源代码结构

`PSBRawOwner`、`PSBRawNode`、一指针 `PSBFile` holder、直接双接口
`PSBValueDispatch`、`PSBMedia` 已分层。ARM64 字节偏移只记录为反编译证据，
没有用 padding 或 packing 强行污染 wasm32 ABI。

旧 eager decoder、`loadMotionSnapshot`、`PlayerFrameStep.cpp`、
`PlayerFrameStepping.cpp` 及其 `psbfile_decoded_compat` / `motionplayer_offline` targets
已全部删除。fresh decompile
`Player_ctor@0x6CED30`、`Player_dtor@0x6CFADC`、`Player_loadMotion@0x6B0F10` 也确认
Android Player 没有 decoded owner；`motionplayer_ncb_register@0x6D9B08` 的注册链没有
DecodedPSB/TypeHandler，故生产 `main.cpp` 已去掉 `PSBFile.h` 的静态注册副作用。

`MotionSnapshot`、`loadMotionSnapshot`、decoded 帧/曲线/资源 helper 及此前的
`OfflineMotionSnapshot.h/.cpp` 已整体删除；`RuntimeSupport.h`、`PlayerInternal.h`、`Player.h` 的静态
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
eager `tTJSVariant`/`shared_ptr<IPSBValue>`；HashMap A 的 mapped value 是
`LoadedResourceRecord`，其首字段是一指针语义的 `PSBFile` holder，随后内联 Win/KRKR
两张 source map；命中时从 record.file 的同一 owner 创建新的 root dispatch。

raw 主链后的 `attachDecodedSnapshotCompatibility` 已删除：cache hit/miss 都只从
HashMap A 的 `LoadedResourceRecord::file` 创建 fresh root dispatch，不再第二次读取/解码文件。
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
- ResourceManager cache hit `0x6A8E94..0x6A8EB8` 可证明一指针 holder 的 copy+AddRef；
  miss insert `0x6A926C..0x6A92A8` 可证明 mapped record 上的 Release-old→copy→AddRef，
  两处临时 holder 随后各自 Release。这里只证明具体 callsite 的净序列；优化后二进制不能
  唯一证明 `PSBFile` 声明了哪种 copy/move special member，也不能证明通用 assignment
  是否存在 self guard。本地 copy-shaped 表示只负责保留上述已观察生命周期。
- holder 替换文件只释放自己的 owner 引用，旧 node/dispatch 仍保持 allocation 存活。
- `0x598A64` 不是 move ctor 函数本体，而是按值返回并消费 source/this 的 transfer helper；
  可证明的是复制 owner、incoming zero-ref 删除和清 source，不能证明其中内联了何种
  special member。`Transfer_guess` 的名字/member 身份因此继续标为猜测。
- `PSBMedia::Resolve@0x59A698..0x59A6EC` 可证明的净序列是 release current、安装 strict
  getter 返回的 owner/node、保留 incoming zero-ref 删除边界。优化后指令既可由 move
  assignment 产生，也可由 copy assignment + 临时析构经相消产生；本地采用的 move-shaped
  special member 只是一个保持净语义的表示，不能标成已恢复的唯一源码结构。
- `sub_598D58@0x598D58` 只证明该 try-get hit 的 out 参数按
  Release-old→copy owner→AddRef→write node 更新，不能单独证明原始类存在通用
  `PSBRawNode::operator=(const&)`。后续已在 `sub_695DE8@0x696A90` 找到同一 raw-node
  同时作为 source/out 的真实 alias caller，证明 try-get 本体没有 self guard；但这仍不证明
  未知的通用 copy/move special member 是否存在 self guard。
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
的 map/vector 对象树及 compatibility target 已全部删除。
ResourceManager HashMap A 使用
`std::unordered_map<ttstr, LoadedResourceRecord, ttstr_hash, ttstr_equal>`；record 内声明
`PSBFile -> Win texture map -> KRKR source-entry map`，对应 ctor `0x6A88CC` 与 mapped ctor
`0x6EBCFC` 的 libstdc++ bucket/node-chain/嵌套容器拓扑；
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
dispatch ctor `sub_597AD4@0x597AD4` 的 ABI 可观察到 incoming owner-slot value 与 node
pointer，函数体把二者写入 dispatch 并对 owner AddRef；各调用点产物也没有本地旧版 retained temporary 的
额外引用计数边界。本地用两个裸参数表达这条净数据流，但二进制不能排除源码参数原本是一只
零开销 raw-node holder 子对象，因此不把“两裸字段签名”升格为唯一源码形状。typed root
getter、ResourceManager load 尾段和 findMotion 两个命中分支均由该 ctor 建立 dispatch owner
引用，不再经过本地通用 Create factory。
raw node validity `sub_598E44@0x598E44` 独立检查 `owner && node`；类型消费者
`sub_599554@0x599554`、`sub_5995D8@0x5995D8` 直接读取 `node[0]`。本地 `GetType()` 旧有的
null→tag0 安全归一化已删除，未先做显式 validity 检查的调用保留原始空指针边界。

### 6. 边界行为

反编译并落实到本地实现：最小 0x40 字节、`PSB\0` signature、offset 的严格/非严格比较、
MDF 解压失败 fallback、storage invalid-buffer 的原始泄漏边界、unknown tag 抛错、
known non-dictionary contains=false、strict missing-key 抛错、负数组下标、
`TJS_MEMBERMUSTEXIST`、dispatch invalidate、no-op normalize、空 locally-accessible name、
packed value tag `0x11` 与有符号 stride，以及真实加密 motion PSB 的 xorshift filter。

尚不能封口：没有现成 fixture/oracle 覆盖 filter 后 offset 验证失败、MDF zlib 失败的
runtime 路径，以及损坏 packed table 的实际越界/崩溃表现。现有 PIMG 与加密 motion PSB
已经覆盖 NCB typed class 的真实 TJS 构造、同-container media 复用/miss、缺失 container
异常后旧缓存保留、成功跨-container 替换，以及替换后旧 stream metadata/析构的本地守护。
stream 的 borrowed/non-retaining 性质仍由反编译构造链证明；本地测试不读取悬挂 block，且
Android runtime oracle 已实现但本轮无连接设备、尚未取得真实结果，不能把本地守护或
离线 fake-engine/RPC 验证提升为二进制运行结果。

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
    `0x6A959C`、ctor `0x6A88CC`、mapped ctor `0x6EBCFC` 与 dtor `0x6DB3E8`
    证明 mapped value 是 `LoadedResourceRecord`：首字段为可复制的一指针 `PSBFile`
    holder，随后是 Win/KRKR 两张 source map；cache hit/miss 共用 fresh-dispatch tail。
    现已纠正 value 类型、filter、严格
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
| frame slot/evaluator | `parseFrame@0x6926B4` 接收 raw `frameList+index` 且只 parse；`mergeFrameContent@0x692AB0` 再按 slot index 取 content，保留 raw 字符串/variant、32 数值 mesh 与两槽 merged 状态 | live 节点已按 raw `frameListVariant` 实现 selective reset、parse/merge 分离、raw owner、mesh 32 数值、init/reseek/forward/back/modified 重建；node 0 按 `0x6BB4D4` 始终是 synthetic root 并直接复制 delta。旧 decoded/legacy/test model 已整体删除 | CLOSED |
| source texture | `0x6948E8/0x695DE8` 从 RM HashMap A 的 record.root 导航；`0x695DE8` 同时被 Player 与 render-time getter `0x6F1060` 调用；prepared item 在 `0x6C360C` 直接保存 `SourceState*`，`0x6AE154..0x6AE188` 在 getter 后现场重读 rect；`0x6D5C68` 则使用 direct getter `0x6F67CC`；非 atlas 路径把 `SourceState.object` 经 `0x6C1B70` 送入按 `(full Variant key,src,blendMode)` 命中的 `SourceCache_loadSource@0x6A7BA8` | mapped record、两表及 unload 生命周期已复原；2026-07-23 又恢复共享 `0x695DE8` 边界、两种 getter、item→SourceState 直接 alias、getter 后 rect 重读、object-only fallback、精确 cache tuple、Player 常驻 descriptor/color Dictionaries、公开 NCB `(source,descriptor)`、分支内重复资源调用与字段写序；atlas rect/尺寸已纠正。整页 CPU Update 是 Web 纹理 API 边界；`Player.loadSource(name)` 仅是额外 Web compatibility helper，不污染精确 cache | AUDITED PRODUCTION SITES + EXTRA COMPATIBILITY SURFACE + PLATFORM BOUNDARY |
| variable query/interpolation | D3D 五个枚举方法是无条件 TODO throw；Emote range/frameList 读取 Engine HM5/+1248，HM5 miss 才递归 Player+384 参数表和所有子 Player；updateLayers 无条件调用 `0x6BBE20` 遍历 +1296 var-track deque | 五个 D3D wrapper、range/frameList 与 updateLayers live 插值均已切到 raw Engine/Player owner；旧 snapshot frame/range 查询及首帧旁路已删除 | CLOSED |
| Player variableKeys | getter 直接遍历 Player+1296 `std::deque<VariableLabelScope>`，每次分配并返回一个新 Array；没有 setter、Player 缓存或 motion-load 副作用 | 数据源与 owner 已对齐；`createTJSArrayWithItems_guess` 复刻 `sub_704CB8` 的 Array Variant + borrowed Items 组合，getter 直接按 deque 顺序 emplace | CLOSED |
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
    `PlayerFrameStep/Stepping` 及旧 legacy/独立测试模型已整体删除，故不再构成第二条
    frame slot 数据流；该项记为 CLOSED。
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
    Update”是已注明的 Web 纹理 API 边界；这些 named raw owner/decode 站点已
    对齐，但不能据此把尚未逐调用者复核的 source 数据流或整个
    psbfile/motionplayer 记为 CLOSED。
44. 联合反编译 `Player_findSource@0x6948E8`、
    `ResourceManager_findSource@0x6AAB3C`、`SourceCache_loadSource@0x6A7BA8`、
    `ObjSource_getClip@0x69D35C`、`ObjSource_drawLayer@0x69D6D8` 与
    `ObjSource_ensureTexture@0x6DA454`，确认非 atlas source 不从 decoded motion
    side graph 取像素：RM 直接包装 raw icon node，ObjSource 惰性读取 width/height/
    pixel/compress/pal，执行 RL8/RL32、palette expand 或 ReverseRGB，仅持有一只 texture；
    SourceCache 对目标 cache entry Layer 调 `drawLayer(entry.layer)` 后进入颜色 bake；
    `_bufLayer` 只参与低四位 blend 1/2 的后处理。**2026-07-19 纠错：**此前虽删除
    decoded 旁路，却把 ObjSource 内部误写成 `tTJSVariant` dict facade，并据此过早标为
    CLOSED；fresh xref/构造/析构审计证实其真实字段是 raw owner/node pair + texture。现已把
    `findSource/getClip/drawLayer/ensureTexture` 全链改为直接 `PSBRawNode`，同时恢复两次 `pal`
    gate、逐字段 clip 写入、aligned buffer、pitch-copy、`tTVPBitmap→texture` 与析构顺序；
    `SourceCache.cpp` 仍无 `_activeMotion` 或 `MotionSnapshot` 类型消费。
44a. **2026-07-23 纠正过早 CLOSED：**fresh xref 证明 `sub_695DE8` 除
    `Player_findSource@0x694BF0` 外还有 `sub_6F1060@0x6F1148` 第二调用者；后者由
    `D3DAdaptor_renderFromPlayer@0x6ADE24` 传给 `sub_6ADFBC`。prepared item 在
    `sub_6C2334@0x6C360C` 保存 node 持久 `SourceState*`，纹理 getter 可就地补建 atlas，
    随后的 `0x6AE154..0x6AE188` 再从同一对象读取 rect。本地原先保存
    `sourceObject/sourceTexture/sourceRect` 快照，既漏掉第二调用者，也让 getter 后的新
    rect 不可见；该错误不能归为 Web 平台边界。现已恢复共享 out-of-line helper、直接
    alias 与现场重读。同期逐指令复核还纠正了 `findSource` 整对象 clear、Win
    texture/icon 顺序、spec2 伪造 path、fallback 提前返回/valid 时机，以及 KRKR 解码中
    被错误提升合并的 compress/pixel/resource 调用，把 strict-pal 纠正为 try-pal，
    并把两个清零 size 槽纠正为单个未初始化 size 槽。
    最后两条调用边界也已拆清：`D3DImage_draw@0x6D5C68` 传入
    `sub_6F67CC`，只返回现有 `SourceState.texture`；只有
    `D3DAdaptor_renderFromPlayer@0x6ADE24` 传入 `sub_6F1060`。
    `sub_6F1060` 与 PrivateMotionGLL `0x6DE738` 的 fallback 都把 helper 调用后的
    `SourceState.object` 直接交给 `sub_6C1B70`，不读取 `source.path`。本地生产路径现按
    `(commandKey,commandSrc,blendMode)` 精确匹配，color 仅触发原节点重烘焙，incoming
    object 只在 bake 调用期间借用，且禁止把 module key 当 storage path。后续 fresh 证据已
    恢复公开 NCB `(source,descriptor)`；剩余 `Player.loadSource(name)` 是独立 Web-only 兼容 helper，
    不是 alias cache 或 NCB 替代实现。
    因此旧“source texture 整链 CLOSED”表述只可保留为对当时 raw owner/map 拓扑的历史
    结论，不得再外推为未经逐调用者复核的 100% 证明。
44b. fresh `Player_ctor@0x6CED30`、`Player_random@0x6BA7B8` 与
    `ResourceManager_ctor@0x6A88CC` 交叉证伪了旧“Player+676 是 RandomGenerator”：
    +676/+716 是逐 render item 写入 key/src/blend/color 的 descriptor 与 color 对象；
    Player+992 才是 ResourceManager dispatch，`Player_random` 对它调用 `random`；真正
    `Math.RandomGenerator` 由 RM ctor 构造并持有。本地已删除每 Player 重复 RNG 及 child
    传播，直接沿 `_resourceManager` dispatch 调用。同时保留 ctor 对同一 RM dispatch 的
    三份独立 Variant owner（findSource / render SourceCache / canonical+random），并在
    第二、三份 RM owner 之间持有 descriptor/color 两只 Dictionary，构造时执行
    `descriptor.color = colors`。声明顺序为 findSource → render SourceCache → descriptor
    → colors → canonical，使普通 C++ 逆序析构恢复 canonical → colors → descriptor →
    render SourceCache → findSource 的对象生命周期。
    相关旧 analysis/memory 同步就地纠正。
44c. fresh `SourceCache_ctor@0x6A78F4`、`trim@0x6A6B08`、
    `loadSource@0x6A7BA8`、`clearCache@0x6A8438` 与
    `EmoteObject_init@0x67DBAC` 证伪了旧“SourceCache ctor 第二参数是 layerType”的标签。
    它实际是 cache byte limit；cache 另持 current bytes，每节点在 `drawLayer` 后记录
    `4*width*height`。miss 仅在插入前且 current>limit 时 trim：按新到旧扫描，以有符号
    比较决定逐节点保留或删除，因此结果是 greedy subsequence，并不保证是连续前缀；阈值
    为 uint32 `limit*99/100`。随后烘焙、计重、front 插入；同色 hit 原地返回，只有 color
    mismatch 才在同一 Layer 上重烘焙，再 `push_front(copy)+erase(old)`。clear 对每个 Layer
    调 dispatch `Invalidate(self)`，清 list 并把 current 归零。EmoteObject 先求值
    `global.kag`，再把包含 `Object/ObjThis` 的完整 `tTJSVariant` 以字面 20 MiB 传给 RM，
    RM 又把完整 Variant CopyCtor 给 SourceCache base；不再降成 raw dispatch 后重建 closure。
    production route 的容量、命中顺序、owner/bufLayer 构造链现已复刻。2026-07-23 fresh
    `loadSource@0x6A7BA8`、节点 copier `0x6EAC60`、bake `0x6A6BE0` 与 Player caller
    `0x6C1B70` 又纠正了两项旧结论：源码容器是 `std::list<Entry>`，不是手写 intrusive
    list；Entry 身份是完整 `tTJSVariant key + ttstr src + blendMode`，color 仅为 mutable
    payload。公开 NCB 签名现为 `(iTJSDispatch2 *source, iTJSDispatch2 *descriptor) -> Layer`，
    color mismatch 的节点复制/旧节点析构和 Player 常驻 descriptor/color Dictionary 均已落地。
    `Player.loadSource(name)` 的 by-name helper 仍是独立 Web 兼容面，但不再污染精确 cache。
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
    所有权和空状态行为均不一致。现已改为直接遍历 `_variableLabelScopes`，并删除
    `_variableKeys` 字段、伪 setter、clear 与 snapshot 同步函数。2026-07-22 fresh
    decompile `sub_704CB8@0x704CB8` 后又确认当前实现已通过
    `createTJSArrayWithItems_guess` 持有 Array Variant 和 borrowed Items 指针，并直接向
    `tTJSArrayNI::Items` emplace；该调用链/容器写入路径现为 CLOSED。
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

- 2026-07-23 source consumer/cache/RNG 纠正后，Web Debug 从 32 个受影响目标重新编译并
  最终链接 `index.html` 成功，`git diff --check` 通过；显式
  `psb_rl_decompress_wasm` 目标为最新。独立 Wasmtime driver 使用仓库现有 8 个
  `psb_rl_decompress` case 全部完成（8 host calls，无 crash）。标准 LLDB runner 仍因本机
  缺少 `wasm-objdump` 无法进入 probe；此前隔离 LLDB 尝试又受 macOS attach 权限阻止，
  因而这 8 项只记作 port-wasm 执行验证，不冒充 LLDB/Android oracle 差分。
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
  media/TJS 与 packed-table 损坏输入覆盖；下一阶段闭合了 typed TJS 构造、array listing
  和同-container 缓存分支，当时跨-container 替换、dictionary listing 与 adaptor-null
  仍未覆盖。2026-07-23 已用现有加密 PSB 补上前者的本地生命周期覆盖；后两项仍开放。

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
  **不**把 bit55 扩展到最高字节。`GetInt@0x599438` 自身对 tag `0x09/0x0A/0x0C`
  明确物化 X0；完整 20 个 direct xref 中 18 个只消费 W0，另两个丢弃返回值，没有 caller
  消费 X0 高位，因此该 quirk 在现有 caller 上不可观察。二进制尚不能唯一闭合其源码签名
  是 `tjs_int` 还是 `tjs_int64`，不能把 caller
  截断误写成 callee 的返回宽度；惰性 TJS Integer 与 double 转换仍会观察到差异。本地通用
  reader 已恢复 Android 的 7-byte zero-extension 边界。仓库没有天然 tag `0x0B` 物料，
  按规则未构造 fixture。

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
  `PSBValueDispatch_EnumMembers@0x596F50` 与 media `GetListAt@0x5999F4`
  交叉确认：数组 count 在 TJS numeric 访问和枚举调用面均折叠成 signed 32-bit，负索引
  加 count 也执行 32-bit ADD；dictionary count 的循环仍保持 unsigned。该轮恢复了上述
  numeric/枚举调用面，却遗漏 `PropGet@0x597854` 返回 `count` 时也经
  `operator=(tjs_int32)@0xA0FF28` 做 `SXTW`；后续逐函数复审已把该最后一处从本地
  `tjs_int64` 零扩展纠正为 signed 32-bit 赋值。

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

- 2026-07-19 对当时截断于 `0x59AA84` 的 PSB 主区 90 个函数重新枚举后，fresh decompile
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
  `PSBMedia::Resolve@0x59A4B0` 时曾把两条生命周期错误合并：try-get 命中及 Resolve
  最终 out 写回确实走 Release-old→copy owner→AddRef→write node；Resolve 循环内的
  `0x59A694..0x59A704` 则只有 release/install/zero-ref-delete 的优化后净序列。2026-07-22
  source-shape 交叉审计证明该指令序列既可来自 move，也可来自经优化相消的 copy + 临时
  析构；本段曾先后把它唯一解释成 copy 和 move，两种强断言均作废，只保留净语义。

- 2026-07-19 重新从 IDA 枚举当时定义为 `0x59641C..0x59AA84` 的 **90** 个函数，并 fresh
  decompile/disasm typed state/registrar `0x597E98..0x5980F4`、完整 dispatch 微型 vtable
  槽 `0x596D78..0x597AD4`、media ctor/dtor/ref/name `0x59849C/0x5997F0..0x5998A8`、
  char-index helper `0x59A284` 与 owner 主链 `0x598708/0x598960/0x598AAC`。逐槽确认
  `TJS_E_NOTIMPL`、valid/invalidate、native Construct、media 非原子引用计数、字段逆序
  析构及 Factory→root→load 注册顺序均与本地一致；同时补回两处此前被正常路径掩盖的
  生命周期边界：`sub_598708@0x598828..0x598840` 在 Adopt 替换后仍检查新 owner 的
  zero-ref 并析构，`sub_5980F4@0x5981A0..0x5981EC` 在构造参数 load 抛异常时析构已经
  发布到 result 的 native holder、保持 result slot 不清零并原样重抛。Mac 四目标构建、
  `psbfile-dll` **484/484**、`motionplayer-dll` **398/398** 与 Web Debug 最终链接全部
  通过。该轮覆盖后来发现漏计 `0x59AA84..0x59B708` 的 NCB tail；后续 manifest
  先补到 108，2026-07-22 再拆出三个被 IDA 合并的入口并纠正为 111 个业务/NCB函数；
  2026-07-23 又沿 PLT 调用链补回 `0x59B7E8` 的 vector 扩容慢路径，当前为 112。
  `adb devices -l`
  当前为空，本轮 Android oracle 未执行；这是外部验证缺口，不是
  实现失败，也不替代此前在线 AVD 的 `status=ok` 历史证据。

- 2026-07-19 fresh decompile `sub_59673C@0x59673C`、
  `PropGetByNum@0x5976C4`、`PropGet@0x597854`、`EnumMembers@0x596F50` 与 raw node
  `0x598B3C/0x598C58/0x598D58/0x598E44`，补回三类被正常调用掩盖的源码结构/边界：
  惰性 Variant 转换恢复为以 `PSBValueDispatch *this` 读取 `this->value.owner` 的成员方法，
  不再由 caller 抽取并显式传 owner；PropGet/PropGetByNum 的 count、成功转换和非 throwing
  miss 全部恢复对 result 的无条件写入/清空，删除本地额外的 null-output 安全 no-op；strict
  dictionary miss 在异常 helper 意外返回时恢复 `{null,null}` 输出，不再构造
  `{owner,null-node}` 并额外 AddRef。Enum callback result 忽略、closure ObjThis 选择、
  `TJS_ENUM_NO_VALUE` argc 与四只 Variant 析构顺序经逐指令复核无需修改。Mac 四目标构建、
  `psbfile-dll` **484/484**、`motionplayer-dll` **398/398** 与 Web Debug 最终链接全部
  通过。ADB 列表仍为空，边界 oracle 本轮未执行且未伪造崩溃 fixture。

- 2026-07-19 再次 fresh decompile `PSBMedia::Resolve@0x59A4B0`、strict getter
  `sub_598C58@0x598C58` 并逐指令复核 `0x59A694..0x59A704`，纠正上一阶段写反的生命周期
  结论：循环内的可观察净效果是先释放 current owner，再安装 strict getter 的 sret
  owner/node；新 owner 非空时读取观测 refcount，零值直接析构并保留 destination 悬挂
  边界。只有循环结束写回调用者 out 的 `0x59A730..0x59A774` 才明确执行
  Release-old→copy→AddRef→write node。本地删除了人为强制的 `const child` 作用域，采用
  能产生同一净序列的直接赋值；2026-07-22 复核确认“这必然是 prvalue move assignment”
  仍是过度推断，copy + 临时析构经优化也可生成同形指令。该阶段
  仍错误地把 caller out 本身当作循环 current，未真正复原“仅成功尾块写回 out”。这一遗漏
  已由后续 fresh audit 纠正。Mac 四目标构建、`psbfile-dll` **484/484**、
  `motionplayer-dll` **398/398** 与 Web Debug 最终链接均通过；冷启动
  `oracle-arm64-31` 并恢复 frida-server 后，已提交 `ezsave.pimg` 的 Android octet/storage
  两条 oracle 均为 `status=ok`，owner size/refcount、inline header、magic 与 strict refresh
  全部吻合。该正常资产 oracle 是非回归守护，不替代 zero-ref 边界的逐指令证据。

- 2026-07-19 fresh decompile/disasm `PSBFile::LoadStorage@0x598538` 并检查异常落点
  `0x5986D0..0x5986E8`，确认 stream 建立后的所有异常清理都只调用 stream 虚析构；
  `0x5985CC` 分配的输入 buffer 及 MDF 分支 `0x598674` 分配的 decoded buffer 均没有
  RAII cleanup。本地此前用两只 `unique_ptr<uint8_t[]>`，会在 `ReadBuffer` 抛出、decoded
  allocation 抛出或 Adopt/filter 抛出时额外释放 Android 泄漏的 buffer。现已恢复裸指针
  数据流：MDF 失败只删除 decoded，成功先删除 source 再接管 decoded，正常 Adopt 失败及
  异常继续保留原始泄漏边界；stream 的 RAII 保持不变，因为它精确对应 landing pad。
  仓库没有会抛异常的现成 stream/fixture，按规则未人为构造物料。Mac 四目标构建、
  `psbfile-dll` **484/484**、`motionplayer-dll` **398/398** 与 Web Debug 最终链接均通过；
  `ezsave.pimg` 的 Android octet/storage oracle 两条均为 `status=ok`，继续作为正常路径
  非回归守护。

- 2026-07-19 fresh decompile/disasm `PSBMedia::Resolve@0x59A4B0` 的完整 owner/out 链，
  纠正上述遗漏：`0x59A548..0x59A55C` 把 root 只写入局部 current，循环
  `0x59A698..0x59A704` 也只净更新该局部（源码 move/copy 形状不可辨识）；无首个 slash、任一 contains miss 或其他
  非成功退出都不写 caller out。只有最后 segment 成功后的 `0x59A730..0x59A774` 才对
  caller out 执行 Release-old→copy current owner→AddRef→write node。本地此前在函数入口
  `value = root` 并直接沿 value 遍历，会让失败调用泄漏部分解析状态到 out；现已恢复独立
  `current` owner，并把 `value = current` 收束到成功尾块。现有 142 个 MDF 资产也完成
  只读普查：zlib 失败 **0**、解压后 header offset 失败 **0**，因此仍无可合法复用的天然
  失败 fixture，未构造或篡改物料。Mac 四目标构建、`psbfile-dll` **484/484**、
  `motionplayer-dll` **398/398** 与 Web Debug 最终链接均通过；`ezsave.pimg` 的 Android
  octet/storage oracle 两条继续为 `status=ok`，作为成功路径非回归守护。

- 2026-07-19 继续 fresh decompile/disasm `PSBMedia::GetListAt@0x5999F4`，确认
  `0x599A4C..0x599A70` 在 media 方法内部直接读取 raw `node[0]` 并执行完整 tag switch；
  它没有调用独立的 type-category helper `sub_599554@0x599554`。本地此前先调用
  `GetTypeCategory()` 再按 category 6/7 分流，正常 array/dictionary 输出虽等价，却增加了
  Android 调用链中不存在的一层分类调用。现已恢复本函数自有的 raw tag switch：`0x20`
  保留 signed 32-bit array 循环，`0x21` 保留 unsigned dictionary 循环及跨迭代复用的
  `std::string`，全部已知非容器 tag 原样 no-op，未知 tag 继续抛精确 internal-error 文本。
  Mac 目标对象的 undefined-symbol 表已确认 `PSBMedia.cpp.o` 不再引用
  `PSBRawNode::GetTypeCategory`；四目标构建成功，`psbfile-dll` **484/484**、
  `motionplayer-dll` **398/398**，Web Debug 最终链接成功。在线 AVD 的
  `ezsave.pimg` Android octet/storage oracle 两条均为 `status=ok`。

- 2026-07-19 fresh decompile/disasm dispatch 四入口
  `EnumMembers@0x596F50`、`GetCount@0x5975E0`、`PropGetByNum@0x5976C4`、
  `PropGet@0x597854`，确认它们同样各自在函数体读取 raw `node[0]` 并展开完整 tag
  switch，而不是调用 `GetTypeCategory@0x599554`。对 `0x599554` 的全 code-xref 复核得到
  唯一四个真实调用点：`Motion_ObjSource_width_getter@0x69D19C`、height getter
  `0x69D27C`、clip getter `0x69D35C`、drawLayer `0x69D6D8`；dispatch/media 均不在其中。
  本地四个 dispatch 入口现已恢复自己的 raw-tag 分派，array/dictionary 逻辑及已知 tag
  返回码保持不变。另一个正常标量路径上的生命周期偏差也已纠正：Android Enum 在完成
  tag 分类后先构造 name/memberFlags/memberValue/callbackResult 四只 Variant，随后才判断
  category 6/7；non-container 因而仍经历四只 Variant 的逆序析构。本地旧 early-return
  位于构造之前，现已移到四只 owner 建立之后。上述 xref 同时保留了 `0x599554` helper，
  不能因插件内调用清零而删除；四个 ObjSource consumer 的 raw-node owner 形状已由下一条
  独立审计闭合，不以一次负搜索判断其实现状态。Mac 目标对象的 undefined-symbol
  表已确认 `main.cpp.o` 不再引用 `PSBRawNode::GetTypeCategory`；四目标构建成功，
  `psbfile-dll` **484/484**、`motionplayer-dll` **398/398**，Web Debug 最终链接成功。
  在线 AVD 的 `ezsave.pimg` Android octet/storage oracle 两条均为 `status=ok`。

- 2026-07-19 fresh decompile/disasm `ResourceManager_findSource@0x6AAB3C`、
  ObjSource 默认构造失败清理 `0x6E3EFC`、析构 `0x6E407C`、origin/size/clip 访问器
  `0x69D014/0x69D0D8/0x69D19C/0x69D27C/0x69D35C`、纹理物化 `0x6DA454`、
  `drawLayer@0x69D6D8` 与 Layer 严格转换 `0xA7A050`，证伪了本地/本文此前的
  “ObjSource 内含一只 tTJSVariant dict facade”结论。`0x6AAFC0..0x6AAFDC` 分配 0x18
  字节，复制 icon raw owner/node pair、对 owner `++refcount`，并把第三只 qword 置零；
  `0x6E407C` 先调用 texture vtable+16 Release，再递减 raw owner，zero-ref 时执行
  `sub_598B3C+operator delete`。本地 ObjSource 现恢复 `PSBRawNode + texture*` 字段及相同
  析构顺序，且不再添加 Android 中不存在的 texture 槽清零。`0x6AAFE0..0x6AB044` 以
  `sticky=false/err=false` 创建 adaptor，null 返回分支只写 void、不 delete 新 ObjSource；
  本地也保留该失败泄漏边界。RM 从 mapped `PSBFile::GetRoot()` 直接执行 fixed-key strict / dynamic-key
  has+strict 导航，不再制造四层 TJS dispatch。访问器恢复 strict miss/非-dict 32/clip
  try-gate 边界；`0x6DA454` 的重复 pal gate、RL 元素宽度、aligned allocation、palette
  expand、pitch-aware `tTVPBitmap` copy 与 CreateTexture2D 链也已逐步复刻；drawLayer 改用
  texture 自身 `GetWidth/GetHeight`，不再重新读取 PSB 尺寸。Mac 对象符号表确认
  `SourceCache.cpp.o` 的 ObjSource 实现只引用 `PSBRawNode` 导航/转换/析构符号，不再引用
  PSB TJS dispatch；Mac 四目标构建、`psbfile-dll` **484/484**、`motionplayer-dll`
  **398/398** 与 Web Debug 最终链接全部通过。motionplayer fixture 实际进入 `tTVPBitmap`
  分配路径；在线 AVD 的 `ezsave.pimg` Android octet/storage oracle 两条均为 `status=ok`。

- 2026-07-19 对 PSBFile.dll 业务/NCB实现 `0x59641C..0x59B708` 重新做区间全枚举；当时
  先得到 108 个 IDA function，2026-07-22 进一步拆出被错误并入 `0x59A4B0/0x59AEEC`
  的 `0x59A8D8/0x59A968/0x59B14C`，得到 **111** 个业务/NCB函数。2026-07-23
  再确认 `0x598FFC` 经 PLT 调到 `0x59B7E8`，加入 1 个本源码触发的 vector 扩容慢路径，
  当前相关函数总数为 **112**。早先把 `0x59AA84` 当终点
  只覆盖 90 个入口的边界已纠正；最终 F 组共有 22 个 typed NCB 入口，相较旧 90-entry
  边界净新增 21 个；这些入口全部属于自动注册/反注册、
  holder/析构/成员/属性/方法/参数转换包装，并由
  本地 `NCB_REGISTER_CLASS(PSBFile) + Factory/Property/Method` 同一模板生成。逐一将入口地址与生产源码/本文地址证据反查后，未单列的入口均属于
  iTJSDispatch2 一指令 `TJS_E_NOTIMPL` 槽、析构/Release 包装、NCB typed-class state
  helper 或 `std::vector<std::string>::reserve` 实例化；没有发现遗漏的业务方法。随后 fresh
  decompile raw loader/owner `0x598268/0x598538/0x598708/0x598960/0x598AAC`、raw node
  `0x598B58/0x598C58/0x598D58/0x598E44/0x598E64/0x5992E8/0x599438/0x599554/
  0x5995D8/0x5996E4`、dispatch `0x59673C/0x596E24/0x596F50/0x5975E0/
  0x5976C4/0x597854`、media `0x5998C4..0x59A4B0` 及 typed registration
  `0x597E98..0x5980F4/0x59A8D8..0x59B708`，当前实现的分支、输出覆盖时机、AddRef/Release 顺序、
  invalid-object/member-miss 错误码与 callback 参数数均未发现新偏差。此次复核同时纠正了
  `.claude/agent-memory/ida-deep-analyzer/project_m9_source_subsystem.md` 中仍把 ObjSource
  写成 `tTJSVariant` facade、把 KRKR/raw pixel 标为 open 的过期结论。

- 同轮 fresh decompile typed NCB 自动注册与 tail `0x59A8D8/0x59A968/0x59AA84/
  0x59ABD8/0x59AC04/0x59AC0C/0x59AC7C/0x59AD08/0x59AD84/0x59AEE4/
  0x59AEEC/0x59B14C/0x59B268/0x59B28C/
  0x59B378/0x59B460/0x59B484/0x59B48C/0x59B570/0x59B6DC/0x59B700/
  0x59B708`，确认 native holder 的 0x18-byte `{vptr,PSBFile*,sticky/no-delete}` 状态、
  owner teardown、三种析构、Already registered/No Global Dispatch/Multiple constructors
  异常边界、root getter/RO setter、load argc>=1 门控及首参 Variant 多次按值复制均由
  ncbind 模板生成；本地注册声明产生同一层次，没有手写简化。另 fresh decompile
  `EnumMembers@0x596F50` 完成接口名确认，IDB 已从
  `PSBValueDispatch_EnumMembers_guess` 改为 `PSBValueDispatch_EnumMembers` 并保存。

- 2026-07-19 fresh decompile `PSBMedia::Open@0x59993C`、
  `tTVPMemoryStream` block ctor `0x8F7C74`、complete destructor `0x8F7D04`
  与 deleting destructor `0x8F7D68`，闭合了 `psb:`
  resource stream 的跨对象生命周期：Open 从当前 cached PSBFile 取 raw resource pointer
  与 size 后直接交给 0x20-byte memory stream；非空 block 不复制，ctor 置
  `Reference=true`，也不对 raw owner/PSBFile 做 AddRef。析构仅在 `block != null &&
  Reference == false` 时释放，因此借用流不会释放 resource。若调用方仍持有旧 stream，
  而同一全局 PSBMedia 因另一个 container 名替换 `_file`，旧 block 可以悬空；这是 Android
  原版的可观察边界，不应通过 stream 持有 owner 或复制 buffer “安全化”。本地
  `PSBMedia::Open → new tTVPMemoryStream(data,size)`、通用 memory-stream ctor/dtor 与上述
  存储、标志和释放条件逐项一致。

- 同日对当前 `reference/` 与 `tests/` 中的天然二进制资产做只读盘点：共识别 142 个
  lower-case `mdf\0` wrapper，全部 zlib 解压成功且实际长度等于 wrapper 声明长度；连同
  raw PSB 共识别 222 个解包后 `PSB\0` 文件，全部满足 `sub_598960` 的八项 header-offset
  比较。因此当前工作区确实没有 MDF zlib-failure 或 filter 后 offset-failure 的天然样本；
  这不是从一次扩展名负搜索得出的结论，而是逐文件 signature、zlib 与 header 数值检查。
  按“不从零制造 fixture”约束，两项损坏输入 Android runtime 边界继续如实保留为验证缺口。

- 2026-07-22 fresh decompile/disasm `PSBFile::Load@0x598268`、
  `LoadStorage@0x598538`、owner 析构 `0x598B3C` 及通用分配器
  `TJSAlignedAlloc@0xA0DE48`/`TJSAlignedDealloc@0xA0DE90`，发现本地 raw PSB buffer
  一直错误使用普通 `new uint8_t[]`。Android 的四个分配点
  `0x5982D4/0x5983E8/0x5985C8/0x598674` 均传 `align_bits=4`，返回 16-byte aligned
  指针并在其前一 pointer slot 保存原始 base；owner 析构及 storage MDF 成功替换 source
  分别在 `0x598B48/0x5986B0` 走 `TJSAlignedDealloc`。本地已恢复相同 allocator family。
  同轮逐指令确认三个失败清理点 `0x598328/0x59840C/0x59869C` 却故意直接调用
  `operator delete[]`，并非 aligned dealloc；本地保留这组混合释放，未以“更安全”为由
  改写。storage read/Adopt/filter 异常的 raw buffer 泄漏边界也保持不变。

- 同轮 fresh decompile/disasm `PropGetByNum@0x5976C4` 的
  `0x597800..0x597848`：原版把 count-table 尾部和 element offset 的完整相对地址留在
  `W9` 中，所有加减乘按 32 位回绕，最终以 `ADD X2, X8, W9, SXTW` 加到 packed-array
  基址。本地旧实现 `end + uint32_t offset` 在 64-bit host 上会零扩展高位损坏 offset；现已
  显式形成 `uint32_t relativeOffset`，再转 `int32_t` 做符号扩展。正常 PSB 的低位 offset
  不变，损坏输入的高位边界恢复为 Android 行为。没有把这一点推广到 EnumMembers 或
  dictionary 路径，因为那些站点须各自以指令证据判定 UXTW/SXTW。

- 随后的独立边界审计及 fresh decompile/disasm `EnumMembers@0x596F50` 又确认 dictionary
  value 路径 `0x597388..0x59739C` 的另一种 32-bit 规则：Android 先在 `W8` 中把
  `offsets.end-(node+1)` 与 entry offset 相加并回绕，再以零扩展结果从 `node+1` 加址。
  本地旧 `offsets.end + offsets[index]` 在 64-bit host 上不会丢弃这次相加产生的 bit 32。
  现已显式先形成 `uint32_t relativeOffset`，再从 `node+1` 加址；array 分支保持原有
  host base + UXTW entry offset，不作泛化修改。

- 上述三项修改及后续生命周期收口后，Mac `psbfile-dll` **554/554**、
  `motionplayer-dll` **1197/1197** 通过，Mac 目标、Wasmtime 默认 Debug build、显式
  `krkr2_wasmtime_guest` 目标与 Web Debug 最终链接成功，`git diff --check` 通过。当前
  112-entry manifest 仍未发现漏写的 Android 业务入口或源码触发的容器慢路径；尚不能唯一证明的是
  `Transfer_guess@0x598A64`、若干 zero-xref helper、`PackedArrayView_guess` 及
  `ReadPackedCount_guess` 等 inline helper 的原始名字、成员身份和源码拼写；但上述三个
  已审计调用面中的本地产物额外调用边界已经消除。ARM64 对象字段偏移在这些判断中只作为反编译定位坐标；本地
  复刻字段语义、类型、顺序、容器和生命周期，不硬凑 ARM64 ABI 字节偏移。PSB 文件内的
  serialized offset 则是数据格式契约，仍必须逐位复刻。

- 2026-07-22 对所有 packed-table 消费点独立核查 W32/SXTW/UXTW：
  `FindNameIndex@0x596478..0x596480`、`FindDictionaryValueOffset@0x596608..0x59667C`、
  `DecodeName@0x597BA0..0x597C10` 及 `EnumMembers@0x597114..0x597308` 都先在 W
  寄存器内把 header displacement 与 `count*width` 合并回绕，再从原始 table begin 做
  UXTW 加址。本地 `PackedArrayView_guess::end = values + count*width` 在 64-bit host 上漏掉
  整体回绕，现改为 `begin + uint32_t(header-10+count*width)`。同轮确认
  `PropGetByNum@0x597810..0x597814` 的 entry 索引是 W32 product 后 UXTW；本地已从 signed
  host 乘法改成显式 `uint32_t entryOffset`，而最终 node relative 继续保持既有 SXTW。

- 同轮用现有 `ezsave.pimg` 补齐 typed NCB `load/root`、只读 setter、dictionary/array
  `EnumMembers`（含 packed 顺序与 `TJS_ENUM_NO_VALUE`）、Octet、NativeInstanceSupport 与
  Invalidate 状态机；复用原测试已加载的加密 motion PSB 覆盖 `assign@0x59673C` 的
  float→Real 分支 `metadata/bustControl/0/friction == 0.125`，没有创建或篡改 fixture。
  Mac `psbfile-dll` 随后补入空 path segment miss 与缺失 container 抛异常后保留旧缓存，
  当前为 **554 assertions / 8 cases**。

- source-shape 复核还限定了证据强度：`Transfer_guess@0x598A64` 的 hidden-sret 消费行为、
  `Resolve@0x59A698..0x59A6EC` 的净 owner 生命周期及 `sub_598D58@0x598D58` 的 hit out
  更新顺序可证；helper 原名/member 身份、Resolve 的 move-vs-copy+temporary、显式 special
  members/self guards、`sub_597AD4` 的两裸参数-vs-零开销 raw-node holder 参数、
  `GetInt@0x599438` 的 `tjs_int`-vs-`tjs_int64` 返回型、`PackedArrayView_guess` 是否真实源类型，
  以及 `memcpy`/unaligned cast 写法均不可辨识。代码与本文已删除把其中任一等价形状冒充
  唯一源码事实的表述。

- 2026-07-22 的最终 local→Android 与生命周期复核又修正四组细节。①
  `LoadStorage@0x598570..0x59858C` 的 placed-path `ttstr` 在 `TVPCreateStream` 返回后立即
  析构，本地不再把它保活到函数退出；MDF 成功路径 `0x5986A4..0x5986B4` 先写实际长度，
  仅在 decode pointer 非空时释放/替换 source。② `GetDictionaryKeys@0x598EF8` 的 reusable
  `std::string` 只在 dictionary gate 后构造；`ContainsDictionaryKey@0x5995F8` 的空 raw-node
  临时量则在 tag switch 前构造，所有出口共享同一析构层。③
  `EnsureContainer@0x599F20..0x599FCC` 恢复
  `SetObject(AddRef×2) → Release factory 初始引用 → CopyRef 到 _file →
  析构局部 Variant`，null adaptor 也以 void 局部 Variant 走同一 CopyRef；
  `Resolve@0x59A56C` / `0x59A5A4` 只把 `IndexOf('/') == -1` 视为 miss/last，
  未错误泛化为任意负值。
  `GetLocallyAccessibleName@0x599DD8` 本地改用最小 `Clear()` 形状，但二进制不能唯一排除
  被优化成同一指令的空 `ttstr` 赋值。④ `NativeInstanceSupport@0x596D90` 的 lazy
  `PSBValueClass` id 状态收回函数体，删除本地产物额外暴露的
  `GetPSBValueClassID()` 外部边界；是否曾有 inline helper 仍属不可辨识源码拼写。

- 同轮函数序言复扫拆开 `0x59A8D8`（auto-register Regist）、`0x59A968`
  （Unregist）和 `0x59B14C`（raw factory FuncCall），随后又沿 PLT 补回
  `0x59B7E8` 的 vector 扩容慢路径，并把错并在其后的 `0x59B9C8`
  （`PackinOne.dll` callback）拆成独立函数；112-entry manifest 已闭合，且所有
  `SUB SP` / pre-index `STP/STR` 序言均成为 function start。IDB 已追加 vtable xref/边界
  注释并保存；adaptor 第三字段的旧文档语义也由错误的 `constructed` 纠正为
  `_sticky/no-delete`。Mac `psbfile-dll` **554/554**、`motionplayer-dll` **1197/1197**、
  Wasmtime 默认 Debug build、显式 `krkr2_wasmtime_guest` 目标与 Web Debug 最终链接通过，
  `git diff --check` 通过。默认 build 不包含额外 guest 测试目标；单独重建该目标时也已
  成功，harness 的 `clipRect` 参数类型现与生产字段同为 `array<float,4>`。

- 2026-07-23 最终 SourceCache/Player 生命周期复核补齐 byte-budget trim、同色命中不
  重排、三份 RM dispatch owner 的 ctor/dtor 相对顺序，以及完整 `global.kag` Variant /
  20 MiB 构造链后，Web Debug 32 个受影响
  目标完成并链接 `index.html`；Mac Release `motionplayer-dll`/`psbfile-dll` 完成链接，
  `psbfile-dll` **575/575 assertions（10 cases）**、`motionplayer-dll`
  **1214/1214 assertions（16 cases）** 全绿。既有 port-wasm RL driver 的 8 个 case 亦
  全绿；它不是 Android oracle，不能替代 cache 淘汰边界的运行时差分。

## 后续闭合条件

要对“psbfile 插件自身”给出 100% 结论，至少还需要：

1. 使用现有天然损坏资产覆盖 MDF zlib 失败及 filter 后 offset 校验失败；MDF 成功路径已
   用本机现有 `.ks.scn` 在 Android oracle 验证，但仓库没有已提交 MDF fixture；media/TJS
   注册路径已由现有 PIMG 闭合。2026-07-19 曾对当时外部可用的 142 个 MDF 与 222 个
   解包后 PSB 做只读普查，未发现天然失败样本；它们不在当前 checkout 中，当前
   `reference/` 为空且 tracked test asset 只有两只 PSB/PIMG，不能把历史外部资产数写成
   当前仓库物料。没有现成物料时记录验证缺口，不从零伪造 fixture。
2. packed-table 的 tag/width/stride 分支已逐项反编译；剩余工作是用现有损坏资产或
   Android oracle 核对真实越界/崩溃表现，包括损坏 table、tag `0x0B` 的 7-byte
   zero-extension quirk 与 >4 GiB storage 截断边界；不能为了测试主动构造新 fixture。
3. media 已覆盖 array listing、空段 miss、同 container miss、缺失 container 异常与旧缓存
   保留。此前“只有一只可加载 container”的断言已被现有加密 motion PSB 证伪；当前测试已
   覆盖 `ezsave → motion → ezsave` 的成功跨-container replacement，以及旧 stream 在 owner
   replacement 后自身 metadata/析构仍可用的本地守护。stream 不保活 owner、block 为 borrowed
   仍由 ctor/dtor 反编译证据证明，测试不读取悬挂 block。仍未由天然可达节点覆盖的是
   dictionary media listing 与 CreateAdaptor-null。cross-container Android runner 已实现，
   但本轮没有连接设备，真实 libkrkr2.so 执行结果仍待取得；不能用离线协议模拟替代。
4. 优化后二进制尚不能唯一恢复若干源码拼写：`sub_597AD4` 是两裸参数还是零开销 raw-node
   holder 参数、`GetInt@0x599438` 返回 `tjs_int` 还是 `tjs_int64`、PSBFile/raw-node 的显式
   special members 与 self guards、若干 inline helper 的原名/member 身份、
   `PackedArrayView_guess` 是否为真实源类型，以及 unaligned read 的具体写法。若要把“尽可能”
   提升为字面 100%，需要带符号/未优化构建、调试类型或原始源码等新增证据，不能从当前
   ARM64 优化产物反向唯一指定其中一种等价源码形状。

若把范围扩展到“当前 Web 端到端运行链”，还需继续审计
`0x6C2334/0x6C4E28/0x6C7440` 的 render-list/build/execute 全链；这不属于 psbfile
插件自身的闭合条件，不能与上面四项混作同一结论。
