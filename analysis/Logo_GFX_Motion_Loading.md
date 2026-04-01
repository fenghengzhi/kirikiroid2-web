# Logo GFX_Motion.tjs Loading Analysis

## 事实

### 1. libkrkr2.so中没有GFX_Motion相关字符串
通过对libkrkr2.so（arm64二进制）的完整UTF-8和UTF-16搜索确认：
- "GFX_Motion" — 不存在
- "GenericFlip" — 不存在
- "MotionController" — 不存在
- "flipUpdate" — 不存在

**结论：GFX_Motion.tjs、GenericFlip、MotionController全是纯TJS游戏脚本概念，不涉及C++引擎层。**

### 2. AffineSourceMotion.tjs注册了.MTN扩展名
从patch/AffineSourceMotion.tjs的TJS字节码反汇编：
```
AffineSourceMotion.extSourceMap[".PSB"] = AffineSourceMotion
AffineSourceMotion.extSourceMap[".MTN"] = AffineSourceMotion
```
这意味着`.mtn`文件可以通过AffineSourceMotion的extSourceMap系统分发。

### 3. GFX_Motion.tjs存在于sysscn/目录
文件路径：`data/sysscn/GFX_Motion.tjs`

GFX_Motion.tjs定义了`MotionController`类，继承`GenericFlip`和`Motion.Player`。
它通过`GenericFlip.Entry()`注册ext="mtn"，让GenericFlip系统能处理`.mtn`文件。

### 4. 没有任何脚本加载GFX_Motion.tjs
搜索了所有TJS文件的字节码，没有找到任何`Scripts.execStorage("GFX_Motion.tjs")`调用：
- Initialize.tjs — 加载了GFX_Fire/Movie/Flash/Particle/AMovie.tjs，**没有GFX_Motion**
- motion.tjs — 没有
- AffineSourceMotion.tjs — 没有
- AfterInit.tjs — 没有
- patch/ 目录 — 没有

### 5. Logo的KAG调用链（custom.ks）
```
line 45: [addSysHook name="first.logo" call storage="custom.ks" target=*logo]
line 84: *logo
line 89: [ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans]
line 92: [ev storage=m2logo.mtn chara=m2cheeseware_logo motion=back_white notrans]
line 98: [ev file=blandlogo1.png notrans]
```

### 6. `[ev]`是GenericFlip系统的KAG tag
GenericFlip通过Types数组匹配文件扩展名，分发到对应的class。
如果"mtn"扩展名已注册（通过GenericFlip.Entry），[ev]会创建MotionController实例。
如果未注册，[ev]会尝试通过其他已注册的type匹配或静默失败。

## 关键问题

**GFX_Motion.tjs从未被加载，所以GenericFlip不认识"mtn"扩展名。**
但这是原版游戏就有的问题还是web移植引入的问题？

可能性：
1. **原版Android版也不加载GFX_Motion.tjs** — 那Logo在原版是通过其他机制播放的
2. **原版Android版通过某个机制加载了GFX_Motion.tjs** — 需要找到这个机制

由于AffineSourceMotion.tjs已经注册了`.MTN`到extSourceMap，`[ev storage=yuzulogo.mtn]`可能通过extSourceMap被AffineSource系统处理（而不是GenericFlip/MotionController路径）。

## 6. [ev] tag的完整dispatch链（从TJS字节码反汇编确认）

```
[ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans]
  ↓ KAG tag handler
world.tjs: getImageData(params)
  → ext = Storages.extractStorageExt("yuzulogo.mtn") → "mtn"
  → fliptype = GenericFlip.GetType("mtn")
  → return dict { storage: "yuzulogo.mtn", fliptype: fliptype }
  ↓
AffineSourceFlip.loadImages(storage, fliptype)
  → _flipType = fliptype (如果空则再调GenericFlip.GetType)
  → GenericFlip.GetClass(_flipType) → 获取MotionController类
  → _flipStorage = storage
  ↓
AffineSourceFlip.startFlip()
  → _flip = _window.createGenericFlip(_flipType)
  → _flip.flipEntry(this)   // 注册到display chain
  → _flip.flipStart(storage, options) // 开始播放
  ↓
MainWindow.createGenericFlip(flipType)
  → cls = GenericFlip.GetClass(flipType)  // → MotionController
  → new cls(window) → MotionController实例
  ↓
MotionController.flipStart → start(storage)
  → resourceManager.load(storage) // 加载.mtn
  → updateParam(options)          // play(motion, flags)
  ↓
MotionController.flipUpdate(tick) [每帧]
  → targetLayer.clear()
  → this.progress(delta)  // C++ Player::progressCompatMethod
  → this.draw(targetLayer) // C++ Player::drawCompat → renderToLayer
  → this.flipAssign(targetLayer) // copy到display layers
```

