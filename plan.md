# Motionplayer 四参考二进制恢复计划

当前恢复目标以 `reference/binaries/` 中 Android arm64、Android armv7、iOS arm64、
iOS armv7 四个参考目标为唯一原生实现证据。旧 `libkrkr2.so` 地址、由它推导出的
`Like_0x...` 名称和旧对象布局说明均不得继续作为当前结论。

## 已闭合纵切面

- V288：闭合 `-contfreq` generation-sticky 读取、limit-thread ctor/Execute/SetEnabled/End/priority-100
  shutdown、零/负/interval-zero 边界、SystemControl bool gate/pending/EventEnable/maintenance pump；从四端
  add/remove xref 穷举 compressed texture 外全部 raw owner，恢复 Layer transition、layerExMovie、
  MoviePlayerLayer 的 embedded callback vtable/thunk/body、析构移除、Start 异常非回滚、同步
  remove+re-add、duplicate Play/Stop不移除与同轮 live append。源码执行语义保持不变，只补四端证据注释。
  四端 packed canonical 均完成 candidate/canonical 冷读、发布后 SHA-256 校验、零 session 与零 loose
  sidecar；加载 emsdk 后 Web Debug 31/31 构建及固定五产物通过。证据见
  `analysis/motionplayer_contfreq_systemcontrol_raw_hook_registrants_four_binary_2026-08-22.md`。

- V287：闭合 continuous-event 的 pump pending gate、raw hook与closure双vector、Begin-before-growth、
  manual AddRef/Release、priority-10 logical destroy/backing-storage split、live size/base重读、同轮append/
  reallocation、自移除墓碑、exclusive提前返回、event-disabled分层与四种EH；并把compressed software
  texture的secondary-base hook、life 3刷新/同轮3→2、zero→-1和析构remove时序接回完整调用链。四库
  candidate与canonical均完成packed cold-read，发布后hash逐字节一致且最终零session/零loose-sidecar；
  EventIntf和SystemControl默认WCHAIN诊断已隔离为Emscripten显式编译期开关，默认/开启两条编译路径及
  最终Web no-work build均通过，避免JS URL query/stack/log counter污染参考路径。证据见
  `analysis/motionplayer_continuous_event_hook_handler_delivery_lifecycle_four_binary_2026-08-22.md`。

- V286：`tTVPOGLTexture2D::RestoreNormalSize` 的 strict max gate、min-32 POT RGBA allocation、两个
  guarded function-local static、virtual `ApplyVertex(rect)`、raw new GL name/FBO状态、draw后
  metric/delete/publish/scale commit、两个唯一 caller与四种 EH encoding已四端闭合。纠正 V282–V284
  “Restore自己做 PixelData/readback”的过时归属：existing PixelData绕过 Restore，readback只在
  `GetScanLineForRead`递归caller；另保留 Android armv7独有 scaleW-only normal gate。四库写回18
  function/8完整 UTF-16 item/31 comment，candidate与canonical两轮18/18 cold-read、Web build通过。
  证据见 `analysis/motionplayer_ogl_restore_normal_size_state_commit_eh_four_binary_2026-08-22.md`。

- V285：从 `TVPWindowLayer` create/init沿 `window -> PrimaryLayerArea -> DrawSprite` children retain链，
  闭合 remove/onExit/cleanup/parent-null/release/compact、derived/base destructor和 Sprite adapter owner
  cascade；同时恢复 PoolManager raw-pointer栈、AutoreleasePool ctor/dtor/clear/pop、普通clear重入保留与
  dying-pool重入 debt丢失/identity-blind pop边界。证明 `removeFromParent`不等于无条件即时delete，
  null remove仍为崩溃/UB。证据见
  `analysis/motionplayer_window_scenegraph_nested_pool_teardown_four_binary_2026-08-22.md`。

- V284：闭合 `TVPWindowLayer::UpdateDrawBuffer -> GetAdapterTexture -> Sprite::setTexture -> Cocos
  AutoreleasePool` 全链，恢复new adapter creator debt、Sprite先retain新/后release旧、pool clear detach与
  reentrant append边界；证明同primary尺寸、不同target owner的正常窗口路径可命中same-size reuse，
  只换GL name而不重绑owner，形成name/owner分离与stale-handle窗口。证据见
  `analysis/motionplayer_window_ogl_adapter_sprite_autorelease_lifetime_four_binary_2026-08-22.md`。

- V283：闭合 OGL split `AsSingleTexture` 的cache GL-name清空、固定四通道resize、Bitmap早release/null、
  replacement name晚发布、raw tmp异常泄漏、完全不计入VMem和converted split最终析构null dereference；
  同时复核 base/static/mutable constructor的InternalInit commit/EH，纠正“空TVPCheckMemory可抛”的旧
  因果，保留manual-init尺寸未初始化窗口。证据见
  `analysis/motionplayer_ogl_split_as_single_ctor_commit_eh_four_binary_2026-08-22.md`。

- V282：从 deferred texture deleting slot穷举所有built-in software/OGL concrete vtable，恢复software
  bitmap/compress/half/lz4 owner、continuous-hook raw secondary pointer、OGL base/static/mutable/split
  destructor与nested AdapterTexture2D反向owner；证明built-in dtor不直接Release另一texture、same-size
  adapter只换name不换owner，并删除portable diagnostics中的旧单目标绝对地址。证据见
  `analysis/motionplayer_concrete_texture_software_ogl_adapter_lifetime_four_binary_2026-08-22.md`。

- V281：从 manager `delete DrawBuffer`闭合 `tTVPDestTexture -> tTVPNativeBaseBitmap` 构造/析构、
  Bitmap/PrerenderedFont/CachedText/Font.Face owner顺序，以及 `iTVPTexture2D::Release` count==1不归零、
  raw deferred vector growth、snapshot recycle、duplicate/reentry/clear/static-exit边界；保留Bitmap构造期
  有意未初始化和析构异常terminate语义。证据见
  `analysis/motionplayer_dest_texture_native_bitmap_deferred_release_lifetime_four_binary_2026-08-22.md`。

- V280：`tTVPLayerManager::DetachPrimary` 的 focus/capture/touch/mouse/modal/tree 清理链、
  delayed live `Primary` snapshot、phase-B fixed argument与final live-slot clear、`SetFocusTo` raw focus
  publication和callback后 Owner平衡、touch/modal live-vector erase/reentry、clear-before-callback、
  无transaction rollback及四种 EH encoding；并恢复 Android armv7 先前漏标的两个 out-of-line
  catch/rethrow landing。当前 `LayerManager.cpp` 语义保持不变，只补四端证据注释；四库写回45
  comment/43 rename/2 function definition并完成36次canonical fresh readback，armv7经prebackup、独立
  candidate验证/发布、canonical save及final packed readback闭合。双syntax、Web/Wasmtime/guest build、
  三目标no-work、CTest无注册测试、三Wasm validate/Module、Wasmtime object定向反汇编、hash/section/
  diff/stale/零session/process审计均通过；相对V279总大小`+0/+0/-14`。证据见
  `analysis/motionplayer_layer_manager_detach_primary_reentry_container_eh_four_binary_2026-08-22.md`。

- V279：`tTVPLayerManager` 构造布局、两个故意未初始化 marker、非原子 `AddRef/Release`、
  final release 不写零、`DrawBuffer`/三个 vector 的析构所有权、borrowed `DrawDeviceData`、
  BaseLayer `new -> publish -> attach -> register` 与
  `detach -> unregister -> release -> clear` owner 边界；四端 EH 差异、canonical IDB 写回与
  cold readback 见
  `analysis/motionplayer_layer_manager_release_destructor_publication_owner_boundary_four_binary_2026-08-22.md`。

- `SeparateLayerAdaptor` 的创建、析构、owner/target Variant 与 private target 生命周期；
- `__Private_Motion_GLLayer` native class 注册、继承的 `Construct`、derived ctor/dtor、
  脚本 callback、命令 deque 与 RenderItem owning texture 生命周期；
- `PrivateMotionGLL::Draw_GPU` 的目标/reference 双纹理数据流、render-method selector、
  stencil 状态机、float-to-double 点转换、affine 提交以及合法 mesh payload 的合批提交；
- Player direct texture 与 `D3DAdaptor` 共享 raw renderer 的 callable 数据流、target/reference
  选择、跨 item triangle batch、state-change flush、正常/异常退出与共享 selector/stencil cache。
- Player 与 PrivateMotionGLL 共用 mesh backend 的 source 越界 software repeat、GPU 日志边界、
  控制点整体快路径、逐 point/cell clip pruning、并行 `vector<PointD>`、六点绕序、一次性
  `std::function` callback、source texture 引用与异常清理。
- 被四端各 71 个调用点共享的 TJS Array/Items helper：fresh Array dispatch 的 factory owner、
  `(dispatch,dispatch)` 双 closure AddRef、factory Release、精确 `TJS_S_OK` native-instance
  gate、64/32 位 `tTJSArrayNI::Items` 的 `+16/+8` 布局、返回 struct 的 `+24/+12` borrowed
  pointer，以及非零 status 保留 owning Array 但发布 null Items、无 graceful-null factory
  fallback 和四种 unwind 编码；删除旧 `sub_704CB8 @ 0x704CB8` 单目标注释。
- `EmoteEngine` HM6 `ttstr -> {int32 type,index}` 与 HM7 `ttstr -> double` 的四端成员偏移、
  56/28/40/20B map header、old-libstdc++ 初始 11 buckets 与 libc++ lazy buckets、四种
  node layout、Hint hash/equality、zero-init operator[]、duplicate overwrite、不重链 hit、
  bucket-dependent iteration、HM6 clear 保留 bucket storage、HM7 跨 metadata reset 存活和
  HM7 -> HM6 逆析构；纠正旧 `0x686944` upsert 误标和统一 32B node 注释。
- `EmoteEngine` HM4 instant-variable `unordered_set<ttstr>` 的四端成员偏移、
  56/28/40/20B set header、24/12/24/12B ABI-specific node、old-libstdc++ 预建 11 桶与
  duplicate-before-lookup candidate allocation、libc++ lazy 0->2 bucket 与 lookup-before-allocation、
  Hint/node 双 hash、bucket-predecessor/global-chain、clear 保留 bucket allocation、final dtor、
  builder union/no-erase 及 Track instant flag 构建时快照；删除三处旧单目标 helper 注释。
- `EmoteEngine` HM1/HM2 mirror positive/negative cache 与相邻 pattern vector：四端偏移、
  构造/clear/dtor 顺序、专用 `variableMatchList` hint、first-`IndexOf>=1`（prefix 会屏蔽后续
  occurrence）、cache stale/gate toggle、const-lvalue lookup-before-allocation insert、与 Android
  HM4 rvalue candidate-before-lookup 的 value-category 差异，以及异常部分提交/无 erase 边界。
- 插件级 Bezier basis 静态 map cache、逐运算多项式结合顺序、固定 16 控制点 patch
  tessellator、row-major 输出，以及零/负 division 的 NaN、异常和异常后缓存状态。
- motionplayer 的 per-Layer attached instance 布局/懒生命周期、两个 debug appearance Variant、
  普通 mesh frame 与 Bezier patch frame/mesh-frame 的 TJS Array/deque 构造、inclusive 线框
  调用、固定 32 坐标 parser，以及三点 `drawBeziers` + 两端 reverse 的历史边界行为。
- Layer attached class 的完整九成员注册顺序、四个 copy/operate typed wrapper、face/type cache、
  auto mode、mode/face 到 bitmap method 映射、整层 clear、source-only-Layer 边界、共享 mesh/
  Bezier render、full-clip update 与两类 debug overlay。
- 独立 stateless `BezierPatch`→`Layer` attach class 的八方法注册、unsigned flat-array 双读取协议、
  三个仿射/平移变换、两个 bounds Dictionary、固定 16 点正向求值、10×10 逆三角映射，以及
  奇数尾补零、负 count 巨大循环域、空 bounds sentinel、`calcMeshBounds` 重复 `left` 写入、
  ordered NaN gate、退化 cell 继续和正向 `tTVPPointD +=` 未初始化的原版源码 UB；同时闭合
  unit quad→basis map→default points→binding state 的静态初始化与逆析构顺序、四种 STL
  container teardown，并修复 Android arm64 三个误合并 callback 的 IDB 边界。
- `EmoteEngine` 的 Player 与七个 direct-controller 单指针 `unique_ptr` owner、正常析构的
  libstdc++/libc++ slot 清零差异，以及构造失败时的 pending allocation 与已初始化前缀回滚。
- `EmoteEngine` 完整 declaration-order constructor：十个 deque、四个空 vector、HM1–HM7、
  三个 Void Variant、Player/七 controller member initializer、wind/trigger/double defaults 与
  position→scale→angle→color seed 顺序；纠正旧“vector reserve(10)”误读为 Android 七个
  unordered 容器的 10→11 bucket policy，并恢复 trailing HM4–HM7 失败时 owner-prefix unwind
  及 idle controller tail 不提前初始化的边界。
- render helper 身份迁移：重新锁定四端 command builder、完整 canvas renderer 与完整
  accurate SeparateLayerAdaptor renderer 的函数边界和双分支调用图；证明旧
  `0x5CB08C/0x6C99B8/0x6C9CA8` 在当前 Android arm64 中分别属于 STL、transform-order 与
  camera-position 路径，而旧 `0x6C4E28/0x6C6B48/0x6C715C/0x6C7440` 只是两个完整 renderer
  内部位置。将 active `Like_0x...` helper、测试和 opt-in 诊断标签迁移为语义 `_guess` 名，
  同时明确它们是内联 block 的源码级抽取而非四端独立 native function。
- `EmoteEngine` deque #4 Eye、deque #5 Eyebrow 与 deque #6 Mouth element 各自的
  单指针 `unique_ptr` owner、字符串先于 controller 的逆成员析构、raw-pointer 目标
  emplace，以及 controller ctor 失败释放但 deque growth 失败泄漏的原版异常边界；
  Mouth 还闭合了双 label、双 HM6/HM7 数据流与 emplace 后部分初始化状态。
- `EmoteEyebrowController` 的无 vptr slim 布局、两条 ABI-specific deque、内嵌
  mesh resolver、state/value/beginFrame 偏移、Android eager 与 iOS lazy deque 构造差异、
  `beginFrame/edge/node` 唯一 metadata 输入、主/次轨道状态机与完整逆析构；2026-08-14
  新鲜复核四端 ctor 后删除 portable 源码中的旧单 A64 `sub_...` 构造伪码。
- `Motion.EmotePlayer` 四端 NCB 注册器的 2 常量 + 70 成员完整同序表，以及特殊入口的
  真实调用边界：`progress` 毫秒换帧 wrapper、`frameProgress` 直达同一完整 Engine core、
  `startWind` 五 `float` 直达 Engine、`stopWind` 仅 delete/null emitter 且保留缓存、
  `pass()` 零参数 timeline flush；纠正本地三条误路由和参数类型，删除该 surface 的旧
  单 A64 地址注释，并修复 Android arm64 progress core 被错误挂成双 wrapper remote tail
  chunk 的恢复 IDB 边界。详见
  `analysis/motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-14.md`。
- `Motion.D3DEmotePlayer` 四端注册器的 4 常量 + 54 成员完整交错顺序、每项 property/typed/
  raw descriptor family，以及 `module,clear,...,animating,skip,pass,progress,modified,...,contains`
  的真实构造发布序列；证明只有 variadic `load` 是 native-instance raw callback，其余九个
  本地手写 raw shim 均应由生成式 typed ncbind adapter 取代，并闭合 receiver→result clear→
  arity→native unwrap gate。删除旧 `sub_52E504`（当前落在 D3DEmoteModule registrar 内）和
  M11/旧进度地址注释。详见
  `analysis/motionplayer_d3d_emoteplayer_ncb_surface_four_binary_2026-08-14.md`。
- `Motion.D3DEmoteModule` 四端零参数 typed constructor、无常量、七成员精确顺序、
  0x20/0x1C 自然布局与默认值、十三个直接字段 accessor，以及 `D3DEmotePlayer.module`
  沿父级/root class-id 有序 map 懒建并由父级统一销毁的共享生命周期；纠正 timeline
  常量 owner 的过时注释，并保留空 owner 解引用、raw candidate store failure 与 shell
  不参与释放的原生边界。详见
  `analysis/motionplayer_d3d_emote_module_surface_lifecycle_four_binary_2026-08-14.md`。
- `Motion.Player` 四端一个 typed Variant constructor、零常量、92 成员完整交错发布顺序
  与 43 RW property/17 RO property/27 ordinary typed/2 explicit typed/3 raw 的 descriptor
  family；证明 raw 仅为 `setVariable/play/progress`，而脚本 `clear` 是直接绑定递归
  draw-to-layer body 的二 Variant typed method。恢复 constructor 的单 Void 空-adaptor
  sentinel、首参 CopyRef、surplus 忽略、attach-failure 析构，以及 clear 的
  membername→null receiver→result clear→二参 gate→native unwrap→双 Variant owner 边界；
  删除错误的 `Player::clearCompat` raw shim 和旧单目标地址注释。详见
  `analysis/motionplayer_player_ncb_surface_four_binary_2026-08-14.md`。
- `EmoteEngine` deque #7 Clamp 的四端自然布局、Android/iOS ARM32 double 对齐差异、
  libstdc++/libc++ block capacity、整记录清零式默认追加、五字段写入顺序、异常后部分
  entry 保留，以及 `varUd -> varLr` 逆析构；现有 C++ 字段和 builder 顺序已经一致。
- `EmoteEngine` deque #8 Transition 的单指针 `unique_ptr` owner、`{owner,label,flag}`
  四端布局、raw-pointer 直接 emplace、四种 STL block capacity、`label -> controller`
  析构、ctor-failure 回收/grow-failure 泄漏/post-emplace 不回滚，以及 reset 正序与正常
  析构逆序下不同的 Selector borrow 生命周期。
- `EmoteEngine` deque #9 Selector 的单指针 `unique_ptr` entry owner、四端 48/24 字节
  布局、未初始化 gate、borrowed targets、raw-pointer 直接 emplace、四种 STL block
  capacity、`targets -> label -> controller` 析构，以及真实 C++ constructor 的 option
  move、初始 selection、member unwind/new-expression 回收、grow-failure 泄漏与
  post-emplace 不回滚边界。
- `EmoteEngine` deque #10 Loop 的 `{unique_ptr<EmoteLoopController>, label}` owner、
  四端 16/8 字节布局、libstdc++/libc++ block capacity、zero/value-init controller、
  12-byte keyframe vector、raw-pointer 直接 emplace、`label -> keyframe backing -> controller`
  逆成员析构，以及 resize/property/grow failure 的 pre-emplace 泄漏与 post-emplace
  label/HM6 failure 保留边界。
- `EmoteEngine` deque #1 simple spring 的 48/28 字节 entry 自然布局、四种 deque
  block capacity、`unique_ptr<EmoteSpringState>` owner、真实参数 constructor/new-expression
  回滚、构造后 raw-pointer 窗口、直接 emplace 接管、pre-emplace 泄漏与
  `keyY -> keyX -> shapeLabel -> spring` 逆成员析构。
- `EmoteEngine` deque #2/#3 chain spring 的共同 builder/element specialization、56/32 字节
  自然布局、四种 deque block capacity、真实参数 constructor/new-expression 回滚、
  故意未写的 init byte、`unique_ptr<EmoteBustChainSpring>` raw-pointer 接管、pre-emplace
  泄漏、post-emplace 部分 entry 保留，以及
  `keyC -> keyB -> keyA -> shapeLabel -> spring` 逆成员析构。
- `EmoteEngine` HM3 timeline mapped value 的 `unique_ptr<TimelineData>` 与
  `unique_ptr<EmoteVarController>`、TimelineData 的自然 `deque<Track>` header、Track 内
  `unique_ptr<EmoteVarController>`，四端 112/88/84B mapped value 与 56/28B Track 布局、
  Android/iOS hash node 前缀差异、owner replacement、nested 逆析构和隐式 move-only
  生命周期。
- `EmoteEngine` wind emitter 的 raw single-owner 身份、1564B/128×12B 固定布局、构造
  slot/cache 零初始化、正常析构首阶段、stop 的 delete+null/cache 保留，以及 replacement
  先 delete 但不清 owner slot、分配成功后才覆盖所形成的 allocation-failure 悬空/二次
  delete 边界；该证据明确排除将 wind 美化为 `unique_ptr`。
- `EmoteObject` 的三成员 40/20B 自然布局、raw ResourceManager/Engine owner、pending
  new-expression allocation 回收、member 发布时点、正常 `Engine -> RM -> paths` 释放，
  以及发布后 paths/load/metadata 失败只回收 vector/临时量而泄漏 owner 前缀的边界；
  该 constructor unwind 证据排除了 `unique_ptr`。
  2026-08-16 V146又fresh闭合constructor load尾部：成员paths先copy，但load loop和project
  `back()`均读取caller input；所有load result复用一个working Variant，最后值被base覆盖，
  metadata独立存活。base层建立唯一copied/forced/retained accessor，以两个private hint读取
  typed chara/motion；apply后按motion→chara→accessor→metadata→base逆序释放。portable已
  同步source identity和owner数，四库hint命名、19条地址注释、bookmark、回读/保存完成，
  双模式test TU、Web 3/3、Wasmtime 4/4及双wasm解析通过。证据见
  `analysis/motionplayer_emoteobject_input_path_metadata_base_ncb_owner_four_binary_2026-08-16.md`。
- `D3DEmotePlayer` 的 0x38/0x24 listener-shell 布局、primary/secondary 双 raw-owner
  slot、`secondary delete -> primary delete -> pair clear` 的暂时悬空协议、load 的旧链先拆/
  新 primary 后提交、normal dtor 后再注销 listener，以及 typed clone 在 primary clone
  失败时泄漏已注册新 shell 的 raw-local 边界。
- `Player` 持有的 persistent `SeparateLayerAdaptor` raw owner：四端各自字段偏移和对象
  分配尺寸、constructor 零槽、lazy path 的 ctor-success 后发布、立即 begin-pass，以及
  explicit destructor body 的 pointee destroy/delete 后才清槽；iOS 两端的 delete-then-null
  顺序明确排除了 libc++ `unique_ptr::reset()` 的 exchange-first 形态。
- `ObjSource` 的 24/12B `{PSBRawNode owner,node; texture}` 自然布局、脚本默认构造的
  全零 native record、NCB adaptor 的 native/sticky 条件销毁、`texture -> PSB owner` 析构
  顺序，以及 lazy `CreateTexture2D` 返回值直接发布后再释放 bitmap/像素临时量的 retained
  raw slot；同时保留 `CreateAdaptor` 返回 null 时泄漏刚构造 native facade、null texture
  被 `drawLayer` 自然解引用、后续 Layer 调用失败不回滚 texture 槽等原版边界。
- `Player` 的六槽 persistent source workspace：两个连续 ResourceManager Variant owner、
  descriptor/颜色 Dictionary 引用图、primary/work 两个 lazy Layer、factory raw owner 跨
  `descriptor.color` PropSet 的正常/异常释放顺序、primary-only gate 与 publish-before-sizing
  所形成的粘滞半初始化边界、五类渲染后端共用 resolver，以及 work/颜色/primary/descriptor
  的逆析构和 property-graph 最终释放顺序。
- `ResourceManager` outer module map 与两个 nested texture map：四种 STL record/node ABI、
  Android libstdc++ eager ten-policy 与 iOS libc++ lazy-zero default construction（源码均无
  显式 `rehash(10)`）、outer candidate/rehash 强回滚、load 的 miss-only record 发布、
  Win texture 的 Release/store/AddRef 顺序，以及 KRKR descriptor 全零插入后按
  texture/origin/rect/clip 逐字段原位写入所形成的异常后部分状态；同时闭合 erase/clear
  保留 buckets 和 destructor body clear 后自动释放 bucket storage 的生命周期。
- `Player` 的 chara/stealthChara 双层状态机：四端 live writer 与 pending coordinator、
  typed getter/setter、值相等完整 no-op、primary/stealth 写槽差异、真实变化只清两条 motion
  label 与 playing、stealth-first copy queue/last-write-wins/getter 不可见、primary 后以持久
  pending 字段直接引用 nested flush 再释放；同时删除已落入 node-tree/render/shader 函数内部
  的旧单目标地址 helper 名。
- `Player.stop` 的 typed zero-argument void NCB 链：四端 registration helper、generated
  FuncCall 与单字节 native body，result 预清、负 argc 失败、surplus 参数忽略和 resolver
  failure 边界；删除会错误返回 Boolean true 的 raw `stopCompat`，并确认 stop 只清 playing，
  保留 labels/content/context/cursors/sync/pending 与全部容器。
- `Player::frameProgress` 的顶层状态机：parameterized/idle/first-frame/
  queue-gated cursor/正反 loop-wrap 分支，full reseek 的真实落入与调用次数，
  以及 idle gate 对同一 `_parameterEntries` vector 而非 node deque/独立
  prepared-frame list 的精确字段身份。
- parameter parse/append nested ncb source identity：2026-08-16 V145四端fresh反编译确认
  parse只拒绝Void，copied root accessor覆盖Count、逐项typed indexed getter、append和finalize，
  root最后释放；append只拒绝非Object，copied item accessor早于vector default append，随后
  固定四次typed字段读取。`id`复用command-list共享hint，另外三个字段各用独立hint；optional
  `division`是一次null-hint `MEMBERMUSTEXIST` checkVariant，失败但写值仍走差值fallback。
  本地已恢复直接ncbind结构，并以root/item重入owner、failure-after-write、vector渐进提交和
  finalize-before-root-dtor探针锁定边界；四库各23条逐地址注释、两书签、重反编译回读和保存
  完成，普通/headless test TU语法、Web 33/33、Wasmtime 62/62及双wasm解析通过。证据见
  `analysis/motionplayer_parameter_parse_append_nested_ncb_accessor_strict_division_four_binary_2026-08-16.md`。
- `Player::initNonEmoteMotion` nested ncb source identity：2026-08-16 V147四端fresh确认
  函数级motion-content accessor覆盖全部named读取、parameter选择、建树和变量初始化；
  priority成员另建full-expression临时accessor取得`[0]`，该accessor在root `content`前
  已释放，而由结果建立的root accessor一直存活到函数尾并先于motion accessor释放。
  `parameterize` hint与`Player_initNodeFields_guess`共享同一32位槽；V162 已校正后续
  `parameter` 虽只有 init 一个 consumer，却仍属于 `isValid` 与 `releaseLayerId` 之间的进程级
  全局序列。本地已恢复3角色accessor、8次typed getter与共享hint，并增加reentrant owner-clear/
  unwind probe；四库各1条函数注释、25条逐地址注释、2条data注释、bookmark、双函数重编译、
  xref读回和保存完成。普通/headless test TU语法、两套完整构建和双wasm解析通过。证据见
  `analysis/motionplayer_init_non_emote_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。
- `Player::initVariables` nested ncb source identity 与 empty-scope gate：2026-08-16
  V148四端fresh确认先clear deque，再以full-expression retained motion accessor取得
  `variable`，该临时owner在Void gate前释放；非Void结果建立函数级list accessor，Count只
  读取一次，每个indexed Variant又在append前建立逐项accessor。entry仍先append再按
  label(string)→seed→label(Variant)→scope(Variant→ttstr)写入；scope分支依据转换后字符串
  非空，而不是Variant非Void，因而empty String与Void都不加`::`。本地已恢复3角色accessor、
  5次typed getter和IsEmpty gate，并以motion/list/item三层reentrant owner、post-write
  failure、append-before-read和空/非空scope探针锁定生命周期；四库各1条函数注释、27条
  逐地址注释、bookmark、重编译回读与保存完成。普通/headless test TU语法、两套完整构建、
  双wasm解析通过；CTest两树均无已注册测试，未虚报runtime执行。证据见
  `analysis/motionplayer_init_variables_nested_ncb_accessor_empty_scope_four_binary_2026-08-16.md`。
- shared `VariableTrackEasing_evaluate_guess` nested NCB、segment base 与 unordered
  endpoint：2026-08-16 V149四端fresh确认函数级root accessor先后读取typed `x`/`y`
  Variant，再建立两个独立array accessor；Count只取x一次，普通析构为y accessor→x
  accessor→y Variant→x Variant→root accessor。x/y hint并非easing私有，而与Quad、
  PositionControlCurve、LayerGetter vertex及camera-offset字典共享同一进程槽。endpoint
  raw branch为`PL`/`LE`，两者均接受unordered，stride `MI`只接受ordered-less，故
  `t=NaN`严格在Count、x[0]后返回y[0]。stride cursor是segment end，四对控制点从
  `segmentEnd-3`开始；Count-1、+3、-3、+1均按32位wrap。本地恢复三层accessor、共享
  hint、unordered-inclusive gate、正确control base和精确多项式分组，并以dispatch顺序、
  NaN endpoint及三层reentrant owner-clear探针锁定。四库各1条函数注释、27条逐地址注释、
  2个准确data-offset注释、bookmark、重编译回读与保存完成。普通/headless test TU语法、
  两套完整构建和双wasm解析通过；CTest两树均无已注册测试。证据见
  `analysis/motionplayer_shared_easing_nested_ncb_segment_base_unordered_four_binary_2026-08-16.md`。
- VariableTrack slot step/merge nested NCB source tree 与 stale payload：2026-08-16
  V150四端fresh确认两个out-of-line helper共同保留frame-source root accessor，indexed
  Variant直接建立frame accessor；nonzero merge再由frame `content`建立第三层accessor，且
  content owner跨越后续frame-level `easing` getter/assignment，普通析构严格为content→
  frame→root。step先写frameIndex、再写time、最后clear merged；merge先置merged，type0只写
  typeZero并保留interp/interval/value/easing，未知非零type保留interp而刷新其他payload。
  所有typed getter均flags0/null hint/receiver==objthis，ordinary post-write failure不形成gate；
  uint32 frame index以同一signed32 bit pattern进入numeric getter，signed interval低32位写入
  uint32 slot。本地恢复2/3层accessor、精确write prefix与unwind，并新增reentrant root/frame/
  content clear、post-write failure、type0 stale payload、throw prefix与旧easing析构探针。
  四库各2条函数注释、29条逐地址注释、bookmark、双函数重编译读回与保存完成；普通/
  headless test TU语法、两套完整构建和双wasm解析通过，CTest两树无已注册测试。证据见
  `analysis/motionplayer_variable_slot_step_merge_nested_ncb_lifecycle_four_binary_2026-08-16.md`。
- MotionNode frame parser nested NCB、五槽共享hint与异常写前缀：2026-08-16 V151四端
  fresh确认parser先selective reset、再写frameIndex，随后保留frame-list root accessor，
  indexed Variant直接建立frame accessor；非type0再以frame.content建立第三层accessor，普通
  析构严格为content→frame→root。time/type/content/mask/act是连续五个process-wide hint：
  time还由skipToSync复用，content由merge/skip复用，mask由merge复用，type跨play/render/
  query/skip等路径共享。type0只写done并跳过content；未知非零type因reset保留crossfading
  false；无0x40000 mask保留旧action但由mask gate禁止消费。portable恢复三层accessor、
  typed Real/Integer/Variant/String getter、五共享hint、action retain-before-release与六类throw
  prefix，并扩展skipToSync hint探针。四库均拆分并命名5个4-byte data item，各写1条函数注释、
  21个code-head注释、5个data-head注释、bookmark，重编译读回并保存；普通/headless test
  TU语法、两套完整构建、双wasm解析与objdump通过，CTest两树无已注册测试。证据见
  `analysis/motionplayer_node_frame_parse_nested_ncb_shared_hint_lifecycle_four_binary_2026-08-16.md`。
- MotionNode frame mergeContent 完整 accessor owner tree 与字段提交边界：2026-08-16 V152
  四端fresh确认merged=true先于done早退；非done路径长期保留frame-list root→indexed frame→
  frame.content三层accessor，coord/object-color/mesh/object-points/motion/model/prt/camera/
  anchor/feedback按block建立nested owner。A32/iOS各直接出现13次AsObject；A64把root/frame/
  mesh scalar-replace为X25/X26/X22，但normal nested release与最终content→frame→root次序不变，
  后部大CFG仅为EH cleanup。portable恢复全部owner block、caller-owned dispatch getter overload、
  ordinary post-write failure继续conversion/commit，以及coord第二项抛异常时defaults+x已提交的
  精确prefix；新增四层reentrant clear、objthis/flags/content-hint、done早退和unwind探针。
  四库完成函数/逐地址注释、bookmark、重编译读回与保存；普通/headless test TU语法、两套
  完整构建、双wasm解析与objdump通过，CTest两树无已注册测试。证据见
  `analysis/motionplayer_node_frame_merge_content_accessor_owner_tree_four_binary_2026-08-16.md`。
- 单节点 timeline evaluator 的 `ti` quotient→u32 饱和向零转换、32 位乘积回绕、
  u32→double 链，以及 `1e-7`/`DBL_EPSILON` 三个 ordered-GE 门槛在阈值相等、
  NaN ratio 和 NaN old/new 差上的写回与 payload 早退边界。
- parent 4×4 Bezier mesh deformation 的五重 gate、输入/输出逐点 float 量化、零尺寸
  直除、原始 `u/v` 四点导数采样、两条 `atan2f` 参数顺序与面积缩放公式；同时闭合
  固定 16 点 vector wrapper 的错误尺寸后继续调用边界、注册期 row-major 单位曲面、
  scalar→NEON evaluator 单向进程级切换，以及 timeline 单侧空 patch 的单位曲面回退。
- 顶点阶段的 actual-parent→meshAncestor 选链、accumulated/ancestor dirty 传播、raw
  `meshCombine`/派生 separator/`hasMeshData` 三态拆分、函数级临时 patch vector 复用、
  `working += parent - identity` 的 mismatch 后继续与无检查边界、TL/TR/BR/BL 双线性建格、
  unsigned division/回绕、`7233/7241 + source.valid` 外层与 `5185/5193 + !source.blank`
  四角内层 gate，以及 separator 前全网格、后仅 anchor 的两阶段继承和平移。
- force-visible `emoteEdit` 几何镜像：真实 `coord`/`mtx` UTF-16 键、已有 Array 的原地
  `PropSetByNum(TJS_MEMBERENSURE)`、十个 named property 的精确顺序与 Real/Integer 类型、
  mapped anchor 输入、共享/独立 member-hint 槽、base/coord/mtx 三层 retained dispatch 的
  逆序释放，以及返回码忽略、异常传播和部分写入不回滚边界。
- ground-correction callback 的字段身份复审：call site 传当前 Player，worker 经
  `Player+0` root link 读取 root 的 raw current-dispatch bridge（64 位 `+16`、32 位 `+8`），
  而非 per-node layer dispatch；同时恢复 callback/result 独立 AddRef、两只参数 Variant
  CopyRef、共享 member-hint、current/parent 参数顺序、x/y/z 增量写入和完整逆清理。
- stencil-composite prepared-item 后处理：当前 Player 原始节点顺序的 type-12/bit-4/drawn
  三重 gate、同一 item 的 main/aux/self 三处借用拓扑、mask pointer-vector 的 raw order 与
  重复保留、type-0 直接 append、type-3 在普通模式展开 child range/preview 模式追加 wrapper，
  以及 trusted raw pointer 无 null guard、逐步 vector 修改不回滚的异常边界。
- prepared-item priority 选择入口：color 后/root-only 早退、persistent priority Variant 的
  CopyRef→AsObject retained dispatch→副本清理、同一 receiver/objthis 的反向数字读取、
  HRESULT 忽略、低 32 位 `+1` 回绕与 unchecked deque 选择，以及 getter 重入替换持久 owner
  后仍沿用旧 dispatch、duplicate/omitted index 只修改所选 drawn byte 的边界。
- prepared-item 非 preview type-4 particle 递归：selected node active gate 前执行、一个
  retained Array dispatch 覆盖 count 与全部升序数字查询、共享 main/aux 直接递归、颜色/
  draw/prior 三参数传播、container 自身无条件跳过，以及重入清 persistent Variant 后继续
  使用旧 receiver、成功 native query 返回 null 后仍无检查成员调用的边界。
- prepared-item 非 preview type-3 child/wrapper：active gate 后一次 borrowed child resolve、
  plain 共享列表直递归与 draw/mask wrapper 分流、wrapper child range 展开、forced child draw、
  aux-before-parent publication 的异常部分状态，以及 ordinary/wrapper 共用的 nullable raw
  visible-ancestor 只判 null、self parent 保留和 unchecked parent-item materialization。
- prepared ordinary-item admission：force/type 与 source-valid gate 后、item ensure 前即发布
  `drawnThisFrame=1`，完整 materialization 结束才追加 main；allocation/string/ancestor/vector/
  main-growth 任一异常均不回滚 drawn 或 item 前缀，且 priority duplicate/reentrant mutation 可
  让最终 byte 与 main borrowed-pointer membership 分离。
- `prepareRenderItems` 外层：motion-content 只按 Variant non-Void type tag gate、caller
  main/aux 不清空、neutral/false/false 递归后 stable-sort 整个 live main、aux 原样保留、空
  main 仍返回 true，以及 comparator 直接解引用 raw item 并只做 double `<`，无 null/tie/NaN
  美化和四种 STL temporary-buffer fallback 差异。
- `calcBounds` 非 preview type-4 粒子遍历：一个 retained Array dispatch 覆盖单次 count、
  全部升序 live 数字查询和每个 child 的递归 AABB pass；persistent node Variant 重入清空后
  仍沿用旧 receiver、count 快照而元素按需读取、四个 double 包含等号合并、异常只保留已合并
  前缀，并在粒子循环后继续同一节点的 active/type-3/ordinary 控制流。
- `calcBounds` 入口字段隔离：persistent ResourceManager Object owner 的临时副本清理后直接
  写四个 `±DBL_MAX` sentinel，不读取 motion context、不构造路径字符串；把旧诊断残留的
  `matchedMotionPath()` 移到显式启用的 Web trace sidecar，使默认 Octet context 不再在 AABB
  pass 中产生无关 `ttstr` 转换异常。
- `calcBounds` 非 preview type-3 child：active-slot gate 后直接从 node 持久 Variant 借用
  Object/native Player，无局部 AddRef/null guard；递归后继续使用同一 raw child，四个 double
  先窄化发布到 node float AABB，再提升合并 parent，并保留 malformed conversion、wrong-native
  crash 与重入清最后 owner 可悬空的边界。
- `calcBounds` 普通 node 点容器：两个独立连续 vector 按 composite 全范围优先、transformed
  仅 nonempty gate 后固定 16 点、两者空才内联 4 角；闭合四端 vector record/元素 ABI、
  transformed 多余尾部忽略和 size<16 越过逻辑 end 的 allocator/UB 边界。
- `calcBounds` 普通 node type mask：normal `0x1441` 与 preview `0x1449` 的合法 bit 集和
  source-valid 短路顺序，以及 unchecked signed `1 << nodeType` 在 AArch64 低 5 位、AArch32
  低 8 位/大 shift 清零、wasm 低 5 位之间的 malformed 平台 UB；不添加原版不存在的 guard。
- `getCommandList` Bezier `division`：signed int32 patch division 与 raw Player ratio 的
  double 乘积先转 signed int64，再仅在有序 `<50` 时保留转换；闭合 AArch64 `FCVTZS X`、
  ARMv7 外部 `__aeabi_d2lz`/`___fixdfdi` owner 边界、NaN/`>=50` 固定输出 Integer 50，
  并以显式 AArch64 saturation profile 消除 Web 的 NaN/越界 C++ 强转 UB。
- prepared-item Bezier division producer：node raw 32 位 division 按 unsigned 转 double，
  与 raw Player ratio 相乘后 signed-int32 饱和、再用 signed 整数 `>=50` cap；固定 NaN=0、
  `+Inf`=50、`-Inf`=`INT32_MIN`、raw `0xFFFFFFFF` 的边界，并闭合与后续 command 阶段
  再次乘 ratio 所形成的两阶段数据流及持久 item stale/异常部分发布时序。
- `calcViewParam` mesh-chain division producer：ancestor raw 32 位 division 按 unsigned
  转 double，乘一次 raw Player ratio 后做 unsigned-int32 饱和，再以 unsigned 整数比较
  cap 50；固定 NaN/负数/`-Inf`/`Inf*0` 为 0、正溢出/`+Inf` 为 50，并与 prepared 的
  signed-int32 及 command 的 signed-int64/浮点 ordered-select 语义彻底拆分。
- `updateLayers` 实际网格 division：共同 unsigned-int32 饱和后，own type-1 的 cap 却
  用 signed `LT/GE`，inherited source 才用 unsigned `CC/CS`；闭合 `2^31`/`+Inf` 在
  own 路径保留 sign-bit word、uint32 split 回绕、stored X/Y 再按 signed int 进入建格
  loop 的跨域数据流，并拆分此前错误共享的 cap helper。
- `updateLayers` unsigned 除零 owner：两份 AArch64 的 inline `UDIV` 直接证明 quotient
  为 0；Android armv7 经 ELF PLT 导入 `__aeabi_uidiv`、iOS armv7 经 Mach-O stub 导入
  libSystem `___udivsi3`，零除结果/信号属于外部 runtime 而非 plugin bytes；Web helper
  显式命名为 AArch64 profile，修正旧文档把 ARM32 也写死为零的过度结论。
- render-time Bezier cell division：D3D、canvas 三处 producer、accurate-SLA 与
  PrivateMotionGLL 在四端全部使用 extent 分别 uint32 饱和、W-register 乘加回绕、unsigned
  divide、输出 word 回绕后 signed 解释的同一模板；删除从旧 `libkrkr2.so` 地址误推的
  floating `FDIV/FCVTZS` helper，统一所有 backend，并固定 NaN/无穷/负 extent、分母与
  numerator 回绕、negative division bit pattern 及 AArch64/ARMv7 除零 owner 边界。
- process-shared D3DAdaptor：零初始化 raw BSS slot、无 guard/atomic/退出析构、首次
  width→height→allocation→main-Window owner→signed `/2` center→ctor→成功后 publication，
  constructor failure delete 与 retry、脚本工厂 allocation→四参数顺序转换→ctor→result
  publication/new-expression failure delete、首次 owner/尺寸永久冻结、多 Player 共享/并发竞态，
  以及 target `setSize→visible→render→capture` 的无 positive gate 顺序；删除旧
  `static unique_ptr`、预发布 partial object、每帧 adaptor visible、Layer Update 和
  伪造的 lastCanvas side effect（V246 又证明字段本身不存在），并保留默认
  `canvasCaptureEnabled=false` 导致 shared render
  入口立即返回这一反直觉四端共同边界。
- `updateLayers` parameter-mode reset：尾部第二个 record range 是既有
  `vector<MotionParameterEntry>`，不是 per-node eval scratch；四端按 56/48/56/44B
  步长只清 `mode`，把 binder mode 固定为一次成功 update 的 trigger。删除伪
  `_perNodeEvalData` 的 pre-phase resize/evalTime 写与额外 allocation/异常点，恢复
  node flags/dirty→parameter mode→final state bytes 的 cleanup 数据流。
- `updateLayers` root invariant / diagnostic isolation：四端入口在 producer flag reset 后
  直接解析并读写 deque root，没有 empty recovery branch；完整函数零字符串引用且 direct
  call 集不含 Variant/string/path 转换。删除 port-only 空节点静默返回，并把
  `matchedMotionPath()` 限制到显式开启 Web trace 后，避免默认更新路径新增分配与异常点；
  同时清掉仍引用旧 `libkrkr2.so` 的 sidecar 注释。
- `Player.progress` diagnostic isolation：四端 wrapper 在 receiver/argc 后只做
  `AsReal→*60/1000→bridge/inlined bridge`，没有 path、format、stack/log/snapshot 调用。
  将 motion-context 文本化、两次 eager `fmt::format`、栈回溯及 path filter 全部移入缓存的
  opt-in trace/snapshot gate，恢复默认每帧 wrapper 的分配与异常边界。
- `playing/allplaying` getter diagnostic isolation：四端 local getter 仅为 6–8B 的 byte
  load/return，aggregate getter 只遍历 type-3 child、strict unwrap、递归并回退同一 byte；
  将三个返回分支的 motion-path 文本化移入 PRTDIAG 总 gate。并纠正“所有目标都在 child
  后 reload deque size”的旧注释：iOS armv7 hoist entry bound，另三端可见 reload。
- draw-entry diagnostic isolation：四端 `drawCompat` 与六参数 affine setter 全部为
  0 string references；将默认 draw 中的 motion-context 文本化、TJS stack walk 及五组
  eager `std::string` route-check 临时对象移入缓存的 path gate，setter path 也移入总 gate；
  删除恒 `if(false)` 的 matrix-log lambda/call sites，保持 render routing/owner unwind 不变。
- `updateLayers` phase-2 trace projection isolation：旧代码每 node 无条件构造
  `TimelineTraceState`、窄化 active/other src 到两个 owning strings；四端完整 native 函数
  0 string refs 且 evaluator 前无此 callee。将 path 与整个 projection 延迟到一次缓存的
  path-specific gate，默认 phase 2 恢复无额外 per-node allocation/conversion/异常点的数据流。
- MotionSub snapshot isolation：四端主函数除 production `'/'` split delimiter 外无
  path/SNAP/fprintf 数据流，preview 是最早业务 gate；将 parent path 与 snapshot filter
  移到 preview 后的 opt-in gate，两个 snapshot block 各只物化一次 child path，默认 child
  update/teardown/shared-step 不再转换 parent/child context Variant。
- prepared-item build/sort diagnostic isolation：四端 recursive builder 与 prepare wrapper
  均为 0 string refs；完整 builder call-set 只含 Variant 数值读取、child 递归、颜色/mesh/
  rect 与 pointer-vector 生产 helper。将 parent/child path、fmt/snapshot 以及 wrapper 的
  `vector<double>` 排序键副本移入缓存的 opt-in gate，保留 native stable-sort pointer buffer、
  全 main range 排序与 comparator 边界。
- `buildRenderCommands` diagnostic isolation：四端函数虽含 9–19 个生产 TJS/Layer string
  refs，但 `SNAPCMD`/trace/path/failure-reason 文本在 ASCII/UTF-16/UTF-32 全目标均零命中，
  完整 direct-call scan 也无 formatting/logger/string-conversion。将 parent path、per-item
  failure string、重复 expected-corner projection 和 tail snapshot/count 收进缓存 gate；
  两个旧单地址 helper 改为语义 `_guess` 名，并保持 retired-tree cleanup 之前无 sidecar throw。
- canvas submit diagnostic isolation：确认本地 render wrapper + execute helper 共同对应四端
  完整 `Player_renderToCanvas_guess`，不是两个 native ABI。七个 sidecar labels 的三编码搜索
  全部零命中，完整 call-set 无 path/format/logger；将两层 path、per-item trace/RGBA unpack、
  mask path 与 SNAPCOPY 收进缓存 gate，并把无条件 owning `branch` 改为静态 literal pointer，
  保持 source owner、direct/buffered dispatch、ancestor mask 与 final setClip 顺序。
- vertex/ShapeAABB diagnostic isolation：四端 vertex function 的 41–58 个 production calls
  不含 path/format/logger；四端 ShapeAABB 更是 0 strings/0 calls。把 quad trace path 从
  per-node 提升为一次 phase gate，把 SNAPSHAPE path/window 从 active shape node loop 提升到
  helper entry；默认 geometry 不再转换 context、复制 expected vertices 或 narrow src/label。
- post-draw internal-Layer diagnostic isolation：四端普通 `assignImages` helper 全为 0 string
  refs，accurate `piledCopy` helper 的完整 calls 也只含 Variant/accessor/materialize/dimension/
  dispatch。把 path、false-path log 和 true-path fmt/check 移入 trace gate，保持 producer flag
  snapshot/early return、height-before-width、owner reverse cleanup；清掉相邻旧 `sub_6CE...` 文本。
- motion load pipeline diagnostic isolation：完整复扫四端 `playImpl`、普通 motion initializer
  与 node-tree wrapper；三函数只含 production load/Variant/property/parameter/tree/container
  调用，六组 PRTDIAG/path 文本的 ASCII/UTF-16/UTF-32 检索全端零命中。把三个 sampled
  counter、load 前后 path 比较、initializer logs 与 build per-node projection 收进缓存的
  opt-in trace gate，保持逐字段提交、state-byte commit、旧树 teardown 和容器 publication。
- `Motion.ResourceManager` 完整 NCB surface / constructor 生命周期：四端共同为一个
  `(Variant,int32)` typed constructor 加精确 12 项 typed descriptor，唯一 property 是 RO
  `bufLayer`，decrypt setters 属于后续 emoteplayer 注入。闭合 membername、单 Void shell、
  result-clear/argc、surplus、两参数 owning conversion、native allocation、adaptor attach
  rollback 与 0xE8/0x80/0xC8/0x70 ABI；并修正本地把 layer-id sentinel 提前到脚本 RNG
  求值之前的异常生命周期偏差，恢复 `eval RNG -> insert(0) -> next/state=1`。
- `ResourceManager::layerIdState_guess` 完整插件范围 consumer audit：四端复扫全部
  ResourceManager/NCB 主体、227/237/235/236 个已恢复 Player/MotionNode/SourceCache 主体，
  并在原始指令层覆盖直接成员访问、地址形成和从相邻槽开始的 64 位/`LDRD`/`STRD`
  packed 访问。结果统一为 ctor 写 1 后 dormant，析构不处理；物理槽保留，原名未知继续
  `_guess`，不再把“尚未找到 consumer”列作未闭合数据流。
- `D3DAdaptor` 四整数与五布尔之间的独立 32 位 dormant scalar：四端 ctor 都显式写 0，
  Android 以 packed 64-bit/`STRD` 同时发布后续布尔默认组，iOS 单独 word store；完整
  15-member surface、capture/render、Player wrapper、source/target callback 和析构均无
  构造后访问。补回旧布局表遗漏的 `+16` 行，源码/IDB 从 offset placeholder 改为保守
  `_dormantState_guess`，原始名仍未知。
- `D3DAdaptor` 状态 consumer 分类：`canvasCaptureEnabled` 是 Player-to-target render 总门，
  `clearEnabled` 是显式 FillARGB 门；`visible`/`alphaOpAdd` 只有 property 回显，`resizable`/
  `clearColor` 只有 setter-retained storage。四端 raw renderer 的唯一 method-selector call 均以
  `MOV W3,#1` / `MOVS R3,#1` 固定传 alpha-add，clear helper 则只消费显式 `color` 形参；纠正
  旧文档“渲染携带 adaptor alphaOpAdd”的过时结论，并记录 capture flag 不 gate 直接
  `captureCanvas` 的边界；同时恢复 `FillARGB` raw method 与 color parameter ID 的两个独立
  function-local-static guard、异常重试、已发布前槽保留和无退出析构生命周期。
