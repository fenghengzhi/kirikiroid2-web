//
// Created by LiDon on 2025/9/15.
// libkrkr2.so has TWO independent NCB classes here (no inheritance between them):
//   D3DEmotePlayer chain (the class with the real API):
//     D3DEmotePlayerNativeInstance(24B) → EmoteObject(40B, sub_67DBAC)
//       → EmoteEngine(1496B, sub_67E38C) → Player(1384B, new(0x568)+0x6CED30)
//   EmotePlayer chain (degenerate shell, only `finalize` registered):
//     EmotePlayerNativeInstance(24B, 0x68629C) → +8 EmoteEngine(1496B) directly
//       (same dtor sub_67F4B8 = EmoteEngine_dtor; NO EmoteObject middle layer)
//
#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include "../DrawDeviceD3DIntf.h"
#include "tjs.h"
#include "Player.h"
#include "EmoteEngine.h" // EmoteEngine declared in dedicated header (P0 step 1)

namespace motion {

    enum MaskMode { MaskModeStencil = 0, MaskModeAlpha = 1 };

    enum TimelinePlayFlag {
        TimelinePlayFlagParallel = 1,
        // M11 D-02: binary D3DEmoteModule registers this as
        // `TimelinePlayFlagDifference` (NOT `TimelinePlayFlagSequential`).
        // Value 2 is unchanged; this is a TJS-exposed symbol rename to align
        // with libkrkr2.so sub_52E504 NCB constant.
        TimelinePlayFlagDifference = 2
    };


    // === libkrkr2.so D3DEmotePlayer 对象链(已验证,见
    // analysis/EmotePlayer_Internal_Implementation.md §2.0)===
    // 二进制不是单一扁平对象,而是 4 级独立 operator new 堆对象,指针连接:
    //   D3DEmotePlayerNativeInstance(24B, sub_68629C)
    //     +8  → EmoteObject(~40B, sub_67DBAC)
    //             +8   → EmoteEngine(1496B, sub_67E38C)
    //                      +1064 → Player(1384B, new(0x568)+Player_ctor 0x6CED30)
    // 本地按此拓扑分离(不强求字节级偏移);Player 用指针持有,不再 by-value 内嵌。
    // 注:二进制在 D3DEmotePlayer_load(0x52FDD4) 中懒创建此链;本地当前为构造期
    // 即建(eager),功能等价,懒创建留作后续 fidelity 改进。

    // EmoteEngine class is now declared in EmoteEngine.h (P0 step 1 refactor).

    // EmoteObject — 二进制 40B EmoteObject(EmoteObject_init @0x67DBAC)。字段:
    //   +0  ResourceManager*  (operator new(0xE8)=232B, ctor sub_6A88CC)
    //   +8  EmoteEngine*       (operator new(0x5D8)=1496B, EmoteEngine_ctor)
    //   +16 vector<ttstr> 资源路径数组(ttstrVector_assign_67F0CC)
    //
    // EmoteObject 自持 ResourceManager：与 0x67DBAC 一样在构造体内 new 唯一
    // ResourceManager，并让 sticky NCB adaptor 指向同一对象。析构 @0x67F420
    // 显式执行 Engine -> ResourceManager -> paths，不能用 ResourceManager 值拷贝
    // 或 shared-state 副本代替这条所有权链。
    //
    // CLAUDE.md rule: EmoteEngine* is a raw pointer (NOT unique_ptr) with
    // manual new/delete in EmoteObject ctor/dtor — aligned with binary's
    // explicit operator new / operator delete pattern.
    class EmoteObject {
    public:
        explicit EmoteObject(const std::vector<ttstr> &modulePaths);
        ~EmoteObject();

        EmoteObject(const EmoteObject&) = delete;
        EmoteObject& operator=(const EmoteObject&) = delete;

        EmoteEngine &engine() { return *_engine; }
        [[nodiscard]] const EmoteEngine &engine() const { return *_engine; }

