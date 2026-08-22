# Player canonical RM / context / outline / meshline / tag Variant cluster（V254，2026-08-18）

## 1. 结论

V253 的四个 live string owners后是五个无 gap的 `tTJSVariant`：

```text
tTJSVariant resourceManager          // constructor RM的第三份独立CopyRef
tTJSVariant findMotionContext        // successful load result[1]
tTJSVariant outline                  // persistent style owner
tTJSVariant meshline                 // persistent style owner
tTJSVariant tagFrameSource           // ordinary motion["tag"]
```

constructor只CopyRef第一项；后四项各自默认构造Void。它们不是一个数组、union或shared Variant storage；
每次 assignment独立retain/release，正常析构严格逆序。portable旧声明把 context放到class尾、outline放在
frame metadata前、meshline放在completion/mask后、tag放在speed group后，无法复现owner lifetime。

## 2. 四端offset

| owner | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| canonical ResourceManager | `+0x3E0` | `+0x2AC` | `+0x370` | `+0x26C` |
| findMotionContext | `+0x3F4` | `+0x2B8` | `+0x384` | `+0x278` |
| outline | `+0x408` | `+0x2C4` | `+0x398` | `+0x284` |
| meshline | `+0x41C` | `+0x2D0` | `+0x3AC` | `+0x290` |
| tagFrameSource | `+0x430` | `+0x2DC` | `+0x3C0` | `+0x29C` |
| next byte | `+0x444` | `+0x2E8` | `+0x3D4` | `+0x2A8` |

Variant native size为64-bit 20 bytes、32-bit 12 bytes；每项恰好以前项末端为起点。最后tag的type/payload
结束后直接进入下一scalar/control group。

## 3. constructor与独立owner

cluster construction anchors：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6CC300..0x6CC320` | `0x5936E8..0x593706` | `0x10011ED34..0x10011ED4C` | `0x11D6B8..0x11D6F8` |

共同顺序：

```text
CopyRef(rm) -> canonical RM
findMotionContext = Void
outline = Void
meshline = Void
tagFrameSource = Void
```

canonical是与 V251 find-source/source-cache两项相同 dispatch的第三份独立引用；任一 earlier/later member
释放不会修改另外两槽storage。constructor中后四个只是tag/payload Void初始化，不创建Dictionary、Array
或style object。constructor failure unwind只销毁到当时已经完成的最后member。

## 4. findMotionContext incremental commit

playImpl在 non-Void load-result路径中先提交live motion labels，再从 result container index 0提交
motionContent，随后index 1才提交 context：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6AF92C` | `0x5802C4` | `0x1001076E8` | `0x104CD8` |

index 1 getter失败但不抛时，Void output仍覆盖旧context；getter/assignment抛异常时，已经提交的labels与
motionContent保留、旧context保留到assignment真正发生。context提交后的 type/property/init failure又不会
回滚新context。load失败分支清 motionContent/context并清playing，但不改live labels。

消费者不会借用一个裸dispatch pointer：findMotion/findSource、render/project、child construction等在各自
作用域CopyRef context；child member assignment产生自己的独立owner。

## 5. outline与meshline properties

访问器映射：

| property | Android arm64 get/set | Android armv7 get/set | iOS arm64 get/set | iOS armv7 get/set |
| --- | --- | --- | --- | --- |
| outline | `0x6D6B10/0x6D6B1C` | `0x598F8A/0x598F98` | `0x100125648/0x100125654` | `0x12486E/0x12487C` |
| meshline | `0x6D6B24/0x6D6B30` | `0x598FA0/0x598FAE` | `0x10012565C/0x100125668` | `0x124884/0x124892` |

getter按值CopyRef，setter执行 Variant copy assignment。Object、String、Integer、Real与Void类型全部原样
保存；不经`ttstr`、Boolean或truthiness转换。outline与meshline互不alias，任一setter失败/抛出不改另一项。
render path仅以两者type是否Void门控frame primitive，再把拥有引用的参数副本传给Layer calls。

## 6. tag owner的提交与只读getter

