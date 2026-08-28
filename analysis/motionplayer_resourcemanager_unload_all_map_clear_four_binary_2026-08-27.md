# ResourceManager::unloadAll module-map clear 四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四端 `ResourceManager::unloadAll` 的共同源码只有 `_loadedModules.clear()`。它清空
`unordered_map<ttstr, LoadedResourceRecord>` 的所有outer nodes并保留outer bucket allocation；
不调用SourceCache::clearCache、不Invalidate Layer、不修改RandomGenerator、不修改Layer ID set/counter，
也不输出日志。

每个outer node销毁时，mapped `LoadedResourceRecord`按声明逆序销毁：KRKR source map、Win source
map、PSB file owner，然后销毁outer ttstr key并释放node。两个inner map逐entry Release纹理、销毁
inner ttstr key和node，最后释放各自bucket storage。outer clear完成后只把outer bucket slots置null、
head/size置零；ResourceManager destructor随后自动map析构才释放保留的outer bucket allocation。

本地容器与owner顺序已经匹配，唯一偏差是`unloadAll()`调用clear前额外执行生产
`LOGGER->debug`。四端callback与clear helper没有任何日志调用、字符串或logger gate；本轮删除该
额外可观察副作用。

## 2. 四端 callback 映射

| 平台 | callback | wrapper/主体指令 | outer clear helper |
|---|---:|---:|---:|
| Android arm64 | internal entry `0x6A60D8` | 37 | callback内联 |
| Android armv7 | `0x57B32C` | 2 | `0x59A62C`，17条 |
| iOS arm64 | `0x1001012CC` | 2 | `0x10013A138`，22条 |
| iOS armv7 | `0xFE3FE` | 2 | `0x13A246`，20条 |

四端均fresh decompile/full disassembly。Android arm64地址有独立`SUB SP`函数序言，但IDA当前把它
误并到`ResourceManager` destructor `0x6A5F74`尾部；本轮尝试定义真实边界时因overlap被IDA拒绝，
故保留internal-entry disposition，并从完整126条containing stream中单独读取`0x6A60D8..0x6A6168`
的37条流。其余三端wrapper仅调整`this`到map subobject后tail-call clear helper。

## 3. outer node 与 LoadedResourceRecord 析构映射

| 平台 | outer node/value destroy | 指令 | KRKR inner map dtor | Win inner map dtor |
|---|---:|---:|---:|---:|
| Android arm64 | `0x6D87C8` | 28 | `0x6D8838`，41 | `0x6D88DC`，41 |
| Android armv7 | `0x59A65A` | 16 | `0x59A68A`，12 | `0x59A72E`，12 |
| iOS arm64 | inline in `0x1001282F0` | 25 | `0x100128354`，14 | `0x100128414`，14 |
| iOS armv7 | inline in `0x127664` | 21 | `0x12769E`，12 | `0x12777C`，12 |

outer clear/node/value和inner map destructors均fresh decompile；关键主体完成full disassembly或总指令
计数。Mapped texture-entry destroy为Android arm64 inner loop inline、Android armv7
`0x59A710/0x59A7B4`各11条、iOS arm64 `0x1001283DC` 13条、iOS armv7
`0x1276E4` 45条（含SjLj envelope）。四端都在texture非null时调用其Release虚槽，再销毁ttstr key。

## 4. 共同源码伪代码

```text
void ResourceManager::unloadAll() const:
    loadedModules.clear()
```

标准库clear展开后的owner顺序为：

```text
for each outer hash node:
    destroy node.second.krkrSourceEntries:
        for each inner node:
            if texture != null: texture.Release()
            destroy inner ttstr key
            delete inner node
        free inner bucket storage

    destroy node.second.winSourceTextures:
        for each inner node:
            if texture != null: texture.Release()
            destroy inner ttstr key
            delete inner node
        free inner bucket storage

    destroy node.second.file
    destroy node.first ttstr
    delete outer node

clear outer bucket slots
outer head = null
outer size = 0
// retain outer buckets, bucket_count and max_load_factor
```

`PackedSourceAtlasEntry`在texture后只有整数、rect和double array等trivial数据，因此它与
`WinSourceTextureEntry`的非trivial析构核心都只是texture Release；这解释了iOS两个inner map共享
同一mapped-value destructor helper，而不是证明两个完整value类型相同。

## 5. 平台/STL差异

### 5.1 Android libstdc++

- outer node链直接保存next；clear逐链销毁，再对全部bucket slots做memset，empty map也可触碰
  现有bucket array。
- Android arm64把outer clear与node destroy loop内联到callback；armv7复用独立helper。
- inner map destructor清nodes/slots后释放非inline bucket array；small/default bucket可能指向对象内
  sentinel，不delete。

### 5.2 iOS libc++

- outer clear先检查size；size为0时直接返回，不扫描bucket array。
- 非空时按node链销毁，清first-node、bucket slots和size，但保留outer bucket allocation。
- inner map destructor销毁nodes后把bucket pointer交换/置null并free原allocation。

这些是同一`unordered_map::clear()`和成员析构的标准库实现差异，不应手写成平台分支。

## 6. 边界行为

- empty调用：逻辑no-op；Android可能仍memset bucket slots，iOS按size gate完全不触碰bucket。
- clear不shrink/rehash outer map，下一次load可复用bucket allocation与max-load-factor状态。
- 纹理Release、PSB owner release、ttstr析构和node delete按已述顺序发生；不存在先清bucket再销毁
  values的两阶段快照。
- callback不返回删除数量，typed wrapper忽略clear helper残留寄存器值。
- 无锁；与load/find/source texture访问并发会形成unordered_map和owner data race。
- 函数不catch异常。原生texture Release通常不抛；若外部虚实现违约抛出，clear已销毁前缀且容器
  处于标准库的partial-destruction边，不回滚。

## 7. 本地逐行对照与修正

`ResourceManager::_loadedModules`当前类型是：

```text
unordered_map<ttstr, LoadedResourceRecord, ttstr_hash, ttstr_equal>
```

`LoadedResourceRecord`声明为`PSBFile -> winSourceTextures -> krkrSourceEntries`，普通C++逆序析构自然
产生四端共同的`KRKR -> Win -> PSB`顺序。两个texture entry析构都只Release非null纹理；key/node/bucket
由unordered_map管理。本地不需要手写clear loop或改变container。

修正前函数：

```cpp
LOGGER->debug("ResourceManager::unloadAll()");
_loadedModules.clear();
```

目标：

```cpp
_loadedModules.clear();
```

删除日志恢复四端纯clear调用图，避免额外format/string/logger读取、线程同步和潜在异常点。

## 8. 验证边界

- 已删除额外`LOGGER->debug`，将unloadAll body状态从pending提升为implemented；
- 已执行`git diff --check`、coverage/NCB TSV字段校验和NCB确定性重生成，生成结果逐字节一致；
- 四端IDB已补充callback/clear/node/value/inner-map析构命名或注释、书签并原位保存；
- 当前缺少CMake/Ninja/Emscripten和完整依赖头，不能宣称正式unit/Web build。
- 无需伪造fixture；outer bucket容量与inner texture owner可由四端静态容器流证明，现有物料没有
  独立暴露所有allocator内部状态的oracle。
