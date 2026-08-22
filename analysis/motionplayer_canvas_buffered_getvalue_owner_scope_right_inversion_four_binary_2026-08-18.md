# ordinary Canvas buffered `GetValue`/owner scope/right-inversion 四参考二进制闭环（V244，2026-08-18）

## 1. 结论

四个当前参考实现共同锁定了 ordinary Canvas buffered branch 的源码结构与控制流：

1. `_sourceCacheObject` 先经一个完整表达式临时 `tTJSVariant` CopyRef 构造
   `ncbPropAccessor resourceManager`；accessor只留下`Object`的raw retain，临时Variant在任何
   `bufLayer`回调前析构；
2. `bufLayer`不是手写`PropGet`的source-level特例，而是
   `resourceManager.GetValue(TJS_W("bufLayer"), Tag<tTJSVariant>(), 0, &hint)`的返回值；
3. Android arm64把这个模板完整内联成
   `PropGet -> property-result Variant -> CopyRef返回值 -> property-result析构`，另外三端保留
   `Motion_propGetVariant_guess` out-of-line helper；
4. `ncbPropAccessor buffer{tTJSVariant(bufLayer)}`再次执行temporary CopyRef、Object-only
   `AsObject` retain、temporary Variant析构；
5. `bufferRight < bufferLeft`只跳过`setSize/copy/mask/operateRect`图像阶段，**不会跳过item**；
6. 无论图像阶段正常完成还是因`right < left`被跳过，buffered局部owners都在debug-frame
   gate之前按`buffer raw Object -> bufLayer Variant -> ResourceManager raw Object`顺序清理；
7. `drawLine/drawMeshFrame/drawBezierPatch*Frame`仍在该item上执行。此前源码的`continue`和
   owner外层作用域都与四个参考不符。

这也修正了V243报告“下一边界”里把`right < left`暂称为early continue的过时判断；该分支是
image-phase-only skip。

## 2. 最可能的原始源码形状

四端编译差异能由同一份C++自然解释：

```cpp
if(!useDirectRenderPath) {
    {
        ncbPropAccessor resourceManager{tTJSVariant(_sourceCacheObject)};
        tTJSVariant bufLayer = resourceManager.GetValue(
            TJS_W("bufLayer"), ncbTypedefs::Tag<tTJSVariant>(), 0,
            &bufLayerHint);
        ncbPropAccessor buffer{tTJSVariant(bufLayer)};

        read target width/height;
        compute left/top/right/bottom;
        if(!(bufferRight < bufferLeft)) {
            setSize/copy;
            apply ancestor masks;
            operateRect;
        }
    } // buffer raw -> bufLayer Variant -> ResourceManager raw

    draw debug frame;
}
```

这里的双层花括号不是Web侧整理用的装饰：四份参考都把三层buffered owners的normal tail放在
debug-frame gate前；直接把这些局部变量留在per-item外层作用域会产生可观察的Release时点差异。

## 3. `GetValue<tTJSVariant>` source identity

### Android arm64：模板内联

- `0x6C4F98`：ResourceManager temporary CopyRef；
- `0x6C4FB8`：inline `AsObject`/Object AddRef；
- `0x6C4FD8`：temporary Variant dtor；
- `0x6C500C`：inline `GetValue`的`PropGet`写property-result local；
- `0x6C5018`：CopyRef到persistent returned `bufLayer`；
- `0x6C5020`：property-result local dtor；
- `0x6C502C`：buffer temporary CopyRef；
- `0x6C5050`：inline `AsObject`/Object AddRef；
- `0x6C5070`：buffer temporary Variant dtor。

### Android armv7：模板保留helper

- `0x58E844 / 0x58E850 / 0x58E85A`：ResourceManager temporary CopyRef、AsObject、dtor；
- `0x58E87C`：`Motion_propGetVariant_guess`，sret直接形成persistent `bufLayer`；
- `0x58E88A / 0x58E890 / 0x58E89A`：buffer temporary CopyRef、AsObject、dtor。

### iOS arm64：模板保留helper

- `0x100118C00 / 0x100118C18 / 0x100118C24`：ResourceManager prefix；
- `0x100118C48`：`Motion_propGetVariant_guess`；
- `0x100118C54 / 0x100118C60 / 0x100118C6C`：buffer prefix。

### iOS armv7：模板保留helper

- `0x117068 / 0x11707C / 0x117088`：ResourceManager prefix；
- `0x1170B8`：`Motion_propGetVariant_guess`；
- `0x1170CA / 0x1170D8 / 0x1170E4`：buffer prefix。

iOS armv7的helper本体在`0xEDBF0`：先调用accessor dispatch的`PropGet`写本地Variant，随后
CopyRef到sret，再析构本地Variant；普通HRESULT完全未参与返回控制。本轮把该函数由
`sub_EDBF0`恢复为`Motion_propGetVariant_guess`。

## 4. right-inversion边界

四端均只有一个ordered horizontal test：

| target | compare/branch | skip target |
| --- | --- | --- |
| Android arm64 | `0x6C50D8 / 0x6C50DC` | `0x6C5984` nested cleanup |
| Android armv7 | `0x58E938 / 0x58E93C` | `0x58F252` nested cleanup |
| iOS arm64 | `0x100118CD8 / 0x100118CE0` | `0x100119530` nested cleanup |
| iOS armv7 | `0x11719C / 0x1171A0` | `0x117B1C` nested cleanup |

因此：

- `right < left`：不调用buffer `setSize`、任何copy、mask、`operateRect`；
- `right == left`：宽度0，仍执行图像阶段；
- `bottom < top`：没有对应early skip，负高度仍传入`setSize`；
- NaN造成unordered compare时：不跳过图像阶段；
- 横向反转不影响后续outline/meshline gate和按mesh type分派的frame helper。

