# SourceCache / ObjSource NCB 注册面四二进制联合恢复

日期：2026-08-27

## 1. 本 slice 的边界

本轮只闭合 `Motion.SourceCache` 和 `Motion.ObjSource` 的 delayed-subclass setup、成员
registrar、零参数脚本构造器、发布顺序和 callback 入口。回调 body 的资源解码、缓存
容器、Layer owner 和异常边界继续保留为独立语义 slice；“注册面已映射”不冒充“方法
实现已恢复”。

四个参考二进制联合权威，本地 `cpp/plugins/motionplayer/main.cpp` 只在取得四端证据后
逐行对照。

## 2. delayed subclass 调用链

### SourceCache

| 平台 | `Motion.SourceCache` wrapper | delayed setup | member registrar | constructor publisher |
|---|---|---|---|---|
| Android arm64 | `0x6FB504` | `0x6FB668` | `0x6A5988` | registrar 内联，始于 `0x6A59A8` |
| Android armv7 | `0x599738` | `0x5B6A20` | `0x57B0DC` | `0x57B14C` |
| iOS arm64 | `0x100126064` | `0x10014DF88` | `0x100100F90` | `0x100101018` |
| iOS armv7 | `0x12514C` | `0x14FC78` | `0xFE12A` | `0xFE19C` |

### ObjSource

| 平台 | `Motion.ObjSource` wrapper | delayed setup | member registrar | constructor publisher |
|---|---|---|---|---|
| Android arm64 | `0x6FB9F0` | `0x6FBB54` | `0x69A098` | registrar 内联，始于 `0x69A0B8` |
| Android armv7 | `0x59977C` | `0x5B6CDC` | `0x575028` | `0x5750F4` |
| iOS arm64 | `0x1001260DC` | `0x10014E328` | `0x1000F8D30` | `0x1000F8E38` |
| iOS armv7 | `0x125194` | `0x150088` | `0xF5C48` | `0xF5D24` |

四端共同结构为：

```text
register_or_unregister(classObject, registering):
    if classInfo already exists and registering:
        return false
    make transient registration state(classObject, registering)
    if registering:
        initialize distinct delayed ncbClassInfo tuple
    publish members through class-specific registrar
    finalize/unregister transient state
    return classInfo_exists || !registering
```

两个类拥有彼此独立的 class-info tuple、class ID、class object 和 finalize hook。它们不是
相邻表项共享同一份状态。四端 wrapper 仅在成功注册时为 `Motion` 发布 subclass adaptor；
注销路径清理 tuple，不把成员 registrar 误当成独立全局类。

## 3. SourceCache 精确四行表

| 序号 | 脚本名 | native kind | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---|---|---|---|---|
| 1 | `<constructor>` | zero-arg constructor | inline `0x6A59A8` | `0x57B14C` | `0x100101018` | `0xFE19C` |
| 2 | `loadSource` | method | entry `0x6A4F88` in function `0x6A4CD4` | `0x57ACC8` | `0x1001009AC` | `0xFDB50` |
| 3 | `clearCache` | method | entry `0x6A5818` in function `0x6A4CD4` | `0x57B018` | `0x100100F10` | `0xFE0D4` |
| 4 | `bufLayer` | getter-only property | `0x6A58DC` | `0x57B060` | `0x100100F84` | `0xFE11A` |

Android arm64 把 `loadSource` 与 `clearCache` 放在 IDA 的同一个 `0x6A4CD4..0x6A58DC`
函数范围中，但 registrar 分别保存 `0x6A4F88` 和 `0x6A5818` 两个内部入口。该平台差异
必须作为 mapping disposition 保存；不能为了让表格好看而人为定义重叠函数。

构造 publisher 四端都把参数个数传为零，随后仅在 registering 状态发布 descriptor。
这与本地 `NCB_CONSTRUCTOR(())` 一致；`SourceCache(owner, cacheSize)` 只属于
`ResourceManager` 的 native 基类构造路径，不是第二个脚本构造重载。

## 4. ObjSource 精确七行表

| 序号 | 脚本名 | native kind | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---:|---|---|---|---|---|---|
| 1 | `<constructor>` | zero-arg constructor | inline `0x69A0B8` | `0x5750F4` | `0x1000F8E38` | `0xF5D24` |
| 2 | `originX` | getter-only property | `0x69A3F4` | `0x57511C` | `0x1000F8E88` | `0xF5D4C` |
| 3 | `originY` | getter-only property | `0x69A4B8` | `0x575180` | `0x1000F8EEC` | `0xF5E04` |
| 4 | `width` | getter-only property | `0x69A57C` | `0x5751E4` | `0x1000F8F50` | `0xF5EBC` |
| 5 | `height` | getter-only property | `0x69A65C` | `0x575258` | `0x1000F8FD0` | `0xF5F8C` |
| 6 | `clip` | getter-only property | `0x69A73C` | `0x5752CC` | `0x1000F9050` | `0xF605C` |
| 7 | `drawLayer` | method | `0x69AAB8` | `0x5754E4` | `0x1000F930C` | `0xF63C0` |

