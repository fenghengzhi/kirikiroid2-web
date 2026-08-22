# Motionplayer SourceCache::clearCache typed-null 边界（2026-08-13）

## 结论

四个当前参考二进制的 `SourceCache::clearCache` 对每个 entry Layer：

1. 只检查 `tTJSVariant` 类型码是否等于 Object；
2. 类型命中后直接读取并解引用 Variant 的 `Object` 指针；
3. 调用 `Object->Invalidate(0, nullptr, nullptr, Object)`；
4. 不使用 closure 的 `ObjThis`；
5. 没有 typed-null Object 的空指针恢复/跳过分支。

仓库此前额外检查 `AsObjectNoAddRef() != nullptr`，会静默跳过 malformed
typed-null Object。这不是当前四端边界。本轮已删除该额外保护。

## 四端指令证据

### Android arm64

`0x6A5818` 是 IDA 合并的 clearCache chunk：

- `0x6A5840..0x6A5848` 比较 node layer Variant type `== 1`；
- `0x6A584C` 直接载入 Object 指针；
- `0x6A585C..0x6A5868` 从该指针取 vtable 并调用 Invalidate slot；
- 参数是 flag=0、member=null、hint=null、objthis=Object。

Object load 后没有 `CBZ`。

### Android armv7

`0x57B018`：

- `0x57B02C..0x57B030` 检查 type==1；
- `0x57B032` 读 Object；
- `0x57B038..0x57B040` 立即解引用并调用 vtable `+0x38`；
- 无 null test。

### iOS arm64

`0x100100F10`：

- `0x100100F30..0x100100F38` 检查 type==1；
- `0x100100F3C` 读 Object；
- `0x100100F40..0x100100F58` 立即调用 vtable `+0x70`；
- 无 null test。

### iOS armv7

`0xFE0D4`：

- `0xFE0EA..0xFE0EE` 检查 type==1；
- `0xFE0F0` 读 Object；
- `0xFE0F6..0xFE0FE` 立即调用 vtable `+0x38`；
- 无 null test。

## 清理顺序

四端都先完成整表 Invalidate 遍历，再调用 list clear helper，最后把
`currentCacheBytes` 置零。Invalidate 返回值被丢弃；即使某个普通 dispatch
返回错误码，遍历仍继续。bufLayer、owner、primaryLayer 与 limit 不变。

正常插件路径的 entry Layer 来自成功的全局 `Layer` CreateNew，因此该边界主要
影响损坏/人为构造的 typed-null 状态；端口不应为原实现不存在的状态静默恢复。

## 修改

`cpp/plugins/motionplayer/SourceCache.cpp` 的 clearCache 现在只按 Type==Object
进入分支，随后直接调用 Object 的 Invalidate，并明确记录 Object/ObjThis 选择。

四个 IDB 的 clearCache 入口也补充了相同边界注释。

## 验证

- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过；只有既有
  `_tss` warning。
- Web Debug：重编 `SourceCache.cpp`、归档 motionplayer 并成功链接
  `index.html/index.wasm`。
- Wasmtime guest：重编 `SourceCache.cpp` guest object，成功链接并转换 wasm。
