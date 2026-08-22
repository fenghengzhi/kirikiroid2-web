# Motion.EmotePlayer 原生 raw controller setters 四参考二进制复核

## 结论

本轮从 `EmotePlayer_ncb_registerMembers_guess` 的连续 member 14–19 注册槽重新定位，
对 `setCoord`、`setScale`、`setRotate`、`setColor`、`setOuterForce` 以及共享
outer-force router 做了四端 fresh decompile/xref 复核。结论是：

1. 这些函数不是接收 `iTJSDispatch2 *objthis` 后自行查 native instance 的普通 TJS
   callback；第四参数已经是 NCBind wrapper 解出的 Engine-sized `EmotePlayer` payload。
2. 每个 callback 自己只做最小 argc 检查。参数不足统一返回
   `TJS_E_BADPARAMCOUNT (-1004)`；argc 覆盖的 argv slot 被直接信任，没有 null 容错。
3. 必填参数先按源码顺序转换，随后才转换可选 transition 和 ease。此前端口把可选
   参数提前求值，且把缺参和 null slot 合并成 `TJS_E_INVALIDPARAM`，均不匹配参考。
4. ease 的 piecewise 映射在 double 域完成，最后才和目标值、transition 一起窄化为
   float。`ease==0` 得 1，正数得 `ease+1`，负数得 `1/(1-ease)`。
5. `setColor` 先执行 TJS `AsInteger`，然后把结果的低 32 位按低字节到高字节拆为
   四个无符号 0–255 float channel。
6. `setOuterForce` 的临时 `ttstr` owner 跨越共享 router 调用并在返回后立即析构；共享
   router 在标签比较前窄化 x/y，只对精确的 `bust`、`hair`、`parts` 命中进一步窄化
   duration/power 并调用 controller。未知标签静默返回，也不写 dirty。

源码已把 member 14–19 统一恢复为 NCBind native-instance raw-callback signature，并把
本轮五个 setter 的转换、double ease、float narrowing 和 outer-force 路由顺序对齐。

## 四端函数映射

| 函数 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `EmotePlayer_setCoordCompat_guess` | `0x66F440` / `0x2BC` | `0x55A450` / `0xE6` | `0x1001AD778` / `0x11C` | `0x1ACE5C` / `0xF6` |
| `EmotePlayer_setScaleCompat_guess` | `0x66F6FC` / `0x24C` | `0x55A548` / `0xD0` | `0x1001AD894` / `0xDC` | `0x1ACF52` / `0xBE` |
| `EmotePlayer_setRotateCompat_guess` | `0x66F948` / `0x214` | `0x55A628` / `0xB2` | `0x1001AD970` / `0xCC` | `0x1AD010` / `0xBC` |
| `EmotePlayer_setColorCompat_guess` | `0x66FB5C` / `0x268` | `0x55A6E8` / `0x94` | `0x1001ADA3C` / `0xB8` | `0x1AD0CC` / `0x9E` |
| `EmotePlayer_setOuterForceCompat_guess` | `0x66FE58` / `0x2E0` | `0x55A828` / `0xDC` | `0x1001ADB98` / `0xF0` | `0x1AD218` / `0x134` |
| `EmoteEngine_setOuterForceTarget_guess` | `0x670138` / `0xEC` | `0x55A928` / `0xB0` | `0x1001ADC9C` / `0xD8` | `0x1AD37C` / `0xE2` |

Android arm64 中五个 callback 均有两处 data xref 落在
`EmotePlayer_ncb_registerMembers_guess @ 0x67CEA8` 的连续注册序列；iOS arm64 中每个
callback 各有一处 data xref 落在 registrar `0x1001B5130`。两份 32 位产物经寄存器
间接调用，IDA 未恢复到函数入口的静态 data xref，但函数相对次序、相邻边界、字符串
注册槽与 callback body 形状都与 64 位参考一致。

共享 outer-force router 在四端都只有两个 code caller：Primary raw callback 和
D3DEmotePlayer direct façade。D3D 入口分别为 `0x530E6C`、`0x495048`、
`0x1002334BC`、`0x2321F8`。

## Native-instance raw callback ABI

六个连续 member 14–19 的 callback body 直接用第四参数访问 Engine controller、
queuing 和 dirty 字段；函数体内不存在 `objthis` native-class lookup。对应的 NCBind
源码形状是：

```cpp
static tjs_error callback(tTJSVariant *result,
                          tjs_int numparams,
                          tTJSVariant **param,
                          EmotePlayer *nativeInstance);
```

这会选择 `ncbRawCallbackMethod<tjs_error (*)(..., T *)>` 特化：Function dispatch
先验证 receiver、解出 `T *`，再进入 callback。端口原来的 `iTJSDispatch2 *` 末参会
选择普通 `tTJSNativeClassMethodCallback` wrapper，迫使 callback 自己解包，并把错误
路径变成端口私有的 `TJS_E_INVALIDOBJECT`。本轮已删除这层伪调用链。

callback body 的 argc gate 仍然位于参数转换之前。直接调用 body 时，即使 native
payload/argv 为 null，只要 argc 不足也会先得到 `TJS_E_BADPARAMCOUNT`；从脚本 Function
dispatch 进入时，NCBind 的 receiver gate 则发生在 callback body 之前。

## 参数转换与缺省值

共同顺序如下：

