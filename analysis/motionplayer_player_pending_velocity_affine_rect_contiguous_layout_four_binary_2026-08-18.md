# Player pending stealth / camera velocity / affine / particle rect 连续布局（V252，2026-08-18）

## 1. 结论

V251 的 raw `SeparateLayerAdaptor*` 后，四份参考二进制给出相同源码声明序列：

```text
ttstr pendingStealthMotion
ttstr pendingStealthChara
double cameraVelocityX
double cameraVelocityY
double cameraVelocityZ
double drawAffineM11
double drawAffineM12
double drawAffineM21
double drawAffineM22
float  drawAffineM14
float  drawAffineM24
float  particleOutsideRect[4]
```

两个 `ttstr` 是独立 owning slots；其后的 velocity/affine/rect 是整整 80 bytes POD。包括 pending pair
后，64-bit对象中的连续区为96 bytes，32-bit对象中为88 bytes。四端 rect末端都直接触及下一个
nontrivial member，不存在 padding、`lastCanvas` Variant 或其他隐藏 owner。

V247 已从 reader/writer恢复 velocity、affine和 rect的角色，但当时 class前部仍未闭合。本轮重新从
四端 constructor、destructor、play/chara coordinators和直接边界取证，证明它们确实是 V251 raw pointer
的直接后继，再做源码物理迁移；没有把 V247 的旧字段位置直接当作前提。

## 2. 四端精确 offset

| 字段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| raw adaptor pointer | `+0x2F8` | `+0x1F4` | `+0x288` | `+0x1B4` |
| pending motion | `+0x300` | `+0x1F8` | `+0x290` | `+0x1B8` |
| pending chara | `+0x308` | `+0x1FC` | `+0x298` | `+0x1BC` |
| velocity X | `+0x310` | `+0x200` | `+0x2A0` | `+0x1C0` |
| velocity Y | `+0x318` | `+0x208` | `+0x2A8` | `+0x1C8` |
| velocity Z | `+0x320` | `+0x210` | `+0x2B0` | `+0x1D0` |
| affine m11 | `+0x328` | `+0x218` | `+0x2B8` | `+0x1D8` |
| affine m12 | `+0x330` | `+0x220` | `+0x2C0` | `+0x1E0` |
| affine m21 | `+0x338` | `+0x228` | `+0x2C8` | `+0x1E8` |
| affine m22 | `+0x340` | `+0x230` | `+0x2D0` | `+0x1F0` |
| affine m14/m24 | `+0x348/+0x34C` | `+0x238/+0x23C` | `+0x2D8/+0x2DC` | `+0x1F8/+0x1FC` |
| particleOutsideRect | `+0x350..+0x35F` | `+0x240..+0x24F` | `+0x2E0..+0x2EF` | `+0x200..+0x20F` |
| next nontrivial member | `+0x360` | `+0x250` | `+0x2F0` | `+0x210` |

64-bit `ttstr` slot为8 bytes，32-bit为4 bytes。pending pair结束与第一个 double天然对齐且无额外 gap；
三个 velocity、四个 affine linear doubles、两 float translations与四 float rect之间也全部连续。

## 3. constructor：owner 与 POD 的共同初始化

pending pair和 velocity triple 都从 null/`+0.0` 开始：

| 目标 | pending/velocity初始化 anchor | affine/rect初始化 anchor |
| --- | ---: | ---: |
| Android arm64 | `0x6CC2BC` | `0x6CC530` / rect `0x6CC510` |
| Android armv7 | `0x59369E` 的 0x20-byte clear | `0x59384E` / rect `0x593884` |
| iOS arm64 | `0x10011ECEC..0x10011ECFC` | `0x10011EEAC` / rect `0x10011EED4` |
| iOS armv7 | `0x11D654` 的两次128-bit zero store | `0x11D8E2` / rect `0x11D942` |

Android arm64/iOS armv7 的 vector stores和 Android armv7 的 0x20-byte clear会跨源码 member边界，把两个
null `ttstr` slots与三个 zero doubles合并初始化。这是 optimizer store folding，不表示 pending strings
是 POD或属于 velocity array。它们的独立 CopyRef/release与 destructor仍证明 owner身份。

