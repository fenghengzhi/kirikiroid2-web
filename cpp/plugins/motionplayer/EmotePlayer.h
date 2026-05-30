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
#include "tjs.h"
#include "Player.h"
#include "EmoteEngine.h" // EmoteEngine declared in dedicated header (P0 step 1)

namespace motion {

    enum MaskMode { MaskModeStencil = 0, MaskModeAlpha = 1 };

    enum TimelinePlayFlag {
        TimelinePlayFlagParallel = 1,
        TimelinePlayFlagSequential = 2
    };

    // EmotePlayer — 二进制 EmotePlayer NCB 类(24B native instance,
    //   EmotePlayerNativeInstance_create @ 0x68629C
    //   EmotePlayer_NCB_classInit @ 0x686148)
    // 二进制只注册一个 `finalize` 成员;无 script-facing API。
    // 与 D3DEmotePlayer 是两个完全独立的 NCB 类(无继承关系)——二进制中分别
    // 由 EmotePlayer_NCB_classInit 和 D3DEmotePlayer_ncb_register @ 0x541D98 注册。
    // 字段语义(经反编译 0x68629C/0x6862D0 确认):
    //   +8  = EmoteEngine* payload,懒创建(工厂 0x68629C 置 0)
    //   +16 = ownership/sticky 字节,destroy(0x6862D0) gate `if (+8 && !+16)`:
    //         置位时跳过 delete(对应 ncbind 的 _sticky)
    class EmotePlayer {
    public:
        EmotePlayer() = default;
        explicit EmotePlayer(ResourceManager) {}
        virtual ~EmotePlayer() = default;

    private:
        // 二进制布局: vtable +0 / ptr +8 / byte +16,sizeof = 24
        EmoteEngine *_payload = nullptr; // 二进制 +8 — 懒创建的 EmoteEngine,sub_67F4B8 析构
        bool _owned = false;             // 二进制 +16 — ownership/sticky 标志(destroy gate)
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

    // EmoteObject — 二进制 40B EmoteObject(sub_67DBAC)。+0 ResourceManager,
    // +8 EmoteEngine*, +16.. 已加载 PSB 引用。
    // CLAUDE.md rule: EmoteEngine* is a raw pointer (NOT unique_ptr) with
    // manual new/delete in EmoteObject ctor/dtor — aligned with binary's
    // explicit operator new / operator delete pattern.
    class EmoteObject {
    public:
        explicit EmoteObject(ResourceManager rm);
        ~EmoteObject();

        EmoteObject(const EmoteObject&) = delete;
        EmoteObject& operator=(const EmoteObject&) = delete;

        EmoteEngine &engine() { return *_engine; }
        [[nodiscard]] const EmoteEngine &engine() const { return *_engine; }

        tTJSVariant _module; // 已加载的 PSB(对应二进制 +16.. loadedPSBs)

    private:
        EmoteEngine *_engine = nullptr; // +8 — raw pointer, manual new/delete
    };

    // D3DEmotePlayer — 二进制 D3DEmotePlayer NCB 类(≥56B 独立 native instance,
    //   D3DEmotePlayer_ncb_register @ 0x541D98
    //   D3DEmotePlayer_ncb_registerMembers @ 0x52E504)
    // 持有 +24 EmoteObject 主链 + 壳层字段(+40 baseScale, +44 userScale,
    // +48 visible, +49 smoothing) + 全部 NCB 暴露的 API 方法。
    // 与 EmotePlayer 是两个完全独立的 NCB 类,无继承关系。
    class D3DEmotePlayer {
    public:
        explicit D3DEmotePlayer(ResourceManager rm);
        ~D3DEmotePlayer();

        // --- Properties ---
        void setUseD3D(bool v) { _useD3D = v; }
        [[nodiscard]] bool getUseD3D() const { return _useD3D; }

        void setCompletionType(int v) { player().setCompletionType(v); }
        [[nodiscard]] int getCompletionType() const { return player().getCompletionType(); }

        void setChara(ttstr v) { player().setChara(v); }
        [[nodiscard]] ttstr getChara() const { return player().getChara(); }

        void setMotion(ttstr v) { player().playMotionLike_0x6B2284(v, 0); }
        [[nodiscard]] ttstr getMotion() const { return player().getMotion(); }

        void setMotionKey(ttstr v) { player().setMotionKey(v); }
        [[nodiscard]] ttstr getMotionKey() const { return player().getMotionKey(); }

        void setMaskMode(tjs_int v) { player().setMaskMode(v); }
        [[nodiscard]] tjs_int getMaskMode() const { return player().getMaskMode(); }

