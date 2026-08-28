# MotionPlayer callback 重入、owner clear、容器增长与对象替换四参考横向审计

日期：2026-08-28  
原始任务：`MP-B07`

## 1. 结论

四个参考二进制没有全局reentrancy guard、generation counter或“进入回调前复制所有状态”的策略。
每个call site分别决定：

- 哪些dispatch/Variant/string/slot index在回调前取得owning snapshot；
- 哪些node/item/source/controller/listener/texture pointer只是borrowed；
- 回调后哪些字段重新从live object读取；
- 哪些容器循环重新读取size/end，哪些继续使用旧iterator/reference；
- owner被clear/replaced后，旧对象由snapshot继续保活还是立即成为dangling boundary。

因此callback重入的正确复刻不是“一律安全”也不是“一律UB”。参考中同时存在稳定snapshot、live
reread、deque element reference稳定但iterator失效、vector growth全失效、unordered rehash iterator失效
但node reference通常保留、list current-node erase失效，以及raw native pointer无owner的尖锐路径。

现有production实现已经按纵切面保留这些不同策略，没有发现task-local偏差。本轮不做production edit。

## 2. 本轮 fresh 四端证据

本轮使用原生`mcp__idalib__*`对64个callback/reentry代表范围重新执行decompile、完整disassembly、
strings/constants/callees和`xrefs_to/from`。所有decompile成功，所有disassembly均未截断。

| 平台 | 独立范围 | 完整指令 | `xrefs_to` | `xrefs_from` | IDB 更新 |
|---|---:|---:|---:|---:|---|
| Android arm64 | 16 | 12,880 | 50 | 17 | 16条任务注释、1个书签 |
| Android armv7 | 16 | 8,827 | 37 | 16 | 16条任务注释、1个书签 |
| iOS arm64 | 16 | 7,545 | 44 | 16 | 16条任务注释、1个书签 |
| iOS armv7 | 16 | 10,478 | 41 | 16 | 16条任务注释、1个书签 |
| 合计 | 64 | 39,730 | 172 | 65 | 64条注释、4个书签；四库原位保存 |

Android arm64 Engine progress target位于编译器合并/forward归属范围，入口xref总数因此比通常的每range
一条from多1；本轮按resolved完整309指令范围审计，没有把它误判为额外source-level callback。

## 3. 四端函数映射

| reentry范围 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Player playback | `0x6AF664`，459 | `0x580158`，281 | `0x100107540`，236 | `0x104AE8`，386 |
| loadMotionResult callback | `0x6AE2F0`，504 | `0x57F654`，253 | `0x1001067BC`，225 | `0x103BBC`，356 |
| Player frameProgress | `0x6BE44C`，278 | `0x58A63A`，240 | `0x100113B50`，197 | `0x111556`，238 |
| updateLayers | `0x6B871C`，685 | `0x5856E0`，764 | `0x10010E544`，719 | `0x10BE5C`，821 |
| findSourceForNode | `0x691CC8`，1,191 | `0x570500`，676 | `0x1000F316C`，586 | `0xEF97C`，952 |
| Canvas coordinator/item execution | `0x6C4820`，2,363 | `0x58E2CC`，1,891 | `0x1001186E0`，1,531 | `0x11653C`，2,155 |
| append prepared items | `0x6BF714`，1,507 | `0x58B178`，944 | `0x1001148F8`，820 | `0x1123D8`，1,034 |
| private GLL builder | `0x6DBB18`，761 | `0x59CB20`，671 | `0x10012B7D0`，465 | `0x12A304`，703 |
| D3D deep renderer | `0x6AB39C`，606 | `0x57D3DC`，655 | `0x100104450`，545 | `0x101850`，888 |
| mesh submit callback | `0x69AFE4`，1,829 | `0x575800`，871 | `0x1000F974C`，787 | `0xF685C`，1,035 |
| SourceCache live clear | `0x6A5818`所在combined range，763 | `0x57B018`，30 | `0x100100F10`，29 | `0xFE0D4`，29 |
| D3D listener remove | `0x5311C8`，25 | `0x4952AC`，24 | `0x100233720`，11 | `0x23259A`，9 |
| draw-to-texture envelope | `0x6D3048`，211 | `0x5976AC`，129 | `0x100123970`，138 | `0x122C10`，213 |
| Engine progress | `0x67A3F8`所在resolved range，309 | `0x55FEF0`，95 | `0x1001B4304`，89 | `0x1B3E10`，104 |
| particle system phase | `0x6BC4BC`，1,290 | `0x588A48`，1,234 | `0x100111D08`，1,112 | `0x10F51C`，1,452 |
| recursive module LoadModule | `0x701DE8`，99 | `0x5BA8E8`，69 | `0x10029FDE4`，55 | `0x2A48FC`，103 |

