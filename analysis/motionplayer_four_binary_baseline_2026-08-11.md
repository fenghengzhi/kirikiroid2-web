# MotionPlayer 四参考二进制基线与 Join reset 调用链（2026-08-11）

## 1. 目标与证据边界

本轮把 `reference/binaries/` 下四个目标共同视为权威，重新建立 MotionPlayer 的注册入口，并沿 `Motion.Player.play` 追到 `PlayFlagJoin` 的 reset/snapshot 数据流。目的不是把旧 `libkrkr2.so` 地址平移过来，而是找出四个产物共享的源码结构，同时明确 ABI、STL 和编译器内联造成的差异。

本轮实际打开并完成 Hex-Rays/缓存预热的数据库如下：

| 目标 | 输入文件 | IDB | 会话 |
| --- | --- | --- | --- |
| Android arm64 | `reference/binaries/Kirikiroid2_1.3.9_Android_arm64-v8a.so` | 同名 `.so.i64` | `motion_android_arm64` |
| Android armv7 | `reference/binaries/Kirikiroid2_1.3.9_Android_armabi-v7a.so` | 同名 `.so.i64` | `motion_android_armv7` |
| iOS arm64 | `reference/binaries/Kirikiroid2_1.3.9_iOS_arm64` | 同名 `.i64` | `motion_ios_arm64` |
| iOS armv7 | `reference/binaries/Kirikiroid2_1.3.9_iOS_armv7` | 同名 `.i64` | `motion_ios_armv7` |

四个会话均报告自动分析完成、Hex-Rays 可用、函数/字符串/交叉引用缓存可用。Android 映像基址为 `0`；iOS arm64 为 `0x100000000`；iOS armv7 为 `0x4000`。

## 2. 字符串搜索与注册入口

### 2.1 `doAlphaMaskOperation`

普通 IDA 字符串搜索在四个库中均为空。按宽字符串规则继续搜索原始字节：UTF-8 与 UTF-32LE 均为空，UTF-16LE 在每个目标中得到唯一命中。命中前后字节和尾部双零均已通过边界读取确认。

| 目标 | UTF-16LE 字面量 | 引用它的 Motion 根 registrar |
| --- | ---: | ---: |
| Android arm64 | `0x14D6A2E` | `sub_6D6EE8@0x6D6EE8` |
| Android armv7 | `0xD86288` | `sub_5991D0@0x5991D0` |
| iOS arm64 | `0x10195D3DA` | `sub_100125974@0x100125974` |
| iOS armv7 | `0x174F73E` | `sub_124B7C@0x124B7C` |

四个 registrar 均在本轮重新反编译。它们共享以下源码级顺序：

```text
注册 23 个常量：
  LayerTypeObj=0, LayerTypeShape=1, LayerTypeLayout=2,
  LayerTypeMotion=3, LayerTypeParticle=4, LayerTypeCamera=5,
  ShapeTypePoint=0, ShapeTypeCircle=1, ShapeTypeRect=2, ShapeTypeQuad=3,
  PlayFlagForce=1, PlayFlagChain=2, PlayFlagAsCan=4,
  PlayFlagJoin=8, PlayFlagStealth=16,
  TransformOrderFlip=0, TransformOrderSlant=3,
  TransformOrderZoom=2, TransformOrderAngle=1,
  CoordinateRecutangularXY=0, CoordinateRecutangularXZ=1,
  MaskModeStencil=0, MaskModeAlpha=1

随后注册 11 个子类：
  Point, Circle, Rect, Quad, LayerGetter, Player,
  SourceCache, ObjSource, ResourceManager, SeparateLayerAdaptor, D3DAdaptor

最后把两个方法挂到 Motion 命名空间对象：
  doAlphaMaskOperation, getD3DAvailable
```

差异只在生成代码：Android arm64 registrar 长 `0x784`，内联了大量子类包装器分配/失败路径；Android armv7、iOS arm64、iOS armv7 分别长 `0x1FC`、`0x328`、`0x2EC`，更多地保留小 helper。32 位包装上下文中的状态指针位于第 5 个槽，iOS arm64 位于第 9 个槽，Android arm64 则被内联消解。这些都不是注册顺序或所有权差异。

