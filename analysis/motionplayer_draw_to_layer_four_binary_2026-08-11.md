# Motion.Player clear / draw-to-target 四参考二进制共同实现（2026-08-11）

## 结论

脚本成员 `clear` 的底层实现不是普通的“对 Layer 调一次 `fillRect`”。四个参考二进制共同给出一条有严格优先级和提前返回边界的递归分派链：

1. motion Variant 为 void 时立即成功返回；
2. 目标是 `Motion.D3DAdaptor` 时，以本次 fill 参数转换出的整数直接清目标纹理，然后立即返回；
3. 否则若目标是 `Motion.SeparateLayerAdaptor`，先把工作目标替换为 adaptor 内持有的 `targetLayer` Variant；
4. 把工作目标解析成原生 Layer；若 Layer 存在但没有 `MainImage`，整段返回，既不 fill，也不递归 child Player；
5. fill 是对象时，把它当可调用对象并传入四个矩形整数；否则通过全局 `Layer` 类对象调用五参数 `fillRect`；
6. 最后只递归 node index `1..n-1` 中 `nodeType == 3` 的 child Player，每次递归都拥有 target/fill 的 Variant 副本。

旧本地注释把前两次 `NativeInstanceSupport` 调用误读成按数字读取未知属性，并把 D3D 快速路径标成无法静态恢复的缺口。这一解释已被四端的 NCB class-info 初始化槽、相邻注册器和同一批 GetNativeInstance 模板实例共同否定：两个 class ID 分别就是 `D3DAdaptor` 和 `SeparateLayerAdaptor`。

## 四平台地址映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmotePlayer.clear` wrapper | `0x67EE44` | `0x561DA8` | `0x1001B5D04` | `0x1B595C` |
| Player recursive body | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |
| D3D target clear | `0x6AB08C` | `0x57D184` | `0x100104130` | `0x10149C` |
| Layer Variant -> native helper | `0xA7959C` | `0x79AFCE` | `0x10035FF10` | `0x36366C` |
| D3DAdaptor NCB class-ID 槽 | `0x1AB5820` | `0x1111B3C` | `0x101ADF738` | `0x1831838` |
| SeparateLayerAdaptor NCB class-ID 槽 | `0x1AB57F8` | `0x1111B28` | `0x101ADF710` | `0x1831824` |
| Player NCB class-ID 槽 | `0x1AB5848` | `0x1111B50` | `0x101ADF770` | `0x183185C` |

四个 recursive body 均已命名为 `Player_drawToLayerRecursive_guess`，D3D helper 均已定型为带 `int color` 的 `D3DAdaptor_clearTargetTexture_guess`，Layer helper 均已命名为 `TJSNI_Layer_FromVariant_guess`。三组 NCB class-ID 槽也已逐库命名、定型并保存。

2026-08-16 又对四端 Player registrar、stored target、完整 xref 和 two-Variant
typed adapter 做了 fresh 复核：这个 recursive body 本身就是脚本 `clear` 直接保存的
typed member target，不存在第二个 native compatibility wrapper。本地成员身份因此从
`drawToLayerCompat` 修正为未知源码名的 `drawToLayerRecursive_guess`；完整源结构证据见
`analysis/motionplayer_player_clear_direct_typed_entry_four_binary_2026-08-16.md`。

## NCB 身份的完整证据链

64 位 Android 的两个静态 class-info 对象分别占据：

```text
SeparateLayerAdaptor: 0x1AB57E8 .. 0x1AB5808
D3DAdaptor:           0x1AB5810 .. 0x1AB5830
Player:               0x1AB5838 .. 0x1AB5858
```

其中 `+0x10` 的 32 位槽是运行期 native class ID。Android arm64 的 `0x6A92E8` / `0x6AA1E4` 和 Android armv7 的 `0x57C544` / `0x57CBF4` 是对应 GetID 一行函数。iOS 的 `InitFunc_44/45/46` 以同样相邻的 class-info 结构清零三个槽。相邻 create/get-native 模板实例又分别进入 `SeparateLayerAdaptor`、`D3DAdaptor` 和 `Player` 已知构造/析构体。因此 recursive body 传给 dispatch vtable `NativeInstanceSupport(TJS_NIS_GETINSTANCE, classID, &adaptor)` 的并非属性索引或字符串 hint。

64 位 adaptor payload 是：

```text
+0  native-instance vptr
+8  native object pointer
```

32 位对应 `+0/+4`。所以 recursive body 在成功 NIS 后读取 adaptor `+8/+4`，得到真正的 D3D/SLA/Player 指针。SLA native object 内的 `targetLayer` Variant 位于 64 位 `+20`、32 位 `+12`；这正是四端命中 SLA 后复制回工作 target Variant 的源地址。

## 共同伪代码

以下只省略 Variant 析构的异常清理边，保留可观察分支顺序：

```cpp
void Player::drawToTarget(Variant target, Variant fill) {
    if (motion.type == Void)
        return;

    target.requireObject();

    if (D3DAdaptor *d3d = getNative<D3DAdaptor>(target.Object)) {
        int color = fill.type == Object ? 0 : fill.AsInteger();
        d3d->clearTargetTexture(color);
        return;
    }

    if (SeparateLayerAdaptor *sla =
            getNative<SeparateLayerAdaptor>(target.Object)) {
        target = sla->targetLayer;
    }

    LayerNative *layer = Layer_FromVariant(target);
    if (layer != nullptr && layer->MainImage == nullptr)
        return;

    Variant layerClass = getGlobalClass("Layer");
    Rect bound = lazyGetPlayerDrawBound();
    Variant left(bound.left), top(bound.top);
    Variant width(bound.right - bound.left);
    Variant height(bound.bottom - bound.top);

    if (fill.type == Object) {
        fill.Object->FuncCall(
            member = nullptr,
            args = {left, top, width, height},
            objthis = fill.ObjThis);
    } else {
        layerClass.Object->FuncCall(
            member = "fillRect",
            hint = processGlobalFillRectHint,
            args = {left, top, width, height, fill},
            objthis = target.Object);
    }

    for (node in nodes[1..]) {
        if (node.type == 3) {
            Player *child = getNative<Player>(node.childPlayerVariant.Object);
            child->drawToTarget(copy(target), copy(fill));
        }
    }
}
```

