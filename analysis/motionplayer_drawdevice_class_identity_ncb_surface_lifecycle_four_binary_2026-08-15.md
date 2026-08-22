# Motionplayer DrawDevice 七类身份、NCB surface 与 D3DPicture 生命周期（四参考）

日期：2026-08-15

## 1. 结论

本纵切面重新审计 `reference/binaries/` 中 Android arm64、Android armv7、iOS
arm64、iOS armv7 四个当前参考目标，纠正了本地 `DrawDeviceD3D.cpp` 中持续沿用的
类名位移。四端共同证明 `DrawDeviceD3D.dll` translation unit 不是六类，而是以下七类：

```text
DrawDeviceD3D
  -> D3D
  -> D3DLayer
  -> D3DImage
  -> D3DPicture
  -> D3DEmoteModule
  -> D3DEmotePlayer
```

其中：

- `DrawDeviceD3D` 与独立的 `D3D` 都是 compositor root，拥有完全相同且顺序一致的
  34 项 Factory/member table；
- `D3DLayer` 才是 visible、front/back index、draw plane、matrix、clip 与 listener fan-out
  对象；
- `D3DImage` 是 root-owned set 中的 texture holder，只暴露 width、height 与 load；
- `D3DPicture` 是此前本地完全缺失的 triangle listener，构造参数严格为
  `(D3DLayer *, D3DImage *)`；
- `D3DEmotePlayer` 的 Factory 与 clone 第一个 typed native 参数也是 `D3DLayer *`，不是
  旧源码和旧 IDB 所写的 `D3DImage *`；
- root 的 `primaryWidth`/`primaryHeight` 是 RW property。两端 setter 都只是直接字段写入，
  不触发 resize、window notification、texture release 或 dirty flag；
- root 的 `ManagedObjects` 是 `std::set<D3DImage *>` 裸指针索引。root 析构不遍历、删除或
  detach 其中对象，因而 `D3DImage` 先于 root 析构是原始实现的生命周期前置条件；
- `D3DPicture::scale` 可写可读，但四端 Draw 都不读取它。这是需要保留的 stored-but-unused
  边界，而不是应当“补齐”的缩放功能。

本文的绝对地址只作为 recovery 证据。可编译源码新增注释不写绝对地址；无法从 stripped
产物确定的原始源码名继续使用 `_guess`。

## 2. 七类 registrar 与 static-init 证据

### 2.1 static-init bundle

| 目标 | static init |
|---|---:|
| Android arm64 | `0x42CBD8` |
| Android armv7 | `0x2FF094` |
| iOS arm64 | `0x10024CB00` |
| iOS armv7 | `0x24E6D8` |

四个函数都按源码顺序构造七个 class auto-register object。`ncbAutoRegister`
constructor 采用 head insertion，因此 `LoadModule` 的 ClassRegist 正向 list 顺序严格反转：

```text
D3DEmotePlayer
  -> D3DEmoteModule
  -> D3DPicture
  -> D3DImage
  -> D3DLayer
  -> D3D
  -> DrawDeviceD3D
```

### 2.2 registrar 地址

| class | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| `DrawDeviceD3D` | `0x52A618` | `0x492790` | `0x10023070C` | `0x22F622` |
| `D3D` | `0x52BC18` | `0x492F10` | `0x100230FF0` | `0x22FDFA` |
| `D3DLayer` | `0x52CE8C` | `0x49345C` | `0x100231618` | `0x230408` |
| `D3DImage` | `0x52D768` | `0x493950` | `0x100231AFC` | `0x230932` |
| `D3DPicture` | `0x52DCE0` | `0x493BBC` | `0x100231DD0` | `0x230B86` |
| `D3DEmoteModule` | `0x52E388` | `0x493E54` | `0x100232078` | `0x230DB0` |
| `D3DEmotePlayer` | `0x52E8E4` | `0x494078` | `0x100232278` | `0x230F46` |

独立 `D3D` 曾被漏掉，是因为短 UTF-16 class name 可落入后缀池或相邻 string item，普通
IDA string enumeration 并不可靠。四端补做宽字符串字节与 static-init 指令 xref 后得到：

