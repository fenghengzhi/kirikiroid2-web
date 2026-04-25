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

} // namespace motion::detail
