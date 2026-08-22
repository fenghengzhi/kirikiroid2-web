# motionplayer setVariable 路由、双层 ease 与整数转换四参考复核（2026-08-15）

## 结论

本轮对四份 `reference/binaries/` 重新取得通用 Engine router、Primary
`Motion.EmotePlayer` raw callback、D3D façade、五组caller xref与关键指令窗口。fresh
证据修正了三个本地边界：

1. Engine在HM6命中后用double计算第五参数的piecewise factor，但只在实际controller
   调用分支通过index/label/gate之后才把value、transition、factor窄化为float；
2. mouth第一label直接写 `beginFrame`，使用 `FCVTZS W,D` / `VCVT.S32.F64` 的signed
   toward-zero saturation，不经过float；本地原 `static_cast<int>` 对NaN/越界为C++ UB；
3. Primary EmotePlayer raw callback会先把脚本ease以double做一次piecewise变换，再调用
   Engine；Engine对该结果再做同一变换。D3D façade不做Primary预变换，直接转发原ease。

源码已恢复lazy float narrowing、显式signed-int32转换和Primary双层ease；raw callback的
参数不足返回值也从`TJS_E_INVALIDPARAM`恢复为`TJS_E_BADPARAMCOUNT`，并恢复label -> value
-> optional transition -> optional ease的转换顺序。

## 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Engine setVariable | `0x66E608` / `0x53C` | `0x559D84` / `0x1E6` | `0x1001ACDBC` / `0x2FC` | `0x1AC5F4` / `0x250` |
| Primary raw callback | `0x66F1D0` / `0x270` | `0x55A368` / `0xC4` | `0x1001AD684` / `0xE0` | `0x1ACD18` / `0x118` |
| D3D direct façade | `0x5309A8` / `0xC` | `0x494D18` / `0x8` | `0x100233150` / `0xC` | `0x231D68` / `0x8` |

Primary callback此前仍为`sub_*`。Android arm64/iOS arm64的data xref明确从
`EmotePlayer_ncb_registerMembers_guess`的member 14注册槽指向它；四端函数形状、相对
顺序、argc错误码、参数转换和Engine call完全一致。本轮统一命名为
`EmotePlayer_setVariableCompat_guess`；原始源码符号无法从strip产物证明，故保留
`_guess`。

## Engine router 的共同数据流

### HM6 find、factor 与 dirty

router先对 `unordered_map<ttstr, EmoteVarRef>`做非插入find。miss不计算factor、不写dirty，
直接通过HM7 `operator[]`取得/插入double mapped value并写原始value。

hit时顺序是：

```cpp
double factor;
if (fifth == 0.0) factor = 1.0;
else if (fifth > 0.0) factor = fifth + 1.0;
else factor = 1.0 / (1.0 - fifth);
engine.dirty = true;
switch (ref.type) { ... }
```

NaN不等于0且ordered `>0`失败，因此走reciprocal并传播NaN；±0都映射1。factor在四端
保持binary64直至实际controller call前的FCVT/VCVT。

dirty store与HM7 fallback位置：

| 目标 | dirty=1 | HM7 get-or-insert/write |
|---|---:|---:|
| Android arm64 | `0x66E710` | `0x66E73C` |
| Android armv7 | `0x559DE0` | `0x559E02` |
| iOS arm64 | `0x1001ACE2C` | `0x1001ACE58` |
| iOS armv7 | `0x1AC64C` | `0x1AC66E` |

任何HM6 hit都会先置dirty，包括type 0/1/2被directEdit门拦截、type 3/未知type、
transition/selector gate为0、mouth两label都不匹配。只有ref miss不置dirty。

### type 路由与失败边界

- type 0/1/2：`directEdit==0`时返回，不写HM7；`directEdit!=0`时落入HM7 upsert；
- type 3及未知type：返回，不落入HM7；
- type 4/5：以ref.index直接索引eye/eyebrow deque并enqueue；
- type 6：先比较mouth `label`，命中时直接写beginFrame；否则比较talkLabel，命中才
  调mouth target setter；两者相同则第一label优先；
- type 7/8：先以ref.index直接索引entry，再测试entry gate；gate为0不读取controller
  owner、不窄化三个double、不读取queuing；gate非0才调用transition/selector setter。

所有deque index均信任HM6保存的32位bit pattern，没有bounds check；owner也没有null
guard。64位端将index零扩展为uint32后参加deque arithmetic，32位端自然使用同一32位
bit pattern。Web/wasm32的size_t转换与两个32位参考一致。

### lazy double-to-float conversion

参考端不会在switch前统一生成三个float。例：