| 目标 | `D3D` string/取址 | static-init 取址点 |
|---|---:|---:|
| A64 | `0x15531B8` | `0x42CC08`, `0x42CC34` |
| A32 | `0x2FF1FC` | `0x2FF0F8` |
| I64 | `0x10196F960` | `0x10024CB90` |
| I32 | `0x1761D0C` | `0x24E784`, `0x24E78A`, `0x24E7A6` |

这也解释了为什么此前“六个可见完整字符串 = 六个 class”的注释不能继续使用。

## 3. 本地旧类名位移

本纵切面开始时，本地源码形状与四端真实身份的对应关系为：

| 本地旧名 | 四端真实类 |
|---|---|
| `DrawDeviceD3D` | `DrawDeviceD3D` |
| `D3DLayer`（common root） | `D3D` |
| `D3DImage`（listener/matrix） | `D3DLayer` |
| `D3DPicture`（texture holder） | `D3DImage` |
| 不存在 | `D3DPicture` |

因此只改 D3DEmotePlayer 参数名并不能闭合类型链；必须同时完成 root、listener、texture
holder 的三段重命名，并补上真正的 D3DPicture。

## 4. 两个 common root 的精确 NCB table

`DrawDeviceD3D` 与 `D3D` 的 registrar 在四端都包含同一个 34-entry 序列：

| # | member | mode |
|---:|---|---|
| 1 | Factory | two-argument factory |
| 2 | `children` | RO property |
| 3 | `clearColor` | RW property |
| 4 | `transState` | RW property |
| 5 | `add` | method |
| 6 | `remove` | method |
| 7 | `startTransition` | method |
| 8 | `stopTransition` | method |
| 9 | `update` | method |
| 10 | `checkEnable` | method |
| 11 | `getModule` | method |
| 12 | `capture` | method |
| 13 | `offsetX` | RW property |
| 14 | `offsetY` | RW property |
| 15 | `setOffset` | method |
| 16 | `stretchType` | RW property |
| 17 | `bicubicParam` | RW property |
| 18 | `forceRenderTexture` | RW property |
| 19 | `interface` | RO property |
| 20 | `setPrimarySize` | method |
| 21 | `primaryWidth` | RW property |
| 22 | `primaryHeight` | RW property |
| 23 | `setScreenRect` | method |
| 24 | `screenLeft` | RW property |
| 25 | `screenTop` | RW property |
| 26 | `screenWidth` | RW property |
| 27 | `screenHeight` | RW property |
| 28 | `primaryLayers` | RO property |
| 29 | `layerManagerIndex` | RW property |
| 30 | `getPrimaryLayerBitmap` | method |
| 31 | `destLeft` | RO property |
| 32 | `destTop` | RO property |
| 33 | `destWidth` | RO property |
| 34 | `destHeight` | RO property |

旧 macro 有三类偏差：`interface` 被放在最前；`update/checkEnable/getModule/capture` 被放到
末尾；`getPrimaryLayerBitmap` 被放在四个 destination property 之后。另外 primary 两个维度
被错误注册成 RO。

### 4.1 primary direct setter

| callback | A64 | A32 | I64 | I32 | 字段偏移 |
|---|---:|---:|---:|---:|---:|
| width get | `0x52BA78` | `0x492DF4` | `0x100230F18` | `0x22FD68` | 64-bit `+480/+384`; 32-bit `+292/+244` |
| width set | `0x52BA80` | `0x492DFA` | `0x100230F20` | `0x22FD6E` | 同上 |
| height get | `0x52BA88` | `0x492E00` | `0x100230F28` | `0x22FD74` | width field 后 4 bytes |
| height set | `0x52BA90` | `0x492E06` | `0x100230F30` | `0x22FD7A` | 同上 |

四端 setter 都是一条直接 store 语义。它与 `setPrimarySize(width,height)` 的通知行为不同；
本地不能为了复用而让单属性 setter 调用组合方法。

## 5. D3DLayer、D3DImage 与 D3DPicture surface

### 5.1 D3DLayer

精确顺序：

```text
Factory
DrawPlaneFront = 1
DrawPlaneBack = 2
DrawPlaneBoth = 3
visible RW
frontIndex RW
backIndex RW
drawPlane RW
setMatrix
setMatrixGL
setClip
```

它继承内部 `D3DLayerObject` 语义，持有 listener list；matrix 变更和 update 会通过 listener
虚表进行 fan-out。D3DEmotePlayer 与 D3DPicture 都继承同一个 listener base，并在 base
constructor/destructor 中向 D3DLayer 注册/注销自己。