- `D3DAdaptor::captureCanvas` software/GPU 提交边界：删除四参考不存在的 null Layer、null
  candidate 和 guarded target-Release 安全路径；software 恢复 target-size→Layer resize→source→
  destination→双 pitch 的严格调用顺序、equal-pitch 32 位乘积后符号扩展和 unequal-pitch signed
  height gate；GPU 恢复 candidate 先 AddRef、Layer 接管 old target、adaptor 无条件 Release、
  candidate/null 先发布再 create 的非事务交换。记录 AssignTexture 异常泄漏保护引用、create
  异常/返回 null 后 Layer 已提交而 adaptor 为 null 的边界，四份 IDB 已补注释/书签并保存。
- `D3DAdaptor` 参数 constructor / new-expression 失败生命周期：四端共同在 Window AddRef
  之前安装 width/height/centers 与 raw Window，之后才 lookup manager/CreateTexture；恢复源码
  member-initializer 与辅助 initializer 的可观察顺序。闭合 map-subobject unwind、完整 dtor
  不执行、raw Window 引用不 rollback、null texture 返回仍成功发布，以及 NCB/shared 外层
  storage delete 与 publication-after-ctor。补查 A32 主函数边界外 cold landing、I64 独立 cleanup
  function，并澄清 I32 SjLj `call-site -> selector = -1` 后 owner/ctor 均 delete 的 ABI 映射。
- `D3DAdaptor` 正常 destructor / software-texture map 节点生命周期：恢复 explicit clear→target
  Release+null→Window Release 但不清 raw slot→空 map 自动析构的精确顺序，删除本地额外
  `_window=nullptr`。闭合 borrowed pointer key、mapped holder Release-before-node-delete、header
  尾提交与不安全重入窗口；Android GNU tree 按 descending key 销毁，iOS libc++ 按
  left-right-root postorder。补齐 target/Window Release 异常先清 map subobject 再 terminate，
  并纠正 A64 callable-tree helper 的过时误名。
- `D3DAdaptor` software-texture miss / emplace 提交与重入边界：四端固定先取得 private
  OpenGL manager，再按 scanline→pitch→width/height→format 创建 static copy；源码以 locals
  恢复 callback/exception 顺序。闭合 pre-find 与 emplace 间重入插入同 key 时 map 保留旧
  holder、caller 却返回未缓存新 copy 的非事务边界，以及 allocator failure/null copy/
  creation-ref 残留。区分 GNU candidate-before-lookup/AddRef→duplicate Release 与 libc++
  lookup-before-allocation；四份 IDB 已补 emplace/find 命名、注释、书签并保存。
- Motion dispatch 八个 legacy member-hint globals：重新用四端 UTF-16LE 字面量、data xref
  和 call argument 定位 `window/piledCopy/assignImages/Layer/meshCopy/bezierPatchCopy/
  affineCopy/bufLayer` 的独立 zero-init `tjs_uint32` 槽，删除源码旧 A64 地址。修正
  `SeparateLayerAdaptor` assignImages 错传 null hint 的行为；区分 SLA ctor 的 null-window-
  hint 与 internal-layer materialization 的 cached window hint。iOS IDB 已硬化八个 dword
  名称/类型，Android 聚合 BSS 用 exact-offset 注释/书签，四库均保存。
- `Motion.SourceCache` 完整 NCB surface / 双构造路径 / adaptor 生命周期：四端 registrar
  只发布零参数 constructor 与精确 `loadSource -> clearCache -> bufLayer(RO)` 三项；纠正本地
  误把 ResourceManager 专用 `(Variant,int32)` base constructor 发布给脚本的旧实现。闭合
  membername、单 Void shell、result clear、非负 surplus 全忽略、attach rollback、四端
  0x58/0x34/0x60/0x38 布局和 non-sticky list/Variant 逆序释放；首声明 `= default` 恢复
  ncbind `new SourceCache()` 的三个 Void、双零计数、空 list value-initialization。
- `Motion.ObjSource` 完整 NCB surface / 双构造路径 / facade 边界：四端共同为一个零参数
  constructor，随后精确五个 RO property `originX/originY/width/height/clip` 与 `drawLayer`
  method；闭合 membername、单 Void shell、surplus ignore、0x18/0x0C 三槽零 native、attach
  rollback、strict/category/optional getter差异和 non-dictionary draw 早退。明确区分脚本默认
  attach 失败的 dtor+free 与 ResourceManager raw-node factory adaptor-null 时不 reclaim native，
  并保持 texture -> PSB owner 的析构顺序。
- `Motion.LayerGetter` 完整 NCB surface / live facade / getter 容器边界：四端共同为零参数
  constructor 加精确 29 个 RO property、0 method/raw/setter，`left/x` 与 `top/y` 分别复用 native
  target；闭合 8/4B null-node wrapper、单 Void shell、surplus ignore、attach rollback、non-sticky
  adaptor 只销毁 wrapper 而永不拥有 MotionNode，以及 null getter自然解引用和 tree rebuild 后悬空
  facade。恢复 `layerVisible = visible && active`，删除 drawFlag 旧注释，并保持 `angle*pi` 后自加倍
  再除 360 的 overflow 边界；同时闭合 fresh coord/mtx/vtx/color/bezier/shape 容器与 type-gated
  motion/particle Variant CopyRef。
- `Motion` 顶层 class/namespace 的完整 NCB surface / 发布与反注册生命周期：四端严格为
  23 个静态整数常量、11 个 in-flow subclass、两个 namespace typed method，Player 固定为第六
  subclass 且无 global alias；闭合无显式 constructor 时 RegistEnd 追加 `-1002` dummy、空 adaptor、
  class-info/global publication、正向 unregistration，以及异常 cleanup 仍发布 partial prefix 的
  非事务边界。两个 free-function wrapper 均恢复 membername/null receiver/result clear/arity 顺序，
  alpha 精确要求 11 参数并持有两个 closure temporary，D3D availability 忽略所有非负 surplus。
- `Motion.D3DAdaptor` 完整 NCB surface / Factory / typed-nullsub 边界：四端严格为一个
  Factory、11 个 method、4 个 RW property、0 raw，并恢复 `visible`、`alphaOpAdd`、
  `captureCanvas` 的原始 interleaving。闭合 one-Void shell、Factory result 不变、valid 参数加
  null receiver 时 construct 后 attach rollback、native factory 无 friendly pointer guard、分配后
  按索引 1..4 转整数再 publication；修正 `registerBg`/`registerCaption` 原先丢失的
  `(Variant,float,float,float,bool)` / `(Variant,float,float)` typed conversion ABI。
- 插件 module partition / dependency / load lifecycle：重新确认 `D3DEmoteModule` 与
  `D3DEmotePlayer` 属于 `DrawDeviceD3D.dll` 的七 class static-init bundle，而不是
  `emoteplayer.dll`；恢复七 class 源码构造顺序与 head-insert 导致的反序执行、
  `DrawDeviceD3D -> emoteplayer -> motionplayer` PreRegist 链、两个 native class ID 的
  dependency 前后顺序，以及 EmotePlayer/decrypt setter 手工发布。闭合 `LoadModule` 的
  小写 key、registered set + internal map + 三 `std::list` 数据流、already/missing 均 false、
  末尾 commit、异常 partial side effects/retry 和 no-unload 边界；移动两个 registrar、补
  DrawDevice callback，并修正本地重复加载误返回 true。
- `D3DLayerObject` / `D3DLayer` / listener 容器与生命周期：闭合四端基类/派生布局、
  Android 两指针 list 与 iOS `next/prev/size` libc++ 差异、duplicate push 与 remove-all、
  borrowed owner 的析构前置条件、统一 parent setter、root pointer multiset 单节点删除和重复 add；
  恢复三参数 `OnUpdate(int,Variant)` ABI、基类 pure `TransformPoint` 与 manager 恒 false
  override。确认成员为真实 `cocos2d::Mat4`，并根据 `Mat4::set` 写入体修正此前写反的最终
  映射：`setMatrix` 保持输入原序，`setMatrixGL` 保存转置。四份 recovery IDB 已补结构、
  命名、类型、书签，Android arm64 被误合并的 trap/OnUpdate 函数边界也已拆开。
- `D3DImage` / texture holder / `ManagedObjects` 生命周期：恢复 3-word/3-pointer
  `{vptr,borrowed root,holder*}` 布局、virtual destructor 后的第三个 clear-holder 虚槽和真实
  `TJS::tTJSRefHolder<iTVPTexture2D>`；闭合四端 factory 的 BADPARAMCOUNT/INVALIDTYPE 分界、
  root `std::set<D3DImage*>` unique insertion/erase、root-first 悬空 owner 前置条件，以及
  D3DPicture 对 image 的纯借用。`load` 现在精确只调用一次 `GetMainImage()`，保留直接覆盖
  旧 holder 的泄漏，并纠正旧移植错误释放 software-copy 初始引用的偏差；四份 recovery IDB
  已补对象/holder 类型、factory/load/dtor/clear/set helper 命名、类型、注释与书签。
- DrawDevice/root transition 状态机：闭合 `startTransition` 的严格 options object、
  `method/vague/rule` MEMBERMUSTEXIST 查询、大小写敏感 universal 判定、旧 rule 先释放再替换、
  无 layer/image/texture null guard、正常末尾 active/state 提交，以及 `stopTransition` 的
  method=-1/state=0/rule release 而 vague 保留。四端完整 `Show()` 均只读 active/state，固定
  `AlphaBlend_SD(opacity=state)`，method/vague/rule 零读取；据此删除本地 graceful-null 偏差，
  但不补造 universal shader。四份 recovery IDB 已补命名、注释与书签。
- DrawDevice render targets / capture 数据流：闭合 `capture` 每次新建 texture、先发布
  CurrentTarget、FrontItems 严格 `<` 门槛、software MainImage::Update / GPU AssignTexture、
  正常尾部 Release+清零而异常不清理的边界；恢复 Show 只凭 BackTarget 做 grow-only 复用、
  Front→Back 释放/创建、无创建失败 guard，以及 screen-size 变化释放双 target 而 primary-size
  只通知 Window。把 D3DLayerObject 的旧 texture 参数猜测修正为 `Draw(const tTVPPoint&)`，
  D3DLayer 再从 Parent 取 CurrentTarget。四份 recovery IDB 已同步命名、类型、注释和书签。
- DrawDevice FrontItems / BackItems 节点与 live-key 比较器：四端节点分配严格为 64 位
  `0x28`、32 位 `0x14`，载荷只有 `D3DLayerObject *`，不存在独立 int key；比较器分别
  解引用实时 front/back index，等价值不拒绝，故源码结构为两个带不同 comparator 的
  `std::multiset`。恢复 `equal_range(object)` 后按 pointer identity 只删首个、setter 的
  erase→写字段→insert 顺序，并记录重复节点残留时原位改 key 会破坏树不变量、分配失败时
  对象保留新 index 但从目标树缺席、AddChild 第二棵树失败时部分提交的边界。源码和相关
  旧文档已从 multimap 纠正，四份 recovery IDB 已补精确 node/container 类型、helper 名、
  签名、注释与书签。
- `splitTtstr` 共享 helper / owning vector / 边界与调用面：四端共同为按值 remainder、
  按引用 separator、三指针 `std::vector<ttstr>` hidden-return；每个 prefix 持有独立 intrusive
  字符串引用，final remainder 无条件 push，leading/trailing/adjacent separator 保留空元素。
  四套空 wide-string 构造都规范化为 null storage，`IndexOf` 对 null needle 返回 `-1`，所以空
  separator 产生 singleton 而不会死循环。闭合精确九处 caller、caller-side vector teardown、
  Android EHABI/iOS cold/SjLj cleanup 差异；恢复每轮 `separator.GetLen` → `remainder.GetLen` →
  `separator.GetLen` 的三 getter 数据流，并从公共头迁出旧四端绝对地址。
- dispatch 属性访问 helper / Variant 临时所有权：闭合 named real/int/bool/Variant/string、
  numeric real/int/Variant、`count` 和 strict optional probe 的四端函数映射；确认 receiver
  与 objthis 恒为同一 holder dispatch，普通 HRESULT 被忽略但 getter 写入结果仍有效，count
  固定 `PropGet(0,"count",null)`。恢复 Variant-return 的属性临时→返回副本→临时析构、string
  的独立 ttstr owner、typed helper 直接转换，以及 strict Variant 成功时
  probe→中间副本→destination、失败时 destination 不变的提交/析构顺序；删除公共头最后六个
  旧 Android helper 地址并增加失败 getter 先写 result 的回归测试。
- NodeLabelMap raw-label 插入/查找/teardown：闭合 build 递归第一次 `label` 读取后的
  `operator[]`、duplicate first-key-owner/last-index、四套 libstdc++/libc++ RB-tree node ABI、
  null-backed key 先于任一 non-null backing 的 comparator，以及旧节点 suffix erase 后的 key
  Release/tree delete/header reset。确认 Android armv7 与 iOS 两端的 stencil、camera constraint、
  CameraNode、motion dt==4、particle dt==4 都显式调用同一 Player resolver，而 Android arm64
  只是等价内联；据此删除旧单端 free helper 身份、恢复 member 调用链、修正 allocated-empty
  key 合并偏差，并移除 particle 多出的 mapped-index bounds guard。
- full-reseek tag/root ABI、游标与 owner 尾部：四端六处 call site 均证明成员只接收
  `this`，动态 time getter 后重新读取 live evaluation 字段；恢复 tag coarse 双增量、
  signed `count<1` 全跳过、real scan/integer cache、align→sync→action 和 root negative-count
  边界。函数作用域 owner 构造顺序固定为 tag source、priority source、current root、next
  root，跨 variable/node/join/HM1 尾部后逆序析构；重入测试覆盖 count getter 清持久字段、
  改 evaluation time 和 later priority getter 清最后 external tag owner。
- variable-track incremental forward/rewind：四端各三个调用点共同纠正两函数为
  this-only ABI，循环内从 Player 重读 live evaluation time。forward 恢复 per-track
  count-only local source owner、persistent-field step/merge、raw cursor active、32 位回绕
  `count-2` 后 signed compare、ordered-LT 的 NaN-continue、cursor-before-step 异常提交及
  slot0/slot0 双 merge；rewind 恢复无 count/owner、live ordered-GT、uint32 index-zero
  下溢到 signed `-1` 以及 physical slot0/slot1 merge。新增回归覆盖 INT_MIN、NaN、owner
  尾释放、throwing numeric getter 与 time-getter 重入；四份 recovery IDB 已改类型、注释、
  书签并保存，Catch2 TU 语法检查及 33-step Web Debug 编译/链接通过。
- incremental tag/root forward/rewind：恢复 aggregate 先 tag owner、tag 完成后才 priority
  owner，二者跨 root/variable/node phase 到尾部再 priority→tag 逆析构；forward tag
  `count>=1` 而 root 无 gate，32 位回绕 `count-2`/cursor、ordered-LT 使 NaN 继续且
  root `INT_MIN-2` 仍可进入。rewind tag 仅 `count!=0`、root 无 count lookup，ordered-GT
  使 NaN 停止且 cursor0 下溢到 signed `-1`。align→sync→action、root content/time 与
  live getter 重入提交顺序均已由四端回归固定；四份 recovery IDB 已补注释/书签并保存，
  Catch2 TU 语法检查与 33-step Web Debug 最终链接通过。
- incremental non-root node forward/rewind：四端共同恢复 `1..<live nodeCount` 与
  libstdc++/libc++ deque 差异，parameterized node 两方向走同一共享 stepper。ordinary
  forward 快照 raw selector，持有 count-only frame-list owner，使用 wrapping signed
  limit、live ordered-LT/NaN-continue 与 parse-before-crossed-action；rewind 无 count/owner，
  使用 live ordered-GT/NaN-stop、zero-underflow 到 signed `-1` 与 entered-slot action。
  两方向均延迟 exact `flags=1`，再 physical slot0/slot1 merge，并恢复无 nodeType 范围
  保护的 direct source-mask shift。owner/异常前缀/数值读取回归、Catch2 TU 语法检查、
  33-step Web Debug 最终链接以及四份 recovery IDB 注释/书签/保存均通过。
- TransformOrder / Player HM2 旧单端注释迁移：四端根 registrar 重新确认 operation ID
  `Flip/Angle/Zoom/Slant=0/1/2/3` 与 script publication
  `Flip/Slant/Zoom/Angle`；Player binder 重新确认 HM2 偏移为
  `+320/+220/+248/+180`。HM2、join variable snapshot、Engine scalar map 复用同一
  `LabelValueMap::operator[]` specialization，四套 libstdc++/libc++ node 为
  `32/32/32/20` 字节且 miss 都 CopyRef key、value-init double。已从编译源码删除旧
  `libkrkr2.so` registrar 地址、`Player+320` 共同身份和私有 HM2-upsert 误名。
- logo-chain/snapshot Web sidecar native absence：以 ida-search-string 三编码在四份当前
  IDB 检索 `tracelogochain`、`traceLogoChain`、`-tracelogochain`、`snaplogo`、
  `logoChain`，全部零命中；fresh EmoteObject construct/load 与 Player play/load body
  也无 query/trace/path-format sidecar。保留 non-Emscripten 恒 false、Web 显式 opt-in
  行为，删除 RuntimeSupport 中旧 `libkrkr2.so` 地址/helper 注释，四份 IDB 已补注释、
  bookmark 并保存。

- shared Layer factory / ordinal resolver / workspace 异常边界：三份非内联参考中同一 helper
  各有七个 xref，跨 SourceCache ctor/load、command-builder group、full/payload-free SLA
  resolver 与 workspace primary/work；Android arm64 对应内联。恢复唯一公共 helper 的
  raw global/created owner、忽略 HRESULT、null 自然失败与 CreateNew 异常泄漏边界；补回
  Variant-only active/retired map resolver、absolute/hitThreshold 且不递增 sequence，并让
  accurate renderer 去掉原生不存在的 payload/refresh 输出。四份 recovery IDB 已重命名、
  注释、书签并保存，syntax-only 与 35-step Web Debug 最终链接通过。

- sticky shared-D3D 的 SLA target 生命周期：纠正旧的 `tryResolveLayerDispatch(SLA)` /
  native-Layer 直写实现，恢复 shared adaptor 先构造、SLA active/retired 红黑树交换、sequence
  清零、payload-free ordinal 0、局部 Variant CopyRef、retired clear、private+active clear，随后
  仍从已 Invalidate closure 执行 TJS `setSize`/`visible`、render、capture 的反直觉顺序；普通
  target 才直接复制原 Variant。共享 constructor helper 删除无意义 target 参数，visible
  call site 当时误建为独立 hint；后续 V160 已由四端完整 xref 校正为与 SLA assign、accurate
  SLA、calcViewParam 共用同一 `visible` 槽。三端非内联 map swap helper 已统一命名，四份 IDB 已注释、书签并
  保存；syntax-only 与 38-step Web Debug 最终链接通过。证据并入
  `analysis/motionplayer_shared_d3d_adaptor_lifecycle_four_binary_2026-08-14.md`。

- EmoteAngleController setter / state 字典 / restore 生命周期：四端重新确认 12B deque
  的 immediate/append 入队边界、七字段 `phase/tick/speed/exponent/frame/prev/target` 顺序，
  并用 UTF-16LE 原始字节推翻 iOS Hex-Rays 的单字符假象。恢复七个跨 Var/eye/eyebrow/
  mouth/selector 共用的进程级 member-hint 槽；Angle restore 现按 closure CopyRef、强制
  Object 转换、accessor retain、临时 Variant 提前释放、逐字段 strict probe 的原顺序执行，
  非对象不再静默跳过，同时保留 `prev`/`target` 都写 `startRad`、从不恢复 `targetRad`
  的发布版 bug。四份 IDB 已重命名/注释/书签/保存，syntax-only 与 4-step Web Debug
  最终链接通过。证据并入
  `analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md`。

- controller-state restore/accessor/request-queue 家族：四端 fresh 审计 Blink、Eyebrow、
  Mouth、Var、Selector，确认只有 Var 在原入参非 Object 时静默返回，其余 controller
  都先 CopyRef/强制 Object/accessor retain。strict Variant probe 恢复“临时读取、成功后
  才 copy-assign，失败不污染调用者目标”；Eye/Eyebrow `rq` 只在取得 native Array 后
  clear deque，并按 `p0`、`p1` 顺序逐项提交，异常保留已重建前缀；Var 的
  `frame/prev/target` 复用入参 Variant 槽并逐索引即时写数组。`v/length/lengthDone/rq/
  p0/p1/mouth` 的共享 hint 已接回源码，BustChainSpring 的 `length` 复用同一槽。四份
  IDB 已重命名/注释/书签/保存；syntax-only 与 34-step Web Debug 最终链接通过。证据见
  `analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md`。

- EmoteEngine 顶层 state pipeline：四端重新解析 serialize/unserialize 的真实入口，确认
  `timeline/eye/eyebrow/mouth/transition/selector/base/outerforce` 固定顺序和逐子树即时
  提交。serialize 在建 Dictionary 前以 `dt=0` 刷新 controller 输出并写变量表；
  unserialize CopyRef/强制 Object/retain dispatch 后，以 flags 0 忽略 getter HRESULT，
  缺失 child 作为 Void 继续，其中缺失 `timeline` 仍先 stop timeline。恢复七个顶层专用
  hint，`mouth` 继续复用 controller 槽；异常只 cleanup 临时对象，不回滚已发生的预刷新
  或前序 restore。四份 IDB 已重命名/注释/书签/保存，syntax-only 与 3-step Web Debug
  最终链接通过。证据见
  `analysis/motionplayer_engine_state_pipeline_four_binary_2026-08-15.md`。

- Timeline state snapshot/restore：四端确认 active-label vector 按序遍历并以
  `timelineStates[label]` materialize stale map entry，item 固定发布
  `label/flags|1/curTime/blendRatioCtrl/stopWhenBlendDone`。restore 在 Array gate 前无条件
  stop 全部 timeline；每个 Object item 以 retained accessor 读取，随后按
  flags/curTime、play、inclusive window、autoStop、blend controller 即时提交，且
  `blendRatioCtrl` strict probe 复用并覆盖 by-value timeline Variant 槽。五字段共享 hint
  已接回源码，四份 IDB 已重命名/注释/书签/保存；syntax-only 与 10-step Web Debug
  最终链接通过。证据见
  `analysis/motionplayer_timeline_state_snapshot_restore_four_binary_2026-08-15.md`。

- Eye/Eyebrow/Mouth/Transition/Selector collection restore wrappers：五类均只接受 native
  Array，跳过非 Object item，并以 retained accessor 和共享 `engineLabel` hint 执行成功后
  才提交的 strict `ttstr` label probe。Eye 独有 end check；其它四类 unknown label 均直接
  解引用 end iterator。Eye/Eyebrow/Transition 在调用 controller restore 前复制 Variant，
  Mouth/Selector 则直接借用 raw item，源码已将后二者恢复为 const-reference 边界。四端
  Array Items 与五类 controller deque 的 record/block 布局已完整记录；strict string helper
  已在四份 IDB 命名，入口、hint、容器布局均已注释/书签/保存。syntax-only、3-step Web
  Debug 最终链接与定向 diff check 通过。证据见
  `analysis/motionplayer_collection_child_restore_wrappers_four_binary_2026-08-15.md`。

- Base/OuterForce state snapshot/restore：四端确认 Base 固定
  `coord/scale/color/rotate`、OuterForce 固定 `bust/hair/parts`；七字段 serialize/restore 各自
  复用同一 hint。restore 只做 Object type gate，随后以一份跨全部字段存活的 retained
  accessor 执行 flags-0 getter、忽略 HRESULT，并逐 child 即时提交。前三个 Base Var 字段和
  三个 OuterForce Var 字段缺失时静默，Base `rotate` 缺失则在前三项之后进入 Angle 的 Object
  转换异常。源码已恢复 accessor 生命周期和七个 hints；四份 IDB 的 key/hint/入口已命名或
  注释/书签/保存，syntax-only 与 3-step Web Debug 最终链接通过。证据见
  `analysis/motionplayer_base_outerforce_state_snapshot_restore_four_binary_2026-08-15.md`。

- Eye/Eyebrow/Mouth/Transition/Selector collection serialize wrappers：五类均创建 fresh
  native Array 并按 controller deque 顺序发布 item。Eye/Eyebrow/Mouth/Selector 与嵌套
  request queue 使用 `ncbPropAccessor(TJSCreateDictionaryObject(), false)` 直接接管 factory
  reference，逐字段 `SetValue` 后发布 `{dispatch,dispatch}` closure；Transition 独有
  Var-state Variant -> force Object -> accessor retain -> Variant early Clear -> label-last ->
  closure publish 流程。源码已恢复 accessor 所有权、request-pair item 生命周期和
  Transition 的 shared `engineLabel` hint；四端入口已注释/书签/保存，syntax-only 与
  10-step Web Debug 最终链接通过。证据见
  `analysis/motionplayer_collection_child_serialize_wrappers_four_binary_2026-08-15.md`。

- Var / Angle state serializer Dictionary 所有权：四端确认两类函数都由
  `ncbPropAccessor(TJSCreateDictionaryObject(), false)` 直接接管 factory reference，按固定
  七字段顺序忽略 `PropSet` 状态，并在 accessor 析构前返回 `{dispatch,dispatch}` closure。
  Var 为 `frame/prev/target` 各创建一份 fresh Array，以 signed count 按序读取
  `currentValue/startValue/targetValue`；Angle 则写 `currentRad/startRad/targetRad` 标量。
  四份 IDB 中旧 Var 注释曾把 `prev/target` 角色写反，本轮通过 IDAPython 同时替换两种
  function-comment 槽并逐库重新反编译核验；源码已恢复真实 Dictionary owner 协议，
  syntax-only 与 3-step Web Debug 最终链接、定向 diff check 通过。证据见
  `analysis/motionplayer_var_angle_state_serializer_owner_four_binary_2026-08-15.md`。

- Timeline item / Base / OuterForce / 顶层 Engine state Dictionary owner 家族：四端 fresh
  反编译确认四类都以 `ncbPropAccessor(TJSCreateDictionaryObject(), false)` 直接接管
  factory reference，child Variant 逐项 serialize/PropSet/析构，忽略 HRESULT，最后发布
  `{dispatch,dispatch}`。Timeline 直接把 closure append 到 Array，且 map `operator[]` 先于
  item factory；另外三类在 accessor 析构前构造返回 closure。源码已去除这四条路径上的
  owning-Variant + borrowed generic setter 压平，恢复真实 owner/异常结构；16 个 IDB 入口
  已补 owner 注释和 bookmark，syntax-only 与 3-step Web Debug 最终链接、定向 diff check
  通过。证据见
  `analysis/motionplayer_state_dictionary_serialize_owner_family_four_binary_2026-08-15.md`。

- playing-timeline info item Dictionary handoff：四端 fresh 反编译确认该查询使用不同于状态
  serializer 的 owner 协议——先创建原始 owning Dictionary Variant，再 copy/force Object/
  accessor retain/复制临时 early Clear；按 `label/flags/blendRatio` 写入后，Array 复制的是
  原始 Variant，随后释放 accessor 与原 Variant。源码已移除 borrowed raw dispatch 和三份
  跨循环尾存活的 field Variant，恢复逐字段临时量与完整 handoff；HM3 miss 仍在 factory 前
  跳过，PropSet failed HRESULT 仍发布部分 item。四份 IDB 已补注释/bookmark，syntax-only
  与 3-step Web Debug 最终链接、定向 diff check 通过。证据见
  `analysis/motionplayer_playing_timeline_info_dictionary_handoff_four_binary_2026-08-15.md`。

