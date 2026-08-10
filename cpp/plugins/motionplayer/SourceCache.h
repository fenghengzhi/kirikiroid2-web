//
// Reconstructed from the four current reference binaries. Exact current
// mappings are kept beside the PSB-consuming members below.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <utility>

#include "psbfile/PSBRawFile.h"
#include "MotionNode.h"
#include "tjs.h"

class iTVPBaseBitmap;
class iTVPTexture2D;
class tTVPBaseBitmap;

namespace motion {

    class Player;
    class ResourceManager;

    namespace detail {
        struct MotionNode;
        struct PreparedRenderItem;
        struct PlayerRuntime;
    }

    // Current SourceCache registrars are sub_6A5988, sub_57B0DC,
    // sub_100100F90, and sub_FE12A. Their loadSource/clearCache/bufLayer
    // callbacks are 6A4F88/6A5818/6A58DC, 57ACC8/57B018/57B060,
    // 1001009AC/100100F10/100100F84, and FDB50/FE0D4/FE11A.
    class SourceCache {
    public:
        struct Entry {
            // The four current loadSource implementations and their list-node
            // copy paths establish this source-level payload order. ABI byte
            // offsets intentionally stay out of the compiled type.
            tTJSVariant key;
            tTJSVariant layer;
            ttstr src;
            tjs_int blendMode;
            tjs_int colors[4];
            tjs_int byteWeight = 0;
        };

        SourceCache();
        SourceCache(tTJSVariant owner, tjs_int cacheSize);
        ~SourceCache();

        tTJSVariant loadSource(iTJSDispatch2 *source,
                               iTJSDispatch2 *descriptor);
        tTJSVariant loadSourceByName(const Player *player,
                                     const ttstr &name,
                                     const tTJSVariant &currentSource);
        tTJSVariant loadRenderSourceLayerFromItemLike_0x6C1B70(
            Player &player,
            const detail::PreparedRenderItem &item);
        iTVPTexture2D *loadRenderSourceTextureFromItemLike_0x6C1B70(
            Player &player,
            detail::PreparedRenderItem &item);
        iTVPTexture2D *loadRenderSourceTextureForItem_guess(
            Player &player,
            detail::PreparedRenderItem &item);
        void clearCache();
        void eraseSource(ttstr name);
        tTJSVariant getBufLayer() const;
        std::size_t size() const;

    private:
        void bakeSource_guess(iTJSDispatch2 *source, Entry &entry);
        void trimCacheBeforeInsert_guess();
        tTJSVariant loadRawSourceVariant(const Player *player,
                                         const ttstr &name,
                                         std::string &resolvedKey) const;

        tTJSVariant _owner;
        tTJSVariant _primaryLayer;
        tTJSVariant _bufLayer;
        std::uint32_t _currentCacheBytes = 0;
        std::uint32_t _cacheLimitBytes = 0;
        std::list<Entry> _entries;
    };

    // The four current references reconstruct ObjSource as a thin raw-node
    // facade: a PSBRawNode owner/node pair followed by a lazy texture. Its
    // allocation is 0x18 bytes in both 64-bit ABIs and 0x0c in both 32-bit
    // ABIs; those sizes are compiler layout evidence, not source constants.
    // Every member navigates the raw node directly. The former
    // _key/_src/_blendMode/_color fields were a port invention. Its actual
    // fields are precisely the retained raw owner/node pair plus lazy texture;
    // MASTER's older "ObjSource missing 6 members" verdict was also inverted.
    //
    // ResourceManager::findSource constructs it at
    // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6A7F1C,
    // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_57BDE0,
    // Kirikiroid2_1.3.9_iOS_arm64!sub_100102594, and
    // Kirikiroid2_1.3.9_iOS_armv7!sub_FF890. The "src" branch navigates
    // module["source"][group]["icon"][icon] and wraps the resulting sub-dict
    // through ncbInstanceAdaptor<ObjSource>::CreateAdaptor.
    // Player::findSourceForNode_guess and the production load-source route
    // consume this same facade.
    // The inherited NCB loadSource has the exact `(source,descriptor)` boundary;
    // the separate Player by-name helper is Web compatibility code and does not
    // create a second cache topology. There is no decoded MotionSnapshot image
    // side path in SourceCache.
    class ObjSource {
    public:
        ObjSource() = default;
        explicit ObjSource(const PSB::PSBRawNode &source) : _source(source) {}
        ~ObjSource();

        ObjSource(const ObjSource &) = delete;
        ObjSource &operator=(const ObjSource &) = delete;

        // The four originX/originY wrappers have no category gate: both use
        // the strict raw dictionary getter followed by GetInt. Their current
        // mapping is recorded in the psbfile four-binary audit.
        tjs_int getOriginX() const {
            return _source.GetDictionaryValueStrict("originX").GetInt();
        }
        tjs_int getOriginY() const {
            return _source.GetDictionaryValueStrict("originY").GetInt();
        }
        // The four width/height wrappers return 32 only when the raw node's
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
        // The four clip wrappers build a fresh property object from
        // dict["clip"].{left,top,right,bottom}. drawLayer lazily materialises
        // pixel/palette/RL data, assigns the retained texture to the target
        // Layer and resizes it; the exact ensureTexture source name is stripped.
        tTJSVariant getClip() const;
        void drawLayer(tTJSVariant target);

    private:
        void ensureTexture_guess();

        PSB::PSBRawNode _source; // qword[0..1]: retained owner + raw node
        iTVPTexture2D *_texture = nullptr; // qword[2]: retained lazy texture
    };

    // Motion.Point compatibility facade.
    struct Point {
        int type = 0;
        double x = 0, y = 0;

        int getType() const { return type; }
        double getX() const { return x; }
        double getY() const { return y; }
        bool contains(double, double) { return false; }
    };

    // Motion.Circle compatibility facade.
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

    // Motion.Rect compatibility facade.
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

    // Motion.Quad compatibility facade.
    struct Quad {
        int type = 3;
        // 4 corners × 2 floats = 8 values
        double verts[8] = {};

        int getType() const { return type; }
        tTJSVariant getP() const;
        bool contains(double, double) { return false; } // stub
    };

    // Motion.LayerGetter is a non-owning one-pointer facade over a live
    // MotionNode. Every one of
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