### 5.2 D3DImage

精确顺序：

```text
Factory
width RO
height RO
load
```

native 对象持有 root 裸指针和一个 texture reference holder。constructor 插入 root 的
`std::set<D3DImage *>`，destructor 先释放当前 holder，再从 set erase。

四端 repeated `load` 都直接覆盖 holder pointer，不先销毁旧 holder；因此旧 holder 及其
texture reference 会泄漏。移植源码保留该边界，不把它改成 `unique_ptr::reset`。

### 5.3 D3DPicture

精确顺序：

```text
Factory(D3DLayer, D3DImage)
opacity RW
blendMode RW
stretchType RW
bicubicParam RW
assignImageRange
clearImageRange
setCoord
setScale
getScale
```

property callbacks：

| callback group | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| opacity/blend/stretch/bicubic | `0x52E1FC..0x52E238` | `0x493D4A..0x493D70` | `0x100231F90..0x100231FCC` | `0x230D06..0x230D2C` |
| assign range | `0x52E244` | `0x493D80` | `0x100231FD8` | `0x230D3A` |
| clear ranges | `0x52E2C4` | `0x493DD0` | `0x100232034` | `0x230D84` |
| set coord | `0x52E2D0` | `0x493DD6` | `0x10023205C` | `0x230DA2` |
| set/get scale | `0x52E2D8/0x52E2E0` | `0x493DDC/0x493DE0` | `0x100232068/0x100232070` | `0x230DA8/0x230DAC` |

`assignImageRange` 依序 append 四个 `int32`。`clearImageRange` 只把 vector end 重置为 begin，
保留 capacity/allocation；后续 append 可复用该缓冲。V205 进一步确认 Android 两端是
libstdc++ growth（`size + max(size,1)`），iOS 两端是 libc++ growth
（`max(size+1, 2*capacity)`）；在合法的满容量调用点两者都得到 `1,2,4,8,...`。32-bit
`max_size` 是 `0x0FFFFFFF` 个 16-byte tuple，64-bit 是 `2^60-1`。旧 A64 recovery IDB
曾把 `0x53390C` 的 vector grow 入口错误并入前一 D3DImage set-erase helper，现已拆成独立函数。

## 6. D3DEmotePlayer typed owner 纠正

旧 recovery 名 `D3DImage_NCB_unboxArg_guess` 在四端都读取 D3DLayer registrar wrapper 所初始化
的 class-ID state：

| 目标 | unboxer | D3DLayer state/wrapper 关系 |
|---|---:|---|
| A64 | `0x542CB8` | 读取 qword array index 48；state init `0x42CA88`、getter `0x52CDFC`、registrar `0x52CE8C` |
| A32 | `0x49EE98` | 读取 `dword_110E12C[48]`；同一 state helper 紧邻 registrar `0x49345C` |
| I64 | `0x10023F8C0` | 读取 `dword_101AEE450`；wrapper `0x10023F0A8` 由 D3DLayer auto-reg vtable 引用并调用 `0x100231618` |
| I32 | `0x23F19E` | 读取 `dword_1838E68`；wrapper `0x23E83C` 由 D3DLayer auto-reg vtable 引用并调用 `0x230408` |

因此本地已把 `D3DEmotePlayer` constructor、Factory、clone、listener owner getter、matrix
scale helper、draw origin transform 和 parent module lookup 全部改为 `D3DLayer *`。

## 7. D3DPicture Factory、布局与生命周期

### 7.1 Factory boundary

| 目标 | descriptor FuncCall | native allocate | ctor |
|---|---:|---:|---:|
| A64 | `0x53F068` | `0x53F258` | allocation 内联 |
| A32 | `0x4A0F84` | `0x4A10D0` | `0x4A11B0` |
| I64 | `0x1002421D8` | `0x100242374` | `0x1002424BC` |
| I32 | `0x242034` | `0x2421E4` | `0x242340` |

四端共同边界：

- named member invocation 精确返回 `TJS_E_MEMBERNOTFOUND`；
- 恰好一个 Void 在清 result 之前返回成功，留下 D3DPicture empty adaptor shell；
- 其它路径先清 result，再以参数少于 2 返回 `TJS_E_BADPARAMCOUNT`；
- 第一个参数通过 D3DLayer class ID 严格 unbox，第二个参数通过 D3DImage class ID 严格
  unbox，surplus 不读取；
