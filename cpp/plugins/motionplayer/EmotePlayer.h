//
// Created by LiDon on 2025/9/15.
// Reverse-engineered from libkrkr2.so D3DEmotePlayer API surface
//
#pragma once

#include <map>
#include <string>
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ResourceManager.h"

namespace motion {

    enum MaskMode { MaskModeStencil = 0, MaskModeAlpha = 1 };

    enum TimelinePlayFlag {
        TimelinePlayFlagParallel = 0,
        TimelinePlayFlagDifference = 1
    };

    class EmotePlayer {
    public:
        explicit EmotePlayer(ResourceManager rm) {}

        // --- Properties ---
        void setUseD3D(bool v) { _useD3D = v; }
        [[nodiscard]] bool getUseD3D() const { return _useD3D; }

        void setSmoothing(bool v) { _smoothing = v; }
        [[nodiscard]] bool getSmoothing() const { return _smoothing; }

        void setMeshDivisionRatio(double v) { _meshDivisionRatio = v; }
        [[nodiscard]] double getMeshDivisionRatio() const { return _meshDivisionRatio; }

        void setQueuing(bool v) { _queuing = v; }
        [[nodiscard]] bool getQueuing() const { return _queuing; }

        void setHairScale(double v) { _hairScale = v; }
        [[nodiscard]] double getHairScale() const { return _hairScale; }

        void setPartsScale(double v) { _partsScale = v; }
        [[nodiscard]] double getPartsScale() const { return _partsScale; }

        void setBustScale(double v) { _bustScale = v; }
        [[nodiscard]] double getBustScale() const { return _bustScale; }

        [[nodiscard]] bool getAnimating() const { return false; }

        void setProgress(double v) { _progress = v; }
        [[nodiscard]] double getProgress() const { return _progress; }

        void setModified(bool v) { _modified = v; }
        [[nodiscard]] bool getModified() const { return _modified; }

        void setDrawVisible(bool v) { _drawVisible = v; }
        [[nodiscard]] bool getDrawVisible() const { return _drawVisible; }

        void setDrawOpacity(double v) { _drawOpacity = v; }
        [[nodiscard]] double getDrawOpacity() const { return _drawOpacity; }

        void setOpengl(bool v) { _opengl = v; }
        [[nodiscard]] bool getOpengl() const { return _opengl; }

        void setModule(tTJSVariant v) { _module = v; }
        [[nodiscard]] tTJSVariant getModule() const { return _module; }

        // --- Methods ---
        tTJSVariant clone();
        void show();
        void hide();
        void assignState();
        void initPhysics();

        void setRot(double rot);
        double getRot();

        void setCoord(double x, double y);
        void setScale(double s);
        double getScale();
        void setColor(tjs_int color);
        tjs_int getColor();

        tjs_int countVariables();
        ttstr getVariableLabelAt(tjs_int idx);
        tjs_int countVariableFrameAt(tjs_int idx);
        ttstr getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx);
        double getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx);

        void setVariable(ttstr label, double value);
        double getVariable(ttstr label);

        void startWind(double a, double b, double c);
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

        void setTimelineBlendRatio(ttstr label, double ratio);
        double getTimelineBlendRatio(ttstr label);
        void fadeInTimeline(ttstr label, double duration, tjs_int flags);
        void fadeOutTimeline(ttstr label, double duration, tjs_int flags);

        void skip();
        void pass(double dt);

        void setOuterForce(double x, double y);
        tTJSVariant getOuterForce();
        bool contains(double x, double y);

    private:
        bool _useD3D = false;
        bool _smoothing = true;
        double _meshDivisionRatio = 1.0;
        bool _queuing = false;
        double _hairScale = 1.0;
        double _partsScale = 1.0;
        double _bustScale = 1.0;
        double _progress = 0.0;
        bool _modified = false;
        bool _drawVisible = true;
        double _drawOpacity = 1.0;
        bool _opengl = false;
        tTJSVariant _module;

        double _rot = 0.0;
        double _coordX = 0.0;
        double _coordY = 0.0;
        double _scale = 1.0;
        tjs_int _color = 0xFFFFFF;
        double _outerForceX = 0.0;
        double _outerForceY = 0.0;
        bool _visible = true;

        std::map<std::string, double> _variables;
        std::map<std::string, double> _timelineBlendRatios;
    };

    // Thin wrapper for top-level NCB registration (avoids ncbind conflict)
    class D3DEmotePlayer : public EmotePlayer {
    public:
        explicit D3DEmotePlayer(ResourceManager rm) : EmotePlayer(rm) {}
    };

} // namespace motion
