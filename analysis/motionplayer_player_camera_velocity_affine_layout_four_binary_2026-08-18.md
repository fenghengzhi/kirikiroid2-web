# Player camera velocity、damping 与 draw-affine 相邻布局（V247，2026-08-18）

## 1. 结论

V246 在四份参考的 `Player` constructor/destructor 中把 draw-affine 前方 24-byte 区缩小到
“只构造清零、析构跨过”的 POD。V247 继续追踪这个区域的全部 reader/writer 后，可以把它精确恢复为
三个持久 `double`：`cameraVelocityX/Y/Z`。它不是 Variant、owner、临时 scratch 或 padding。

四份参考给出同一份源码声明顺序：

1. `frameDelta` 后紧邻 `cameraDamping`；
2. damping 后是四个连续控制 byte：`noUpdateYet`、`reverseSeekFlag`、
   `cameraConstraintDirty`、`drawAffineMatrixNonIdentity`；
3. 更靠后的两个 pending stealth `ttstr` 后紧邻三个 camera velocity `double`；
4. velocity Z 后紧邻六个 draw-affine scalar；
5. affine 的两个 float translation 后紧邻四 float `particleOutsideRect`。

因此旧本地布局中两个相邻关系都被四端共同否定：damping 不是 velocity 的第四个成员，
non-identity flag 也不位于 affine/particle rect 后。`firstFrame` 与 `motionCompleted` 则属于更早的
`queuing/firstFrame/directEdit/motionCompleted` byte group，不能挪到 damping 一带。

## 2. 四端字段偏移矩阵

偏移均相对完整 native `Player` object；十六进制和十进制是同一数值的两种表示。

| 字段/连续组 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| frame delta | `+0x250` / 592 | `+0x188` / 392 | `+0x1E0` / 480 | `+0x148` / 328 |
| camera damping | `+0x258` / 600 | `+0x190` / 400 | `+0x1E8` / 488 | `+0x150` / 336 |
| noUpdateYet | `+0x260` / 608 | `+0x198` / 408 | `+0x1F0` / 496 | `+0x158` / 344 |
| reverseSeekFlag | `+0x261` / 609 | `+0x199` / 409 | `+0x1F1` / 497 | `+0x159` / 345 |
| cameraConstraintDirty | `+0x262` / 610 | `+0x19A` / 410 | `+0x1F2` / 498 | `+0x15A` / 346 |
| drawAffineMatrixNonIdentity | `+0x263` / 611 | `+0x19B` / 411 | `+0x1F3` / 499 | `+0x15B` / 347 |
| pending stealth motion | `+0x300` / 768 | `+0x1F8` / 504 | `+0x290` / 656 | `+0x1B8` / 440 |
| pending stealth chara | `+0x308` / 776 | `+0x1FC` / 508 | `+0x298` / 664 | `+0x1BC` / 444 |
| velocity X/Y/Z | `+0x310/+0x318/+0x320` | `+0x200/+0x208/+0x210` | `+0x2A0/+0x2A8/+0x2B0` | `+0x1C0/+0x1C8/+0x1D0` |
| affine block | `+0x328` | `+0x218` | `+0x2B8` | `+0x1D8` |
| particleOutsideRect | `+0x350..+0x35F` | `+0x240..+0x24F` | `+0x2E0..+0x2EF` | `+0x200..+0x20F` |

64 位 `ttstr` 是 8-byte owner，32 位是 4-byte owner，所以 pending pair 到 velocity 的邻接在四端都
没有额外语义字段。affine block 的共同内部布局为：

```text
double m11
double m12
double m21
double m22
float  m14
float  m24
float  particleOutsideRect[4]
```

这与六参数 setter 的调用参数顺序不同：setter 接收 `m11,m21,m12,m22,m14,m24`，再按上面的
native member order 写入。

## 3. constructor：初始化身份和相邻顺序

| 目标 | `Player` ctor | velocity 零初始化 | damping=1 | control-byte 证据 |
| --- | ---: | ---: | ---: | ---: |
| Android arm64 | `0x6CC110` | `0x6CC4E4..0x6CC4E8` | `0x6CC4F0` | `0x6CC478/0x6CC4B4/0x6CC53C` |
| Android armv7 | `0x5935C4` | `0x5937F6..0x5937FE` | `0x593822` | `0x5937AE/0x5937E4/0x593862` |
| iOS arm64 | `0x10011EC04` | `0x10011ECF4`（另有同值合并写） | `0x10011EE74` | `0x10011EE00/0x10011EE3C/0x10011EEB8` |
| iOS armv7 | `0x11D488` | `0x11D84C..0x11D858` | `0x11D878..0x11D87C` | `0x11D7BA/0x11D824/0x11D90A` |

