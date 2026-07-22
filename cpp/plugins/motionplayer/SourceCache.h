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

#include "psbfile/PSBRawFile.h"
#include "tjs.h"

class iTVPBaseBitmap;
class iTVPTexture2D;
class tTVPBaseBitmap;

namespace motion {

    class Player;
    class ResourceManager;

    namespace detail {
        struct MotionNode;
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
    // binary ObjSource is a thin raw-node facade: operator new(0x18) holds a
    // PSBRawNode owner/node pair in qword[0..1] and a lazy texture in qword[2].
    // Every member navigates the raw node directly. The former
    // _key/_src/_blendMode/_color fields were a port invention. Its actual
    // fields are precisely the retained raw owner/node pair plus lazy texture;
    // MASTER's older "ObjSource missing 6 members" verdict was also inverted.
    //
    // Now constructed by ResourceManager::findSource (ResourceManager.cpp,
    // aligned with the binary RM findSource @0x6AAB3C): the "src" branch
    // navigates module["source"][group]["icon"][icon] and wraps the resulting
    // sub-dict in this facade via ncbInstanceAdaptor<ObjSource>::CreateAdaptor
    // (mirrors operator new(0x18) + sub_6EC124). Player_findSource @0x6948E8
    // and SourceCache_loadSource @0x6A7BA8 now consume this same facade; there
    // is no decoded MotionSnapshot image side path in SourceCache.
    class ObjSource {
    public:
        ObjSource() = default;
        explicit ObjSource(const PSB::PSBRawNode &source) : _source(source) {}
        ~ObjSource();

        ObjSource(const ObjSource &) = delete;
        ObjSource &operator=(const ObjSource &) = delete;

        // originX/originY @0x69D014/0x69D0D8 have no category gate: both use
        // the strict raw dictionary getter followed by GetInt.
        tjs_int getOriginX() const {
            return _source.GetDictionaryValueStrict("originX").GetInt();
        }
        tjs_int getOriginY() const {
            return _source.GetDictionaryValueStrict("originY").GetInt();
        }
        // width/height @0x69D19C/0x69D27C return 32 only when the raw node's
        // category is not dictionary; a missing member on a dictionary throws.
        tjs_int getWidth() const {
            return _source.GetTypeCategory() == 7
                ? _source.GetDictionaryValueStrict("width").GetInt()
                : 32;
        }
        tjs_int getHeight() const {
            return _source.GetTypeCategory() == 7
                ? _source.GetDictionaryValueStrict("height").GetInt()
                : 32;
        }
        // clip @0x69D35C builds a fresh property object from
        // dict["clip"].{left,top,right,bottom}. drawLayer @0x69D6D8 lazily
        // materialises the dict's pixel/palette/RL data through
        // ObjSource_ensureTexture @0x6DA454, assigns that retained texture to
        // the target Layer and resizes it.
        tTJSVariant getClip() const;
        void drawLayer(tTJSVariant target);

    private:
        void ensureTextureLike_0x6DA454();

        PSB::PSBRawNode _source; // qword[0..1]: retained owner + raw node
        iTVPTexture2D *_texture = nullptr; // qword[2]: retained lazy texture
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

    // Aligned to libkrkr2.so Motion.LayerGetter (0x69B350): the native object
    // is a non-owning one-pointer facade over a live MotionNode.  Every one of
    // its 29 read-only properties dereferences that node when the property is
    // read; it does not snapshot or retain any node field.
    class LayerGetter {
    public:
        LayerGetter() = default;
        explicit LayerGetter(detail::MotionNode *node) : _node(node) {}

        int getType() const;
        ttstr getLabel() const;
        ttstr getSrc() const;
        bool getVisible() const;
        bool getBranchVisible() const;
        bool getLayerVisible() const;
        double getX() const;
        double getY() const;
        double getLeft() const;
        double getTop() const;
        tTJSVariant getCoord() const;
        bool getFlipX() const;
        bool getFlipY() const;
        double getZoomX() const;
        double getZoomY() const;
        double getAngleDeg() const;
        double getAngleRad() const;
        double getSlantX() const;
        double getSlantY() const;
        double getOriginX() const;
        double getOriginY() const;
        int getOpacity() const;
        tTJSVariant getMtx() const;
        tTJSVariant getVtx() const;
        tTJSVariant getColor() const;
        tTJSVariant getBezierPatch() const;
        tTJSVariant getShape() const;
        tTJSVariant getMotion() const;
        tTJSVariant getParticle() const;

    private:
        // LayerGetter default construction @0x6E2CA0 writes only a null node
        // pointer.  The getters deliberately have no null guard, preserving
        // the binary's directly-constructed-object boundary behavior.
        detail::MotionNode *_node = nullptr;
    };

} // namespace motion