- variable publication Variant reset 生命周期：四端 fresh 反编译确认 metadata reset 尾部
  先用 fresh Array temporary copy-assign `_variableLabelsBase`，立即析构 temporary，再从
  base 成员 CopyRef `_variableLabels`；随后 fresh Dictionary Variant copy-assign
  `_variableFrameLists` 并立即析构，最后才 clear instant-label set 和 variable-range map。
  源码已缩短 Array helper 局部量 scope，去掉跨后续赋值/clear 的额外 Object/ObjThis 引用；
  Dictionary full-expression 与三成员/两容器顺序保持。四份 IDB 已补注释/bookmark，
  syntax-only 与 3-step Web Debug 最终链接、定向 diff check 通过。证据见
  `analysis/motionplayer_variable_publication_variant_reset_lifecycle_four_binary_2026-08-15.md`。

- `buildVariableList` owner / lookup / Array pipeline：四端 fresh 反编译确认 builder 每次同时
  重建 current-label Array 与 frame-list Dictionary，再为输入和新 Dictionary建立全函数
  retained accessor。每个 item/frameList/frame各有 copy/force/retain/early-clear；每个 label
  在 strict Dictionary probe 前 eager 创建 candidate Array，hit 会第二次 flags-0 getter并
  丢弃 candidate，miss 则 label-before-PropSet，普通 HRESULT失败不回滚。源码已恢复 fresh
  Dictionary、双 getter、receiver owners、原 frame Variant publication，并补回共享
  `engineFrameListHint_guess`；四份 IDB 已补注释/bookmark，syntax-only 与 34-step Web
  Debug 最终链接、定向 diff check 通过。证据见
  `analysis/motionplayer_build_variable_list_owner_pipeline_four_binary_2026-08-15.md`。
  2026-08-16 V144又从四端重新提取主函数和调用序列，纠正“源码已完整恢复getter”的过强
  旧表述：portable仍残留raw property wrapper。现已直接恢复root/Dictionary/item/frameList/
  frame五个accessor、2次Count、6次typed GetValue、strict HasValue与SetValue，新增四层
  failure-after-write/reentrant owner和duplicate-label candidate复用probe；四库各24条地址
  注释回读并保存，普通/headless test TU语法、Web 3/3、Wasmtime 4/4与双wasm解析通过。
  详见
  `analysis/motionplayer_build_variable_list_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`。

- `removeVariableLabel_guess` owner / FuncCall pipeline：四端 fresh 字符串 xref、反编译和
  caller xref 确认该 helper 仅有 selector builder 两条调用边与 selector sync 一条调用边；
  它 copy/force 当前 label Variant，建立 retained accessor 后 early Clear 临时 Variant，再用
  独立 `remove` mutable hint、null result、单个 string Variant 参数和 retained dispatch
  ObjThis 调用 `FuncCall`，参数先析构、accessor 后释放，HRESULT 被忽略。源码已移除跨脚本
  调用的裸 dispatch borrow，并将无符号依据的旧名改为 `_guess`；四份 IDB 已回写语义名、
  注释和 bookmark，syntax-only 与 10-step Web Debug 最终链接、定向 diff check 通过。
  证据见
  `analysis/motionplayer_remove_variable_label_owner_funccall_four_binary_2026-08-15.md`。

- `syncSelectorControls_guess` publication / deque compaction：四端 fresh 主函数、caller xref
  与两个单 caller STL clone 确认 fresh Array owner 存活到函数尾，base 成员先发布再复制
  current `std::deque<tTJSVariant>` Items，成功后才置 dirty。disabled entry 使用真正
  `std::remove`，逐项 Variant copy-assign 后故意丢弃 new-end，不 erase、不析构 tail，后续
  entry 继续在未缩短的完整 deque 上运行；enabled/target 分支和重入时逐次读取 selector
  byte 的边界亦已闭合。源码行为原已一致，仅收紧 owner/publication/tail 注释；四库已回写
  两个 `_guess` helper 名、详细注释和 bookmark，syntax-only、10-step Web Debug 最终链接
  与定向 diff check 通过。证据见
  `analysis/motionplayer_selector_sync_publication_deque_compaction_four_binary_2026-08-15.md`。

- `getVariableFrameList` query owner / return handoff：四端 fresh 反编译确认公开 getter先
  copy/force Engine frame-list Dictionary，建立 retained accessor 后 early Clear闭包临时量，
  再以 flags=0、动态 label字符与 label自身 mutable hash hint执行一次 PropGet。HRESULT被
  忽略，输出临时量显式 CopyRef到隐藏返回对象后先析构，最后才释放 receiver dispatch；
  custom dispatch即使失败但写值也会发布。源码已移除跨 getter 的双闭包引用，恢复独立
  dispatch owner和返回 CopyRef顺序；四库已补注释/bookmark，syntax-only、3-step Web
  Debug最终链接与定向 diff check通过。证据见
  `analysis/motionplayer_variable_frame_list_query_owner_return_four_binary_2026-08-15.md`。

- `getVariableRange` Dictionary owner / return handoff：四端 fresh 反编译确认 HM5 hit
  先建立局部 owning Dictionary，copy/force/accessor 后 early Clear 输入副本，以
  EmotePlayer 专属 `min/max` hint 写属性，最后显式 CopyRef 到隐藏返回对象；miss 为
  Player 按值参数额外 CopyRef label。Player valid-range 则直接在隐藏返回 Variant 中
  建 Dictionary，再用另一对 Player 专属 hint 写属性；四个 setter bool 均忽略，失败状态
  不回滚。源码已移除泛型 Dictionary helper、恢复两条不同 owner 链与 label 按值 ABI；
  四库已补注释/bookmark，syntax-only 与 33-step Web Debug 最终链接通过。证据见
  `analysis/motionplayer_variable_range_dictionary_owner_handoff_four_binary_2026-08-15.md`。

- main/diff timeline-label Array owner / return handoff：四端 fresh 反编译确认两个直接
  注册查询体仅源 vector 不同；均建立 fresh owning Array + borrowed native Items，快照
  begin/end 后按序把每个 `ttstr` CopyRef为 type-2 Variant，最后显式 CopyRef Array 到隐藏
  返回对象并析构 helper。libstdc++/libc++ deque块分别为25×20、42×12、204×20、
  341×12；无 reserve、过滤、排序或中间 vector。源码数据流原已一致，本轮补 owner注释
  与 Engine析构后快照独立存活回归；四库补详细注释/bookmark，syntax-only 与 3-step
  Web Debug 最终链接通过。证据见
  `analysis/motionplayer_timeline_label_array_owner_handoff_four_binary_2026-08-15.md`。

- loop/total-frame query miss / value ABI：四端 fresh 反编译推翻旧文档的 throw结论：
  loop HM3 miss拼接完整诊断，调用普通单参数非重要 `TVPAddLog` wrapper，再返回 false；
  total miss静默返回 `0.0`。两者 Engine参数均为按值 `ttstr`，D3D wrapper额外 CopyRef后
  调用并释放；ordered `loopBegin >= 0` 仍固定 -0/NaN边界，pass后 raw发布任意
  `lastTime`。源码已把异常改回日志、恢复按值ABI及EmotePlayer bool返回，回归覆盖
  非插入 miss和负数/NaN/Inf lastTime；syntax-only 与 10-step Web Debug最终链接通过。
  证据见
  `analysis/motionplayer_loop_total_log_miss_value_abi_four_binary_2026-08-15.md`。

- timeline enumeration count/index/flags：四端 fresh 反编译确认三个 count均直接以内联
  vector begin/end差计算并窄化；公开 `tjs_int` 索引按原32位转为Engine `tjs_uint32`，
  负数走普通无符号越界路径。label hit CopyRef，miss所调的四端空字面量构造helper均
  返回null-backed `ttstr`。flags不会对空label短路，仍执行非插入HM3 find，因此若已
  存在空标签节点，越界索引和有效空标签都会返回其flags。源码原行为一致，本轮补精确
  注释和空key回归；旧文档中“越界恒为0”和“A64尾块已修复”两项过时结论已纠正。
  Android A64的101条Engine指令已复核/恢复，但IDA尾块归属缺陷受当前MCP mutation
  surface限制仍显式保留；四库均补注释/bookmark。syntax-only与3-step Web Debug
  最终链接通过。证据见
  `analysis/motionplayer_timeline_enumeration_empty_key_owner_four_binary_2026-08-15.md`。

- timeline play/is-playing/stop：四端 fresh 反编译与UTF-16LE字面量+xref证明 play的
  HM3 miss不抛异常，而与loop query复用同一诊断字面量和普通单参数日志wrapper后
  正常返回。flags bit0会在lookup前先clear active vector，miss不回滚；hit对active
  label执行full-range `std::count`，非first-hit find，再按append→lazy state init→
  controller init→seek(0)提交。is-playing/stop以ttstr backing pointer是否为null分支，
  分别表达“任意active”和clear-all；命名stop仅erase首个匹配。源码已将throw改为
  精确日志+return，并恢复std::count，回归覆盖replace-before-miss与HM3非插入；四库
  已补注释/bookmark，syntax-only与3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_timeline_play_log_commit_vector_four_binary_2026-08-15.md`。

- timeline blend/fade lazy commit：四端 fresh 反编译确认blend setter以非插入HM3
  find开始，miss静默；hit按lazy timeline state init→controller setTarget(value栈副本、
  transition、power、Engine queuing byte)→autoStop double写入提交。getter由D3D按值
  label局部owner内联find，仅node与timelineData均存在时发布float blendWeight为double，
  不要求active。fade-in没有play成功返回门：未知label会由flags=3 play先clear并记录
  一条日志，再继续两次静默blend miss；fade-out不查active，直接setBlend(...,true)。
  源码主体原已一致，本轮补提交顺序注释和fade-in miss回归；四库补注释/bookmark，
  syntax-only与3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_timeline_blend_fade_lazy_commit_four_binary_2026-08-15.md`。

- timeline pass cursor/erase commit：四端 fresh 反编译确认零参数facade直达flush核心；
  核心以HM3 `at`、ordered loop门、parallel bit2→20-frame fade→bit4、instant-only
  flush、普通timeline erase-without-index-increment运行。两份arm64进一步以 `ADD Wn`
  计算cursor+1/循环自增并 `SXTW` 到size_t比较，两份armv7同为32位环绕；源码旧
  版本在加法前提升size_t，在INT32_MAX边界不等价。本轮改为显式uint32环绕→int32→
  size_t符号扩展，回归固定cursor -1从frame0开始、-2跳过普通列表；文档补bit4、
  frame即时写入和最终erase的部分提交/不回滚顺序。四库补注释/bookmark，syntax-only
  与3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_timeline_pass_cursor_erase_commit_four_binary_2026-08-15.md`。

- skip/reset phase owner gate：四端 fresh 反编译确认D3D零参数facade和Motion直接绑定
  都到完整Engine reset；active phase以HM3 operator[]物化，ordered loop分支在phase
  内显式检查blend owner非null才调用reset，non-loop则inclusive lastTime window后
  erase-without-increment。主phase严格为outer-force bust/hair/parts→三组spring re-arm→
  blink→eyebrow→mouth→selector→transition→position/scale/angle/color；selector先于
  transition会即时提交其新排队。源码已把null gate从本地容错helper内提回Engine phase，
  旧文档纠正direct owners与spring groups混写；四库补注释/bookmark，syntax-only与
  3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_skip_reset_phase_owner_gate_four_binary_2026-08-15.md`。

- `animating` filter-set / owner gate：四端 fresh 指令流纠正旧移植与旧文档把
  `timelineData` null门只包住track收集的错误；HM3 hit但data为空时会跳过整个active
  item，活动blend与负`loopBegin`都不参与查询。只有data存在后才收集所有track label，
  再无null guard地解引用blend并检查loop；临时unordered_set持有去重ttstr owner，随后按
  selector→transition→eye→eyebrow→mouth过滤standalone controller，mouth保留双label门。
  源码已恢复前置门并补data空/活动blend/负loop组合回归；四库补注释/bookmark并保存，
  syntax-only与3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_animating_filter_set_owner_short_circuit_four_binary_2026-08-15.md`。

- timeline pre-progress共享余量/ordered erase：四端 fresh 主函数与三个非内联helper
  确认double residual只在active扫描外初始化，loop wrap消费后由后续label继续使用；
  ordered `loopBegin >= 0`使NaN走non-loop，loop尾窗用数值fmax把NaN/负数钳到+0。
  `lastTime`只在non-loop执行且先于autoStop blend解引用，loop timeline只由autoStop
  完成移除。源码已修正每项重置dt、`<0` NaN分支、`std::max` NaN传播、通用lastTime
  和提前blend解引用五个后果；回归覆盖跨label residual、NaN、loop lastTime和空blend
  短路。四库补语义名/类型/注释/bookmark，syntax-only与3-step Web Debug最终链接通过。
  证据见
  `analysis/motionplayer_pre_progress_shared_residual_ordered_erase_four_binary_2026-08-15.md`。

- timeline contribution checked lookup/舍入/caller：四端 fresh 推翻旧文档和源码的
  operator[]物化结论，确认逐active label调用HM3 `at`，较晚miss不插入且不回滚已写
  caller double前缀。flags2在data读取前门控；普通非空track按active→physical-track
  顺序逐项执行float乘法/舍入后提升double，instant/空frame跳过，重复label不去重。
  caller仅为clamp LR/UD与progress HM7→mirror→mode0 bind；三端outline和A64内联/无入边
  clone已区分为optimizer拓扑。源码改checked `at`，回归覆盖skip、duplicate、rounding和
  partial commit；四库补clone语义名/类型/注释/bookmark，syntax-only与3-step Web Debug
  最终链接通过。证据见
  `analysis/motionplayer_timeline_contribution_checked_lookup_rounding_callers_four_binary_2026-08-15.md`。

- timeline window null-data/cursor/routing：四端 fresh确认null timelineData跳过全部track/
  cursor工作但仍无条件提交currentTime；源码旧版直接解引用的崩溃已修。data存在后仍按
  物理track index读取cursor，保留flags4 instant skip与seek compact vector错位；crossing
  固定inclusive `<=`/strict `<`，tail sentinel只推进不dispatch，flags2普通track走内部
  controller，其余走通用setVariable。指令进一步纠正旧文档：64位为FMAX而非FMAXNM，
  NaN四端都传播，只有极端-0呈64位+0/32位-0差异。回归覆盖null-data cursor不变/time
  提交；四库补函数/分支注释和bookmark，syntax-only与3-step Web Debug最终链接通过。
  证据见
  `analysis/motionplayer_timeline_window_null_data_cursor_routing_four_binary_2026-08-15.md`。

- timeline seek cursor-clear/future-action/ordered clamp：四端 fresh确认seek先把cursor
  vector的end重置到begin并保留storage/capacity，随后无null guard解引用timelineData；
  因而null data会留下“逻辑清空、currentTime未提交”的失败前缀。data存在时只扫描tail
  sentinel前的frame，flags4 instant track不追加cursor；每槽先更新lastAction再测试
  `frame.time <= time && next.time > time`，所以目标早于frame0或为NaN仍可重放最后一个
  pre-sentinel action。cursor先追加、action后重放、currentTime最后提交，异常不回滚。
  transition用ordered `raw <= 0 ? +0 : raw`，有限负数及±0归一为+0、NaN传播；源码已
  替换不等价的`std::max`并补vector复用/未来action回归。四库补函数/分支注释和bookmark，
  syntax-only与3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_timeline_seek_cursor_clear_future_action_four_binary_2026-08-15.md`。

- timeline initialization commit/owner lifecycle：四端 fresh重新闭合state/controller两个
  initializer及caller拓扑。state builder先替换decoded-data owner，再逐字段、Track、Frame
  渐进提交，失败不恢复旧data或已emplace的默认/部分对象；它不写flags/currentTime/
  cursors，并同时由play与blend lazy materialization调用。controller initializer仅由play
  调用，先提交flags；bit1关闭时null data安全，打开时无guard读取。空轨/instant轨完整
  保留旧owner/queue/state，普通非空轨才原地清零或新建count=1 owner；新建分支不额外
  setter。源码主体已匹配，本轮补语义注释和选择性owner回归；四库补提交门/skip门注释与
  bookmark，syntax-only与3-step Web Debug最终链接通过。证据见
  `analysis/motionplayer_timeline_initialization_commit_lifecycle_four_binary_2026-08-15.md`。

- `setVariable` router/double-ease/integer conversion：四端 fresh重新闭合Engine HM6→typed
  deque/HM7路由、Primary raw callback、D3D direct façade及五类caller。HM6 hit先用double
  计算第五参数factor并置dirty；value/transition/factor只在index/label/gate通过后才窄化
  float，gate false/default/mouth first-label不读取queuing也不窄化。mouth first-label用
  `FCVTZS W,D`/`VCVT.S32.F64`直接写signed beginFrame，NaN=0、上下溢出饱和；源码已
  移除UB `static_cast<int>`。Primary member14先以double预变换script ease，Engine再变换，
  因而省略/0→power2、2→power4；D3D/Engine direct的2仅→3。raw callback同时恢复
  bad-param-count错误码及label→value→optional参数顺序；旧pass专用伪名和“两caller”
  文档已纠正。四库补raw语义名/类型/注释/bookmark，syntax-only与10-step Web Debug
  最终链接通过。证据见
  `analysis/motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`。

- Primary raw controller setters/native callback ABI：四端 fresh闭合member15–19及共享
  outer-force router，并结合连续member14–19 registrar槽确认callback第四参已是NCBind
  解出的native payload，不是需在body内再查的objthis。五个body恢复BADPARAMCOUNT、必填
  →可选的Variant转换顺序和argc覆盖slot无null容错；ease在double域映射后才窄化。
  color先AsInteger再按低到高字节展开；outer-force临时ttstr跨router调用，router在标签
  比较前窄化x/y，命中bust/hair/parts后才窄化duration/power，未知标签不写dirty。
  源码已统一member14–19 native-instance callback signature并修正setter提交顺序；新增
  callback body argc门、native direct call与NCBind foreign-receiver回归。四库补20个Compat
  语义名/类型及24组注释/bookmark；syntax-only、10-step Web Debug最终链接和diff check
  通过，四库原位保存。
  证据见
  `analysis/motionplayer_primary_raw_controller_setters_four_binary_2026-08-15.md`。

- Primary `clear/contains` typed NCB family：四端 fresh registrar direct member-pointer与
  实际body推翻旧raw callback表。member8是typed two-Variant void wrapper：先建立target/
  fill owner，无条件进入Player，no-motion gate在内层；member10是typed label/double/
  double Boolean wrapper，raw label递归lookup miss短路，命中才读current geometry。源码
  删除两个端口自造compat与INVALIDOBJECT/INVALIDPARAM/default-Void边界，注册恢复
  `NCB_METHOD`；回归覆盖null/wrong receiver、arity、surplus、Void/Boolean result和内层
  no-motion no-op。四库补typed语义名/类型/注释/bookmark；syntax-only、10-step Web
  Debug最终链接和diff check通过，四库原位保存。证据见
  `analysis/motionplayer_emoteplayer_clear_contains_typed_four_binary_2026-08-15.md`。

- Primary #4 `initPhysics` typed landing：四端 wide-string/xref 与 registrar
  fresh disasm 均确认脚本项直接存储 `EmoteEngine::applyMetadata_guess`，成员指针
  adjustment 为零，且与 `draw/unserialize` 共用一 Variant 按值、void 返回的
  typed NCBind Function dispatch；闭合 null receiver、eager result clear、
  argc-before-native-unwrap、surplus-argument、旧 NCBind 多段 Variant copy、core
  reset 后第二 owner copy 及两字成员指针调用。源码已把 core 改回按值参数，删除
  `EmotePlayer::initPhysics` 转发层并直接注册 Engine 成员；新增静态签名和 typed
  边界回归，四 IDB 统一工厂/FuncCall/invoke/copy helper 语义名、注释和书签。
  证据见
  `analysis/motionplayer_init_physics_typed_binding_owner_four_binary_2026-08-15.md`。

- Primary #11/#12 state typed landing：四端 UTF-16LE/xref 与 registrar fresh disasm
  一致确认 `serialize/unserialize` 分别直接存储 Engine snapshot/restore core，成员指针
  adjustment 为零，不存在 EmotePlayer forwarding body。#11 无参 Variant wrapper 的
  membername→receiver→eager result clear→负 argc→native unwrap 顺序已闭合；所有非负
  surplus 均忽略。成功调用先接收 hidden-sret Variant，再 copy-construct 第二临时值，
  仅在 result 非空时 copy-assign，最后按第二临时→返回临时顺序析构；null result 仍完整
  执行 snapshot/临时 owner 链。Android arm64 helper 内部 bool→0/-1，另外三端直接
  0/-1，对外一致。源码删除两个 forwarding 并直接注册 inherited Engine 成员；新增
  静态签名与 typed boundary/owner 回归，四库补语义名、类型、注释和书签。证据见
  `analysis/motionplayer_state_method_typed_binding_owner_four_binary_2026-08-15.md`。
  syntax-only、10-step Web Debug 最终链接和定向 diff check 通过；四份 recovery IDB
  已完成名称/类型/伪代码回读并原位保存。

- Primary #9 `getVariable` typed landing：四端 registrar fresh disasm 一致确认脚本项
  直接存储 `EmoteEngine::getVariable` 与零 member adjustment，不存在 Primary facade
  forwarding body。one-ttstr/double Function 的 membername→receiver→eager result clear→
  argc→native unwrap 顺序、surplus ignore、Variant copy→owned ttstr 原子 retain/release、
  direct Engine member-pointer invocation 及 double→临时 `tvtReal` Variant handoff 已闭合；
  null result 仍执行 core 并释放字符串 owner。源码删除转发层并直接注册 inherited Engine
  成员；新增静态签名和 typed boundary/owner 回归，四库补工厂/FuncCall/invoke/converter
  语义名、类型、注释和书签。证据见
  `analysis/motionplayer_get_variable_direct_binding_typed_owner_four_binary_2026-08-15.md`。
  syntax-only、完整 Web Debug 最终链接和定向 diff check 通过；四份 recovery IDB 已
  完成强制反编译回读并原位保存。

- Primary #2/#5 Engine direct typed landing：四端 registrar fresh disasm 确认
  `frameProgress`/`startWind` 分别直接存储 `EmoteEngine::progress(double)` 与
  `EmoteEngine::setWind_guess(float×5)`，两者 member adjustment 均为零，不存在 Primary
  facade forwarding body。one-double/void 与 five-float/void Function 的共同
  membername→receiver→result clear→minimum argc→native unwrap 顺序已闭合；前者只复制
  param0、AsReal 后保留完整 double，后者按 0..4 依次 copy/AsReal/destruct，再于 Engine
  比较前逐项窄化 float，所有 surplus 均忽略。源码删除两个转发层并显式注册 inherited
  Engine 成员；新增静态签名、typed boundary、zero-step 与相邻 float 精度回归。四库补
  两套 factory/FuncCall/invoke 语义名、类型、注释和书签。证据见
  `analysis/motionplayer_frame_progress_start_wind_direct_binding_typed_precision_four_binary_2026-08-15.md`。
  syntax-only、10-step Web Debug 最终链接和定向 diff check 通过；四份 recovery IDB
  已完成强制反编译回读并原位保存。

- Primary #50–52/#62–69 late Engine direct landing：四端 registrar fresh disasm
  确认 11 个 descriptor 全部直接保存既有 Engine core 与零 member adjustment；#50
  property setter slot 为 null。源码删除这些无状态 facade declaration/body，改为显式
  inherited `Property/Method`；回归静态锁定 Boolean、void、no-arg Variant、ttstr Boolean/
  double、按值 ttstr selector action 等签名，并经真实 Primary adaptor 覆盖 surplus、
  empty Array、miss/no-op 与 result 类型。#70 fresh target 仍是实际 Primary→Player
  `getCommandList` thunk，已作为负对照保留。四库 44 个 descriptor 点补 direct-target/
  adjustment 注释和 16 个分组 bookmark。证据见
  `analysis/motionplayer_primary_late_engine_direct_binding_four_binary_2026-08-16.md`。
  selector 源码签名经完整目标编译纠正为按值 `ttstr`（ABI 仍表现为隐藏地址）；真实
  Emscripten `-fsyntax-only`、10-step Web Debug 最终链接、定向 façade/负对照检查与
  `git diff --check` 均通过，四份 recovery IDB 已完成强制回读并原位保存。

- Primary #53–56 raw timeline compatibility / #59 direct blend getter：四端 UTF-16LE
  string/xref、registrar 与 target fresh 复核确认 #53–56 和 #57/#58 共用 native-instance
  raw-callback descriptor，不是 typed member；#53 flags 可选默认0，#54/#55 label 可省略
  为空，#56 在 inactive 分支只执行 flags=3 play 与 blend0 seed 后提前返回，active 分支
  才读取可选 duration/ease/autoStop 并 target1。#59 则是 one-ttstr/double typed family
  直接保存 Engine getter、adjustment=0，源码签名为按值 ttstr。源码删除五个 synthetic
  façade，恢复四个 raw callbacks 和 #59 direct Method；回归经真实 Primary adaptor 锁定
  argc、result、owner、首次/后续调用、surplus 与 tvtReal 边界。证据见
  `analysis/motionplayer_primary_timeline_raw_callbacks_direct_blend_getter_four_binary_2026-08-16.md`。
  Emscripten syntax-only、10-step Web Debug 最终链接、定向 stale-façade/registration 检查
  与 `git diff --check` 通过；四库目标与 registrar 已强制回读并原位保存。

- Primary #35–49 typed target / exact scale-setter reuse：四端 fresh registrar、stored
  member pointer 与 60 个 target body 确认本段全部为 typed method/property，adjustment
  均为零。#35–38 是实际 Primary→Player affine/camera/root wrapper；#39–44 直接读写
  Engine-sized payload 的 hair/parts/bust double triplet，且 #42–44 property setter
  分别逐字复用 #39/#41/#40 method target，不存在三个 property-only façade；#45–48
  先经 NCBind Boolean 转换，但 native setter 忽略结果并写 true，selector 还总是 sync；
  #49 为 Variant CopyRef getter 与 null setter。源码删除 `set*ScaleProp` 合成层并让
  property 直接注册同一 setter；真实 Primary adaptor 回归覆盖 property 路由及 raw
  double/dirty/metadata 边界。四库补 18 个语义名、64 个 prototype、72 组注释和 16 个
  bookmark，并强制回读、原位保存。证据见
  `analysis/motionplayer_primary_mid_typed_binding_exact_scale_setter_reuse_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、10-step Web Debug 最终链接、定向 stale-façade/registration
  检查及 `git diff --check` 均通过。

- Primary #20–34 typed property / exact alias / Variant publication：四端 fresh registrar、
  stored getter/setter 与 target body 一致确认 15 项均为 typed property，member adjustment
  全为零。#23 `motionKey` 与 #24 `project` 逐字复用同一 getter/setter pair，setter 原生
  参数是 ttstr，故脚本 Variant 先转字符串再以 String Variant copy-assign persistent
  motion-context owner；#29/#31、#30/#32 各自复用同一个 raw-frame getter，不执行
  `Motion.Player` 短名的正数毫秒换算；#34 是 Primary 自身构造 Variant 的 wrapper，递归
  uint32 count 经 signed tjs_int 发布为 tvtInteger。源码删除 project/time 合成 facade，
  修正 motion-context setter 签名和 #34 返回类型；真实 Primary adaptor 回归覆盖字符串
  强制转换、双向 alias、raw-time pair 和 Integer Variant。四库补 28 个语义名、40 个
  prototype、88 组注释和 16 个 bookmark，强制回读并原位保存。证据见
  `analysis/motionplayer_primary_property_alias_typed_variant_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、10-step Web Debug 最终链接与限定 Primary 注册块的定向检查通过。

- D3DEmotePlayer #5–7 shell-only visibility / always-true listener：四端 fresh registrar、
  四个 leaf target、listener `IsVisible` 与相邻 `Draw` 共同推翻端口的 Player visibility
  forwarding。show/hide/property 只读写 shell `+0x30/+0x20` byte，未 load 与 clear 后
  都合法；listener 只在 owner scale 改变时进入 Engine，最后固定返回 true，Draw 也不读
  shell byte。因此该状态脚本可观察、跨 clear 保留但不控制渲染，且可与 Player root
  visibility 保存相反值。源码删除三个 `Player::setVisible` 额外调用，真实 adaptor 回归
  覆盖 pre-load PropSet/PropGet、show/hide、Void result、clear 保留与 loaded shell/Player
  解耦。四库补 20 个语义名、20 个 prototype、32 组注释和 12 个 bookmark，强制回读并
  原位保存。证据见
  `analysis/motionplayer_d3d_visibility_shell_only_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、强制最终 Web Debug 链接和定向 stale-forwarding 检查通过。

- Primary #3 `draw` typed-owner / two-copy Variant lifetime：四端 fresh registrar、stored
  target、wrapper body、one-Variant/void typed adapter 与 `Player_drawCompat` 一致确认注册
  保存真实 Primary wrapper 与零 adjustment；adapter 持有 argv[0] 第一份按值副本，wrapper
  从 Engine-sized payload 取 embedded Player，并为内联 `Player::draw` 再持有一份副本后
  直达 render dispatcher，caller Variant 不变。恢复 membername→receiver→result clear→argc
  →payload unwrap 的错误顺序、surplus 忽略和 Void publication；D3D class-ID 首路由在空
  motion 前就设置 embedded Player sticky byte，作为 typed-owner 回归 oracle。源码删除旧的
  含糊 `Player_draw_NCBWrapper` 标签并补真实 adaptor 回归；四库补 4 个语义名、4 个
  prototype、16 组注释和 12 个 bookmark，强制回读并原位保存。证据见
  `analysis/motionplayer_primary_draw_typed_owner_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only 与 10-step Web Debug 最终链接通过。

- Motion.Player #78 `draw` direct typed entry / source-structure correction：四端 fresh
  registrar、stored target、完整 render body、xref 与 one-Variant/void typed specialization
  一致确认 descriptor 直接保存 `Player::draw(tTJSVariant)` 与零 adjustment；adapter 的
  argv[0] owned copy 就是 draw 参数，body 直接执行 D3D→SLA→ordinary routing，不存在第二
  个 `Player::drawCompat(Variant*)` C++ member。源码将完整 dispatcher 收回 `Player::draw`
  并删除 port-created helper/declaration，同时保留历史差分 trace event 字符串；真实 Player
  adaptor 回归覆盖 member type、receiver/arity/result gate、surplus 忽略、sticky D3D byte 与
  caller Variant 不变。同轮四端 UTF-16LE/xref 还确认 `captureCanvas` 名称只属于
  D3DAdaptor registrar，删除 Player 上未注册且零调用的 `captureCanvasCompat` raw 残留。
  四库将旧 recovery semantic identity 改名为 `Player_draw_guess`，补齐
  one-Variant typed creator/allocate/ctor/FuncCall/invoke 名称、8 个 prototype、16 组注释和
  12 个 bookmark，强制回读并原位保存。证据见
  `analysis/motionplayer_player_draw_direct_typed_entry_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、33-step Web Debug 最终链接、helper 零匹配扫描与限定
  `git diff --check` 均通过。

- Motion.Player #73 `clear` direct typed entry / recursive worker identity：四端 fresh
  registrar、stored target、完整 xref、worker 与 two-Variant/void typed adapter 共同确认
  `Player_drawToLayerRecursive_guess` 本身就是 `clear` descriptor 的零 adjustment member
  target；不存在第二个 `drawToLayerCompat` native member 或 raw shim。其余 native caller
  只有 Primary `clear` wrapper 与 type-3 child self-recursion，各自建立独立 Variant owner。
  源码将 declaration/body/注册/Primary forwarding/递归/测试统一改为未知源码名的
  `drawToLayerRecursive_guess`，并以静态 member-pointer 类型和真实 Function-object 回归
  锁定 typed 边界。四库替换 registrar 旧 compat 注释、补 worker 源结构注释，强制回读
  并原位保存。证据见
  `analysis/motionplayer_player_clear_direct_typed_entry_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、Web Debug 最终链接、旧名零匹配与限定 `git diff --check`
  均通过。

