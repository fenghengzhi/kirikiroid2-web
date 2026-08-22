# `__Private_Motion_GLLayer` 生命周期、原生命令 deque 与注册边界（四参考二进制）

日期：2026-08-13

## 1. 范围与结论

本轮只使用 `reference/binaries/` 中四个目标及其 recovery IDB，重新恢复
`__Private_Motion_GLLayer` 的类注册、native allocation、继承关系、构造/析构、脚本
callback、逐帧命令 deque 与 `Draw_GPU` 的所有权边界。旧 `libkrkr2.so` 地址只用于定位
本地过时代码，不作为语义证据。

四端共同的源级结构是：

```cpp
class PrivateMotionGLLayer_guess : public tTJSNI_Layer {
    int stencilCount;                 // 构造时故意不初始化
    std::deque<RenderItem> commands;

    // Construct / Invalidate 均直接继承 tTJSNI_Layer
    // 唯一新增的重要虚函数实现是 Draw_GPU
};
```

关键结论：

1. private native 不覆盖 `Construct`，脚本构造 callback 取得 native 后直接调用
   vtable 首槽，即继承的 `tTJSNI_Layer::Construct`；
2. derived ctor 的顺序是 base ctor、`std::deque` ctor、`Type = ltAlpha(2)`，不调用
   `AllocateImage()`；
3. derived dtor 直接析构 deque，再进入 base dtor；没有先执行一次显式 `clear()`；
4. derived 对象在 deque 前还有一个 `int stencilCount`，builder 每帧覆盖，ctor 不初始化；
5. `Draw_GPU` 只以 `stencilCount >= 1` 判断是否建立 stencil buffer，不能由扫描 deque
   中的两个 ref byte 替代；
6. deque element 在 64 位为 88 bytes、32 位为 72 bytes；尾部 texture 是 owning ref，
   析构先 Release texture，再析构首部 point vector；
7. element ctor 故意不初始化 `meshDivX/meshDivY`，仅 geometry 1/2 的后续分支写入；
8. point vector 增长抛异常时没有本地 `pop_back()` 回滚，已经入 deque 的部分 element
   留在容器中，随后由正常 clear/dtor 回收；
9. native class 只注册同名 constructor、`setSize`、RW `visible`、RW `absolute` 四项；
10. callback 使用 NCB/native-class 宏式的“GETINSTANCE 后直接解引用”，不提供本地
    `TJS_E_NATIVECLASSCRASH` 保护分支。

## 2. 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| UTF-16 class name | `0x14D6AF8` | `0xD862F6` | `0x10195D4BE` | `0x174F822` |
| native-class registrar | `0x6DA664` | `0x59BD98` | `0x10012A73C` | `0x1293D4` |
| CreateNativeInstance | `0x6DA810` | `0x59BEE0` | `0x10012A8AC` | `0x1295CC` |
| derived complete dtor | `0x6DA8AC` | `0x59BF50` | `0x10012A928` | `0x1296BA` |
| derived deleting dtor | `0x6DA8F8` | `0x59BF80` | `0x10012A96C` | `0x1296EC` |
| `Draw_GPU` | `0x6DA94C` | `0x59BFB4` | `0x10012A9B4` | `0x129724` |
| inherited `Construct` | `0x7FC818` | `0x62C0B4` | `0x1000749EC` | `0x71C54` |
| constructor NCM callback | `0x6DB62C` | `0x59C8D0` | `0x10012B538` | `0x12A182` |
| `setSize` callback | `0x6DB6C0` | `0x59C934` | `0x10012B5AC` | `0x12A1C4` |
| `visible` get/set | `0x6DB84C` / `0x6DB8CC` | `0x59C9B4` / `0x59CA0C` | `0x10012B63C` / `0x10012B69C` | `0x12A222` / `0x12A258` |
| `absolute` get/set | `0x6DB9A8` / `0x6DBA2C` | `0x59CA68` / `0x59CAC4` | `0x10012B704` / `0x10012B768` | `0x12A292` / `0x12A2CA` |
| SLA private-target ensure | `0x6D2D28` | `0x5974D0` | `0x100123670` | `0x122884` |
| Player non-accurate SLA draw | `0x6D2A38` | `0x597328` | `0x1001233C8` | `0x12257C` |
| command builder | `0x6DBB18` | `0x59CB20` | `0x10012B7D0` | `0x12A304` |

