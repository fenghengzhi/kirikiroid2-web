# Player direct SeparateLayer 路由与 private Motion GLL owner（四端，2026-08-27）

## 1. 入口、函数量与 fresh 证据

### direct SLA coordinator

| 端 | 入口 | body instructions | 独立 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6D2A38` | 184 | body 内 landing pads |
| Android armv7 | `0x597328` | 118 | — |
| iOS arm64 | `0x1001233C8` | 121 | table-driven unwind |
| iOS armv7 | `0x12257C` | 207 | `0x1227B6`, 76 instructions |

四端 630 条 coordinator body 和 iOS armv7 76 条 SjLj cleanup 均已 fresh full
decompile/disassemble。

### ensure private Motion GLL native Layer

| 端 | 入口 | body instructions | 独立 cleanup |
|---|---:|---:|---:|
| Android arm64 | `0x6D2D28` | 142 | body 内 guard/Variant cleanup |
| Android armv7 | `0x5974D0` | 110 | — |
| iOS arm64 | `0x100123670` | 103 | table-driven unwind |
| iOS armv7 | `0x122884` | 169 | `0x122A66`, 21 instructions |

四端 524 条 ensure body 和 iOS armv7 21 条 SjLj cleanup 同样全部读取。两组入口、
accurate/post/legacy callees、software-render probe 和 cleanup 已命名、注释/bookmark，
四个 IDB 已保存。

## 2. direct SLA 共同控制流

```cpp
void Player::renderToSeparateLayerAdaptor(SeparateLayerAdaptor *sla) {
    PreparedRenderItemList mainList;
    PreparedRenderItemList auxList;
    if (!prepareRenderItems(mainList, auxList)) return;
    applyPreparedRenderItemProjection(mainList);

    static bool accurate =
        TVPIsSoftwareRenderManager()
            ? true
            : IndividualConfigManager::GetInstance()
                  ->GetValue<bool>("ogl_accurate_render", false);

    if (accurate) {
        renderAccurateSeparateLayerAdaptor(sla, mainList, auxList);
        updateAccurateSLAAfterDraw(tTJSVariant(sla->targetLayer));
    } else {
        auto *privateLayer = ensurePrivateMotionGLL(sla);
        int width = privateLayer->GetRect().right
                  - privateLayer->GetRect().left;
        int height = privateLayer->GetRect().bottom
                   - privateLayer->GetRect().top;
        privateLayer->stencilCount =
            privateLayer->buildCommands(width, height,
                                        this, mainList, auxList);
        if (!TVPWindowUpdateEventsDelivering)
            privateLayer->Update(false);
    }
}
```

backend choice 的 static guard 位于 prepare 和 projection 之后。因此早期 prepare false
不会初始化它，projection 抛异常也不会读取 renderer/config；第一次成功到达 guard
才决定进程余生使用哪条 backend。software renderer 无条件 true，否则只读一次
`ogl_accurate_render`、default false。初始化抛异常会执行 guard_abort，后续调用重试；
guard_release 后配置变化不再生效。

本地原来每次调用 `isAccurateSlaRenderEnabled()`，且用 `TVPGetRenderManager()` 手工
判 software、允许 config singleton 为空。四端明确是 `TVPIsSoftwareRenderManager()`
和无 null guard 的 singleton dereference。本轮已恢复精确 helper 和函数内 static。

## 3. accurate 分支 owner 顺序

四端先把 Player、SLA、main、aux 传给 accurate renderer。返回后才从 SLA 的
`_targetLayer` 按值 copy 一个完整 Variant（Object 和 ObjThis 两个 owner），把该临时
值传入 accurate post-draw，再立即析构。没有在 render 前预取 target，没有诊断 raw
Layer resolve，也没有 trace scope/log/summary。

本地原有 headless/logo sidecar 会在 renderer 前额外复制 target、做 Layer resolve，
并改变异常与 owner 顺序；本轮已从 coordinator 全部删除。accurate renderer 和
`updateAccurateSLAAfterDraw` 的内部图层分配、piledCopy、逐 item owner 仍是后续两个
独立深层切片。

## 4. legacy 分支与窗口事件 gate

legacy 分支先确保 private Motion GLL native Layer；ensure 每次都把它 resize 为 target
Layer 当前 Rect 的 width/height。coordinator 随后再次从 private Layer Rect 读取
`right-left` 和 `bottom-top`，将两值传给 command builder。builder 返回 stencil count，
四端直接写入 private Layer 字段。最后只在全局
`TVPWindowUpdateEventsDelivering == false` 时调用 `Update(false)`；true 时完全跳过，
不排队补偿更新。

本地现已保留 native Layer pointer 读取尺寸并直接写 stencil，不再为这两步重复做
private class-ID resolve。但当前 `buildPrivateMotionGLLCommands_guess` 仍以内部 script
dispatch 为参数，通过 dispatch-based queue helpers 清空/append；参考是 native
private Layer 成员函数直接访问 queue。因此 direct coordinator 的 backend/gate/commit
顺序已有四端证据并恢复，但 legacy builder integration 尚不能标记完整 IMPLEMENTED，
必须在读取四端 builder 全 body 后连同 queue 容器一起重构。

## 5. ensurePrivateMotionGLL 精确源结构

共同算法：

1. 严格从 SLA `_targetLayer` Variant 得到 native Layer；
2. 检查 `_privateTarget` type tag；Object 时严格 class-ID 解析并复用 native private
   Layer，Void 时进入创建；其他 type 在 Object conversion 处抛错；
3. 创建路径以函数内 static private class dispatch 调 `CreateNew(argc=2,
   [owner,target], objthis=global)`；
4. 将返回 dispatch 同时作为 Object/ObjThis 赋给 `_privateTarget`，释放 CreateNew 的
   原始返回引用；
5. 从新 Variant 解析 borrowed native private Layer，先写 SLA absolute，再 visible=true；
6. 无论新建/复用，都以 target Rect 的 unsigned width/height `SetSize`；
7. 返回 borrowed `tTJSNI_BaseLayer*`，不是 script dispatch。

原本本地步骤 1..6 基本一致，但错误地把 `_privateTarget` dispatch 作为返回值，迫使
caller 再做 NativeInstanceSupport。本轮把声明、friend、实现和测试统一恢复成 native
Layer 返回；iOS armv7 cleanup 证明 static private-class 创建失败会 guard_abort，live
created Variant 会按 call-site 析构。没有失败回滚已提交的 `_privateTarget`。

## 6. 本地修改、测试与状态

修改覆盖：

- `PlayerRenderInternal.cpp`：software/config helper 恢复为四端调用形状；
- `PlayerRenderTargets.cpp`：static backend choice、移除 coordinator trace，恢复
  accurate/legacy 顶层顺序和 native Layer stencil 写入；
- `PrivateMotionGLL.h/.cpp`、`SeparateLayerAdaptor.h`：ensure 返回 native Layer，
  stencil setter 接收 native Layer；
- unit case `__Private_Motion_GLLayer uses private ClassID only`：增加返回类型静态断言、
  native pointer 与 retained dispatch class-ID resolver 同一性、复用 resize 测试。

`ensurePrivateMotionGLL` 记为 `IMPLEMENTED`。direct SLA coordinator 记为
`EVIDENCED_4_4`：backend static/owner/gate 已实现，但 legacy command builder 尚保留
dispatch-based queue 结构，不能提前宣称源结构闭合。`git diff --check` 和覆盖表 12 列
校验通过；本机缺 CMake/Ninja/Emscripten 且无现成 build/out，正式测试未运行。