        // 0x67F0CC 对每个 8B 元素直接原子 AddRef/Release，0x67DBAC 随后把
        // 元素传给 ResourceManager_loadResource；0x52FDD4 的 producer 对每个
        // TJS 参数先做 variant→ttstr。因此 +16 的源码容器是 vector<ttstr>，
        // 不是 vector<tTJSVariant> / vector<tTJSVariant*>。
        [[nodiscard]] const std::vector<ttstr> &modulePaths() const {
            return _modulePaths;
        }

    private:
        ResourceManager *_rm = nullptr; // +0 — raw owning pointer, manual delete
        // P3-B (2026-06-05): the RM dispatch facade (binary sub_67E20C wrapper).
        //   EmoteObject creates it once from *_rm and flows it down to EmoteEngine
        //   -> Player -> child Players (RM dispatch-in, @0x6CED30). Declared after
        //   _rm; the dtor clears this sticky facade before manually deleting _rm.
        tTJSVariant _rmDispatch;
        EmoteEngine *_engine = nullptr; // +8 — raw pointer, manual new/delete
        std::vector<ttstr> _modulePaths; // +16 — resource paths, refcounted handles
    };

    // EmotePlayer — 二进制 EmotePlayer NCB 类(24B native instance,
    //   EmotePlayerNativeInstance_create @ 0x68629C
    //   EmotePlayer_NCB_classInit @ 0x686148)
    // CORRECTION (2026-06-03, fresh decompile EmotePlayer_loadClass@0x685BC0):
    //   旧注释"二进制只注册一个 finalize 成员;无 script-facing API"是 **错的**。
    //   0x685BC0 先调 EmotePlayer_NCB_classInit@0x686148(注册 `finalize`),
    //   再调 EmotePlayer_ncb_registerMembers@0x67FAC8,注册完整 70 成员 + 2 常量
    //   (TimelinePlayFlagParallel/Difference)。成员回调多为 Player_*/sub_*,操作
    //   底层 EmoteEngine/Player(progress=sub_6818B4 经 0x67D060 直接调
    //   EmoteEngine_preProgress_guess、
    //   Player_HM2_upsert、bindParameterValue)。即 Motion.EmotePlayer 是套在同一
    //   Player/EmoteEngine 机器上的第二个 NCB facade(与 Motion.Player @0x6D69C8
    //   是两套独立注册面,但语义重叠)。
    // 与 D3DEmotePlayer(DrawDeviceD3D.dll, 0x52E504, 54 成员)是两个完全独立的 NCB
    // 类(无继承关系)——分别由 EmotePlayer_loadClass 和 D3DEmotePlayer_ncb_register
    // @ 0x541D98 注册。三类成员拓扑:EmotePlayer(70)/Player(92)/D3DEmotePlayer(54)。
    // 字段语义(经反编译 0x68629C/0x6862D0 确认):
    //   +8  = EmoteEngine* payload,懒创建(工厂 0x68629C 置 0)
    //   +16 = ownership/sticky 字节,destroy(0x6862D0) gate `if (+8 && !+16)`:
    //         置位时跳过 delete(对应 ncbind 的 _sticky)
    class EmotePlayer {
    public:
        EmotePlayer() = default;
        virtual ~EmotePlayer();

        // ============================================================
        // Motion.EmotePlayer NCB 暴露面 — 对齐 libkrkr2.so
        //   EmotePlayer_ncb_registerMembers @0x67FAC8(70 成员 + 2 常量)。
        // 注册顺序见 main.cpp NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)。
        // 二进制成员回调多为 Player_*/sub_*,操作底层同一 Player/EmoteEngine
        //   机器(progress=sub_6818B4 直接 EmoteEngine_preProgress_guess)。故本地各成员
        //   委托给内部 player()/engine(),与 D3DEmotePlayer wrapper 共用同一
        //   Player 机器(两套独立 NCB facade,语义重叠 = 二进制设计)。
        // ============================================================

