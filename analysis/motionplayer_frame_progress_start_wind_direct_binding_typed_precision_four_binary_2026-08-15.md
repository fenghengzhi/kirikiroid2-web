# MotionPlayer Primary #2/#5：Engine 直绑与 typed 数值精度四参考复原

日期：2026-08-15

本文只使用 `reference/binaries/` 中四个当前参考二进制，闭合 Primary
`Motion.EmotePlayer` 的第 2 项 `frameProgress` 和第 5 项 `startWind`。旧本地源码已经把
两条调用的数据流写对，但仍各保留一个 `EmotePlayer` forwarding member；fresh registrar
证据证明这个源级层次不存在：两个 descriptor 都直接保存 `EmoteEngine` 成员指针，且
member adjustment 为零。

两条入口还暴露了两个不同的 generated typed specialization：`frameProgress` 是
one-`double`/`void`，`startWind` 是 five-`float`/`void`。后者不是让 Engine 自己把五个
double 窄化，而是在 NCBind 边界依次完成 `Variant -> double -> float`。

## 1. 四端 registrar direct target

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Primary registrar | `0x67CEA8` | `0x5612E8` | `0x1001B5130` | `0x1B4DE0` |
| `frameProgress` target materialization/call | `0x67CF88..0x67CF9C` | `0x56134A` | `0x1001B51BC` | `0x1B4E5A` |
| stored progress target | `0x67A3F8` | `0x55FEF0` | `0x1001B4304` | `0x1B3E10` |
| `startWind` target materialization/call | `0x67D0A4..0x67D0F4` | `0x56138C` | `0x1001B521C` | `0x1B4EB4` |
| stored wind target | `0x66DD8C` | `0x559900` | `0x1001AC718` | `0x1ABF24` |
| adjustment（两项） | `0` | `0` | `0` | `0` |

四端恢复后的 target prototype 分别是：

```cpp
void EmoteEngine::progress(double frameDt);
void EmoteEngine::setWind_guess(float minAngle, float maxAngle,
                                float amplitude, float freqX, float freqY);
```

Android arm64 的 `frameProgress` 路径以 `MOV X3,XZR` 传零 adjustment；`startWind`
内联构造 `0x40` 字节 Function object，以全零 `Q0` 同时初始化 code/adjustment slot，
再把 Engine wind code pointer 插入低 64 位。其余三端的 factory 调用均显式传 `a4=0`。

因此对应源级注册是：

```cpp
Method(TJS_W("frameProgress"), &EmoteEngine::progress);
Method(TJS_W("startWind"), &EmoteEngine::setWind_guess);
```

## 2. one-double/void Function 家族

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| create Function | `0x67EBA0` | `0x56A488` | `0x1001C61A0` | `0x1C34E0` |
| allocate | create 内联 | `0x56A4BC` | `0x1001C61F4` | `0x1C3508` |
| Function ctor | create 内联 | `0x56A4F8` | `0x1001C6258` | `0x1C35C8` |
| vtable | `0x1A15C88` | `0x10B8850` | `0x101AE94E8` | `0x18366CC` |
| `FuncCall` | `0x68A020` | `0x56A560` | `0x1001C62F0` | `0x1C36C8` |
| convert/invoke | `0x68A0CC` | `0x56A684` | `0x1001C6370` | `0x1C3798` |

64 位 Function object 是 `0x40` 字节，成员指针 code/adjustment 位于 `+0x30/+0x38`；
32 位对象是 `0x24` 字节，对应 `+0x1C/+0x20`。这与此前 state/getVariable typed
families 的 ordinary Function 布局一致，只是 specialized vtable/FuncCall 不同。

### 2.1 外层边界

四端共同顺序：

```text
membername != null -> TJS_E_MEMBERNOTFOUND, result untouched
objthis == null     -> TJS_E_NATIVECLASSCRASH, result untouched
result != null      -> clear to Void
numparams < 1       -> TJS_E_BADPARAMCOUNT
unwrap native       -> failure is TJS_E_NATIVECLASSCRASH
copy/convert only param[0]
invoke stored Engine member
return TJS_S_OK
```

`param[1..]`、flag、hint 均忽略。Android/iOS AArch64 把 argc gate 和 unwrap 合入下一级
helper，两个 32 位端在 `FuncCall` 中直接展开；可观察顺序相同。

### 2.2 参数 owner 与精度

invoke copy-construct `param[0]` 为本地 Variant，调用 `AsReal()` 得到 `double`，随后在
进入 Engine 前析构该 Variant。返回值不经过 `float` 临时量：完整 64-bit double 直接按
平台浮点 ABI 传给 `EmoteEngine::progress`。Hex-Rays 在 AArch64 上显示的
`long double/q8` 是寄存器类型伪影，32 位端成对返回/传递的两个 32-bit word 和 core
prototype 均确认源类型是普通 double。

void typed family 已在调用前清空 result；成功后不再建立返回 Variant。`result==nullptr`
只跳过 clear，不跳过参数转换或 Engine 调用。