- Motion.Player dead millisecond progress convenience removal：四端 fresh raw wrapper、
  bridge 与 xref 复核确认脚本 wrapper 自身执行 `AsReal * 60/1000` 后直达 frame-unit
  bridge；A64 仅把短 bridge 内联，另外三端保留直接 call。Engine 两个 caller 原生即传
  frame unit，四端都没有额外 range-clamp helper。本地
  `progressMillisecondsCompat_guess` 未注册、零 production caller、仅两处测试使用，且
  虚构负值/60000ms 归零策略，现已删除；测试显式换算后调用
  `progressFrames_guess(nullptr, frameDt)`。四库 wrapper 补 source-structure 注释、强制
  回读并原位保存。证据见
  `analysis/motionplayer_player_progress_dead_convenience_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、Web Debug 最终链接、旧名零匹配与限定 diff check 均通过。

- Motion.Player dead no-argument render conveniences：四端 fresh Player registrar、typed
  draw target/xref 与 UTF-16LE `captureCanvas` string/xref 复核确认 Player 唯一 render
  entry 是 `draw(tTJSVariant)`，其 native caller 只有 Primary wrapper 与 registrar；无参
  `draw()` 仅四处测试调用，`Player::captureCanvas()` 零调用。每端唯一
  `captureCanvas` string 的全部 xref 都属于 D3DAdaptor registrar，真实
  `D3DAdaptor::captureCanvas(Variant)` 保持不变。源码删除两个 Player convenience；
  no-load 回归改走真实 typed draw，prepare smoke 改走显式 differential test hook，静态
  类型检查锁定 Player 不再有 overload。四库补 source-graph 注释、强制回读并原位保存。
  证据见
  `analysis/motionplayer_player_dead_noarg_render_conveniences_four_binary_2026-08-16.md`。
  完整测试 TU syntax-only、Web Debug 最终链接、负/正向 surface scan 与限定 diff check
  均通过。

- D3DEmotePlayer dead unregistered façade removal：四端 fresh UTF-16LE string/xref
  复核确认 `play`、`draw`、`setMirror` 对完整 4 常量 + 54 member D3DEmotePlayer
  registrar 均为零引用；本地三个同名 C++ façade 同样零调用，且不承载任何已注册
  descriptor 或内部调用链。源码删除这三个 declaration/body，并让真实 class object
  回归以 `TJS_MEMBERMUSTEXIST` 锁定三名均返回 `TJS_E_MEMBERNOTFOUND`、失败 result
  保持不变。当时暂留的 `pass(double)` 后来已在独立 pass/progress 纵切面中删除。
  四库 registrar 追加 absence-audit 注释并原位保存；完整测试 TU syntax-only、Web Debug
  最终链接、定向零匹配和 `git diff --check` 均通过。

- D3DEmotePlayer dead `pass(double)` convenience removal：四端 fresh registrar、direct
  target、xref 与函数体共同确认 script `pass()` 直接存储零参数
  `passTimelines_guess`，script `progress(double)` 则存储独立 one-double wrapper；前者
  无条件进入 Engine timeline flush，后者只在 `dt != 0.0` 时进入 Engine progress core，
  二者不存在 native overload 或 forwarding call。源码删除未注册、零 production caller、
  仅三处测试使用的 `pass(double)` 转发 convenience，并把这些测试改走真实
  `progress(double)`；静态 member-pointer 类型与 Function-object arity 回归锁定两条
  script 边界保持分离。证据见
  `analysis/motionplayer_d3d_dead_pass_double_convenience_four_binary_2026-08-16.md`。
  四库两个 target 均已补 source-structure 注释、强制反编译回读并原位保存；测试 TU
  syntax-only、10-step Web Debug 最终链接、旧接口零匹配与限定 diff check 均通过。

- Motion.Player dead wind façade / Engine back-pointer removal：四端 fresh 完整 Player
  registrar 对 `startWind/stopWind` 均为零命中；UTF-16LE literal 的全部 xref 只落在
  Primary 与 D3D 两个真实 registrar。共享 Engine wind core 的完整 xref 集只含 D3D
  start/stop wrappers 和 Primary startWind direct binding，且四端 Engine ctor 都按原生
  Player size 构造、发布 owner 后立即进入 controller 构造，不向 Player 回写 Engine
  `this`。源码因此成组删除未注册、零 production caller 的 Player start/stop、仅为二者
  服务的 `_engineBack` 尾指针及 ctor 安装；wind predicate 测试改为直接调用五-float
  Engine core。证据见
  `analysis/motionplayer_player_dead_wind_facade_backpointer_four_binary_2026-08-16.md`。
  真实 Player adaptor 回归锁定两名均为 `TJS_E_MEMBERNOTFOUND`；四库 registrar/ctor/core
  补注释、强制回读并原位保存，测试 TU syntax-only、33-step Web Debug 最终链接和旧路径
  零匹配扫描均通过。

- D3DEmotePlayer dead copied Player-property façade block：四端 fresh 完整 54 项 registrar
  对 `completionType/chara/motion/motionKey/maskMode/outline/priorDraw/`
  `frameLastTime/frameLoopTime/loopTime` 全部零命中；逐名 UTF-16LE 搜索和完整 xref
  归属只落在 Motion.Player、Motion.EmotePlayer、相关 load/eval data path，及独立
  D3DEmoteModule 的 `maskMode`，没有一条来自 D3DEmotePlayer registrar。本地十八个 inline
  getter/setter 零 D3D caller，仅复制其他类的 API，现已整块删除；真实 class-object absence
  回归扩展到十名。证据见
  `analysis/motionplayer_d3d_dead_player_property_facades_four_binary_2026-08-16.md`。
  四库 registrar 已补 property-absence 注释、强制回读并原位保存；测试 TU syntax-only、
  10-step Web Debug 最终链接、D3D class block 旧名零匹配与限定 diff check 均通过。

- D3DEmotePlayer `getPlayer` provenance correction：四端 UTF-16LE `getPlayer` 与 recovery
  function-name 查询均为零，完整 54 项 registrar 也不含该名；本地九个调用全部位于单测，
  生产 D3D methods 始终走 private unchecked `player()` chain。考虑 unused inline accessor
  可被完全 strip，未武断宣称原始源码绝无同形 helper，而是把无 provenance 的 public API
  拼写降格为 `playerForDifferentialTest_guess()`，明确其仅为端口测试 hook。证据见
  `analysis/motionplayer_d3d_getplayer_test_hook_four_binary_2026-08-16.md`。
  四库 registrar 已补 provenance 注释、强制回读并原位保存；测试 TU syntax-only、
  10-step Web Debug 最终链接、旧名零匹配与限定 diff check 均通过。

- Motion.Player dead `_metadata/_busy` placeholder removal：四端 fresh 完整 92 项 registrar
  均无 `metadata/busy`；唯一 UTF-16LE `metadata` literal 的全部 xref 只属于
  `EmoteObject_init_guess`，`busy` literal 四端全无。ctor/dtor 进一步闭合末段五个连续
  Variant owner：resourceManager、motion context、outline、meshline、tags，64/32 位分别按
  20/12-byte 精确步幅连续构造并逆序析构，不存在第六个 Player metadata owner 或中插 busy
  byte。本地两字段除死 accessor 外零读写，现连同零调用 `CREATESITE (temp)` parent accessor
  一并删除；真实 `_parentPlayer` link 保持不变。证据见
  `analysis/motionplayer_player_dead_metadata_busy_placeholders_four_binary_2026-08-16.md`。
  四库 Player registrar/ctor/dtor 已补边界注释、强制回读并原位保存；测试 TU
  syntax-only、33-step Web Debug 最终链接、placeholder 零匹配与限定 diff check 均通过。

- Motion.Player dead `emoteEdit/_tags` placeholder removal：四端 fresh UTF-16LE 完整搜索
  显示每端唯一 `emoteEdit` literal 的全部 xref 只属于 node-field initializer，用来把 layer
  的 `emoteEdit` Dictionary CopyRef 到 `MotionNode`；完整 92 项 Player registrar 无同名
  member。四端 `getTags` 都 CopyRef 普通 motion 初始化提交的唯一 tag-frame owner，ctor/dtor
  尾部也只存在 resourceManager、motion context、outline、meshline、tags 五个连续 Variant
  槽。本地 `Player::emoteEdit(args)` 零调用、零注册，孤立 `_tags` 除该死写入外零读者，现已
  成组删除；真实 `_tagFrameSourceVariant`、节点 `emoteEditVariant` 与 Engine directEdit
  property 保持不变。证据见
  `analysis/motionplayer_player_dead_emoteedit_tags_placeholder_four_binary_2026-08-16.md`。
  真实 Player adaptor absence 回归扩展到 `emoteEdit`；四库注释、强制回读、原位保存及
  syntax-only、Web Debug 最终链接、零匹配扫描、限定 diff check 均已闭合。

- Motion.Player copied D3DAdaptor bg/caption façade removal：四端 fresh 完整搜索显示
  `removeAllBg/removeAllCaption/registerBg/registerCaption` 每名每端都只有一个
  UTF-16LE literal，全部 code xref 只归 D3DAdaptor registrar；完整 92 项 Player
  registrar 与 recovery function-name 查询均无四名。真正 D3DAdaptor 的 remove 两项是
  typed `void()` nullsub，register 两项则保留五/三参数 Variant/float/bool 转换 wrapper，
  不能删除。本地 Player 四方法零 production caller，两个 Variant vector 无 renderer/getter/
  serializer 消费者，现已连同 draw-cache smoke 的死调用成组删除；真实 Player adaptor
  absence 回归扩展到四名。证据见
  `analysis/motionplayer_player_dead_d3dadaptor_bg_caption_facades_four_binary_2026-08-16.md`。
  四库 Player/D3DAdaptor registrar 注释、强制回读、原位保存与 syntax-only、Web Debug
  最终链接、旧路径零匹配、限定 diff check 均已闭合。

- Motion.Player copied D3DAdaptor state façade removal：四端 fresh Player registrar 与
  function-name 查询均无 `setSize/setClearColor/setResizable/removeAllTextures`；后三名
  每端唯一 UTF-16LE literal 的 registrar xref 全归 D3DAdaptor。`setSize` 因 Layer-like
  动态调用而存在多份/共享 literal，但每端 D3D row 可由局部 xref 精确定址，仍无 Player
  descriptor。本地 Player setSize/setResizable/removeAllTextures 零 caller，setClearColor
  仅一处 smoke 调用，`_width/_height/_clearColor/_resizable` 全无读者；removeAllTextures
  还错误清理 SourceCache 而非 D3D ordered texture map。现删除 Player 四方法、四字段与死
  smoke 调用，真实 D3DAdaptor 状态、typed wrapper 和 map 生命周期不变。证据见
  `analysis/motionplayer_player_dead_d3dadaptor_state_facades_four_binary_2026-08-16.md`。
  四库 registrar 注释/强制回读/保存与 syntax-only、Web Debug 最终链接、零匹配、限定
  diff check 均已闭合。

- Motion.Player copied SourceCache façade removal：四端 fresh `loadSource/clearCache`
  每端各只有一个 UTF-16LE literal；`clearCache` 全部 code xref 只归 SourceCache 与
  ResourceManager registrar，`loadSource` 还被 Player render-source resolver 以
  `sourceCache.loadSource(source, descriptor)` 的 receiver/两-Variant ABI 动态调用，但没有
  Player descriptor 或 Player 同名函数。本地 Player::loadSource 零 caller，其唯一目标
  `SourceCache::loadSourceByName` 也只是无其他 caller 的 Web compatibility/raw-name 旁路；
  Player::clearCache 仅一处 smoke 调用并错误把 SourceCache list clear 与 `_lastCanvas.Clear()`
  耦合。现删除两个 Player façade、by-name helper 与死调用，真实 descriptor cache、RM
  继承重注册与render resolver生命周期不变；V246已另行证明当时误认的lastCanvas字段不存在。证据见
  `analysis/motionplayer_player_dead_sourcecache_facades_four_binary_2026-08-16.md`。
  四库 receiver 注释/强制回读/保存与 syntax-only、Web Debug 最终链接、零匹配、限定
  diff check 均已闭合。

- `SeparateLayerAdaptor` ordered-map native node ABI：四端 fresh resolver、默认插入、
  link/rebalance 与 erase/destroy helper 共同确认容器是
  `std::map<uint32_t, SeparateLayerPayload>`，ordinal 只在 pair key，mapped payload
  直接从 64 位 node `+40` / 32 位 node `+20` 开始；节点大小分别为 `0xD0/0x98`，
  default payload 分别整段 zero-fill `0xA8/0x84`。同时闭合 libstdc++/libc++ map
  object/header 差异、Android arm64 重复 hint 临时分配、三端 lookup-before-allocation、
  先 link/count 后 resolver payload assignment 的非回滚边界，以及 vector1→vector0→
  ttstr→Variant 的逆序析构。源码删除虚构的 mapped ordinal/`.payload` 间接层，改回
  direct payload `operator[]`；回归静态锁定 mapped type，并覆盖重复索引和 unsigned
  key 迭代顺序。四库补语义名、类型、注释和书签。证据见
  `analysis/motionplayer_separate_layer_payload_map_node_abi_four_binary_2026-08-15.md`。
  syntax-only、完整 Web Debug 最终链接和定向 diff check 通过，四份 recovery IDB 已
  完成名称回读并原位保存。

- Primary `fadeInTimeline/fadeOutTimeline` raw callback lifecycle：四端 fresh registrar
  stored callback pointer 与八个回调体确认 member57/58 沿用 native-instance raw ABI，
  外层先做 receiver 解包，body 只做 label 必填 gate；随后依次持有 ttstr label、把
  duration 立即窄化 float、在 double 域完成 ease 分段映射后再窄化 power。fade-in
  缺失状态时以 flags=3 play、零时长置 0 后始终推向 1；fade-out 直接推向 0 并设置
  auto-stop。源码将两个收窄点改为显式局部量，回归覆盖 foreign receiver、argc gate
  与相邻 float 可区分的 double-domain 映射；syntax-only、10-step Web Debug 最终链接、
  定向 diff check 均通过，四份 recovery IDB 已原位保存。证据并入
  `analysis/motionplayer_timeline_control_four_binary_2026-08-11.md`。

- Blink phase-0 wait-position conversion：四端 fresh `EmoteBlinkController_step_guess`
  一致在 enable gate 后以 `FCVTZS W,S` / `VCVT.S32.F32` 把持久 `blinkPos` 转成 signed
  int32，再与 `beginFrame` 精确比较；共同边界是向零饱和，NaN→0、正/负越界分别到
  `INT32_MAX/INT32_MIN`。源码以局部 `_guess` helper 消除裸 C++ cast 的 NaN/越界 UB，
  端到端回归通过 timer commit 覆盖阈值、无穷、NaN、分数与 mismatch。证据见
  `analysis/motionplayer_blink_wait_position_conversion_four_binary_2026-08-16.md`；四库已补
  conversion 注释/书签并原位保存；普通/headless syntax-only、Web Debug 与 Wasmtime Headless
  Debug 完整构建、定向 diff check 均通过。Wasmtime 对象库同时补齐其最终 guest 目标本来已有的
  `cocos2dx::cocos2d` imported-target 编译依赖，使完整 `Mat4` 头路径在 guest objects 阶段可见。

- PrivateMotionGLL Bezier scalar order：四端 fresh basis/tessellator 指令确认四个 cubic basis
  项的逐次 `FMUL` 结合顺序，以及每个 4×4 contribution 严格按 `basisY*basisX`、
  `weight*coordinate`、旧 accumulator `FADD` 执行；四端均没有 FMA。当前生产表达式已吻合，
  仅补防重结合语义注释与位级回归，覆盖 division=3 内点、division=0 NaN、division=-1 空表
  和带强消去 control points 的四个内部 patch 点。证据见
  `analysis/motionplayer_private_motion_bezier_scalar_order_four_binary_2026-08-16.md`；四库注释/书签
  与原位保存、普通/headless syntax-only、Web Debug 与 Wasmtime Headless Debug 完整构建均
  已闭合。

- Dispatch setter exact-zero Boolean return ABI：四端 fresh 检查三个共享 wrapper，确认 byte
  setter 构造 `tvtInteger`、两个 real setter 构造 `tvtReal`，虚调用返回码都以 `== 0` 而非
  `TJS_SUCCEEDED` materialize `bool`；全量 code-xref caller context 均把结果当 statement 丢弃，
  force-visible 镜像因此继续全部 16 次有序增量写入而无非抛异常 early-out。本地三个 helper 已
  恢复 `bool` 返回并由 `TJS_S_OK/TJS_S_TRUE/TJS_E_FAIL`、flags/index/member/hint/objthis/
  Variant 类型回归锁定。证据见
  `analysis/motionplayer_dispatch_setter_boolean_return_four_binary_2026-08-16.md`；四库 compare/caller
  注释、书签、Android arm64 两个旧 `__int64` 返回原型和原位保存均已闭合；普通/headless
  syntax-only、Web Debug、Wasmtime Headless Debug 与定向 diff check 均通过。

- Force-visible `ncbPropAccessor` source identity：四端 fresh 栈布局确认 base/coord/mtx 都是
  vptr+dispatch 的 polymorphic `ncbPropAccessor`；base 由 copied Variant 构造，nested 两层由
  `GetValue<tTJSVariant>(name,Tag,0,null)` 返回临时构造，尾部严格 matrix→coord→base 逆序
  Release。三个 setter 则逐项吻合 `ncbind.hpp` 的 `SetValue<T>` 模板。本地已删除 force-only
  owner/setter 复制层并直接使用 ncbind 类型，exact-zero/ABI 回归转为覆盖真实模板。证据见
  `analysis/motionplayer_force_visible_ncb_prop_accessor_source_identity_four_binary_2026-08-16.md`；
  四库虚表/模板命名、临时链注释、书签与原位保存，以及普通/headless syntax-only、Web Debug、
  Wasmtime Headless Debug、定向 diff check 均已闭合。

- Ground-correction result accessor double-read：四端 fresh worker/helper 证明 callback result 先由
  copied Variant 构造 `ncbPropAccessor`，随后每个坐标调用 `getRealValue(index,0)`：先
  `PropGetByNum(TJS_MEMBERMUSTEXIST)` 并以 `TJS_SUCCEEDED` 接受任意非负状态，probe 值销毁后再
  `PropGetByNum(0)` 取真正转换值；missing 只 probe 一次并返回 0。本地单读已替换为真实 ncbind
  convenience method，回归以可重入 result dispatch 锁定 `{0/0,1/1,2}` 的 flags/index 序列、
  `TJS_S_TRUE` 成功、第二次值和最终析构。证据见
  `analysis/motionplayer_ground_correction_accessor_double_read_four_binary_2026-08-16.md`；四库 helper
  命名/注释/书签/原位保存、普通/headless syntax-only、Web Debug、Wasmtime Headless Debug 和
  定向 diff check 均已闭合。

- Internal workspace dimension accessor source identity：四端 fresh materializer/accurate-SLA
  caller 与模板 helper 确认，每个函数各自只构造一个 target `ncbPropAccessor`，并在同一 owner
  上按 height→width 执行带共享 hint 的 `HasValue(MEMBERMUSTEXIST)` +
  `GetValue<tjs_int>(flags=0)`；probe 接受任意非负状态，第二次返回码被忽略，negative probe
  只读一次并返回 0。本地裸 dispatch helper 已改为借用真实 accessor，回归锁定 objthis/hint/
  flags/失败边界。证据见
  `analysis/motionplayer_internal_workspace_dimension_ncb_accessor_four_binary_2026-08-16.md`；四库
  模板命名/prototype/comment/bookmark 与原位保存已完成；普通/headless syntax-only、Web Debug
  `11/11`、Wasmtime Headless Debug `20/20` 和定向 diff check 均通过。

- `Player::resolveRenderSource` fast-path accessor chain：四端 fresh resolver/helper 确认
  descriptor→color→work 三个 `ncbPropAccessor` owner；blendMode 为一次 hinted named
  `GetValue<tjs_int>`，colors 为无 probe 的四次 indexed `GetValue<tjs_int>`，work dimensions 才是
  hinted `HasValue` + second `GetValue` 双读，正常清理 work→color→descriptor。本地已删除裸
  dispatch 复制层并用真实模板，失败但写值的回归锁定单读/status-ignore/objthis/index 顺序。
  证据见 `analysis/motionplayer_resolve_source_ncb_accessor_chain_four_binary_2026-08-16.md`；四库
  numeric helper 命名/prototype、resolver comment/bookmark、readback 与原位保存已完成；普通/
  headless syntax-only、Web Debug `3/3`、Wasmtime Headless Debug `4/4` 与定向 diff check 均通过。

- Engine state `ncbPropAccessor::GetValue` source identity：四端 fresh helper/caller 证明
  request queue `p0/p1` 是 named `GetValue<float>`、Var controller 三个 channel 是 indexed
  `GetValue<float>`，Base/OuterForce 七个子树是 named `GetValue<tTJSVariant>`；float 收窄发生在
  模板内部，全部 getter status 被忽略且无 probe。本地删除三个 raw-dispatch 手写展开并直接
  使用真实模板，失败但写值的回归锁定 Variant/float、hint/index/objthis 和单读边界。证据见
  `analysis/motionplayer_engine_state_ncb_getvalue_source_identity_four_binary_2026-08-16.md`；四库
  helper 命名/prototype/comment/bookmark/readback 与原位保存已完成；普通/headless syntax-only、
  Web Debug、Wasmtime Headless Debug 和定向 diff check 均通过。

- NodeTree `ncbPropAccessor` source identity 与 transform 双读：四端 fresh initializer/
  recursive/whole-builder 证明 motion-content、layers、逐层对象、transform 数组和 stencil
  list 均是具有 vptr+dispatch 的真实 accessor，ResourceManager 则保持独立 raw retained
  dispatch。端口删除七个 plugin-local raw getter 展开，恢复 typed `GetValue`、`HasValue`
  和 `GetArrayCount`；`transformOrder[index]` 修正为 `getIntValue(index,0)`，回归锁定
  `TJS_S_TRUE` probe、存在项双读/第二值胜出、第二 HRESULT 忽略、缺项单读/default 0 和
  objthis。证据见
  `analysis/motionplayer_node_tree_ncb_accessor_source_identity_four_binary_2026-08-16.md`；四库
  helper 重命名/prototype/comment/bookmark/readback 与原位保存已完成；普通/headless
  syntax-only、Web Debug、Wasmtime Headless Debug 和定向 diff check 均通过。

- Eye/Blink、Eyebrow、Mouth metadata `ncbPropAccessor` source identity：四端 fresh 三组
  constructor 证明输入 Variant copy 后构造 root accessor，edge/node 及逐项 pair/row 继续
  构造独立 nested accessor；源 Variant 在 accessor 接管引用后立即析构，迭代 owner 逐项释放，
  node→edge→root 在构造尾逆序 Release。三个类共享 beginFrame/endFrame/blink 参数/edge/node
  八槽 hint family；Mouth 只用 slot 0，Eyebrow 用 0/6/7，Blink 用全部。所有 `GetValue` 都忽略
  getter HRESULT，`GetArrayCount` 读取 `count` 属性而非 `GetCount()`。本地已恢复真实 ncbind
  类型和共享 hints，失败但写值的 probe 锁定转换、flags/hint/objthis/调用顺序。证据见
  `analysis/motionplayer_emote_controller_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库命名/comment/bookmark/readback 已完成，普通/headless syntax-only、Web Debug、Wasmtime
  Headless Debug、最终 wasm 解析和定向 diff check 均通过。

- Simple/BustChain spring metadata `ncbPropAccessor` source identity：四端 fresh 两组 ctor
  证明输入 Variant copy 后构造 root accessor；simple 读取五个 hinted real，BustChain 读取
  八个 hinted scalar，并让 `length/scale_x/scale_y` 各构造顺序 nested accessor、读取 index
  0/1 后立即 Release。两个 ctor 共享 gravity/scale_x/scale_y hint，BustChain 另有七槽私有
  family 并复用 controller-state length 槽；所有 typed getter 忽略 HRESULT，real 先按 double
  转换再收窄 float。本地已恢复真实 ncbind owner/作用域/共享 hints，失败但写值 probe 锁定
  flags、hint、index、objthis 与值流。证据见
  `analysis/motionplayer_spring_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库命名/comment/bookmark/readback 已完成，普通/headless syntax-only、Web Debug `34/34`、
  Wasmtime Headless Debug `66/66`、最终 wasm 解析和定向 diff check 均通过。

- Eye/Eyebrow/Mouth builder `ncbPropAccessor` source identity：四端 fresh 三组 builder 证明
  copied control Variant 构造循环外 root accessor；每轮 indexed `GetValue<tTJSVariant>` 保留
  独立 source element，再由第二份 Variant copy 构造 element accessor。controller ctor 接收前者，
  `enabled/label/talkLabel` 由后者读取；公共尾部严格 accessor→source，循环尾再释放 root。
  三 builder 共享 enabled/label hint，Mouth 另有 talkLabel hint；所有 getter HRESULT 均被忽略。
  本地已恢复真实 accessor/source 作用域，probe 在外层 getter 内放弃 element owner，并以失败但
  写值锁定 count/index/named value、hint、objthis、调用顺序和恰好一次析构。证据见
  `analysis/motionplayer_leaf_controller_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/readback 与原位保存已完成，普通/headless syntax-only、
  Web Debug `3/3`、Wasmtime Headless Debug `4/4`、最终 wasm 解析和定向 diff check 均通过。

- Transition builder `ncbPropAccessor` source identity：四端 fresh 独立确认 copied control
  Variant 构造 root accessor，indexed getter 保留 source element，第二份 Variant copy 构造
  element accessor；enabled/label 读取复用 leaf builders 的共享 hint，iteration cleanup 严格
  accessor→source，循环尾再释放 root。raw controller→deque owner、flag=1、sparse type-7 map
  publication 与异常边界保持不变。本地已迁移真实 ncbind 结构，并把失败但写值/source lifetime
  probe 扩到 Transition，锁定 shared hint pointer、flags/index/objthis、调用顺序与恰好一次析构。
  证据见
  `analysis/motionplayer_transition_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/readback/原位保存、普通/headless syntax-only、Web Debug、
  Wasmtime Headless Debug、最终 wasm 解析和定向 diff check 均已闭合。

- Selector builder nested `ncbPropAccessor` source identity：四端 fresh 独立恢复 root selector
  accessor、retained outer element source+accessor、direct-temporary optionList accessor，以及逐项
  direct-temporary option accessor。selector label先于 enabled；option tail为 label→accessor，
  enabled selector tail为 optionList accessor→moved-from vector→selector label→element accessor→
  outer source，循环尾再释放 root。label/enabled复用 Engine-wide hints，optionList/offValue/
  onValue拥有三个 Selector-only hints；off/on是模板内完成 real→float收窄的 typed getter。
  本地已恢复完整 nested owner层级，失败但写值的两层 array/element probe锁定 source lifetime、
  flags/index/objthis、read order、shared/distinct hints和 float值流。证据见
  `analysis/motionplayer_selector_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/paginated readback/原位保存、普通/headless syntax-only、
  Web Debug、Wasmtime Headless Debug、最终 wasm解析和定向 diff check均已闭合。
- Loop builder nested `ncbPropAccessor` source identity：四端 fresh 恢复 copied-input root accessor、
  retained outer element source+accessor、direct-temporary transitionList accessor和逐帧 direct-temporary
  frame accessor。enabled复用 Engine-wide hint；transitionList/var_loop拥有两个 Loop-only hint；每帧
  三项走无 hint 的 indexed `GetValue<tjs_real>`，再由 caller窄化 float。portable实现保持 raw
  controller pre-emplace owner gap、12-byte keyframe、var_loop publication与 type-3 sparse metadata
  index不变；失败但写值的多层 dispatch probe锁定 owner、read order、flags/index/objthis和 hint
  identity。证据见
  `analysis/motionplayer_loop_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/readback/原位保存、普通/headless syntax-only、Web Debug、
  Wasmtime Headless Debug、最终 wasm解析、定向旧 getter/旧地址扫描和限定 diff check均已闭合。
- Clamp builder `ncbPropAccessor` 与共享 hint：四端 fresh 恢复 copied-input root accessor、retained
  outer metadata source+accessor，以及 append后严格 `{type,var_lr,var_ud,min,max}` 的 typed reads。
  enabled/type复用既有 Engine-wide slots；var_lr/var_ud由 Bust/Chain/Clamp共享；min/max跨文件复用
  EmotePlayer HM5 range Dictionary setter的同一 pair。portable实现集中公共 range hints、删除错误的
  EmotePlayer-local pair，并保持 default append commit、partial entry no-rollback和 entry逆析构不变；
  失败但写值 probe锁定 source lifetime、read order、flags/objthis和值流。证据见
  `analysis/motionplayer_clamp_builder_ncb_accessor_shared_hint_four_binary_2026-08-16.md`；四库
  comment/bookmark/双函数 force-recompile/readback/原位保存、普通/headless syntax-only、Web Debug、
  Wasmtime Headless Debug、wasm解析、定向扫描和限定 diff check均已闭合。
- Mirror builder nested `ncbPropAccessor` source identity：四端 fresh 恢复 copied-input root
  accessor与 `variableMatchList` direct-temporary nested accessor；sole named read继续使用独立
  process-wide hint。Count只快照一次，每项直接 indexed `GetValue<ttstr>` 后 append，getter写值后
  的失败 HRESULT被忽略；builder不 clear/filter/dedup/cache-invalidate，partial vector prefix不回滚，
  尾部严格 nested list→root accessor。可重入 owner-drop probe锁定 exact objthis、count/index、
  source lifetime和恰好一次逆序析构。证据见
  `analysis/motionplayer_mirror_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/readback/原位保存、普通/headless syntax-only、Web Debug、
  Wasmtime Headless Debug、wasm解析和定向扫描均已闭合。
- Bust builder nested `ncbPropAccessor` 与共享 vec3 helper：四端 fresh 恢复 loop-wide root accessor、
  retained outer source+metadata accessor、direct-temporary param accessor，以及每次 op/p/pv调用内
  copied-Variant vec3 accessor。param/op/p/pv/ofs/baseLayer由 Bust/Chain共享，var_lr/var_ud与 Clamp
  共享；vec3 x/y还与 shape-anchor共享、z为 helper-only。portable保留 raw spring owner gap、append
  commit、sparse dual publication和 param→metadata accessor→outer source cleanup；失败但写值和
  可重入 owner-drop probe锁定三层 objthis/read order/hint/value/lifetime。证据见
  `analysis/motionplayer_bust_builder_nested_ncb_accessor_vec3_hint_four_binary_2026-08-16.md`；四库 helper
  rename/comment/bookmark/双函数 force-recompile/readback/原位保存、普通/headless syntax-only、Web
  Debug `3/3`、Wasmtime Headless Debug `4/4`和 wasm解析均已闭合。当轮 Chain 只复用共享
  helper；其 outer builder 已由下一个独立纵切面闭合。
- Chain builder nested owner / serialized-role mapping：四端 fresh 恢复 copied-input loop-wide root、
  retained outer source+metadata accessor、direct-temporary param accessor，以及按 bp→p→pv 构造且
  同时存活的三个 indexed accessor。关键旧移植错误已纠正：serialized bp写 internal p、serialized
  p写 internal pv、serialized pv写 internal bp；成功析构严格 pv→p→bp→param→metadata accessor→
  outer source→root。param/op/p/pv/ofs/baseLayer与 Bust共享，var_lr/var_ud与 Bust/Clamp共享，
  bendR/bendS/bp/var_lrm为 Chain-only hint。失败但写值与可重入 storage-drop probe锁定 Count一次、
  exact flags/hint/objthis、六次 indexed次序、三数组共同 lifetime、角色值流、三 key sparse publication
  和逆序析构。证据见
  `analysis/motionplayer_chain_builder_nested_ncb_accessor_role_hint_four_binary_2026-08-16.md`；四库
  comment/bookmark/force-recompile/decompile与 `15/15` comment readback/原位保存、普通/headless
  syntax-only、Web Debug `3/3`、Wasmtime Headless Debug `4/4`与两个 wasm parse均已闭合。
- Instant-variable builder copied root ncb accessor：四端 fresh 恢复 copied-input loop-wide root
  accessor、Count一次快照与 typed indexed `GetValue<ttstr>`；失败但写值的 HRESULT仍消费结果，
  首项可重入清除 caller owner 时 dispatch由 accessor继续保活，返回后恰好析构一次。A64将 indexed
  wrapper内联且 `0x683AD0` 是24-caller Variant→ttstr共享叶子；A32/iOS64/iOS32保留6-caller通用
  template instance，已由过时 `_NodeTree_guess` 统一改名。证据见
  `analysis/motionplayer_instant_variable_builder_ncb_accessor_indexed_ttstr_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/readback/原位保存、普通/headless syntax-only、Web Debug、
  Wasmtime Headless Debug、两个 wasm parse和定向旧路径零命中均已闭合。
- Timeline builder root/nested element ncb accessor：四端 fresh 恢复先清 main/diff、copied-input
  loop-wide root、Count一次快照、typed indexed Variant source、second-copy nested element accessor、
  HRESULT-gated `HasValue(diff)` scratch析构、随后忽略写值后失败HRESULT的bool/label typed reads，
  以及 vector push→HM3 raw owner提交。可重入 probe锁定 root/element/scratch/HM3四段 owner、exact
  flags/hint/objthis/read order与 element accessor→source→最终 root清理。A64内联 indexed/HasValue
  wrapper，其余三端通用helper已逐个 fresh decompile。证据见
  `analysis/motionplayer_timeline_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库 comment/bookmark/force-recompile/decompile/readback/原位保存、普通/headless syntax-only、
  Web Debug `3/3`、Wasmtime Headless Debug `4/4`、两个 wasm parse与旧 raw helper零命中均已闭合。
- Timeline initialization六层 ncb source tree：四端 fresh 恢复 copied rawElement root、direct
  variableList、retained indexed variable+nested accessor、direct frameList、retained rawFrame+
  nested accessor、optional direct content accessor，以及typed getter写值后失败HRESULT仍消费结果。
  修正真实异常前缀偏差：frame Count现在先于Track append；rawFrame read继续先于Frame append。
  完整嵌套probe锁定11次named read、可重入root owner drop与四层恰好一次析构；Count抛出probe
  锁定新TimelineData/blend已提交而Track仍为空。证据见
  `analysis/motionplayer_timeline_initialization_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`；
  四库1+22 comments/bookmark/force-recompile/readback/原位保存、普通/headless syntax-only、Web
  Debug `3/3`、Wasmtime Headless Debug `4/4`、两个 wasm parse、六accessor/两Count/十三typed read
  定向审计均已闭合。

- `visible/setPos/opacity` 三槽共享 hint 家族：四端 fresh UTF-16LE、global xref 与 accurate/assign/
  calc/command/draw decompile 共同确认它紧接十二个 renderer primitive 槽，consumer 矩阵分别为
  `visible={SLA assign,accurate,calcView,shared-D3D draw}`、`setPos={accurate}`、
  `opacity={SLA assign,accurate,calcView,getCommandList}`；后继 `isValid` 只属于 getBounds。
  删除旧移植虚构的 shared-D3D 专用 visible 变量，让 draw 与其它 consumer 共享地址，并给 SLA
  assign 的 visible/opacity target publication 补精确 hint；失败 dispatch 仍不分支/不回滚。
  三槽已在四库重建为独立 `unsigned int`、注释/bookmark/force-recompile/readback/原位保存；
  ordinary/headless syntax-only、Web Debug `57/57`、Wasmtime Headless Debug `90/90`、两份 wasm
  parse/section 审计均通过。证据见
  `analysis/motionplayer_visible_setpos_opacity_hint_family_four_binary_2026-08-16.md`。

- `Player::getBounds/isValid` 共享 hint 与 Dictionary 生命周期：四端 fresh decompile/xref
  确认先判 Y 再判 X；无序只写 `isValid=false`，有序按 left/top/right/bottom/width/height
  发布六个 Real，再按 minX/maxX/minY/maxY classifier 次序写 Boolean。删除旧 bounds-only
  七个重复 cache word，让六个 geometry key 复用既有跨 MotionNode/BezierPatch/SLA/render/
  calc/command consumer 的共享槽，并把唯一 consumer 的 `isValid` 接到 opacity 后继全局。
  Dictionary 现由 `ncbPropAccessor(factory,false)` 直接接管，返回 closure 在 accessor 析构前
  构造。四库重建相关 4-byte data item、注释/bookmark/force-recompile/readback/原位保存；
  ordinary/headless syntax-only、最终 Web Debug `35/35`、Wasmtime Headless Debug `68/68`、
  两份 wasm parse/section 审计通过。证据见
  `analysis/motionplayer_get_bounds_isvalid_shared_hint_lifecycle_four_binary_2026-08-16.md`。

- `Player::initNonEmoteMotion` 的 `parameter` 全局 hint 与分支边界：四端 fresh UTF-16LE、
  global xref、init decompile 与左右邻槽确认它严格位于 `isValid` 后、node-release
  `releaseLayerId` 前；唯一 consumer 只说明调用范围，不改变进程级 storage identity。删除
  `PlayerCore.cpp` 的旧 local hint，新增共享 `parameterMemberHint_guess`；回归探针锁定
  `parameterize -> parameter` 顺序、两个精确 hint、retained objthis/root 和异常逆序析构。
  四库均重建为 size-4 data item、注释/bookmark/force-recompile/readback/原位保存；ordinary/
  headless syntax-only、Web/Headless 最终链接、wasm parse/section 审计通过。证据见
  `analysis/motionplayer_init_parameter_hint_global_boundary_four_binary_2026-08-16.md`。

- 旧树 reset 与 `releaseLayerId/window/piledCopy` 连续 hint：四端 fresh raw-byte/xref/decompile
  闭合 `parameter` 后的三个 4-byte 槽。reset 先固定 retained ResourceManager，后经 child
  invalidation、HM1 reset、非 root live-deque loop，对 layerId1/layerId2 无条件调用并以
  `rawFlag20` 单独门控第三 ID；三次都复用同一 hint、null result、忽略普通 status，最后才
  erase suffix/clear label map。删除 TU-local release hint，并把已有 window/piledCopy 从错误分组
  迁回精确序列；新增 root/零负 ID/owner-clear/failure 探针。四库三槽 size-4 重建、每库13处
  注释、bookmark、三函数重编译/readback/原位保存完成；ordinary/headless syntax-only、Web
  `38/38`、Headless `72/72`、双 wasm parse/section 审计通过。证据见
  `analysis/motionplayer_old_node_reset_release_window_piled_hint_sequence_four_binary_2026-08-16.md`。

- `Player::isExistMotion` 私有 hint、借用 receiver 与参数别名：四端 fresh UTF-16LE、xref、
  wrapper/strict-conversion decompile 确认先构造 `motion/<stealthChara>/<name>` path，之后才严格
  解释 canonical ResourceManager；receiver 不 copy、不 AddRef。两参数为持久 context 成员原址
  与局部 path，flags=0、objthis=receiver、普通 HRESULT 忽略，result 无条件 bool conversion，
  正常按 result→path 析构。四端 hint 只有唯一 consumer，但相对 `piledCopy` 的位置依次为
  `+0xC/+0xC/+0x4/+0xC`，仅 Intel64 偶然相邻，故保留 function-local static 而不扩张八槽
  全局家族。
  源码修正 path/strict-conversion 异常前缀并命名私有 hint；双调用 probe 锁定 hint 复用/全局槽
  隔离、context 原址变更、普通失败 truthy result 与调用期零 AddRef/Release。四库重建 size-4
  backing word、literal/函数/边界注释、bookmark、force-recompile/readback/原位保存完成；
  ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/section 与定向 diff check
  通过。证据见
  `analysis/motionplayer_is_exist_motion_private_hint_borrowed_receiver_four_binary_2026-08-16.md`。

- `Player::random -> ResourceManager::random -> Math.RandomGenerator.random` 共享缓存与 owner：
  四端 fresh UTF-16LE/raw-hit、函数、字段偏移、call-shape 与 hint consumer 复核证明 Player
  和 ResourceManager 两层使用同一个 32-bit process-wide member-hint word；旧源码的两个
  local static 已合并为 `randomMemberHint_guess`。Player 复制 canonical closure、AsObject
  额外保留 raw receiver、回调前销毁 copy、结果转换后 Release；ResourceManager 直接在持久
  generator Variant 上严格取 Object 并借用 receiver，单次调用零 AddRef/Release。两层都忽略
  ordinary HRESULT 并无条件 AsReal。Android 两端各有一份短 range helper 和一份内联完整
  random wrapper 的零引用副本；iOS 均裁剪 range helper。fresh old-address audit 还修复了 A64
  recovery 库中 `Player_random_guess` 仅首条指令被定义的缺口，恢复完整 101 条指令、jump
  table、shared-hint operand 与 17 条 caller 关系。三组 owner/hint/status probe、两套 syntax-only、
  Web/Headless 最终链接、双 wasm parse/section、定向 diff check 均通过；四库 size-4 entity、
  literal/函数/调用注释、bookmark、force-recompile/readback/原位保存完成。证据见
  `analysis/motionplayer_shared_random_hint_owner_lifecycle_four_binary_2026-08-16.md`。

- `Player::calcViewParam` 共享/私有 member-hint identity：四端 fresh decompile/data-xref
  证明旧源码所谓 17 个 calc 专属槽中有 12 个是伪重复；blendMode/originX/originY/
  division/geometry/clip 全部复用现有 process-wide word。真实相邻六槽精确为
  `mbp/invOffset/invMatrix/patch/cmesh/matrix`，其中 patch 又与 getCommandList 共享，clip
  与 MotionNode::findSource 共享。删除 12 个重复声明/定义并把全部 callsite 改回共享槽，
  唯一 patch 定义迁回六槽第四位；真实 fixture 的透明 output-dispatch probe 锁定 12 个
  publication/read hint、flags 与 objthis。四库共 28 个 data boundary 重建为 size-4
  `unsigned int`、注释/bookmark/force-recompile/readback/原位保存完成；ordinary/headless
  syntax-only、Web/Headless 最终链接、双 wasm parse/import/export/section 审计通过，两配置
  CTest 均未注册测试。证据见
  `analysis/motionplayer_calc_view_param_shared_private_hint_identity_four_binary_2026-08-16.md`。

- `Player::playImpl` 的 `type` 共享 hint identity：四端 fresh decompile/xref 证明 retained
  motion-content 的 type getter 不拥有 TU-local backing word，而与 frame parser、
  SeparateLayerAdaptor publication、accurate render、calcViewParam、skipToSync、getCommandList
  共用 `typeMemberHint_guess`。删除错误的 `motionTypeMemberHint_guess`，getter 改为全局共享槽；
  lifetime probe 从“hint 非空”加强为精确指针 identity，同时保留 result owner 与 flags 断言。
  四库 size-4 data item、入口/callsite 注释、bookmark、force-recompile/readback/原位保存完成；
  ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/import/export/section
  审计通过，两份 wasm 相对 V166 均减少 21 bytes 且 import/export 不变。证据见
  `analysis/motionplayer_play_impl_shared_type_hint_identity_four_binary_2026-08-16.md`。

- Timeline decoded-frame `time/content` 独立 hint pair：四端 fresh decompile/xref 证明这对
  相邻 backing word 只被 `EmoteEngine::initializeTimelineState` 使用，与
  `MotionNodeFrameSlot`/`skipToSync` 的同名 `timeMemberHint/contentMemberHint` 地址相距
  `0x190/0x12C/0xA58/0x748`，不能机械合并。源码行为保持不变，仅加固 family 注释；四库
  8 个 size-4 data item、命名、入口/operand 注释、bookmark、force-recompile/readback/
  原位保存完成。证据见
  `analysis/motionplayer_timeline_time_content_distinct_hint_pair_four_binary_2026-08-16.md`。

- Bezier bounds 共享 geometry hint family：四端 fresh decompile/data-ref 证明
  `calcPatchBounds`、`calcMeshBounds` 的六个 publication 与 `reverseCalcBezierPatch` 的四个
  lookup 全部复用插件级 `left/top/right/bottom/width/heightMemberHint_guess`；旧源码六个
  TU-local static 是伪重复。A32/iOS 两端直接渲染全局符号，A64 两个 producer 则渲染为与
  六地址逐一相等的十进制 operands。删除六个 local word、统一 callsite，并补
  `MotionDispatch.h` 显式依赖；回归 probe 清零/恢复六槽，锁定真实 Dictionary publication
  会更新全局槽及 bounds 数值。四库 24 个 data/function boundary 注释、12 个 bookmark、
  force-recompile/readback/原位保存完成；ordinary/headless syntax-only、Web/Headless 最终
  链接、双 wasm parse/import/export/section 审计通过，两份 wasm 对称减少 143 bytes；两配置
  CTest 均未注册测试。证据见
  `analysis/motionplayer_bezier_bounds_shared_geometry_hint_family_four_binary_2026-08-16.md`。

- `clearWholeLayer` 共享 geometry/drawing hint family：四端 fresh decompile/data-xref 证明
  clear helper 的 `width/height` 直接复用全局 geometry pair，`neutralColor` 与
  `Player::buildRenderCommands` 共享，`fillRect` 与 SourceCache bake、alpha-mask、command/
  canvas/accurate-SLA/draw-to-layer 路径共享；旧源码四个 function-local static 均为伪重复。
  两个 native callers 精确为 `meshCopy` 与 `bezierPatchCopy`。删除四槽并统一全局 callsite；
  异常截断 probe 从公共 clear=true wrapper 锁定 getter/call 顺序、flags、receiver、四个 hint
  指针和 `[0,0,width,height,neutralColor]` 参数。四库 8 个 size-4 data item、函数/调用注释、
  bookmark、force-recompile/readback/原位保存完成；ordinary/headless syntax-only、Web/
  Headless 最终链接、双 wasm parse/import/export/section 审计通过，两份 wasm 对称减少 52
  bytes；两配置 CTest 均未注册测试。证据见
  `analysis/motionplayer_clear_whole_layer_shared_drawing_hint_family_four_binary_2026-08-16.md`。

- plural drawing-method 共享 hint pair：四端 fresh UTF-16LE bytes、literal/data xrefs 与
  decompile 证明 debug mesh、public mesh frame、Bezier mesh frame 的 `drawLines` 全部复用
  第一槽；debug Bezier control 与 public Bezier frame 的 `drawBeziers` 全部复用紧邻第二槽，
  且与 Player 单数 `drawLine` 分离。把旧源码两份 lines local 与两份 beziers local 收敛为
  TU 级相邻二槽；临时 Layer-class recorder 锁定两个 public lines paths 的精确指针相等、
  beziers family 内相等且两 family 不等，并覆盖 flags/receiver/二参数。四库 8 个 size-4
  data item、五类函数/operand 注释、每库五 bookmark、20 函数 force-recompile/readback/
  原位保存完成；ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/import/
  export/section 审计通过，产物与 V170 等长且 ABI 表面不变；两配置 CTest 均未注册测试。
  证据见
  `analysis/motionplayer_plural_drawing_method_shared_hint_pair_four_binary_2026-08-16.md`。

