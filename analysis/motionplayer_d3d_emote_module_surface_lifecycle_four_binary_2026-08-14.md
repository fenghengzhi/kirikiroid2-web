# global.D3DEmoteModule 注册面、布局与生命周期四参考审计（2026-08-14）

## 结论

`global.D3DEmoteModule` 是一个很小但独立的 native-instance NCB 类。四份当前
参考二进制共同给出同一份源码级结构：

- 一个零参数 typed constructor；
- 不注册任何常量；
- 恰好七个 typed 成员，并保持固定交错顺序；
- 每个 property accessor 都是直接字段读写；
- `setMaxTextureSize(width, height)` 只按宽、高手工写入两个相邻 `int32`；
- 实例只有一个虚析构 vptr，不携带 Player、Engine 或 D3DEmotePlayer owner；
- `D3DEmotePlayer.module` 从父级 DrawDevice/root 的 class-id 有序 map 懒取/懒建，
  D3DEmotePlayer native shell 本身不保存返回对象；但 generated non-sticky result adaptor
  会与 root map 同时成为该 raw pointer 的 owner。

因此旧单 Android-arm64 注释把 `0x52E504` 解释为 D3DEmotePlayer table 的做法是错误
的：该位置落在当前 `D3DEmoteModule` registrar 函数内部。旧端口把这些配置实现成
静态全局字段、把默认值解释成零、把 `setMaxTextureSize` 留作日志 stub，也都与四份
当前参考不符。

> **2026-08-17 ClassInfo/owner 更正：** 本文的成员表、对象布局和字段访问器结论继续有效，
> 但“root map 是唯一 owner”不完整。`D3DEmotePlayer.module` 返回 `D3DEmoteModule *` 后，
> generated result converter 会调用 `CreateAdaptor(native,false,false)`；成功 adaptor 也是
> non-sticky owner。于是 root map 与 TJS adaptor 会同时 delete 同一 pointer，且 module 没有
> back-pointer 清 map。独立 ClassInfo static init 也不是旧文沿用的七类 registration bundle，
> 正确四端入口是 `0x42CB18 / 0x2FEFD4 / 0x10024CA40 / 0x24E628`。完整注册、constructor、
> 三态 boxing 与 no-unload 以
> `motionplayer_d3d_emote_module_classinfo_constructor_double_owner_four_binary_2026-08-17.md`
> 为准。

## 四端函数映射

| 角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| registrar | `0x52E388` | `0x493E54` | `0x100232078` | `0x230DB0` |
| registrar 大小 | `0x448` | `0xC4` | `0x148` | `0x13A` |
| typed native constructor | `0x54177C` | `0x4A30F0` | `0x100244AA8` | `0x244F64` |
| `createAdaptor` | `0x5430A4` | `0x4A44FC` | `0x100246344` | `0x246B54` |
| `D3DEmotePlayer.getModule` | `0x52FF78` | `0x494864` | `0x100232B68` | `0x2317C0` |

四个 registrar 都先创建零参数 constructor descriptor，再按下面顺序构造七个
member descriptor。它们没有 `MaskMode*` 或 `TimelinePlayFlag*` 常量；四个共享常量
属于相邻但独立的 `Motion.D3DEmotePlayer` 注册面。

这里“零参数”是 C++ signature，不是 exact-arity gate。outer wrapper 对每个非负 argc 都
进入 constructor invoke 并完全忽略 argv；只有恰好 `argc==1` 且 arg0 为 Void 时提前成功，
只保留 empty adaptor。负 argc 返回 `TJS_E_BADPARAMCOUNT`。因此普通零参和 surplus 参数都
会构造 module，而两参数且 arg0 为 Void 也不会触发 sentinel。

## 精确 NCB 发布顺序

| 序号 | 脚本名 | descriptor family | native 形态 |
|---:|---|---|---|
| 1 | `maskMode` | typed RW property | `int get() const` / `void set(int)` |
| 2 | `maskRegionClipping` | typed RW property | `bool get() const` / `void set(bool)` |
| 3 | `mipMapEnabled` | typed RW property | `bool get() const` / `void set(bool)` |
| 4 | `alphaOp` | typed RW property | `int get() const` / `void set(int)` |
| 5 | `protectTranslucentTextureColor` | typed RW property | `bool get() const` / `void set(bool)` |
| 6 | `pixelateDivision` | typed RW property | `int get() const` / `void set(int)` |
| 7 | `setMaxTextureSize` | typed method | `void(int width, int height)` |

没有 raw callback，也没有 property/method 分组后的第二轮发布。这里的顺序会决定
descriptor 构造和异常部分发布状态，因此本地 `main.cpp` 按真实顺序逐项编号。

