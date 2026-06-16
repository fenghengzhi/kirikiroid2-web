# KAGParser `taglist` 缺失 → `[ev]` 对象命令路径失效（yuzulogo/m2logo 不加载）

> 结论状态：**已端到端证实**（`.so` 反编译 + 游戏 TJS 脚本 + 运行时日志三向交叉）。
> 现象：dracu_boot 启动 logo 序列里 `[ev storage='yuzulogo.psb']` / `[ev storage='m2logo.psb']` 不显示。
> 根因：Web 版 `KAGParser` 未在 GetNextTag 返回的 tag 字典上构造 `taglist` 成员，导致游戏脚本
> `KAGEnvironment.getCommandTarget` 的 env-object 分支被 `taglist===void` 门挡死，返回 `null`。

---

## 1. 因果链（三向证据）

### (1) `.so` —— Android KAGParser 在返回的 tag 字典上写 `taglist`
- 写 `taglist` 的小函数：`sub_54F438 @ 0x54F438`、`sub_568F88 @ 0x568F88`。
  - 二者等价：`ttstr_createFromWide(L"taglist")` 做 key（UTF-16LE，故 IDA ASCII `find` 搜不到），
    以 flag `0x200`（`TJS_MEMBERENSURE`）调 dispatch 的 `PropSetByVS`（vtable `+80`）把
    `dict.taglist = <object>`（a1[1]，一个 TJS 对象）写入。
- 调用方（GetNextTag 一族，巨型函数）：
  - `sub_54F438` ← `sub_550A74 @ 0x550A74`（0x5b5c）
  - `sub_568F88` ← `sub_561F3C @ 0x561F3C`（0x704c）
- `sub_561F3C` 的返回对象经 native 方法包装 `sub_55B864 @ 0x55B864` 打包成 TJS 方法 result variant
  （`sub_55B864` 内：`v5 = sub_561F3C(...)` → 作为返回值变体回填）。即 **`sub_561F3C` 返回的就是
  GetNextTag 的 tag 字典，`taglist` 写在该字典上**。
- 旁注：`sub_561F3C` 中 `taglist` 写入点紧邻一个 `ttstr_createFromWide("ch")` + `0x200` 的成员写
  （0x562b74 一带）。实装前需用 ida-deep-analyzer 把 `sub_561F3C`/`sub_550A74` 与本地
  `tTJSNI_KAGParser::_GetNextTag` 做精确映射，确认 `taglist` 该挂在哪个返回路径/字典上。

### (2) 本地 —— 未构造 taglist
- `cpp/core/base/KAGParser.cpp:2451`（`_GetNextTag` 属性解析循环）只 `DicObj->PropSetByVS(...)`
  写每个属性，**从不构造/写 `taglist`**。→ 返回的 `DicObj.taglist === void`。

### (3) 游戏脚本 —— `getCommandTarget` 把 env-object 路径 gate 在 taglist 上
文件：`reference/xp3/dracu_boot/DRACU-RIOT/patch/KAGEnvironment.tjs`
（patch/ 覆盖 data/system/；Android 与 Web 跑同一份脚本字节码）。

`getCommandTarget(tagName, elm)`（line 2549）对 `"ev"` 的执行路径：
```
switch("ev")                       // 非 env/object，跳过
obj = getEnvObject("ev")           // 不传 elm → objects["ev"] 未注册 → void
if (elm["class"]!==void || elm.init!==void || isExistEnvObject("ev")) {   // "ev" 存在 → true
    if (elm.taglist !== void && elm.taglist.count > 1)   // line 2571 —— 关键门
        return getEnvObject(tagName, elm);               // 注册并返回 env 对象（会加载 PSB）
    else
        return null;                                     // ← Web 命中这条
}
```
`elm.taglist` 语义（line 1671 / 1884）：**按源码顺序的属性名有序数组**
（`var names = elm.taglist; if (names===void) names = Scripts.getObjectKeys(elm)`；
迭代时 `case "tagname": case "taglist": break` 跳过自身）。
`count > 1` = 该标签除 tagname 外**确实带了参数**。

同样的 taglist 门还出现在 line 2616/2625/2634（`.stand`/`.stage`/`.layer` 自动 env 对象路径）。

### (4) 运行时日志佐证
`.debugtmp/dracu_taglist_logo.log`：
- line 141：`tag='ev' storage='yuzulogo.psb' ... taglist='<void>'`
- line 143：`getCommandTarget("ev",...) result=<object:0x0>`
  —— `<object:0x0>` 是 **`null`**（不是 `<void>`），精确命中 line 2571 的 `return null` 分支，
  而非函数末尾的 `return void`。→ ev 永不进 env 对象路径，yuzulogo/m2logo 不加载。
- line 242–247：`title.psb` 走 `[motionload]` 独立路径 → `Player::playCompat`，与 getCommandTarget 无关，
  故 title 正常。

---

## 2. 对照排查中曾出现的误判（已纠正，勿复犯）

- **误判**："init 在 taglist=void 下 getCommandTarget 仍返回有效对象（log line 181），所以 taglist 不是判别因子。"
  - **纠正**：脚本证明 `init` 走的是 `envCommands`/`stages`/`isExistAction` 分支
    （返回 `this`/`defaultTarget`，**根本不检查 taglist**），而 `ev` 走 `isExistEnvObject` 分支才 gate 在 taglist。
    两者不同分支，init 的成功**不能**用来否证 taglist 因果。