- 先分配/构造 native，再通过 D3DPicture class ID 查询 receiver adaptor 并 raw attach；
- receiver/adaptor 失败会删除 fresh picture 并返回 `TJS_E_NATIVECLASSCRASH`；
- populated receiver re-entry 直接覆盖 payload，不删除旧 picture，因而泄漏旧 range allocation
  和仍留在旧 D3DLayer list 中的 listener node；
- 三端在 strict conversion 抛异常时释放尚未构造的 native storage，Android armv7 没有对应
  new-expression cleanup，会泄漏 `0x40` raw storage。

完整 ClassInfo/adaptor/typed-factory/vector 闭环见
`motionplayer_d3dpicture_classinfo_typed_factory_listener_ranges_adaptor_owner_four_binary_2026-08-17.md`。

### 7.2 精确字段布局

| 字段 | 64-bit | 32-bit | 初值/语义 |
|---|---:|---:|---|
| vptr | `+0` | `+0` | listener/picture vtable |
| D3DLayer owner | `+8` | `+4` | borrowed；base dtor 注销 |
| stretchType | `+16` | `+8` | `8` |
| bicubicParam | `+20` | `+12` | `-0.5f`；script 边界为 Real |
| D3DImage | `+24` | `+16` | borrowed texture holder |
| duplicate transform D3DLayer | `+32` | `+20` | Draw 读取 matrix/transform |
| opacity | `+40` | `+24` | `255` |
| blendMode | `+44` | `+28` | `2` |
| vector begin/end/capacity | `+48/+56/+64` | `+32/+36/+40` | `vector<Rect4i>` |
| tail state/padding guess | `+72` | `+44` | constructor 清零；精确原名未知 |
| coord x/y | `+76/+80` | `+48/+52` | `0,0` |
| scale | `+84` | `+56` | `1.0f`；Draw 不读 |
| scale tail guess | `+88` | `+60` | constructor 清零 |

allocation size 为 64-bit `0x60`、32-bit `0x40`。recovery IDB 已写入
`D3DPicture64_guess`（96 bytes）和 `D3DPicture32_guess`（64 bytes）类型。

### 7.3 destructor

| 目标 | dtor | deleting dtor |
|---|---:|---:|
| A64 | `0x53F560` | `0x53F5C4` |
| A32 | `0x4A121C` | `0x4A1244` |
| I64 | `0x100242558` | `0x10024258C` |
| I32 | `0x2423AA` | `0x2423D2` |

destructor 先释放 image-range vector allocation，再进入 listener base destructor，从
D3DLayer listener list 删除自身。deleting variant 随后释放 object allocation。

## 8. D3DPicture Draw 数据流

| 目标 | Draw | vertex helper |
|---|---:|---:|
| A64 | `0x53F62C` | `0x53F95C` |
| A32 | `0x4A1270` | `0x4A14E4` |
| I64 | `0x1002425C4` | `0x10024295C` |
| I32 | `0x242400` | `0x242798` |

Android armv7 的 helper 旧 IDB function start 为 `0x4A14D4`，前 16 bytes 实际是被误解码
的数据前缀；真实 Thumb prologue 在 `0x4A14E4`。recovery IDB 已修正 function boundary。

四端 Draw 等价数据流：

1. 取得 DrawDevice 私有 `opengl` render manager；
2. 调 `GetRenderMethod(opacity, true, blendMode)`；返回 null 则立即退出；
3. 创建 source/destination 两个 `vector<tTVPPointD>`，各 reserve
   `imageRangeCount * 6`；
4. 从 duplicate D3DLayer pointer 复制 4×4 matrix；
5. 读取 D3DPicture coord，调用 D3DLayer virtual `TransformPoint`；
6. 用变换后的 coord 覆盖 matrix translation；
7. 对每个 `{left,top,right,bottom}` 依次发出六点：
   `(L,T), (R,T), (L,B), (R,T), (L,B), (R,B)`；