## 对象布局与构造默认值

四份 constructor 都先分配完整对象、写入虚表，再写默认字段，最后把 native pointer
安装到脚本实例的 ncbind adaptor slot。64 位分配 `0x20` 字节，32 位分配 `0x1C`
字节。

| 字段 | 64 位偏移 | 32 位偏移 | 类型 | constructor 默认值 |
|---|---:|---:|---|---:|
| vptr | `+0` | `+0` | pointer | `D3DEmoteModule` vtable |
| `maskMode` | `+8` | `+4` | `int32` | `1` |
| `maskRegionClipping` | `+12` | `+8` | `bool` | `false` |
| `mipMapEnabled` | `+13` | `+9` | `bool` | `true` |
| `protectTranslucentTextureColor` | `+14` | `+10` | `bool` | `false` |
| padding | `+15` | `+11` | byte | ABI padding |
| `alphaOp` | `+16` | `+12` | `int32` | `0` |
| `pixelateDivision` | `+20` | `+16` | `int32` | `100` |
| `maxTextureWidth` | `+24` | `+20` | `int32` | `0` |
| `maxTextureHeight` | `+28` | `+24` | `int32` | `0` |

字段的源码声明顺序看起来把 `protectTranslucentTextureColor` 放在 `alphaOp` 之前，
而 NCB 发布顺序把 `alphaOp` 放在 property #4；两者并不矛盾。registrar 顺序不是
C++ member declaration order 的证据，constructor 与十三个 tiny accessor 才共同锁定
上表布局。

该类之所以有 vptr，仅因为 `D3DModuleBase_guess` 提供虚 deleting destructor。四端
complete/deleting destructor 均为 trivial；没有额外 owned pointer、reference count 或
容器 member。

typed constructor 的失败边界也保持 native 形态：创建出来的 native object 只有在
脚本 receiver/instance slot 可安装时才发布；slot 缺失路径调用虚 deleting destructor 后
返回 native-class construction error。它不是父级 module cache 的创建入口；脚本直接
`new Motion.D3DEmoteModule()` 得到独立对象。

## Accessor 与边界行为

### 四端 accessor 地址

| native 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| get/set `maskMode` | `0x52E7D0` / `0x52E7D8` | `0x493FD2` / `0x493FD6` | `0x100232210` / `0x100232218` | `0x230F10` / `0x230F14` |
| get/set `maskRegionClipping` | `0x52E7E0` / `0x52E7E8` | `0x493FDA` / `0x493FDE` | `0x100232220` / `0x100232228` | `0x230F18` / `0x230F1C` |
| get/set `mipMapEnabled` | `0x52E7F4` / `0x52E7FC` | `0x493FE2` / `0x493FE6` | `0x100232230` / `0x100232238` | `0x230F20` / `0x230F24` |
| get/set `alphaOp` | `0x52E808` / `0x52E810` | `0x493FEA` / `0x493FEE` | `0x100232240` / `0x100232248` | `0x230F28` / `0x230F2C` |
| get/set `protectTranslucentTextureColor` | `0x52E818` / `0x52E820` | `0x493FF2` / `0x493FF6` | `0x100232250` / `0x100232258` | `0x230F30` / `0x230F34` |
| get/set `pixelateDivision` | `0x52E82C` / `0x52E834` | `0x493FFA` / `0x493FFE` | `0x100232260` / `0x100232268` | `0x230F38` / `0x230F3C` |
| `setMaxTextureSize` | `0x52E83C` | `0x494002` | `0x100232270` | `0x230F40` |

重新应用 prototype 后，四端反编译都收敛为相同伪代码：

```cpp
int getMaskMode(const D3DEmoteModule *self) {
    return self->maskMode;
}

void setMaskMode(D3DEmoteModule *self, int value) {
    self->maskMode = value;
}

bool getBooleanField(const D3DEmoteModule *self) {
    return self->field;
}

void setBooleanField(D3DEmoteModule *self, bool value) {
    self->field = value;
}

void setMaxTextureSize(D3DEmoteModule *self, int width, int height) {
    self->maxTextureWidth = width;
    self->maxTextureHeight = height;
}
```

可观察边界如下：

- `maskMode`、`alphaOp`、`pixelateDivision` 保留完整 native `int32`，包括负数和
  `INT_MIN/INT_MAX`；没有 enum range check 或 clamp。
- 三个 Boolean property 经 typed ncbind 转为 `bool` 后存一个字节。AArch64 的部分
  反编译显示 `value & 1`，其余目标显示 byte store；这是 generated typed conversion/
  codegen 差异，不是不同的脚本 API。
