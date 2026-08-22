//
// Reconstructed from the four current reference binaries. Exact current
// mappings are kept beside the PSB-consuming members below.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <utility>

#include "psbfile/PSBRawFile.h"
#include "MotionNode.h"
#include "tjs.h"

class iTVPBaseBitmap;
class iTVPTexture2D;
class tTVPBaseBitmap;

namespace motion {

    class D3DAdaptor;
    class Player;
    class ResourceManager;

    namespace detail {
        struct MotionNode;
        struct PreparedRenderItem;
    }

    // All four current SourceCache member tables expose loadSource,
    // clearCache, and a getter-only bufLayer property.  ResourceManager
    // re-registers those exact callbacks rather than forwarding to a second
    // cache object or keeping duplicate state.
    // A successful zero-argument script construction attaches this object to
    // a non-sticky adaptor which owns it. Invalidate/destruction tears down the
    // list and persistent Variants directly; it does not call public
    // clearCache, so cached Layers receive no script-visible Invalidate there.
    class SourceCache {
    public:
        struct Entry {
            // The four current loadSource implementations and their list-node
            // copy paths establish this source-level payload order. ABI byte
            // offsets intentionally stay out of the compiled type.
            tTJSVariant key;
            tTJSVariant layer;
            // Lookup identity is the exact (key, src, blendMode) triple.
            // Packed colors are mutable hit payload and byteWeight is the
            // most recent bake result.
            ttstr src;
            // Normal loadSource overwrites blendMode before reading it.  The
            // missing-color branch, however, deliberately initializes only
            // colors[0]; colors[1..3] retain the native source's indeterminate
            // stack contents and are still compared/copied/baked.  Do not add
            // aggregate/default initialization here: that would erase an
            // observable original boundary.
            tjs_int blendMode;
            tjs_int colors[4];
            tjs_int byteWeight = 0;
        };

        // First-declaration `= default` is intentional.  ncbind constructs
        // the published zero-argument form with `new SourceCache()`, matching
        // the four-reference value-initialized all-Void/zero/empty state.
        SourceCache() = default;
        SourceCache(tTJSVariant owner, tjs_int cacheSize);
        ~SourceCache();

        tTJSVariant loadSource(iTJSDispatch2 *source,
                               iTJSDispatch2 *descriptor);
        tTJSVariant loadRenderSourceLayerFromItem_guess(
            Player &player,
            const detail::PreparedRenderItem &item);
        iTVPTexture2D *loadRenderSourceTextureFromItem_guess(
            Player &player,
            detail::PreparedRenderItem &item);
        // Combined D3D source callback: existing/new KRKR atlas borrows return
        // directly, while only a generic Layer fallback is passed through the
        // adaptor's software-copy cache.
        iTVPTexture2D *loadRenderSourceTextureForItem_guess(
            Player &player,
            D3DAdaptor &adaptor,
            detail::PreparedRenderItem &item);
        void clearCache();
        tTJSVariant getBufLayer() const;
        std::size_t size() const;

    private:
        void bakeSource_guess(iTJSDispatch2 *source, Entry &entry);
        void trimCacheBeforeInsert_guess();

        // These are three independently retained Variant closures.  bufLayer
        // is constructed once from (owner, primaryLayer); clearing cache
        // entries never invalidates or replaces it.
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
    // ResourceManager::findSource's four-reference "src" branch navigates
    // module["source"][group]["icon"][icon], constructs this facade, then
    // wraps it through ncbInstanceAdaptor<ObjSource>::CreateAdaptor.
    // Player::findSourceForNode_guess and the production load-source route
    // consume this same facade.
    // A compatible non-sticky adaptor owns and destroys the facade. A failed
    // or incompatible publication does not reclaim the preconstructed facade;
    // because its PSBRawNode has already retained the owner, both allocations
    // remain live. The direct zero-argument script constructor is different:
    // metadata-attachment failure runs the ObjSource destructor and frees it.
    // The inherited NCB loadSource has the exact `(source,descriptor)` boundary.
    // The former port-only Player by-name cache helper was removed; there is no
    // second cache topology or decoded MotionSnapshot image side path.
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

        // Destruction releases the retained texture first and the raw-node
        // owner second. Neither native member slot is cleared by that body.
        PSB::PSBRawNode _source; // retained owner plus borrowed node address
        iTVPTexture2D *_texture = nullptr; // one retained lazy texture reference
    };

    // The four public geometry classes are type-specific NCB facades over one
    // full shared record.  Their script constructors write only type; all 15
    // doubles deliberately retain default-initialized storage contents.  A
    // non-sticky NCB adaptor owns a heap facade only after attachment succeeds;
    // the per-type ClassInfo tuple is a separate, static, non-owning lookup.
    struct GeometryShapeBase_guess : detail::HitData {
        explicit GeometryShapeBase_guess(std::int32_t shapeType) noexcept {
            type = shapeType;
        }
        explicit GeometryShapeBase_guess(
            const detail::HitData &source) noexcept
            : detail::HitData(source) {}

        int getType() const { return type; }
        bool contains(double x, double y) {
            return detail::hitTestHitData(*this, x, y);
        }
    };

    struct Point : GeometryShapeBase_guess {
        Point() noexcept : GeometryShapeBase_guess(0) {}
        explicit Point(const detail::HitData &source) noexcept
            : GeometryShapeBase_guess(source) {}

        double getX() const { return values[0]; }
        double getY() const { return values[1]; }
    };

    struct Circle : GeometryShapeBase_guess {
        Circle() noexcept : GeometryShapeBase_guess(1) {}
        explicit Circle(const detail::HitData &source) noexcept
            : GeometryShapeBase_guess(source) {}

        double getX() const { return values[0]; }
        double getY() const { return values[1]; }
        double getR() const { return values[2]; }
    };

    struct Rect : GeometryShapeBase_guess {
        Rect() noexcept : GeometryShapeBase_guess(2) {}
        explicit Rect(const detail::HitData &source) noexcept
            : GeometryShapeBase_guess(source) {}

        double getL() const { return values[3]; }
        double getT() const { return values[4]; }
        double getW() const { return values[5] - values[3]; }
        double getH() const { return values[6] - values[4]; }
    };

    struct Quad : GeometryShapeBase_guess {
        Quad() noexcept : GeometryShapeBase_guess(3) {}
        explicit Quad(const detail::HitData &source) noexcept
            : GeometryShapeBase_guess(source) {}

        tTJSVariant getP() const;
    };

    static_assert(sizeof(Point) == sizeof(detail::HitData));
    static_assert(sizeof(Circle) == sizeof(detail::HitData));
    static_assert(sizeof(Rect) == sizeof(detail::HitData));
    static_assert(sizeof(Quad) == sizeof(detail::HitData));

    // Motion.LayerGetter is a non-owning one-pointer facade over a live
    // MotionNode. Its delayed NCB class has a separate process-static,
    // non-owning ClassInfo tuple. After a successful non-sticky attachment the
    // script adaptor owns only this small facade, never the MotionNode. Every
    // one of its 29 read-only properties dereferences the current node when the
    // property is read; it does not snapshot or retain any node field. Player
    // destruction or node-tree replacement can therefore leave a surviving
    // script facade dangling.
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
        // Four-reference default construction writes only a null node pointer.
        // Metadata-attach failure deletes this facade, while successful direct
        // script construction leaves the pointer null. The getters deliberately
        // have no null or lifetime guard, preserving both direct-construction
        // null dereference and later dangling-node boundaries.
        detail::MotionNode *_node = nullptr;
    };

} // namespace motion