本地 `cpp/plugins/motionplayer/main.cpp` 的 23 个常量、11 个子类及两个命名空间方法已经逐项匹配。旧注释只引用单个 `libkrkr2.so`，本轮已改成四目标口径。此基线最初只确认了 `doAlphaMaskOperation` 的 registrar 所有权与参数入口；其函数体随后已完成四份逐体对照和本地修复，完整证据见 `analysis/motionplayer_alpha_mask_four_binary_2026-08-11.md`。不得继续把本段的早期 scope 限制作成当前未完成状态。

基线阶段记录、现已由后续专项完成审计的函数体地址：

| 目标 | `doAlphaMaskOperation` | `getD3DAvailable` |
| --- | ---: | ---: |
| Android arm64 | `sub_6AC4E4@0x6AC4E4` | `sub_6ADD40@0x6ADD40` |
| Android armv7 | `sub_57E1E8@0x57E1E8` | `sub_57F4A8@0x57F4A8` |
| iOS arm64 | `sub_100104E68@0x100104E68` | `sub_10010654C@0x10010654C` |
| iOS armv7 | `sub_10243C@0x10243C` | `sub_103908@0x103908` |

### 2.2 Player registrar 与 `play`

`pixelateDivision` 用于可靠定位 Player member registrar。Android 两份只有一个 UTF-16LE 实例；iOS 两份各有 Player 与 D3D registrar 两个实例，按 xref 所属 registrar 区分。

| 目标 | Player `pixelateDivision` 字面量 | Player member registrar |
| --- | ---: | ---: |
| Android arm64 | `0x14BE890` | `sub_6D3DA8@0x6D3DA8` |
| Android armv7 | `0xD766DC` | `sub_597EC8@0x597EC8` |
| iOS arm64 | `0x10195CD82` | `sub_1001244F8@0x1001244F8` |
| iOS armv7 | `0x174F0E6` | `sub_123848@0x123848` |

根 registrar 中 Player 子类包装 helper 也完成了新反编译：

| 目标 | Player 子类 helper | 内层 setup |
| --- | ---: | ---: |
| Android arm64 | `sub_6FB0E4@0x6FB0E4` | `sub_6FB254@0x6FB254` |
| Android armv7 | `sub_5996F4@0x5996F4` | `sub_5B677C@0x5B677C` |
| iOS arm64 | `sub_100125FEC@0x100125FEC` | `sub_10014DC04@0x10014DC04` |
| iOS armv7 | `sub_125104@0x125104` | `sub_14F880@0x14F880` |

Player 的精确 `play` UTF-16LE 字面量、NCB wrapper、native `play`、`playImpl` 与 Join reset 如下：

| 目标 | `play` 字面量 | NCB wrapper | native `play` | `playImpl` | Join reset |
| --- | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | `0x14BD70A` | `sub_6CFFE8@0x6CFFE8` | `sub_6AF5C8@0x6AF5C8` | `sub_6AF664@0x6AF664` | `sub_6AFF5C@0x6AFF5C` |
| Android armv7 | `0xD75F56` | `sub_59565C@0x59565C` | `sub_5800EC@0x5800EC` | `sub_580158@0x580158` | `sub_580668@0x580668` |
| iOS arm64 | `0x10195CE38` | `sub_1001212C0@0x1001212C0` | `sub_1001074A4@0x1001074A4` | `sub_100107540@0x100107540` | `sub_100107B90@0x100107B90` |
| iOS armv7 | `0x174F19C` | `sub_120050@0x120050` | `sub_104A7C@0x104A7C` | `sub_104AE8@0x104AE8` | `sub_1051AC@0x1051AC` |

四份 wrapper、native `play`、`playImpl`、Join reset 均在本轮重新反编译。共同调用链为：

```text
Motion.Player.play NCB wrapper
  -> 从 NCB 对象解析 native Player
  -> 要求 argc >= 2
  -> argv[1] 转 flags，复制 motion variant
  -> Player::play(motion, flags)
       if flags 含 Stealth 且特定资源指针为空：
           retain 待播 variant
       else：
           playImpl(motion, flags)
           若已有待播 stealth variant：以 Stealth 再播一次并 release
             -> playImpl 在 flags 含 Join 时调用 reset
```

