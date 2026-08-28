# Player affine / skipToSync / isExistMotion（四参考二进制，2026-08-26）

## 1. callback 映射

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| setDrawAffineTranslateMatrix | `0x6D22F4` shared tail | `0x596C40` | `0x100122D54` | `0x121D90` |
| skipToSync | `0x6D08E4` | `0x595C48` | `0x100121A78` | `0x1207F8` |
| isExistMotion | `0x6CDBD4` | `0x5942F4` | `0x10011F558` | `0x11E054` |

12 个 endpoint 均已 fresh decompile + disassemble、命名/注释并保存。
Android arm64 的 `skipToSync` 原被 IDA 并入从 `clear` 开始的巨型错误函数；本轮
按 `0x6D08E4..0x6D0CD4` 恢复独立函数并重新反编译。

## 2. setDrawAffineTranslateMatrix

API 参数顺序为 `m11, m21, m12, m22, m14, m24`，内部字段顺序为
`m11, m12, m21, m22, m14, m24`：

```cpp
m11Field = m11; m12Field = m12;
m21Field = m21; m22Field = m22;
m14Float = float(m14); m24Float = float(m24);
nonIdentity =
    m11 != 1.0 || m21 != 0.0 || m12 != 0.0 ||
    m22 != 1.0 || m14 != 0.0 || m24 != 0.0;
return nonIdentity;
```

| 端 | m11/m12/m21/m22 | float m14/m24 | flag |
|---|---|---|---:|
| Android arm64 | `+0x328/+0x330/+0x338/+0x340` | `+0x348/+0x34C` | `+0x263` |
| Android armv7 | `+0x218/+0x220/+0x228/+0x230` | `+0x238/+0x23C` | `+0x19B` |
| iOS arm64 | `+0x2B8/+0x2C0/+0x2C8/+0x2D0` | `+0x2D8/+0x2DC` | `+0x1F3` |
| iOS armv7 | `+0x1D8/+0x1E0/+0x1E8/+0x1F0` | `+0x1F8/+0x1FC` | `+0x15B` |

所有字段先写，再写/返回 flag；无 dirty、draw 或 validation side effect。identity
比较使用原始 binary64 参数而不是窄化后的 translation field：非零 double 即使
下溢为 float `0.0`，flag 仍为 true。NaN 也为 true；`-0.0 == 0.0`，因此 signed
zero identity 返回 false。typed NCB 将 bool 结果发布成 Integer 0/1，少于六参
在调用 body 前失败，额外参数忽略。

Android arm64 与 cameraOffset 类似：`0x67F2C8` 是 EmotePlayer forwarding
thunk，加载 embedded Player 后 tail-branch 到 `0x6D22F4`；Player registrar
直接进入 shared tail。IDB 用 wrapper 名、bookmark 和 line comment 保留真实
共享代码形状。

本地原先在纯 setter 后追加 motion-path 构造和 Web trace logging；四端 body
均只有存储、比较和 return，没有这些调用。本轮已删除额外 trace 副作用，并
新增“binary64 非零但 float translation 下溢为零仍返回 true”的测试。

## 3. skipToSync

### 3.1 gate 与 owner

仅在 `playing != false && loopTime < 0.0` 时进入；两项均是 ordered compare，
NaN/`-0.0` loopTime 不进入。进入后先 CopyRef persistent tag-frame Variant，
严格转为 Array owner并读取 count。该局部 owner 跨越全部遍历，所以 re-entrant
getter 清掉 persistent field 也不会销毁正在扫描的 Array。

### 3.2 dead-read traversal

按 index `0..count-1` 读取每个 tag frame。只有 `type == 1` 时继续，依次读取：

1. `time` double；
2. `content` Variant/Object；
3. `content.sync` bool。

三个值都不参与最终计算；这些读操作仍必须保留其 dispatch side effect、异常和
hint 顺序。访问 flags 均为 0，result status 由 accessor 默认转换处理。任一异常
发生在最终 state commit 前。

### 3.3 cursor transition