## 7. 根因确认

**GFX_Motion.tjs必须被加载**，否则GenericFlip.GetType("mtn")返回空，
createGenericFlip失败（返回void），Logo不显示。

但游戏的Initialize.tjs没有加载GFX_Motion.tjs。可能原因：
1. **GFX_Motion.tjs是后来patch加入的功能** — 但patch.xp3里也没有加载它的代码
2. **原版有某个机制自动加载sysscn/目录下的脚本** — 需要确认
3. **原版kirikiroid2引擎有自动加载GFX扩展脚本的C++代码** — 但libkrkr2.so里没有GFX_Motion字符串

## 8. first.ks解密方式

文件格式：`FE FE 01 FF FE <encrypted UTF-16LE data>`
- Header: 3 bytes `[0xFE, 0xFE, mode=0x01]`
- BOM: 2 bytes `[0xFF, 0xFE]`
- Data: UTF-16LE，每个char16做adjacent bit swap:
  ```
  ch = ((ch & 0xAAAA) >> 1) | ((ch & 0x5555) << 1)
  ```
- 解密代码在 `cpp/core/base/TextStream.cpp` line 157-177

解密后的first.ks内容：
```
@call storage="custom.ks"        ← 注册syshook
*first
[syshook name="first.init"]
[syshook name="first.logo" cond=!SystemConfig.stopSkipOnMessageReceived]
[sysjump from="first" to="title"]
```

## 9. Logo确实被触发了（console证据）

console log确认：
- `motion_yuzulogo.mtn.tjs` 在72489ms被加载
- `playCompat` 被调用，timeline `yuzulogo` 有241帧
- `startSelfDrive` 被调用
- **但92ms后allplaying变成0**（应该4秒才对）
- `drawCompat`和`renderToLayer`从未被调用

**问题不是Logo没触发，而是Logo动画在92ms内就播完了（应该4秒），且SelfDrive handler从未调用drawCompat。**

## 10. 根因确认（从TJS字节码反汇编证实）

### 10.1 AffineSourceMotion.canWaitMovie对非emote返回0（游戏TJS代码证据）

AffineSourceMotion.tjs (patch版) 的 `canWaitMovie` 函数反汇编：
```javascript
canWaitMovie() {
    if (_player != void) {
        if (_storageType == "emote") {
            return _playing;    // emote类型: 检查_playing
        }
    }
    return 0;                   // 非emote类型: 直接返回0 !!!
}
```

Logo的`.mtn`文件`_storageType`不是"emote"，所以canWaitMovie**总是返回0**。

### 10.2 [ev waitmovie]通过canWaitMovie决定是否等待

`[ev waitmovie]` KAG tag handler (KAGEnvImage.tjs) 只设置flags:
```javascript
waitmovie() {
    this.syncMode = 1;
    this.updateFlag = 1;
    this.waitMode = 2;
}
```

然后KAG环境的`createWait`函数 (world.tjs) 检查`waitMode`:
```javascript
createWait(mode) {
    if (mode == 2) {
        if (!this.canWaitMovie) return void;  // 不等待!
        return { name: "movie_world_" + this.name, stopfunc: "stopMovie" };
    }
}
```

当`canWaitMovie`返回0，`createWait`返回void，[ev waitmovie]立即通过。

### 10.3 SelfDrive的canWaitMovie覆盖

我们的`startSelfDrive`确实覆盖了AffineSourceMotion对象的canWaitMovie:
```cpp
obj.canWaitMovie = function(){ return this._playing; }
```
console确认`affineSource=1`（非null，覆盖成功）。

但可能存在**时序问题**：
- `createWait`在`playCompat`之前就被KAG环境的update循环调用了
- 或者`createWait`检查的对象不是被覆盖的那个对象
- 或者world.canWaitMovie的getter用的是不同的对象引用

### 10.4 frameProgress数据证实

console确认Logo的`frameProgress`只被调用了3次，总推进量=0.06帧：
```
dt=0.06, tickCount=0     ← 来自progressCompat(rawDelta=1)
dt=0,    tickCount=0.06   ← dt=0，不推进
dt=0,    tickCount=0.06   ← dt=0，不推进
```

但m2logo的SelfDrive在首次触发时，所有timeline已达到totalFrames (91/91)。
这说明**timeline不是被frameProgress推完的，而是在创建时就已经到达终点**。

### 10.4.1 真正原因：`stopCompat`被调用（日志证实）

console证据:
```
[59469ms] [stopCompat] path=yuzulogo.mtn
[59630ms] [stopCompat] path=m2logo.mtn
```