## 3. Join reset：共同数据流和容器边界

### 3.1 函数映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| clear HM3/HM4 | `sub_6B54C4@0x6B54C4` | `sub_583A54@0x583A54` | `sub_10010BC60@0x10010BC60` | `sub_109614@0x109614` |
| 插值+节点求值聚合 helper | 内联进 reset | `sub_583A82@0x583A82` | `sub_10010BCA0@0x10010BCA0` | `sub_109642@0x109642` |
| variable-track 插值器 | `sub_6B9200@0x6B9200` | `sub_5860BC@0x5860BC` | `sub_10010F094@0x10010F094` | `sub_10C8D2@0x10C8D2` |
| 单节点 timeline evaluator | `sub_696EC4@0x696EC4` | `sub_573158@0x573158` | `sub_1000F6C34@0x1000F6C34` | `sub_F3894@0xF3894` |

除 Android arm64 的聚合 helper 被内联外，表中所有角色都在本轮重新反编译。三份独立聚合 helper 都只有 reset 一处 xref，控制流相同。四份单节点 evaluator 也已重新反编译；本轮不修改其算法体，只依据函数入口和调用点修正源码参数顺序。

### 3.2 共同伪代码

```cpp
void Player::resetForJoin_guess() {
    if (queuing)
        return;

    perNodeLayerStateMap.clear(); // HM3
    variableSnapshotMap.clear();  // HM4

    // 三个目标保留 helper；Android arm64 把这一段内联进 reset。
    interpolateVariableTracks();  // 无显式时间参数，读取 Player.currentTime
    for (size_t i = 1; i < nodes.size(); ++i) {
        nodes[i].flags = 1;
        evaluateTimeline(nodes[i], Player.currentTime, true);
    }

    for (VariableLabelScope &item : variableTracks) {
        Slot &active = item.slot[item.cursor & 1];
        if (!active.typeZeroFlag)
            variableSnapshotMap[item.cascadeKey] = item.value;
    }

    for (size_t i = 1; i < nodes.size(); ++i) {
        Node &node = nodes[i];
        if (!node.joinTarget)
            continue;
        if (node.nodeType <= 8 && ((1 << node.nodeType) & 0x19D)) {
            Key key = buildNodePathKey(nodes, i);
            initPerNodeSnapshot(node, perNodeLayerStateMap[key]);
        }
    }
}
```

关键边界行为：

- `_queuing` 为真时，两个 map 均不清空，节点也不求值；函数直接返回。
- 两个节点循环都从索引 `1` 开始并以 `i < nodes.size()` 结束；根节点 `0` 明确排除，不存在尾哨兵节点。
- reset 先清 HM3/HM4，再插值、求值，最后按 HM4、HM3 的顺序重建快照。
- timeline evaluator 的返回值在聚合 helper 中被忽略；设置 `node.flags = 1` 和显式 `dirty=true` 仍然都保留。
- HM4 只收录 active slot 非 type-zero 的 variable track；key 是 cascade key，value 是插值器刚写入的 `item.value`。
- HM3 先检查 `joinTarget`，再检查节点类型掩码 `0x19D`；key 是节点路径而不是 raw label。
- Android arm64 中的大段 deque 块指针/除法运算是 libstdc++ 生成细节；另外三份 libc++/32 位运算形式不同，但源码边界一致。

### 3.3 两个源码签名修正

#### variable-track 插值器没有显式时间参数

四份函数都从 Player 对象读取当前评估时间，而不是从参数读取：Android arm64、Android armv7、iOS arm64、iOS armv7 的对象内偏移分别为 `+456`、`+288`、`+344`、`+228`。偏移差是对象 ABI 差异，所有目标的源码语义都是无参 Player member。

本地旧声明 `interpolateVarTrackValuesLike_0x6BBE20(double)` 把对象字段错误提升成了显式参数。本轮改为无参，并让 reset 与正常 update 路径都直接调用无参 member。

#### evaluator 的源码参数顺序是 `(node, currentTime, dirty)`

