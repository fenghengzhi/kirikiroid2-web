# MotionPlayer Player 图像空方法与 D3DAdaptor nullsub 四端复核

日期：2026-08-11

## 结论

本轮区分了两个容易因同名而混淆的表面：

- `D3DAdaptor::unloadUnusedTextures` 是四份当前参考二进制都真实注册的空函数，
  必须保留；它与 `setPos/removeAllBg/removeAllCaption/registerBg/registerCaption`
  共同构成六个有意暴露的 nullsub。
- `Player::unloadUnusedTextures/copyRect/adjustGamma` 不在四端精确的 92-member
  `Motion.Player` 注册表内，本地也没有任何内部 caller。三个空 C++ wrapper 是
  旧端口遗留，不对应当前可达原版函数，现已删除。

`SourceCache` 对 buffer TJS 对象调用的 `copyRect` 和 `adjustGamma` 是另一条数据
流：那两个名字属于外部图像/Layer-like dispatch 的动态方法调用，不能据此在
`motion::Player` 上补同名空方法。

## 四端证据

| 目标 | D3DAdaptor registrar | setPos nullsub | unloadUnusedTextures nullsub | Motion.Player registrar |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x6AA274` | `0x6AAB84` | `0x6AACE0` | `0x6D3DA8` |
| Android ARMv7 | `0x57CC58` | `0x57CF64` | `0x57CF82` | `0x597EC8` |
| iOS ARM64 | `0x1001039A4` | `0x100103D3C` | `0x100103D98` | `0x1001244F8` |
| iOS ARMv7 | `0x100D94` | `0x101128` | `0x10115C` | `0x123848` |

四个 D3DAdaptor registrar 都把 `unloadUnusedTextures` 直接绑定到只有 return 的
独立 nullsub；其余五个空成员也以同样方式注册。它们不是“尚待实现”的 stub。

跨 ASCII、UTF-16LE、UTF-32LE 搜索确认：

- `unloadUnusedTextures` 的唯一 MotionPlayer 注册 xref 落在 D3DAdaptor
  registrar；Player registrar 没有该成员。
- `copyRect/adjustGamma` 的引用落在图像/buffer 相关注册或动态调用路径，不落在
  四个 Player registrar。
- 本地精确引用扫描显示三个 `Player::` 空 wrapper 只有声明和定义，没有 caller；
  删除它们不改变 host/internal 数据流。

## 本地修正

- 删除 `Player.h` 和 `PlayerRender.cpp` 中三个无参考、无 caller 的空方法。
- 保留 D3DAdaptor 的六个原生 nullsub，并把原注释中“six”但只列五个的歧义改成
  “setPos 加以下五个”。
- 修正 `main.cpp` 的 92-member 说明：有真实内部 caller 的未暴露 helper 继续
  保留；这三个纯空、无 caller wrapper 不再以“仅取消 TJS 暴露”为由残留。

## 验证状态

- Web Debug 与 Wasmtime Debug 的 `motionplayer` 增量构建均完成；随后两个目录的
  Ninja dry-run 都报告 `no work to do`。
- `git diff --check` 通过；输出只有仓库既有的 LF/CRLF 转换提示。
