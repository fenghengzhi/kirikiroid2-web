//
// Reverse-engineered from libkrkr2.so motionplayer.dll
// Stub classes for TJS API compatibility
//
// Aligned to libkrkr2.so Motion_namespace_ncb_register (0x6D9B08):
// Includes Point, Circle, Rect, Quad, LayerGetter stubs + SourceCache/ObjSource.
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "tjs.h"

class iTVPBaseBitmap;
class iTVPTexture2D;
class tTVPBaseBitmap;

namespace motion {

    class Player;
    class ResourceManager;

    namespace detail {
        struct PlayerRuntime;
    }

    // Aligned to libkrkr2.so SourceCache:
    //   0x6A78F4 constructor stores owner/primaryLayer/bufLayer/list state.
    //   0x6A7BA8 loadSource scans a list cache before materializing a Layer.
    //   0x6A8438 clearCache releases cached layer image entries.
    //   0x6A84FC bufLayer returns the cached bufLayer variant.
    class SourceCache {
    public:
        struct Entry {
            std::string key;
            std::string resolvedKey;
            int blendMode = 0;
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            tTJSVariant rawSource;
            tTJSVariant sourceObject;
            std::shared_ptr<tTVPBaseBitmap> backingBitmap;
            iTVPTexture2D *sourceTexture = nullptr;
        };

        SourceCache();
        SourceCache(tTJSVariant owner, tjs_int layerType);
        ~SourceCache();

        void setLayerOwner(tTJSVariant owner, tjs_int layerType);

        tTJSVariant loadSource(tTJSVariant keyOrSource, tTJSVariant currentSource);
        tTJSVariant loadSourceByName(const Player *player,
                                     const ttstr &name,
                                     const tTJSVariant &currentSource);
        tTJSVariant loadRenderSourceByName(
            const Player &player,
            const ttstr &name,
            const tTJSVariant &currentSource,
            int blendMode,
            const std::array<std::uint32_t, 4> &packedColors,
            iTJSDispatch2 *layerTreeOwnerObject,
            iTJSDispatch2 *parentLayerObject);
        iTVPTexture2D *loadRenderSourceTextureByName(
            const Player &player,
            const ttstr &name,
            const tTJSVariant &currentSource,
            int blendMode,
            const std::array<std::uint32_t, 4> &packedColors);
        void clearCache();
        void eraseSource(ttstr name);
        tTJSVariant getBufLayer() const;
        std::size_t size() const;

        const Entry *findEntry(const std::string &key,
                               int blendMode,
                               const std::array<std::uint32_t, 4> &packedColors) const;

    private:
        Entry *findEntry(const std::string &key,
                         int blendMode,
                         const std::array<std::uint32_t, 4> &packedColors);
        Entry *findEntryByKey(const std::string &key);
        Entry &ensureEntry(const std::string &key,
                           const std::string &resolvedKey,
                           int blendMode,
                           const std::array<std::uint32_t, 4> &packedColors);
        bool ensureEntryBackingBitmap(Entry &entry,
                                      const Player *player,
                                      const std::string &key,
                                      int blendMode,
                                      const std::array<std::uint32_t, 4> &packedColors);
        void releaseEntryTexture(Entry &entry);
        tTJSVariant loadRawSourceVariant(const Player *player,
                                         const ttstr &name,
                                         std::string &resolvedKey) const;

        tTJSVariant _owner;
        tTJSVariant _primaryLayer;
        tTJSVariant _bufLayer;
        tjs_int _layerType = 0;
        std::list<Entry> _entries;
    };