Android armv7 的反编译签名直接恢复为 `(node, double, int)`；聚合调用点把 `node` 放入 `R0`、`currentTime` 放入 `R2:R3`，把常量 true 放在栈参数槽。iOS armv7 调用点把时间放入 `R1:R2`、bool 放入 `R3`。两份 arm64 因 AAPCS64 把浮点和整数参数分配到不同寄存器组，单看 Hex-Rays 原型会显示 `char` 在 `double` 前，但调用点和两个 32 位目标共同证明源码顺序仍为 `(node, double currentTime, bool dirtyArg)`。

本地旧声明为 `(node, dirtyArg, currentTime)`。行为在本地调用者与被调者同时错误排序时可能暂时自洽，但它不等于原始源码结构。本轮同步修改了声明、定义、update 调用和 reset 聚合调用。

## 4. 本地逐项对照与本轮修改

| 四目标共同结构 | 修改前本地状态 | 本轮处理 |
| --- | --- | --- |
| 插值器读取 Player 当前时间、无显式参数 | 接受 `double clampedEvalTime`，两个调用者传 `_clampedEvalTime` | 改成无参 member，函数体直接读 `_clampedEvalTime` |
| evaluator 源码顺序 `(node, time, dirty)` | 声明/定义/调用均为 `(node, dirty, time)` | 同步修正声明、定义和所有调用点 |
| reset 在快照前插值并求值全部非根节点 | 只插值；注释把节点循环标为 `DEFERRED` | 新增 `evaluateTimelinesForJoinSnapshot_guess()` 并由 reset 调用 |
| 聚合 helper 先插值，再从索引 1 遍历节点、设 flags=1、强制 dirty | 本地无对应 helper | 按三份独立函数和 A64 内联体恢复；返回值显式忽略 |
| registrar 结论必须来自四目标 | `main.cpp` 注释只引用旧 `libkrkr2.so` | 改成四目标所有权/顺序口径；地址移入本文 |
| Emote controller builder/step 路径已实现 | 仍有“builder 未移植、deque 永远为空”和 `STUB_WARN` 注释 | 清除已被现有实现证伪的注释；保留真实实现路径说明 |

新增 helper 使用 `_guess`，因为四份产物都没有保留可证明的精确 C++ 源名。旧的 `Like_0x...` 名称是现有移植层的历史交叉引用命名，本轮未做全仓重命名。

## 5. 已知差异与后续审计队列

1. `doAlphaMaskOperation` 四个函数体已在同一轮完成审计和修复；完整证据见 `analysis/motionplayer_alpha_mask_four_binary_2026-08-11.md`。
2. `getD3DAvailable` 与 cached software-renderer helper 已完成四体对照、实现修复和 IDB 改进；见本文第 8 节。
3. 仓库中仍有大量只写 `libkrkr2.so` 的历史注释。这些注释是审计线索，不应自动当作四参考证据；修改对应语义前必须重新建立四文件函数映射。简单弹簧和链式胸部弹簧都已由后续专项完成该迁移，分别见 `analysis/motionplayer_simple_spring_four_binary_2026-08-11.md` 与 `analysis/motionplayer_bust_chain_spring_four_binary_2026-08-11.md`。本条早期版本把后者列为待办，现已被后续证据和实现证伪。
4. reset 的直接后继 `init/load motion -> reseek/prune HM3/HM4 -> updateLayers` 已由后续 `progress_reseek`、`join_snapshot`、`variable_track` 以及各 updateLayers 纵切完成对象生命周期与容器审计。此条保留为基线阶段的历史 scope，不能再视为当前待办。
5. Android arm64 的 helper 内联不能被误判成“该平台缺少步骤”；相反，源码结构应由另外三份保留 helper 的目标与 A64 内联体共同恢复。

## 6. IDB 改进

本轮先对四库分别执行 rename dry-run，全部通过后才写入并保存 IDB。由于产物没有保留足以证明原始 C++ 拼写的符号，39 个新名称都按规则保留 `_guess`：

