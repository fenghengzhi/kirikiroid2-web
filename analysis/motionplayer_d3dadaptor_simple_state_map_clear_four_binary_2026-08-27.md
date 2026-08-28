# D3DAdaptor 简单状态、兼容空操作与纹理 map 清空四参考二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的边界

本轮逐体闭合 `Motion.D3DAdaptor` 除 Factory 和 `captureCanvas` 之外的 14 个 NCB
成员：四个普通状态写入方法、`removeAllTextures`、六个兼容空操作，以及四组 bool
property。Factory 已由注册面报告闭合；`captureCanvas` 涉及 Layer 图像尺寸、pitch、纹理
替换和异常所有权，继续保留为独立深层 slice，不能由本报告代替。

四端 fresh decompile 和完整 disassembly 表明，本地源码已经精确保持这些 body 的共同源
结构，因此本轮无需修改运行时 C++。

## 2. 四端 callback 等价类

### 2.1 普通 method

| script member | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 | 共同语义 |
|---|---:|---:|---:|---:|---|
| `setPos` | `0x6AAB84` | `0x57CF64` | `0x100103D3C` | `0x101128` | 空操作 |
| `setSize` | `0x6AAB88` | `0x57CF66` | `0x100103D40` | `0x10112A` | 依次写 width、height 两个 int32 |
| `setClearColor` | `0x6AAB90` | `0x57CF6C` | `0x100103D48` | `0x101130` | 写一个 int32，不触发 clear |
| `setResizable` | `0x6AAB98` | `0x57CF70` | `0x100103D50` | `0x101134` | 写 `_resizable` byte |
| `removeAllTextures` | `0x6AAC98` | `0x57CF74` | `0x100103D58` | `0x101138` | 销毁整棵软件纹理副本 map 并复位为空 |
| `removeAllBg` | `0x6AACD0` | `0x57CF7A` | `0x100103D88` | `0x101154` | 空操作 |
| `removeAllCaption` | `0x6AACD4` | `0x57CF7C` | `0x100103D8C` | `0x101156` | 空操作 |
| `registerBg` | `0x6AACD8` | `0x57CF7E` | `0x100103D90` | `0x101158` | 空操作；typed NCB 转换仍发生在 wrapper |
| `registerCaption` | `0x6AACDC` | `0x57CF80` | `0x100103D94` | `0x10115A` | 空操作；typed NCB 转换仍发生在 wrapper |
| `unloadUnusedTextures` | `0x6AACE0` | `0x57CF82` | `0x100103D98` | `0x10115C` | 空操作 |

`setPos` 加后五个 legacy method 构成六个跨平台 no-op 等价类。这里的“空操作”只描述
native body；公开 wrapper 的参数个数门禁、Variant/float/bool 转换和 result 清空行为仍然
存在，已由注册面报告和现有 NCB 测试覆盖。

### 2.2 read/write property

| property | Android arm64 getter/setter | Android armv7 getter/setter | iOS arm64 getter/setter | iOS armv7 getter/setter |
|---|---|---|---|---|
| `visible` | `0x6AACE4` / `0x6AACEC` | `0x57CF84` / `0x57CF88` | `0x100103D9C` / `0x100103DA4` | `0x10115E` / `0x101162` |
| `alphaOpAdd` | `0x6AACF8` / `0x6AAD00` | `0x57CF8C` / `0x57CF90` | `0x100103DAC` / `0x100103DB4` | `0x101166` / `0x10116A` |
| `canvasCaptureEnabled` | `0x6AAEC8` / `0x6AAED0` | `0x57D09C` / `0x57D0A0` | `0x100103F88` / `0x100103F90` | `0x10127C` / `0x101280` |
| `clearEnabled` | `0x6AAEDC` / `0x6AAEE4` | `0x57D0A4` / `0x57D0A8` | `0x100103F98` / `0x100103FA0` | `0x101284` / `0x101288` |

Android arm64 registrar 的展开布局不能按相邻 descriptor 字段位置猜 getter/setter 方向；
上表方向来自每个 body 的实际 load/store。四端 getter 都把对应 byte 零扩展为 bool；setter
都只写该 byte。Android arm64 setter 在写入前显式 `& 1`，另外三端在 typed bool 转换后
直接写 byte，源级共同结构仍是普通 `bool` getter/setter。

## 3. 共同字段布局和源级伪代码

前五个 int32 字段在四端均从对象偏移 `+0` 开始；指针宽度只影响后续字段：

| 状态 | LP64 偏移 | ILP32 偏移 | 行为 |
|---|---:|---:|---|
| width / height | `+0` / `+4` | `+0` / `+4` | `setSize` 成对顺序写入 |
| visible | `+20` | `+20` | byte echo state |
| canvasCaptureEnabled | `+21` | `+21` | byte state |
| clearEnabled | `+22` | `+22` | byte state |
| resizable | `+23` | `+23` | byte state |
| alphaOpAdd | `+24` | `+24` | byte echo state |
| clearColor | `+40` | `+32` | int32 retained state |

