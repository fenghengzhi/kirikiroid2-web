# Motion 私有 OpenGL manager 与 D3DAdaptor render envelope 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

Motion 的 D3D 路径并不使用进程默认 render manager。四端共同拥有一个独立的 guarded
function-local raw pointer，它用精确宽字符串 `opengl` 调用 named manager lookup；
D3DAdaptor target 创建/替换/clear、software source 上传、target bind、method selector、
stencil、triangle submit 和 Private Motion GLL 都共享这个私有 manager。

`TVPIsSoftwareRenderManager()` 仍然检查进程默认 renderer，并决定 capture/software-copy
分支。这两个概念必须同时保留：默认 renderer 可以是 software，而 Motion D3D target 和
GPU 批处理仍固定落在私有 OpenGL manager。

本地此前只有 software-copy 上传使用私有 manager，其余 D3D target/clear/render backend
误用了 `TVPGetRenderManager()`。本轮把私有 getter提升为 Motion render-backend 共享 root，
并修复全部已映射 D3D/Private-GLL 调用点。

## 2. 私有 manager getter 等价类

| 平台 | getter | 完整指令数 | guarded raw slot | UTF-16LE `opengl` |
|---|---:|---:|---|---:|
| Android arm64 | `0x6930E4` | 51 | `0x1AB5528` / guard `0x1AB5530` | `0x14BEF82` |
| Android armv7 | `0x570EA0` | 46 | `0x11119B0` / guard `0x11119B4` | `0xD76D26` |
| iOS arm64 | `0x1000F3D90` | 28 | `0x101B699E8` / guard `0x101B699F0` | `0x10195D430` |
| iOS armv7 | `0xF0834` | 68 | `0x187D680` / guard `0x187D684` | `0x174F794` |

共同源级形状：

```text
getPrivateOpenGLRenderManager():
    static manager = TVPGetRenderManager(TJS_W("opengl"))
    return manager
```

初始化时构造临时 TJS string，做 named lookup，销毁临时 string，再发布 raw manager pointer
并 release guard。cached pointer 是 borrowed/process-lifetime 值，无退出析构、Release 或 null
检查。初始化失败的平台 EH cleanup 负责销毁临时 owner、abort active guard，再继续 unwind；
成功 guard 不会因后续 render 失败而重置。

## 3. 全编码字符串定位结果

普通 `find(type=string, "opengl")` 只返回 ASCII 字符串列表，未定位上述 TJS 宽字面量。
按 `ida-search-string` 流程补搜原始字节：

- UTF-8 模式：`6F 70 65 6E 67 6C`；
- UTF-16LE 模式：`6F 00 70 00 65 00 6E 00 67 00 6C 00`；
- UTF-32LE 模式：对应六个 4-byte code unit。

四个 IDB 的所有模式都读取到 `cursor.done=true`。UTF-32LE 无匹配；UTF-16LE 命中后读取
前后 40 bytes，确认上表四处都从独立 code-unit 边界开始、前面是前一字符串终止符、末尾
紧跟 UTF-16 null。xref 分别回到 getter 内的 `0x693120`、`0x570ECA`、
`0x1000F3DC0`、`0xF08A0` 一带。

iOS 还存在另一份 standalone `opengl`，xref 到 DrawDeviceD3D 自己的 manager getter；
Android 复用同一 literal 但有两个 getter xref。它们证明 DrawDevice 与 Motion 各自拥有
guarded cache，不能因名称相同就把两个 raw static owner 合并。

## 4. D3DAdaptor render envelope

| 平台 | render envelope | 完整指令数 | shared deep renderer | 显式 unwind cleanup |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6AB204` | 100 | `0x6AB39C` | body 内 owner cleanup chunks |
| Android armv7 | `0x57D2CC` | 79 | `0x57D3DC` | DWARF/normal function-owner cleanup disposition |
| iOS arm64 | `0x100104284` | 87 | `0x100104450` | libc++ function-owner paths内联 |
| iOS armv7 | `0x101680` | 140 | `0x101850` | `0x1017E6`，43 条 SjLj cleanup |

共同伪代码：

```text
D3DAdaptor.renderFromPlayer(player, mainList):
    if !canvasCaptureEnabled:
        return

    sourceGetter = std::function(capture player + this adaptor)
    manager = getPrivateOpenGLRenderManager()
    manager.SetRenderTarget(this.targetTexture)

    targetRect = {0, 0, this.width, this.height}
    adaptorGetter = std::function(capture this adaptor)
    sharedDeepRenderer(
        target=this.targetTexture,
        adaptorGetter,
        targetRect,
        sourceGetter,
        mainList,
        player,
        xOffset=0.5f,
        yOffset=0.5f)

    destroy adaptorGetter, then sourceGetter