- MotionLayer clip quartet 与跨 TU `update` hint：四端 fresh decompile/data-xref/operand
  readback 证明 mesh 与 Bezier-patch renderer 精确复用一组按
  `clipLeft/clipTop/clipWidth/clipHeight` 排列的四槽，紧邻第五槽 `update` 又与 alpha-mask
  compositor 共用，但 alpha-mask 的 clip getter family 仍保持独立。删除两个 renderer
  合计八个 function-local clip cache，建立一个 TU-local quartet；删除两处 local update
  并提升为 `motion::detail` process-global。probe 从两个 public operate 入口进入，在 native
  Layer conversion 前的 `clipHeight` 异常截止点锁定 getter 顺序、flags、receiver、quartet
  内互异、跨 renderer 精确指针相等，并证明 update 为不同的第五槽。四库 20 个 size-4
  data item、三类函数/operand 注释、每库三 bookmark、12 函数 force-recompile/readback/
  原位保存完成；ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/import/
  export/section 审计通过，两份产物相对 V171 对称增加 41 bytes 且 ABI 表面不变；两配置
  CTest 均未注册测试。证据见
  `analysis/motionplayer_motionlayer_clip_quartet_shared_update_hint_four_binary_2026-08-17.md`。

- source descriptor `blank` 读写共享 hint：四端 fresh data-xref 与八份 fresh decompile
  证明 `ResourceManager::findSource("blank/...")` 以 `TJS_MEMBERENSURE` 写 Integer 1，
  MotionNode generic fallback 以 flags 0 读 bool，但二者精确复用同一个 process-lifetime
  4-byte `blankMemberHint_guess`；它位于 originY 与 clip 槽之间，又与 clip 保持独立。
  本地运行时数据流原已一致，本轮补强语义注释，并扩展 retained-owner reentry probe，
  锁定第五个 getter 的 hint 指针等于全局 blank 槽且不等于第六个 clip 槽。四库四个
  size-4 data item、20 处 data/function/operand 注释、12 bookmarks、8 函数 force-
  recompile/readback/原位保存完成；ordinary/headless syntax-only、Web/Headless 最终链接、
  双 wasm parse/import/export/section 审计通过，产物与 V172 等长且 ABI 表面不变；两配置
  CTest 均未注册测试。证据见
  `analysis/motionplayer_blank_source_descriptor_shared_hint_identity_four_binary_2026-08-17.md`。

- MotionNode `findSource` 唯一 hint boundary：四端 fresh data-xref/decompile 与 UTF-16LE
  raw-byte search 证明 `width` 前一独立 4-byte 槽只服务 MotionNode generic fallback；
  `findSource` referenced literal 同时供 ResourceManager NCB registration 使用，但注册表
  不引用 hint data，不能算第二个 hint consumer。保持 `PlayerRender.cpp` TU-local
  process-lifetime storage；补强唯一 native consumer 注释，并在 portable convenience
  wrapper 上锁定连续两次 dispatch 的非空、精确相同 hint 指针。四库 4 个 size-4 data
  item、12 处 data/function/operand 注释、8 bookmarks、4 函数 force-recompile/readback/
  原位保存完成；ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/import/
  export/section 审计通过，产物与 V173/V172 等长且 ABI 表面不变；两配置 CTest 均未注册
  测试。证据见
  `analysis/motionplayer_motionnode_find_source_hint_unique_boundary_four_binary_2026-08-17.md`。

- `x/y` shared hint IDB boundary completion：早期 shared-easing 纵切面已闭合 C++ 与五类
  consumer，但 Android arm64 因旧大 BSS aggregate 只留 offset comment。V175 fresh 复核
  四端 12/18/6/15 xrefs，并对 A64 的 Quad publication、PositionControlCurve root/nested
  reads、shared easing reads、LayerGetter vertex publication、camera-offset publication 全部
  重新反编译，证明 x/y 始终独立传址，无 aggregate/indexed access。四库现统一为 8 个
  size-4 data items；36 处 data/function/operand 注释、12 bookmarks、20 函数 force-
  recompile/readback、四库原位保存完成，旧 `unk_*` 全为零。C++ 已正确，无源码/wasm
  改动；V174 双 wasm 审计继续有效。证据见
  `analysis/motionplayer_xy_shared_hint_idb_boundary_completion_four_binary_2026-08-17.md`。

- MotionLayer `StretchType` static、双 render-manager snapshot 与 shared submit boundary：
  四端 fresh decompile/data/xref/guard/closure 共同证明 mesh 与 Bezier renderer 各自用第一份
  manager 选择 method，只在 method 非空时以显式 null manager 调用共享 submit helper；helper
  解析第二份 manager，用单个 4-byte 函数局部 static ID 加 ABI guard 枚举并设置参数，再由
  捕获第二份 manager 的 callback 执行 `OperateTriangles`。删除旧源码虚构的 manager-identity
  static 和 per-manager re-enumeration，并把 source/target Layer、第一 manager/method、conditional
  update 与 method-gated debug 从 helper 迁回两个外层 renderer；helper 现在原样返回 backend
  Boolean，`stretchType` 仍先收窄为 unsigned short。四库 helper/data/guard 命名、20 comments、
  12 bookmarks、14 函数 force-recompile/readback 与原位保存完成；ordinary/headless syntax-only、
  Web/Headless 最终链接、双 wasm parse/import/export/section、定向结构计数与 diff check 均通过，
  两配置 CTest 仍未注册测试。证据见
  `analysis/motionplayer_motionlayer_stretch_type_static_manager_snapshot_submit_boundary_four_binary_2026-08-17.md`。

- PositionControlCurve `t/s/p` member-hint global topology：四端 fresh evaluator decompile、
  data xref 与相邻 family 复核证明 root `t/s` 是紧随 shared `x/y` 的两个独立 size-4
  process-global words，selected-segment `p` 则是紧邻但不 alias shared `Layer` class hint 的
  另一独立 word；读取顺序固定为 root `x/y/t/s` 后 segment `x/y/p`。把旧
  `PlayerFrameProgress.cpp` 中聚在一起的 `motion::internal` 三槽迁到 `motion::detail`
  shared-global family，并补 root/segment recorder 锁定顺序、flags、objthis、实际指针和
  六槽两两 distinct。四库 12 data items、16 comments、4 bookmarks、4 evaluator force-
  recompile/symbol readback 与原位保存完成；ordinary/headless syntax-only、Web/Headless
  最终链接、双 wasm parse/import/export/section 均通过，产物较 V178 各增加 12 bytes且 ABI
  表面不变；两配置 CTest 仍未注册测试。证据见
  `analysis/motionplayer_position_control_tsp_hint_global_topology_four_binary_2026-08-17.md`。

- SeparateLayerAdaptor shared `absolute` member-hint boundary：四端 fresh data xref、三类
  consumer decompile/disassembly 与 UTF-16 literal 归属复核证明，shared `absolute` 是紧随
  `Layer` class hint 的另一独立 size-4 process-global word；payload-free resolve、payload
  resolve 与 assign rebased target publication 三者复用该地址，而 assign 的 source getter
  保持 null hint。恢复三处 target wiring，并扩展 recorder 锁定一次 assign 的两次 absolute
  writes、第三 ordinal consumer、flags、objthis 及与 hitThreshold distinct 的分界。四库 4 data
  items、20 comments、4 bookmarks、12 consumer force-recompile/readback 与原位保存完成；
  ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/import/export/section、
  CTest 与 diff check 均完成，产物较 V179 各增加 72 bytes 且 ABI 表面不变。构建 cache 的
  `/upstream/...` 错误工具链路径已通过显式 EMSDK 和双 preset reconfigure 恢复。证据见
  `analysis/motionplayer_separate_layer_absolute_shared_hint_boundary_four_binary_2026-08-17.md`。

- SeparateLayerAdaptor shared `hitThreshold` member-hint boundary：四端 fresh xref、两类
  resolve decompile 与三编码 string search 证明，payload-bearing 和 payload-free resolve
  向 target Layer 发布常量 256 时复用同一个独立 size-4 word；assign 尾部没有第三次直接
  publication，headless Player surrogate 也不是 native data consumer。四端相对 absolute 的
  byte gap 为 `0x34/0x24/0x14/0x14`，明确不能由单端 adjacency 外推。恢复两个 target
  wiring，并扩展 recorder 锁定 flags、value、objthis、shared pointer、absolute/hitThreshold
  distinct 与 failure-through 行为。四库 4 data items、16 comments、4 bookmarks、8 consumer
  force-recompile/readback 与原位保存完成；ordinary/headless syntax-only、Web/Headless 最终
  链接、双 wasm parse/import/export/section、CTest 与 diff check 均完成，产物较 V180 各
  增加 75 bytes 且 ABI 表面不变。证据见
  `analysis/motionplayer_separate_layer_hit_threshold_shared_hint_boundary_four_binary_2026-08-17.md`。

- SeparateLayerAdaptor assign `type/left/top` shared-hint boundary：四端 fresh assign
  decompile/disassembly 与既有 global data xref 证明，三个 source getter 都保持 null hint，
  三个 target `TJS_MEMBERENSURE` publication 则分别复用 process-wide type family 与
  geometry family 的 exact word；这是补齐既有 family 的遗漏 consumer，不是新增三个 global。
  恢复三处 target wiring，并扩展 recorder 锁定 source 顺序/flags/null、target exact pointer、
  value、objthis 与 failure-through。四库既有 12 data items 无需重建；16 comments、4 bookmarks、
  4 assign force-recompile、三端 fresh decompile 与 A64 merged-tail disassembly readback及原位保存
  完成。ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/import/export/
  section、CTest 与 diff check 均完成，产物较 V181 各增加 3 bytes 且 ABI 表面不变。证据见
  `analysis/motionplayer_separate_layer_assign_type_left_top_shared_hint_boundary_four_binary_2026-08-17.md`。

- SeparateLayerAdaptor assign 双阶段 source read 与 shared `setSize` method-hint boundary：
  四端 fresh getter-helper decompile 与 A64 merged-tail disassembly 证明，八个 integer member
  均先执行 `MEMBERMUSTEXIST`/null-hint disposable probe，成功后才执行 flags 0/null-hint read；
  probe 负值返回 default 0，第二次 HRESULT 被忽略。target setSize 使用独立 process-wide word，
  四端严格归一为 10 个 direct calls / 7 个 consumer functions，不复用 width/height property
  hints。恢复双读和 SLA method wiring，探针锁定 16-call 顺序/flags/hints、probe value 丢弃、
  第二次 failure-through 及完整 setSize ABI。四库 4 data items、20 comments、4 bookmarks、32
  force-recompile/readback 与原位保存完成；普通/三编码 string search、ordinary/headless
  syntax-only、Web/Headless 最终链接、双 wasm parse/import/export/section、CTest 与 diff check
  均完成，产物较 V182 各增加 101 bytes 且 ABI 表面不变。证据见
  `analysis/motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`。

- SourceCache bake 共享 result Variant 与连续 method-hint family boundary：四端 fresh bake
  decompile/disassembly 证明，`drawLayer/setSize/copyRect/fillRect/operateRect/adjustGamma` 最多六个
  dynamic calls 从函数入口到最后 Layer accessor 之后复用同一个非空 Void result storage，所有
  ordinary HRESULT 均被忽略且失败不会清空前值；render-to-canvas 虽复用 exact `operateRect`
  hint，却明确传 null result，闭合了 hint identity 与 result ABI 的边界。四端另共同恢复
  `drawLayer/setSize/copyRect/operateRect/adjustGamma` 连续五个独立 size-4 process-global words，
  下一 `primaryLayer` 槽为 accessor boundary。源码与 recorder 已锁定 pointer identity、initial
  Void、failure-through/前值可见、exact hints 和槽间 distinct。四库 24 data items、64 comments、
  4 bookmarks、16 force-recompile/readback 与原位保存完成；ordinary/headless syntax-only、
  Web/Headless 最终链接、双 wasm parse/import/export/section、CTest 与 diff check 均完成，产物
  较 V183 各增加 73 bytes，且增量严格只在 CODE section，ABI 表面不变。证据见
  `analysis/motionplayer_source_cache_bake_shared_result_hint_family_boundary_four_binary_2026-08-17.md`。

- command builder `primaryLayer` 按需求值、shared hint 与 raw-owner publication boundary：
  四端 fresh builder/wrapper decompile、call-site disassembly、hint xref 与四编码 string search
  共同证明，common builder 不在入口预取 scratch owner/parent；lazy SLA 首次构造与每个
  `composedLayer == Void` group 各自重新求值 `Window.mainWindow`，再以 strict accessor、
  flags 0、同一个 exact `primaryLayer` word 和非空 Void result 读取属性。普通 HRESULT 被
  忽略，无 type/null 友好恢复。SLA new-expression 保持 allocation 先于 PropGet、ctor-success
  后才发布 Player raw slot、primary temp 清理后立即 begin-pass 的精确顺序；group 则把按需
  owner/primary Variants 交给 shared factory 后 copy-assign 保存槽。源码删除 common builder
  入口两个 resolver 与 compose raw scratch 参数，headless-only execute helper 保持独立边界。
  四库 32 comments、4 bookmarks、8 force-recompile/readback、wrapper semantic rename 与原位保存
  完成；ordinary/headless syntax-only、Web/Headless 最终链接、双 wasm parse/section、CTest 与
  diff check 均完成。产物较 V184 各减少 1,375 bytes，其中 CODE -1,005、name -366、FUNCTION
  -4，ABI 表面与其余 section 不变。证据见
  `analysis/motionplayer_build_render_commands_primary_layer_on_demand_hint_lifecycle_four_binary_2026-08-17.md`。

- render-source/serialized-command `key` shared-hint 读写 family：从四端 primaryLayer 后相邻
  anonymous word 出发，fresh data xref、六 consumer decompile/disassembly 与精确 ASCII/
  UTF-16/UTF-32 search 证明，它是独立的 process-wide `commandKeyMemberHint_guess`，不是
  primary accessor family。`SourceCache::loadSource` 是唯一 flags-0/non-null Variant reader；
  normal builder、canvas、accurate-SLA、getCommandList 与 private-GLL 是五类
  `TJS_MEMBERENSURE` publisher。全部保持 receiver==objthis，reader 忽略 failing HRESULT 并
  保留 dispatch 写值，publisher ordinary status 不截断后续 fields。本地六处生产 wiring 原已
  正确，本轮只补 provenance 注释与读写 ABI/failure-through/pointer-distinct 回归。四库新建
  4 个 size-4 data items、32 comments、4 bookmarks、24 consumer force-recompile/readback 并
  原位保存；ordinary/headless syntax-only、Web/Headless 完整链接、双 Wasm parse/section、
  CTest 与 diff check 均完成。两份产物与 V185 完全相同，所有 section 和 ABI 表面零变化。
  证据见
  `analysis/motionplayer_render_source_key_shared_hint_read_write_family_four_binary_2026-08-17.md`。

- PSB OwnerFilter process-global `std::function` 内部布局、静态生命周期与并发边界：从
  `commandKey` 后继 candidate 出发，四端 fresh BSS word xref、replacement/copy/swap/dtor、
  static-init 与 ResourceManager load 共同证明，LP64/iOS32 的 `key + 4` 是 alignment gap，
  真正后继是同一个 default-empty OwnerFilter。Android/libstdc++ 为 erased buffer + manager +
  invoker，object size 32/16 B；iOS/libc++ 为 inline storage + null/self/heap active-target
  pointer，object size 32/20 B。四端都保持 copy-before-swap 强异常保证、跨全部 manager
  instance 的 process lifetime、replacement/process-exit 析构和无 lock/snapshot 的 data-race
  boundary。本地算法原已正确，只补源码 provenance 与旧报告裁决。四库新建 4 types/4 typed
  data items，恢复 9 helper names、31 comments、4 bookmarks、22 force-recompile/readback 并
  原位保存。两套 syntax-only/full link、Wasm parse/section/import/export、CTest 与 diff check
  均通过；Web/Headless 仍为 85,647,577/84,994,718 bytes、539/538 imports、69 exports，所有
  section size 相对 V186 零变化。证据见
  `analysis/motionplayer_psb_owner_filter_std_function_layout_static_lifetime_four_binary_2026-08-17.md`。

- `D3DAdaptor::clearTargetTexture` 两级 function-local static cache、guard 与异常生命周期：
  从 `g_randomMemberHint_guess` 后继四槽出发，四端 fresh clear/landing decompile、disassembly
  与逐槽 xref 共同证明，它们不是四指针容器，而是 borrowed/raw `FillARGB` method pointer、
  独立 8/4-byte ABI guard、32-bit `color` parameter ID 和第二只 guard。disabled clear 不触达
  任一 guard；method 初始化失败重试 stage 1，color-ID 初始化失败保留 method 并只重试 stage 2；
  两个 trivial statics 无 AddRef/Release/exit destructor。首次成功 clear 执行两次 manager lookup，
  后续每次只执行提交前 fresh lookup；SetParameter 与 target==source OperateRect 之间无本地 lock/
  null guard。本地生产控制流原已正确，仅补 provenance。四库新建 16 typed data items、恢复 3 个
  split landing helper/range 名、25 comments、4 bookmarks、7 function force-recompile/readback 并
  原位保存。ordinary/headless syntax-only、Web/Headless full link、Node parse、section、CTest 与
  diff check 均通过；产物仍为 85,647,577/84,994,718 bytes、539/538 imports、69 exports，所有
  section 相对 V187 零变化。后继 word 已独立归入 `Motion_doAlphaMaskOperation_guess`。证据见
  `analysis/motionplayer_d3d_clear_target_texture_local_static_cache_guard_lifecycle_four_binary_2026-08-17.md`。

- alpha-mask GPU 六方法 cache family、公共可写目标 setup 与异常/所有权边界：从 D3D clear
  后继 cluster 出发，四端 fresh compositor/landing decompile、逐槽 xref 和 entity readback
  共同证明，六条 GPU operation 各自拥有 raw/borrowed method pointer、独立 ABI guard 和无
  guard 的 mutable uint32 compile hint；三个 threshold branch 另有独立 parameter ID/guard。
  nonempty 路径在 mode/op 判定前即完成 source texture 与 software writable-pixel 或 GPU
  reference/writable-target setup，因此 unsupported combination 仍产生 bitmap side effect 并脚本
  `update`。恢复六组精确 software 数学、shader、blend tuple、fresh-manager one-source submit，
  以及 op1 四 strip script clear；method 初始化失败只 abort method guard且不回滚已写 hint，
  param 初始化失败保留 method。六 method 无 AddRef/Release/exit dtor，threshold parameter write
  与 submit 之间无本地锁。portable 删除通用 CPU-only/prevalidation 结构，恢复 6 个独立 GPU
  static branch。四库新建/修正并回读 96 个 typed data items、恢复 iOS 两个 split landing helper、
  注释 Android abort sites、bookmark/recompile/readback 并原位保存。ordinary/headless syntax-only、
  Web/Headless full link、双 Wasm parse/section、CTest 与 scoped diff check 均通过；当前产物为
  85,652,233/84,999,374 B，imports 539/538、exports 69。相对 V188 两端均精确 `+4,656 B`：
  CODE `+4,368`、DATA `+288`，其余 section 与 ABI surface 不变。证据见
  `analysis/motionplayer_alpha_mask_gpu_method_cache_guard_lifecycle_four_binary_2026-08-17.md`。

- shared render-method 两选择器、12 组 branch-local cache 与 manual-null-sentinel 生命周期：
  四端 fresh selector/batch/private-draw decompile、逐槽重建和零值 readback 共同证明，ordinary 与
  alpha-test 是两个独立函数，而非 bool 统一 helper；每个函数有 Add/Sub/Mul/Screen/default/
  default-a 六个分支，共 12 个 raw/borrowed method、12 个 BSS-zero color ID 和 6 个 BSS-zero
  threshold ID，没有 C++ guard、数组或聚合 cache。首次初始化按 method→color→threshold 发布；
  color/threshold 枚举抛出后 method 保持非空，后续不重试且可能使用 BSS 0 ID；null method 在
  EnumParameterID 处故障，若恢复则下次重试 lookup。每次调用仍改写共享 method 参数，alpha-test
  固定 threshold 64，无锁/atomic/TLS、AddRef/Release、exit destructor 或软件 fallback。恢复
  12 个显式 branch-local static，拆分两个 selector，并令 batch 与 Private Draw 按 stencil 路由；
  四库重建/命名 120 个 selector typed data items（另保留 4 个前驱共享指针）、写入 140 comments、
  4 bookmarks、force-recompile/readback 16 个函数并原位保存。ordinary/headless syntax-only、
  Web/Headless full link、Node parse、section/import/export、CTest 与 scoped diff check 均通过；
  当前产物为 85,654,197/85,001,338 B，imports 539/538、exports 69。相对 V189 两端均精确
  `+1,964 B`：FUNCTION `-1`、CODE `+2,229`、DATA `-160`、name `-104`，GLOBAL 与 ABI
  surface 不变。证据见
  `analysis/motionplayer_shared_render_selector_manual_null_cache_lifecycle_four_binary_2026-08-17.md`。

- Point/Circle/Rect/Quad 的 NCB ClassInfo、注册链与 adaptor owner 生命周期：四端 fresh
  static-init/Setup/RegistBegin/End/CreateAdaptor/constructor/destructor 和数据 xref 共同证明，
  四类共享完整 HitData 布局但各有独立 `ncbClassInfo<T>::InfoT + guard`。LP64/32-bit InfoT
  分别为 32/16 B，guard 为 8/4 B；Set/Clear non-owning，Setup 以 classObject 判重复，
  publication、卸载和 CreateAdaptor 均无 lock/atomic/rollback。纠正 V190 尾部的跨链接器
  邻接假设：Android 物理后继确是 geometry state，iOS 同模板静态量被 coalesce 到另一 BSS
  cluster，原物理后继是无关聚合数据。恢复 Motion wrapper→Setup→RegistBegin/End→成员发布、
  单 Void constructor sentinel、0x18/0x0c adaptor、non-sticky owner、idempotent Invalidate/dtor，
  以及 class missing/CreateNew failure/throw/incompatible adaptor 的 native copy 泄漏和
  incompatible script object 仍可返回边界。四库创建 4 types/32 typed data items、完成 80 次
  零值读取、恢复 200 个函数名、180 comments、4 bookmarks、132 force-recompile/readback 并
  原位保存；portable 仅补三处 provenance/owner 注释。双模式 syntax-only/full link、Node parse、
  section、CTest 与 scoped diff check 均通过；Web/Headless 仍为 85,654,197/85,001,338 B、
  imports 539/538、exports 69，全部 section 相对 V190 精确零变化。证据见
  `analysis/motionplayer_geometry_ncb_classinfo_adaptor_lifecycle_four_binary_2026-08-17.md`。

- LayerGetter 的独立 ClassInfo、注册事务、one-pointer facade 与 adaptor publication：四端
  fresh init-array/Setup/RegistBegin/RegistEnd/CreateAdaptor/constructor/producer/destructor 证明，
  它是 geometry 后第五个独立 `ncbClassInfo<T>::InfoT + guard`。LP64/32-bit InfoT 为 32/16 B，
  guard 为 8/4 B；ClassInfo name/classObject 与 facade 内 MotionNode 均是 borrowed，Set/Clear
  无 AddRef/Release、lock 或 rollback。纠正旧报告把 Android arm64 Setup 当 wrapper、把
  RegistBegin 当 static init 的层级混名；恢复 direct constructor node=null/attach-failure delete、
  non-sticky adaptor 只删除 facade、node-tree rebuild 后 dangling，以及 class missing/CreateNew
  failure/throw/incompatible metadata 的泄漏矩阵。特别确认 incompatible metadata 不受 error flag
  控制，仍返回新 script shell，同时泄漏 facade；list 因此只有真正 null 才以 Void 保位。四库
  创建 8 typed data items，统一/回读 70 function entries，写入 78 comments、4 bookmarks，完成
  70 force-recompile 与 20 logical zero-field reads 并原位保存；portable 仅补三处无地址注释。
  双模式 syntax-only/full link、Node parse、section、CTest 与 scoped diff check 均通过；Web/
  Headless 仍为 85,654,197/85,001,338 B、imports 539/538、exports 69，全部 section 相对 V191
  精确零变化。证据见
  `analysis/motionplayer_layer_getter_classinfo_adaptor_publication_lifecycle_four_binary_2026-08-17.md`。

- ObjSource 的独立 ClassInfo、注册事务、script-shell/native 分离发布与 retained-owner 生命周期：
  四端 fresh init-array/Setup/RegistBegin/End/direct-constructor/CreateAdaptor/GetAdaptor/producer/
  destructor 证明，LP64/ILP32 InfoT 为 32/16 B、guard 为 8/4 B，name/classObject 均 borrowed，
  Set/Clear 无 AddRef/Release、lock 或 rollback。纠正旧报告把 Android arm64 Setup 当 Motion
  wrapper、把 RegistBegin 压成 class init、把 Invalidate/完整析构/deleting destructor 混名；
  恢复 shell-first CreateAdaptor 的精确 error 矩阵：negative native type 在 error=true 时抛错，
  default error=false 时仍返回 script shell，support 成功但 null adaptor 更是不受 error 控制。
  ResourceManager 在发布前已构造 0x18/0x0c ObjSource 并 retain PSB owner，故 null/throw/
  incompatible 路径同时泄漏 facade allocation 与 owner retain；direct constructor attach failure
  则完整析构回收。compatible non-sticky adaptor 按 texture→PSB owner 顺序销毁，sticky 只清槽不
  销毁 native。四库创建 8 typed data items，统一/回读 70 function entries，成功写入 80 comments、
  4 bookmarks，完成 70 force-recompile 与 20 logical zero-field reads 并原位保存；portable 仅补三处
  无地址注释。双模式 syntax-only/full link、Node parse、section、CTest 与 scoped diff check 均
  通过；Web/Headless 仍为 85,654,197/85,001,338 B、imports 539/538、exports 69，全部 section
  相对 V192 精确零变化。证据见
  `analysis/motionplayer_objsource_classinfo_adaptor_publication_lifecycle_four_binary_2026-08-17.md`。

- SourceCache 的独立 ClassInfo、注册事务、zero-argument direct attach 与 adaptor teardown：
  四端 fresh static-init/Setup/RegistBegin/End/constructor/Invalidate/destructor 证明，LP64/ILP32
  InfoT 为 32/16 B、guard 为 8/4 B，name/classObject borrowed，Set/Clear 无 AddRef/Release、
  lock 或 rollback。纠正旧报告把 Android arm64 Setup 当 wrapper、把 `CreateEmpty` 统称
  adaptor create、把 Android armv7 多入口 Thumb cluster 压成单一析构函数；确认当前四参考没有
  `CreateAdaptor(existing SourceCache*)` producer，脚本 native 唯一来自 zero-arg value-init/attach，
  ResourceManager 则直接构造 SourceCache base。attach failure 按 list→bufLayer→primaryLayer→owner
  完整回收；non-sticky adaptor 同序 teardown 且不调用 public clearCache，cached Layers 因而不收
  脚本 Invalidate，sticky 只清槽不销毁 external native。四库创建 8 typed data items，统一/回读
  67 function entries，成功写入 77 comments、4 bookmarks，完成 67 force-recompile 与 20 logical
  zero-field reads并原位保存；portable 仅补两处无地址注释。双模式 syntax-only/full link、Node
  parse、section、CTest 与 scoped diff check 均通过；Web/Headless 仍为 85,654,197/85,001,338 B、
  imports 539/538、exports 69，全部 section 相对 V193 精确零变化。证据见
  `analysis/motionplayer_source_cache_classinfo_registration_adaptor_teardown_four_binary_2026-08-17.md`。

- SeparateLayerAdaptor 的独立 ClassInfo、Factory constructor descriptor、shell attach 与 adaptor
  owner gate：四端 fresh InfoT/static-init/Setup/RegistBegin/End/member registrar/factory/ClassID xref/
  vtable 证明，LP64/ILP32 InfoT 为 32/16 B、guard 为 8/4 B，name/classObject borrowed，Set/Clear
  无 AddRef/Release、lock 或 rollback。`Factory(...)` 以动态类名登记 constructor，不是公开的
  `Factory` method；它把 constructor-seen 置真，因此四端虽保留 `-1002` dummy fallback，正常路径
  不发布 dummy。NativeClass 先建 0x18/0x0c `{native=null,sticky=false}` shell adaptor，factory 只取
  arg0/缺省 Void并忽略 surplus；class-ID lookup/attach 任一失败完整 dtor+delete，成功只写 native，
  不写 sticky、也不先清旧槽。全量 xref 排除 existing-native/sticky producer；Player 两处仅消费脚本
  SLA，其 persistent SLA 是独立 raw owner。四库新建 8 typed data items/4 layout types，完成 85 次
  rename、85 signatures、77 comments、4 bookmarks 与 98 force-recompile/readback target 并原位保存；
  portable 只补无地址 provenance/owner 注释。ordinary/headless syntax-only、双最终链接、Node
  parse、section 审计、CTest 与 diff check 均通过；Web/Headless 仍为 85,654,197/85,001,338 B、
  imports 539/538、exports 69，哈希与全部 section 相对 V195 精确零变化。证据见
  `analysis/motionplayer_separate_layer_adaptor_classinfo_factory_registration_four_binary_2026-08-17.md`。
- V197 已完成 `D3DAdaptor` 独立 ClassInfo/Factory shell/owner topology 四架构闭环：确认 InfoT 为
  LP64 32 B / ILP32 16 B、name/classObject 均为 borrowed raw slot，16-row 首行是动态类名构造器而非
  public `Factory` method，constructorSeen 与 dummy `-1002` 的保留/抑制路径、CreateEmpty shell、普通
  Factory 只写 native 且保持 non-sticky、attach failure 返回 `TJS_E_NATIVECLASSCRASH/-1008`；完整 ClassID
  xref 又证明参考插件没有 existing-native/CreateAdaptor/sticky producer，Player 的 process-global shared
  D3D renderer 是独立 raw owner，Motion 另发布 vptr-only subclass item。四库 recovery IDB 共新增/校正
  8 个 typed data item、4 个显式 padding 布局类型、76 个 rename、79 个函数签名、86 条注释、4 个 bookmark
  与 100 个强制反编译目标并全部原位保存；ordinary/headless syntax-only、Web/Wasmtime build、双 wasm Node
  parse、section 审计、CTest 与 diff check 均通过，产物相对 V196/V195 字节级零变化。证据见
  `analysis/motionplayer_d3d_adaptor_classinfo_factory_owner_topology_four_binary_2026-08-17.md`。
- V198 已完成 `Player` 独立 ClassInfo/Setup/CreateAdaptor producer-owner topology 四架构闭环：确认
  InfoT 为 LP64 32 B / ILP32 16 B，borrowed name/classObject 的 first-writer Set、非事务成员前缀发布、
  Motion vptr-only static subclass item、24/12 B empty shell 和 non-sticky teardown；typed constructor
  重入只覆写 native、泄漏旧 Player。exhaustive helper/wrapper xref 证明 existing-native producer 恰好是
  type-3 node 与 type-4 particle 两条 `CreateAdaptor(child,false,false)`；又闭合 CreateNew null、GetAdaptor
  失败却返回 non-null empty shell、正常 populated shell 三态，前两态都不回收 supplied native。四库
  recovery IDB 共回写 8 个 typed data、4 个 ABI layout type、82 个 rename、84 个最终签名、125 条
  comment、4 个 bookmark 与 125 次成功 recompile request（106 unique target）并全部原位保存；两套
  syntax/build、双 wasm parse/section、CTest 与 diff check 均通过，产物相对 V197/V196 字节级零变化。
  证据见
  `analysis/motionplayer_player_classinfo_adaptor_producer_owner_topology_four_binary_2026-08-17.md`。
- V199 已完成 `Motion.EmotePlayer` 独立 ClassInfo/delayed Setup/typed Factory/no-unregister topology
  四架构闭环：确认 LP64 32 B / ILP32 16 B InfoT、borrowed name/classObject、先发布 ClassInfo 再注册
  成员的 prefix-visible Setup、PreRegist 丢弃 Setup bool 后直接把 classObject 写入 Motion 且不分配
  subclass item、无 term callback/正常卸载不 Clear ClassInfo 或移除公开成员，以及 partial-registration
  retry 会重新发布旧 class。四端 outer wrapper 又推翻本地 raw/零参实现：真实形态要求一个 Variant，
  唯一 Void 是 empty-shell sentinel，普通零参 `-1004`，surplus 忽略；Factory 是唯一 payload producer，
  non-sticky attach 只覆写 native，重入会泄漏旧 Engine。源码已改为 typed pointer-return factory并加注册级
  回归。四库 recovery IDB 共回写 8 个 typed data、4 个 ABI layout type、70 个 rename、64 个签名、
  84 条 comment、4 个 bookmark 与 74 次成功 recompile request并原位保存；ordinary/headless syntax-only、
  Web/Wasmtime build、双 wasm parse/section/hash、CTest 与 diff check 均通过。typed wrapper 使两份产物
  精确同增 7,198 B，FUNCTION/CODE/DATA/name 分别同增 `0xC/0x636/0x60/0x157C`，import/export 与
  GLOBAL 不变。证据见
  `analysis/motionplayer_emoteplayer_classinfo_typed_factory_no_unregistration_four_binary_2026-08-17.md`。
- V200 已完成 `Motion` 根独立 ClassInfo/global publication/dormant Unregist topology 四架构闭环，
  并纠正早期根报告把“auto-register 模板生成的 Unregist 虚函数体”写成实际 module unload 的过时
  叙事。四端确认根 InfoT 为 LP64 32 B / ILP32 16 B，RegistBegin 在 23 常量、11 subclass、2 method
  与 `global.Motion` 之前发布 borrowed ClassInfo，异常仍可公开 partial prefix；Unregist wrapper 可按
  正向顺序删除 surface/Clear root 与 subclass ClassInfo，但每端入口都只有 auto-register vtable data
  xref。集成式 LoadModule 只走 Regist 三行并 insert registered set，没有 erase、UnloadModule 或可达
  AllUnregist，因此成功状态保持到进程退出。四库 recovery IDB 共回写 8 个 typed data、4 个 ABI type、
  18 个 rename/签名、60 条 comment、4 个 bookmark 与 47 次成功 recompile request；双 syntax/build、
  Wasm parse/section/hash、CTest 与 diff check 均通过，产物相对 V199 精确零变化。证据见
  `analysis/motionplayer_motion_root_classinfo_dormant_unregistration_four_binary_2026-08-17.md`。
- V201 已完成 `D3DEmotePlayer` 独立 ClassInfo/typed Factory/clone CreateAdaptor/listener-owner/no-unload
  topology 四架构闭环，并系统纠正旧 `D3DImage *` owner 误归。四端确认 InfoT 为 LP64 32 B /
  ILP32 16 B；Factory 要求一个 D3DLayer，唯一 Void 是 empty-shell sentinel，surplus 忽略，raw
  storage 在解箱前分配但由 new-expression EH 回收，成功 attach 为 non-sticky raw overwrite，重入会
  泄漏旧 listener shell。clone 同样要求目标 D3DLayer，只复制 primary，是唯一
  `CreateAdaptor(copy,false,false)` producer；result-null 会泄漏 clone，CreateAdaptor 具有 null /
  non-null empty / populated 三态，前两态不回收 clone，strict converter 的 null 路径还会无保护
  Release。生成 Unregist 虚槽无 integrated loader caller，成功 ClassInfo/process surface 不卸载。
  四库 recovery IDB 共回写 8 个 typed data、8 次 ABI type declaration/update、76 个 rename、77 个
  签名、112 次 comment set/update、4 个 bookmark 与 112 次成功 recompile request并原位保存；双
  syntax/build、Wasm parse/section/hash、CTest 与 diff check 均通过，两份 Wasm 相对 V200 精确零变化。
  证据见
  `analysis/motionplayer_d3d_emoteplayer_classinfo_factory_clone_owner_topology_four_binary_2026-08-17.md`。
- V202 已完成 `global.D3DEmoteModule` 独立 ClassInfo/zero-arg typed constructor/root-map adaptor
  double-owner/no-unload topology 四架构闭环，并纠正把七类 registration bundle 当成 ClassInfo init、
  把类误写成 `Motion.D3DEmoteModule`、以及“root map 唯一 owner”的旧叙事。四端确认 InfoT 为
  LP64 32 B / ILP32 16 B；constructor 对所有非负 argc 成功并忽略 argv，只有恰好一项 Void 是
  empty-shell sentinel，raw attach non-sticky 且重入会泄漏旧 payload。`D3DEmotePlayer.module` 是
  唯一 existing-native producer：result 非 null 时调用 `CreateAdaptor(native,false,false)`，成功后
  root map 与 adaptor 同时 delete 同一 pointer；CreateAdaptor 仍有 null/empty/populated 三态和
  null-release 崩溃边界，module 无 back-pointer，wrapper/root 任一先析构都会留下 UAF/double-free
  窗口。generated Unregist 无 loader caller。四库共回写 8 个 typed data、4 个 ABI type、75 个
  rename/签名、113 条 comment、4 个 bookmark 与 103 次成功 recompile request；新增注册级回归
  覆盖零参、exact-one-Void 和 two-arg-leading-Void surplus。双 syntax/build、Wasm parse/hash/section、
  CTest 与 diff check 均通过，产物相对 V201 精确零变化。证据见
  `analysis/motionplayer_d3d_emote_module_classinfo_constructor_double_owner_four_binary_2026-08-17.md`。
- V203 已完成 `global.D3DLayer` 独立 ClassInfo/raw Factory/concrete adaptor/listener owner/no-unload
  四架构闭环：区分 concrete D3DLayer、D3DLayerBase root view 与 borrowed D3DLayerObject view；
  确认 one-Void empty shell、arg0-only root conversion、surplus、non-sticky attach、attach-failure delete、
  populated re-entry leak，以及 duplicate listener node、remove-all 与 live-iterator UAF 边界。四库保存、
  双 syntax/build/Wasm/CTest/diff 均通过且产物零变化。证据见
  `analysis/motionplayer_d3dlayer_classinfo_factory_adaptor_listener_owner_four_binary_2026-08-17.md`。
- V204 已完成 `global.D3DImage` 独立 ClassInfo/raw Factory/ManagedObjects/concrete adaptor/borrower
  四架构闭环：确认 root arg0-only、one-Void、surplus、fresh set insertion、attach-failure rollback、
  populated re-entry 的 image/set/holder 扩散泄漏、root set non-owning 与 D3DPicture raw borrowing；
  constructor insertion throw 时仅 Android armv7 泄漏 raw storage。四库保存、双验证通过且产物零变化。
  证据见
  `analysis/motionplayer_d3dimage_classinfo_factory_managedset_adaptor_owner_four_binary_2026-08-17.md`。
- V205 已完成 `global.D3DPicture` 独立 ClassInfo/two-argument typed Factory/listener/ranges/adaptor owner
  四架构闭环：确认 outer result-clear 与 exact-one-Void、D3DLayer→D3DImage strict order、surplus、
  attach rollback、populated re-entry 的 listener/range leak、三端 raw-storage EH 与 Android armv7
  `0x40` leak、D3DLayer/D3DImage raw borrower、libstdc++/libc++ vector growth/max_size/clear，以及
  stored-but-unused scale。A64 recovery IDB 还修正了旧 D3DImage set helper吞并 D3DPicture vector grow
  的函数边界。四库原位保存；双 syntax/build、Wasm parse/hash/section、CTest 与 diff check 均通过，
  产物相对 V204 精确零变化。证据见
  `analysis/motionplayer_d3dpicture_classinfo_typed_factory_listener_ranges_adaptor_owner_four_binary_2026-08-17.md`。
