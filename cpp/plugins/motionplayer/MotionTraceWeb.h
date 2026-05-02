#pragma once

namespace motion {
    class Player;
}

namespace motion::detail {

    class MotionTraceProgressScope {
    public:
        MotionTraceProgressScope(Player *player, void *objthis);
        ~MotionTraceProgressScope();

        MotionTraceProgressScope(const MotionTraceProgressScope &) = delete;
        MotionTraceProgressScope &operator=(const MotionTraceProgressScope &) = delete;

    private:
        Player *_player = nullptr;
    };

    void motionTraceRecordUpdatePlayer(Player *player);

#if defined(KRKR2_WASMTIME_HEADLESS)
    class MotionTraceRenderDrawScope {
    public:
        MotionTraceRenderDrawScope(Player *player, void *argVariant,
                                   void *targetObject);
        ~MotionTraceRenderDrawScope();

        MotionTraceRenderDrawScope(const MotionTraceRenderDrawScope &) = delete;
        MotionTraceRenderDrawScope &operator=(const MotionTraceRenderDrawScope &) = delete;

        void setRoute(const char *route);

    private:
        Player *_player = nullptr;
        void *_argVariant = nullptr;
        void *_targetObject = nullptr;
        const char *_route = nullptr;
    };

    class MotionTraceRenderExecuteScope {
    public:
        MotionTraceRenderExecuteScope(Player *player, void *renderLayerObject,
                                      bool skipUpdate);
        ~MotionTraceRenderExecuteScope();

        MotionTraceRenderExecuteScope(const MotionTraceRenderExecuteScope &) = delete;
        MotionTraceRenderExecuteScope &operator=(const MotionTraceRenderExecuteScope &) = delete;

        void setResult(bool ok);

    private:
        Player *_player = nullptr;
        void *_renderLayerObject = nullptr;
        bool _skipUpdate = false;
        bool _ok = false;
    };

    void motionTraceRenderPreparedItems(Player *player, const char *kind,
                                        const char *samplePoint);
    void motionTraceRenderCommands(Player *player, const char *kind,
                                   const char *samplePoint,
                                   int canvasWidth, int canvasHeight);
#endif

} // namespace motion::detail