        void setOutline(ttstr v) { player().setOutline(v); }
        [[nodiscard]] ttstr getOutline() const { return player().getOutline(); }

        void setPriorDraw(double v) { player().setPriorDraw(v); }
        [[nodiscard]] double getPriorDraw() const { return player().getPriorDraw(); }

        void setFrameLastTime(double v) { player().setFrameLastTime(v); }
        [[nodiscard]] double getFrameLastTime() const { return player().getFrameLastTime(); }

        void setFrameLoopTime(double v) { player().setFrameLoopTime(v); }
        [[nodiscard]] double getFrameLoopTime() const { return player().getFrameLoopTime(); }

        void setLoopTime(double v) { player().setLoopTime(v); }
        [[nodiscard]] double getLoopTime() const { return player().getLoopTime(); }

        void setProcessedMeshVerticesNum(int v) { player().setProcessedMeshVerticesNum(v); }
        [[nodiscard]] int getProcessedMeshVerticesNum() const { return player().getProcessedMeshVerticesNum(); }

        void setSmoothing(bool v) { _smoothing = v; }
        [[nodiscard]] bool getSmoothing() const { return _smoothing; }

        void setMeshDivisionRatio(double v);
        [[nodiscard]] double getMeshDivisionRatio() const { return engine()._meshDivisionRatio; }

        void setQueuing(bool v) { engine()._queuing = v; }
        [[nodiscard]] bool getQueuing() const { return engine()._queuing; }

        void setHairScale(double v) { engine()._hairScale = v; }
        [[nodiscard]] double getHairScale() const { return engine()._hairScale; }

        void setPartsScale(double v) { engine()._partsScale = v; }
        [[nodiscard]] double getPartsScale() const { return engine()._partsScale; }

        void setBustScale(double v) { engine()._bustScale = v; }
        [[nodiscard]] double getBustScale() const { return engine()._bustScale; }

        void setBodyScale(double v) { engine()._bodyScale = v; }
        [[nodiscard]] double getBodyScale() const { return engine()._bodyScale; }

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
        void load(tTJSVariant data);
        tTJSVariant clone();
        void show();
        void hide();
        void assignState();
        void initPhysics();

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

        void setTimeline(ttstr label, bool loop);

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
        tTJSVariant getOuterForce();
        bool contains(double x, double y);
        bool contains(ttstr label, double x, double y);
        static tjs_error containsCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis);

        // Access to internal Player for delegation from NCB methods
        Player &getPlayer() { return player(); }
        const Player &getPlayer() const { return player(); }

    private:
        // 对象链访问器(链在 ctor 即建,析构前恒非空,故无需 null 守卫)
        EmoteObject &obj() { return *_emoteObj; }
        [[nodiscard]] const EmoteObject &obj() const { return *_emoteObj; }
        EmoteEngine &engine() { return _emoteObj->engine(); }
        [[nodiscard]] const EmoteEngine &engine() const { return _emoteObj->engine(); }
        Player &player() { return engine().player(); }
        [[nodiscard]] const Player &player() const { return engine().player(); }

        // 壳层字段(对应 libkrkr2.so D3DEmotePlayer wrapper §2.2)
        float _baseScale = 1.0f;   // +40, finalScale = baseScale * userScale (sub_530260)
        float _userScale = 1.0f;   // +44
        bool _visible = true;      // +48
        bool _smoothing = true;    // +49
        bool _useD3D = false;
        bool _opengl = false;
        bool _drawVisible = true;
        double _drawOpacity = 1.0;

        // 二进制对象链(壳 +24 → EmoteObject +8 → EmoteEngine +1064 → Player)。
        // Player 由链尾的 EmoteEngine 用指针持有,不再 by-value 内嵌。
        // CLAUDE.md 硬规则 + 二进制对齐:native instance 持有 EmoteObject* 为裸指针
        //   (二进制 D3DEmotePlayer_load 0x52FDD4 `operator new(0x28)`,析构
        //    EmoteObject_destroy 0x67F420 `operator delete`),手动 new/delete,
        //   不用智能指针。
        EmoteObject* _emoteObj = nullptr;
    };

    // Inline EmoteEngine::player() definitions — placed after Player is
    // complete (Player.h has been included above) so D3DEmotePlayer's inline
    // accessors that ultimately call engine().player() resolve in headers.
    inline Player& EmoteEngine::player() { return *_player; }
    inline const Player& EmoteEngine::player() const { return *_player; }

} // namespace motion