| 已确认角色 | 写入的 IDB 名称 |
| --- | --- |
| Motion 根 registrar | `Motion_ncb_register_guess` |
| Player member registrar | `Player_ncb_registerMembers_guess` |
| `play` NCB wrapper | `Player_play_ncb_guess` |
| native `play` | `Player_play_guess` |
| `playImpl` | `Player_playImpl_guess` |
| Join reset | `Player_resetMotionState_guess` |
| HM3/HM4 clear helper | `Player_clearJoinSnapshotMaps_guess` |
| 插值+节点求值聚合 helper | `Player_evaluateTimelinesForJoinSnapshot_guess` |
| variable-track 插值器 | `Player_interpolateVarTrackValues_guess` |
| 单节点 evaluator | `Player_evaluateTimeline_guess` |

Android arm64 没有独立聚合 helper，因此该库写入 9 个名称；其余三库各写入 10 个。四次保存均成功。

## 7. 构建与测试结果

验证工具链：CMake `4.4.2`、Ninja `1.13.2`、Bison `3.8.2`、Emscripten `4.0.23`。Bison 满足项目要求。构建时显式设置了 `EMSDK`、`EMSDK_PYTHON` 和 `VCPKG_ROOT`。

1. 现有 `out/web/debug` 增量构建成功：78 个步骤完成，`motionplayer` 静态库和最终 WebAssembly 均重新链接。只有仓库既有的 TJS/Emscripten warnings。
2. 新建 `Wasmtime Headless Debug Config` 构建树成功；116 个 vcpkg 包从本机二进制缓存恢复。
3. `krkr2_wasmtime_guest` 全量构建成功：286 个步骤完成并生成 `out/wasmtime/debug/krkr2_wasmtime_guest.wasm`。
4. 对同一 guest 再次增量构建得到 `ninja: no work to do`，依赖图已收敛。
5. `test_motion_timing.py` 与 `test_motion_playback_strict_oracle.py` 共 10 项离线单测全部通过。
6. 完整 motion-playback Wasmtime 运行回归尚未执行：仓库声明的主机依赖 `wasmtime/PyOpenGL/glfw/Pillow` 未安装；尝试安装到隔离的 `out/python-wasm-deps` 时，主机在连接 PyPI 前发生 DNS 解析失败，本地 pip cache 也没有 `wasmtime` wheel。这是测试环境依赖阻塞，不是 guest 编译或源码测试失败。

## 8. `getD3DAvailable` 四目标对照

四份 namespace free function 及其 backend helper 均在本轮重新反编译：

| 目标 | `getD3DAvailable` | cached backend helper |
| --- | ---: | ---: |
| Android arm64 | `sub_6ADD40@0x6ADD40` | `sub_848BDC@0x848BDC` |
| Android armv7 | `sub_57F4A8@0x57F4A8` | `sub_65728C@0x65728C` |
| iOS arm64 | `sub_10010654C@0x10010654C` | `sub_100323EB8@0x100323EB8` |
| iOS armv7 | `sub_103908@0x103908` | `sub_32930C@0x32930C` |

四份 free function 都是：

```cpp
return cachedBackendFlag() ^ 1;
```

四份 helper 都使用 C++ guard variable 只初始化一次：取得全局 render manager，调用同一个虚函数槽并把低位 bool 缓存在静态字节。`doAlphaMaskOperation` 在该 helper 为真时走 CPU scanline，为假时走 GPU program；因此该槽是 `IsSoftware()`，共同源码结构为：

```cpp
bool cachedBackendFlag() {
    static bool isSoftware = GetRenderManager()->IsSoftware();
    return isSoftware;
}

bool getD3DAvailable() {
    return !cachedBackendFlag();
}
```

本仓库已经有同结构的 `TVPIsSoftwareRenderManager()`：内部以 function-local static 缓存 `TVPGetRenderManager()->IsSoftware()`。修改前 `motion_getD3DAvailable()` 硬编码返回 `true`，导致 software backend 也报告 D3D 可用；正确的平台映射应直接返回 `!TVPIsSoftwareRenderManager()`，同时保留原版的一次初始化语义。

该修改已实施，并通过 Web Debug（3 个增量步骤）与 Wasmtime guest（2 个增量步骤）重新链接。四个 IDB 均先通过 dry-run，再写入 `Motion_getD3DAvailable_guess` 与 `TVPIsSoftwareRenderManager_guess` 并保存成功。