affine初值在四端都是：

```text
m11 = 1.0
m12 = 0.0
m21 = 0.0
m22 = 1.0
m14 = 0.0f
m24 = 0.0f
particleOutsideRect = {0, 0, 0, 0}
```

setter参数顺序仍是 `m11,m21,m12,m22,m14,m24`，但 physical member order为
`m11,m12,m21,m22,m14,m24`。V247恢复的 damping位于 earlier frame-delta group，不属于这里。

## 4. pending motion 的 direct-member flush 与异常边界

函数映射：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Player play coordinator | `0x6AF5C8` | `0x5800EC` | `0x1001074A4` | `0x104A7C` |
| Player playImpl | `0x6AF664` | `0x580158` | `0x100107540` | `0x104AE8` |

共同逻辑：

```text
if Stealth && liveStealthChara.owner == null:
    pendingStealthMotion = CopyRef(request)
    return

playImpl(request, flags)
if pendingStealthMotion.owner != null:
    playImpl(reference-to-pendingStealthMotion-field, Stealth)
    release(pendingStealthMotion)
    pendingStealthMotion = null
```

queue是 retain-new/release-old的独立 copy assignment，连续 queue为 last-write-wins。flush没有先复制到
local、没有 move、也没有提前清 field；nested `playImpl` 整段借用持久 field本身。若 first playImpl或
nested Stealth playImpl抛异常，位于调用后的 release/null不执行，pending owner保持原值。getter也不把
pending值当作 live motion公开。

## 5. pending chara 的对称但独立状态机

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| chara coordinator | `0x6AFEC8` | `0x5805FC` | `0x100107AFC` | `0x105140` |
| live-slot writer | `0x6AFDA0` | `0x580554` | `0x100107A2C` | `0x105098` |

共同协议与 motion owner对称：Stealth且 live stealth chara为空时 CopyRef queue；否则先运行 live writer，
再把 pending field本身作为 borrowed argument以 Stealth重入 writer，正常返回后才 release/null。nested
writer异常同样保留 pending。两个 pending fields彼此独立，不形成 pair object，也不在 play与setChara
之间互相清理。

live chara/motion slots位于对象更后部；pending getter不可见、primary materialization后才 flush的行为不因
本轮物理迁移改变。

## 6. normal destruction 与 owner/POD边界

四端 destructor共同从 next nontrivial member直接跳到 pending pair：

| 目标 | later member dtor | pending chara | pending motion | workspace继续 |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6CD0AC` / `+0x360` | `0x6CD0B4` / `+0x308` | `0x6CD0C0` / `+0x300` | `0x6CD0CC` |
| Android armv7 | `0x593CE4` / `+0x250` | `0x593CEC` / `+0x1FC` | `0x593CF4` / `+0x1F8` | `0x593CFC` |
| iOS arm64 | `0x10011F378` / `+0x2F0` | `0x10011F380` / `+0x298` | `0x10011F388` / `+0x290` | `0x10011F390` |
| iOS armv7 | `0x11DDF4` / `+0x210` | `0x11DDFE` / `+0x1BC` | `0x11DE08` / `+0x1B8` | `0x11DE14` |

因此 80-byte velocity/affine/rect span没有 destructor；pending owners按 declaration reverse order先 chara、
后 motion释放，随后立即进入 V251 work/colors/primary/descriptor/ResourceManager/rootContent teardown。
constructor已把 pending slots初始化null，later construction failure只需对已经完成的 owner逆序回滚；POD
span没有 cleanup action。iOS armv7 SJLJ与其他端正常 destructor在这条边界相互印证。

## 7. portable 源码修改

`cpp/plugins/motionplayer/Player.h` 已把以下完整区块从 class后部迁到
`_renderSeparateLayerAdaptor` 后：

- `_pendingStealthMotion`、`_pendingStealthChara`；
- `_cameraVelocityX/Y/Z`；
- `_drawAffineM11/M12/M21/M22/M14/M24`；
- `_particleOutsideRect`。

原位置的重复声明已删除；注释改为 direct-member flush、连续布局与下一未闭合 nontrivial member边界。
play/chara/velocity/affine/particle算法均未改写。本轮只恢复物理声明顺序及由此决定的 constructor/destructor/
exception owner时序。

## 8. IDB 写回与 iOS armv7 安全保存

四库各写回6条 comment、6个 bookmark，共24/24。semantic renames：

- 四端 chara coordinator统一为 `Player_setCharaWithFlags_guess`；
- iOS armv7补回 `Player_play_guess`；
- stripped private identities继续保留 `_guess`。

iOS armv7 different-path保存：

- V251 canonical备份：
  `out/idb-recovery/v252-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.pre-v252.i64`；
- V252 candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v252.i64`；
- candidate经独立 `C:\IDA\idat.exe -A` probe，退出码0；
- old loose `id0/id1/nam`移入 `pre-v252-canonical-loose/`；
- candidate安装canonical后，MCP reopen读回V252 comments、`Player_play_guess`、
  `Player_setCharaWithFlags_guess`与既有`Player_playImpl_guess`；