- 裸 `[ev]`（无参，log line 144/150）即便实装 taglist 后 `count==1` 也应 `return null`（不显示）——这是**正确**行为，
  实装时不要为了"让 ev 都生效"而破坏 `count > 1` 门。

---

## 3. 修复方向（实装要点）

1. 在 `_GetNextTag` 解析每个属性时，按**源码顺序**把属性名 push 进一个 TJS `Array`（taglist），
   连同 `"tagname"`（以及脚本所跳过的 `"taglist"` 自身项）一并构成。
2. 解析结束后把该 `Array` 以 key `"taglist"` 写进返回的 `DicObj`（`TJS_MEMBERENSURE`）。
3. `count` 必须真实反映属性个数：带参标签 `count>1`，裸标签 `count==1`。
4. 精确容器选型/写入点/值对象身份须对照 `sub_561F3C`（及 sibling `sub_550A74`）复刻，
   不要用"功能等价"的 std 容器或简化构造糊弄；先做 `.so`↔本地映射再动手。

## 3.5 实装与验证（已完成）

### 反编译确认的架构（sub_561F3C @0x561F3C）
- `*(parser+16)` = `DicObj`（结果字典）；`*(parser+24)` = `TagList`（独立 Array，与 DicObj 并行维护）。
- 缓存方法对象：`qword_1AB3C18` = Array.add；`qword_1AB3C08` = TagList 的 clear；`qword_1AB3BF8` = DicObj 的 clear。
- 每轮 parse loop 顶部 clear DicObj 的同时 clear TagList（0x562308 一带，与 DicClear 成对）。
- **每次往 DicObj 写一个成员 → 同步 `TagList.add(键名字符串)`**（add 站点 0x567e08/0x562eac/0x562f40/0x563384/0x562c64/0x562cf4/0x564280/0x563c70，objthis 全为 `*(parser+24)`，add 的值是 `v948` = 键名 ttstr）。
- 返回前 `sub_568F88(parser+16)` 设 `DicObj.taglist = TagList`（0x567e14/0x567e20/0x564c10）。
- 例外（未复刻、已标注缺口）：宏全量展开 `[tag *]`（本地 KAGParser.cpp 的 `DicAssign` 路径）下，.so 改用 `TJSCreateArrayObject @0x5672dc` 枚举 DicObj 成员重建 taglist；本次只复刻主增量机制，宏-* 路径 taglist 暂留旧值（罕见路径，注释/此处标注）。

### 本地实装（cpp/core/base/KAGParser.{h,cpp}）
- 新增成员 `TagList`/`TagListClear`/`TagListAdd`；ctor 中 `TJSCreateArrayObject(&arrayclass)` 并取 "clear"/"add"，Invalidate 释放。
- 三个辅助：`TagListClearItems()`（loop 顶部，与 DicClear 成对）、`TagListAddName(name)`（每个 DicObj store 后）、`AttachTagList()`（每个 `return DicObj` 前，= sub_568F88）。
- store↔add 配对站点：interrupt(tagname)、行尾[r eol](tagname+eol)、ch(tagname+text)、换行[r](tagname)、普通标签名(tagname)、通用属性循环(attribname, `if(store)` 内)。返回点 4 处（interrupt/line-end/char-or-r/normal）均前置 AttachTagList。

### 构建 + 运行时验证（dracu_boot fixture + diag=taglist trace）
- `cmake --build out/web/debug` 通过，无错误。
- 重跑 trace 对比：
  - `[ev storage='yuzulogo.psb']`（*logo line 87）的 `taglist` 由 `<void>` → `count=N`；
  - `getCommandTarget("ev",...)` 由 `result=<object:0x0>`(null) → `result=<object:0x61f8838>`(有效对象)；
  - 后续进入 env 对象路径：`getImageName("YUZULOGO.PSB")` → `storage.getPlaced.autopath ...data.xp3>motion/yuzulogo.psb result=1` → `getImageData result=<object:0xac1b750>`，**PSB 实际加载**；
  - `stage` 命令目标（原日志同样 0x0）现亦返回有效对象。
- 残留次要现象：`motion_yuzulogo.psb.tjs`（motion 参数 tjs）不存在，走 MOTION-FALLBACK 直接定位 `yuzulogo.psb`——属另一条 motion 参数加载支线，与 taglist 因果无关，PSB 本体已加载。

## 4. 关键地址速查
| 符号 | 地址 | 作用 |
|---|---|---|
| `sub_561F3C` | 0x561F3C | GetNextTag 一族，返回 tag 字典；内部写 taglist |
| `sub_550A74` | 0x550A74 | sibling，同样调 taglist setter |
| `sub_568F88` | 0x568F88 | `dict.taglist = obj` setter（被 561F3C 调） |
| `sub_54F438` | 0x54F438 | 同上（被 550A74 调） |
| `sub_55B864` | 0x55B864 | native 方法包装：把 561F3C 返回对象回填为方法 result |
| 本地 `_GetNextTag` | `cpp/core/base/KAGParser.cpp:2451` | 属性循环；缺 taglist 构造 |
| 脚本 `getCommandTarget` | `patch/KAGEnvironment.tjs:2549`（门 @2571） | env-object 路径 gate 在 `elm.taglist.count>1` |