## Layer 有图门与错误边界

Layer helper 的 64 位 native `MainImage` 字段位于 `+280`，32 位位于 `+204`。这与核心 `tTJSNI_BaseLayer::GetHasImage()` 的实现 `return MainImage != nullptr` 对齐。四端条件都是：

```text
nativeLayer == null || nativeLayer.MainImage != null
```

只有条件为真才进入 fill 和递归体。因此“有 native Layer、但 MainImage 为 null”不是退回通用脚本调用，而是整段静默跳过。它也阻断 type-3 child Player 递归。

`Layer_FromVariant` 对非 Layer 对象的 `NativeInstanceSupport` 失败走核心 `TVPSpecifyLayer` 异常路径；它不是任意 duck-typed `fillRect` 对象的接受入口。null Object 可得到 null native 指针，但普通非 Layer dispatch 会抛错。目标 Variant 本身不是对象时，更早的对象转换已经抛出 Variant conversion error。

## fill Variant 的精确分派

### D3D 快速路径

对象 fill 不调用对象，固定映射为颜色整数 `0`。其余类型使用 TJS 整数转换：

- void -> `0`；
- string -> TJS 字符串整数转换；
- integer -> 原值；
- real -> 截断为整数；
- octet -> TJS conversion error。

D3D 清理函数只检查 `clearEnabled`。它不读取 adaptor 的 `clearColor` 字段，而是把 recursive body 传入的 `color` 直接写进 `FillARGB.color`。D3D 命中后，无论 `clearEnabled` 是否为 false，都立即返回，不执行 Layer 路径或 child 递归。

### callable fill

fill 类型为 Object 时参数恰好四个，且均为 Integer Variant：`left/top/width/height`。没有第五个 fill 参数，也不调用 target Layer 的 `fillRect`。调用接收者严格使用 fill closure 中保存的 `ObjThis` 指针；原生调用没有把 null ObjThis 自动替换为 Object。

### 非对象 fill

非对象分支不直接对 target dispatch 调方法。它先取得全局 `Layer` 类 closure，然后对类对象调用 `fillRect`，把工作 target 的 **Object 指针** 作为 objthis，并复用一个进程级 TJS member hint。目标 closure 自身若另有 ObjThis，不参与这里的接收者选择。

两个 FuncCall 的 HRESULT 都不改变 native void 返回形状；正常 TJS 异常仍可沿调用栈传播。

## Variant 生命周期和递归

四端 `EmotePlayer.clear` wrapper 都先对 target/fill 建立 owning Variant 副本，调用 Player body 后按 fill、target 的逆序销毁。SLA 命中时只替换本次调用的 target Variant；调用者的原 Variant 不被写回。

每个 type-3 child 递归边又分别复制当前工作 target 和原 fill，递归返回后先销毁 fill copy，再销毁 target copy。这个细节保证 SLA 解包后的 target 会传给 child，而 fill callable/字符串等引用在整个子调用期间独立存活。

## D3DAdaptor 相邻纠错

本轮 fresh decompile 同时暴露出旧 D3D 文档和本地类布局的两处偏差：

- native D3D 对象保存的是一个 AddRef 的 Window dispatch 指针，不是完整 `tTJSVariant`；
- `D3DAdaptor_clearTargetTexture` 的真实源级签名带 `int color`，颜色来自调用参数；`D3DAdaptor_renderFromPlayer` 本身没有调用这个 helper。继续下钻其源纹理/目标纹理两个闭包，也没有发现对 `clearColor` 的读取或隐藏清屏；共享 renderer 的 stencil clear 是另一类 GPU 状态操作。

本地 `D3DAdaptor` 字段已按四端公共布局重排：四个尺寸/中心整数、保留 `+0x10` 整数、五个属性字节、Window 指针、clearColor、target texture、`std::map` 软件纹理缓存。64 位对应 Window `+32`、clearColor `+40`、target `+48`；32 位对应 `+28/+32/+36`。

## 本地恢复与验证

本轮本地改动包括：

- 复原 D3DAdaptor、SeparateLayerAdaptor、普通 Layer 三段优先级；
- 复原 SLA `targetLayer` 替换和 Layer `GetHasImage()` 早退；
- 复原 callable fill 的四参数、raw ObjThis 调用；
- 普通 fill 改为通过全局 Layer 类对象、进程级 hint 和 target Object objthis 调五参数 `fillRect`；
- 每个 type-3 child 递归显式拥有 target/fill 副本；
- D3D clear 改为显式 color 参数，并删除 Player-to-D3D 普通渲染路径中不存在的额外 clear；
- D3D Window 所有权从双指针 Variant 改为单 dispatch AddRef/Release，并按原生字段顺序重排；
- 新增 callable/SLA no-image/D3D-fast-return 回归用例。

验证结果：

- Web Debug Build 通过；
- Wasmtime Headless Debug Build 通过；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Emscripten 编译参数执行 `-fsyntax-only` 通过，只有仓库既有 `_tss` 字面量弃用警告；
- 四个 IDB 的 worker 和 clear helper 定型后重新反编译，均显式显示相同的三组 class ID、`color` 参数和 `mainImage` 条件；
- 四个 IDB 已保存。
