# MotionPlayer TransformOrder / Player HM2 旧单端注释迁移（2026-08-15）

## 1. 范围与结果

本轮针对 `Player.h` / `PlayerVariable.cpp` 中最后几条直接绑定旧 Android
`libkrkr2.so` 地址或 `Player+320` 单端偏移的编译源码注释，重新从
`reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、iOS armv7
四份当前参考取证。

结果是两处源码类型和行为都已正确，不需要改变运行逻辑：

- `TransformOrderFlip/Angle/Zoom/Slant = 0/1/2/3` 是 operation ID；Motion 根
  registrar 的 script 发布顺序则是 Flip(0)、Slant(3)、Zoom(2)、Angle(1)；
- Player HM2 是 `unordered_map<ttstr,double,ttstr_hash,ttstr_equal>`，binder 对 raw
  label 无条件写入；旧单端 `+320` 只属于 Android arm64，不能继续出现在共同源码
  注释里。

需要纠正的是证据身份与容器描述：旧注释给出的单一 helper 地址不能代表当前四端，
且该 operator[] specialization 并非 Player HM2 私有，它还被 Engine scalar map 与
Player join variable snapshot 等同型 `LabelValueMap` 复用。

## 2. TransformOrder 的四端注册证据

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Motion_ncb_register_guess` | `0x6D6EE8` | `0x5991D0` | `0x100125974` | `0x124B7C` |
| Flip(0) publication | `0x6D7074` | `0x5992D0` | `0x100125AFC` | `0x124CDC` |
| Slant(3) publication | `0x6D708C` | `0x5992E0` | `0x100125B14` | `0x124CF2` |
| Zoom(2) publication | `0x6D70A4` | `0x5992F0` | `0x100125B2C` | `0x124D08` |
| Angle(1) publication | `0x6D70BC` | `0x599300` | `0x100125B44` | `0x124D1E` |

四个根 registrar 都先发布 15 个 Layer/Shape/PlayFlag 常量，再连续发布这四个
TransformOrder 常量，之后才是 Coordinate 与 MaskMode 常量。字符串 xref 也全部回到
同一根 registrar：

| script string | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `TransformOrderFlip` | `0x14D68C2` | `0xD86148` | `0x10195D1BE` | `0x174F522` |
| `TransformOrderSlant` | `0x14D68E8` | `0xD8616E` | `0x10195D1E4` | `0x174F548` |
| `TransformOrderZoom` | `0x14D6910` | `0xD86196` | `0x10195D20C` | `0x174F570` |
| `TransformOrderAngle` | `0x14D6936` | `0xD861BC` | `0x10195D232` | `0x174F596` |

因此 enum declaration 按 operation ID 排列为 Flip、Angle、Zoom、Slant 并不与注册
顺序冲突。`main.cpp` 的四条 `Variant` publication 保留 Flip、Slant、Zoom、Angle 的
native script ABI 顺序。旧 `Motion_namespace_ncb_register` 单端地址注释已改为这条共同
语义，不把任何绝对地址写回编译源码。

完整 Motion 根 namespace 的 23 常量、11 subclass、两 method 与 dummy constructor
生命周期见
`analysis/motionplayer_motion_root_ncb_surface_lifecycle_four_binary_2026-08-14.md`。

## 3. Player HM2 的字段位置与写入点

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_bindParameterValue_guess` | `0x6C1A48` | `0x58C4D8` | `0x100116410` | `0x113D54` |
| HM2 operator[] call/store | `0x6C1FEC..0x6C1FF4` | `0x58C6F0..0x58C6F4` | `0x1001166C0..0x1001166C4` | `0x113FF4..0x113FF8` |
| Player HM2 offset | `+320` | `+220` | `+248` | `+180` |
| map header size | `56` | `28` | `40` | `20` |
| STL family | old libstdc++ | old libstdc++ | libc++ | libc++ |

四端 binder 的共同尾部是：

```text
if split(rawKey, scope, suffix):
    update HM1 cascade entry and matching descendant ramps

HM2[rawKey] = value
apply matching ramps on this Player by rawKey
```

HM2 写入不要求 split 成功，不过滤空 ttstr，也不把 key 窄化为 `std::string`。这与
`Player_readBoundParameterValue_guess` 的路由配对：可 split label 读 HM1 joined key，
不可 split label 以原始 ttstr 查 HM2。

`+320` 只是 Android arm64 的物理成员偏移。Android armv7 因指针宽度及旧
libstdc++ header 缩成 `+220`；iOS 两端使用 libc++，分别为 `+248/+180`。源码只能
表达成员和容器语义，不能在共同注释里继续把单端偏移当作类定义。

## 4. 共享 LabelValueMap operator[] 与 node ABI

| 项目 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `LabelValueMap_getOrInsertMapped_guess` | `0x683D24` | `0x56559C` | `0x10010BD28` | `0x1096A4` |
| node size | `32` | `32` | `32` | `20` |
| mapped double offset | `+16` | `+16` | `+24` | `+12` |

节点布局：

```text
Android arm64 old libstdc++:
  next@0, key@8, double@16, cachedHash@24                    size 32

Android armv7 old libstdc++:
  next@0, alignment pad@4, key@8, double@16, cachedHash@24  size 32

iOS arm64 libc++:
  next@0, cachedHash@8, key@16, double@24                   size 32

iOS armv7 libc++:
  next@0, cachedHash@4, key@8, double@12                    size 20
```

四端 operator[] 的共同对象生命周期为：

1. 从 ttstr backing 读取或建立 cached UTF-16 hash；null backing 的 hash 为 0；
2. 在当前 bucket 查 hash 与 ttstr equality；
3. hit 直接返回 mapped double 地址，不 relink、不复制 key；
4. miss 分配一个 STL-specific node，CopyRef key，value-initialize double 为 `+0.0`；
5. insert/rehash 后返回 mapped 地址，binder 再写实际 value。

Android arm64 内联展开可见完整 cached hash 算法：逐 UTF-16 code unit 使用
`(1025*x) ^ ((1025*x)>>6)` 累积，再乘 9，最后做
`32769*(h^(h>>11))`，非 null key 得到零时缓存 `UINT32_MAX` sentinel。其他三端调用
同一 `tTJSHashFunc_ttstr_Make` 语义。源码的 `ttstr_hash` / `ttstr_equal` 因而正确。

fresh xref 证明这个 helper 同时服务：

- `Player_bindParameterValue_guess` 的 HM2；
- `Player_resetMotionState_guess` 写 join variable snapshot 时的另一个 Player
  `LabelValueMap`；
- 多个 `EmoteEngine` HM7 scalar writer。

所以旧注释中的 `Player_HM2_upsert_labelToValue` 私有身份不成立。正确恢复应以共享
specialization 命名，并由 caller 的 receiver offset 决定具体是哪一张 map。这个原则也
避免把 `Player_resetMotionState_guess` 对 HM4 snapshot 的写入误认成 HM2 clear/upsert。

## 5. 本地迁移

本轮只修改编译源码注释：

- `Player.h` 的 TransformOrder 注释改为四端共同的 ID 与 publication order；
- 删除 `_variableValues` 说明里的 `Player+320` 单端身份；
- HM2 注释改写为 raw ttstr -> double、binder 尾写、共享 LabelValueMap specialization、
  cached hash 与 miss CopyRef/value-init 生命周期；
- `PlayerVariable.cpp` 的 HM2 lookup 注释去掉单端偏移，保留“不窄化原始 label”的
  共同语义。

没有改变 enum 值、注册顺序、容器类型或运行时数据流。绝对地址与平台布局只保留在
本文和恢复 IDB 中。
