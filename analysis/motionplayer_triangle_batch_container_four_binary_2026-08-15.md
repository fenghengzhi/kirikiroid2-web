# MotionPlayer TriangleBatch 非对称键与 vector 生命周期（四参考二进制）

日期：2026-08-15

## 1. 本轮结论

四端的 direct/D3D raw renderer 都在栈上持有同一语义的 batch accumulator。重新从当前
`reference/binaries/` 四端检查 append、flush 和 range-insert helper 后，确认旧分析中把
reference texture 列为批次键是过时结论。原版实际行为是：

1. append 的相等键依次比较 method、source texture、target texture、四个 clip 整数和
   packed color；不读取、不比较 cached reference texture；
2. reference-only 变化既不 flush，也不更新 cached reference，后续最终提交继续使用该批次
   第一次建批时留下的 reference；
3. 其他键变化时先 flush，再写 method/source/target/reference/clip；append 虽然比较 packed
   color，却不写 packed-color 字段，该字段只由 method-selection helper 更新；
4. source point range 先插入 source vector，destination range 后插入 destination vector；
5. flush 只以 destination vector 非空为 gate，提交 `destination.size()/3` 个三角形和仅一个
   `{sourceTexture, sourceBegin}` texture element；只有虚调用正常返回后才把两个 vector 的
   end 回退到 begin，capacity 与所有 cached state 都保留。

这两个不对称点都可能显得像遗漏，但四个目标的控制流与字段访问完全一致，因此 Web 端按
原始边界保留，而不是“修正”为更对称的设计。

## 2. 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| batch append | `0x6D9290` | `0x59AD20` | `0x100128AFC` | `0x127DAA` |
| batch flush | 各 transition/append 内联 | `0x59ADD8` | `0x100128C08` | `0x127E6A` |
| `vector<tTVPPointD>` range insert | `0x6D9CD4` | `0x59B5A8` | `0x10012957C` | `0x1285D4` |

recovery IDB 中四个 stripped range-insert helper 已统一恢复为
`std_vector_tTVPPointD_rangeInsert_guess`；`_guess` 明示这不是原符号名。

## 3. 批次键：reference 明确缺席

### 3.1 64 位

Android arm64 在 `0x6D92C8–0x6D9344` 比较：

- `+0` method；
- `+8` source；
- `+16` target；
- `+80/+84/+88/+92` clip；
- `+108` packed color。

字段 `+24` reference 在整个相等路径没有 load。新批路径在 `0x6D9400–0x6D940C` 写
method/source/target/reference/clip，却没有写 `+108`。

iOS arm64 的同构区间是 `0x100128B3C–0x100128BAC`；它同样从 `+16` target 直接进入
`+80` clip 比较，跳过 `+24`。`0x100128BB8–0x100128BC4` 写四个指针和 clip，不写
packed color。

### 3.2 32 位

Android armv7 在 `0x59AD3A–0x59AD4A` 比较 `+0/+4/+8` 三个指针，然后在
`0x59ADA4–0x59ADD2` 比较 `+40..+52` clip 与 `+64` packed color；`+12` reference 没有
参与。新批字段写位于 `0x59AD62–0x59AD7A`。

iOS armv7 对应区间为 `0x127DB8–0x127DD6`、`0x127E36–0x127E66`，新批字段写为
`0x127DEC–0x127E08`。它也不读取 `+12` reference，并且不写 `+64` packed color。

所以 reference-only 变化时，条件整体仍为相等：两段几何连续追加，flush 的虚调用读取的
仍是旧 `+24`/`+12`。这不是反编译器漏参数，因为四端 disassembly 的 load/compare 序列都
直接跨过了该字段。

## 4. packed color 的所有权边界

batch method transition 的 cache key 包含 packed color、blend low nibble、alphaOpAdd、
alphaTest。键变化时它先 flush，然后更新这四个字段并选择/缓存 render method。append 再
比较 packed color 是对调用顺序的防御性一致性检查，但 append 自身不取得该字段的所有权。

正常 raw renderer 总是先执行 method transition，随后才 append，因此 packed color 已匹配；
若独立直接调用 append 且传入的 color 不等于 constructor 默认值 `0xFFFFFFFF`，每次 append
都会再次判为新批：先提交旧 destination，再写除 packed color 外的 state。Web 回归测试显式
覆盖了这个边界，防止以后在 append 中“顺手同步”该字段。

## 5. 两个 vector 的内部实现与异常边界

两个容器都是 `std::vector<tTVPPointD>`；元素恰为两个 double、16 bytes，四端 helper 因其
trivial copy 性质使用 `memcpy`/`memmove`，没有逐元素构造或析构调用。batch 总是以 end 作为
插入位置，范围为空时 helper 直接返回。

Android 使用 libstdc++ 风格 `_M_range_insert`：容量不足时按
`oldSize + max(oldSize, insertedCount)` 方向增长并检查最大元素数；Android arm64 最大值为
`0x0FFFFFFFFFFFFFFF` 个 16-byte element，32 位最大值为 `0x0FFFFFFF`。重新分配时依次搬
insert-position 前缀、输入范围、旧后缀，最后 delete 旧 allocation。

iOS 使用 libc++ vector insert 路径：有余量时在原 allocation 内搬尾部和输入范围；容量不足
时通过临时 split-buffer/新 allocation 路径增长，再把新 storage 接回 vector。64/32 位的
上限同样由 16-byte element 可表示范围决定。两套 STL 的精确 capacity 增长是目标平台运行库
差异，不是 motionplayer 自有容器；共同的源代码结构仍是两个普通 `std::vector`。

append 固定先调用 source-vector insert，再调用 destination-vector insert。因此：

- source allocation 抛出时，destination 尚未触碰；
- source insert 成功而 destination allocation 抛出时，source 已经领先一个 range；没有跨
  两个 vector 的事务回滚；
- flush 只看 destination 是否为空，不以 source size 做一致性检查。

flush 的正常返回后清理点为：

| target | `OperateTriangles` | destination end 回退 | source end 回退 |
|---|---:|---:|---:|
| Android arm64 append 内联路径 | `0x6D93D4` | `0x6D93F0` | `0x6D93F4` |
| Android armv7 | `0x59AE36` | `0x59AE3C` | `0x59AE3E` |
| iOS arm64 | `0x100128C88` | `0x100128C8C–0x100128CAC` | `0x100128CB0–0x100128CD0` |
| iOS armv7 | `0x127ED8` | `0x127EDA–0x127EF4` | `0x127EF6–0x127F10` |

虚调用在所有 end 回退之前；若它抛异常，pending vertices 保留，之后再次调用 flush 会重试
同一批数据。正常 clear 只回退 end，不 delete storage，也不重置 method、texture、clip、
stencil 或 method-key 字段。

## 6. 源码与验证落地

- `MotionRenderBackend.cpp`：从 append key 删除 reference 比较；reference 仍仅在其他键触发的
  新批路径更新；删除 append 对 packed color 的赋值；保留 source-then-destination insert 和
  callback-return-then-clear 顺序。
- `motionplayer-dll.cpp`：新增 recording render-manager 回归，证明两次仅 reference 不同的
  append 合并为一次两三角形提交并使用第一 reference；独立 non-default packed-color append
  每次分批；第一次 flush 抛出后第二次会重试同一 pending range。
- 四个 recovery IDB：修正旧的“比较 reference”注释，恢复 range-insert helper 语义名，增加
  非对称键、state write、flush exception 边界注释和 bookmark，并保存数据库。

验证：单元测试翻译单元的 Emscripten 语法检查与完整 `Web Debug Build` 均通过；
`git diff --check` 通过。