    // Aligned to libkrkr2.so ObjSource (ncb_registerMembers @0x69CCB8). The
    // binary ObjSource is NOT a fields struct — it is a thin dict FACADE:
    // operator new(0x18) holds a single tTJSVariant (qword[0]) referencing the
    // PSB "source" dict, and every member reads dict[key]. The former
    // _key/_src/_blendMode/_color fields were a port invention (MASTER's
    // "ObjSource missing 6 members" was inverted — it has no struct fields).
    //
    // Now constructed by ResourceManager::findSource (ResourceManager.cpp,
    // aligned with the binary RM findSource @0x6AAB3C): the "src" branch
    // navigates module["source"][group]["icon"][icon] and wraps the resulting
    // sub-dict in this facade via ncbInstanceAdaptor<ObjSource>::CreateAdaptor
    // (mirrors operator new(0x18) + sub_6EC124). Reachable only through the TJS
    // NCB binding Motion.ResourceManager.findSource — there is no internal C++
    // caller, and the port unit tests exercise Player::findSource (a separate
    // function), so this path is oracle-inert under the current fixtures.
    class ObjSource {
    public:
        ObjSource() = default;
        explicit ObjSource(tTJSVariant sourceDict) :
            _sourceDict(std::move(sourceDict)) {}

        // originX/originY @0x69D014/0x69D0D8 = dict["originX"/"originY"] (int).
        tjs_int getOriginX() const { return readInt(TJS_W("originX"), 0); }
        tjs_int getOriginY() const { return readInt(TJS_W("originY"), 0); }
        // width/height @0x69D19C/0x69D27C = dict["width"/"height"]; the binary
        // returns 32 when the backing source variant is not a dict object (its
        // `type != 7` gate), which readInt's default branch reproduces.
        tjs_int getWidth() const { return readInt(TJS_W("width"), 32); }
        tjs_int getHeight() const { return readInt(TJS_W("height"), 32); }
        // clip @0x69D35C (prop-ro) builds a Motion.Rect from
        // dict["clip"].{left,top,right,bottom}; drawLayer @0x69D6D8 (method) does
        // a SetSize + Layer draw (a draw-time op, platform-boundary-adjacent).
        // Both bodies deferred — registered for surface fidelity, void / no-op.
        tTJSVariant getClip() const { return {}; }
        void drawLayer(tTJSVariant /*target*/) {}

    private:
        // Reads an int member from the backing source dict; returns `dflt` unless
        // the backing is a dict object (binary `type == 7` gate) holding `key`.
        tjs_int readInt(const tjs_char *key, tjs_int dflt) const {
            if(_sourceDict.Type() != tvtObject) {
                return dflt;
            }
            iTJSDispatch2 *obj = _sourceDict.AsObjectNoAddRef();
            tTJSVariant v;
            if(obj &&
               TJS_SUCCEEDED(obj->PropGet(0, key, nullptr, &v, obj)) &&
               v.Type() != tvtVoid) {
                return static_cast<tjs_int>(v);
            }
            return dflt;
        }

        tTJSVariant _sourceDict;  // qword[0]: the PSB "source" dict facade
    };

    // Aligned to libkrkr2.so Motion.Point (0x690FBC)
    struct Point {
        int type = 0;
        double x = 0, y = 0;

        int getType() const { return type; }
        double getX() const { return x; }
        double getY() const { return y; }
        bool contains(double, double) { return false; }
    };

    // Aligned to libkrkr2.so Motion.Circle (0x691300)
    struct Circle {
        int type = 1;
        double x = 0, y = 0, r = 0;

        int getType() const { return type; }
        double getX() const { return x; }
        double getY() const { return y; }
        double getR() const { return r; }
        bool contains(double px, double py) {
            double dx = px - x, dy = py - y;
            return dx * dx + dy * dy <= r * r;
        }
    };

    // Aligned to libkrkr2.so Motion.Rect (0x6916A4)
    struct Rect {
        int type = 2;
        double l = 0, t = 0, w = 0, h = 0;

        int getType() const { return type; }
        double getL() const { return l; }
        double getT() const { return t; }
        double getW() const { return w; }
        double getH() const { return h; }
        bool contains(double px, double py) {
            return px >= l && px < l + w && py >= t && py < t + h;
        }
    };