共同源级形状为：

```text
setSize(w, h):
    this.width = w
    this.height = h

setClearColor(color):
    this.clearColor = color

setResizable(value):
    this.resizable = bool(value)

getFlag(slot):
    return zero_extend(this.slot)

setFlag(slot, value):
    this.slot = bool(value)
```

`setClearColor` 不调用 render manager，也不清理 target；`setResizable` 没有 resize 副作用；
四组 property 同样没有隐藏的 dirty、notify 或资源操作。

## 4. `removeAllTextures` 的容器和所有权

四端都把该成员实现为一棵 STL 有序树的 whole-map clear。相关 helper：

| 平台 | clear/erase helper | 节点销毁 helper或清理入口 | 遍历形状 |
|---|---:|---:|---|
| Android arm64 | `0x6D8C38` | 内联于同一 helper | 右子树递归，左链迭代 |
| Android armv7 | `0x59A8CE` clear/reset；`0x59A8EC` erase-tree | `0x59A918` destroy-node | 右子树递归，左链迭代 |
| iOS arm64 | `0x1001285E4` | 内联于同一 helper | 左、右子树递归 |
| iOS armv7 | `0x127928` | `0x1279B4` SjLj terminate cleanup | 左、右子树递归 |

LP64 节点的 mapped value 位于 `node+40`，ILP32 位于 `node+20`。销毁每个节点时，四端
都对 mapped texture holder 调用 intrusive `Release`，再释放树节点；树 key 本身只是原始
texture 指针身份，没有对应 `Release`。完成后 header/root/左右极值/count 被复位为标准空树。

共同源级形状为：

```text
removeAllTextures():
    softwareTextureCopies.clear()

destroyTree(node):
    destroy children according to the platform STL traversal shape
    node.mappedTextureHolder.Release()
    delete node
```

这与本地容器
`std::map<iTVPTexture2D *, TJS::tTJSRefHolder<iTVPTexture2D>>` 精确对应：key 是 borrowed
source identity；mapped holder 拥有软件副本的一个 intrusive 引用。清空 map 只释放
holder 引用，不释放 borrowed key，也不主动回收调用者仍持有的 factory-return 引用。
iOS armv7 的 SjLj cleanup 在析构逃逸时进入 terminate 路径，符合标准容器析构/clear 的
异常边界；共同 C++ 源不需要手写 ABI 清理代码。

## 5. 本地实现和验证映射

- `cpp/plugins/motionplayer/D3DAdaptor.h:50` 起的四组 property、`:62` 起的简单方法和
  `:129` 起的 map key/value 类型与四端 body 一致；
- `cpp/plugins/motionplayer/D3DAdaptor.cpp:73` 的 `setSize` 与 `:78` 的
  `_softwareTextureCopies.clear()` 保持原始源结构；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:10170` 已覆盖真实 NCB descriptor、typed
  nullsub 参数门禁、surplus 参数和四组 property echo；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:11277` 已覆盖软件副本 map holder 引用、
  `removeAllTextures` 后计数归零，以及调用者 factory-return 引用仍存活的边界。

本轮没有添加伪造 fixture，也没有为了贴合某一 ABI 改写 `std::map` 为手工红黑树。

## 6. fresh 证据和剩余工作

- 完整读取四端 18 个 callback 函数（10 个 method body 加 8 个 property accessors），
  指令数分别为 47 / 30 / 40 / 40；
- 完整读取四端 map clear/erase/destroy helper，指令数分别为 24 / 38 / 23 / 59；
- 四个 IDB 已对 map clear/erase/destroy helper 命名，对 `removeAllTextures` 根添加注释和
  书签，并原位保存；
- 14 个非 Factory、非 `captureCanvas` 成员可从
  `BODY_PENDING_SEPARATE_SLICE` 提升为 `IMPLEMENTED`；
- 本报告完成时 `captureCanvas` 仍保留 pending；随后
  `MP-D14-D3DADAPTOR-CAPTURE-CANVAS` 已闭合其 software/GPU body、target texture 替换和
  共享 Layer helper 边界。构造/析构异常生命周期、共享 renderer 和 Player 交叉数据流
  已由 `MP-L14-D3DADAPTOR-LIFECYCLE` 闭合；共享 renderer 和 Player 交叉数据流继续由
  其他或后续 slice 闭合。software map 的查找、插入、命中与 factory/holder 失败边后来由
  `MP-R14-D3D-SOURCE-GETTER-MAP-INSERT` 闭合；shared deep renderer 的外层、batch、method 与
  stencil 又由 `MP-R14-D3D-DEEP-BATCH-STENCIL` 闭合，公共 mesh helper 随后由
  `MP-R14-D3D-MESH-SUBMIT-CELLS` 闭合。

当前环境缺少 CMake、Ninja 和 Emscripten，且单头文件语法检查被缺失的
`boost/locale.hpp` 阻塞，因此本 slice 不宣称完成正式 native/Web 构建。
