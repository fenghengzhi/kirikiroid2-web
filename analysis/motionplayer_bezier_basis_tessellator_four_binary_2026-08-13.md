# Motionplayer Bezier basis 与 patch tessellator 四参考恢复

日期：2026-08-13

本记录只采用 `reference/binaries/` 中四个参考目标的新反编译、反汇编与交叉引用证据。
旧 `libkrkr2.so` 注释和旧单目标地址不作为结论来源。

## 四端映射

| 语义名（恢复） | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `BezierBasis_getCubicTable_guess` | `0x69DE30` | `0x576C7C` | `0x1000FB4A8` | `0xF854C` |
| `PrivateMotionGLL_tessellateBezierPatch_guess` | `0x6D9138` | `0x59ABC8` | `0x1001289AC` | `0x127C6C` |

后一个 helper 的命名沿用已有恢复名，但交叉引用表明它不是 PrivateMotionGLL 独占：Android
arm64 的 Player D3D 提交和 PrivateMotionGLL Draw_GPU 都调用同一个函数。因此本地实现放在共享
`MotionRenderBackend`，而不是任何一个类的私有实现中。

## Basis cache 的结构与生命周期

四端共同实现为：

```cpp
static std::map<int, std::vector<std::vector<double>>> cache;

const auto found = cache.find(division);
if(found != cache.end()) return found->second;
auto &table = cache[division];
table.resize(size_t(sign_extend(division) + 1));
// division >= 0 时再填表
```

缓存对象具有进程/插件静态生命周期。命中后直接返回已有 table，不验证内容也不重新生成；函数中
看不到 mutex 或其他并发保护。`operator[]` 插入发生在 `resize` 之前，因而异常会改变后续调用的
可观察状态：

- `division == -1`：`resize(0)` 成功，空 table 永久进入 cache；
- `division <= -2`：首次调用会以极大的无符号 size 调 `resize`，通常抛出
  `length_error`/`bad_alloc`，但 map 中已经留下该 key 的空 table；
- 同一个 `<= -2` key 的第二次调用直接命中并返回空 table，不再执行失败的 `resize`；
- `division == 0`：table 长度为一，仍执行 `double(0) / double(0)`，四项系数都是 NaN；
- 正数：table 长度恰为 `division + 1`，下标为 `0..division`。

这否定了旧本地实现中的“负 division 提前返回”和“division 为零时令 t=0”保护。保护虽更友好，
却会改变异常、缓存状态和 NaN 传播。

## 系数的逐指令结合顺序

对每个 `i`：

```cpp
double t = double(i) / double(division);
double m = 1.0 - t;
double m2 = m * m;
row.push_back(m * (m * m));
row.push_back((t * m2) * 3.0);
row.push_back((t * (t * m)) * 3.0);
row.push_back(t * (t * t));
```

arm64 与 armv7 的 `FMUL`/`VMUL` 序列均支持上述分组；没有 fast-path，也没有以一个通用
`pow` 或两阶段 cubic blend 代替。恢复源保留分组，是为了让有限精度舍入尽量与参考目标一致。

## Tessellator 数据流

调用顺序固定为先取 X basis，再取 Y basis；即使 division 为负，也会先触发两个缓存查询/插入，
然后才由循环条件决定是否输出点。

对 `y = 0..divisionY`、`x = 0..divisionX` 的每个输出点：

```cpp
double outX = 0.0;
double outY = 0.0;
for(int i = 0; i != 16; ++i) {
    double weight = basisY[y][i / 4] * basisX[x][i % 4];
    outX = outX + weight * controlPoints[i].x;
    outY = outY + weight * controlPoints[i].y;
}
result.push_back({outX, outY});
```

关键边界：

- 始终读取恰好 16 个控制点，没有 `size() >= 16` 检查；不足时自然发生越界访问；
- 输出为 Y 外层、X 内层的 row-major；
- result 从空 vector 逐点 `push_back`，原生函数没有预先 `reserve`；
- 不以“先沿 U 混合四条曲线、再沿 V 混合一次”的数学等价形式计算，因为那会改变浮点结合顺序；
- 任一零 division 对应的 basis 含 NaN，因此参与的输出坐标自然传播 NaN；
- 负 division 的异常和 cache 副作用发生在输出循环判断之前。

## 共享范围的额外交叉引用

Android arm64 中 basis helper 除 tessellator 的两次调用外，还有三个内部消费者：

- `0x69D7B0` 固定请求 division 3，以四行 cubic 权重生成传给脚本 `drawBeziers` 的点列；
- `0x69E9F8` 分别请求 X/Y division，执行另一条 16 控制点 patch 构建路径；
- `0x6A0210` 固定请求 division 3，为两组 `drawBeziers` 调用生成曲线点。

因此 cache 是插件级共享基础设施。上述三个消费者现已在后续四参考纵切面中完整闭合：固定
division-3 debug helper 生成两组各四条、每条四点的 `drawBeziers`；parser/tessellator 直接读取
32 坐标；公开 `drawBezierPatchFrame` 则保留每条仅三点和两端 reverse 的历史行为。八个此前
完全缺失的 `BezierPatch` Layer API、cache/default-points 静态顺序与退出析构见
`motionplayer_bezier_patch_layer_api_four_binary_2026-08-14.md`。

## 本地实现变化

- 在 `MotionRenderBackend` 增加共享 `cubicBezierBasisTable_guess` 和
  `tessellateBezierPatch_guess`；
- 移除 Player 与 PrivateMotionGLL 中两份重复的两阶段 cubic blend；
- 两条调用链都先以原生 float 加法得到 offset 后的 `vector<tTVPPointD>` 控制点，再交给共享
  tessellator；
- 保留静态 map、插入先于 resize、NaN、异常后空 cache entry、固定 16 点读取和无 reserve 行为；
- 将 unit quad、basis map 与 default points 按四端初始化 bundle 的真实顺序定义在同一个
  translation unit，使 binding state→default points→basis map 的逆退出顺序可控；
- 四个 recovery IDB 均已给 basis helper 补语义名，给 basis/tessellator 写入行为注释并保存。

## 验证

- Web Debug 完整重新编译和最终链接成功；
- `git diff --check` 对相关文件通过，仅报告工作树既有的 LF/CRLF 转换提示。

## 后续纵切面已闭合与仍不确定项

- 三个额外 basis 消费者、八个 `BezierPatch` Layer API、静态 cache/default-points 的初始化与
  析构顺序均已由 2026-08-14 后续记录闭合；
- 仍不能把极端分配失败在各平台 libc++/libstdc++ 中具体抛出的异常类别写死；共同证据只能确认异常可从
  `vector::resize` 传播，以及 map entry 已先插入。