编译器会跨源码声明顺序合并零写、常量写或 store pair，因此单看机器指令时间顺序不足以恢复成员
声明顺序；这里使用的是四端一致的 object offset、所有成员 reader/writer、owner 析构边界和 ABI
尺寸共同约束。三个 velocity 初值都是精确 `+0.0`，damping 初值是 binary64 `1.0`。

destructor 在 pending `ttstr` 和后续 owner 之间跨过整个 velocity/affine/rect POD 区，没有任何
Variant/dispatch/string release。这继续确认 V246 对 `_lastCanvas` 的否定，也把那 24 bytes 从
“未知 POD”收敛成 velocity triple。

## 4. updateLayers reader：先积分，后持久衰减

| 目标 | updateLayers | 首个 velocity load | damping compare |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6B871C` | `0x6B8740` | `0x6B87C0` |
| Android armv7 | `0x5856E0` | `0x5856F4` | `0x585784` |
| iOS arm64 | `0x10010E544` | `0x10010E598` | `0x10010E628` |
| iOS armv7 | `0x10BE5C` | `0x10BE98` | `0x10BF3A` |

共同逻辑是：

```cpp
if (velocityX != 0.0) {
    root.delta.dirty = true;
    root.delta.posX += frameDelta * velocityX;
}
if (velocityY != 0.0) {
    root.delta.dirty = true;
    root.delta.posY += frameDelta * velocityY;
}
if (velocityZ != 0.0) {
    root.delta.dirty = true;
    root.delta.posZ += frameDelta * velocityZ;
}