ordinary init的tag提交：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6B0B58` | `0x580CA8` | `0x100108310` | `0x1059EA` |

source顺序为 loopTime、lastTime、tag、priority、priority[0].content；tag assignment一旦完成，later priority/
root getter或assignment失败都不回滚。`Player.tags` getter位于
`0x6D69F8/0x598E50/0x100125544/0x124748`，只CopyRef persistent slot，不重新读取motion、不clone容器。
property没有setter。

forward/rewind/reseek另建局部tag owner越过完整遍历；回调或getter重入清Player field时，本次遍历仍由local
CopyRef保活。

## 7. normal dtor与异常回滚

normal reverse sequence：

| Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| ---: | ---: | ---: | ---: |
| `0x6CD018..0x6CD03C` | `0x593C94..0x593CB8` | `0x10011F328..0x10011F34C` | `0x11DD92..0x11DDBE` |

```text
tagFrameSource
meshline
outline
findMotionContext
canonical ResourceManager
then stealthMotion/motion/stealthChara/chara
```

constructor unwind遵循同一completed-member reverse原则。没有显式clear descriptor property、没有把style owners
强制转字符串，也没有把context/tag先move到local。外部getter返回的CopyRef可独立越过Player析构。

## 8. portable源码修改

`Player.h` 已把 `_findMotionContextVariant`、`_outline`、`_meshline`、`_tagFrameSourceVariant`按参考顺序
直接放到 canonical `_resourceManager` 后，并删除四处旧声明。注释说明incremental commit、精确 Variant type
保留、tag-before-priority与独立CopyRef；算法和NCB注册未改写。

## 9. IDB写回与安全保存

四库各写回6 comments/6 bookmarks，共24/24。iOS armv7恢复
`Player_get/setOutline_guess`、`Player_get/setMeshline_guess`、`Player_getTags_guess`五个访问器名；其余三库
已保有相同semantic names。

iOS armv7 different-path保存：

- V253 canonical备份：`out/idb-recovery/v254-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v254.i64`；
- candidate：同目录`Kirikiroid2_1.3.9_iOS_armv7.v254.i64`；
- independent `C:\IDA\idat.exe -A` probe退出0；old loose files移入`pre-v254-canonical-loose/`；
- canonical替换后MCP回读五个访问器名与constructor comment；
- candidate/canonical均377,411,792 bytes，SHA-256
  `92DB6DE68F589736AD136540BC04D1654454FE0B87CC0B0033197B3968A8F93A`。

最终IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,548,443 | `4CCE213039EED25C6772D63A1FE944500E7200FD5FB1F5CBBF261719F24EB49A` |
| Android armv7 | 345,780,548 | `E8D7EC6B3369C28CC6BF3721AAD47BCE0BF3667AF66B9A9DEFD993D3EAE0EA1A` |
| iOS arm64 | 334,778,226 | `5DD8E7B430D367156DA9250E019B1195DB9F6538191A77629BB94C97EA1C26B6` |
| iOS armv7 | 377,411,792 | `92DB6DE68F589736AD136540BC04D1654454FE0B87CC0B0033197B3968A8F93A` |

## 10. 验证与wasm基线

- ordinary/headless complete motionplayer syntax：通过，仅既有`_tss`warning；
- Web 33-step、Wasmtime 62-step、guest 2-step/exnref：通过；
- 三条no-work复验：通过；IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web | 85,655,362 | `0x1BD31` | `0x1A410C5` | `0x5A3E40` | `0x3185F7B` | `8F14A28A8BC0F84B0C0D43877B8C2B04DE547E1ADE54CD52F3014E240875CA68` |
| Wasmtime | 85,002,503 | `0x1BA50` | `0x19E9073` | `0x5A1090` | `0x3141E11` | `7E8F0F63B0018F66CC5713E126F2A770A9B64E5F688B418E48C4E8E0B992F66F` |
| guest | 151,478,451 | `0x1618E` | `0x13D7DF9` | `0x4D1630` | `0x1421EBA` | `9606E44DABDB40A26D150AE36B291B50079F05A0FA05A6E42119519429A6E7EE` |

相对V253，两主模块总大小和列出section size不变、仅hash变化；guest CODE不变，总大小减少36 bytes，来自
debug metadata。

## 11. 下一边界

V255 从tag owner结束后的直接scalar/control group开始：四端起点
`+0x444/+0x2E8/+0x3D4/+0x2A8`。优先fresh恢复 sync/camera/preview/prior/playing等byte与相邻double/int的真实
声明顺序，再处理后继container边界；不能沿用当前class后部按属性主题分组的顺序。