    // Aligned to libkrkr2.so Motion.Quad (0x691AD0)
    struct Quad {
        int type = 3;
        // 4 corners × 2 floats = 8 values
        double verts[8] = {};

        int getType() const { return type; }
        tTJSVariant getP() const;
        bool contains(double, double) { return false; } // stub
    };

    // Aligned to libkrkr2.so Motion.LayerGetter (0x69B350)
    // 28 read-only properties from node state
    class LayerGetter {
    public:
        LayerGetter() = default;

        void setType(int v) { _type = v; }
        void setLabel(ttstr v) { _label = v; }
        void setVisible(bool v) { _visible = v; }
        void setBranchVisible(bool v) { _branchVisible = v; }
        void setLayerVisible(bool v) { _layerVisible = v; }
        void setX(double v) { _x = v; }
        void setY(double v) { _y = v; }
        void setFlipX(bool v) { _flipX = v; }
        void setFlipY(bool v) { _flipY = v; }
        void setZoomX(double v) { _zoomX = v; }
        void setZoomY(double v) { _zoomY = v; }
        void setAngleDeg(double v) { _angleDeg = v; }
        void setAngleRad(double v) { _angleRad = v; }
        void setSlantX(double v) { _slantX = v; }
        void setSlantY(double v) { _slantY = v; }
        void setOriginX(double v) { _originX = v; }
        void setOriginY(double v) { _originY = v; }
        void setOpacity(int v) { _opacity = v; }
        void setMtx(tTJSVariant v) { _mtx = v; }
        void setVtx(tTJSVariant v) { _vtx = v; }
        void setColor(tTJSVariant v) { _color = v; }
        void setBezierPatch(tTJSVariant v) { _bezierPatch = v; }
        void setShape(tTJSVariant v) { _shape = v; }
        void setMotion(tTJSVariant v) { _motion = v; }
        void setParticle(tTJSVariant v) { _particle = v; }

        int getType() const { return _type; }
        ttstr getLabel() const { return _label; }
        bool getVisible() const { return _visible; }
        bool getBranchVisible() const { return _branchVisible; }
        bool getLayerVisible() const { return _layerVisible; }
        double getX() const { return _x; }
        double getY() const { return _y; }
        double getLeft() const { return _x; }
        double getTop() const { return _y; }
        bool getFlipX() const { return _flipX; }
        bool getFlipY() const { return _flipY; }
        double getZoomX() const { return _zoomX; }
        double getZoomY() const { return _zoomY; }
        double getAngleDeg() const { return _angleDeg; }
        double getAngleRad() const { return _angleRad; }
        double getSlantX() const { return _slantX; }
        double getSlantY() const { return _slantY; }
        double getOriginX() const { return _originX; }
        double getOriginY() const { return _originY; }
        int getOpacity() const { return _opacity; }
        tTJSVariant getMtx() const { return _mtx; }
        tTJSVariant getVtx() const { return _vtx; }
        tTJSVariant getColor() const { return _color; }
        tTJSVariant getBezierPatch() const { return _bezierPatch; }
        tTJSVariant getShape() const { return _shape; }
        tTJSVariant getMotion() const { return _motion; }
        tTJSVariant getParticle() const { return _particle; }

    private:
        int _type = 0;
        ttstr _label;
        bool _visible = true, _branchVisible = true, _layerVisible = true;
        double _x = 0, _y = 0;
        bool _flipX = false, _flipY = false;
        double _zoomX = 1.0, _zoomY = 1.0;
        double _angleDeg = 0, _angleRad = 0;
        double _slantX = 0, _slantY = 0;
        double _originX = 0, _originY = 0;
        int _opacity = 255;
        tTJSVariant _mtx;
        tTJSVariant _vtx;
        tTJSVariant _color;
        tTJSVariant _bezierPatch;
        tTJSVariant _shape;
        tTJSVariant _motion;
        tTJSVariant _particle;
    };

} // namespace motion