        // --- #1-19 Functions ---
        void progress(double dt);                         // #1  sub_6818B4
        void frameProgress(double dt);                    // #2  sub_6817C0
        void draw(tTJSVariant target);                    // #3  Player_draw
        void initPhysics(tTJSVariant metadata);           // #4  sub_67D4D0
        void startWind(double minAngle, double maxAngle, double amplitude,
                       double freqX = 0.0, double freqY = 0.0); // #5 Player_startWind
        void stopWind();                                  // #6  sub_681A38
        bool play(ttstr label, tjs_int flags = 0);        // #7  Player_play
        void clear();                                     // #8  sub_681A64
        double getVariable(ttstr label);                  // #9  Player_getVariable
        bool contains(ttstr label, double x, double y);   // #10 sub_681B0C
        static tjs_error containsCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param, iTJSDispatch2 *objthis);
        tTJSVariant serialize();                          // #11 sub_675E40
        void unserialize(tTJSVariant data);               // #12 sub_678044
        void pass(double dt);                             // #13 sub_681C48
        void setVariable(ttstr label, double value, double transition = 0.0,
                         double ease = 0.0);              // #14 sub_671DF0
        static tjs_error setVariableCompat(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param, iTJSDispatch2 *objthis);
        void setCoord(double x, double y, double transition = 0.0,
                      double ease = 0.0);                 // #15 sub_672060
        static tjs_error setCoordCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param, iTJSDispatch2 *objthis);
        void setScale(double s, double transition = 0.0,
                      double ease = 0.0);                 // #16 sub_67231C
        static tjs_error setScaleCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param, iTJSDispatch2 *objthis);
        void setRotate(double rot, double transition = 0.0,
                       double ease = 0.0);                // #17 sub_672568
        static tjs_error setRotateCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *objthis);
        void setColor(tjs_int color, double transition = 0.0,
                      double ease = 0.0);                 // #18 sub_67277C
        static tjs_error setColorCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param, iTJSDispatch2 *objthis);
        void setOuterForce(ttstr label, double x, double y,
                           double transition = 0.0, double ease = 0.0); // #19 sub_672A78
        static tjs_error setOuterForceCompat(tTJSVariant *result, tjs_int numparams,
                                             tTJSVariant **param, iTJSDispatch2 *objthis);

        // --- #20-33 Properties ---
        void setCompletionType(int v) { player().setCompletionType(v); }   // #20
        [[nodiscard]] int getCompletionType() const { return player().getCompletionType(); }
        void setChara(ttstr v) { player().setChara(v); }                    // #21
        [[nodiscard]] ttstr getChara() const { return player().getChara(); }
        void setMotion(ttstr v) { player().playMotionLike_0x6B2284(v, 0); } // #22
        [[nodiscard]] ttstr getMotion() const { return player().getMotion(); }
        void setMotionKey(tTJSVariant v) { player().setMotionKey(std::move(v)); } // #23
        [[nodiscard]] tTJSVariant getMotionKey() const { return player().getMotionKey(); }
        void setProject(tTJSVariant v) { player().setProject(v); }          // #24
        [[nodiscard]] tTJSVariant getProject() const { return player().getProject(); }
        void setMaskMode(tjs_int v) { player().setMaskMode(v); }            // #25
        [[nodiscard]] tjs_int getMaskMode() const { return player().getMaskMode(); }
        void setMeshDivisionRatio(double v);                                // #26
        [[nodiscard]] double getMeshDivisionRatio() const { return engine()._meshDivisionRatio; }
        void setOutline(ttstr v) { player().setOutline(v); }                // #27
        [[nodiscard]] ttstr getOutline() const { return player().getOutline(); }
        void setPriorDraw(double v) { player().setPriorDraw(v); }           // #28
        [[nodiscard]] double getPriorDraw() const { return player().getPriorDraw(); }
        // EmotePlayer's time getters are a distinct raw-frame API from
        // Motion.Player's millisecond-converting getters: 0x681E94 backs both
        // frameLastTime/lastTime with Player+1128; 0x681EA0 backs both
        // frameLoopTime/loopTime with Player+1136.
        [[nodiscard]] double getFrameLastTime() const { return player().getFrameLastTime(); } // #29 RO
        [[nodiscard]] double getFrameLoopTime() const { return player().getLoopTime(); }      // #30 RO
        [[nodiscard]] double getLastTime() const { return player().getFrameLastTime(); }      // #31 RO
        [[nodiscard]] double getLoopTime() const { return player().getLoopTime(); }            // #32 RO
        [[nodiscard]] tTJSVariant getBounds() const { return player().getBounds(); }          // #33 RO
        [[nodiscard]] int getProcessedMeshVerticesNum() const { return player().getProcessedMeshVerticesNum(); } // #34 RO (sub_681EB4)

        // --- #35 setDrawAffineTranslateMatrix (Function, sub_68C664 desc) ---
        static tjs_error setDrawAffineTranslateMatrixCompat(
            tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
            iTJSDispatch2 *objthis);

        // --- #36-41 Functions (camera / scale-with-args) ---
        tTJSVariant getCameraOffset();                    // #36 sub_681EF0
        void setCameraOffset(double x, double y);         // #37 sub_681EF8
        // #38 modifyRoot @0x681F0C: engine+1064 -> player+200 -> root+1584 = 1.
        void modifyRoot();                                // #38 sub_681F0C
        // #39-41 setHairScale/setPartsScale/setBustScale write engine spring
        //   target fields +1184/+1192/+1200 (sub_681F20/28/30), NOT the
        //   _hairScale/_partsScale/_bustScale (+1080) fields. The hairScale/
        //   bustScale/partsScale PROPERTIES (#42-44) read these SAME fields.
        void setHairScale(double v) { engine()._bustSpring1Const = v; }    // #39 -> +1184
        void setPartsScale(double v) { engine()._bustSpring2Const = v; }   // #40 -> +1192
        void setBustScale(double v) { engine()._scalarField_1200_1d = v; } // #41 -> +1200

        // --- #42-44 Properties (read SAME +1184/+1192/+1200 fields) ---
        void setHairScaleProp(double v) { engine()._bustSpring1Const = v; }    // #42 hairScale -> +1184
        [[nodiscard]] double getHairScale() const { return engine()._bustSpring1Const; }
        void setBustScaleProp(double v) { engine()._scalarField_1200_1d = v; } // #43 bustScale -> +1200
        [[nodiscard]] double getBustScale() const { return engine()._scalarField_1200_1d; }
        void setPartsScaleProp(double v) { engine()._bustSpring2Const = v; }   // #44 partsScale -> +1192
        [[nodiscard]] double getPartsScale() const { return engine()._bustSpring2Const; }
        // #45-48: four EmoteEngine byte-flag properties. Binary setters are
        //   "set-always-1 trigger" semantics (write field=1 regardless of arg):
        //   debugPrint=engine+1163(sub_681F50/58), queuing=engine+1161
        //   (sub_681F64/6C), directEdit=engine+1159(sub_681F78/80),
        //   selectorEnabled=engine+1160(sub_681F8C/94, setter also runs
        //   sub_670D1C). NOT Player-backed and NOT local shell bools.
        void setDebugPrint(bool) { engine()._debugPrintFlag = true; }       // #45 -> +1163
        [[nodiscard]] bool getDebugPrint() const { return engine()._debugPrintFlag; }
        void setQueuing(bool) { engine()._emoteAnimatorFlag = true; }       // #46 -> +1161
        [[nodiscard]] bool getQueuing() const { return engine()._emoteAnimatorFlag; }
        void setDirectEdit(bool) { engine()._syncWaiting = true; }          // #47 -> +1159
        [[nodiscard]] bool getDirectEdit() const { return engine()._syncWaiting; }
        // #48 selectorEnabled setter @0x681F94 ignores the input, writes the
        //   +1160 byte to 1, then synchronizes through sub_670D1C.
        void setSelectorEnabled(bool) {
            engine()._selectorEnabled = true;
            engine().syncSelectorControlsLike_0x670D1C();
        }
        [[nodiscard]] bool getSelectorEnabled() const { return engine()._selectorEnabled; }
        // #49 variableKeys @0x681FA0: a single tTJSVariant CopyRef from
        //   EmoteEngine+1208. resetMetadataState creates this owning Array.
        [[nodiscard]] tTJSVariant getVariableKeys() {
            return engine()._variableLabelsBase;
        } // #49 RO
        [[nodiscard]] bool getAnimating() const { return player().getAllplaying(); }             // #50 RO

        // --- #49-68 Functions ---
        void setMirror(bool mirror);                      // #49 sub_671DB0
        void skip();                                      // #50 sub_66EB8C
        void playTimeline(ttstr label, tjs_int flags);    // #51 sub_672E44
        void stopTimeline(ttstr label);                   // #52 sub_681FAC
        bool getTimelinePlaying(ttstr label);             // #53 sub_68209C
        void setTimelineBlendRatio(ttstr label, double ratio); // #54 sub_6821B0
        void fadeInTimeline(ttstr label, double duration, tjs_int flags);  // #55 sub_6736EC
        void fadeOutTimeline(ttstr label, double duration, tjs_int flags); // #56 sub_6739F4
        double getTimelineBlendRatio(ttstr label);        // #57 sub_6821C8
        tTJSVariant getVariableRange(ttstr label);        // #58 sub_673BEC
        tTJSVariant getVariableFrameList(ttstr label);    // #59 sub_68229C
        tTJSVariant getMainTimelineLabelList();           // #60 sub_674F54
        tTJSVariant getDiffTimelineLabelList();           // #61 sub_6750C0
        tTJSVariant getLoopTimeline(ttstr label);         // #62 sub_67522C
        double getTimelineTotalFrameCount(ttstr label);   // #63 sub_6753F0
        tTJSVariant getPlayingTimelineInfoList();         // #64 sub_6754C4
        bool isSelectorTarget(ttstr label);               // #65 sub_6823FC
        void activateSelectorTarget(ttstr label);         // #66 sub_67581C
        void deactivateSelectorTarget(ttstr label);       // #67 sub_675BF4
        tTJSVariant getCommandList();                     // #68 sub_682520

    private:
        // 对象链访问器:与 D3DEmotePlayer 同构(reach 同一 Player 机器)。
        // 二进制 EmotePlayer native instance +8 = EmoteEngine 直接(无 EmoteObject
        // 中间层);本地复用 EmoteObject 链以 reach Player —— 该 ABI 偏移差异
        // 是平台必然(CLAUDE.md 字节布局方法论:对齐源码语义,非 packed 偏移)。
        EmoteEngine &engine() { return _primaryObj->engine(); }
        [[nodiscard]] const EmoteEngine &engine() const { return _primaryObj->engine(); }
        Player &player() { return engine().player(); }
        [[nodiscard]] const Player &player() const { return engine().player(); }

        EmoteObject *_primaryObj = nullptr; // 二进制 +8 EmoteEngine 链(本地经 EmoteObject)
        // debugPrint/directEdit/selectorEnabled/queuing are EmoteEngine byte
        //   flags (+1163/+1159/+1160/+1161), NOT shell bools — see accessors.
    };

    // D3DEmotePlayer — 二进制 D3DEmotePlayer NCB 类(≥56B 独立 native instance,
    //   D3DEmotePlayer_ncb_register @ 0x541D98
    //   D3DEmotePlayer_ncb_registerMembers @ 0x52E504)
    // 持有 +24 EmoteObject 主链 + 壳层字段(+40 baseScale, +44 userScale,
    // +48 visible, +49 smoothing) + 全部 NCB 暴露的 API 方法。
    // 与 EmotePlayer 是两个完全独立的 NCB 类,无继承关系。
    class D3DEmotePlayer : public D3DLayerListener {
    public:
        explicit D3DEmotePlayer(D3DLayerObject *d3dImageOwner);
        ~D3DEmotePlayer() override;

        static tjs_error factory(D3DEmotePlayer **result,
                                 tjs_int numparams,
                                 tTJSVariant **param,
                                 iTJSDispatch2 *objthis);

        // --- Properties ---
        void setUseD3D(bool v) { _useD3D = v; }
        [[nodiscard]] bool getUseD3D() const { return _useD3D; }

        void setCompletionType(int v) { player().setCompletionType(v); }
        [[nodiscard]] int getCompletionType() const { return player().getCompletionType(); }

        void setChara(ttstr v) { player().setChara(v); }
        [[nodiscard]] ttstr getChara() const { return player().getChara(); }

        void setMotion(ttstr v) { player().playMotionLike_0x6B2284(v, 0); }
        [[nodiscard]] ttstr getMotion() const { return player().getMotion(); }

        void setMotionKey(tTJSVariant v) { player().setMotionKey(std::move(v)); }
        [[nodiscard]] tTJSVariant getMotionKey() const { return player().getMotionKey(); }

        void setMaskMode(tjs_int v) { player().setMaskMode(v); }
        [[nodiscard]] tjs_int getMaskMode() const { return player().getMaskMode(); }

        void setOutline(ttstr v) { player().setOutline(v); }
        [[nodiscard]] ttstr getOutline() const { return player().getOutline(); }

        void setPriorDraw(double v) { player().setPriorDraw(v); }
        [[nodiscard]] double getPriorDraw() const { return player().getPriorDraw(); }

        // (A2) setFrameLastTime delegate removed: `frameLastTime` is RO in the
        // binary (= +1128 motion["lastTime"], no setter).
        [[nodiscard]] double getFrameLastTime() const { return player().getFrameLastTime(); }

        // R1.H2: setFrameLoopTime/getFrameLoopTime delegates removed —
        // backing field `_frameLoopTime` was a port-invented duplicate of
        // _frameTickCount on +1120. The NCB property `frameLoopTime` is
        // rebound directly to setLoopTime/getLoopTime (+1136) in main.cpp.

        void setLoopTime(double v) { player().setLoopTime(v); }
        [[nodiscard]] double getLoopTime() const { return player().getLoopTime(); }

        void setProcessedMeshVerticesNum(int v) { player().setProcessedMeshVerticesNum(v); }
        [[nodiscard]] int getProcessedMeshVerticesNum() const { return player().getProcessedMeshVerticesNum(); }

        void setSmoothing(bool v) { _smoothing = v; }
        [[nodiscard]] bool getSmoothing() const { return _smoothing; }

        void setMeshDivisionRatio(double v);
        [[nodiscard]] double getMeshDivisionRatio() const { return engine()._meshDivisionRatio; }

        // FIX 2026-06-04: was routing to player().setQueuing (Player+480) per a
        // falsified class-layout-auditor note. Fresh decompile of the actual NCB
        // callbacks D3DEmotePlayer_setQueing@0x5300dc / getQueing@0x5300cc
        // (member key L"queing") shows they read/write the EmoteEngine byte flag
        // @engine+1161, set-always-1 (setter writes constant 1, ignores arg) —
        // the SAME +1161 field EmotePlayer::setQueuing uses (_emoteAnimatorFlag).
        // (IDB labels corrected in commit 222b176; this local code now matches.)
        void setQueuing(bool) { engine()._emoteAnimatorFlag = true; }       // +1161 set-always-1
        [[nodiscard]] bool getQueuing() const { return engine()._emoteAnimatorFlag; }

        void setHairScale(double v) { engine()._hairScale = v; }
        [[nodiscard]] double getHairScale() const { return engine()._hairScale; }

        void setPartsScale(double v) { engine()._partsScale = v; }
        [[nodiscard]] double getPartsScale() const { return engine()._partsScale; }

        // D3DEmotePlayer NCB member L"bustScale" reads the engine +1200 double
        // (_scalarField_1200_1d) — verified get/setBustScale @0x530130/0x530140
        // in libkrkr2.so sub_52E504, the SAME field the Player class #43 exposes.
        // The former local getBodyScale (engine._bodyScale) was a behaviour-guess
        // name with no +1200 backing; renamed here to match the binary member key.
        // The earlier engine._bustScale-reading getBustScale was the bogus `queing`
        // binding (queing is a byte flag, see setQueuing) and is removed.
        // NOTE: sibling members hairScale(+1184)/partsScale(+1192) still read the
        // port _hairScale/_partsScale (+1080) shadows above — same misalignment,
        // left for a follow-up (out of this change's scope).
        void setBustScale(double v) { engine()._scalarField_1200_1d = v; }
        [[nodiscard]] double getBustScale() const { return engine()._scalarField_1200_1d; }

        void setVisible(bool v);
        [[nodiscard]] bool getVisible() const { return _visible; }

        [[nodiscard]] bool getAnimating() const;

        void setProgress(double v) { engine()._progress = v; }
        [[nodiscard]] double getProgress() const { return engine()._progress; }

        void setModified(bool v) { engine()._modified = v; }
        [[nodiscard]] bool getModified() const { return engine()._modified; }

        void setDrawVisible(bool v) { _drawVisible = v; }
        [[nodiscard]] bool getDrawVisible() const { return _drawVisible; }

        void setDrawOpacity(double v) { _drawOpacity = v; }
        [[nodiscard]] double getDrawOpacity() const { return _drawOpacity; }

        void setOpengl(bool v) { _opengl = v; }
        [[nodiscard]] bool getOpengl() const { return _opengl; }

        void setModule(tTJSVariant v);
        [[nodiscard]] tTJSVariant getModule() const;

        [[nodiscard]] bool getPlayCallback() const { return engine()._playCallback; }

        // --- Methods ---
        void create();
        void load(const std::vector<ttstr> &modulePaths);
        static tjs_error loadCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param,
                                    iTJSDispatch2 *objthis);
        tTJSVariant clone();
        void show();
        void hide();
        void assignState(tTJSVariant state);

        void setRot(double rot, double transition = 0.0,
                    double ease = 0.0);
        static tjs_error setRotCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis);
        double getRot();

        void setCoord(double x, double y, double transition = 0.0,
                      double ease = 0.0);
        static tjs_error setCoordCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);
        void setScale(double s, double transition = 0.0,
                      double ease = 0.0);
        static tjs_error setScaleCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);
        double getScale();
        void setMirror(bool mirror);
        void setColor(tjs_int color, double transition = 0.0,
                      double ease = 0.0);
        static tjs_error setColorCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);
        tjs_int getColor();

        tjs_int countVariables();
        ttstr getVariableLabelAt(tjs_int idx);
        tjs_int countVariableFrameAt(tjs_int idx);
        ttstr getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx);
        double getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx);

        void setVariable(ttstr label, double value, double transition = 0.0,
                         double ease = 0.0);
        static tjs_error setVariableCompat(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *objthis);
        double getVariable(ttstr label);

        void startWind(double minAngle, double maxAngle, double amplitude,
                       double freqX = 0.0, double freqY = 0.0);
        static tjs_error startWindCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *objthis);
        void stopWind();
        static tjs_error stopWindCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);

        tjs_int countMainTimelines();
        ttstr getMainTimelineLabelAt(tjs_int idx);
        tjs_int countDiffTimelines();
        ttstr getDiffTimelineLabelAt(tjs_int idx);
        tjs_int countPlayingTimelines();
        ttstr getPlayingTimelineLabelAt(tjs_int idx);
        tjs_int getPlayingTimelineFlagsAt(tjs_int idx);

        bool isLoopTimeline(ttstr label);
        tjs_int getTimelineTotalFrameCount(ttstr label);
        void playTimeline(ttstr label, tjs_int flags);
        bool isTimelinePlaying(ttstr label);
        void stopTimeline(ttstr label);

        void setTimelineBlendRatio(ttstr label, double ratio);
        double getTimelineBlendRatio(ttstr label);
        void fadeInTimeline(ttstr label, double duration, tjs_int flags);
        void fadeOutTimeline(ttstr label, double duration, tjs_int flags);

        // D3DEmotePlayer_setTimeline @0x5308A4: four-instruction receiver thunk
        // into EmoteEngine::setTimelineBlendLike_0x6735AC. The three floating
        // arguments pass through unchanged.
        void setTimeline(ttstr label, bool autoStop, float value,
                         float transition, float easingWeight);

        bool play(ttstr label, tjs_int flags = 0);
        void draw(tTJSVariant target);
        static tjs_error setDrawAffineTranslateMatrixCompat(
            tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
            iTJSDispatch2 *objthis);

        void skip();
        void addPlayCallback();
        void pass(double dt);
        void progress(double dt);

        void setOuterForce(double x, double y);
        void setOuterForce(ttstr label, double x, double y,
                           double transition = 0.0, double ease = 0.0);
        static tjs_error setOuterForceCompat(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis);
        [[noreturn]] tTJSVariant getOuterForce();
        // M11 D-09 P0: removed AABB `contains(double x, double y)` overload
        // — port invention. binary D3DEmotePlayer::contains @0x530b5c has
        // a single (label, x, y) signature.
        bool contains(ttstr label, double x, double y);
        static tjs_error containsCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);

        // Access to internal Player for delegation from NCB methods
        Player &getPlayer() { return player(); }
        const Player &getPlayer() const { return player(); }

    private:
        // off_19FE020 listener slots used by D3DImage::OnUpdate/Draw.
        bool IsVisible() override;                       // 0x533CBC
        void Draw(iTVPTexture2D *target) override;       // 0x533D4C

        // 对象链访问器:读主槽 _primaryObj(二进制 instance+24)。
        // 二进制 EmoteEngine_progress(0x67D01C 起)无条件解引用主槽 EmoteObject*,
        // 不做 null 检查 —— 靠调用时序保证(clear 后必先 load 再 progress)。
        // 故本地访问器同样不加 null 守卫,与二进制 1:1。
        EmoteObject &obj() { return *_primaryObj; }
        [[nodiscard]] const EmoteObject &obj() const { return *_primaryObj; }
        EmoteEngine &engine() { return _primaryObj->engine(); }
        [[nodiscard]] const EmoteEngine &engine() const { return _primaryObj->engine(); }
        Player &player() { return engine().player(); }
        [[nodiscard]] const Player &player() const { return engine().player(); }

        // 壳层字段(对应 libkrkr2.so D3DEmotePlayer wrapper §2.2)
        float _baseScale = 1.0f;   // +40, finalScale = baseScale * userScale (sub_530260)
        float _userScale = 1.0f;   // +44
        bool _visible = false;     // +48, ctor sub_542764 writes zero
        bool _smoothing = false;   // +49, ctor sub_542764 writes zero
        bool _useD3D = false;
        bool _opengl = false;
        bool _drawVisible = true;
        double _drawOpacity = 1.0;

        // 二进制对象链:D3DEmotePlayer 持有【两个】EmoteObject 槽 —— 主槽
        // instance+24 + 次槽 instance+32(见 analysis/D3DEmotePlayer_56B_layout.md)。
        //   - 主槽 _primaryObj:load/clone 创建,clear/dtor 拆除。指向
        //     EmoteObject(+8 EmoteEngine +1064 Player)。
        //   - 次槽 _secondaryObj:二进制构造 sub_52FFBC 清零、全部已反编译生命周期
        //     路径(construct/load/clear/destroy)只写 0,从不建非 null EmoteObject。
        //     logo 用例下为保留但不激活的退化槽。本地建模为真实槽默认 null。
        // 生命周期语义(二进制):
        //   clear/create 0x52FD84: destroy(次); destroy(主); 主=次=null
        //   load 0x52FDD4:         destroy(次); destroy(主); 主=次=null; 主=new
        //   dtor sub_533C00:       destroy(次); destroy(主)
        // CLAUDE.md 硬规则:EmoteObject* 裸指针 + 手动 new/delete,不用智能指针。
        // sub_42C7F8 maps the class descriptor using off_1A012E0 to the binary
        // literal L"D3DImage". D3DEmotePlayer native-create sub_542764 unwraps
        // precisely that NCB class (dword_1AB2630) and stores the native owner at
        // base+8; no ResourceManager exists on this shell. The owner is a raw
        // non-owning pointer: ctor/clone register this listener through the
        // D3DLayerObject +48 slot, and dtor unregisters through +56 only after
        // both EmoteObject slots have been destroyed (0x542764/0x52FFBC/0x533C00).
        D3DLayerObject *_d3dImageOwner = nullptr;
        EmoteObject* _primaryObj = nullptr;    // instance+24
        EmoteObject* _secondaryObj = nullptr;  // instance+32(保留, 生命周期主链恒 null)
    };

    // Inline EmoteEngine::player() definitions — placed after Player is
    // complete (Player.h has been included above) so D3DEmotePlayer's inline
    // accessors that ultimately call engine().player() resolve in headers.
    inline Player& EmoteEngine::player() { return *_player; }
    inline const Player& EmoteEngine::player() const { return *_player; }

} // namespace motion