所有 property descriptor 的 setter、indexed getter/setter 槽均为 null；`drawLayer` 是普通
method descriptor。四端 constructor publisher 同样使用零参数边界，与本地
`NCB_CONSTRUCTOR(())` 一致。

## 5. registrar 共同伪代码

```text
SourceCache::registerMembers(state):
    publishZeroArgConstructor(dynamicClassName)
    if !registering: return
    publishMethod("loadSource", loadSourceCallback)
    if !registering: return
    publishMethod("clearCache", clearCacheCallback)
    if !registering: return
    publishReadOnlyProperty("bufLayer", getBufLayerCallback)

ObjSource::registerMembers(state):
    publishZeroArgConstructor(dynamicClassName)
    if !registering: return
    publishReadOnlyProperty("originX", getOriginXCallback)
    if !registering: return
    publishReadOnlyProperty("originY", getOriginYCallback)
    if !registering: return
    publishReadOnlyProperty("width", getWidthCallback)
    if !registering: return
    publishReadOnlyProperty("height", getHeightCallback)
    if !registering: return
    publishReadOnlyProperty("clip", getClipCallback)
    if !registering: return
    publishMethod("drawLayer", drawLayerCallback)
```

Android arm64 把 descriptor 对象构造和 publish 展开在 registrar 内；其余三端主要调用
模板 helper。这个编译形态差异不改变脚本名、顺序、kind、getter-only 状态或 callback
等价类。

## 6. 与本地源码逐行对照

`cpp/plugins/motionplayer/main.cpp` 当前精确匹配：

```cpp
NCB_REGISTER_SUBCLASS_DELAY(SourceCache) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(loadSource);
    NCB_METHOD(clearCache);
    NCB_PROPERTY_RO(bufLayer, getBufLayer);
}

NCB_REGISTER_SUBCLASS_DELAY(ObjSource) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(originX, getOriginX);
    NCB_PROPERTY_RO(originY, getOriginY);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    NCB_PROPERTY_RO(clip, getClip);
    NCB_METHOD(drawLayer);
}
```

本轮无需修改编译语义。尤其没有增加 setter、额外构造重载、继承 `SourceCache` 成员或
为 `ObjSource` 添加 `loadSource`。

## 7. 证据量和未完成项

fresh 证据覆盖：

- 8 个 `Motion` subclass wrapper：86/25/28/25 条指令的两组对应体；
- 8 个 delayed setup：Android arm64 两个 tuple setup 各 80 条，Android armv7 各
  48 条，iOS arm64 各 32 条，iOS armv7 各 71 条；
- 8 个 member registrar：`SourceCache` 为 133/36/31/37 条，`ObjSource` 为
  208/64/60/68 条；
- 6 个非内联 constructor publisher：Android armv7、iOS arm64、iOS armv7 每类分别
  为 16/20/16 条；Android arm64 publisher 已在完整 registrar 中逐指令读取；
- 对 wrapper、setup、registrar、constructor publisher 和全部 callback 入口完成 fresh
  function map / xref 对照；
- 四个 IDB 的 wrapper、setup、registrar 和独立 callback 已命名，Android arm64 两个
  内部 callback entry 已加行注释，四端 registrar 已加书签，全部数据库已原位保存。

本注册面 slice 当时只把 11 条注册面从 `UNMAPPED` 提升为 `EVIDENCED_4_4`。后续
`analysis/motionplayer_sourcecache_load_clear_buflayer_four_binary_2026-08-27.md` 已把
SourceCache 的 3 个非构造 callback 以及 ResourceManager 中复用的同三行全部提升为
`IMPLEMENTED`，包括缓存 list、Layer owner、trim/bake/tint、public clear 和 getter sharp
boundary。再后续
`analysis/motionplayer_objsource_getters_clip_draw_decode_texture_lifetime_four_binary_2026-08-27.md`
已把 ObjSource 的 6 个非构造 callback、RL8/RL32、palette/BGRA、Bitmap/Texture owner 和
adaptor 析构链全部提升为 `IMPLEMENTED`。这两个类的公开非构造面现已闭合；完整
root-reachable helper / 对象 / 容器台账仍需继续推进。
