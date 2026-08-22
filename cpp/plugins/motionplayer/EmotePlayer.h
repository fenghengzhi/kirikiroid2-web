//
// Created by LiDon on 2025/9/15.
// The native plugin has two independent NCB classes here (no inheritance):
// D3DEmotePlayer uses D3DEmotePlayerNativeInstance → EmoteObject →
// EmoteEngine → Player; Motion.EmotePlayer's NCB adaptor owns one Engine-sized
// facade allocation directly.
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

    class D3DEmoteModule;

    enum MaskMode { MaskModeStencil = 0, MaskModeAlpha = 1 };

    enum TimelinePlayFlag {
        TimelinePlayFlagParallel = 1,
        // All four current D3DEmotePlayer registrars expose this as
        // `TimelinePlayFlagDifference` (NOT `TimelinePlayFlagSequential`).
        // Value 2 is unchanged; only the TJS-exposed symbol name differs.
        TimelinePlayFlagDifference = 2
    };


    // === D3DEmotePlayer 对象链（四份当前参考已验证）===
    // 二进制不是单一扁平对象,而是 4 级独立 operator new 堆对象,指针连接:
    //   D3DEmotePlayerNativeInstance
    //     → EmoteObject (40B on 64-bit, 20B on 32-bit)
    //         → EmoteEngine
    //             → Player
    // 本地按此拓扑分离(不强求字节级偏移);Player 用指针持有,不再 by-value 内嵌。
    // 四端和本地都由 D3DEmotePlayer::load 懒创建主链；plain shell 构造只注册
    // listener 并把两个 EmoteObject 槽清零。

    // EmoteEngine class is now declared in EmoteEngine.h (P0 step 1 refactor).

    // EmoteObject — 四份当前参考共同确认只有三个成员：
    //   owning ResourceManager*，owning EmoteEngine*，vector<ttstr> paths。
    // 64 位对象为 40B，32 位对象为 20B；各 ABI 的 RM/Engine 大小不同。
    //
    // EmoteObject 在构造体内 new 唯一 ResourceManager，并用仅存在于构造
    // 栈上的 sticky、error=false NCB adaptor Variant 把同一对象传入 Engine；
    // adaptor 始终只借用该 native，创建或类型匹配失败也不回收它。正常析构
    // 严格执行 Engine -> ResourceManager -> paths，不能用值拷贝或
    // shared-state 副本代替这条所有权链。
    //
    // ResourceManager and Engine are deliberately raw owners. All four ctor
    // unwind paths destroy the paths vector and pending, not-yet-constructed
    // allocation only; they do not release either owner after its pointer was
    // published in the object. unique_ptr would repair that native leak edge.
    class EmoteObject {
    public:
        explicit EmoteObject(const std::vector<ttstr> &modulePaths);
        ~EmoteObject();

        EmoteObject(const EmoteObject&) = delete;
        EmoteObject& operator=(const EmoteObject&) = delete;

        // 四端 helper 都按 paths 重建资源对象，再用 Engine state Variant
        // 迁移完整运行态。返回 copy 始终是 raw local：ctor 抛出只 delete
        // pending storage；serialize 抛出泄漏完整 copy；unserialize 抛出仅
        // 析构 state Variant，仍不回收 copy。原始 C++ 精确函数名不可由
        // 产物证明。
        EmoteObject *clone_guess();

        EmoteEngine &engine() { return *_engine; }
        [[nodiscard]] const EmoteEngine &engine() const { return *_engine; }

        // 四端初始化器随后把每个元素传给 ResourceManager::load；外层
        // producer 对每个 TJS 参数先做 variant→ttstr。因此源码容器是 vector<ttstr>，
        // 不是 vector<tTJSVariant> / vector<tTJSVariant*>。
        [[nodiscard]] const std::vector<ttstr> &modulePaths() const {
            return _modulePaths;
        }

    private:
        ResourceManager *_rm = nullptr; // raw owner; ctor failure may leak it
        EmoteEngine *_engine = nullptr; // raw owner; ctor failure may leak it
        std::vector<ttstr> _modulePaths; // resource paths, refcounted handles
    };

    // Motion.EmotePlayer 的 ncbind adaptor 直接拥有一个 Engine-sized payload；
    // 没有 EmoteObject、额外 ResourceManager 或 paths 中间层。四份 typed
    // Factory wrapper 都要求一个 tTJSVariant 参数槽；唯一一项 Void 是 ncbind
    // 创建 empty adaptor shell 的 sentinel，普通零参数返回 BADPARAMCOUNT。
    // 真实调用只复制 arg0 并忽略 surplus。这里用无新增数据、无虚函数的派生
    // facade 复用注册模板，同时保持单堆对象和直接 Engine 所有权。四端均只
    // 生成普通 Engine 析构：adaptor 的 _deleteInstance 显式完成 Engine
    // 析构与 allocation delete；adaptor 自己的 deleting destructor 随后释放
    // adaptor shell，二者不是同一个 deleting-destructor 层级。
    //
    // 它与 D3DEmotePlayer 是两套完全独立的 NCB 类；后者仍经 EmoteObject 链
    // 持有 Engine。原版是否在源码层也采用继承无法由二进制确定。
    class EmotePlayer : public EmoteEngine {
    public:
        explicit EmotePlayer(const tTJSVariant &rmDispatch) :
            EmoteEngine(rmDispatch) {}
        ~EmotePlayer() = default;

        static EmotePlayer *factory(tTJSVariant rmDispatch);

        // ============================================================
        // Motion.EmotePlayer NCB 暴露面：四端均为 70 成员 + 2 常量。
        // 注册顺序见 main.cpp NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer)。
        // 成员回调操作底层同一 Player/EmoteEngine 机器；progress 先把毫秒
        // 换算为 60-fps 帧量，frameProgress 则把帧量直接交给同一个完整
        // Engine progress core。其余成员按注册器的直接目标委托给
        // player()/engine()；这与独立 D3DEmotePlayer facade 的重叠语义一致。
        // ============================================================

        // --- #1-19 Functions ---
        void progress(double milliseconds);               // #1
        // #2 frameProgress binds EmoteEngine::progress directly.
        // #3 is a real Primary wrapper: copy the by-value target once more into
        // the embedded Player's native draw dispatcher.
        void draw(tTJSVariant target);
        // #4 initPhysics binds EmoteEngine::applyMetadata_guess directly.
        // #5 startWind binds EmoteEngine::setWind_guess directly.
        void stopWind();                                  // #6
        void play(ttstr label, tjs_int flags = 0);        // #7  Player_play
        void clear(tTJSVariant target, tTJSVariant fill); // #8 typed draw-to-layer
        // #9 getVariable binds EmoteEngine::getVariable directly.
        bool contains(ttstr label, double x, double y);   // #10 raw-label shape hit
        // #11 serialize and #12 unserialize bind the inherited Engine state
        // methods directly; neither has an EmotePlayer forwarding body.
        void pass();                                      // #13
        // Members 14-19 are native-instance raw callbacks: NCBind resolves the
        // EmotePlayer payload before entering the callback body. Member 14
        // pre-transforms script ease as double; the shared Engine router
        // transforms that result again. D3D's API is direct.
        void setVariable(ttstr label, double value, double transition = 0.0,
                         double ease = 0.0);              // #14
        static tjs_error setVariableCompat(tTJSVariant *result, tjs_int numparams,
                                           tTJSVariant **param,
                                           EmotePlayer *nativeInstance);
        void setCoord(double x, double y, double transition = 0.0,
                      double ease = 0.0);                 // #15
        static tjs_error setCoordCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        EmotePlayer *nativeInstance);
        void setScale(double s, double transition = 0.0,
                      double ease = 0.0);                 // #16
        static tjs_error setScaleCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        EmotePlayer *nativeInstance);
        void setRotate(double rot, double transition = 0.0,
                       double ease = 0.0);                // #17
        static tjs_error setRotateCompat(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         EmotePlayer *nativeInstance);
        void setColor(tjs_int color, double transition = 0.0,
                      double ease = 0.0);                 // #18
        static tjs_error setColorCompat(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        EmotePlayer *nativeInstance);
        void setOuterForce(ttstr label, double x, double y,
                           double transition = 0.0, double ease = 0.0); // #19
        static tjs_error setOuterForceCompat(tTJSVariant *result, tjs_int numparams,
                                             tTJSVariant **param,
                                             EmotePlayer *nativeInstance);

        // --- #20-34 Properties ---
        void setCompletionType(int v) { player().setCompletionType(v); }   // #20
        [[nodiscard]] int getCompletionType() const { return player().getCompletionType(); }
        void setChara(ttstr v) { player().setChara(v); }                    // #21
        [[nodiscard]] ttstr getChara() const { return player().getChara(); }
        void setMotion(ttstr v) { player().playMotion_guess(0, v); } // #22
        [[nodiscard]] ttstr getMotion() const { return player().getMotion(); }
        // #23 and #24 share this exact getter/setter member pair. The setter
        // receives an NCBind-converted string and stores a String Variant in
        // the Player's persistent motion-context slot.
        void setMotionKey(ttstr v) {
            player().setMotionContextVariant_guess(tTJSVariant(v));
        } // #23/#24
        [[nodiscard]] tTJSVariant getMotionKey() const { return player().getMotionKey(); }
        void setMaskMode(tjs_int v) { player().setMaskMode(v); }            // #25
        [[nodiscard]] tjs_int getMaskMode() const { return player().getMaskMode(); }
        // Direct forwarding to the embedded Player scalar. The Engine's
        // metadata/controller scale pair remains independent.
        void setMeshDivisionRatio(double v);                                // #26
        [[nodiscard]] double getMeshDivisionRatio() const {
            return player().getMeshDivisionRatio();
        }
        void setOutline(tTJSVariant v) { player().setOutline(v); }           // #27
        [[nodiscard]] tTJSVariant getOutline() const { return player().getOutline(); }
        void setPriorDraw(bool v) { player().setPriorDraw(v); }             // #28
        [[nodiscard]] bool getPriorDraw() const { return player().getPriorDraw(); }
        // EmotePlayer exposes raw-frame aliases for both name pairs, unlike
        // Motion.Player whose lastTime/loopTime pair converts positive frames
        // to milliseconds. #29/#31 and #30/#32 reuse the exact same getter
        // members rather than separate forwarding aliases.
        [[nodiscard]] double getFrameLastTime() const { return player().getFrameLastTime(); } // #29 RO
        [[nodiscard]] double getFrameLoopTime() const { return player().getFrameLoopTime(); } // #30 RO
        [[nodiscard]] tTJSVariant getBounds() const { return player().getBounds(); }          // #33 RO
        // The recursive core count is uint32, but the Primary wrapper itself
        // returns an Integer Variant and therefore performs the signed tjs_int
        // publication cast before the read-only property descriptor sees it.
        [[nodiscard]] tTJSVariant getProcessedMeshVerticesNum() const {
            return tTJSVariant(static_cast<tjs_int>(
                player().getProcessedMeshVerticesNum()));
        } // #34 RO

        // --- #35 setDrawAffineTranslateMatrix ---
        bool setDrawAffineTranslateMatrix(double m11, double m21,
                                          double m12, double m22,
                                          double m14, double m24);

        // --- #36-41 Functions (camera / scale-with-args) ---
        tTJSVariant getCameraOffset();                    // #36
        void setCameraOffset(double x, double y);         // #37
        void modifyRoot();                                // #38
        // #39-41 methods and #42-44 properties share the same consecutive
        // Engine scale triplet; they do not use facade- or shell-local shadows.
        // The raw double stores do not dirty the Engine, enqueue a controller,
        // or normalize non-finite/signed-zero values.
        void setHairScale(double v) { engine()._hairScale = v; }    // #39
        void setPartsScale(double v) { engine()._partsScale = v; }  // #40
        void setBustScale(double v) { engine()._bustScale = v; }    // #41

        // --- #42-44 Properties (reuse the exact #39-41 setter members) ---
        [[nodiscard]] double getHairScale() const { return engine()._hairScale; }
        [[nodiscard]] double getBustScale() const { return engine()._bustScale; }
        [[nodiscard]] double getPartsScale() const { return engine()._partsScale; }
        // #45-48: four adjacent EmoteEngine byte properties. Their typed
        // Boolean setters never read the converted argument: assigning false,
        // true or Void always stores true. They are not Player fields and are
        // therefore distinct from Player's frame queuing/direct-edit state.
        void setDebugPrint(bool) { engine()._debugPrintFlag = true; }
        [[nodiscard]] bool getDebugPrint() const { return engine()._debugPrintFlag; }
        void setQueuing(bool) { engine()._queuing = true; }
        [[nodiscard]] bool getQueuing() const { return engine()._queuing; }
        void setDirectEdit(bool) { engine()._directEdit = true; }
        [[nodiscard]] bool getDirectEdit() const { return engine()._directEdit; }
        // This trigger additionally performs a selector/variable-label sync on
        // every assignment, including false and repeated true assignments.
        void setSelectorEnabled(bool) {
            engine()._selectorEnabled = true;
            engine().syncSelectorControls_guess();
        }
        [[nodiscard]] bool getSelectorEnabled() const { return engine()._selectorEnabled; }
        // #49: returns an owning Variant CopyRef of the Engine's published
        // variable-key value. It is Void before metadata reset. Afterwards,
        // repeated reads alias the same Array dispatch until metadata reset or
        // selector synchronization replaces the published snapshot.
        [[nodiscard]] tTJSVariant getVariableKeys() {
            return engine()._variableLabelsBase;
        } // #49 RO
        // #50 animating binds EmoteEngine::getAnimating_guess directly.

        // --- #51-70 Functions ---
        // #51 setMirror binds EmoteEngine::setMirror_guess directly.
        // #52 skip binds EmoteEngine::resetControllers_guess directly.
        // #53-58 are native-instance raw callbacks. #53 requires only label
        // and defaults flags to zero; #54/#55 also accept an omitted label.
        // #56-58 require label and have distinct optional blend/fade tails.
        static tjs_error playTimelineRawCallback_guess(
            tTJSVariant *result, tjs_int numparams,
            tTJSVariant **param, EmotePlayer *nativeInstance); // #53
        static tjs_error stopTimelineRawCallback_guess(
            tTJSVariant *result, tjs_int numparams,
            tTJSVariant **param, EmotePlayer *nativeInstance); // #54
        static tjs_error getTimelinePlayingRawCallback_guess(
            tTJSVariant *result, tjs_int numparams,
            tTJSVariant **param, EmotePlayer *nativeInstance); // #55
        static tjs_error setTimelineBlendRatioRawCallback_guess(
            tTJSVariant *result, tjs_int numparams,
            tTJSVariant **param, EmotePlayer *nativeInstance); // #56
        static tjs_error fadeInTimelineRawCallback_guess(
            tTJSVariant *result, tjs_int numparams,
            tTJSVariant **param, EmotePlayer *nativeInstance); // #57
        static tjs_error fadeOutTimelineRawCallback_guess(
            tTJSVariant *result, tjs_int numparams,
            tTJSVariant **param, EmotePlayer *nativeInstance); // #58
        // #59 binds EmoteEngine::getTimelineBlendRatio_guess directly.
        tTJSVariant getVariableRange(ttstr label);        // #60
        tTJSVariant getVariableFrameList(ttstr label);    // #61
        // #62-69 bind the corresponding EmoteEngine timeline/selector
        // members directly; there are no Primary forwarding bodies.
        tTJSVariant getCommandList();                     // #70

    private:
        EmoteEngine &engine() { return *this; }
        [[nodiscard]] const EmoteEngine &engine() const { return *this; }
        Player &player() { return EmoteEngine::player(); }
        [[nodiscard]] const Player &player() const {
            return EmoteEngine::player();
        }

        // The four trigger properties above are Engine-owned bytes, not shell
        // booleans; see their accessor comments for the intentional one-way API.
    };

    static_assert(sizeof(EmotePlayer) == sizeof(EmoteEngine),
                  "Motion.EmotePlayer facade must not add payload storage");

    // Independent D3D shell.  Its listener base owns the non-retaining
    // D3DLayer link; this class owns exactly two raw EmoteObject slots and the
    // scale/visibility state exposed by the native member table.
    class D3DEmotePlayer : public D3DLayerListener {
    public:
        explicit D3DEmotePlayer(D3DLayer *d3dLayerOwner);
        ~D3DEmotePlayer() override;

        // Generated typed Factory: ordinary calls require one D3DLayer; one
        // Void creates only the empty adaptor shell. The successful attach is
        // non-sticky and overwrites an existing native slot without teardown.
        static D3DEmotePlayer *factory(iTJSDispatch2 *objthis,
                                       D3DLayer *d3dLayerOwner);

        // --- Properties ---
        // This is shell-local compatibility state.  Unlike most D3D facade
        // properties it never traverses the lazy EmoteObject/Engine/Player
        // chain, so it remains readable and writable before load and after
        // clear.  The four reference render/update paths have no other read of
        // this byte.
        void setSmoothing(bool v) { _smoothing = v; }
        [[nodiscard]] bool getSmoothing() const { return _smoothing; }

        // The shell traverses EmoteObject -> EmoteEngine -> Player and performs
        // the same raw access as Motion.Player and Motion.EmotePlayer.
        void setMeshDivisionRatio(double v);
        [[nodiscard]] double getMeshDivisionRatio() const {
            return player().getMeshDivisionRatio();
        }

        // The native member name is the historical misspelling `queing`.
        // Its callbacks follow the unchecked primary-object chain to the same
        // Engine append/replace byte as Motion.EmotePlayer.queuing. The typed
        // Boolean argument is ignored: every assignment stores true.
        void setQueuing(bool) { engine()._queuing = true; }
        [[nodiscard]] bool getQueuing() const { return engine()._queuing; }

        void setHairScale(double v) { engine()._hairScale = v; }
        [[nodiscard]] double getHairScale() const { return engine()._hairScale; }

        void setPartsScale(double v) { engine()._partsScale = v; }
        [[nodiscard]] double getPartsScale() const { return engine()._partsScale; }

        // All three D3D scale properties follow shell -> primary EmoteObject ->
        // Engine and read/write the same fields exposed by Motion.EmotePlayer.
        void setBustScale(double v) { engine()._bustScale = v; }
        [[nodiscard]] double getBustScale() const { return engine()._bustScale; }

        // D3D shell compatibility state only. show/hide and this property
        // setter are four-reference leaf stores and remain valid before load
        // and after clear. They do not update Player root visibility; the
        // listener IsVisible() path likewise never reads this byte.
        void setVisible(bool v);
        [[nodiscard]] bool getVisible() const { return _visible; }

        [[nodiscard]] bool getAnimating() const;

        [[nodiscard]] bool getModified() const {
            return player().getRootModified_guess();
        }

        [[nodiscard]] D3DEmoteModule *getModule() const;

        // --- Methods ---
        void clear();
        void load(const std::vector<ttstr> &modulePaths);
        static tjs_error loadCompat(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param,
                                    D3DEmotePlayer *nativeInstance);
        D3DEmotePlayer *clone(D3DLayer *d3dLayerOwner);
        void show();
        void hide();
        void assignState(tTJSVariant state);

        void setRot(double rot, double transition, double ease);
        double getRot();

        void setCoord(double x, double y, double transition, double ease);
        void setScale(double s, double transition, double ease);
        double getScale();
        void setColor(tjs_int color, double transition, double ease);
        tjs_int getColor();

        tjs_int countVariables();
        ttstr getVariableLabelAt(tjs_int idx);
        tjs_int countVariableFrameAt(tjs_int idx);
        ttstr getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx);
        double getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx);

        void setVariable(ttstr label, double value, double transition,
                         double ease);
        double getVariable(ttstr label);

        void startWind(float minAngle, float maxAngle, float amplitude,
                       float freqX, float freqY);
        void stopWind();

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

        double getTimelineBlendRatio(ttstr label);
        void fadeInTimeline(ttstr label, float duration, float easingWeight);
        void fadeOutTimeline(ttstr label, float duration, float easingWeight);

        // The script member named setTimelineBlendRatio is backed by this
        // five-argument receiver thunk; its trailing Boolean is autoStop.
        void setTimeline(ttstr label, float value, float transition,
                         float easingWeight, bool autoStop);

        void skip();
        void passTimelines_guess();
        void progress(double dt);

        void setOuterForce(ttstr label, double x, double y,
                           double duration, double power);
        [[noreturn]] tTJSVariant getOuterForce();
        // The native surface has only the (label, x, y) form; the former AABB
        // overload was a port invention.
        bool contains(ttstr label, double x, double y);

        // Portable test hook only; production D3D members use the private
        // unchecked player() chain below.
        Player &playerForDifferentialTest_guess() { return player(); }
        const Player &playerForDifferentialTest_guess() const {
            return player();
        }

    private:
        // Listener slots invoked by D3DLayer update/draw fan-out.
        bool IsVisible() override;
        void Draw(iTVPTexture2D *target) override;

        // Primary-slot access is deliberately unchecked. The native API relies
        // on load-before-use sequencing after construction or clear.
        EmoteObject &obj() { return *_primaryObj; }
        [[nodiscard]] const EmoteObject &obj() const { return *_primaryObj; }
        EmoteEngine &engine() { return _primaryObj->engine(); }
        [[nodiscard]] const EmoteEngine &engine() const { return _primaryObj->engine(); }
        Player &player() { return engine().player(); }
        [[nodiscard]] const Player &player() const { return engine().player(); }

        // Deliberately raw owner pair. clear(), load replacement and destruction
        // all destroy/delete secondary first and primary second while both slots
        // still contain their old addresses, then zero the pair together. The
        // compiler-generated outer deleting destructor releases the D3D shell
        // only after the listener base is torn down. Per-member unique_ptr reset
        // would change those observable owner-slot windows.
        EmoteObject* _primaryObj = nullptr;
        EmoteObject* _secondaryObj = nullptr;

        // Shell scalar state follows the two owned slots in source order.
        float _baseScale = 1.0f;
        float _userScale = 1.0f;
        bool _visible = false;
        bool _smoothing = false;
    };

    static_assert(sizeof(D3DEmotePlayer) ==
                      (sizeof(void *) == 8 ? 0x38u : 0x24u),
                  "D3DEmotePlayer shell must match the four-reference ABI");

    // Inline EmoteEngine::player() definitions — placed after Player is
    // complete (Player.h has been included above) so D3DEmotePlayer's inline
    // accessors that ultimately call engine().player() resolve in headers.
    inline Player& EmoteEngine::player() { return *_player; }
    inline const Player& EmoteEngine::player() const { return *_player; }

} // namespace motion