```text
setCoord:      argc<2 -> -1004; x; y; transition?; ease?
setScale:      argc<1 -> -1004; scale; transition?; ease?
setRotate:     argc<1 -> -1004; rotation; transition?; ease?
setColor:      argc<1 -> -1004; packed integer; transition?; ease?
setOuterForce: argc<3 -> -1004; label owner; x; y; transition?; ease?
```

transition 缺省 `0.0`。ease 的 callback-local power 缺省 `1.0`；存在 ease 时在 double
域执行：

```cpp
double power = 1.0;
if(ease > 0.0) {
    power = ease + 1.0;
} else if(ease < 0.0) {
    power = 1.0 / (1.0 - ease);
}
```

这里与 member 14 有一个关键差异：15–19 的 power 只经过这一层映射后直接进入
controller；member 14 的 callback-local结果还会被 general Engine `setVariable`
router 再映射一次。

argc 只用于决定 slot 是否存在。参考 callback 不检查 `param`、必填 slot 或可选 slot
指针是否为 null，也不会把覆盖范围内的 null 指针解释为缺省值。

## dirty、queuing 与 float 提交

Primary 的 coord/scale/rotate/color 四个入口共同：

- 先完成全部 TJS-to-double/TJS-to-integer 转换和 double ease 映射；
- 随后形成目标 float、duration float、power float；
- 读取直属 controller 和同一个 queuing byte；
- 写 dirty byte 为 1；
- 调用 Var/Angle controller target setter。

优化器在两份 64 位和两份 32 位产物中会围绕无副作用的 `FCVT`/field load 重排个别
指令，但共同的语义提交点是 dirty 紧邻最终 controller 调用，且任何可能抛出的 Variant
转换都早于 dirty。源码因此先缓存 double power、float 参数、controller 与 queuing，
再提交 dirty。

outer-force 不写 dirty。它与 D3D façade共同进入共享 router，并使用 queuing 决定
replace/append。

## Color 的整数与字节边界

Android armv7、iOS arm64 和 iOS armv7 显式调用 TJS `AsInteger`；Android arm64 把
同一 Variant 整数转换路径内联。结果只消费低 32 位：

```cpp
uint32_t packed = static_cast<uint32_t>(integer);
float values[4] = {
    float(uint8_t(packed)),
    float(uint8_t(packed >> 8)),
    float(uint8_t(packed >> 16)),
    float(uint8_t(packed >> 24))
};
```

这不是 ARGB 的高字节优先展开。以 `0x44332211` 为例，controller 收到
`17, 34, 51, 68`。四 channel controller 的 20-byte keyframe 仍保留既有的
alpha/duration word alias；该容器边界见
`analysis/motionplayer_controller_setters_four_binary_2026-08-11.md`。

## Outer-force owner、路由和窄化边界

Primary callback 先从 `param[0]` 构造一个栈上 `ttstr` owner，再转换 x/y、可选
transition/ease。共享 router 返回后 callback 立即释放 owner；label 不会被 controller
或 Engine 保留。

共享 router 的归一顺序是：

```cpp
float values[2] = {float(x), float(y)};
controller = exactRoute(label);  // bust -> hair -> parts
if(!controller) return;
controller->setTarget(values, float(duration), float(power), queuing);
```

因此未知标签仍会执行 x/y 的 double-to-float conversion，但不会窄化 duration/power、
不会读取目标 controller、不会排队，也不会设置 dirty。此前端口先路由再构造 values，
并在进入 router 前把 power 提前窄化成 float；本轮均已恢复。

Android arm64 在每次 UTF-16 比较前显式检查 `ttstr` backing owner，其他三端把相同
空字符串/比较语义收进字符串 helper；共同可观察结果仍是精确、区分大小写的三标签
匹配，无 fallback。

## 源码、IDB 与回归改动

- `EmotePlayer.h/.cpp`：member 14–19 callback 末参统一改为
  `EmotePlayer *nativeInstance`，删除 callback 内手工 native-instance lookup。
- `setCoord/setScale/setRotate/setColor`：double ease 映射和所有 float 参数先形成，
  缓存 controller/queuing 后再写 dirty 与 enqueue。
- 五个 compat callback：参数不足统一为 `TJS_E_BADPARAMCOUNT`，删除 argv null 容错，
  恢复必填到可选的转换顺序。
- `setOuterForce`：向共享 router 传 double power；共享 router 在标签比较前形成 x/y
  float values。
- 四个 recovery IDB 中 20 个 callback 统一命名为 `*Compat_guess`，应用共同 prototype，
  强制刷新反编译，并在五个 callback 与共享 router 上写入注释和书签。
- 单元回归覆盖六个 callback 的 argc-first body gate、native payload direct call、
  coord 参数/power 入队，以及注册后的 NCBind wrapper 对 foreign receiver 先返回
  `TJS_E_NATIVECLASSCRASH`。

## 验证

- 完整 motionplayer Catch2 翻译单元 Emscripten syntax-only：通过；只有项目既有
  `_tss` literal-operator 弃用告警。
- `cmake --build --preset "Web Debug Build"`：10-step 增量编译与 `index.html` 完整
  链接通过；只有项目既有编译器/Emscripten 告警。
- 本纵切面文件的 `git diff --check`：通过；只有工作区既有 LF/CRLF 提醒。
- 四个 recovery IDB 均已在语义命名、prototype、注释与 bookmark 写回后原位保存。