```cpp
initial = cachedTotalFrames;
upper = initial;
scanTags();
if (count >= 1) upper = cachedTotalFrames; // re-entrant reload

cursor = initial;
if (cursor < 0.0) cursor = 0.0;
clamped = cursor;
if (cursor > upper) clamped = upper;

queuing = true;
firstFrame = true;
frameTickCount = cursor;
clampedEvalTime = clamped;
```

| 端 | tag Variant | total/loop | raw cursor | clamped | queue/first |
|---|---:|---|---:|---:|---|
| Android arm64 | `+0x430` | `+0x468/+0x470` | `+0x460` | `+0x1C8` | `+0x1E0/+0x1E1` |
| Android armv7 | `+0x2DC` | `+0x310/+0x318` | `+0x308` | `+0x120` | `+0x138/+0x139` |
| iOS arm64 | `+0x3C0` | `+0x3F8/+0x400` | `+0x3F0` | `+0x158` | `+0x170/+0x171` |
| iOS armv7 | `+0x29C` | `+0x2CC/+0x2D4` | `+0x2C4` | `+0xE4` | `+0xFC/+0xFD` |

空 tag Array 不 reload upper；非空 Array 即使没有 type-1 frame也 reload。negative
total 得到 raw `+0.0`，但 upper 仍可令 clamped 保持 negative。NaN 通过 ordered
比较保留。四端最终独立 stores 的机器顺序不同，普通单线程源语义是一组 commit。

AArch64 把 lower clamp 编译为 `FMAX`，ARMv7 保留 `VCMPE` ordered branch；这与
此前 cursor setter 相同，是 signed-zero/NaN payload 的 machine-level platform
boundary。portable source 保留 ordered source shape，不声称跨 ISA bit-exact。

## 4. isExistMotion

共同数据流：

```cpp
Variant path = L"motion/" + stealthChara + L"/" + name;
Dispatch *rm = resourceManager.AsObjectNoAddRef();
Variant *args[2] = { &findMotionContextField, &path };
Variant result;
rm->FuncCall(0, L"isExistMotion", &functionLocalHint,
             &result, 2, args, rm);
return bool(result);
```

- path 在严格 ResourceManager Object 转换之前完成；invalid manager 因此只在
  path owner 已构造后抛。
- receiver 是 canonical resourceManager Variant 的 borrowed Dispatch，不 AddRef；
  objthis 也是同一 Dispatch。
- arg0 直接别名 persistent motion-context field，不是 CopyRef snapshot；re-entrant
  callee 可原地替换它，后续调用观察新值。arg1 是局部 path owner。
- FuncCall status 完全忽略；失败但写入 truthy result 仍返回 true，失败且未写入
  的默认 Void 返回 false。
- member hint 是该函数私有的 persistent static slot，不属于相邻进程级 hint
  family；重复调用复用同一地址。
- 不调用 onFindMotion，不查询 primary chara，而使用 live stealthChara。

既有测试覆盖 failed-status/truthy result、persistent context mutation、borrowed
receiver AddRef/Release 不变、hint identity、精确 path 和 invalid manager 顺序。

## 5. calcViewParam 状态

本轮同时完成四端入口映射和 fresh 全函数反编译：`0x6CE908 / 0x594958 /
0x1001201CC / 0x11EED4`，并在 IDB 命名为 `Player_calcViewParam_guess`。该方法在
cursor prelude 后执行 `frameProgress(0)`、`updateLayers()`，再导出每个 non-root
node 的多层 Dictionary/Array/mesh inheritance 数据，单端最多 1349 条指令。
为了不把“地址和入口已知”冒充“整个 export body 已闭合”，其完整字段顺序、
mesh ancestor owner 和每个 exception frontier 留给独立 calcViewParam 切片，
本报告不计完成覆盖项。

## 6. 验证状态

affine setter 因删除额外 trace 记为 `IMPLEMENTED`；skipToSync 与 isExistMotion
记为 `EVIDENCED_4_4`。正式 CMake/Emscripten 工具链不可用，未执行测试；
`git diff --check` 在本轮后另行执行。