```

`canvasCaptureEnabled=false` 在任何 allocation、manager lookup、target bind 或 lambda owner
构造之前返回。开启后，source getter 捕获 Player 和 adaptor 的 borrowed native pointers；
`std::function` 只拥有其 erased callable storage，不给 Player/adaptor 增加业务引用。target bind
发生在 target rect 与第二 callable 构造之前。

width/height 直接取 adaptor retained int32 字段，left/top 为 0；offset 固定是两个 `0.5f`，
不是 centerX/centerY。iOS armv7 cleanup 按构造状态逆序销毁两个 function owner，再 resume；
任何 target bind/deep-render 异常都不清 target、也不改 captureEnabled。

本地为了可维护性把 native 单体拆成
`D3DAdaptor::renderFromPlayer_guess -> Player::renderPreparedItemsToD3DTexture_guess` 两层；
只要 gate、callable capture、target bind、尺寸/offset 和 owner cleanup 保持上述顺序，这个提取
不改变可观察语义。shared deep renderer 的逐 item/method/batch body 继续作为下一 slice，不能
由 envelope 证据冒充闭合。

## 5. 私有 manager 消费闭包

四端 getter 各有 28 个 native xref，覆盖：

- D3DAdaptor 构造与 capture replacement 的 RGBA target factory；
- clear 的 `FillARGB` method 初始化与 in-place `OperateRect`；
- software source bridge 的 static texture upload；
- D3DAdaptor/Player target bind 与 shared deep renderer；
- blend/alpha-test method selector、stencil begin/end、triangle batch submit；
- Private Motion GLL 的 GPU triangle operation。

因此本地修复不是把“默认 manager 配置为 opengl”，而是显式建立一个独立
`motion::render_backend_guess::getPrivateOpenGLRenderManager_guess()`：

- `D3DAdaptor.cpp` 的 ctor、capture replacement、clear 和 software upload 改用它；
- `PlayerRenderTargets.cpp` 的 D3DLayer/D3DAdaptor target bind 与 batch manager 改用它；
- `MotionRenderBackend.cpp` 的 method selector、stencil、repeat upload 改用它；
- `PrivateMotionGLL.cpp` 的 triangle submit 改用它；
- `TVPIsSoftwareRenderManager()` 和所有普通 Canvas/software 分支保持默认 renderer 语义。

## 6. 验证与剩余边界

- 四端 manager getter、wide literal boundary/xref 与 D3DAdaptor render envelope 均 fresh
  decompile，并完整读取全部 disassembly；
- manager getter、literal、render envelope 和 iOS armv7 unwind cleanup 已命名/注释/书签，
  四个 IDB 已原位保存；
- `git diff --check` 与覆盖 TSV 严格字段检查通过；
- 当前环境缺少 CMake、Ninja 和 Emscripten，且单头文件语法检查被缺失的
  `boost/locale.hpp` 阻塞，因此本 slice 不宣称正式 native/Web 构建。

software source getter 的 map lookup/insert/failure 引用边已由
`MP-R14-D3D-SOURCE-GETTER-MAP-INSERT` 闭合；四端 shared deep renderer 的 item admission、
method/cache、batch flush、stencil 与异常 owner 状态又由
`MP-R14-D3D-DEEP-BATCH-STENCIL` 闭合，公共 mesh submit/repeat/cell/AABB helper 随后由
`MP-R14-D3D-MESH-SUBMIT-CELLS` 闭合。相邻 Bezier basis/tessellation helpers 后续也由
`MP-R14-BEZIER-BASIS-TESSELLATION` 闭合。