## 3. five-float/void Function 家族

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| create Function | registrar 内联 | `0x56A924` | `0x1001C6770` | `0x1C3BDC` |
| allocate | registrar 内联 | `0x56A958` | `0x1001C67C4` | `0x1C3C04` |
| Function ctor | registrar 内联 | `0x56A994` | `0x1001C6828` | `0x1C3CC4` |
| vtable | `0x1A15EC8` | `0x10B8970` | `0x101AE9728` | `0x18367EC` |
| `FuncCall` | `0x68A3A8` | `0x56A9FC` | `0x1001C68C0` | `0x1C3DC4` |
| convert/invoke | `0x68A4C4` | `0x56AAC0` | `0x1001C69A0` | `0x1C3E5C` |

外层顺序与 one-double family 相同，唯一 arity 差异是 `numparams >= 5`。receiver 非空
后 result 先清空，所以 argc 0..4 和 native unwrap failure 都留下 Void result；null
receiver 仍保持旧 result。`param[5..]` 完全不读取、不复制、不转换。

### 3.1 五项顺序转换

四端按 index 0、1、2、3、4 顺序重复：

```text
copy-construct Variant(param[i])
double wide = copied.AsReal()
destroy copied Variant
remember wide
```

得到五个 double 后，各自显式窄化成 float，再按 native ABI 调 Engine wind member。iOS
arm64 把第五项在其 Variant destructor 前窄化，其余项在五次 destructor 后成批窄化；
这不改变 owner 或数值边界，因为窄化只读已经保存的 raw double。32 位端也先保存五个
double word-pair，再以 VFP conversion 形成五个 float。

如果第 i 项 Variant conversion 抛出，前面已经完成的临时 Variant 均已析构，当前临时
量由 unwind 清理，后续项和 Engine core 都不会执行。普通成功路径在调用 Engine 前已经
释放全部参数 Variant copy。

### 3.2 wind core 的输入意义

NCBind 传入的五个 float 随后由 Engine core 完成：

- `abs(amplitude)`；
- 负 amplitude 时交换 min/max；
- 依据 64/32 位参考各自的 stop predicate 决定 delete/null 或创建 emitter；
- 更新五个 float cache 与 emitter gate/velocity。

因此 double→float 必须发生在这些比较、绝对值、交换和 stop predicate 之前。用 facade
接受 double 再转发，或把 Engine 签名改成 double，都会改变临界值、NaN payload 和
相邻可表示 float 的边界。

## 4. 调用链与对象生命周期

```text
script frameProgress(value, ...surplus)
  -> one-double/void typed Function
  -> unwrap Primary adaptor
  -> copied Variant(param0) -> double -> destroy copy
  -> EmoteEngine::progress(double)

script startWind(a,b,c,d,e, ...surplus)
  -> five-float/void typed Function
  -> unwrap Primary adaptor
  -> five independent Variant copies / AsReal / destructors
  -> five double-to-float narrowings
  -> EmoteEngine::setWind_guess(float x5)
```

Function object 由类表拥有，不拥有 native Engine。adaptor 管理 Primary payload，零
adjustment 直接把其首地址作为 Engine self。progress 调用期只有本地参数 Variant owner；
wind Function 同样不拥有 emitter，`_windEmitter` 的 raw owner 与五个 cache 都属于 Engine，
其创建、替换或 delete/null 生命周期完全发生在 Engine core 内。

## 5. 源码、回归与 recovery IDB

源码已删除 `EmotePlayer::frameProgress` 和 `EmotePlayer::startWind` declaration/body，
registrar 改为两个显式 inherited Engine member binding。保留 `EmotePlayer::progress`：
第 1 项确实是独立的 milliseconds→frames wrapper，不能与第 2 项合并。也保留 Primary
`stopWind`：第 6 项确实是专用 delete/null body，不是第 5 项的全零参数别名。

回归锁定：

- 两个 Engine 成员指针的严格签名；
- membername/receiver/result-clear/arity/native-unwrap 顺序；
- one-double family 的 surplus 忽略、完整 double 入口和 zero-step dirty drain；
- five-float family 的 4/5 参数边界、六参调用时第六个字符串不转换；
- 五个输入先按 TJS Real 读取、再各自窄化 float，并由 Engine 更新相应 cache；
- null result 仍执行全部转换和 Engine wind core。

四份 recovery IDB 已统一补入两个 Function family 的 create/allocate/ctor/FuncCall/invoke
语义名、精确 8 参数 FuncCall prototype、registrar direct-target 注释和四组 bookmark。
强制失效 Hex-Rays cache 后，四端均回读到新的 typed 名称、最小 argc 和调用顺序。

验证结果：

- 完整 motionplayer Catch2 翻译单元使用 Web Debug 的真实 Emscripten 参数执行
  `-fsyntax-only` 成功，只有仓库既有 `_tss` warning；
- `cmake --build --preset "Web Debug Build" --parallel` 完成 10 步重编译与最终
  `index.html` 链接；
- 定向 `git diff --check` 通过，并确认源码不再残留两个 Primary facade body 或对应
  `NCB_METHOD` 绑定；
- 四份 recovery IDB 最终原位保存均返回 `ok=true`。

当前配置仍没有可直接运行的 native Catch2 motionplayer executable，因此这里只记录
完整测试翻译单元的真实 Web ABI 编译结果，不把 syntax-only 表述为运行时测试已执行。