- V206 已完成 `global.D3D` 独立 ClassInfo/raw two-argument Factory/concrete adaptor/root containers/
  lifecycle 四架构闭环：确认 LP64 32 B / ILP32 16 B tuple、独立 guard/class ID、first-publication-
  wins/Clear、0xB0/0x70 native-class descriptor、0x18/0x0C non-sticky owner adaptor、one-Void shell、
  raw result preserve、arg0→arg1/surplus、attach-failure deleting-dtor rollback，以及 sticky
  `D3DLayerBase` borrowed view。根内四棵 RB tree 已闭合为 Front/Back pointer multiset、borrowed
  ManagedObjects set、mapped-value-owning Modules map，并恢复 target/texture/module-value/Variant/tree
  的析构顺序和 libstdc++/libc++ header/node teardown 差异。重新审计还推翻旧报告的统一 EH 推断：
  D3D factory 仅 A64 phased cleanup，A32/I64 conversion/ctor escape 泄漏 allocation，I32 conversion
  raw-delete 而 ctor 进入 SJLJ terminate/trap；Android complete dtor 只是 linker folding，iOS 两类保持
  distinct。四库共回写 8 个 ABI type、93 个 rename、77 条 comment、16 个 bookmark 并原位保存；
  源码补 provenance/ownership/EH 注释，测试新增 one-non-Void gate 与 unfilled-shell property crash。
  双 syntax/build、Wasm parse/hash/section、CTest 与 diff check 均通过，产物相对 V205 字节级零变化。
  证据见
  `analysis/motionplayer_d3d_classinfo_raw_factory_root_adaptor_containers_lifecycle_four_binary_2026-08-17.md`。
- V207 已完成 `global.DrawDeviceD3D` 独立 ClassInfo/raw Factory/concrete adaptor/registration/EH/
  destructor identity 四架构闭环，并且没有从 V206 的 D3D 模板实例外推：确认正确 ClassInfo 字段为
  initialized/name/classID/classObject（LP64 32 B、ILP32 16 B）、独立 guard/class ID、0xB0/0x70
  native-class descriptor、0x18/0x0C non-sticky owner adaptor、prefix publication/global-miss no rollback、
  one-Void/result-preserve/business-first/attach rollback。八个 factory 边界分别验证后才确认两 root class
  的目标矩阵相同：A64 phased cleanup，A32/I64 exception leak，I32 conversion raw-delete 而 ctor SJLJ
  terminate/trap。Android 仅 complete dtor ICF，iOS complete/deleting body 均 distinct。V207 同时修正
  V206 报告的 ClassInfo name-first 文字错误并覆盖四库 D3D tuple 注释；四库新增 8 layout type、67 rename、
  58 append comment、4 corrected comment、16 bookmark 与 45 type application，全部原位保存并关闭。
  源码只补 provenance；回归新增 DrawDeviceD3D 对称 raw-descriptor、反向 wrong-shell、empty-shell 与
  surplus attach。双 syntax/build、Wasm parse/hash/section、CTest 与 diff check 均通过，产物相对 V206
  字节级零变化。证据见
  `analysis/motionplayer_drawdeviced3d_classinfo_raw_factory_adaptor_exception_destructor_four_binary_2026-08-17.md`。
- V208 已完成内部 `D3DLayerBase` ClassInfo/PreRegist/SetAdaptor/sticky promotion/failure teardown
  四架构闭环：确认它不是 global script class，却拥有 LP64 32 B / ILP32 16 B 的
  initialized/name/classID/null-classObject tuple；PreRegist 直接 Register 并 first-publish，随后加载
  emoteplayer.dll，最后才注册单-word `D3DLayerObjectNativeInstance` ID，途中失败不回滚 prefix。
  0x18/0x0C adaptor 的 existing populated/native-null/fresh 三态已闭合：native-null 会保留 sticky，
  REGISTER 先于失败返回发布 native且不回滚，root ctor 忽略 bool并严格 GET/置 sticky；REGISTER
  failure 只有在 attachment 不可再 GET 时才导致最终空写。四库写回 8 type、36 rename、32 type
  application、36 comment、16 bookmark，A64 另修复一个前置 BRK 吞并 Invalidate 的函数边界。
  源码把 lazy Find fallback 改回真实 `ncbClassInfo<DrawDeviceObjectBase>` 和直接 PreRegist publication，
  回归确认两个内部 ID 已注册但 global 同名成员不存在。双 syntax/build/Wasm validate/CTest/diff
  均通过；行为性修正使两个 Wasm 相对 V207 同减 449 B，DATA 不变，section delta 同构且已记录。
  证据见
  `analysis/motionplayer_d3dlayerbase_classinfo_preregist_adaptor_sticky_failure_four_binary_2026-08-17.md`。
- V209 已完成 `D3DLayerObjectNativeInstance` 单-word ID/two-field borrowed adaptor/
  `tTJSCustomObject` 固定四槽容器/factory re-entry split-brain/lifecycle 四架构闭环：确认
  LP64 instance/ID arrays 位于 `+0x30/+0x50`、ILP32 位于 `+0x24/+0x34`；REGISTER
  first-empty/no-dedupe、GET oldest-first、满槽/缺失返回 -1、其他 flag 返回 -1002，Finalize/
  dtor 均按 slot 3→0 调 Invalidate/Destruct。普通 D3DLayer shell 的 slot0 concrete、slot1
  borrowed 起始状态和三次重入时间线已闭合：前两次填 slot2/3，第三次以后 borrowed
  registration 失败但被忽略并泄漏；concrete dispatch 看最新代，root add/remove 永远看最旧代，
  旧 concrete generation 又因 slot0 overwrite 独立泄漏。iOS armv7 SjLj landing 还确认异常只清
  base、不回收 raw adaptor。四库写回 8 ABI type、57 rename、20 type application、37 comment、
  16 bookmark 并保存关闭；源码保持既有算法，只补精确注释/static_assert，回归用真实 shell
  连续重入三次验证四槽耗尽仍成功和 remove stale-oldest。双 syntax/build/Wasm/CTest/diff 均通过，
  两个产品 Wasm 与 V208 字节级一致。证据见
  `analysis/motionplayer_d3dlayerobject_borrowed_adaptor_four_slot_container_reentry_lifecycle_four_binary_2026-08-17.md`。
- V210 已完成内建 NCB startup/indexed-vs-registered/`xp3filter.dll`-only eager load 与
  `DrawDeviceD3DZ.dll` dependency alias 四架构闭环：确认 startup 严格执行三条
  `AllRegist(line)` 后只直接 inner-load xp3filter，motionplayer/emoteplayer/DrawDevice 仅被
  索引，等待 `Plugins.link` 或依赖 callback；companion 的唯一 PreRegist 并非空函数，而是
  public-load `DrawDeviceD3D.dll` 并忽略 bool。完整 lazy 图因此为
  `DrawDeviceD3DZ -> DrawDeviceD3D -> emoteplayer -> motionplayer`，异常逐层传播、已完成前缀
  不回滚、各层 marker 只在自身 pipeline 末尾提交。四库写回 12 rename、12 type application、
  24 comment、12 bookmark并保存关闭；源码删除旧 libkrkr2 推断的 motion/emote startup eager
  load、恢复 companion dependency、清除 CMake 旧绝对地址，回归覆盖 companion-first、main-
  already-loaded、mixed-case repeat 与 map miss。ordinary/headless syntax-only、双完整构建、
  Wasm validate/import/export/section、CTest 与 diff check 均闭合；ABI surface 不变，Web/
  Wasmtime 相对 V209 分别减少 112/80 B，差异仅在 CODE/DATA。证据见
  `analysis/motionplayer_internal_plugin_startup_xp3_only_drawdeviced3dz_dependency_four_binary_2026-08-17.md`。
- V211 已完成 script-visible `Plugins.link/unlink/getList`、exact module-key、registered-set
  snapshot 与 Array ownership/EH 四架构闭环：确认 creator 只注册三个 static method；link
  只把首参转 ttstr 后直调 public NCB loader，既不提取 path/改 `.tpm`，也不暴露 loader bool
  或写 result；unlink 保留 conversion 但恒写 1、绝不 erase marker/调用 Unregist；getList 按
  `std::set<ttstr>` comparator 顺序复制 committed lowercase keys，并在 normal/exception 路径
  正确交接/释放 Array。Android libstdc++ node payload 为 LP64/ILP32 `+0x20/+0x10`，iOS
  libc++ 为 `+0x1C/+0x10`，未做跨 ABI 外推。四库写回 16 rename、16 type、24 comment、16
  bookmark并保存关闭；源码先分离 autoload wrapper 与 script exact-key（autoload 的剩余阶段性
  normalization 假设已由 V213 继续纠正），删除四端无命中的 Success/Failed 日志、Emscripten
  hook 和虚假的 d3 flag 尾注/无消费者 transform。
  测试专用 registrar 覆盖 `.tpm`/path miss、uppercase repeat、result preserve、unlink no-op 与
  getList set order。双 syntax/build/Wasm/CTest/diff 闭合；ABI surface 不变，两份产品 Wasm
  相对 V210 对称减少 1,025 B，CODE/DATA 分别同减 `0x36A/0x97`。证据见
  `analysis/motionplayer_plugins_link_unlink_getlist_exact_key_registered_set_four_binary_2026-08-17.md`。
- V212 已完成内建 module 与 `Storages.getPlacedPath/isExistentStorage` namespace isolation、
  resolver ownership/EH 与 NCB container 负向 xref 四架构闭环：确认两个 script method 都只在
  result 非空时调用真实 `TVPGetPlacedPath`，existence 精确等于 returned ttstr non-empty；四端
  registered set/internal map 的完整消费者列表均不含 storage resolver，因此 indexed-only、
  committed marker、callback-prefix failure 都不会合成 `.dll` storage path。A64 旧 IDB 把相邻
  Process/EH 合并，本轮只做保守 entry label/comment/bookmark，不破坏性重切。四库写回 16
  semantic rename、11 type、24 comment、16 bookmark并保存关闭；源码删除 port-only
  HasModule/registered early return、unused helper/log/ncbind include和错误 `0x8EE294` 注释，测试
  probe 分别覆盖 indexed 与人工 committed marker 仍为空/false。双 syntax/build/Wasm/CTest/
  diff 闭合；ABI surface不变，两份产品 Wasm 相对 V211 同减629 B，section delta完全同构。
  证据见
  `analysis/motionplayer_storage_internal_module_visibility_getplacedpath_four_binary_2026-08-17.md`。
- V213 已完成物理 `.tpm` autoload 的目录集合、文件过滤、平台 Name 改写、record 容器、排序、
  count/log 时序与 full joined module-key 四架构闭环：确认 startup 依次扫描 project、`/system`、
  `/plugin`，只接受 `S_IFREG` 且末四字符大小写不敏感等于 `.tpm` 的项；该后缀检查没有短名
  guard。Android 把原始 `.tpm` Name 写入 two-string record，iOS discovery callback 则先改成
  `.dll`；四端只按 Name 做不稳定 `std::sort`，等名项没有 Path tie-break。加载循环先写发现
  count，再记录 `(info) Loading ` + Name，将 `Path + "/" + Name` 原样交给 public NCB loader并
  忽略 bool，绝不提取 basename/storage-name；因此通常不能命中只按 basename 注册的内建 module。
  Android libstdc++ COW record stride 为 LP64/ILP32 `0x10/0x08`，iOS libc++ 为 `0x30/0x18`。
  四库写回 8 semantic rename、4 type、18 comment、8 bookmark并保存关闭；源码删除旧
  `TVPLoadInternalPlugin` normalizer，恢复 Apple-only discovery rewrite，测试覆盖 `.tpm`/`.dll`
  full-path miss。ordinary/headless syntax-only、双完整构建、Wasm validate/import/export/section、
  CTest 与 diff check 均闭合；ABI surface不变，Web/Wasmtime 相对 V212 分别减少 1,387/1,419 B，
  FUNCTION 各减2、CODE/name 对称减少。证据见
  `analysis/motionplayer_physical_tpm_autoload_platform_name_rewrite_full_key_four_binary_2026-08-17.md`。
- V214 已完成 `TVPAutoLoadPluginCount` 的发布时序、重复调用、异常边界、完整 global xref 与
  getter 链接差异四架构闭环：确认 count 是 BSS 零初始化 signed 32-bit found-record snapshot，
  sort 后、empty check/load loop 前一次性写入；它计入同名不同 Path 与后续 miss/repeat/failure，
  并非成功加载数。函数入口不清零，因此 startup/scan/sort 等 pre-publication 异常保留旧值，
  日志/ttstr/loader 等 post-publication 异常则保留本轮完整发现数。Android 保留 raw getter且无
  镜像内 caller；两份 iOS 最终镜像只有 store、无 getter/read xref，与 uncalled getter dead-strip
  相符但未伪造源码平台分支。四端 record stride 对应计数公式为 `0x10/0x08/0x30/0x18`。
  四库写回 10 semantic rename、10 type、14 comment、10 bookmark并保存关闭；源码只补精确
  state-boundary 注释，回归确认 direct public load 不污染 count。双 syntax/build/Wasm/CTest/
  diff 均闭合，产品 Wasm 与 V213 字节级一致。证据见
  `analysis/motionplayer_autoload_count_publication_getter_deadstrip_four_binary_2026-08-17.md`。
- V215 已完成重复 `AllRegist`、append-only borrowed registrar index、非事务异常前缀与静态
  container teardown 四架构闭环：确认每次 line indexing 都从 head 重走并为每个 registrar
  重新分配 list node，绝无 once/clear/dedupe；Android libstdc++ list object/node 为 LP64
  `0x10/0x18`、ILP32 `0x08/0x0C`，iOS libc++ 为 `0x18/0x18`、`0x0C/0x0C` 且显式递增 size。
  重复 K 次后尚未 committed module 的 callback occurrence 扩大 K 倍，执行顺序是 line-major
  `Pre generations -> Class generations -> Post generations`；已 committed module 则由 set guard
  截断但重复节点持续 dormant。索引异常保留已 append prefix，下一次从 head 重加，可造成同行
  和跨行 generation 永久不均。global initializer 先 set 后 map，atexit 逆序 map 后 set；两个
  destructor 只释放 key/tree/list nodes，既不拥有 registrar pointer，也不调用 Unregist。
  四库写回 28 semantic rename、16 type、28 comment、16 bookmark并保存关闭；源码只补精确
  注释，unit fixture 的 once guard 明确为防测试污染而非产品行为。双 syntax、Web 82-step/
  Wasmtime 119-step 全量构建、Wasm/CTest/diff 均闭合，产品与 V214 字节级一致。证据见
  `analysis/motionplayer_ncb_repeated_allregist_append_only_index_static_teardown_four_binary_2026-08-17.md`。
- V216 已完成 `HasModule` final-image absence、internal-map consumer closure 与 source/test
  diagnostic 证据分层四架构审计：四端 map 的完整 direct runtime xref 只有 `AllRegist` builder
  和 inner `LoadModule` lookup，另有 global initializer；function/name 搜索均无 HasModule，不能再
  把当前 header inline helper写成参考 ABI/script surface。端口保留该测试辅助，其 exact、
  non-lowercasing、pure `find` 行为明确只属 source/test contract；回归覆盖 lowercase hit、
  uppercase/path/empty miss及 miss 后不污染原 hit。旧 V210/V212/V215 状态矩阵已改用 binary 可证
  internal-map hit/miss，并标明测试 helper 身份。四库写回 4 inner-loader semantic rename、12
  append comment、8 bookmark，不伪造 HasModule function；双 syntax、Web 82-step/Wasmtime
  119-step 全量构建、Wasm/nm/CTest/diff 均闭合，产品与 V215 字节级一致。证据见
  `analysis/motionplayer_ncb_hasmodule_deadstrip_source_test_diagnostic_four_binary_2026-08-17.md`。
- V217 已完成聚合 `AllUnregist` 的平台 survivorship、虚函数槽与 unload consumer closure 四架构
  审计：两份 Android 最终镜像保留完全相同的 direct top-chain traversal，但均无 caller/xref；
  两份 iOS final image 把聚合入口 dead-strip，只因 registrar vtable 仍保留各类 `Unregist`
  wrapper。Android survivor 按正向 `Pre/Class/Post`、每个静态 registrar 一次执行，不读取
  internal map 或 registered set，因此既不受 V215 duplicate list generations 影响，也不筛选
  loaded module、不 erase marker、不 clear container；若被外部调用，never-loaded registrar 也会
  被调用，异常只留下已执行前缀而无 rollback。`Plugins.unlink`、两个 container destructor 和静态
  teardown 均不消费该路径。四库写回 1 rename、1 type、14 append comment、12 bookmark并保存
  关闭；A64 merged entry 只做保守注释，iOS 不伪造缺失函数。双 syntax、Web 82-step/Wasmtime
  119-step build、Wasm/CTest/diff/IDB session audit 均闭合，产品与 V216 字节级一致。证据见
  `analysis/motionplayer_ncb_allunregist_android_survivor_ios_deadstrip_no_unload_consumer_four_binary_2026-08-17.md`。
- V218 已完成 registrar 静态对象的 pointer-only ABI、trivial destructor、永久 top-chain 与
  mod-init/atexit 交错四架构闭环：module/class name 均是 borrowed `const tjs_char *` literal，base
  仅 `vptr/module/next`（LP64 `0x18`、ILP32 `0x0C`），class registrar 为 `0x20/0x10`，callback
  registrar 为 `0x28/0x14`。四端都没有把 registrar object 传给 `__cxa_atexit`；混合 initializer
  里的 destructor registration 只属于邻接 map/vector/std::function。三个 BSS-zero head 只做
  head-insert，没有 unlink/null；四端又都有 NCB set/map init 之后继续插入的 registrar，证明
  container construction 不是 chain freeze boundary。优化器还可在对象字段写完前发布 head，
  直接闭合原设施 thread-unsafe 的并发边界。四库写回 12 ABI type、8 global type application、
  12 rename、36 comment、24 bookmark并保存关闭；源码加 size/trivial-destructor static_assert，
  禁止未来引入 registrar teardown。双 syntax、Web 82-step/Wasmtime 119-step build、Wasm/CTest/
  hash/section/diff/IDB session audit 均闭合，产品与 V217 字节级一致。证据见
  `analysis/motionplayer_ncb_registrar_pointer_only_trivial_destructor_permanent_top_chain_four_binary_2026-08-17.md`。
- V219 已完成 `TVPRegisterGlobalObject`/`TVPRemoveGlobalObject` 与
  `TVPGetScriptDispatch` owning-reference 合约、null/status/EH 边界四架构闭环：两份 Android
  保留完整 public service 但均无内部 caller；两份 iOS 各自把 getter 的 122 个 unique code
  caller 全量映射/反编译后，确认 generic helper 均被 linker dead-strip，23 个 DeleteMember
  候选严格只是 NCB `UnregistEnd` 同形 wrapper。Register 先构造 nullable object Variant 并对
  nonnull dsp 临时 AddRef，再获取 owning global；它故意无 null-global guard，且只 catch virtual
  PropSet，null name 原样转发。Remove 独有 null-global false 分支，只 catch DeleteMember。两者
  normal/catch 路径都精确平衡 global owner，并以 signed `>=0` 转 bool。四库写回 8 rename、8
  type、20 comment、12 bookmark，保存关闭且不在 iOS 伪造缺失函数；源码只补边界注释，unit TU
  新增 object/null closure、AddRef/Release、positive/negative status、PropSet throw、null name/engine
  回归。双 syntax、Web/Wasmtime 各 24-step build、Wasm/nm/CTest/hash/section/diff/IDB audit 均闭合，
  产品与 V218 字节级一致。证据见
  `analysis/motionplayer_tvp_global_object_service_ownership_null_status_eh_four_binary_2026-08-17.md`。
- V220 已完成 `TVPDoTryBlock` 的 callback/exception state machine、stack description ABI、
  rethrow 与 replacement-exception 四架构闭环：两份 Android 保留入口但无内部 xref；两份 iOS
  均缺失 terminated UTF-16 `eTJS` literal，并分别闭合 286-site/259-function 与
  287-site/243-function `__cxa_rethrow` caller 集后确认 helper dead-strip。正常路径只把
  `tryblock(data)` 放进 try，finally 在 handler 外；异常路径先运行 nullable finally，再构造
  两个 ttstr 的 stack desc（LP64 `0x10`、ILP32 `0x08`）并调用 mandatory catchblock。eTJS
  desc 复制 virtual message，unknown message 留空；callback false swallow，true 以
  `__cxa_rethrow` 保留原对象。finally/desc/catch callback 抛出的新异常传播且不会二次 finally。
  四库写回 2 rename、2 type、32 comment、16 bookmark并保存关闭；源码补精确注释与 two-ttstr
  static_assert，unit TU 覆盖正常/异常顺序、两种 desc、swallow/rethrow identity 及两类 replacement。
  双 syntax、Web/Wasmtime 各 24-step build、Wasm/nm/CTest/hash/section/diff/IDB audit 均闭合，
  产品与 V219 字节级一致。证据见
  `analysis/motionplayer_tvp_do_try_block_callback_exception_state_machine_four_binary_2026-08-17.md`。
- V221 已完成 motionplayer `ttstr` 哈希的 Hint-cache wrapper / core Make 两层边界与
  `LabelValueMap::operator[]` 节点 ABI 四架构闭环：消除“core helper 不访问 Hint”与“容器会缓存
  Hint”的表面冲突，确认前者是纯 UTF-16 32 位 `1025/9/32769` 算法，后者先区分 null backing、
  接受任意 nonzero Hint、仅在 zero 时调用/内联纯 helper，并在 bucket lookup 前把结果或
  `UINT32_MAX` sentinel 写回共享 backing。null-backed `ttstr` 返回 0，而 allocated-empty 与 raw
  null/empty payload 返回 sentinel；复制别名共享同一 Hint。四端同时闭合 hit 不改 Hint/不复制 key、
  miss CopyRef key/value-init `double +0.0`，以及 Android old-libstdc++ `0x20/0x20`、iOS libc++
  `0x20/0x14` 节点布局。四库写回 4 rename、4 type、40 comment、20 bookmark并保存关闭；源码删除
  最后一条旧 `.claude/...libkrkr2` 出处并精确分层，unit TU 新增 alias-cache/raw-null 回归。专用
  ordinary/headless syntax、Web 35-step、Wasmtime 65-step full build、Wasm/CTest/hash/section/diff/
  IDB audit 均闭合，产品与 V220 字节级一致。证据见
  `analysis/motionplayer_ttstr_hash_hint_cache_wrapper_core_make_layer_four_binary_2026-08-17.md`。
- V222 已完成 motionplayer PSB 依赖层七个 raw-node/Variant 边界及最后一批编译测试绝对地址
  注释的四架构复核迁移：fresh 闭合 one-pointer holder Transfer、retained root view、dictionary
  try-get/Contains、string category gate、PSBMedia container replacement 与 resource→Octet copy。
  新确认 try-get hit 是 capture child→release old out→reload/AddRef source owner，完全无 self guard；
  `out==this` 只有另一 holder 保活时安全，sole-owner alias 会进入悬空边界。EnsureContainer 的
  no-slash/load-fail 不提交、adaptor-null 仍提交 Void file+新 name+true、file 先于 name 的 partial
  commit，以及 borrowed stream 与 copied Octet 的相反生命周期也已分层。四库写回 28 rename、
  20 safe type、28 comment、28 bookmark并保存关闭；`psbfile-dll.cpp` 删除 28 个四端绝对地址
  token并注明 alias 测试依赖第二 owner 引用。专用 ordinary/headless syntax、双树 no-work build、
  Wasm/CTest/hash/section/diff/IDB audit 均闭合，产品与 V221 字节级一致。证据见
  `analysis/motionplayer_psb_raw_node_variant_absolute_comment_migration_four_binary_2026-08-17.md`。
- V223 已完成 unordered `ttstr` key 的 cached-hash admission 与 backing-aware equality 四架构
  闭环：node cached hash 先行，只有 hash 命中才执行 backing pointer identity→exactly-one-null
  reject→32-bit Length→UTF-16 payload compare。双 null 与 copied alias 因同 backing 直接相等；
  null-backed 和 allocated-empty 不等并分别 hash 为 0/`UINT32_MAX`，可作为两个 map key；两个
  独立 allocated-empty backing 则 Length=0/payload empty/hash sentinel 均一致，复用同一节点。
  equality 不读取/修复 Hint，因此外部改 Hint 后的 bucket miss 仍属 stable-hash 违约。四库写回
  5 rename、5 type、24 comment、12 bookmark并保存关闭；源码只补 equality 精确注释，unit TU
  新增 same-length mismatch、null/allocated-empty 双向比较和真实 LabelValueMap 双键/复用回归。
  专用 ordinary/headless syntax、Web 35-step、Wasmtime 65-step full build、Wasm/CTest/hash/section/
  diff/IDB audit 均闭合，产品与 V222 字节级一致。证据见
  `analysis/motionplayer_ttstr_equality_backing_null_allocated_empty_bucket_admission_four_binary_2026-08-17.md`。
- V224 已完成 KRKR atlas persistent raw-node、跨 record palette gate、pixel buffer 与 record
  成员布局四架构闭环：枚举结束后 holder 保留最后 icon，第二遍每项先用旧 holder 测 `pal`，
  再 release/assign 当前 record，因此非空 `[r0..rn-1]` 的 mode 序列精确为
  `[pal(rn-1),pal(r0)..pal(rn-2)]`，但 `compress/pixel/pal` payload 均来自赋值后的当前项。
  同时纠正“所有 record 都是 raw/rect/string”的过时结论：Android old-libstdc++ 为
  raw/rect/string，iOS libc++ 为 string/rect/raw；本地按 `_LIBCPP_VERSION` 选择布局，Web DWARF
  已闭合为 size `0x34`、string `+0`、rect `+0x0C`、raw `+0x2C`。signed-low32 allocation、
  unchecked RL、current-pal miss 留未初始化 BGRA、alpha 全零/非正 count 释放并缩 2x2、upload
  后 free 不清 pointer 均保持。AArch64 下游复核还把误落在 post-pack metadata assignment 的
  seed 注释/书签移回真实 `0x693840`，最终四库为 41 comment、21 bookmark并保存关闭；专用双 syntax、
  Web 3-step、Wasmtime 4-step build、Wasm/DWARF/CTest/hash/section/diff/IDB audit 均闭合，两个
  CODE/module 各缩 37 字节且 imports/exports 不变。证据见
  `analysis/motionplayer_krkr_atlas_rotated_palette_gate_record_layout_buffer_lifecycle_four_binary_2026-08-17.md`。
- V225 已完成 KRKR atlas pack→page texture→persistent entry→Update/free 的发布与异常 owner
  四架构闭环：pack 在 oversized rect 上返回 false、空输入仍追加 0x0 bin，但四 caller 完全忽略
  bool；page construction ref 无 null guard且只在正常 page 尾 Release，任意 map/metadata/Update
  unwind 均泄漏该 raw ref。新 entry 被 operator[] 全零发布，setTexture identity-guard 后逐项提交
  origin、inclusive rect 与 clip，异常保留 map node、page AddRef 和已写 prefix。共同纠正本地两处：
  metadata raw node 必须早于 sourceKey 转换/operator[] 建 owner，且 Update 的 rect 引用必须直接
  指向 entry 内 `tTVPRect`，不能是坐标副本临时量；visible BGRA 仍在 Update 后 free 而不清 record
  pointer。四库写回 8 rename、8 type、41 comment、20 bookmark并保存关闭；专用双 syntax、Web
  34-step、Wasmtime 64-step build、Wasm/DWARF/CTest/hash/section/diff/IDB audit 均闭合，两个
  CODE/module 在 V224 基础上各缩 25 字节且 imports/exports 不变。证据见
  `analysis/motionplayer_krkr_atlas_pack_entry_publication_update_exception_owner_four_binary_2026-08-17.md`。
- V226 已完成 KRKR atlas outer loader 的 path gate→module lookup→root owner→cache probe/retry→
  SourceState projection 四架构闭环：唯一 admission 是 `pieces[0]=="src"`，cache miss 对
  `pieces[1]/[2]` 无长度检查；module miss 只写 `valid=false` 而不清 object，module hit 先清 object，
  再建立覆盖 cache hit/miss 与完整 projection 的 PSB root owner。miss 路径的 `valid=false` 精确位于
  strict `root["source"]` 之后，因此 segment/root exception 保留旧 flag，后续普通 group/icon failure
  则稳定为 false。builder success 后二次 lookup 无 entry guard；A64 old-libstdc++ 只检查 bucket
  predecessor 而不检查 successor，其余三端直接解引用 null node。成功 projection 复制 inclusive rect
  推导尺寸并借用 map-owned texture，不 AddRef。源码据此把 root 提升到外层并把 valid reset 下沉到
  strict source-root 后；四库写回 4 rename、4 type、48 comment、24 bookmark，且复核时发现并替换
  四条短暂错误的 `pieces[3]` module-key 注释。专用双 syntax、Web 3-step、Wasmtime 4-step、Wasm/
  CTest/hash/section/diff/IDB audit 均闭合，两个 module 各增 117 字节、imports/exports 不变。证据见
  `analysis/motionplayer_krkr_atlas_outer_path_cache_retry_sourcestate_owner_four_binary_2026-08-17.md`。
- V227 已完成 D3D render-time source texture callback 的 atlas retry→generic Layer fallback→
  software bridge 四架构闭环：已有 `SourceState.texture` 与 retry 后 true+nonnull 两条 atlas 分支都在
  software-renderer test 前直接返回 borrowed page，只有通用 fallback 的 Layer→MainImage→Texture
  才进入 D3DAdaptor ordered pointer map 与 private OpenGL static copy。共同修正本地把三类结果一律
  software-copy 的偏差，并把 adaptor 借用下沉到 combined resolver。进一步确认 null-texture retry
  先对 Player ResourceManager Variant 做严格 `AsObject()`：非 Object 抛异常，nonnull dispatch
  AddRef 后只查 native adaptor，四端均无 Release，因此每次 retry 永久泄漏一个 dispatch ref；
  extraction 还严格早于 motion-context Variant→ttstr，泄漏引用会跨 re-entrant string conversion 保活。
  本地仅在此路径恢复 strict conversion/leak，不扩大到其他 `nativeRM()` caller。四库写回 4 rename、
  4 type、48 comment、24 bookmark并保存关闭；专用双 syntax、Web 34-step、Wasmtime 64-step、Wasm/
  CTest/hash/section/diff/IDB audit 均闭合，两个 module 各增 48 字节且 imports/exports 不变。证据见
  `analysis/motionplayer_render_time_atlas_retry_software_bridge_resource_manager_leak_four_binary_2026-08-17.md`。
- V228 已完成 MotionNode find-source caller 的 strict ResourceManager owner→lazy context→backing-aware
  src/spec route→generic dispatch fallback 四架构闭环：resolver 入口也对 nonnull ResourceManager dispatch
  执行严格 `AsObject()`/AddRef 且四端无 Release，因此每次调用永久泄漏一 ref；context 入口只 CopyRef
  为 Variant，仅 spec=2 或 spec=1+useD3D 才转 `ttstr`。src/icon admission 均测 backing pointer 而非长度，
  null-backed 与 allocated-empty 会分别走不同 spec/path 分支，empty icon 仍追加 `/`。spec=2 只清 object，
  module miss 写 false 后继续 fallback；spec=1 先提交 path，atlas false 继承 helper 全部字段。fallback 首先清
  texture，以 live src/icon 构造 `ttstr`，并把原 context Variant 直接 dispatch 到 persistent object；status/Void
  false 与成功投影的逐字段 partial commit 已形成状态矩阵。源码移除 RM RAII release/eager context/module
  lookup 与 `.empty()` 合并；四库写回 4 type、52 comment、24 bookmark并保存关闭。专用双 syntax、Web
  34-step、Wasmtime 64-step、Wasm/CTest/hash/section/diff/IDB audit 均闭合，两个 module 各增 107 字节且
  imports/exports 不变。证据见
  `analysis/motionplayer_find_source_spec_lazy_context_backing_dispatch_leak_four_binary_2026-08-17.md`。
- V229 已完成 generic findSource dispatch→strict accessor→scalar/clip projection→rect/cleanup 四架构
  闭环：raw status 以 `!=0` 判失败且 result 直接别名 persistent object；status=0/non-Void 后先写
  `valid=true`，再严格 AsObject/AddRef，所以 wrong-type、typed-null 与任意 getter exception 均保留 true
  及已写字段前缀。source accessor 独立保活原 receiver，re-entrant getter 即使替换 object 也不重定向
  后续读取；clip 另有 retained Variant 与第二 accessor。三类 `Motion_propGet*` helper 均忽略 PropGet
  status 后做 AsReal/bool/copy 并清临时 owner；11 个连续 hint word 是 process-wide 共享全局。针对 IDA
  显示的 `f/o/c/l/t/r/b` 又以 UTF-16LE byte search 证明真实完整键名 findSource/originX/clip/left/
  top/right/bottom，避免把显示伪影写进源码。rect 的 A64 FCVTZS、A32 VCVT、i64 FCVTZS+窄化及
  非有限/越界 portability 也已分层。四库写回 14 helper rename/type、44 global name/type、94 comment、
  32 bookmark并保存关闭，同时修正 A64/A32 两条 V228 Void gate 误标。源码仅补精确注释；双 syntax、
  Web 36-step、Wasmtime 69-step、Wasm/CTest/hash/section/diff/IDB audit 均闭合，产品与 V228 字节级
  一致。证据见
  `analysis/motionplayer_find_source_fallback_projection_accessor_hint_partial_commit_four_binary_2026-08-17.md`。
- V230 已完成 spec=2 Win PSB root→group→texture cache→icon→SourceState projection 四架构闭环：
  module hit 后 native 额外 retain 独立 root owner，并精确按 iconNode→groupNode→root 逆序释放；本地原
  helper 过早销毁 root，已提升到 caller。Win texture map 的 initial find 与 render callback 后 operator[]
  均借用同一 live src ttstr，保留 backing/cached Hint，且回调重入替换 active src 后可能把 group-A
  纹理发布到 key-B；旧 narrow→widen snapshot 已删除。cache hit/miss 都在 strict icon 前提交 borrowed
  texture，icon key 则在全部 texture callback 后从 live icon 转换；valid 仅在 icon strict success 后写 true。
  miss 的 truncated 字段、32-bit allocation、RGBA8/A8L8/odd read、factory raw ref、release-old/store/AddRef/
  construction-Release、异常泄漏均确认。icon projection 顺序 originX/Y→width/height→blank/clip defaults→
  left/top；rect 改为 double(width)+double(left) 后再 FP-to-int，不再先窄化再做 signed int 加法。四库
  写回 8 map-helper rename/type、122 comment、40 bookmark并保存关闭；双 syntax、Web 3-step、Wasmtime
  4-step、Wasm/CTest/hash/section/diff/IDB audit 均闭合，两个 module 各增 60 字节且 imports/exports
  不变。证据见
  `analysis/motionplayer_win_source_root_live_key_texture_icon_projection_four_binary_2026-08-17.md`。
- V231 已完成 `SourceState.path` retained `ttstr`→entry split snapshot→live atlas key probe/retry→
  render-item sidecar 四架构闭环：字段偏移分别为 A64/I64 `+0x70`、A32 `+0x68`、I32 `+0x64`；
  loader 入口 AddRef path backing 给 split 的 owning snapshot，但首查与 build callback 后 retry 都保留
  persistent field 地址，因此回调改写 path 后 pieces 仍指旧 group/icon，而 retry 会观察新 key，未发布 key
  继续落入 native unchecked invalid-access boundary。源码把 Web `std::string path` 改为 `ttstr`，spec-1
  直接 copy owner、atlas lookup 用 live `const ttstr&`，仅 Web diagnostic prepared-item/test 在显式边界 narrow。
  双 syntax、首轮 Web 56-step/Wasmtime 86-step 与 trace 隔离后 3-step/4-step、Wasm parse/hash/section
  均闭合；FUNCTION/GLOBAL/DATA/name/import/export 与 V230 相同，port-only trace guard 令 CODE 各增
  `0x44`、module 各增 68 byte。V230 后 iOS armv7 canonical IDB 被确认 root 损坏（native IDA
  `Database is empty`, error 4）；先保留 SHA-256 `3704...517A` 原样备份，再从当天早期健康 recovery
  回补 V228-V231 关键证据、另存/reopen 验证，最终仅替换 canonical `.i64`。修复库 SHA-256
  `43F7...7BDA` 且从原 binary 路径 health 正常；其余三库也顺序保存关闭。证据见
  `analysis/motionplayer_sourcestate_path_ttstr_split_snapshot_live_retry_idb_recovery_four_binary_2026-08-17.md`。
- V232 已完成 `SourceState` true ctor/common-init→compiler-generated copy assignment→destructor→
  render-time consumer gate 四架构闭环，并纠正 Android arm64 把 `0x696770` 错标成完整 ctor 的
  过时结论：真实构造体为 `0x6EED94`，前者只做 common-field 初始化。四端构造都仅建立 Void
  Variant、null borrowed texture、empty retained path 并写 `valid=false`；`blank`、size/origin、
  clip、rect 保留 allocator/placement storage，直到匹配的 writer/node-kind 路径发布。赋值严格按
  prefix→Variant retain/release→borrowed texture+POD raw copy→path retain-old-release-commit→slots，
  SourceState 已提交后后续 slot/vector 才可能异常；析构则在 item body teardown 后按 path→object
  逆序释放且从不碰 texture。几何、父 mesh 与普通 prepared-item 都在相关 payload 前执行
  valid/node-kind gate；type-10 只发布自身会消费的子集，不能误写成 valid 意味全记录已初始化。
  源码移除 `blank`/数值/clip/rect 的无证据构造默认值，仅保留 valid/texture 与 owner 构造，full
  `clear()` 明确为 reconstruction-harness helper；四篇旧 A64 ctor 报告及 V231 clear 说明同步纠正。
  四库顺序写回 72 comment、32 bookmark、16 type、5 rename并保存关闭；双 syntax、Web/Wasmtime
  full build、Wasm parse/CTest/hash/section/diff/IDB audit 均闭合，FUNCTION/GLOBAL/DATA/name/import/
  export 不变，移除冗余构造 stores 令两端 CODE/module 各缩 `0x70`/112 byte。证据见
  `analysis/motionplayer_sourcestate_partial_constructor_copy_destroy_valid_consumer_four_binary_2026-08-17.md`。