普通 IDA string search 在四端都没有命中 class name；按 `ida-search-string` 的规则，
UTF-16LE byte search 在上表地址命中，UTF-32LE 无命中。错误文本
`Please specify layerTreeOwnerInterface object` 同样仅以 UTF-16LE 存在：
`0x1506244 / 0xDAF4EC / 0x101956A58 / 0x1748DBC`。这些 xref 最终落到继承的
Layer `Construct`，不是 private derived override。

## 3. native class 注册与静态类对象生命周期

四个 registrar 都先构造一个 `tTJSNativeClass` 派生对象、注册 class ID 并安装
CreateNativeInstance virtual，然后严格按以下顺序注册四项：

1. method `__Private_Motion_GLLayer`：constructor callback；
2. method `setSize`；
3. read/write property `visible`；
4. read/write property `absolute`。

没有 `clear`、queue callback、隐藏 property 或 derived `Invalidate` callback。

class dispatch 由函数内静态 guard 懒初始化。SLA private-target helper 首次使用时调用
registrar，之后复用同一 class object。四端都取得带引用的 global script dispatch，把
SLA 保存的 owner Variant 与 target Variant 地址直接作为两个 constructor 参数传给
class `CreateNew`，然后释放 global；不会先复制两个 Variant，也不会把 native class
自身当作 constructor 的 `objthis`。

`CreateNew` 的返回码没有友好分支。输出 object 随后被包装进 privateTarget Variant，
临时 raw object 引用被释放；创建失败时不会改写为本地自造的
`Cannot create PrivateMotionGLL.` 异常。

## 4. 继承的 Construct/Invalidate

四端 private vtable 的首槽和第二槽分别指向基础 Layer 实现：

| vtable 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Construct | `0x7FC818` | `0x62C0B4` | `0x1000749EC` | `0x71C54` |
| Invalidate | `0x7FCBE8` | `0x62C2F0` | `0x100074C80` | `0x71F48` |

constructor NCM callback 共同伪代码：

```cpp
PrivateNative *native = nullptr;
objthis->NativeInstanceSupport(GETINSTANCE, PrivateClassID, &native);
return native->Construct(numparams, param, objthis); // virtual slot 0
```

基础 Construct 的共同数据流：

1. `numparams < 2` 返回 `TJS_E_BADPARAMCOUNT`；
2. 保存 `Owner = objthis`，不 AddRef；
3. 从参数 0 取得 object closure；Object 为空抛
   `Please specify layerTreeOwnerInterface object`；
4. 以 closure 的有效 ObjThis 调 `PropGet("layerTreeOwnerInterface")`，失败抛
   `Cannot Retrive Layer Tree Owner Interface.`；
5. 参数 1 有 Layer object 时，用标准 Layer ClassID 做 GETINSTANCE；失败抛标准
   specify-Layer 错误；
6. 有 parent 时复用 parent manager 并 Join；无 parent 时创建 primary manager；
7. 保存参数 0 的 ActionOwner closure。

private class 没有另一套 owner/parent attach 算法，也没有先在 derived callback 里解析
owner/target 再调用 `ConstructResolvedTreeOwner` 的源结构。

## 5. derived object 精确布局

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| allocation size | `0x388` / 904 | `0x280` / 640 | `0x368` / 872 | `0x268` / 616 |
| base subobject size | 816 | 592 | 816 | 588 |
| `stencilCount` | `+816` | `+592` | `+816` | `+588` |
| alignment gap | `+820..823` | none | `+820..823` | none |
| command deque | `+824`, 80 bytes | `+596`, 44 bytes | `+824`, 48 bytes | `+592`, 24 bytes |
| deque ABI | libstdc++ | libstdc++ | libc++ | libc++ |

Android 64/32 的 deque 是旧 libstdc++ map+iterator 结构，iOS 64/32 是 libc++ 的
block-map+start+size 结构。因此相同源级 `std::deque<RenderItem>` 的对象大小分别为
80/44/48/24 bytes，不能用一套二进制 struct 强行解释四端。

CreateNativeInstance 的共同顺序：

```cpp
storage = operator new(totalSize);
tTJSNI_Layer::tTJSNI_Layer(storage);
install derived vtables;
construct deque at platformOffset;
storage->Type = 2; // ltAlpha; base 已是 2，但 derived body 仍重复写一次
return storage;
```

