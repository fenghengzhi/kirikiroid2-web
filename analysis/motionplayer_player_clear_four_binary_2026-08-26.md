# Player.clear 递归 target 分派（四参考二进制，2026-08-26）

## 1. endpoint 与证据

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player clear body | `0x6D0160` | `0x595720` | `0x10012139C` | `0x120168` |
| `ncbPropAccessor(Name)` ctor | `0x5CB46C` | `0x4FC618` | `0x100126C84` | `0x1261DC` |

四个 clear body 均已 fresh 全函数 decompile + disassemble；指令数分别为
359 / 306 / 278 / 354。四个 name-accessor constructor 也已 fresh decompile +
disassemble。八个函数均已在对应 IDB 命名、注释、bookmark 并保存。

脚本 `clear` 不是“卸载 motion”或“清 Player 状态”，而是一个 typed 两 Variant
绘制方法；本地结构名 `drawToLayerRecursive_guess` 保留这一真实职责。

## 2. typed NCB 外层

注册使用 typed detail method，native 签名为：

```cpp
void Player::clear(Variant target, Variant fill);
```

生成 adapter 的共同边界：非空 membername 先交还普通 Dispatch；null receiver 在
result/argc 之前返回 `TJS_E_NATIVECLASSCRASH`；receiver 非空后清 result，再执行
`argc >= 2` 下界检查，随后解析 Player 并各自 CopyRef `argv[0]` / `argv[1]`。
多余参数不转换。现有真实 method-object 测试覆盖这一层。

## 3. 共同主体与分派顺序

```cpp
if (motionContent.Type() == Void) return;

target.AsObjectStrict();
if (D3DAdaptor *d3d = tryNative(target.Object)) {
    int color = fill.Type() == Object ? 0 : fill.AsInteger();
    d3d->clearTargetTexture(color);
    return;
}

if (SeparateLayerAdaptor *sla = tryNative(target.Object)) {
    target = sla->targetLayer;                 // CopyRef temp -> assignment
}

LayerNI *layer = TJSNI_Layer::FromVariant(target);
if (layer != nullptr && !layer->GetHasImage()) return;

ncbPropAccessor layerClass(L"Layer");
Rect bound = drawRegion.GetBound();
Variant left(bound.left), top(bound.top);
Variant width(bound.right-bound.left), height(bound.bottom-bound.top);

if (fill.Type() == Object) {
    fill.Object->FuncCall(0, nullptr, nullptr, nullptr,
                          4, {left,top,width,height}, fill.ObjThis);
} else {
    layerClass->FuncCall(0, L"fillRect", sharedHint, nullptr,
                         5, {left,top,width,height,fill}, target.Object);
}

for (live index = 1; index < nodes.size(); ++index) {
    if (nodes[index].type == 3) {
        Player *child = resolveChildPlayer(nodes[index].motionVariant);
        child->clear(Variant(target), Variant(fill)); // unconditional
    }
}
```

顺序边界：

- motion-content Void gate 在 target/fill 转换之前，空 Player 对任意参数都是成功
  no-op；
- target 严格 Object 转换先于 D3D/Separate class-ID 查询；
- D3D 优先级高于 Separate/Layer，Object fill 固定映射为 color 0，且立即返回；
- Separate 只替换本调用的 by-value target owner；后续 fillRect 与 child 都使用替换后
  target；
- FromVariant 返回 null 并不阻止普通 Object target 进入脚本 fill；只有真实 Layer
  且 `hasImage == false` 才提前返回；
- callable/fillRect 的 TJS status 均忽略，异常仍传播；成功后继续 child recursion。

## 4. `ncbPropAccessor("Layer")` 的 owner 与失败边界

四端 clear 都直接构造 ncbind 的 name constructor，而不是“可失败的 Layer helper”：

1. `TVPGetScriptDispatch()` 取得 owning global；
2. global 非空时执行 `PropGet(flags=0, "Layer", hint=null,
   result=&localVariant, objthis=global)`，status 不检查；
3. 对 local Variant 执行严格 `AsObject()`，只给 Object dispatch AddRef；
4. 销毁 local Variant，释放它的 Object/ObjThis owners；
5. accessor 析构时只 Release retained Object。

ncbind 这一历史 constructor 没有 Release 第一步取得的 global owner；四端机器码也
没有对应 Release。这个每次调用的 global-ref leak 是原始生命周期边界，不能用
“修好 leak”的 helper 替换。Layer 缺失、status 失败且未写 Object、或写入非 Object
都会在第 3 步抛转换异常；即使 fill 本身是 callable，accessor 也先构造。

本地原先使用 `getLayerClassDispatchVariant_guess`：它会 Release global、保留完整
Object/ObjThis Variant，并在失败时静默 return。三处都与四端不符；本轮已改回直接
`ncbPropAccessor(TJS_W("Layer"))`，并增加缺失 Layer 必须抛、callable 不得执行的
边界测试。

## 5. draw-region 与调用 ABI

| 端 | DrawRegion base | valid byte | left/top/right/bottom |
|---|---:|---:|---:|
| Android arm64 | `+0x360` | `+0x384` | `+0x374/+0x378/+0x37C/+0x380` |
| Android armv7 | `+0x250` | `+0x26C` | `+0x25C/+0x260/+0x264/+0x268` |
| iOS arm64 | `+0x2F0` | `+0x314` | `+0x304/+0x308/+0x30C/+0x310` |
| iOS armv7 | `+0x210` | `+0x22C` | `+0x21C/+0x220/+0x224/+0x228` |

