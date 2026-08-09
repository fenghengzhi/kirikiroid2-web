# Android ARM64 PSBFile/PSBMedia 全量运行时闭环（2026-08-03）

## 结论

权威 Android 1.4.4 ARM64 `libkrkr2.so` 上，当前 PSBFile/PSBMedia 天然输入
oracle 的 **24/24 case 均为 `ok`**：无 trace 与单次全量 `--trace` 各完整通过一次，
均无 `cleanup_errors`。本轮没有发现新的生产实现差异，因此没有修改 `cpp/`；114 个
canonical 入口的审计统计继续为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

运行时 PASS 关闭的是此前“尚未上真实设备执行”的覆盖缺口，不能把 stripped/O3 无法
唯一恢复的 15 个源码 token 或 factorization 上限升级为 `ALIGNED`。

## 权威目标与环境

- AVD：`android-12-api31-arm64`，guest ABI `arm64-v8a`，SDK 31；
- fingerprint：
  `google/sdk_gphone64_arm64/emulator64_arm64:12/SE1A.220630.001.A1/9056438:userdebug/dev-keys`；
- GPU：headless `-gpu swiftshader`；旧 `swiftshader_indirect` 在无窗口 GL 路径出现过
  `libGLESv2_enc` 空指针崩溃，属于模拟器后端，不是 psbfile；
- APK：`tests/differential/oracle_runner/harness-apk/prebuilt/krkr2-harness.apk`，
  SHA-256
  `87ea538c9fea8fa390e049a46f837eb40e7716c79aa44d54a9c3a2e35641d5c8`；
- package ABI：`primaryCpuAbi=arm64-v8a`、`secondaryCpuAbi=null`；APK 仅含
  `lib/arm64-v8a/`；
- APK 内 `libkrkr2.so` SHA-256：
  `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`；
- APK 内 `libharness.so` SHA-256：
  `8f2e6678343a424d2bd6ec25b81644a94c642df448e887f24253868a57416b6d`；
- Frida host/server：`16.4.10`。

本闭环只使用 Android ARM64；不使用、生成或恢复 ARMv7/iOS ARMv7 二进制或资料。

## 启动线程隔离

默认 Full TJS 启动包 `caution_minimal.xp3` 在启动结束后还会由 GL 线程运行约 2.5 秒的
continuous handler。Frida 会显著拉长集合枚举；若 harness-rpc 在 handler 尚未移除时
同时进入同一 TJS VM，会先破坏 `startup.tjs` 的 `phase`，随后让枚举回调的 global
accumulator 变成 `Void`，最终因 TJS 异常跨 RPC 边界触发 `std::terminate`。

runner 现默认在 `startupFrom` 后静置 4 秒，再发起跨线程 TJS 调用；可用
`--startup-settle-seconds 0..60` 显式调整。静置后，同一个 APK、输入和 native 调用链在
合并 trace 中稳定通过。该修正只隔离测试线程，不改变目标 APK、PSB/MDF 或生产实现。

## 完整命令

无 trace：

```bash
python3 tests/differential/python/run_psbfile_load_adb.py \
  --serial emulator-5554 \
  --integer-boundary --real-boundary --string-boundary \
  --shape-boundary --resource-boundary \
  --media-interface --media-lifecycle --media-dictionary \
  --media-array --media-adaptor-null \
  --startup-xp3 reference/xp3/caution_minimal/caution_minimal.xp3
```

全量调用链：在同一命令增加 `--trace`。两次命令均退出 0。

## 24 个天然输入/生命周期 case

| 分组 | case 数 | 结果 |
| --- | ---: | --- |
| raw holder / Dictionary lookup / ordered key vector | 3 | 3/3 `ok` |
| PSBMedia interface | 1 | 1/1 `ok` |
| Integer `0x04..0x09` | 7 | 7/7 `ok` |
| Real `0x1D..0x1F` | 3 | 3/3 `ok` |
| String `0x15/0x16` | 2 | 2/2 `ok` |
| Null `0x01` | 1 | 1/1 `ok` |
| Array `0x20` / Dictionary `0x21` | 2 | 2/2 `ok` |
| Resource `0x19` | 1 | 1/1 `ok` |
| media replacement / Dictionary list / Array list / null adaptor | 4 | 4/4 `ok` |
| **合计** | **24** | **24/24 `ok`** |

全量 trace 的逐 case 事件数为：

```text
raw-holder=12, raw-dictionary=82, raw-keys=56, media-interface=3022
integer=[486,1166,1244,1166,950,1382,910]
real=[704,444,888], string=[714,890], null=532
array-collection=35456, dictionary-collection=44564, resource=642
media=[356,160,126,122]
```

这些计数是诊断规模，不是跨 Frida/runner 版本的 golden；判定依赖事件中的目标地址、
参数/返回和 adapter 的状态不变量。

## 引用计数观察口径纠正

Full TJS 的 `TJS_GLOBAL` RPC 使用持久 Variant 栈。栈压缩与 GL 线程清理会在长 trace
期间释放已经无关的临时引用，因此跨多个 RPC 阶段比较“初始绝对 refcount”会产生假差异。
当前 oracle 改为直接观察目标操作的局部边：

- `Invalidate@0x596F0C`：紧邻调用前后 dispatch/owner refcount 净变化必须为 0；
- `NativeInstanceSupport@0x596D90`：matching GET 前后局部 refcount 必须相等；
- 19 个 unsupported vslot：逐 slot 固定 `-1002`、64-byte 输出不变及除 refcount 字段外的
  dispatch 结构不变；每次 refcount transition 仍输出为诊断，但不把并发临时栈回收误判为
  vslot 写入；
- 清除脚本 global 后，目标 dispatch 至少释放 Object/ObjThis 两个引用且仍由独立
  Variant 保活；随后直接 `AddRef/Release` 必须严格 `+1/-1`；
- Resource Octet 初始引用必须不少于 2，销毁独立输出 Variant 后必须下降且仍不少于 1，
  同时 copied data 地址和 612 字节内容保持不变。

集合回调的 ObjThis 仍被故意清零。callback 内所有共享状态使用显式 `global.*`，并以普通
读取加赋值代替依赖 callback `this` 的 compound property 写入；value/no-value 两条枚举仍
分别严格验证参数数目、flags、顺序、类型和 callback `this`。

## 对审计的影响

1. raw holder、packed Dictionary、gnustl COW key vector、Null、Array/Dictionary dispatch、
   Resource、PSBMedia interface/storage/list/adaptor-null 的真实 ARM64 执行面全部闭合；
2. 没有输入字节改写，没有新造 fixture；所有成功路径复用仓库既有天然资产及 SHA pin；
3. 没有发现 `cpp/` 数据流、调用链、对象生命周期、容器或边界行为的新 GAP；
4. 仍缺的只有证据队列中无法由这些成功路径产生的天然失败/极端输入，以及 15 个需要
   Android 1.4.4 新正证据才能消除的源码 token 上限。