- `setMaxTextureSize` 不验证负数、零或宽高关系，不做上限裁剪，不置 modified/dirty，
  也不回传值。
- 这些字段不向 `Player`、`EmoteEngine` 或既有 D3DEmotePlayer shell 传播。
  特别地，`D3DEmoteModule.pixelateDivision` 与 `Player.pixelateDivision` 是两个独立
  raw `int32`。

## `D3DEmotePlayer.module` 数据流与所有权

四个 `getModule` wrapper 的等价数据流为：

```text
D3DEmotePlayer shell
  -> D3DLayer / DrawDevice owner
  -> parent/root module map keyed by ncbClassInfo<D3DEmoteModule>::GetID()
       hit with non-null value -> return existing pointer
       miss or null value      -> new D3DEmoteModule
                                  store into parent map
                                  return the stored raw pointer
```

它不是 per-shell cache。共享同一父级/root module map 的多个 D3DEmotePlayer 会观察
同一个 `D3DEmoteModule`，而不同父级各自保存独立实例。D3DEmotePlayer 的 `clear`、`load`
和析构只处理 primary/secondary EmoteObject slot，不清除此 map entry。若 getter 的 result
为 null，native getter 仍会取/建 map value，但跳过 boxing；result 非 null 时则另建一个
non-sticky TJS adaptor，成功后与 map 并列拥有同一 raw pointer。

本地 DrawDevice 侧保持 `std::map<tjs_uint32, D3DModuleBase_guess *>`，root destructor
遍历并 `delete` 每个值。这与参考中的 class-id 有序 map、raw pointer value 和父级统一
销毁相符，也解释了 base 为什么必须有虚析构。

边界行为同样没有被美化：

- native `getModule` 假定 shell 的 D3D owner 非空；空 owner 会自然走到解引用失败，
  没有 graceful-null return。
- map 中存在 key 但 value 为 null 时，行为与 miss 相同，会分配并覆盖/写入新值。
- allocation/store 之间使用 raw candidate；若 store 抛出，native 路径没有 RAII owner
  回收这个 candidate。当前本地 raw pointer 顺序保留这一异常边界。
- shell 不直接 delete 返回指针；但 generated pointer-result adaptor 会在失效/析构时 delete。
  wrapper 先释放会留下非 null 悬空 map value，后续 getter 重新装箱 UAF；root 先析构会让仍存活
  wrapper 指向已释放 storage；任一顺序的第二次 teardown 都可能 double free。

## 本地修正

- `main.cpp` 明确零参数 constructor、无常量和七成员精确顺序，并逐项编号。
- `D3DEmoteModule.h` 保持 per-instance 自然布局和四端默认值；所有 getter/setter 直接
  访问字段，`setMaxTextureSize` 写入宽高 pair。
- `EmotePlayer.cpp::getModule` 保持 `FindParentModule -> new -> SetParentModule` 顺序，
  并明确 parent/root map 所有权。
- 修正 `EmotePlayer.h` 中把 timeline 常量误归给 D3DEmoteModule registrar 的过时注释；
  正确 owner 是 D3DEmotePlayer registrar。
- 单元测试新增六个公开字段的 constructor 默认值、Boolean 切换、raw `int32` 边界和
  `setMaxTextureSize` typed 调用覆盖。

## IDB 改进与验证

四份 recovery IDB 已统一命名十三个 accessor，应用 `int`/`bool`/`void(int,int)`
prototype，并在 registrar、constructor、createAdaptor、getModule 与 setMaxTextureSize
写入源码级注释。类型应用后的 registrar、constructor、getModule 和全部 accessor 均已
重新反编译，无 decompiler error，随后四库原位保存。

验证结果：

- 精确 registrar 扫描得到一个零参数 constructor、七个同序成员、零常量、零 raw
  callback；与四端表完全一致。
- 整份 `motionplayer-dll.cpp` Emscripten TU syntax check 通过；只保留仓库既有 `_tss`
  literal operator deprecation warning。
- `cmake --build --preset "Web Debug Build"` 重新编译并链接成功；输出只包含既有
  `_tss`、`imagepacker.h` attribute、pthreads/JSPI 等 warning。
- 本纵切面的旧 `D3DEmoteModule registrars expose TimelinePlayFlagDifference`、
  `sub_52E504`/`0x52E504` 和 `setMaxTextureSize stub` 源码注释扫描为零。
- 当前工程没有配置可直接运行这份 Catch2 motionplayer TU 的 native unit executable；
  新回归因此完成了整 TU 编译验证，但没有伪造新的 native runner 结论。