- candidate/canonical均为376,994,000 bytes，SHA-256
  `D0CC8AA3812EADD900E8CB6BA5788AE19F889C81949A586E5F4B874DD00F6043`。

四份最终 V252 IDB：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,417,371 | `D45D227A983BD3EAA8DA49B21FE7DD0E6DF9EAB503982B994008807BD9561E21` |
| Android armv7 | 345,665,860 | `4771EBC34B075064352ADC6CAF060973CDFC5B304A19F60075B93B5B7CACB03C` |
| iOS arm64 | 334,679,922 | `8629966F0D637D223663A60931170DDB61B617BDEC21078D38576A50616CC4F8` |
| iOS armv7 | 376,994,000 | `D0CC8AA3812EADD900E8CB6BA5788AE19F889C81949A586E5F4B874DD00F6043` |

## 9. 验证与 wasm 基线

- complete motionplayer Catch2 TU ordinary/headless syntax：通过，仅既有 `_tss` warning；
- Web旧 cache自动重生成时暴露空 toolchain `/upstream/...`，已中止；按 `krkr2-build` skill dot-source
  `emsdk_env.ps1`、设置VCPKG_ROOT并以`Web Debug Config --fresh`重新配置成功；
- `win_bison --version`实测 GNU Bison 3.8.2；
- Web：fresh configure后的55-step build通过；
- Wasmtime：62-step affected build通过；
- guest：2-step build/link通过并完成 exnref转换；
- Web、Wasmtime、guest三条命令 no-work复验通过；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,354 | `0x1BD31` | `0x1A410BD` | `0x5A3E40` | `0x3185F7B` | `BDF490A88EFBFEB0EC130BD3CDBAC390CBDEAB2EA8CD9369A82F7DB36CD517B1` |
| Wasmtime `index.wasm` | 85,002,495 | `0x1BA50` | `0x19E906B` | `0x5A1090` | `0x3141E11` | `08CA45102C1F753FCC0DD1315E9CEE58716FE268142C1A643D621AB4E10FDB05` |
| Wasmtime guest | 151,478,428 | `0x1618E` | `0x13D7DE1` | `0x4D1630` | `0x1421EBA` | `1A55DCAAAEB03C05B76C6DA495D61D1A35AB705133D06A803BC3B922C0118BBB` |

相对 V251，三份产物的总大小与列出的每个 section size完全不变，仅 SHA-256改变；这与等尺寸字段
重排、owner析构调用位移和 debug layout更新一致，没有新增函数、静态数据或导出。

## 10. V253 follow-up

V253 已闭合 rect后的 ComplexRect、unknown ctor-zero dword、type3/D3D/pixelate、无Player-ctor初始化的
tag cursor/current/next、唯一MotionEvent vector、四live strings与canonical ResourceManager owner。
ComplexRect实际size仅40/32 bytes；完整offset、内部链表、event element teardown、live writer、源码迁移和
IDB/build基线见
`analysis/motionplayer_player_drawregion_tag_event_live_strings_layout_four_binary_2026-08-18.md`。