`stencilCount` 地址在 base 尾和 deque 之间，四端 ctor 都没有写它。这符合 derived
member 为未带 initializer 的 fundamental `int`：base 和 deque 正常构造，ctor body
只执行 `Type = ltAlpha`。本地实现不能为安全起见给它 `= 0`；首次 command build 前
直接 Draw 的 native 行为就是读取未初始化值。

base ctor 已分配默认 Layer image。derived ctor 没有额外调用 `AllocateImage()`；本地旧
调用虽然通常不会重新分配 MainImage，仍会额外 ResetClip、设置 FontChanged 与
ImageModified，属于可观察状态差异。

## 6. derived destructor 与 deque 清理

四端 complete dtor 都是：

```cpp
install derived vtables;
commands.~deque();
tTJSNI_Layer::~tTJSNI_Layer();
```

deleting dtor 在 complete dtor 后再 `operator delete(this)`。不存在 derived dtor body
显式调用 `commands.clear()` 的阶段。

相关 deque destruction/clear helper：

- Android arm64：dtor 从 `loc_6DB134` 进入 deque destroy，range element destroy
  为 `0x6DB260`；builder 的 clear 在 `0x6DBB18` 内联；
- Android armv7：deque dtor `0x59C618`，range destroy `0x59C6C8`；builder clear
  `0x59D3F4`；
- iOS arm64：deque dtor `0x10012B1C0`，element clear `0x10012B208`；builder也调用
  `0x10012B208`；
- iOS armv7：deque dtor `0x129F48`，element clear `0x129F70`；builder也调用
  `0x129F70`。

普通每帧 clear 销毁所有当前 element，并按各 STL ABI 保留/重置 deque 的最小存储；
最终 deque dtor 继续释放 block map。显式先 clear、再让自动 deque dtor 执行的本地
两阶段路径不是 native derived dtor 的源结构。

## 7. RenderItem 布局、构造和所有权

共同源级字段顺序：

```cpp
std::vector<MeshPoint> points;
uint32_t opacity;
uint8_t stencilMaskRef;
uint8_t stencilWriteRef;
int32_t blendMode;
int32_t geometryType;
int32_t meshDivX;        // element ctor 故意不写
int32_t meshDivY;        // element ctor 故意不写
uint32_t packedColors[4];
int32_t sourceRect[4];
iTVPTexture2D *sourceTexture; // owning ref
```

| 字段 | 64 位 offset | 32 位 offset |
|---|---:|---:|
| vector | `+0..23` | `+0..11` |
| opacity | `+24` | `+12` |
| stencil bytes | `+28/+29` | `+16/+17` |
| blend / geometry | `+32/+36` | `+20/+24` |
| div X/Y | `+40/+44` | `+28/+32` |
| colors | `+48..63` | `+36..51` |
| source rect | `+64..79` | `+52..67` |
| source texture | `+80` | `+68` |
| total | 88 | 72 |

独立/半独立 element construction helper：

- Android arm64：在 builder 中内联；
- Android armv7：deque append `0x59D43C`，element ctor `0x59D568`；
- iOS arm64：deque append `0x10012C03C`，element ctor `0x10012CB94`；
- iOS armv7：deque append `0x12ABFC`，element ctor `0x12B518`。

element ctor：default-construct 空 vector，写 opacity/stencil/blend/geometry/colors/rect，
先把 texture 槽写 null；source texture 非空时直接增加其 intrusive refcount，再保存指针。
它不写 div X/Y。随后：

- geometry 0：依次 `push_back` 三个 affine point；div X/Y 保持未初始化；
- geometry 1：把 prepared Bezier point vector 与 element vector 三指针 swap，再根据
  patch/source 尺寸写 div X/Y；
- geometry 2：swap composite mesh vector，再复制 prepared div X/Y；
- 其它 geometry：不填 points，也不写 div X/Y。

四端都在 element append 成功后才执行 point vector push/swap，且 builder 没有捕获
vector allocation 异常并 `pop_back()`。因此异常传播时 deque 已经多出一个持有 texture
的 element。这是对象生命周期边界，不能用“更安全”的回滚改写。

element 析构严格按逆声明顺序观察到：