- V233 已完成 persistent `PreparedRenderItem` lazy ensure→selective constructor→natural member layout→
  owner-slot publication 四架构闭环：item 大小/owner slot 为 LP64 `0x1B0`、ILP32 `0x148`，A64
  五份 inline ensure 与其余三端独立 helper 均只初始化三个 ttstr、四个 vector、三个 Variant tag、
  `rawFlag16/drawFlag/rawFlag20`、`stencilComposite`、`commandPatchDivision`；其他 POD/borrowed pointer/
  geometry/color/clip/renderLayerId 保留 allocator bytes。唯一 throwing op 是最前 `operator new`，slot
  最后发布，所以 allocation failure 不改 node，后续 builder 异常保留已发布 item 的 partial prefix。
  完整自然顺序恢复为 flags→child vector→blend→layer ids→共享 sort/Z→matrix→XY→origin→geometry/
  colors→paint/viewport/clip→opacity/coord/priority/stencil→key/source/parent→mesh→owning tail；删除重复
  commandCoord Z，getCommandList 改读 shared sortKey。`dirtyRect` 与 portable ancestor index 移到 Web
  derived sidecar。由于 call sites 使用 `new T()`，constructor 改为 user-provided 空 body，避免首声明
  `=default` 的 value-initialization 偷偷 whole-object zero；direct unit fixtures 显式发布会消费的 dormant
  字段。Web Wasm 的 native base 自检恰为 328 byte，并逐项命中两份 32-bit offset。四库写回 52 comment、
  17 bookmark、3 helper type并保存关闭；双 syntax/full build/Wasm ctor disasm/CTest/hash/section/diff/IDB
  audit 闭合，FUNCTION/GLOBAL/name/import/export 不变，CODE 各缩 `0x26E`、DATA 各缩 `0x20`、module
  各缩 654 byte。证据见
  `analysis/motionplayer_prepared_item_selective_ctor_native_layout_sidecar_commit_four_binary_2026-08-18.md`。
- V234 已完成 ordinary `PreparedRenderItem` persistent overwrite→command-key exception prefix→
  stale suffix 四架构闭环：source admission 先发布 `drawnThisFrame=true`，item ensure 后精确按
  ownerLabel→three flags→commandKey→numeric/matrix/origin→color remap→corners→commandSrc→blend/
  opacity/source/stencil/draw→parent→paint/viewport→mesh→draw-affine→main append 覆盖。Octet context
  在临时 ttstr conversion 抛出时只提交本轮 owner/flag 前缀，command key 与全部后缀保留上次成功值；
  fresh item 后缀则仍是 V233 的 dormant storage。源码据此重排 ordinary block，并把可能分配的 Web
  `sourceKey` diagnostic narrow 推到完整 native overwrite 之后；unit fixture 以成功 reused item 锁定
  prefix/stale-suffix 状态并恢复后续测试输入。四库写回 52 comment、20 bookmark并保存关闭；双
  syntax、Web 3-step/Wasmtime 4-step build、Wasm/CTest/hash/section/diff/IDB audit 闭合，FUNCTION/
  GLOBAL/DATA/name/import/export 不变，CODE/module 各缩 15 byte。证据见
  `analysis/motionplayer_prepared_ordinary_overwrite_exception_prefix_four_binary_2026-08-18.md`。
- V235 已完成 non-preview type-3 wrapper 与 type-12 stencil post-pass 的持久覆盖/异常 prefix
  四架构闭环：wrapper 精确只刷新 ownerLabel→paint/viewport→root affine→draw=false/stencil→optional
  aux+ancestor→parent→child clear/recurse→caller range insert；它不写 sourceState，也不写 ordinary
  numeric/source/mesh suffix，fresh 字段保持 dormant、reused 字段保持 stale。源码删除错误的 wrapper
  source pointer store，child-Octet fixture 以显式 sentinel 锁定 no-write，同时验证 owner/paint/stencil/
  parent 已提交且 child vector 已清。type-12 post-pass 只做 aux→clear→self→stored-order mask expansions，
  direct pointer 与 non-preview wrapper-child range 分支保留 duplicates/live preview 和 allocation-failure
  prefix。iOS armv7 IDB 对旧“every published item has one source”注释追加 V235 correction；四库共
  写回 72 comment、20 bookmark并保存关闭。双 syntax、Web 24-step/Wasmtime 25-step build、Node/
  CTest/hash/section/diff/IDB audit 闭合；FUNCTION/GLOBAL/DATA/name/import/export 不变，删除错误 store
  令两端 CODE/module 各缩 26 byte。证据见
  `analysis/motionplayer_prepared_type3_wrapper_stencil_stale_source_four_binary_2026-08-18.md`。
- V236 已完成 priority reverse selection 的 duplicate/re-entry→final drawn byte→type-12 post-pass
  四架构闭环：retained priority dispatch只快照 receiver owner，不快照 node deque/count/state；position
  用 getter 前 live size，getter 重入后再用 live container选 node，loop tail也重新读 size。每次 duplicate
  只清 selected byte，成功 admission 再置 true，所以 final byte 取最后一次结果，而 earlier successful
  main append 的同一 persistent pointer 不被 later failure移除；omitted node 完全不清，保留 stale true。
  node-order post-pass直接消费这些 final live bytes，不检查 priority/main membership。test-only dispatch
  hook 锁定 omitted stale-true type-12 进入 aux/self seed，以及 duplicate 前几次成功、最后 source-invalid
  后 main 保留重复指针但 final false/aux空。production 源码无需修改；四库写回 24 comment、12 bookmark
  并保存关闭。双 syntax、双树 no-work、Node/CTest/hash/diff/IDB audit闭合，产品与 V235 字节级一致。
  证据见
  `analysis/motionplayer_prepared_priority_duplicate_final_drawn_stencil_four_binary_2026-08-18.md`。
- V237 已完成 common command builder 的 `rawFlag20/renderLayerId` persistent latch→clip/SLA prefix→
  reset release 四架构闭环：constructor 只清 latch、数值槽保持 dormant；draw=false 全保留，invalid
  clip只清 rawFlag21，valid clip与可能的 persistent SLA先提交。false latch 才逐次 retain RM并零参数
  require；普通失败把 Void 转 0 后仍提交 ID/latch，throw 则保留 false/stale，而后续 materialization
  exception不回滚已提交 latch。reset只对非root按 layer1→layer2→latched render ID释放，不检查数值/
  draw/clip，调用前不清状态；ordinary failure继续erase，throw保留 old tree/latch/map供 retry。源码行为
  无需修改，只补四端证据注释；test-only probe锁定 require ordinary failure与第三 release throw 的
  partial state。四库写回37 comment、16 bookmark并保存关闭；双syntax、Web 4-step/Wasmtime 6-step、
  Node/CTest/hash/section/diff/IDB audit闭合，产品与 V235/V236字节级一致。证据见
  `analysis/motionplayer_render_layer_id_latch_persistence_release_four_binary_2026-08-18.md`。
- V238 已完成 shared mesh-point owning Array Variant与common leaf call-local geometry四架构闭环：
  推翻旧“helper只是重复内联抽取”注释，四端各锁定一个16-caller独立函数。helper以
  `{Array Variant, borrowed Items}`直接deque append，逐点执行f32 `offset+point`后提升Real，完整
  返回owning Variant并以RAII覆盖partial grow/FuncCall exception；删除旧raw dispatch、逐项
  PropSetByNum与手工Release。common leaf在createdOrChanged false前不构造local geometry；type0只建
  6个call-local double Real，type1精确读取commandBezierPatchPoints，type2读取commandComposite，
  删除non-native persistent localCorners/localMeshPoints sidecar及错误ordinary mesh选择。Wasmtime
  differential glue同步移除V158遗留dead marker引用。四库写回36 comment、16 bookmark、4 rename并
  保存关闭；双syntax、Web 11-step、Wasmtime 65-step、guest 2-step、Node/CTest/objdump/hash/section/
  diff/IDB audit闭合。两端CODE各缩`0xB36`、DATA各缩`0xA0`、module各缩3003 byte。证据见
  `analysis/motionplayer_mesh_point_array_variant_leaf_local_geometry_four_binary_2026-08-18.md`。
- V239 已完成common leaf created=true的clip snapshot→descriptor/color原位写→live source resolve→
  width/height→neutralColor/setSize→geometry live-read四架构闭环：accepted clip四边在所有callback前
  snapshot并跨重入保持，persistent clipRect只作镜像；corners、command vectors和SourceState在各自
  consumer处live读取。descriptor按key→src→blendMode、colors按0..3逐步提交，white是uint32零扩展
  Integer 4294967295；resolver return与独立source accessor跨primitive，width/height普通失败仍
  Void→0，neutral/setSize status均忽略。cleanup严格source accessor→source Variant→color→descriptor→
  leaf raw owner，不回滚item/dictionary/Layer副作用。源码修正offset从callback后重读item.clipRect的
  偏差，显式保留四边snapshot。四库写回40 comment、16 bookmark并保存关闭；双syntax、Web 3-step/
  Wasmtime 4-step、guest relink、Node/CTest/hash/section/diff/IDB audit闭合，两端CODE/module各缩23 byte。
  证据见
  `analysis/motionplayer_leaf_clip_snapshot_descriptor_source_size_prefix_four_binary_2026-08-18.md`。
- V240 已完成common builder aux/group尾的paint union→完整四边target clip→viewport wrong-empty→
  `composedLayer` Void gate/owner/factory→setSize/fill→逐子alpha-mask→success publication四架构闭环：
  修正旧port从extent重建origin-zero clip的偏差，并删除loop外`_maskMode`缓存。四端进一步共同锁定
  destination Variant CopyRef→旧left/top snapshot→source Variant CopyRef→live width/height/maskMode/
  stencil的可重入顺序；源码以owned-alpha core避免为控制顺序新增第二轮AddRef/Release。三个probe覆盖
  translated clip、fill exception旧状态保留及source AddRef重入后的live stencil。四库目标写回52 comment、
  16 bookmark；iOS armv7首次pack误报成功但为空，损坏库完整备份后从精确thin slice全量重建，canonical
  经独立idat与MCP双reopen验证，并恢复V240标注/关键semantic names。双syntax、Web semantic 11-step/
  final 3-step、Wasmtime semantic 20-step/final 4-step、guest、Node/CTest/no-work/hash/section/diff/零session
  audit均闭合；两端module较V239各增359 byte，DATA与imports/exports不变。证据见
  `analysis/motionplayer_group_composed_layer_target_clip_live_mask_exception_publication_four_binary_2026-08-18.md`。
- V241 已完成common builder的active/retired whole-tree begin swap→sequence reset→normal-only retired
  cleanup→direct Object Invalidate→exception retry→两 caller传播四架构闭环：existing与lazy adaptor都在
  builder traversal前执行整树swap/reset；leaf/group/tail异常均跳过final clear。cleanup逐节点完整复制
  mapped payload但不erase原node，只按Object tag直接调用`Object->Invalidate(0,null,null,Object)`，不做
  null guard、不使用closure ObjThis并忽略普通HRESULT；抛出则整树保留，重试从first node重新失效此前
  成功节点，下一builder begin则把partial active与unconsumed retired整树交换角色。源码删除旧null/
  ObjThis容错；ordered-map probe锁定distinct ObjThis、ignored failure HRESULT及tree-intact retry。四库
  写回38 comment、16 bookmark、4 rename；iOS armv7经different-path compressed save、独立idat、
  canonical MCP双reopen验证，旧packed/loose库可恢复备份。双syntax、Web/Wasmtime/guest build、Node/
  CTest/no-work/hash/section/diff/零session audit闭合；两端CODE/module各缩16 byte，DATA/name/imports/
  exports不变。证据见
  `analysis/motionplayer_common_builder_retired_tree_normal_tail_retry_four_binary_2026-08-18.md`。
- V242 已完成accurate SLA per-item的base→optional masked→debug→publication四阶段owner拓扑四架构
  闭环：每阶段都先构造call-local Variant CopyRef，以`AsObject()`只retain Object，随后在任何phase
  callback前立即析构temporary closure；跨callback存活的是raw Object retain，不是含ObjThis的long-lived
  Variant copy。normal item尾精确为publication raw Release→final Variant dtor→base raw Release→base
  resolver Variant dtor，异常landing按构造进度条件展开同一owner stack。源码新增共享temporary-copy/
  Object-retain primitive与raw Release guard，四阶段全部改用它；masked alpha-mask by-value destination
  仍由persistent final Variant构造，不额外保留non-native closure。distinct Object/ObjThis probe锁定
  `Object.AddRef, ObjThis.AddRef, Object.AddRef, Object.Release, ObjThis.Release`及phase-end Object-only
  Release。四库写回64 comment、16 bookmark；重建的iOS armv7补回accurate renderer semantic rename并
  通过different-path save、独立idat和canonical MCP readback。双syntax、Web/Wasmtime/guest、Node/
  CTest/no-work/hash/section/diff/零session audit闭合；两端module各增675 byte、CODE各增`0x17E`、name
  各增`0x122`、FUNCTION payload各增3 byte，DATA/imports/exports不变。证据见
  `analysis/motionplayer_accurate_sla_phase_raw_object_owner_topology_four_binary_2026-08-18.md`。
- V243 已完成ordinary Canvas shared source resolver→source accessor temporary CopyRef→Object-only raw
  owner→width/height四架构闭环：resolver result作为persistent per-item Variant保留；accessor先从它构造
  call-local CopyRef，依次retain Object/ObjThis，再由`AsObject()`额外retain Object，随后在任何property
  callback前立即析构temporary closure。旧port先`AsObjectNoAddRef()`再从persistent lvalue建accessor，
  少了一轮可重入CopyRef/Release并把non-Object exception提前。源码改为
  `ncbPropAccessor{tTJSVariant(source.object)}`，diagnostic raw pointer取自accessor；V242 distinct
  Object/ObjThis probe扩展为真实ncb accessor full-expression并锁定五事件prefix与Object-only析构。
  四库写回24 comment、16 bookmark；iOS armv7补回Canvas semantic rename并通过safe packed-copy/idat/MCP
  readback。双syntax、Web/Wasmtime/guest、Node/CTest/no-work/hash/section/diff/零session audit闭合；
  两端module/CODE各增70 byte，FUNCTION/DATA/name/imports/exports不变。证据见
  `analysis/motionplayer_canvas_source_accessor_temporary_copyref_owner_four_binary_2026-08-18.md`。
- V244 已完成ordinary Canvas buffered `GetValue<tTJSVariant>` source identity、三层nested owner scope与
  `right < left`边界四架构闭环：ResourceManager accessor temporary先CopyRef并只保留raw Object；
  `bufLayer`由shared-hint `GetValue`返回persistent Variant；buffer accessor再执行同样temporary-prefix。
  `right < left`只跳过setSize/copy/mask/operateRect image phase，不跳过item；buffer raw→bufLayer Variant→
  ResourceManager raw均在debug frame前清理。源码恢复原始GetValue调用形状、nested scope和right-inversion
  条件，删除错误early continue；端到端probe锁定负HRESULT忽略、无buffer调用、4次drawLine及frame前
  owner释放。四库写回51 comment、16 bookmark，iOS armv7另恢复`Motion_propGetVariant_guess`并经两轮
  different-path save/idat/MCP readback闭合。双syntax、Web/Wasmtime/guest build与section/hash/diff/零
  session audit通过。证据见
  `analysis/motionplayer_canvas_buffered_getvalue_owner_scope_right_inversion_four_binary_2026-08-18.md`。
- V245 已完成ordinary Canvas per-item outer descriptor/color/source/accessor normal/exception tail四架构
  闭环：direct frame后的源码级`continue`、buffered fallthrough、Void styles与unsupported mesh都进入同一
  `source raw→source Variant→color raw→descriptor raw→loop rejoin`正常尾；Android arm64 cold chain和
  iOS armv7 SjLj case 93进一步锁定callback异常先析构branch locals再展开完全相同的outer owner顺序。
  Android armv7/iOS arm64保留其异常ABI表示差异，不伪造同形cold block。源码仅纠正owner跨越debug
  frame的过时注释；probe同时验证nested owners在首个frame前已释放、distinct source Object/ObjThis在
  frame后分别精确释放2/1次，并覆盖direct continue。四库写回34 comment、16 bookmark；iOS armv7
  恢复`Player_renderToCanvas_sjlj_cleanup_guess`并经safe packed-copy/idat/canonical MCP readback闭合。
  双syntax、Web/Wasmtime/guest build与diff/零session audit通过；Web/Wasmtime主wasm与V244逐字节相同。
  证据见
  `analysis/motionplayer_canvas_outer_owner_normal_exception_tail_four_binary_2026-08-18.md`。
- V246 已完成ordinary Canvas item-loop后的无参数final `Layer.setClip`、函数级target/Layer raw owner与
  Player pre-drawAffine布局反证四架构闭环：empty list和loop exhaustion共同进入flags=0、shared hint、
  null result、argc=0/argv=null、ObjThis=target raw Object的`setClip`，普通HRESULT忽略；正常与异常均
  以target→Layer顺序清理，final callback前所有per-item/nested owners都已消失。进一步fresh反编译四端
  Player ctor/dtor确认draw-affine前24-byte区只有POD block-zero且析构全部跨过，Canvas也零field store，
  故本地`_lastCanvas`不是unused native owner而是整个伪字段。源码删除publication和Variant member；
  probe锁定两轮final reset、failure HRESULT忽略及reset时owner均已清空。四库写回48 comment、24
  bookmark，iOS armv7另恢复ctor/dtor semantic names与Canvas void signature，经different-path save、
  独立idat和canonical MCP readback闭合。双syntax、Web/Wasmtime/guest full rebuild、hash/section/diff/
  zero-session audit通过；Web/Wasmtime各缩172 bytes且全部来自CODE。证据见
  `analysis/motionplayer_canvas_final_reset_outer_owner_no_lastcanvas_four_binary_2026-08-18.md`。
- V247 已完成Player frameDelta/damping/control bytes与pending stealth strings/camera velocity/
  draw-affine/particleOutsideRect两段相邻布局四架构闭环：V246 draw-affine前未知24-byte POD精确恢复为
  持久cameraVelocity X/Y/Z；damping实际紧邻frameDelta，后接noUpdateYet/reverseSeek/
  cameraConstraintDirty/drawAffineNonIdentity四byte，non-identity不在affine尾；firstFrame与
  motionCompleted则归回queuing/directEdit组。updateLayers确认三轴先积分、再以同一
  `pow(damping, frameDelta/60)`持久衰减；particle child路径确认velocity变换完成后最后把未校验的
  particleAccelRatio提交为child damping。源码迁移字段顺序并以负damping+zero-zoom case锁定最终writer。
  四库写回36 comment、16 bookmark、iOS armv7另恢复2个`_guess`函数名；iOS armv7经双candidate、独立
  idat、分层可恢复备份与canonical MCP readback安全保存。双syntax、Web 54-step、Wasmtime 62-step、
  guest/no-work/hash/section/零session audit闭合；Web/Wasmtime相对V246各缩29 bytes且只来自CODE，
  FUNCTION/DATA/name不变。证据见
  `analysis/motionplayer_player_camera_velocity_affine_layout_four_binary_2026-08-18.md`。
- V248 已完成Player object prefix四架构闭环：共同声明顺序精确为rootPlayer、parentPlayer、
  non-owning currentDispatch、raw-label std::map、camera position/target/stereovision九double、
  cameraOffset两float、bounds四double、MotionNode deque。map末端与camera X、bounds maxY末端与
  deque首字节在四端均按自然对齐连续；Android armv7的24-byte map后有4-byte double alignment
  padding。cameraOffset不在九-double memset中，但继续扫描完整ctor找到四端
  独立零写，避免误删初始化。源码迁移currentDispatch/map/camera/bounds/deque并修正preview/
  cameraActive/stereovisionActive/priorDraw/hasCamera已知相对顺序，从prefix移除旧chara/motion/outline
  假设；node deque自动析构先于更早map的顺序也随之恢复。四库写回24 comment、24 bookmark，
  iOS armv7补回3个`_guess`函数名并经双candidate、独立idat、两层可恢复备份与canonical MCP回读。
  双syntax、Web 33-step、Wasmtime 62-step、guest/no-work/hash/section/零session audit闭合；两份主wasm
  相对V247各增311 bytes且全在CODE，FUNCTION/DATA/name不变。证据见
  `analysis/motionplayer_player_prefix_currentdispatch_nodelabel_camera_bounds_deque_layout_four_binary_2026-08-18.md`。
- V249 已完成Player node deque直接后继容器的四架构闭环：共同声明顺序精确为MotionNode deque、
  HM1 EvalCascadeMap、HM2 LabelValueMap、non-owning selectedParameterEntry、parameterEntries vector、
  parameterRampMap multimap，且六个对象在四端均无未解释member gap。Android旧libstdc++两个HM空构造
  eager建立11 buckets，iOS libc++保持lazy null buckets；这是同一源码unordered_map的ABI差异。
  正常析构、iOS armv7 SJLJ landing pad与Android allocation-failure cleanup共同确认ramp→vector→HM2→
  HM1→nodes→NodeLabelMap逆序回滚；selected/ramp values均为vector element raw aliases，不拥有entry。
  源码已将五个后继字段从event/static/HM3/HM4旧位置迁回`_nodes`后，并清理过时offset/ownership注释。
  四库写回24 comment、24 bookmark；iOS armv7经different-path candidate、独立idat、可恢复canonical/
  loose备份与MCP readback安全保存。双syntax、Web 33-step、Wasmtime 62-step、guest/no-work/hash/section/
  零session audit闭合；两份主wasm section size不变但hash改变。证据见
  `analysis/motionplayer_player_post_node_hm1_hm2_parameter_container_layout_four_binary_2026-08-18.md`。
- V250 已完成parameterRampMap后到rootContent的连续Player布局闭环：三double经direct writers纠正为
  evaluation cursor、Player emoteAngle、cameraAngle，而不是旧portable注释中的evaluation/loop/last；
  真正frameLast/frameLoop pair位于四端对象后部。其后精确为queuing/firstFrame/directEdit/
  motionCompleted四byte、division Variant、无ctor初始化的motion index、motionList/motionContent/
  priority owners、root cursor/current/next、delta/damping、noUpdate/reverse/constraint/affine四byte、
  internalRenderLayerReady/needsInternalAssignImages两byte及rootContent owner；producer/consumer旧声明顺序
  已纠正。五个Variant正常析构和iOS armv7 SJLJ unwind都按rootContent→priority→motionContent→
  motionList→division→ramp逆序。V250还纠正Android armv7 NodeLabelMap/ramp tree均为24-byte source
  object：前者因后继double产生4-byte padding，后者直接结束于live evaluation `+0x120`。四库写回
  26 comment、24 bookmark，iOS armv7补回3个`_guess`函数名并经candidate/idat/backup/MCP回读安全
  保存。双syntax、Web33-step、Wasmtime62-step、guest2-step/no-work/hash/section/零session audit闭合；
  两份主wasm总大小与列出section size不变但hash改变。证据见
  `analysis/motionplayer_player_post_ramp_frame_core_type1_root_owner_layout_four_binary_2026-08-18.md`。
- V251 已完成rootContent后source workspace与raw SeparateLayerAdaptor owner四架构闭环：共同直接序列为
  两份独立CopyRef的ResourceManager Variant、descriptor Dictionary、Void primary Layer、colors Dictionary、
  Void work Layer与raw adaptor pointer；pointer后在四端直接进入pending stealth pair。descriptor.color新增
  colors引用；lazy materializer先发布primary，再查尺寸/setSize/build work，故later failure留下不会自动重试的
  sticky half-init。raw adaptor成功构造后才publish，Player析构按pointee dtor→operator delete→slot null，随后
  work→colors→primary→descriptor→sourceCache RM→findSource RM→rootContent逆序自动析构。源码恢复连续布局，
  四库写回24 comment/24 bookmark；iOS armv7经candidate/idat/backup/MCP readback安全保存。双syntax、
  Web33-step、Wasmtime62-step、guest2-step/no-work/hash/section/零session audit闭合；两份主wasm各缩8 bytes且
  全部来自CODE。证据见
  `analysis/motionplayer_player_source_workspace_raw_adaptor_layout_four_binary_2026-08-18.md`。
- V252 已完成raw adaptor后pending/velocity/affine/rect连续区四架构闭环：两个独立pending ttstr owner后
  无gap连接cameraVelocity X/Y/Z、四double+两float drawAffine与四float particleOutsideRect；POD部分精确
  80 bytes，rect末端直接触及下一nontrivial member。motion/chara queue均CopyRef，flush把持久field本身
  借给nested call且只在正常返回后release/null，异常保留pending。normal dtor从下一member跨过整个POD
  区，再按pending chara→motion→V251 workspace逆序释放。源码迁移完整区块；四库写回24 comment/24
  bookmark并恢复5个`_guess`名称，iOS armv7经candidate/idat/backup/MCP readback安全保存。双syntax、
  Web fresh-config+55-step、Wasmtime62-step、guest2-step/no-work/hash/section/Bison3.8.2/零session audit闭合；
  三份wasm大小和列出section size均不变、仅hash变化。证据见
  `analysis/motionplayer_player_pending_velocity_affine_rect_contiguous_layout_four_binary_2026-08-18.md`。
- V253 已完成particleOutsideRect后到canonical ResourceManager的连续Player布局四架构闭环：
  tTVPComplexRect实际size为64-bit 40/32-bit 32 bytes，后接一个仅ctor-zero且无第二访问的unknown dword、
  type3/D3D bytes、pixelate int、无Player-ctor初始化的tag cursor/current/next、唯一MotionEvent vector、
  chara/stealthChara/motion/stealthMotion四live owners及第三份RM CopyRef。event元素为44/28-byte
  type+param1+param2，reverse dtor按param2→param1；Player正常析构按later Variants→四strings→vector→
  ComplexRect→pending pair。源码迁移全部字段并删除tag triple过度初始化，unknown保持`_guess`。四库写回
  32 comment/32 bookmark、18 rename；iOS armv7经candidate/idat/backup/MCP readback安全保存。双syntax、
  Web33-step、Wasmtime62-step、guest2-step/no-work/hash/section/零session audit闭合；两主wasm各增8 bytes且
  全部来自CODE。证据见
  `analysis/motionplayer_player_drawregion_tag_event_live_strings_layout_four_binary_2026-08-18.md`。
- V254 已完成canonical ResourceManager后五-Variant连续owner cluster四架构闭环：顺序精确为第三份RM
  CopyRef、findMotionContext、outline、meshline、tagFrameSource；constructor仅CopyRef首项，后四项Void。
  playImpl在labels/motionContent后增量提交result[1] context；outline/meshline getter CopyRef、setter原类型
  copy-assign；ordinary init先提交tag再priority/root，later failure均不回滚。normal dtor严格tag→meshline→
  outline→context→canonical RM→四live strings。源码迁移四个分散owner到RM后。四库写回24 comment/24
  bookmark，iOS armv7补回5个`_guess`访问器并经candidate/idat/backup/MCP readback安全保存。双syntax、
  Web33-step、Wasmtime62-step、guest2-step/no-work/hash/section/零session audit闭合；两主wasm section size
  不变仅hash变化。证据见
  `analysis/motionplayer_player_canonical_context_outline_meshline_tag_variant_cluster_four_binary_2026-08-18.md`。
- V255 已完成tag Variant后标量/控制连续区四架构闭环：九Boolean严格为preview、syncActive、
  cameraActive、stereovisionActive、priorDraw、independentLayerInherit、syncWaiting、playing、
  cameraAlive；其后按自然ABI对齐依次为cameraFov、zFactor、frameTick、lastTime、loopTime、
  completion/mask/count/color与outside/speed/mesh。constructor精确写FOV=0.2、zFactor=+0.0、
  frameTick=0，但故意不写last/loop；源码恢复完整声明顺序、删除last/loop零initializer并修正
  zFactor旧默认1.0。四库写回68 comment/29 bookmark；iOS armv7补回34个`_guess`访问器，
  经candidate/idat/backup/MCP readback安全保存。双syntax、Web33-step、Wasmtime62-step、
  guest2-step/no-work/hash/section/零session audit闭合；两份主wasm及其CODE均精确缩16 bytes。
  证据见
  `analysis/motionplayer_player_tag_end_scalar_control_cluster_four_binary_2026-08-18.md`。
- V256 已完成meshDivisionRatio后HM3/HM4/variable-track deque连续区四架构闭环：四端按各自
  STL ABI精确相邻为PerNodeLayerStateMap、ttstr->raw-double snapshot map与variable deque，
  下一成员边界无gap；Android旧libstdc++默认构造内部hint 10并eager落到11 buckets，iOS
  libc++保持bucketless lazy，这一差异来自相同源级默认构造，故源码没有伪造reserve(10)。
  normal dtor严格deque→HM4→HM3→tag owner，HM3 mapped node生命周期进一步由particle
  Variant→mesh vector→child Variant→embedded ClipSlot→key闭合。源码把三容器迁回mesh后连续
  声明；四库写回40 comment/33 bookmark/24 rename，iOS armv7经candidate/idat/backup/MCP
  readback安全保存。双syntax、Web33-step、Wasmtime62-step、guest2-step/no-work/hash/section/
  零session audit闭合；两主wasm与列出section size不变，guest仅未列出外层多6 bytes。
  证据见
  `analysis/motionplayer_player_hm3_hm4_variable_deque_contiguous_layout_four_binary_2026-08-18.md`。
- V257 已完成variable deque后Player最终tail pointer与对象总size四架构闭环：四端均保留恰好
  pointer-width尾成员且constructor故意不初始化、destructor无owner动作、全镜像无producer；
  Android两端另保留零-xref residual load body，直接从该slot调onFindMotion并使用独立callback
  result，随后才清返回Variant，故不是live shared-result helper的同语义clone；iOS裁掉consumer但
  sizeof仍保留成员。live四端只走rootPlayer prefix currentDispatch。源码删除误占该slot的port-only
  `_sourceCacheNative` cache，render按需unwrap RM，恢复indeterminate raw slot及零caller residual
  method，并纠正六份旧分析。四库写回16 comment/12 bookmark/2 rename；iOS armv7经candidate/
  idat/backup/MCP readback安全保存。双syntax、Web33-step、Wasmtime62-step、guest2-step/no-work/
  hash/section/零session闭合；两主wasm CODE各缩24 bytes，guest CODE缩28 bytes，dead method只增加
  DWARF/custom size。证据见
  `analysis/motionplayer_player_final_tail_dispatch_residual_four_binary_2026-08-18.md`。
- V258 已完成Player从`+0`到`sizeof`的四ABI完整布局总账：把V248--V257十个连续纵切面重新在
  四端constructor、normal destructor与Engine精确allocation处闭合，边界逐段首尾相接并分别
  精确覆盖A64 `0x568`、A32 `0x3B0`、i64 `0x4B8`、i32 `0x348`；当前native declaration共
  111个顶层字段，没有未解释洞、重复成员或native区内port-only字段，A32仅final pointer后4-byte
  真正tail padding。本地Clang ordinary Web32 record dump为final pointer `+0x350`、sizeof
  `0x358`/dsize `0x354`，headless map/counter严格从pointer后`+0x354/+0x368`开始，证明诊断字段
  只追加在native tail之后。源码仅删除final pointer后漂移的旧source-workspace注释并标记native
  layout end，无行为改动。四库写回12 comment/12 bookmark，iOS armv7经candidate/idat/backup/
  MCP readback安全保存。双syntax串行复验、Web33-step、Wasmtime62-step、guest2-step/no-work/
  hash/section/零session audit闭合；两主wasm完全不变，guest仅DWARF/source-line导致hash变化。
  证据见
  `analysis/motionplayer_player_complete_layout_ledger_four_binary_2026-08-18.md`。
- V259 已完成Player 111-field constructor初始状态与body side-effect顺序四端闭环：33个nontrivial
  顶层字段按C++构造（其中三份RM Variant独立CopyRef），78个trivial/raw字段中71项有共同明确
  初值，剩余恰好七项——type-1 index、tag cursor/time trio、last/loop time、final residual
  dispatch——四端完整constructor均故意不写。新发现四参考都严格先发布descriptor/colors两个
  Dictionary并完成descriptor.color PropSet，之后才append synthetic root/复制transform order；旧
  portable顺序相反，改变Dictionary或root allocation失败时的partial-construction/unwind边界，现已
  调整并让factory-return guards跨越root commit。最终Web wasm反汇编直接回读两个Dictionary call→
  PropSet→ensureRoot顺序。四库写回12 comment/12 bookmark，iOS armv7经prebackup/candidate/idat/
  canonical readback安全保存。双syntax、Web3-step、Wasmtime4-step、guest1-step/no-work/hash/section/
  零session audit闭合；两主size/section不变但CODE内容hash变化。证据见
  `analysis/motionplayer_player_constructor_initialization_dictionary_root_order_four_binary_2026-08-18.md`。
- V260 已完成Player constructor partial-construction异常回滚四端闭环：Android两端与iOS arm64
  使用多入口Itanium landing pads，iOS armv7以1-based `call_site`→0-based switch明确区分两次RM
  CopyRef、drawRegion、canonical RM、descriptor/colors factory+assignment、PropSet与root append十个
  frontier；四端最终都先colors→descriptor释放factory-return raw owners，再严格按V258声明逆序清理
  variable deque/HM4/HM3、late Variants、live strings/event/rect、pending strings、source workspace、
  type1/root owners、parameter containers、node deque/map并原样resume异常。trivial POD、raw SLA slot和
  final residual无cleanup；cleanup再抛进入terminate。V259后的RAII/declaration order已自然复现，
  本轮无需源码改动。四库写回16 comment/16 bookmark/2 rename，iOS armv7经prebackup/candidate/
  idat/canonical rename+comment readback安全保存；三套no-work、hash/零session audit闭合，Wasm保持V259。
  证据见
  `analysis/motionplayer_player_constructor_unwind_ladder_four_binary_2026-08-18.md`。
- V261 已完成EmoteEngine外层对Player direct owner的new-expression/publication/unwind边界四端闭环：
  四端都严格先保留`sizeof(Player)` raw allocation、执行完整Player constructor，正常返回后才把
  pointer发布到Engine owner slot，随后才开始position controller；Player constructor抛出时只对
  pending raw storage调用operator delete，跳过完整Player destructor、未发布slot与全部controllers，
  而publication后任一later failure才经completed unique owner执行Player destructor+delete。iOS armv7
  的SJLJ call_site 1/2/3与dispatcher case0/case1/completed-owner label给出最直接分叉，Android/iOS
  64位Itanium landing pads一致，Android armv7保留共享fragment边界差异。当前直接member initializer
  已自然精确复现，只补ABI-neutral注释。四库写回15 comment/15 bookmark，iOS armv7经prebackup/
  candidate/idat/canonical comment readback安全保存；双syntax、最终三目标no-work、hash/section/
  diff-check/零session audit闭合。证据见
  `analysis/motionplayer_engine_player_new_expression_publication_unwind_four_binary_2026-08-18.md`。
- V262 已完成EmoteEngine七个direct-controller owner的逐项commit/EH frontier四端闭环：建立
  position/scale/color/angle/bust/hair/parts各自allocation size、constructor输入、连续owner slot与
  `new/ctor/store`地址总账；Android A64七个pending-delete landing和iOS A64多入口stub均精确从
  当前失败controller之前的completed owner开始，而非统一扫描未构造slots。iOS armv7完整恢复
  call_site 3..15的allocation/constructor成对状态、Angle仅allocation state、call_site 16..19四个
  seed setter full-prefix unwind及cleanup再抛abort；同时证明Android eager deque让Angle存在
  pending-constructor delete，iOS lazy libc++零写让Angle无该frontier，仍来自同一源码constructor。
  当前八个declaration-order unique_ptr initializer已自然精确复现，无行为代码改动；就地纠正V13
  报告中过时的body `reset(new)`和单一parts-first unwind叙述。四库写回53 comment/53 bookmark/
  5个iOS armv7 `_guess` rename，armv7经prebackup/candidate/idat/canonical comment+name readback
  安全保存；双syntax、三目标no-work、Wasm hash、diff-check与零session audit闭合。证据见
  `analysis/motionplayer_engine_controller_owner_commit_unwind_frontier_four_binary_2026-08-20.md`。
- V263 已完成EmoteEngine在parts owner发布后的完整trailing member/seed failure纵切面：精确恢复
  raw wind、五float、八byte、五double、三个Void Variant和HM4–HM7四端offset/concrete store；
  新发现Android A64四个eager 11-bucket HM各有“跳过当前partial member、从前一完整HM/Variant
  开始”的独立cleanup entry，HM7后内联seed无C++ throw call，Android A32完整304-instruction ctor
  则没有任何本地prefix-cleanup landing。iOS两端四个libc++ HM均为non-throwing lazy zero stores，
  唯一晚期frontier是四个seed helper，A64共同landing与A32 SJLJ call_site 16..19/cases15..18都
  从完整HM7→HM4→Variant×3开始；同时纠正iOS两端`dirty=false` concrete store被dead-store
  elimination省略、Android以selector 32-bit store落物为0的差异。源码行为已准确，只修注释与旧
  报告。四库写回53 comment/53 bookmark/8 rename，armv7经prebackup/candidate/idat/canonical
  readback安全保存；双syntax、Wasmtime17-step、guest1-step、Web cache单进程重配后31-step、三目标
  no-work、hash/section/diff-check/零session审计闭合。证据见
  `analysis/motionplayer_engine_trailing_member_construction_seed_unwind_frontier_four_binary_2026-08-20.md`。
- V264 已完成EmoteEngine正常析构从raw wind、HM7–HM4、三个Variant、七controller owner到
  Player owner的四端逐指令纵切面：共同销毁顺序完全一致，但owner-slot concrete write存在稳定
  平台差异——Android两端均为pointee dtor/delete后写null，A64还把下一owner load排在上一slot
  clear之前；iOS两端则先写null再dtor/delete。raw wind四端都只delete且不清 dying Engine slot。
  据此纠正旧报告把Android A32 helper写成clear-before-delete及把A64流水化笼统推广到四端的
  过时叙述；源码八个`reset()`与raw delete行为已准确，仅补ABI-neutral注释，不手写目标指令调度。
  四库写回21 comment/21 bookmark/1 rename，armv7经prebackup/candidate/idat/canonical name+comment
  readback安全保存；双syntax、Wasmtime4-step、guest1-step、Web3-step、三目标no-work、Wasm
  hash/section、diff-check与零session audit闭合。证据见
  `analysis/motionplayer_engine_normal_destruction_owner_slot_write_order_four_binary_2026-08-21.md`。
- V265 已完成EmoteEngine ordinary destructor返回后的外层owner/deleting-destructor/final allocation
  release四端闭环：穷尽四端Engine dtor xref后证明Engine非多态且只有ordinary/nonvirtual dtor，
  不存在独立Engine deleting destructor；EmoteObject、NCB adaptor和typed-constructor pending local
  都显式执行Engine dtor→scalar delete，而adaptor/D3D deleting destructor最终释放的是各自外层
  shell。精确恢复EmoteObject三成员布局与Engine→RM→path teardown、adaptor native+sticky条件协议、
  typed attach success/`TJS_E_FAIL(-1008)`/EH三分叉，以及D3D secondary→primary销毁后paired-zero、
  listener/base teardown、最后delete D3D shell的完整链；同时纠正Android A32 adaptor连续cluster
  与边界外EH fragment的旧报告叙述。源码仅修正四处ownership注释，无行为改动。四库写回46
  comment/40 bookmark/21 rename，armv7经prebackup/candidate/idat/canonical readback安全保存；双
  syntax、Web82-step、Wasmtime119-step、guest1-step、顺序no-work、hash/section/diff-check/零session
  audit闭合，两主wasm与V264逐字节不变。证据见
  `analysis/motionplayer_engine_outer_owner_deleting_destructor_final_release_chain_four_binary_2026-08-21.md`。