当前源码旧逻辑在该分支直接`continue`，会额外丢失debug frame、改变外层owner析构时点，并绕过
此item正常尾部；四端均无此行为。

## 5. normal tail与异常边界

normal/right-inverted共同尾部如下：

| phase | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| buffer raw Release | `0x6C5984..0x6C5994` | `0x58F252..0x58F25A` | `0x100119530..0x100119540` | `0x117B1C..0x117B34` |
| bufLayer Variant dtor | `0x6C5998` | `0x58F25C..0x58F260` | `0x100119544..0x100119548` | `0x117B36..0x117B3A` |
| ResourceManager raw Release | `0x6C59AC..0x6C59B8` | `0x58F264..0x58F276` | `0x10011955C..0x10011956C` | `0x117B44..0x117B54` |
| debug-frame gate | `0x6C59BC` | `0x58F278` | `0x100119574` | `0x117B56` |

异常含义：

- `GetValue`内部`PropGet`抛出时，buffer accessor尚未构造；ResourceManager raw owner随外层unwind
  释放；
- buffer temporary conversion、width/height、copy、mask或`operateRect`抛出时，已完成构造的
  buffered owners按上述逆序unwind，debug frame不会在异常之后补跑；
- debug-frame callback抛出时，三层buffered owners已经清理；随后只会unwind仍存活的source
  accessor/source Variant/color/descriptor等per-item外层owners；
- 普通负HRESULT仍被现有dispatch边界忽略，不等价于C++异常。

## 6. 源码修复

`cpp/plugins/motionplayer/PlayerRenderExecute.cpp`：

- 用带共享hint的`ncbPropAccessor::GetValue<tTJSVariant>`恢复原始source identity；
- 把ResourceManager/bufLayer/buffer及完整图像阶段放入独立局部作用域；
- 把`if(right < left) continue`改为`if(!(right < left)) { image phase }`；
- 把debug-frame调用放到该作用域之后；trace/snapshot仍只在真正执行`operateRect`后发布。

`cpp/plugins/motionplayer/Player.h`新增非脚本可见的test-only submitter入口。

`tests/unit-tests/plugins/motionplayer-dll.cpp`新增端到端夹具：先用inverted viewport命中原生
full-target clip fallback，再构造独立的buffered `right < left`；验证：

- ResourceManager只执行一次`loadSource`与一次带正确hint/objthis的`bufLayer` PropGet；
- buffer对象没有收到`setSize/copy/mask/operateRect`；
- affine debug overlay仍提交4次`drawLine`；
- 第一条frame callback观察到的ResourceManager/buffer Release计数与submitter返回后的计数相同，
  即没有任何buffered owner跨越frame callback。

## 7. IDB写回与iOS armv7安全保存

四库本轮共写回51条comments、16个bookmarks；另在iOS armv7恢复1个semantic function name。

iOS armv7执行两次different-path compressed save（第二次用于补写helper名称），每次均经独立
`C:\IDA\idat.exe`重开退出码0、live session `save=false`关闭、旧canonical/loose files逐文件移动
到recovery目录、安装verified packed copy、MCP重开读回、`save=false`关闭。没有递归删除。

- V243 pre-V244 canonical：376,109,264 bytes，SHA-256
  `93FD4C4766E961CA7982091472AA8A7FB36FC08F230CED185812E56DBA4462E1`；
- first V244 packed copy（补rename前）：376,109,264 bytes，SHA-256
  `545EAF13A0108CACCEC0E351D1E4F1D8C75728CE5629CF339857CD9EF20768A2`；
- final V244 canonical：376,109,264 bytes，SHA-256
  `7D52D78D0E0BCCC352880DA3D8F362249C02BE60D86FAC3477E2AD63C29EA2EF`。

最终MCP读回确认`Player_renderToCanvas_guess`、V244 owner/control-flow comments与
`Motion_propGetVariant_guess`均存在；最终IDB session count为0。

## 8. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过，随后no-work复验通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`：通过；
- `git diff --check`（三个本轮源码/测试文件）：无whitespace error；
- x64 Catch2执行树仍需构建整套缺失的native third-party ports；本轮没有把该外部依赖重建当成
  源码失败，测试TU已由ordinary/headless完整编译覆盖。

产物：

- Web `index.wasm`：85,655,252 bytes；FUNCTION `0x1BD31`；CODE `0x1A41057`；
  DATA `0x5A3E40`；name `0x3185F7B`；SHA-256
  `891B9C2C3604064B320308F97BD1FAC763A6E1EB34CEB1F3DC26E38ADFF9C110`；
- Wasmtime `index.wasm`：85,002,393 bytes；FUNCTION `0x1BA50`；CODE `0x19E9005`；
  DATA `0x5A1090`；name `0x3141E11`；SHA-256
  `3CE70D2FE4FBBA56A2E42B33A5FEFD2BD12581D64CE62F1D5BD11AB4659D3D26`；
- `krkr2_wasmtime_guest.wasm`：151,478,490 bytes；SHA-256
  `416A3CB4E9E2AA0CA93224C98464DCCCD6D76B7F639C08EC7BDE3E395A470E4E`。

## 9. 下一边界

V245继续ordinary Canvas per-item外层normal/exception tail：debug frame之后source accessor raw owner、
resolved source Variant、color accessor、descriptor accessor的精确逆序清理，以及direct branch的
`continue`是否在这些外层owner清理之前/之后形成独立cleanup landing pad。重点锁定每个frame
callback异常时已清理与仍存活的owner集合，并复核当前注释是否把C++ scope和编译器共享landing
pad混为一谈。