## 4. snapshot、live reread与raw borrow分类

| 家族 | 回调前snapshot | 回调后live read | 仍可能失效的borrow |
|---|---|---|---|
| loadMotionResult | currentDispatch、result Object、motion Object owner | Player flags/后续persistent fields按call site | callback可改live Player，但不改旧owner指向 |
| frameProgress | dt/部分slot index与event temporary | node count、部分eval/Variant字段 | node/event container cursor、raw node pointer |
| updateLayers | selected parameter或局部scalar按helper分别snapshot | 某些loop size、parent/slot/ground fields | node deque cursor、parameter/vector pointer、child pointer |
| findSourceForNode | motion context Variant | live src/icon/path与部分SourceState | live MotionNode、ResourceManager native、cache entries |
| Canvas | Layer Class/target owner | priorDraw在两个gate分别读取、width/height callback后继续live route | PreparedRenderItem/source/Layer native pointer |
| private GLL | priorDraw一次snapshot | source/texture/item字段按阶段live读取 | main item、node/source descriptor、private queue references |
| particle phase | activeSlotIndex、receiver、第一遍最终count在明确位置冻结 | child/player字段依每阶段读取 | child vector/Array native Items、child Player pointer |
| D3D deep | batch key某些字段与texture holder | target/source selector与method状态 | prepared item、target/source native pointer |

不能从一个家族的snapshot推导其他家族。例如private GLL必须冻结priorDraw，而Canvas故意在callback-capable
dimension/builder步骤两侧重读priorDraw；把二者统一为一个local bool会改变Canvas route。

## 5. playback与对象替换

loadMotionResult在调用`onFindMotion`/ResourceManager之前持有`currentDispatch` snapshot；callback重入改写
Player的raw dispatch字段不会改变本次receiver。result若为Object，又取得完整owner并贯穿两个indexed
reads。callback可替换argv/result/live motion字段，但旧Dictionary/Array Object仍由owner保活。

成功解析后canonical motion Variant再CopyRef为initializer snapshot。reentrant property getter替换
Player字段不会把当前initializer重定向到新对象。另一方面flags、label、context和container clear按各自
阶段live提交；没有完整Player snapshot，也不会在callback后检测generation并abort。

owner clear只保证被retain的old dispatch继续活；embedded native Player/Engine raw pointer若没有独立
owner，脚本shell clear/Invalidate后仍可能dangling。reference没有shared_ptr升级。

## 6. frame/update循环与容器增长

frameProgress的pending event/node循环按native cursor和部分live-end规则运行。某些callback后重新读取
node count或persistent Variant，因而append可让新尾进入本轮；另一些循环保留旧iterator/end，same-
container mutation则进入失效边界。不能统一改成入口`size` snapshot，也不能统一每次刷新。

updateLayers在ground/source/property callback周围有精确load frontiers：某些遍历下一iteration重读size，
某些直接持有node/parent/parameter pointer。`std::deque`端部增长通常保留element reference，但可能使
iterator/map cursor失效；clear/erase必使element失效。parameter和child pointer若指向vector元素，grow/
rebuild可整体失效。

本地保留reference所证明的deque/vector/map容器与循环形状，没有为reentry制作全容器copy。

## 7. source解析与live key

findSourceForNode入口CopyRef motion context，但src/icon是live active-slot ttstr引用。spec 2、spec 1和
generic fallback对这些key的复制/重读位置不同：

- callback前已复制的fallback path是snapshot；
- KRKR post-build retry可重读live `source.path`；
- texture/cache callback重入改写node slot后，后续只在明确位置观察新值；
- ResourceManager dispatch owner存在故意泄漏/borrowed边界，不能用RAII“顺手安全化”。

SourceState又是原地partial publication；回调替换object/path时，新旧field可能按call frontier混合，
这是reference可观察行为。

## 8. Canvas、prepared item与private GLL

Canvas先retain Layer Class/target owners，但width/height getter、Layer methods和builder均可同步重入。
第一个priorDraw gate之后发生的callback可改变第二个gate；reference允许一次draw跨两种live状态。

appendPreparedRenderItems递归持有node/item/source raw pointers并调用source resolver、child Player、TJS
accessor。若重入clear/rebuild tree或prepared storage，旧pointer可能失效；reference没有先复制完整tree。
已retain的Variant/texture owner只保活其对象，不自动保活拥有raw pointer的container element。

private GLL恰好相反：入口明确snapshot priorDraw，所有gate使用同一个值；但requireLayerId和source
texture callback仍跨越main item/source borrows。queue clear/growth也按native deque进行，无generation
repair。

## 9. D3D target/source/object replacement