- V266 已完成EmoteObject nested clone state-Variant raw copy与D3D listener-shell异常前沿四端闭环：
  精确区分EmoteObject ctor失败只delete pending `0x28/0x14` storage、serialize在state尚未live时
  抛出泄漏完整copy、unserialize在state live时只析构Variant仍泄漏copy三个阶段；外层D3D clone
  同样仅在shell ctor失败时delete pending `0x38/0x24` storage，ctor返回并AddListener后inner
  clone抛出则泄漏完整shell及listener registration。iOS armv7 `call_site=1/-1/2`与两套SJLJ
  dispatcher给出最直接证明，Android A32 function-end后EH fragment、A64内联landing、iOS A64
  split noreturn cleanup归一到同一source raw-local形状。同时发现四库prototype/comment与V132旧
  报告仍残留D3DImage误名，已按四端D3DLayer unbox链纠正。源码只补失败矩阵注释，无行为改动；
  四库写回49 comment/8 bookmark/7 rename/11 type update，armv7经prebackup/candidate/idat/
  canonical readback安全保存。双syntax、Web10-step、Wasmtime17-step、guest1-step、三目标no-work、
  hash/section/diff-check/stale-scan/零session闭合，两主wasm与V265逐字节不变。证据见
  `analysis/motionplayer_nested_clone_state_variant_raw_copy_listener_shell_unwind_four_binary_2026-08-21.md`。
- V267 已完成D3DLayerListener constructor→AddListener list-node注册提交、异常原子性与RemoveListener
  内部容器差异四端闭环：四端都在任何list写入前完成唯一throwing node allocation，随后初始化
  detached node并以no-throw link stores提交；iOS再发布cached size，因而allocation failure不产生
  half-registration。进一步收紧V266：D3DEmote shell ctor在AddListener成功后只有scalar/vptr write，
  constructor failure不可能遗留listener，真实泄漏只发生在ctor返回后的inner clone throw。Android
  使用无cached-size libstdc++ list（A64内联live erase，A32 specialization带alias guard），iOS使用
  带size libc++ list并把matching runs splice到temporary后clear，armv7还保留SJLJ temporary-owner
  cleanup。源码只补精确注释；四库写回48 comment/8 bookmark/20 rename/19 type update/24次定向
  recompile-readback并安全保存。双syntax、Web3/3、Wasmtime4/4、guest1/1、三目标no-work、Wasm
  hash/section、diff-check/stale-scan/零session/process均闭合，两主wasm与V266逐字节不变。证据见
  `analysis/motionplayer_d3dlayer_listener_registration_commit_exception_atomicity_four_binary_2026-08-21.md`。
- V268 已完成D3DLayer listener fan-out、OnUpdate原Variant identity、live-list mutation/reentrancy、
  callback exception与Draw target cache四端闭环。发现并修正portable真实偏差：四参考把来参
  state地址直接放入`parameters[1]`，无Variant copy/AddRef/Release/dtor/EH cleanup，且忽略
  FuncCall普通错误码；旧源码的临时Variant已删除。三个consumer都在callback返回后从current
  node读next，因而future erase会被跳过、tail append同轮可见、持续append可不终止、self-remove
  则UAF；matrix先提交再通知，throw不回滚；Draw只缓存一次target。回归锁定参数地址、定义良好
  mutation和exception/no-rollback。另从canonical fresh readback纠正旧IDB状态：Android A64
  实际仍合并trap+OnUpdate，现经candidate IDAPython安全拆分；A32恢复2-byte `UDF #0xFE`
  function并识别随后2-byte padding，推翻旧`UDF #0xDEFE`。四库写回80 comment/18 bookmark/
  22 rename/22 type/22 force-readback，四库idat/canonical回读闭合。双syntax、Web24/24（先修复
  stale toolchain cache）、Wasmtime4/4、guest1/1、三目标no-work、Wasm/diff/stale/零session
  审计通过；两主CODE各减少60 B，与删除临时Variant路径一致。证据见
  `analysis/motionplayer_d3dlayer_listener_fanout_variant_identity_reentrancy_exception_four_binary_2026-08-21.md`。
- V269 已完成root `UpdateObjects`/capture/Show fan-out、共享Variant owner、live red-black-tree
  cursor与UpdateState提交边界四端闭环：一整轮FrontItems pass只构造一个Variant，所有visible
  child及duplicate node收到同一地址；callback可改写Variant供后续child观察，但独立integer
  state保持入口snapshot。successor只在当前IsVisible/OnUpdate返回后从live node计算，future
  erase/insert改变后续结构路径，current erase为UAF；异常先析构可能已改写的共享Variant再逃逸。
  Show只在全轮正常返回后清零，故重入update在成功路径被0覆盖、随后抛出时保留；capture固定
  传0且不读/清root state。源码行为已准确，仅补ABI-neutral注释；回归增加duplicate参数地址、
  正常重入覆盖和异常重入保留断言。四库写回82 comment/19 bookmark/17 rename/16 type/17次
  force-readback，armv7经prebackup/candidate/idat/canonical fresh readback并同步最终canonical hash。
  双syntax、Web24/24（恢复stale toolchain cache）、Wasmtime4/4、guest1/1、三目标no-work、Wasm
  section/hash、diff/stale/零session/process审计闭合；Wasmtime主wasm与V268逐字节不变。证据见
  `analysis/motionplayer_root_updateobjects_shared_variant_tree_iterator_updatestate_commit_four_binary_2026-08-21.md`。
- V270 已完成root `getChildren` fresh Array/native Items、live owner与deque生命周期四端闭环，
  并修复portable真实偏差：参考构造`ncbArrayAccessor`、忽略fresh Array NativeInstanceSupport
  普通status后取得`tTJSArrayNI::Items`，对exact `IsValid==1`的node直接
  `emplace_back(ScriptOwner,ScriptOwner)`；旧源码逐项`PropSetByNum`引入了不存在的script
  setter/index/flag/error边界。IsValid验证调用前owner snapshot，但append接收同一live field地址
  两次，故重入替换值未经重验成为Object/ObjThis；successor仍在callback/append后计算，current
  erase为UAF。恢复Android libstdc++每block 25/42个packed Variant、iOS libc++ 204/341个，
  allocation→双AddRef→raw store→finish/size commit顺序，以及返回Array两次AddRef、accessor Release、
  body异常Release Array/已追加elements、Release再抛terminate矩阵。回归锁定duplicate native Items
  closure identity。四库写回131 comment/14 bookmark/13 rename/9 type/13 force-readback，armv7经
  candidate pre/post idat、canonical fresh readback及最终hash同步。双syntax、Web3/3、Wasmtime4/4、
  guest1/1、三目标no-work、定向Wasm反汇编、section/hash、diff/stale/零session/process均闭合；
  两主CODE各减少`0x97`。证据见
  `analysis/motionplayer_drawdevice_getchildren_native_array_items_live_owner_deque_lifecycle_four_binary_2026-08-21.md`。
- V271 已完成root `getPrimaryLayers` native Array、Managers snapshot与owner ref leak四端闭环，
  并修复第二条portable script-setter偏差：reference入口snapshot Managers begin/end，严格调用
  manager GetPrimaryLayer，再经old BaseLayer owning owner getter先AddRef一次，把cached owner作为
  Object/ObjThis直接emplace到native Items；旧源码的GetOwnerNoAddRef+temporary Variant+
  PropSetByNum既缺getter ref又多出script numeric setter。getter `+1`不进入任何RAII/cleanup，
  deque closure另AddRef两次且Array销毁只归还这两份，故每non-null manager每次读取永久泄漏1 ref；
  append抛出时Array/已提交elements回滚，刚取得的raw getter ref仍泄漏。恢复vector不reallocate
  append在saved end后本轮不可见、reallocation/erase/clear使cursor UAF、manager/layer strict null
  崩溃、null owner tagged-object等边界。回归锁定native closure及清空返回Array后refcount仍净增1。
  四库写回98 comment/12 bookmark/10 rename/8 type/10 force-readback，armv7 candidate/idat/
  canonical readback/hash同步闭合。双syntax、Web3/3、Wasmtime4/4、guest1/1、三目标no-work、定向
  Wasm、section/hash、diff/stale/零session/process均通过；两主CODE各减少`0x45`。证据见
  `analysis/motionplayer_drawdevice_getprimarylayers_native_array_manager_snapshot_owner_ref_leak_four_binary_2026-08-21.md`。
- V272 已完成root `getPrimaryLayerBitmap` index、manager-data item、construction-cached primary、
  raw image/texture handoff、alias/reentrancy与异常部分提交四端闭环，并纠正V150及portable的真实
  偏差：入口调用manager vtable `GetDrawDeviceData`而非`GetPrimaryLayer`，唯一正常no-op是item null；
  item非空后先严格转换target，再读取item构造时缓存的PrimaryLayer，依次ApplyFont、取单次MainImage
  与raw texture并tail AssignTexture，无source null guard、current-primary resample、AddRef或rollback。
  回归锁定null-item早退、negative/one-past-end、detach current primary仍读cached source、target native
  query重入清空data槽后本次仍沿snapshot item，以及source==target texture identity。四库写回88 comment/
  16 bookmark/16 rename/16 type/16 force-readback；armv7以恢复后authoritative current canonical另制
  prebackup/candidate，经idat/fresh readback后安全发布，未用旧V271 candidate覆盖。双syntax、Web/
  Wasmtime/guest build、三目标no-work、CTest无注册测试、三Wasm validate/construct、定向Wasm、section/
  hash、diff/stale/零session/process审计闭合；两主CODE各增加`0x0D`。证据见
  `analysis/motionplayer_drawdevice_getprimarylayerbitmap_manager_item_cached_primary_raw_texture_four_binary_2026-08-21.md`。
- V273 已完成`DrawDeviceManagerItem::Draw`后半渲染链与base/software slot-10 cache协议四端闭环，
  并修复三条portable真实偏差：两组geometry尺寸都来自同一drawBuffer且分处virtual conversion
  前后，返回texture尺寸完全不参与rect；software路径调用`GetPixelData`而非
  `GetScanLineForRead(0)`；private render manager在入口取得，source严格按
  `GetPixelData -> GetPitch -> direct Width/Height`取样。恢复source rect `{0,0,preW,preH}`、
  target rect `{offsetX,offsetY,postW,postH}`的raw right/bottom语义、一次CurrentTarget同时作为
  target/reference、无texture AddRef/null guard，以及cache miss先Release但不清字段、Create成功后
  才commit（抛出留下dangling pointer）的原版边界。回归新增2x2->3x2 reentrant texture probe，
  锁定`pixels,pitch`虚调顺序；四库各写回15 comment/3 bookmark/3 rename/3 type/3 force-readback，
  armv7经current-canonical prebackup、独立candidate、idat、fresh readback、六组件byte-identical
  publish和canonical最终idat/readback闭合。双syntax、Web/Wasmtime/guest build、三目标no-work、
  CTest无注册测试、三Wasm validate/construct、主/guest定向反汇编、hash/diff/stale审计均通过；
  相对V272三Wasm总大小为`+71/+71/+201`。证据见
  `analysis/motionplayer_drawdevice_manager_item_draw_render_chain_software_cache_four_binary_2026-08-21.md`。
- V274 已对root `capture` software/GPU Layer handoff与异常部分提交重新做四端fresh闭环，并明确
  纠正旧capture报告的两条过时结论：software首个texture虚调是`GetPixelData`而非
  `GetScanLineForRead(0)`；Create返回值直接发布到`CurrentTarget`后不再作为稳定local authority，
  child draw/target conversion之后分别为pixels、pitch、同组width/height、GPU AssignTexture及最终
  Release重读live field，故重入可让数据与Release来自多张不同texture。源码删除local target权威，
  恢复`GetPixelData`及分阶段field reload，并保留创建pointer被覆盖即泄漏、post-publication任意异常
  不Release/不clear、正常尾部Release当时live pointer等边界；旧V110报告同步修订。回归通过target
  NativeInstanceSupport嵌套一次3x2 capture并在严格Layer conversion抛出，锁定outer最终必须交付
  live 3x2 publication而非原始320x240 local。四库写回57 comment/17 bookmark/13 rename/17 type/
  17 force-readback；armv7以V273 authoritative canonical制作独立candidate，经save/idat/fresh
  readback/六组件byte-identical publish/canonical idat-readback闭合。双syntax、Web/Wasmtime/guest
  build、三目标no-work、CTest无注册测试、三Wasm validate/construct及main/guest定向field-load/vslot
  反汇编通过；相对V273三Wasm总大小`-1/-1/+8`。证据见
  `analysis/motionplayer_drawdevice_capture_live_currenttarget_pixel_handoff_four_binary_2026-08-22.md`。
- V275 已对root `StartBitmapCompletion`的draw-buffer、guarded statics、software completion-region
  intrusive circular-list与GPU full-rect路径做四端fresh闭环，并纠正当前源码及两份旧报告的真实
  顺序/对象偏差：函数完全不读取根`CurrentTarget`；target/reference必须在renderer predicate之后
  分支内取得；software固定为region Count/Head snapshot -> raw reference -> reentrant target virtual，
  GPU固定为bitmap width/height -> target virtual -> raw reference。恢复空region仍执行纹理取得、固定
  Head/target/reference snapshot、每rect重读target direct width/height、unsigned right/bottom比较失败即
  break全循环、无left/top/null/AddRef/Release/业务异常rollback等边界。源码按分支重排，旧completion
  CurrentTarget叙述同步修订；四库共写回28 comment/4 bookmark/4 rename/4 type，iOS armv7经独立
  candidate验证后发布，四库均以同一IDALib引擎完成auto-analysis/save及最终ready fresh readback。
  双syntax、Web/Wasmtime/guest build与三目标no-work、CTest无注册测试、三Wasm validate/construct、
  main/guest定向顺序反汇编、hash/section/diff/stale/零session/process审计通过；相对V274三Wasm总大小
  为`+39/+39/+141`。证据见
  `analysis/motionplayer_drawdevice_start_bitmap_completion_branch_snapshot_four_binary_2026-08-22.md`。
- V276 已对root `AddLayerManager`的base-vector提交、concrete HoldAlpha、item `new`/ctor与final
  manager-data发布重新做四端fresh闭环，并补齐旧报告遗漏的平台EH分裂：四端共同先append+AddRef，
  再HoldAlpha=false、predicate、构造item、最后SetDrawDeviceData，任一后续失败都不回滚先前提交；
  A64/I32 ctor escape会先guard-abort/base-dtor，外层再raw delete，A32/I64则无ctor或外层cleanup；
  四端final data-slot virtual均无cleanup，因此完整item可已挂front/back树却没有可回收的manager
  authority。源码保持普通new expression，仅补shipped compiler boundary注释；旧manager生命周期
  报告同步修订。四库共写回46 comment/14 bookmark/18 rename/16 type，iOS armv7两个SJLJ landing
  经独立candidate、发布、canonical auto-analysis及最终ready fresh readback闭合。双syntax、Web/
  Wasmtime/guest build、三目标no-work、CTest无注册测试、三Wasm validate/construct、main/guest定向
  Add顺序及catch覆盖反汇编、hash/section/diff/stale/零session/process审计通过；相对V275三Wasm
  总大小均为`+0`。证据见
  `analysis/motionplayer_drawdevice_add_layer_manager_vector_ctor_eh_publication_four_binary_2026-08-22.md`。
- V277 已对root/base `RemoveLayerManager`、base/software item complete/deleting destructor、parent
  detach与vector erase重入做四端fresh闭环：派生Remove严格data-first清槽再delete，最后才base
  remove，任一失败均不rollback；base在Release期间保持match/end公开，返回后用saved iterator和
  live end做memmove/--end，故callback append-no-realloc会进入tail shift，erase/reallocation可错删
  或UAF，shrink可让长度下溢。补齐A64/I32 destructor escape先做remaining base/list cleanup再
  terminate、A32/I64无local landing的compiler EH矩阵，以及raw delete只在complete正常返回后执行。
  `DrawDeviceD3D.cpp`、共享`DrawDevice.cpp`及两份旧报告均只补证据注释、无语义改写。四库共写回
  79 comment/22 bookmark/37 rename/34 type，armv7 candidate发布、canonical auto-analysis与四端最终
  ready fresh readback闭合。双syntax、Web/Wasmtime/guest build及no-work、CTest无注册测试、三Wasm
  validate/construct、派生/base Remove定向Wasm反汇编、hash/section/diff/stale/零session/process
  审计通过；相对V276三Wasm总大小均`+0`。证据见
  `analysis/motionplayer_drawdevice_remove_layer_manager_data_clear_destructor_release_reentry_four_binary_2026-08-22.md`。
- V278 已对共享`tTVPDrawDevice`析构的manager pointer-vector copy、fixed snapshot Release、callback
  重入与双storage teardown做四端fresh闭环：backup只复制裸指针不AddRef，第一次source size定exact
  capacity、allocation后第二次begin/end定copy长度；Release可修改live vector但不改变backup cursor，
  新append永不Release，提前删除later manager可令后续snapshot UAF。complete root四端都先析构
  secondary draw-device再primary root，故Release期间tree/item仍在但vptr已降base，重入Remove不会清
  plugin data/item；随后primary仅free tree nodes。补齐A64/I32 copy/Release escape清backup/live storage
  后terminate、A32/I64无local landing的EH矩阵。源码与旧报告只补注释；四库写回43 comment/15
  bookmark/13 rename/11 type，armv7 candidate/canonical/四端fresh readback闭合。双syntax、三build/
  no-work、CTest无注册测试、Wasm validate/Module及core destructor定向反汇编、hash/section/diff/stale/
  零session/process审计通过；相对V277总大小`+0/+0/+4`。证据见
  `analysis/motionplayer_drawdevice_destructor_manager_snapshot_release_reentry_four_binary_2026-08-22.md`。

相应证据记录位于：

- `analysis/motionplayer_internal_workspace_dimension_ncb_accessor_four_binary_2026-08-16.md`
- `analysis/motionplayer_resolve_source_ncb_accessor_chain_four_binary_2026-08-16.md`
- `analysis/motionplayer_engine_state_ncb_getvalue_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_node_tree_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_emote_controller_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_spring_metadata_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_leaf_controller_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_transition_builder_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_selector_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_loop_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_clamp_builder_ncb_accessor_shared_hint_four_binary_2026-08-16.md`
- `analysis/motionplayer_mirror_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_bust_builder_nested_ncb_accessor_vec3_hint_four_binary_2026-08-16.md`
- `analysis/motionplayer_chain_builder_nested_ncb_accessor_role_hint_four_binary_2026-08-16.md`
- `analysis/motionplayer_instant_variable_builder_ncb_accessor_indexed_ttstr_four_binary_2026-08-16.md`
- `analysis/motionplayer_timeline_builder_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_timeline_initialization_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_init_non_emote_nested_ncb_accessor_source_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_init_variables_nested_ncb_accessor_empty_scope_four_binary_2026-08-16.md`
- `analysis/motionplayer_shared_easing_nested_ncb_segment_base_unordered_four_binary_2026-08-16.md`
- `analysis/motionplayer_variable_slot_step_merge_nested_ncb_lifecycle_four_binary_2026-08-16.md`

- `analysis/motionplayer_separate_layer_adaptor_object_lifecycle_four_binary_2026-08-13.md`
- `analysis/motionplayer_shared_layer_factory_exception_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_separate_layer_adaptor_four_binary_2026-08-13.md`
- `analysis/motionplayer_private_motion_gll_lifecycle_four_binary_2026-08-13.md`
- `analysis/motionplayer_private_motion_gll_draw_gpu_four_binary_2026-08-13.md`
- `analysis/motionplayer_player_d3d_batch_renderer_four_binary_2026-08-13.md`
- `analysis/motionplayer_common_mesh_backend_four_binary_2026-08-13.md`
- `analysis/motionplayer_tjs_array_items_owner_four_binary_2026-08-14.md`
- `analysis/motionplayer_emote_hm6_hm7_containers_four_binary_2026-08-14.md`
- `analysis/motionplayer_emote_hm4_instant_set_four_binary_2026-08-14.md`
- `analysis/motionplayer_emote_hm1_hm2_mirror_cache_four_binary_2026-08-14.md`
- `analysis/motionplayer_bezier_basis_tessellator_four_binary_2026-08-13.md`
- `analysis/motionplayer_bezier_patch_layer_api_four_binary_2026-08-14.md`
- `analysis/motionplayer_layer_frame_extensions_four_binary_2026-08-13.md`
- `analysis/motionplayer_layer_copy_operate_four_binary_2026-08-13.md`
- `analysis/motionplayer_engine_direct_owner_unwind_four_binary_2026-08-13.md`
- `analysis/motionplayer_emote_engine_ctor_full_four_binary_2026-08-14.md`
- `analysis/motionplayer_render_helper_identity_four_binary_2026-08-14.md`
- `analysis/motionplayer_eye_entry_owner_emplace_four_binary_2026-08-13.md`
- `analysis/motionplayer_angle_controller_lifecycle_four_binary_2026-08-11.md`
- `analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md`
- `analysis/motionplayer_engine_state_pipeline_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_state_snapshot_restore_four_binary_2026-08-15.md`
- `analysis/motionplayer_collection_child_restore_wrappers_four_binary_2026-08-15.md`
- `analysis/motionplayer_collection_child_serialize_wrappers_four_binary_2026-08-15.md`
- `analysis/motionplayer_base_outerforce_state_snapshot_restore_four_binary_2026-08-15.md`
- `analysis/motionplayer_var_angle_state_serializer_owner_four_binary_2026-08-15.md`
- `analysis/motionplayer_state_dictionary_serialize_owner_family_four_binary_2026-08-15.md`
- `analysis/motionplayer_playing_timeline_info_dictionary_handoff_four_binary_2026-08-15.md`
- `analysis/motionplayer_variable_publication_variant_reset_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_build_variable_list_owner_pipeline_four_binary_2026-08-15.md`
- `analysis/motionplayer_eye_eyebrow_enqueue_lifecycle_four_binary_2026-08-11.md`
- `analysis/motionplayer_emoteplayer_ncb_surface_four_binary_2026-08-14.md`
- `analysis/motionplayer_d3d_emoteplayer_ncb_surface_four_binary_2026-08-14.md`
- `analysis/motionplayer_mouth_entry_owner_emplace_four_binary_2026-08-13.md`
- `analysis/motionplayer_clamp_entry_container_four_binary_2026-08-13.md`
- `analysis/motionplayer_transition_entry_owner_emplace_four_binary_2026-08-13.md`
- `analysis/motionplayer_selector_entry_owner_ctor_emplace_four_binary_2026-08-13.md`
- `analysis/motionplayer_loop_control_four_binary_2026-08-12.md`
- `analysis/motionplayer_hair_parts_entry_owner_ctor_emplace_four_binary_2026-08-13.md`
- `analysis/motionplayer_chain_entry_owner_ctor_emplace_four_binary_2026-08-13.md`
- `analysis/motionplayer_hm3_timeline_owner_lifecycle_four_binary_2026-08-13.md`
- `analysis/motionplayer_wind_raw_owner_replacement_four_binary_2026-08-13.md`
- `analysis/motionplayer_emoteobject_raw_owner_ctor_failure_four_binary_2026-08-13.md`
- `analysis/motionplayer_d3d_shell_raw_slot_protocol_four_binary_2026-08-13.md`
- `analysis/motionplayer_player_sla_raw_owner_lifecycle_four_binary_2026-08-13.md`
- `analysis/motionplayer_objsource_texture_owner_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_objsource_texture_exception_matrix_four_binary_2026-08-15.md`
- `analysis/motionplayer_player_source_workspace_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_module_map_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_texture_construction_exception_four_binary_2026-08-15.md`
- `analysis/motionplayer_test_provenance_comment_migration_four_binary_2026-08-15.md`
- `analysis/motionplayer_source_cache_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/motionplayer_objsource_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/motionplayer_layer_getter_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/motionplayer_motion_root_ncb_surface_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_d3d_adaptor_ncb_surface_factory_four_binary_2026-08-14.md`
- `analysis/motionplayer_module_dependency_registration_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_d3dlayer_object_listener_container_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_d3dimage_holder_managedset_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_drawdevice_transition_state_machine_four_binary_2026-08-15.md`
- `analysis/motionplayer_drawdevice_render_targets_capture_four_binary_2026-08-15.md`
- `analysis/motionplayer_drawdevice_front_back_pointer_multiset_four_binary_2026-08-15.md`
- `analysis/motionplayer_player_chara_pending_four_binary_2026-08-14.md`
- `analysis/motionplayer_player_stop_typed_wrapper_four_binary_2026-08-14.md`
- `analysis/motionplayer_frame_progress_state_machine_four_binary_2026-08-14.md`
- `analysis/motionplayer_evaluate_timeline_four_binary_2026-08-13.md`
- `analysis/motionplayer_parent_mesh_deformation_four_binary_2026-08-14.md`
- `analysis/motionplayer_vertex_mesh_chain_composition_four_binary_2026-08-14.md`
- `analysis/motionplayer_force_visible_geometry_mirror_four_binary_2026-08-14.md`
- `analysis/motionplayer_stencil_composite_render_items_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepared_priority_selection_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepared_particle_recursion_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepared_type3_wrapper_parent_link_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepared_ordinary_admission_publication_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepare_wrapper_stable_sort_four_binary_2026-08-14.md`
- `analysis/motionplayer_calc_bounds_particle_owner_four_binary_2026-08-14.md`
- `analysis/motionplayer_calc_bounds_type3_borrowed_child_four_binary_2026-08-14.md`
- `analysis/motionplayer_calc_bounds_point_container_selection_four_binary_2026-08-14.md`
- `analysis/motionplayer_calc_bounds_node_type_mask_shift_four_binary_2026-08-14.md`
- `analysis/motionplayer_get_command_list_division_conversion_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepared_bezier_division_conversion_four_binary_2026-08-14.md`
- `analysis/motionplayer_calc_view_division_conversion_four_binary_2026-08-14.md`
- `analysis/motionplayer_update_layers_mesh_division_compare_domain_four_binary_2026-08-14.md`
- `analysis/motionplayer_update_layers_unsigned_divide_zero_owner_four_binary_2026-08-14.md`
- `analysis/motionplayer_render_bezier_cell_division_four_binary_2026-08-14.md`
- `analysis/motionplayer_shared_d3d_adaptor_lifecycle_four_binary_2026-08-14.md`
- `analysis/motionplayer_update_layers_parameter_mode_reset_four_binary_2026-08-14.md`
- `analysis/motionplayer_update_layers_root_invariant_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_player_progress_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_playing_getters_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_draw_entry_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_update_layers_phase2_trace_projection_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_motion_sub_snapshot_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_prepared_items_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_build_render_commands_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_canvas_submit_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_geometry_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_post_draw_internal_layer_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_motion_load_pipeline_diagnostic_isolation_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_ncb_surface_constructor_four_binary_2026-08-14.md`
- `analysis/motionplayer_resource_manager_layer_id_allocator_four_binary_2026-08-14.md`
- `analysis/motionplayer_d3d_adaptor_dormant_prefix_state_four_binary_2026-08-15.md`
- `analysis/motionplayer_d3d_adaptor_state_consumers_four_binary_2026-08-15.md`
- `analysis/motionplayer_d3d_adaptor_capture_commit_boundaries_four_binary_2026-08-15.md`
- `analysis/motionplayer_d3d_adaptor_constructor_failure_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_d3d_adaptor_destructor_texture_map_four_binary_2026-08-15.md`
- `analysis/motionplayer_d3d_adaptor_software_texture_emplace_commit_four_binary_2026-08-15.md`
- `analysis/motionplayer_dispatch_member_hint_globals_four_binary_2026-08-15.md`
- `analysis/motionplayer_split_ttstr_container_boundary_four_binary_2026-08-15.md`
- `analysis/motionplayer_dispatch_property_access_helpers_four_binary_2026-08-15.md`
- `analysis/motionplayer_node_label_map_lookup_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_join_snapshot_four_binary_2026-08-11.md`
- `analysis/motionplayer_root_priority_reseek_cursor_boundary_four_binary_2026-08-15.md`
- `analysis/motionplayer_tag_absolute_reseek_four_binary_2026-08-15.md`
- `analysis/motionplayer_variable_track_absolute_reseed_four_binary_2026-08-15.md`
- `analysis/motionplayer_node_absolute_reseed_four_binary_2026-08-15.md`
- `analysis/motionplayer_variable_track_incremental_seek_four_binary_2026-08-15.md`
- `analysis/motionplayer_incremental_tag_root_streams_four_binary_2026-08-15.md`
- `analysis/motionplayer_remove_variable_label_owner_funccall_four_binary_2026-08-15.md`
- `analysis/motionplayer_selector_sync_publication_deque_compaction_four_binary_2026-08-15.md`
- `analysis/motionplayer_variable_frame_list_query_owner_return_four_binary_2026-08-15.md`
- `analysis/motionplayer_variable_range_dictionary_owner_handoff_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_label_array_owner_handoff_four_binary_2026-08-15.md`
- `analysis/motionplayer_loop_total_log_miss_value_abi_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_enumeration_empty_key_owner_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_play_log_commit_vector_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_blend_fade_lazy_commit_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_pass_cursor_erase_commit_four_binary_2026-08-15.md`
- `analysis/motionplayer_skip_reset_phase_owner_gate_four_binary_2026-08-15.md`
- `analysis/motionplayer_animating_filter_set_owner_short_circuit_four_binary_2026-08-15.md`
- `analysis/motionplayer_pre_progress_shared_residual_ordered_erase_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_contribution_checked_lookup_rounding_callers_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_window_null_data_cursor_routing_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_seek_cursor_clear_future_action_four_binary_2026-08-15.md`
- `analysis/motionplayer_timeline_initialization_commit_lifecycle_four_binary_2026-08-15.md`
- `analysis/motionplayer_set_variable_router_double_ease_integer_conversion_four_binary_2026-08-15.md`
- `analysis/motionplayer_primary_raw_controller_setters_four_binary_2026-08-15.md`
- `analysis/motionplayer_emoteplayer_clear_contains_typed_four_binary_2026-08-15.md`
- `analysis/motionplayer_init_physics_typed_binding_owner_four_binary_2026-08-15.md`
- `analysis/motionplayer_state_method_typed_binding_owner_four_binary_2026-08-15.md`
- `analysis/motionplayer_separate_layer_payload_map_node_abi_four_binary_2026-08-15.md`
- `analysis/motionplayer_node_frame_merge_member_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_player_load_parameter_node_member_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_update_layers_emote_edit_prior_draw_transform_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_particle_array_add_erase_hint_result_lifecycle_four_binary_2026-08-16.md`
- `analysis/motionplayer_resolve_render_source_hint_family_owner_tree_four_binary_2026-08-16.md`
- `analysis/motionplayer_pending_event_hint_live_end_result_lifecycle_four_binary_2026-08-16.md`
- `analysis/motionplayer_renderer_primitive_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_visible_setpos_opacity_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_get_bounds_isvalid_shared_hint_lifecycle_four_binary_2026-08-16.md`
- `analysis/motionplayer_init_parameter_hint_global_boundary_four_binary_2026-08-16.md`
- `analysis/motionplayer_old_node_reset_release_window_piled_hint_sequence_four_binary_2026-08-16.md`
- `analysis/motionplayer_is_exist_motion_private_hint_borrowed_receiver_four_binary_2026-08-16.md`
- `analysis/motionplayer_shared_random_hint_owner_lifecycle_four_binary_2026-08-16.md`
- `analysis/motionplayer_calc_view_param_shared_private_hint_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_play_impl_shared_type_hint_identity_four_binary_2026-08-16.md`
- `analysis/motionplayer_timeline_time_content_distinct_hint_pair_four_binary_2026-08-16.md`
- `analysis/motionplayer_bezier_bounds_shared_geometry_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_clear_whole_layer_shared_drawing_hint_family_four_binary_2026-08-16.md`
- `analysis/motionplayer_plural_drawing_method_shared_hint_pair_four_binary_2026-08-16.md`
- `analysis/motionplayer_motionlayer_clip_quartet_shared_update_hint_four_binary_2026-08-17.md`
- `analysis/motionplayer_blank_source_descriptor_shared_hint_identity_four_binary_2026-08-17.md`
- `analysis/motionplayer_motionnode_find_source_hint_unique_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_xy_shared_hint_idb_boundary_completion_four_binary_2026-08-17.md`
- `analysis/motionplayer_motionlayer_hold_alpha_shared_hint_bool_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_motionlayer_face_auto_bitmap_method_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_motionlayer_stretch_type_static_manager_snapshot_submit_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_position_control_tsp_hint_global_topology_four_binary_2026-08-17.md`
- `analysis/motionplayer_separate_layer_absolute_shared_hint_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_separate_layer_hit_threshold_shared_hint_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_separate_layer_assign_type_left_top_shared_hint_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_separate_layer_assign_double_read_set_size_shared_hint_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_source_cache_bake_shared_result_hint_family_boundary_four_binary_2026-08-17.md`
- `analysis/motionplayer_build_render_commands_primary_layer_on_demand_hint_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_render_source_key_shared_hint_read_write_family_four_binary_2026-08-17.md`
- `analysis/motionplayer_psb_owner_filter_std_function_layout_static_lifetime_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3d_clear_target_texture_local_static_cache_guard_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_alpha_mask_gpu_method_cache_guard_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_shared_render_selector_manual_null_cache_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_geometry_ncb_classinfo_adaptor_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_layer_getter_classinfo_adaptor_publication_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_objsource_classinfo_adaptor_publication_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_source_cache_classinfo_registration_adaptor_teardown_four_binary_2026-08-17.md`
- `analysis/motionplayer_resource_manager_classinfo_inheritance_adaptor_registration_four_binary_2026-08-17.md`
- `analysis/motionplayer_separate_layer_adaptor_classinfo_factory_registration_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3d_adaptor_classinfo_factory_owner_topology_four_binary_2026-08-17.md`
- `analysis/motionplayer_player_classinfo_adaptor_producer_owner_topology_four_binary_2026-08-17.md`
- `analysis/motionplayer_emoteplayer_classinfo_typed_factory_no_unregistration_four_binary_2026-08-17.md`
- `analysis/motionplayer_motion_root_classinfo_dormant_unregistration_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3d_emoteplayer_classinfo_factory_clone_owner_topology_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3d_emote_module_classinfo_constructor_double_owner_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3dlayer_classinfo_factory_adaptor_listener_owner_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3dimage_classinfo_factory_managedset_adaptor_owner_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3dpicture_classinfo_typed_factory_listener_ranges_adaptor_owner_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3d_classinfo_raw_factory_root_adaptor_containers_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_drawdeviced3d_classinfo_raw_factory_adaptor_exception_destructor_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3dlayerbase_classinfo_preregist_adaptor_sticky_failure_four_binary_2026-08-17.md`
- `analysis/motionplayer_d3dlayerobject_borrowed_adaptor_four_slot_container_reentry_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_internal_plugin_startup_xp3_only_drawdeviced3dz_dependency_four_binary_2026-08-17.md`
- `analysis/motionplayer_plugins_link_unlink_getlist_exact_key_registered_set_four_binary_2026-08-17.md`
- `analysis/motionplayer_storage_internal_module_visibility_getplacedpath_four_binary_2026-08-17.md`
- `analysis/motionplayer_physical_tpm_autoload_platform_name_rewrite_full_key_four_binary_2026-08-17.md`
- `analysis/motionplayer_autoload_count_publication_getter_deadstrip_four_binary_2026-08-17.md`
- `analysis/motionplayer_ncb_repeated_allregist_append_only_index_static_teardown_four_binary_2026-08-17.md`
- `analysis/motionplayer_ncb_hasmodule_deadstrip_source_test_diagnostic_four_binary_2026-08-17.md`
- `analysis/motionplayer_ncb_allunregist_android_survivor_ios_deadstrip_no_unload_consumer_four_binary_2026-08-17.md`
- `analysis/motionplayer_ncb_registrar_pointer_only_trivial_destructor_permanent_top_chain_four_binary_2026-08-17.md`
- `analysis/motionplayer_tvp_global_object_service_ownership_null_status_eh_four_binary_2026-08-17.md`
- `analysis/motionplayer_tvp_do_try_block_callback_exception_state_machine_four_binary_2026-08-17.md`
- `analysis/motionplayer_ttstr_hash_hint_cache_wrapper_core_make_layer_four_binary_2026-08-17.md`
- `analysis/motionplayer_psb_raw_node_variant_absolute_comment_migration_four_binary_2026-08-17.md`
- `analysis/motionplayer_ttstr_equality_backing_null_allocated_empty_bucket_admission_four_binary_2026-08-17.md`
- `analysis/motionplayer_krkr_atlas_rotated_palette_gate_record_layout_buffer_lifecycle_four_binary_2026-08-17.md`
- `analysis/motionplayer_krkr_atlas_pack_entry_publication_update_exception_owner_four_binary_2026-08-17.md`
- `analysis/motionplayer_krkr_atlas_outer_path_cache_retry_sourcestate_owner_four_binary_2026-08-17.md`
- `analysis/motionplayer_render_time_atlas_retry_software_bridge_resource_manager_leak_four_binary_2026-08-17.md`
- `analysis/motionplayer_find_source_spec_lazy_context_backing_dispatch_leak_four_binary_2026-08-17.md`
- `analysis/motionplayer_find_source_fallback_projection_accessor_hint_partial_commit_four_binary_2026-08-17.md`
- `analysis/motionplayer_win_source_root_live_key_texture_icon_projection_four_binary_2026-08-17.md`
- `analysis/motionplayer_sourcestate_path_ttstr_split_snapshot_live_retry_idb_recovery_four_binary_2026-08-17.md`
- `analysis/motionplayer_sourcestate_partial_constructor_copy_destroy_valid_consumer_four_binary_2026-08-17.md`
- `analysis/motionplayer_prepared_item_selective_ctor_native_layout_sidecar_commit_four_binary_2026-08-18.md`
- `analysis/motionplayer_prepared_ordinary_overwrite_exception_prefix_four_binary_2026-08-18.md`
- `analysis/motionplayer_prepared_type3_wrapper_stencil_stale_source_four_binary_2026-08-18.md`
- `analysis/motionplayer_prepared_priority_duplicate_final_drawn_stencil_four_binary_2026-08-18.md`
- `analysis/motionplayer_render_layer_id_latch_persistence_release_four_binary_2026-08-18.md`
- `analysis/motionplayer_mesh_point_array_variant_leaf_local_geometry_four_binary_2026-08-18.md`
- `analysis/motionplayer_leaf_clip_snapshot_descriptor_source_size_prefix_four_binary_2026-08-18.md`

## 仍需恢复

- 其余高价值对象的容器 ABI、调用链、析构/异常回滚与脚本边界；
- 仍含旧单目标绝对地址的注释、测试标签和 helper 名，需在对应四参考纵切面闭合时逐步
  替换为语义名；绝对地址只保留在 `analysis/` 的四端映射表中。

## 每个语义改动的证据门槛

1. 为四个目标建立同一语义的函数/数据映射；
2. 对四端重新取 decompile/disasm/xref，不沿用旧单目标注释；
3. 写出共同伪代码和平台/ABI 差异；
4. 与本地实现逐行比较，并在 `analysis/` 记录未闭合边界；
5. 只实现已由共同证据支持的行为，未知名称保留 `_guess`；
6. 将语义命名和注释写回四个 recovery IDB 并保存；
7. 至少执行 Web build、相关测试 TU/运行时检查与 `git diff --check`。

完整目标仍处于进行中；任何单一纵切面通过构建都不代表 motionplayer 已达到完整一比一。