if (cameraDamping != 1.0) {
    const double factor = pow(cameraDamping, frameDelta / 60.0);
    velocityX *= factor;
    velocityY *= factor;
    velocityZ *= factor;
}
```

这里的 writer-back 直接落回同三个 pre-affine member。若把 24-byte 区解释为 transient scratch，
下一帧 reader、particle child writer 和 damping 后的三次原位乘法将无法同时成立。

## 5. particle child writer：velocity 与 damping 的最终 commit

| 目标 | particle-system function | velocity transform/commit | damping commit |
| --- | ---: | ---: | ---: |
| Android arm64 | `0x6BC4BC` | `0x6BD508` 附近 | `0x6BD594` |
| Android armv7 | `0x588A48` | `0x5893FE` 附近 | `0x58948A` |
| iOS arm64 | `0x100111D08` | `0x100112C20` 附近 | `0x100112CD0` |
| iOS armv7 | `0x10F51C` | `0x1105AA` 附近 | `0x11065C` |

type-4 particle child 的共同写入协议是：

1. 计算并写 child velocity X/Y/Z；
2. 依据 coordinate/zoom path 对 child velocity 做旋转、缩放或 delta/dt 累加；
3. 所有 velocity 调整后，最后把 parent particle node 的 `particleAccelRatio` 原样写入 child
   camera damping。

没有 finite、正数或 `[0,1]` gate。现有 zero-zoom differential case 会自然产生 X infinity、Y/Z
NaN；V247 将 `particleAccelRatio` 设为 `-0.25` 并断言 child damping 也是 `-0.25`，从运行时隔离出
最后那次 damping commit，防止实现把它误并入 velocity 临时量或默认 1.0。

## 6. 四字节 control group

non-identity flag 的真实地址由六参数 affine setter 的每次无条件 byte store、draw path 的 test、以及
constructor clear 共同锁定。它是 damping 后第四个 byte，而不是 affine/rect 后的尾 byte。

相邻三个 byte 也各自拥有独立生命周期：

- `noUpdateYet`：选择首轮 update derivative 分支；
- `reverseSeekFlag`：普通 first-frame reseek 路径消费；
- `cameraConstraintDirty`：跨帧约束强制 dirty latch，帧后清除；
- `drawAffineMatrixNonIdentity`：每次 setter 都从六参数与 identity 比较重新计算，不是 sticky flag。

`firstFrame` 与 `motionCompleted` 的 constructor stores、frameProgress reader/writer 则与
`queuing/directEdit` 形成更早的四 byte declaration group。将它们放在 damping 后虽可能维持 portable
行为，却不能一比一反映共同 native class layout。

## 7. portable 源码修正

`cpp/plugins/motionplayer/Player.h`：

- `_cameraDamping` 移到 `_deltaTime` 后；
- damping 后恢复四个 control byte 的共同顺序；
- `_firstFrame/_motionCompleted` 移回 `_queuing/_directEdit` 组；
- pending stealth string pair 后声明 `_cameraVelocityX/Y/Z`；
- velocity 后依次声明 draw-affine six scalars 与 `_particleOutsideRect`；
- 删除原先位于 affine/rect 后的 non-identity flag 声明；
- 增加只供 differential case 读取 damping 的 `_guess` getter，不增加任何脚本成员。

`tests/unit-tests/plugins/motionplayer-dll.cpp` 扩充既有 particle zero-zoom case，验证
velocity 的 infinity/NaN 边界与负 damping commit 可以同时出现。

算法主体没有为迎合布局而重写；`updateLayers` 和 particle path 已经执行与四端一致的 reader/writer
顺序，本轮主要消除过时字段排列及其误导性注释。

## 8. IDB 写回与 iOS armv7 安全保存

四份恢复 IDB 各写入 9 条 line/function comment 和 4 个 bookmark：constructor 的 velocity/damping/
control group、updateLayers 的 integration/damping、particle child 的 velocity/damping commit。iOS armv7
另将两个此前仍为匿名的函数恢复为 `Player_updateLayers_guess` 与
`Player_updateParticleSystems_guess`；stripped 私有身份保留 `_guess`。

总计写回 36 comments、16 bookmarks、2 semantic renames。Android arm64、Android armv7、iOS arm64
直接保存并关闭。iOS armv7 继续使用 different-path packed save：

- 第一阶段 candidate：`out/idb-recovery/v247-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.v247.i64`；
- 最终 candidate：同目录 `Kirikiroid2_1.3.9_iOS_armv7.v247-final.i64`；
- 两份 candidate 都是 376,699,088 bytes，最终 candidate 经独立 `idat.exe -A` probe 退出码 0；
- 旧 V246 canonical/loose files 保存在 `pre-v247-canonical/`；
- 中间 V247 canonical 及 probe 生成的 loose files 保存在 `pre-v247-field-final/`；
- 最终 candidate 复制到 canonical 后由 MCP 重新打开，两个 semantic names 均成功读回；
- canonical 与最终 candidate SHA-256 同为
  `D46075759C2BA6C7867D85B8A1A6EFB2E2D9A191DDDA781261504BC033A6B95C`；
- 最终 IDA session count 为 0。

四份 V247 canonical IDB 基线：

| IDB | size | SHA-256 |
| --- | ---: | --- |
| Android arm64 | 366,656,687 | `BD10FBDE6F8FFDAA3071F12DA7E78D8F834B2A403EFBB2CE64171AA54DE19C4C` |
| Android armv7 | 345,617,955 | `9DC85AD177B2DEC4F5625DD113CEC6792F0F04BAB87E242FA7E7D9FD252F8CF0` |
| iOS arm64 | 334,599,425 | `0EEB245678C98403619EB1EAE18702B47D9C0643FE35BA0D8BB94DC942F2CA4D` |
| iOS armv7 | 376,699,088 | `D46075759C2BA6C7867D85B8A1A6EFB2E2D9A191DDDA781261504BC033A6B95C` |

## 9. 验证与 wasm 产物

- complete motionplayer Catch2 TU ordinary/headless syntax：通过；两次均仅有既有 `_tss` warning；
- `cmake --preset "Web Debug Config"`：通过，并确认正确 Emscripten toolchain；
- `cmake --build out/web/debug`：54-step full rebuild 通过；
- `cmake --build out/wasmtime/debug`：62-step rebuild 通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`：通过并完成 exnref 转换；
- 三条 build 命令再次执行均为 `ninja: no work to do.`；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,051 | `0x1BD31` | `0x1A40F8E` | `0x5A3E40` | `0x3185F7B` | `9C65072ABC08CD4BC7EE9A1114A9BFBE82CB1123A26E3C785B1516BA358C6325` |
| Wasmtime `index.wasm` | 85,002,192 | `0x1BA50` | `0x19E8F3C` | `0x5A1090` | `0x3141E11` | `E7E11C4C1BA45D03B73860A0214349E19D58759AC9B3A5D2134815F60CF0497E` |
| Wasmtime guest | 151,478,081 | `0x1618E` | `0x13D7CC2` | `0x4D1630` | `0x1421EBA` | `38096DE7AB21560C039F60D0B5D5F64A1F48B51970315BCE27CF024BEE849BE2` |

相对 V246，Web/Wasmtime 主模块各缩小 29 bytes，且 FUNCTION、DATA、name 均不变、CODE 各缩小
`0x1D`。这与最后把 non-identity flag 从错误的远端位置归回 damping-adjacent control group 后的
load/store 地址折叠一致；没有资源、函数数量或导出表漂移。guest 含完整 differential test TU 和大量
debug/name metadata，体积相对 V246 增加 83 bytes，不用作生产模块 CODE 等量守护。

## 10. V248 后续补证

V248 已闭合这里列出的下一边界，并进一步恢复完整 Player prefix：root/parent/currentDispatch 三个
raw word、NodeLabelMap、camera position/target/stereovision 九 double、cameraOffset 两 float、bounds
四 double、MotionNode deque。map 末尾、camera block、bounds 末尾与 deque 在四端均精确邻接；
cameraOffset 虽不在九-double memset 内，但 constructor 另有独立零写。完整 offset、container ABI、
自动析构顺序、iOS armv7 安全保存和 build 基线见
`analysis/motionplayer_player_prefix_currentdispatch_nodelabel_camera_bounds_deque_layout_four_binary_2026-08-18.md`。