8. source vector 保存未变换坐标；destination vector 保存 matrix 变换后的坐标；
9. 从 D3DImage 当前 holder 取 texture；
10. clip 固定为 `{0,0,target.width,target.height}`；
11. 调 `OperateTriangles(method, rangeCount*2, target, target, clip,
    destinationPoints, oneTexture/sourcePoints)`；
12. 析构两个临时 vector 与 matrix temporary。

vertex helper 先构造 `(x,y,0,1)`，通过 matrix 变换，再分别 append raw xy 和 transformed
xy。D3DPicture 的 `scale` 字段在整个路径中没有任何 read xref。

## 9. root raw-container 析构边界

root destructor：

| 目标 | 地址 |
|---|---:|
| A64 | primary body `0x53244C`；concrete complete `0x531410` |
| A32 | primary body `0x49606C`；concrete complete `0x495744` |
| I64 | primary body `0x100233E1C`；concrete complete `0x100233F54` |
| I32 | primary body `0x232B14`；concrete complete `0x232C74` |

完整 concrete 析构先销毁非零偏移处的 `tTVPDrawDevice` 次基类，再进入表中的 primary
body；后者执行 target/rule texture release、module map 中 value 的虚析构以及四个 STL
container 自身析构。`ManagedObjects` set 析构只释放红黑树 node；它没有对 `D3DImage *`
value 调用 destructor，也没有遍历写 null owner。这里的 DrawDevice 是真实次基类，不是
旧移植所写的成员式 adapter。

这与 D3DImage destructor 的 `Owner->ManagedObjects.erase(this)` 共同形成严格生命周期边界：
如果 root 先销毁，之后 D3DImage destructor 会访问失效 owner。当前参考没有 defensive
detach；本地也不新增。

## 10. 本地修复与校验

源码修复：

- 新增独立 common-root `D3D`；
- 把旧 listener/matrix `D3DImage` 纠正为 `D3DLayer`；
- 把旧 texture-holder `D3DPicture` 纠正为 `D3DImage`；
- 新增真实 `D3DPicture` listener、字段、Factory、surface、range vector 与 triangle Draw；
- `D3DEmotePlayer` 全链改用 `D3DLayer *`；
- common macro 改为四端精确注册顺序；
- primary width/height 改为无副作用 direct RW；
- `ManagedObjects` 类型改为 `std::set<D3DImage *>`，保留 root 不 detach 的边界；
- module dependency 文档与 plan 中旧“六类”全部纠正为七类。

测试新增/修正：

- 断言 global 上七个 class 全部存在；
- 断言独立 `D3D` 具有 common surface 且不错误持有 DrawPlane constants；
- 通过脚本 NCB Factory 创建 D3D root、D3DLayer、D3DImage、D3DPicture；
- 覆盖 primary width/height RW；
- 覆盖 D3DLayer constants/defaults；
- 覆盖 D3DImage 空 holder 的 width/height = 0；
- 覆盖 D3DPicture 两参数 gate、默认 opacity/blend/stretch/bicubic、scale roundtrip、
  assign/clear range；
- 把原 D3DEmotePlayer typed-factory/clone 测试从 D3DImage owner 改为 D3DLayer owner。

机器校验：

- `git diff --check` 通过；
- 完整 motionplayer unit-test TU Emscripten syntax check 通过，仅仓库既有 `_tss` warning；
- `Web Debug Build` 成功重编 `DrawDeviceD3D.cpp`、motionplayer library 并链接
  `index.html`；仅仓库既有 Emscripten pthread/memory-growth、JSPI 和 JS library warning；
- 四份 recovery IDB 已写入新名称、注释、bookmark、D3DPicture 结构类型并保存到
  `out/ida-recovery/`。

## 11. 保守边界

- `ImageRangeTail_guess`、`ScaleTail_guess` 的字节位置与初始化已由四端闭合，但 stripped
  产物不足以证明原始成员名或它们是否为命名字段、内嵌类型尾部还是显式 padding；
- D3DPicture Draw 的 `scale` 未使用是当前四参考事实，不能由 API 名称推断遗漏乘法；
- root 与 D3DImage 的不安全析构顺序是可观察前置条件，不能擅自加 owner nulling；
- repeated D3DImage load 的旧 holder 泄漏是当前四参考行为，不能擅自换成 RAII replacement；
- 本纵切面没有声称恢复原始文件名、namespace 或私有 helper 的精确拼写。