`setProgressCompat`**未被调用** — timeline不是被setProgress推到终点的。
`stopCompat`**被TJS调用** — 直接stop掉了所有timeline。

### 10.5 完整因果链

```
custom.ks:
  [ev storage=yuzulogo.mtn chara=LOGO motion=yuzulogo notrans]
    → AffineSourceMotion加载yuzulogo.mtn
    → _player.play("yuzulogo") → C++ playCompat → startSelfDrive

  [ev waitmovie]
    → KAGEnvImage设置waitMode=2
    → world.createWait(2) 检查 canWaitMovie
    → AffineSourceMotion.canWaitMovie() → _storageType != "emote" → 返回0
    → createWait返回void → KAG不等待，立即继续 !!!

  [ev storage=m2logo.mtn ...]  ← 立即执行
    → 加载新motion → stop掉yuzulogo → stopCompat被调用
    → yuzulogo的所有timeline.playing = false

  [ev waitmovie]  ← 同样立即通过

  [allimage hide delete notrans sync]  ← 立即执行
    → stop掉m2logo → stopCompat被调用
```

### 10.6 为什么SelfDrive的canWaitMovie覆盖没生效

startSelfDrive确实覆盖了AffineSourceMotion.canWaitMovie = function(){ return this._playing }
console确认 affineSource=1（覆盖成功）。

但问题是**时序**：
1. `_player.play()` → playCompat → startSelfDrive → 覆盖canWaitMovie (60653ms)
2. 覆盖后，AffineSourceMotion._drawAffine被调用，然后TJS层面的env update循环开始
3. 但`[ev waitmovie]`在TJS脚本中**同步执行**——在play()返回后立即执行
4. createWait检查canWaitMovie时，**覆盖已经生效**
5. 但覆盖的函数返回 `this._playing` — 问题是 `_playing` 的值是多少？

startSelfDrive设置了 `_playing = 1`（line 2612），所以canWaitMovie应该返回1。
**但createWait检查的可能不是同一个对象。**

world.tjs的canWaitMovie getter:
```
get canWaitMovie() {
    var target = getWaitTarget();
    return target != void ? target.canWaitMovie : false;
}
```

`getWaitTarget()`返回的对象可能不是startSelfDrive覆盖的那个AffineSourceMotion实例。

### 10.7 修复方向

根因是AffineSourceMotion.canWaitMovie对非emote motion返回0。

修复方案：**让canWaitMovie对所有motion类型都返回`_playing`**，而不仅仅是emote类型。

这可以通过以下方式之一实现：
1. 在Player::playCompat中，**更早**地覆盖canWaitMovie（在play返回TJS之前）
2. 确保覆盖的canWaitMovie作用在正确的对象上
3. 直接修改AffineSourceMotion.tjs的canWaitMovie逻辑（但这是游戏自己的脚本，不应修改）

## 11. 最终根因确认（从TJS字节码反汇编证实）

### 原版Android使用D3DAffineSourceMotion，web版使用AffineSourceMotion

**D3DAffineSourceMotion.canWaitMovie** (data/system/D3DAffineSourceMotion.tjs):
```javascript
canWaitMovie() {
    return this._playing;  // 所有类型都返回_playing
}
```

**AffineSourceMotion.canWaitMovie** (data/system/AffineSourceMotion.tjs):
```javascript
canWaitMovie() {
    if (_player != void && _storageType == "emote") {
        return _playing;  // 仅emote类型
    }
    return 0;  // mtn类型返回0 → Logo不等待
}
```

**更正**：根目录的`patch.tjs`将`useD3D`强制设为0：
```javascript
Plugins.link("motionplayer.dll");
if(typeof Motion.D3DAdaptor == "undefined") {
    with(Motion.Player) { .useD3D = 0; }
} else {
    &Motion.Player.useD3D = 0;
}
```
所以原版Android上`_useD3D`也是false，也使用AffineSourceMotion（不是D3DAffineSourceMotion）。
canWaitMovie在原版Android上也对mtn返回0。

**原版Android上Logo也不通过[ev waitmovie]等待。Logo的显示依赖于AffineSourceMotion._drawAffine的渲染链（continuous handler驱动drawAffine → _player.draw → renderToLayer）。即使waitmovie不等待，只要渲染链在工作，Logo就能在被stop之前显示至少几帧。**

**web版的问题不是canWaitMovie，而是_drawAffine渲染链没工作——drawCompat从未被调用过。**

### 修复方案

在C++层面，当Player::playCompat被调用后，通过TJS覆盖AffineSourceMotion实例的canWaitMovie，让它对所有类型都返回_playing。当前的startSelfDrive已经做了这个覆盖，但需要确认覆盖作用在正确的对象上且时序正确。