1. sourceTexture 非空则 Release（64 位 vtable `+16`，32 位 `+8`）；
2. vector buffer 非空则释放。

Android libstdc++ block 分别容纳 5 个 88-byte element（440-byte block）和 7 个
72-byte element（504-byte block）；iOS libc++ block step 分别为 4048（46×88）和
4032（56×72）。这些是 STL ABI 差异，不是不同源字段。

## 8. 每帧 command builder 数据流

四端 command builder 的共同高层顺序：

```cpp
int stencilCount = 0;
if(!player.priorDraw) {
    zero every prepared item's two stencil bytes;
    assign stencil chains;                 // count 即使 ref 后来归零仍递增
    materialize clip / command state;
}

commands.clear();

for(item in mainPreparedList) {
    if((blendMode & 0xf) == 6) continue;
    if(item.flag17 || item.flag16) continue;
    if(item.opacity == 0) continue;
    if(player.priorDraw && !item.flag18) continue;
    if(item.sourceState->flag1) continue;

    opacity = player.priorDraw ? trunc_toward_zero(item.opacity / 2)
                               : item.opacity;
    texture = resolve source Layer main-image render texture;
    commands.emplace_back(opacity, texture, item);
    transfer/fill points and mesh divisions by geometry type;
}
return stencilCount;
```

caller 把返回值写到 private native 的 derived `stencilCount` 成员：A64/I64 `+816`，
A32 `+592`，I32 `+588`。写入发生在 builder 返回后、Layer Update 前。

`priorDraw` 为真时 stencil/clip preparation 整段被跳过，count 固定返回 0；但 queue
仍被 clear 并按 flag18 gate 重建，opacity 使用向零截断的半值。

overflow 边界四端一致：candidate 让 count 从 255 增到 256 时只记录一次
`MMotionPlayer: StencilCount overflow(256)`，ref byte 自然截断；count 继续按 int 递增。
某 candidate 即使最终没有 drawable mask target、其 write ref 被归零，count 仍不会
回退。这就是不能通过“queue 是否存在非零 ref”推导 stencil 启用状态的原因。

每帧 clear 的时序在 stencil assignment 与 clip/command materialization 之后。如果上述
前处理抛异常，上一帧 deque 保持不变；在进入 clear 后才切断上一帧 texture owner。

## 9. Draw_GPU 与 stencilCount

四端 `Draw_GPU` 在建立 target bitmap/texture 后读取 derived count：

- A64/I64：`this + 816`；
- A32：`this + 592`；
- I32：`this + 588`。

只有 `count >= 1` 才执行 render target stencil attachment、clear stencil buffer 与 GL
初始状态；循环结束时也只在相同条件下 EndStencil。它不预扫描 deque。

deque 遍历的 element ABI 与第 7 节一致。sourceTexture 为 null 会 `break` 整个循环；
未知 geometry type 只跳过 geometry operate，继续后续 element。blend low nibble、packed
color、opacity、stencil 两 byte 与三种 geometry 分支四端相同。

## 10. callback 边界

四端六个 callback 共同使用：

```cpp
PrivateNative *native = nullptr;
objthis->NativeInstanceSupport(GETINSTANCE, PrivateClassID, &native);
// ignore tjs_error; direct use native
```

`setSize` 唯一显式参数检查是 `numparams < 2 -> TJS_E_BADPARAMCOUNT`，之后把前两个
Variant 转整数并调用 Layer SetSize。`visible` setter 使用 Variant bool conversion；
`absolute` setter 使用 integer conversion。getter 把当前值写给 result helper；helper
自身处理 result 为空的 NCM 调用约定。

因此本地 callback 不能把 GETINSTANCE 失败改写成 `TJS_E_NATIVECLASSCRASH`，也不能在
classID 尚未注册、objthis 为空时静默返回 null。对外的诊断 resolver 可以保留安全
接口，但脚本 callback 必须维持 native 的直接解引用边界。

## 11. 本地实现逐项差异与恢复决策

