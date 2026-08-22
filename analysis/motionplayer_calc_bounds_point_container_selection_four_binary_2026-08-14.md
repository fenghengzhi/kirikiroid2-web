# `calcBounds` 点容器选择与固定 16 点扫描四参考恢复（2026-08-14）

## 1. 结论

普通可见 node 的 AABB 输入在四端都按严格优先级选择：

1. `compositeMeshPoints` vector 非空：按 `[begin,end)` 扫描全部 `MeshPoint`；
2. 否则 `transformedMeshControlPoints` vector 非空：只以 `begin != end` 为 gate，随后从
   `begin` 无条件读取恰好 16 个 `MeshPoint`；
3. 两个 vector 都空：扫描 node 内联四角的恰好 4 个 `MeshPoint`。

当前源码的 `if composite / else if transformed / else corners` 及循环形状已经匹配；本纵切面
补齐两个连续 vector 的四端字段 ABI、fixed-16 越界/忽略尾部边界、确定性回归和 IDB 注释。

## 2. 字段 ABI

`MeshPoint` 四端均为两个连续 binary32，元素宽 8 字节。两个 vector 在 node 内连续排列，
每个 record 是平台标准 `{begin,end,capacityEnd}`：64 位 24 字节，32 位 12 字节。

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| composite `{begin,end,cap}` | `node+2048/+2056/+2064` | `node+1752/+1756/+1760` | `node+2064/+2072/+2080` | `node+1716/+1720/+1724` |
| transformed `{begin,end,cap}` | `node+2072/+2080/+2088` | `node+1764/+1768/+1772` | `node+2088/+2096/+2104` | `node+1728/+1732/+1736` |
| 内联四角首点 | `node+1856` | `node+1616` | `node+1872` | `node+1584` |
| node float AABB | `node+1888` | `node+1648` | `node+1904` | `node+1612` |

这些偏移解释了为什么不能把 `transformedMeshControlPoints` 当作 composite vector 的 alias，
也不能把其中一个简化为固定 `std::array`：两者各自拥有独立 vector allocation/lifetime，
只是 bounds consumer 对第二个 vector 故意忽略实际 size。

## 3. 四端控制流地址

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| source/type admission | `0x6C1480..0x6C1494` | `0x58BFFE..0x58C01A` | `0x100115D7C..0x100115DA0` | `0x11377E..0x11379A` |
| AABB 写 `±FLT_MAX` | `0x6C14A8` | `0x58C03E..0x58C050` | `0x100115DB0..0x100115DBC` | `0x1137AE` |
| composite begin/end 与 empty gate | `0x6C149C..0x6C14AC` | `0x58C022..0x58C054` | `0x100115DC0..0x100115DCC` | `0x1137B2..0x1137BC` |
| composite range 扫描 | `0x6C1660..0x6C16F4` | `0x58C14A..0x58C1B8` | `0x100115EB8..0x100115F38` | `0x1138C0..0x11392E` |
| transformed begin/end empty gate | `0x6C14B4..0x6C14BC` | `0x58C056..0x58C060` | `0x100115DD0..0x100115DE0` | `0x1137C0..0x1137CA` |
| transformed 固定 128B/16 点扫描 | `0x6C1700..0x6C1784` | `0x58C1C4..0x58C22C` | `0x100115F40..0x100115FA8` | `0x113938..0x1139A0` |
| 内联固定 32B/4 点扫描 | `0x6C14C0..0x6C15B8` | `0x58C064..0x58C0D6` | `0x100115DE4..0x100115E60` | `0x1137CE..0x113840` |
| `floorf/floorf/ceilf/ceilf` 发布 | `0x6C123C..0x6C1258` | `0x58C234..0x58C284` | `0x100115FAC..0x100115FC8` | `0x1139AC..0x1139EE` |

优化器会把 4 角 path 展开、把 transformed loop 写成固定 byte-offset loop，而 composite
path 才从 `(end-begin)/8` 推导真实元素数；四端结构仍完全一致。

## 4. 容器与边界行为

- composite vector 的 `capacityEnd` 不参与扫描，只读取 begin/end；
- composite 非空时完全不读取 transformed begin/end 和四角；
- transformed path 只用 `begin != end` 选择分支，不计算 `(end-begin)/8`；
- transformed size 大于 16 时，索引 16 及以后全部忽略；
- transformed size 为 1..15 时，循环越过逻辑 end 继续读取到 `begin+128`，是 allocator
  capacity/unmapped memory 决定结果或崩溃的 UB 边界；没有 size assert、填充或 fallback；
- transformed `size==0` 即便保留非空 capacity，标准 vector 的 begin==end 仍选择四角；
- composite begin/end 损坏成 end<begin 时，四端的无符号/指针差循环会产生巨大 count，属于
  损坏容器 UB，不做 ordered 检查；
- 每点四个 `<=,<=,>=,>=` 比较保持 NaN 忽略与 signed-zero 后写；完成后才统一
  `floorf,floorf,ceilf,ceilf`。

## 5. 回归与验证

新增两组安全的确定性回归：

- composite、transformed(17 点)和四角同时存在，期望只由三个 composite 点产生
  `{-3,6,10,21}`；
- composite 为空、transformed 含 17 点，前 16 点产生 `{0,85,16,101}`，第 17 个极端
  `{-1000,2000}` 必须被忽略，四角也不得参与。

不执行 size<16 的真实越界用例；该行为由四端固定 128-byte loop 记录，而不是在单测中主动
触发宿主 UB。

验证结果：

- 完整 motionplayer 单测 TU 使用真实 Emscripten response file 执行
  `-fsyntax-only`：通过；仅有仓库既有 `_tss` literal-operator 弃用警告；
- 当前源码状态执行 `cmake --build out/web/debug --parallel 8`：通过，motionplayer 重建并
  成功链接最终 `index.html`；本纵切面只增加测试/文档，不再改变已通过构建的生产源码；
- 对新增测试、bounds 主文档、纵切面文档和计划执行 `git diff --check`：通过；仅有工作树
  既有 LF/CRLF 转换提示；
- 四份 recovery IDB 的两层 vector gate、三条扫描路径和 fixed-16 边界注释均已原位保存。