D3D deep renderer接收type-erasedtarget/source getter和method selector。它在batch key变化时flush，再按
阶段取得live target/source/method；callback可reenter并替换这些对象。因此一次renderer call可能用旧
batch snapshot和新selector结果，或让旧prepared pointer失效。

mesh submit对source texture做手工AddRef，保证texture对象本身跨callback存活；geometry vectors、target
callback和post-submit bounds不是由该ref保活。submit抛异常还故意不补Release该source ref，不能用
RAII修复同时宣称reentry等价。

draw-to-texture envelope保留必要outer owners，但内部source/target getter是live的。owner保活和slot
identity稳定是两件事：replacement后old object可活着，同时persistent slot已指向new object。

## 10. SourceCache/listener live遍历

SourceCache clear对live list逐项调用Layer `Invalidate`，callback可append/remove/clear同一list。原生循环
没有snapshot或reentry fence；当前iterator/end是否失效由list mutation精确决定。不能改成先把Layer
owners复制到vector再通知，因为会改变新增项、移除项、重复项和owner时点。

D3DLayer listener list允许duplicates；通知直接遍历live nodes，Remove删除所有matching borrowed
listener pointers。listener callback重入Remove当前/后续node会使active traversal进入native失效边界。
Add/Remove本身不retain listener pointee，也不因duplicate做dedupe。

manager vector与listener list不能混同：vector grow/erase可使后续iterator/reference整体失效；list insert
通常保持其他node稳定，但erase当前node仍失效。现有实现保留对应container种类。

## 11. Engine controller与particle系统

Engine progress依次step live timeline/controller/selector/spring/wind owners并更新variable maps。早期
callback可修改后续entry；same-deque clear或metadata rebuild会使当前iterator/borrow失效。reference没有
把controller列表snapshot为owner vector。

particle phase则有局部冻结：active slot index、particle Array receiver和第一遍完成时count在明确位置
snapshot，第二遍不因重入刷新count。每个child Player/native adaptor仍是borrowed；Array Items替换、child
tree clear或owner teardown可使后续调用stale。局部snapshot不能扩张为全particle graph snapshot。

## 12. module递归重入

LoadModule只在所有Pre/Class/Post registrar成功后向registered set提交key，且没有`loading`marker。
registrar callback在commit前递归加载同一module会再次从完整list开头进入，产生重复registration和
partial side effects；更深递归也没有显式上限。增加once/reentry guard虽“安全”，但不1:1。

AllRegist本身append-only；重复调用还会把相同borrowed registrar pointer再次放入list。module已commit
时最前set gate阻止后续访问，未commit或callback抛异常时重试则再次执行完整重复list。

## 13. 容器失效矩阵

| 容器 | reference中主要用途 | reentry mutation边界 |
|---|---|---|
| deque | nodes、tracks、controllers、private queue、TJS Array Items | end growth可保element reference但invalidate iterator/map cursor；clear/erase销毁element |
| vector | child pointers、options、geometry、active labels、manager list | reallocation使pointer/reference/iterator全失效；erase移动suffix |
| list | SourceCache entries、D3D listeners | insert通常保其他node；erase当前/被借用node失效；pointee多为borrowed |
| unordered_map/set | timeline/variable/cache keys | rehash使iterator失效、node refs通常保留；erase/clear销毁node；无锁并发是data race |
| ordered map/set | layer IDs、SLA maps、D3D texture maps | insert保iterator，erase当前node失效；callback可改tree和owner |
| fixed arrays/pools | spring records、particle固定slot | 地址稳定，但owner replacement/active flag变化仍live可见 |

只有实际call site已证明的borrow形式才决定风险；不能仅凭容器标准保证推断native pointer由谁保活。

## 14. 本地与测试对照

本地对应实现分布在`PlayerTimeline.cpp`、`PlayerFrameProgress.cpp`、`PlayerUpdateLayers.cpp`、
`PlayerResource.cpp`、`PlayerRenderTargets.cpp`、`PlayerRenderItems.cpp`、`PrivateMotionGLL.cpp`、
`MotionRenderBackend.cpp`、`SourceCache.cpp`、`DrawDeviceD3D.cpp`和`ncbind.cpp`。

现有tests已覆盖playback reentrant result/receiver、pending event live-end、particle slot/receiver/count
snapshot、Canvas priorDraw、D3D source/submit owner、listener duplicates/removal、manager mutation和module
loader状态。无法稳定断言的iterator-UB路径不制造portable expectation，只在本报告和IDB保留边界。

## 15. 最终判定

`MP-B07`没有剩余task-local静态差异。owner snapshot、live reread、raw borrow、容器失效、对象replacement
与递归module reentry已完成四端映射；64条IDA任务注释、4个书签已写入并保存到四库。

正式native unit、Web Debug、可控reentrant dispatch注入和cross-reference differential属于`MP-V`阶段。