| 本地旧实现 | 四端证据 | 恢复决策 |
|---|---|---|
| derived ctor 调 `AllocateImage()` | base ctor 已有默认 image；derived 只在 deque ctor 后写 `Type=2` | 删除 AllocateImage，保留直接 `Type=ltAlpha` |
| derived 覆盖 Construct 并调用自造 resolved-owner helper | vtable 直接指向基础 Layer Construct | 删除 override 与只为它存在的 helper |
| derived dtor 显式 clear | complete dtor 直接 deque dtor | default derived dtor |
| 类只有 deque | count 位于 deque 前，且 ctor 不初始化 | 加未初始化 `int` 成员 |
| Draw 扫描 deque 判断 stencil | Draw 只读 count | 使用 builder 写入的 count |
| callback GETINSTANCE 失败返回 native-class-crash | 四端忽略返回码直接解引用 | callback 使用 unchecked helper |
| CreateNew 复制 owner/target，objthis=class | 四端参数指针直指 SLA Variant，objthis=global dispatch | 改为 direct args/global，释放 global |
| 创建失败抛自造文本 | 四端无此分支 | 删除友好失败分支 |
| privateTarget 非 object 时 Clear 后重建 | 四端只有 Void 才创建；其它类型走严格 object conversion | Void-only create，不清理错误类型 |
| native 解析失败 Invalidate 后返回 null | 四端继续直接使用解析结果 | 删除恢复性 Invalidate 分支 |
| RenderItem 所有标量默认 0 | element ctor 不写 mesh div | div 成员无 initializer，geometry 0/未知不写 |
| point build catch 后 pop_back | 四端无 rollback | 删除 catch/pop |
| queue 在前处理前 clear | 四端 stencil/clip 前处理完成后 clear | 调整顺序 |
| 由 queue 扫描推导 stencil | candidate count 可能非零而所有 refs 为零 | 单独保存 builder 返回 count |
| 大量 `0x6D...` 旧地址命名/注释 | 四端当前映射见第 2 节 | compiled source 改语义 `_guess` 名；绝对地址只留本分析文档 |

## 12. 证据置信度与未决项

高置信度且四端完全交叉一致：注册项、继承的 Construct/Invalidate、allocation size、
count/deque offset、deque ABI、element size/字段、texture owner、构造/析构顺序、callback
错误边界、builder clear/append 顺序、Draw 的 count gate。

未知原始 C++ identifier 统一使用 `_guess`。`PrivateMotionGLLayer`、`RenderItem`、
`stencilCount` 是语义恢复名，不宣称为原符号名。

本轮不把 Android libstdc++ 或 iOS libc++ 的内部 node/map 类型写进跨平台 C++ 源码；
保持标准 `std::deque`/`std::vector` 才是四端共同源结构。GPU method 选择、Bezier
tessellation 与三角形提交的更深数值细节另有纵切，本文件只记录与对象生命周期和
command owner 直接相关的部分。

## 13. 落地与验证

本轮按上述四端共同伪代码同步修正：

- `cpp/core/visual/LayerIntf.cpp/.h`：删除只服务于旧 Android 地址推断的
  `ConstructResolvedTreeOwner...` 辅助入口，并删除 public Layer `Construct` 对
  closure `Object` 的二次 GETINSTANCE fallback；
- `cpp/plugins/motionplayer/PrivateMotionGLL.cpp/.h`：恢复继承式 Construct/Invalidate、
  count-before-deque、base ctor 后只设 `ltAlpha`、默认 derived 析构、strict factory、
  direct callback boundary、RenderItem 未初始化 div 与无 rollback append；
- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp`：恢复前处理→clear→append→写 count
  顺序以及 `priorDraw` 跳过前处理的分支；
- unit/wasmtime 辅助调用点迁移到语义 `_guess` 名。unit Layer fixture 现在也调用正式
  `tTJSNI_BaseLayer::Construct`，不再从测试中依赖已证伪的旁路构造器。

验证结果：

1. `cmake --build out/web/debug -j 8` 成功；
2. 用同一 `compile_commands.json` Emscripten 参数对
   `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only` 成功，仅有仓库
   既存的 `_tss` literal-operator 弃用 warning；
3. `git diff --check` 成功；
4. compiled source/test 中已无本轮删除的旧
   `ConstructResolvedTreeOwnerLike`、`ensurePrivateMotionGLLLike`、
   `resolvePrivateMotionGLLNativeLike` 与旧 queue helper 名；
5. 四份 recovery IDB 均写入 registrar、native allocation/destruction、Draw、六个 NCM
   callback、command builder、deque helper 与 RenderItem ctor 的语义 `_guess` 名和布局/
   边界注释，并成功原路径保存。