valid 为 false 时先调用 GetBound 计算/缓存，再按 left、top、width、height 顺序构造
四个 Integer Variant。callable 分支调用 Object closure 本身：member/hint/result 均
null，flags 0，ObjThis 使用 closure 自带的 ObjThis。fillRect 分支则以 Layer class
dispatch 为 method receiver、target Object 为 objthis，使用 process-wide
`fillRect` hint，并为五个 argv 依次建立 owned Variant copy。返回 status 均不读。

## 6. child deque 与 malformed boundary

四端均排除 synthetic root（index 0），只递归 node type 3，并在每轮/递归返回后
观察 live deque end：re-entrant append 可以进入同一遍历。物理容器差异为：

| 端 | deque ABI | MotionNode stride | block 形状 |
|---|---|---:|---|
| Android arm64 | libstdc++ | 2632 | iterator/map arithmetic |
| Android armv7 | libstdc++ | 2272 | iterator/map arithmetic |
| iOS arm64 | libc++ | 2648 | 16 nodes/block |
| iOS armv7 | libc++ | 2228 | 16 nodes/block |

child Variant 非 Object 时严格转换抛；Object 为 null、NativeInstanceSupport 失败、
adaptor/Player 为空时 resolver 得到 null。四端仍无条件以该 null 作为 `this` 递归，
随后在 child motion-content gate 解引用崩溃。原本本地 `if (child)` 静默跳过，本轮
已删除。该 process-crash boundary 不在单元测试中主动触发；非 Object 的可捕获异常
由共享 child resolver 测试覆盖。

## 7. 本地修改与验证状态

修改位于 `PlayerTimeline.cpp:311`：

- 改回严格 name-based `ncbPropAccessor("Layer")` owner/失败形状；
- 删除 wrong-native child 的静默 null guard；
- 删除该 translation unit 已不需要的 `PlayerRenderInternal.h` include。

测试位于 `tests/unit-tests/plugins/motionplayer-dll.cpp:10834` 和 `:23978`：覆盖 D3D
优先级、Separate target、callable closure receiver、no-image gate、真实 type-3 递归、
缺失 Layer 严格异常、typed adapter receiver/result/argc/surplus 边界。

本切片记为 `IMPLEMENTED`；`git diff --check` 通过。正式 CMake/unit/Web build 因
本机缺 CMake/Ninja/Emscripten 未执行。四端完整 EH metadata 中每个 Variant
copy-construction failure 的 landing-pad 编号仍留到最终 ABI exception ledger，
普通 owner 流、脚本异常传播和 malformed native 边界已闭合。

## 8. 2026-08-27 per-call-site EH 闭包

最终异常账本再次 fresh decompile 并完整读取四个主体：Android arm64/armv7、iOS
arm64/armv7 当前函数边界分别为 359/306/274/394 条指令，全部 `cursor.done=true`。
iOS arm64 的独立 LSDA cold cleanup `0x100121800` 另有 56 条，iOS armv7 的 SjLj
switch cleanup `0x120572` 另有 85 条；二者也已完整读取。

四端共同的 source-level cleanup 集合是：

1. Separate target getter/assignment 期间只销毁已经完成构造的 replacement Variant；
2. Layer accessor 建立后，任一 bound/FuncCall/child 访问异常最终都 Release accessor 持有的
   Layer class Object；历史 global owner leak 不在 unwind 中补偿；
3. ordinary fill 分支按 `fill -> height -> width -> top -> left` 逆序销毁；callable 分支没有
   额外 fill copy；
4. child recursion 的两个 by-value Variant 临时量只在各自 CopyRef 成功后进入 cleanup，
   reverse-order 销毁，不回滚先前已经完成的 fill 或先前 child；
5. cleanup destructor 自身抛出时进入 terminate/abort，不用第二个异常替换原异常。

目标形状差异如下：

- Android arm64 把 landing chain 保留在主体 `0x6D062C..0x6D0718`，多个入口按 throwing
  call site 跳入对应 Variant 前缀，再汇合到 Layer accessor release 和
  `_Unwind_Resume`；
- Android armv7 的完整主体没有本帧 landing/unwind cleanup，只有正常路径的逆序析构；
- iOS arm64 由 LSDA 选择 `0x100121800` 内不同入口，cold body 逆序析构最多五个 rect/fill
  Variant、两个 recursion Variant 和 accessor，再 `__Unwind_Resume`；相邻一条 terminate
  thunk处理 cleanup-destructor failure；
- iOS armv7 的 call-site 0..17 进入 `0x120572`。case 5..9 形成 rect/fill Variant 前缀，
  case 14/15 处理 recursion temporaries，case 2..4/11..13 直接进入共同四-Variant/accessor
  tail，case 10/16 terminate，case 17 abort，最后 `__Unwind_SjLj_Resume`。

这些路径严格对应本地按值参数、局部 `tTJSVariant` 与 `ncbPropAccessor` 的 RAII 词法
lifetime；没有发现需要增加 catch、rollback 或手写 cleanup 的源码差异。四个 IDB 已补充
landing/cold/SjLj 命名、注释、书签并保存。因此早期留下的“逐 call-site ABI EH ledger”
现已闭合；正式构建仍是环境限制，不再是 body 证据缺口。