- Android armv7 type 7：gate在`0x559ECE`，通过后才在`0x559ED0/ED4/EE0`转换；
- iOS arm64 type 7：gate在`0x1001ACFC4`，通过后才在`0x1001ACFC8/FDC/FE0`转换；
- iOS armv7 type 8：gate在`0x1AC7E2`，通过后才在`0x1AC7E6/F4/F8`转换；
- mouth talk比较成功后，Android arm64才在`0x66EA74/78/7C`执行三次FCVT。

mouth beginFrame、HM7 fallback、type 0/1/2、type 3、gate false和label mismatch路径都不会
执行这些float窄化。源码现把各三个local float移动到对应call分支内，保持同一顺序。

## mouth beginFrame 的机器转换

| 目标 | 指令 |
|---|---|
| Android arm64 | `0x66EA8C  FCVTZS W9, D8` |
| Android armv7 | `0x559EB2  VCVT.S32.F64 S0, D8` |
| iOS arm64 | `0x1001ACF7C  FCVTZS W8, D8` |
| iOS armv7 | `0x1AC742  VCVT.S32.F64 S0, D8` |

共同数值profile：有限可表示值向0截断；`value >= 2^31`为`INT32_MAX`；
`value <= -2^31`为`INT32_MIN`；NaN为0。转换结果直接写mouth controller的signed
32-bit `beginFrame`，没有预先double->float、clamp脚本分支或异常。

源码新增局部 `_guess` helper显式表达该机器边界，避免WebAssembly/C++对NaN/越界
`static_cast<int>`的未定义行为。回归覆盖正负分数、NaN、±Inf和±2^31。

## Primary 与 D3D 两套 ease 管线

### Primary Motion.EmotePlayer member 14

四份raw callback共同执行：

1. `numparams < 2`返回`-1004 == TJS_E_BADPARAMCOUNT`；
2. 依次转换label、value；
3. transition缺省0；
4. callback-local factor缺省1；第四参数存在时用raw ease执行piecewise变换；
5. 把该double factor作为Engine的第五参数；
6. 释放callback-local ttstr owner并返回0。多余参数忽略。

callback-local factor初始化/Engine call：

| 目标 | default 1 | Engine call |
|---|---:|---:|
| Android arm64 | `0x66F298` | `0x66F3C0` |
| Android armv7 | `0x55A3A6` | `0x55A3F8` |
| iOS arm64 | `0x1001AD6D4` | `0x1001AD738` |
| iOS armv7 | `0x1ACD94` | `0x1ACDEE` |

Engine随后再次变换，所以常见结果为：

| script raw ease | Primary callback输出 | Engine controller power（float窄化前） |
|---:|---:|---:|
| omitted / `0` | `1` | `2` |
| 正数 `e` | `e+1` | `e+2` |
| 负数 `e` | `1/(1-e)` | `1 + 1/(1-e)` |
| NaN | NaN | NaN |

这不是把Engine/Player两个value map都写一遍；后者是已移除的旧移植错误。这里是同一个
ease数值管线的两次piecewise变换。

### D3DEmotePlayer

四个D3D body均为一跳：shell -> primary EmoteObject -> Engine，并原样转发label与三个
binary64值。它不执行Primary预变换；因此raw ease 2在Engine中只变为power 3。D3D的
generated typed adapter要求全部四个参数，而Primary raw callback允许2个必需参数与2个
可选参数。

## 完整 caller topology

四端Engine router恰有五类直接caller：

1. D3D façade；
2. timeline apply-window外部路由；
3. timeline seek外部路由；
4. Primary EmotePlayer raw callback；
5. passTimelines对非parallel/instant轨道的flush路由。

timeline frame builder已经把raw easing存为一次变换后的double；external timeline路由再
经过Engine会二次变换，而flags2普通Track直接调用内部controller，只使用builder的一次
变换。这一差异是参考数据流本身，不能把Engine helper改成“接受已计算power”的setter。

## 源码与回归

本轮源码修订：

- Engine piecewise helper恢复double返回；
- 三个float窄化移入type 4/5/6-talk/7/8的实际call分支；
- mouth第一label改用显式signed-int32 saturated toward-zero helper；
- Primary `EmotePlayer::setVariable`先执行double ease变换，D3D保持direct；
- Primary raw callback恢复bad-param-count错误码、参数转换顺序和argc覆盖slot的直接读取。

新增回归固定mouth转换、talk queue参数、Primary omitted/0 ease -> power2、Primary ease2
-> power4，以及direct Engine/D3D语义的ease2 -> power3。绝对地址仅保留在本文档和四份
recovery IDB中。
