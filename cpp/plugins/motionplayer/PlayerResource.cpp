// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "RenderManager.h"
#include "SourceCache.h"
#include "tjsUtils.h"
#include "../../core/visual/ogl/imagepacker.h"

#include <algorithm>

using namespace motion::internal;

extern unsigned int TVPMaxTextureSize;

namespace motion {

    namespace {
        static_assert(sizeof(tjs_uint) == 4 && sizeof(tjs_int) == 4);

        // Express an ARM W-register reinterpretation without relying on the
        // implementation-defined unsigned-to-signed conversion above INT_MAX.
        constexpr tjs_int signedW32(tjs_uint bits) noexcept {
            return bits <= 0x7fffffffu
                ? static_cast<tjs_int>(bits)
                : -1 - static_cast<tjs_int>(~bits);
        }

        constexpr tjs_int addW32(tjs_int left, tjs_int right) noexcept {
            return signedW32(
                static_cast<tjs_uint>(left) + static_cast<tjs_uint>(right));
        }

        constexpr tjs_int subtractW32(tjs_int left, tjs_int right) noexcept {
            return signedW32(
                static_cast<tjs_uint>(left) - static_cast<tjs_uint>(right));
        }

        bool findWinSourceGroupLike_0x6948E8(
            detail::LoadedResourceRecord &loadedResource,
            const std::string &group, PSB::PSBRawNode &groupNode) {
            PSB::PSBRawOwner *owner = loadedResource.file.GetOwner();
            const PSB::PSBRawNode root(owner,
                                       owner->GetHeader()->entries);
            const PSB::PSBRawNode sourceRoot =
                root.GetDictionaryValueStrict("source");
            // 0x694AEC..0x694B44 uses the non-throwing group lookup and
            // transfers control to the spec-1/fallback route on a miss.
            return sourceRoot.GetDictionaryValue(group.c_str(), groupNode);
        }

        iTVPTexture2D *loadWinAtlasTextureLike_0x6948E8(
            const PSB::PSBRawNode &groupNode,
            detail::LoadedResourceRecord &loadedResource,
            const std::string &group) {
            const ttstr groupKey = detail::widen(group);
            if(const auto it = loadedResource.winSourceTextures.find(groupKey);
               it != loadedResource.winSourceTextures.end()) {
                return it->second.texture;
            }

            const PSB::PSBRawNode textureNode =
                groupNode.GetDictionaryValueStrict("texture");
            // 0x694C8C..0x694D14 performs these two conversions and discards
            // both results; width/height below do not fall back to them.
            (void)textureNode.GetDictionaryValueStrict("truncated_width")
                .GetInt();
            (void)textureNode.GetDictionaryValueStrict("truncated_height")
                .GetInt();
            const int width = textureNode.GetDictionaryValueStrict("width")
                                  .GetInt();
            const int height = textureNode.GetDictionaryValueStrict("height")
                                   .GetInt();
            const char *type =
                textureNode.GetDictionaryValueStrict("type").GetString();
            const PSB::PSBRawNode pixelNode =
                textureNode.GetDictionaryValueStrict("pixel");
            // 0x694E08 passes an uninitialized 32-bit stack slot.  A null
            // resource chunk leaves it untouched in PSB_getResourceData.
            std::uint32_t sourceSize;
            const std::uint8_t *sourcePixels =
                pixelNode.GetResource(sourceSize);

            // 0x694E44..0x694E54 performs both operations in W registers,
            // then calls TJSAlignedAlloc(bytes, 4).  Keep the wrap before the
            // allocator rather than widening through host size_t.
            const tjs_uint pitch = static_cast<tjs_uint>(width) << 2;
            const tjs_uint destinationSize =
                pitch * static_cast<tjs_uint>(height);
            auto *bgra = static_cast<std::uint8_t *>(
                TJS::TJSAlignedAlloc(destinationSize, 4));
            const tjs_int signedSourceSize = signedW32(sourceSize);
            if(std::strcmp(type, "RGBA8") == 0) {
                TVPReverseRGB(
                    reinterpret_cast<tjs_uint32 *>(bgra),
                    reinterpret_cast<const tjs_uint32 *>(sourcePixels),
                    signedSourceSize / 4);
            } else {
                if(std::strcmp(type, "A8L8") != 0) {
                    // 0x694E84..0x694E94 frees before constructing the
                    // unsupported-format exception argument.
                    TJS::TJSAlignedDealloc(bgra);
                    TVPThrowExceptionMessage(
                        TJS_W("MotionPlayer.findSource: Unsupported texture format '%1'"),
                        ttstr(type));
                }
                // 0x694EFC..0x694F30 reads [alpha,luminance] and writes
                // [luminance,luminance,luminance,alpha].  Its signed W32
                // comparison deliberately performs one final out-of-range
                // byte read for a positive odd resource length.
                tjs_uint64 sourceOffset = 0;
                std::uint8_t *destination = bgra;
                if(signedSourceSize >= 1) {
                    do {
                        const std::uint8_t alpha =
                            sourcePixels[sourceOffset];
                        const std::uint8_t luminance =
                            sourcePixels[sourceOffset + 1];
                        destination[0] = luminance;
                        destination[1] = luminance;
                        destination[2] = luminance;
                        destination[3] = alpha;
                        sourceOffset += 2;
                        destination += 4;
                    } while(signedW32(static_cast<tjs_uint>(sourceOffset)) <
                            signedSourceSize);
                }
            }

            auto *texture = TVPGetRenderManager()->CreateTexture2D(
                bgra, signedW32(pitch),
                static_cast<tjs_uint>(width), static_cast<tjs_uint>(height),
                TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_STATIC);
            // 0x694F60..0x694F64 frees before map insertion.  The Android
            // path has no texture-null guard and will still insert/replace the
            // slot before its unconditional construction-reference Release.
            TJS::TJSAlignedDealloc(bgra);
            auto [cached, inserted] =
                loadedResource.winSourceTextures.try_emplace(groupKey);
            (void)inserted;
            cached->second.setTexture(texture);
            // 0x694FAC retains the map value; 0x694FBC releases the texture's
            // construction reference, leaving the nested map as sole owner.
            texture->Release();
            return cached->second.texture;
        }

        struct KrkrAtlasRecordLike_0x695DE8;

        // sub_695DE8 stores a rect subobject at record+0x10, passes that
        // subobject to ImagePacker, and recovers the containing record through
        // the explicit pointer immediately following the rect.  The tail
        // scalars are deliberately left uninitialized until the second pass.
        struct KrkrAtlasRectLike_0x695DE8 : ImagePacker::rect_xywhf {
            KrkrAtlasRecordLike_0x695DE8 *record;
            int contentWidth;
            int contentHeight;
            std::uint8_t *bgra;

            KrkrAtlasRectLike_0x695DE8(
                int x, int y, int width, int height) :
                ImagePacker::rect_xywhf(x, y, width, height) {}
        };

        // sub_698074 @ 0x698074 walks a contiguous value vector, destroys the
        // sole sourceKey string and releases iconNode, but never reads/frees
        // rect.bgra.  A user-declared default destructor also keeps vector
        // growth copy-shaped instead of introducing a noexcept move path.
        struct KrkrAtlasRecordLike_0x695DE8 {
            PSB::PSBRawNode iconNode;
            KrkrAtlasRectLike_0x695DE8 rect;
            std::string sourceKey;

            KrkrAtlasRecordLike_0x695DE8(
                const PSB::PSBRawNode &node, int width, int height,
                std::string key) :
                iconNode(node),
                rect(0, 0, addW32(width, 1), addW32(height, 1)),
                sourceKey(std::move(key)) {}

            ~KrkrAtlasRecordLike_0x695DE8() = default;
        };

        void decodeKrkrRL8Like_0x696E40(
            std::uint8_t *destination, const std::uint8_t *source,
            std::uint32_t sourceSize) {
            const tjs_int signedSourceSize = signedW32(sourceSize);
            if(signedSourceSize < 1) {
                return;
            }
            const std::uint8_t *const sourceEnd =
                source + signedSourceSize;
            do {
                const std::uint8_t marker = *source++;
                if((marker & 0x80u) != 0) {
                    const size_t count = (marker & 0x7fu) + 3u;
                    std::memset(destination, *source++, count);
                    destination += count;
                } else {
                    const size_t count = static_cast<size_t>(marker) + 1u;
                    std::memcpy(destination, source, count);
                    destination += count;
                    source += count;
                }
            } while(source < sourceEnd);
        }

        void decodeKrkrRL32Like_0x696D00(
            std::uint8_t *destination, const std::uint8_t *source,
            std::uint32_t sourceSize) {
            const tjs_int signedSourceSize = signedW32(sourceSize);
            if(signedSourceSize < 1) {
                return;
            }
            const std::uint8_t *const sourceEnd =
                source + signedSourceSize;
            auto *output = reinterpret_cast<tjs_uint32 *>(destination);
            do {
                const std::uint8_t marker = *source++;
                if((marker & 0x80u) != 0) {
                    tjs_uint32 pixel;
                    std::memcpy(&pixel, source, sizeof(pixel));
                    source += sizeof(pixel);
                    const size_t count = (marker & 0x7fu) + 3u;
                    std::fill_n(output, count, pixel);
                    output += count;
                } else {
                    const size_t count = static_cast<size_t>(marker) + 1u;
                    const size_t byteCount = count * sizeof(tjs_uint32);
                    std::memcpy(output, source, byteCount);
                    output += count;
                    source += byteCount;
                }
            } while(source < sourceEnd);
        }

        void decodeKrkrAtlasRecordLike_0x695DE8(
            PSB::PSBRawNode &persistentNode,
            KrkrAtlasRecordLike_0x695DE8 &record) {
            auto &rect = record.rect;
            // 0x696F90 constructs this per-record lookup result while the
            // function-wide persistent node remains live.  0x696F94 checks
            // "pal" on that previous persistent value before 0x696FC0 copies
            // the current record node into it; this one-record-lagged gate is
            // intentional Android data flow, not a compiler scheduling artifact.
            PSB::PSBRawNode scratch;
            const bool hasPalette =
                persistentNode.ContainsDictionaryKey("pal");
            // 0x696FA8 sign-extends both dimensions and multiplies in X21.
            // Allocation sizes then deliberately truncate that product to W.
            const tjs_int64 pixelCount =
                static_cast<tjs_int64>(rect.contentWidth) *
                static_cast<tjs_int64>(rect.contentHeight);
            const tjs_uint pixelCountW32 =
                static_cast<tjs_uint>(pixelCount);
            const tjs_int pixelCountS32 = signedW32(pixelCountW32);
            rect.bgra = static_cast<std::uint8_t *>(
                TJS::TJSAlignedAlloc(pixelCountW32 << 2, 4));
            std::uint32_t resourceSize;
            persistentNode = record.iconNode;

            if(hasPalette) {
                const bool compressed =
                    persistentNode.GetDictionaryValue("compress", scratch) &&
                    std::strcmp(scratch.GetString(), "RL") == 0;
                auto *indexes = static_cast<std::uint8_t *>(
                    TJS::TJSAlignedAlloc(pixelCountW32, 4));
                if(compressed) {
                    const std::uint8_t *pixelData =
                        persistentNode.GetDictionaryValueStrict("pixel")
                            .GetResource(resourceSize);
                    decodeKrkrRL8Like_0x696E40(
                        indexes, pixelData, resourceSize);
                } else {
                    // 0x6970A8 copies the PSB resource length rather than the
                    // destination pixel count.
                    const std::uint8_t *pixelData =
                        persistentNode.GetDictionaryValueStrict("pixel")
                            .GetResource(resourceSize);
                    std::memcpy(
                        indexes, pixelData,
                        static_cast<std::size_t>(
                            signedW32(resourceSize)));
                }

                // Contains("pal") and try-get("pal") are distinct calls in
                // the binary.  A damaged dictionary may pass the former and
                // fail the latter without throwing or expanding the indexes.
                if(persistentNode.GetDictionaryValue("pal", scratch)) {
                    const std::uint8_t *paletteData =
                        scratch.GetResource(resourceSize);
                    const tjs_int paletteCount =
                        signedW32(resourceSize) / 4;
                    std::vector<tjs_uint32> palette(
                        static_cast<std::size_t>(paletteCount));
                    TVPReverseRGB(
                        palette.data(),
                        reinterpret_cast<const tjs_uint32 *>(paletteData),
                        paletteCount);
                    TVPBLExpand8BitTo32BitPal(
                        reinterpret_cast<tjs_uint32 *>(rect.bgra), indexes,
                        pixelCountS32, palette.data());
                }
                TJS::TJSAlignedDealloc(indexes);
            } else {
                const bool compressed =
                    persistentNode.GetDictionaryValue("compress", scratch) &&
                    std::strcmp(scratch.GetString(), "RL") == 0;
                if(compressed) {
                    const std::uint8_t *pixelData =
                        persistentNode.GetDictionaryValueStrict("pixel")
                            .GetResource(resourceSize);
                    decodeKrkrRL32Like_0x696D00(
                        rect.bgra, pixelData, resourceSize);
                    TVPReverseRGB(
                        reinterpret_cast<tjs_uint32 *>(rect.bgra),
                        reinterpret_cast<const tjs_uint32 *>(rect.bgra),
                        pixelCountS32);
                } else {
                    const std::uint8_t *pixelData =
                        persistentNode.GetDictionaryValueStrict("pixel")
                            .GetResource(resourceSize);
                    TVPReverseRGB(
                        reinterpret_cast<tjs_uint32 *>(rect.bgra),
                        reinterpret_cast<const tjs_uint32 *>(pixelData),
                        pixelCountS32);
                }
            }

            bool anyAlpha = false;
            // 0x69720C first gates on signed low-W21, but 0x69722C compares
            // the scan index against the full signed X21 product.
            if(pixelCountS32 > 0) {
                tjs_int64 index = 0;
                do {
                    if(rect.bgra[static_cast<std::size_t>(index) * 4u + 3u] !=
                       0) {
                        anyAlpha = true;
                        break;
                    }
                    ++index;
                } while(index < pixelCount);
            }
            if(!anyAlpha) {
                // 0x697210..0x697248: an entirely transparent image drops its
                // pixel buffer and replaces the padded rectangle with 2x2.
                // contentWidth/contentHeight at record+0x28 are not rewritten.
                TJS::TJSAlignedDealloc(rect.bgra);
                rect.bgra = nullptr;
                rect.w = 2;
                rect.h = 2;
            }
        }

        bool buildKrkrAtlasGroupLike_0x695DE8(
            detail::LoadedResourceRecord &loadedResource,
            const std::string &requestedGroup,
            const std::string &requestedIcon) {
            PSB::PSBRawOwner *owner = loadedResource.file.GetOwner();
            const PSB::PSBRawNode root(owner,
                                       owner->GetHeader()->entries);
            // sub_695DE8 initializes one raw-node scratch at 0x6960D4 and
            // keeps it alive across the requested-icon probe, source
            // enumeration, and packed-record loop. Later assignments
            // deliberately release its previous owner before installing the
            // next node.
            PSB::PSBRawNode iconNode;
            const PSB::PSBRawNode sourceRoot =
                root.GetDictionaryValueStrict("source");
            if(!sourceRoot.GetDictionaryValue(requestedGroup.c_str(),
                                               iconNode)) {
                return false;
            }
            bool requestedIconFound;
            {
                const PSB::PSBRawNode requestedIconRoot =
                    iconNode.GetDictionaryValueStrict("icon");
                requestedIconFound = requestedIconRoot.GetDictionaryValue(
                    requestedIcon.c_str(), iconNode);
            }
            // 0x69612C..0x696154 releases the temporary icon root before it
            // tests the lookup result and enters the failure cleanup path.
            if(!requestedIconFound) {
                return false;
            }

            // 0x69659C..0x696704 appends 0x40-byte records by value.  Growth
            // copies each raw owner/string, then destroys the old range; there
            // is no per-record heap allocation or unique_ptr owner layer.
            std::vector<KrkrAtlasRecordLike_0x695DE8> records;
            for(const auto &group : sourceRoot.GetDictionaryKeys()) {
                const PSB::PSBRawNode groupNode =
                    sourceRoot.GetDictionaryValueStrict(group.c_str());
                const PSB::PSBRawNode iconRoot =
                    groupNode.GetDictionaryValueStrict("icon");
                for(const auto &iconName : iconRoot.GetDictionaryKeys()) {
                    iconNode =
                        iconRoot.GetDictionaryValueStrict(iconName.c_str());
                    const int width =
                        iconNode.GetDictionaryValueStrict("width").GetInt();
                    const int height =
                        iconNode.GetDictionaryValueStrict("height").GetInt();
                    records.emplace_back(
                        iconNode, width, height,
                        "src/" + group + "/" + iconName);
                }
            }

            // Android completes every potentially reallocating value-vector
            // append before publishing rect subobject pointers.  It then
            // decodes all records in a second, encounter-order pass.
            std::vector<ImagePacker::rect_xywhf *> recordPointers;
            for(auto &record : records) {
                record.rect.record = &record;
                record.rect.contentWidth = subtractW32(record.rect.w, 1);
                record.rect.contentHeight = subtractW32(record.rect.h, 1);
                recordPointers.push_back(&record.rect);
                decodeKrkrAtlasRecordLike_0x695DE8(iconNode, record);
            }

            std::vector<ImagePacker::bin> bins;
            const int maxSide = static_cast<int>(TVPMaxTextureSize);
            // sub_A6E50C @ 0xA6E50C returns a bool in W0, but 0x6968B4
            // immediately calls the zero-argument render-backend getter and
            // never tests it.  A pack failure therefore proceeds to the same
            // unchecked cache lookup below.
            (void)ImagePacker::pack(
                recordPointers.data(), static_cast<int>(recordPointers.size()),
                maxSide, bins);

            for(auto &bin : bins) {
                // ImagePacker::pack @ 0xA6E50C appends one 0x0 bin for an
                // empty record set.  sub_695DE8 @ 0x6968C0..0x696C20 still
                // creates that empty texture and releases its construction
                // reference; do not skip the zero-size lifecycle here.
                const tjs_uint atlasStride =
                    static_cast<tjs_uint>(bin.size.w) << 2;
                // 0x6968D4 creates an owned empty page. Each record below
                // writes metadata, uploads its own packed sub-rect, and frees
                // its BGRA buffer before advancing to the next record.
                auto *texture = TVPGetRenderManager()->CreateTexture2D(
                    nullptr, signedW32(atlasStride),
                    static_cast<unsigned int>(bin.size.w),
                    static_cast<unsigned int>(bin.size.h),
                    TVPTextureFormat::RGBA,
                    RENDER_CREATE_TEXTURE_FLAG_STATIC);
                for(auto *baseRect : bin.rects) {
                    auto *rect =
                        static_cast<KrkrAtlasRectLike_0x695DE8 *>(baseRect);
                    auto *record = rect->record;
                    detail::PackedSourceAtlasEntry entry;
                    entry.setTexture(texture);
                    // 0x696914..0x696960 copy-assigns the record node into the
                    // persistent scratch before any metadata reads.
                    iconNode = record->iconNode;
                    entry.originX =
                        iconNode.GetDictionaryValueStrict("originX").GetInt();
                    entry.originY =
                        iconNode.GetDictionaryValueStrict("originY").GetInt();
                    // 0x696A54..0x696A7C stores atlas x/y followed by the
                    // inclusive right/bottom in the four-int descriptor.
                    entry.textureRect = {
                        rect->x,
                        rect->y,
                        subtractW32(addW32(rect->x, rect->w), 1),
                        subtractW32(addW32(rect->y, rect->h), 1),
                    };
                    // 0x696A84..0x696A90 passes this same raw-node storage as
                    // both source and out.  A hit descends iconNode in place;
                    // a miss leaves it unchanged until the next assignment.
                    if(iconNode.GetDictionaryValue("clip", iconNode)) {
                        entry.clip = {
                            iconNode.GetDictionaryValueStrict("left").GetDouble(),
                            iconNode.GetDictionaryValueStrict("top").GetDouble(),
                            iconNode.GetDictionaryValueStrict("right").GetDouble(),
                            iconNode.GetDictionaryValueStrict("bottom").GetDouble(),
                        };
                    }
                    loadedResource.krkrSourceEntries.insert_or_assign(
                        detail::widen(record->sourceKey), std::move(entry));
                    if(rect->bgra != nullptr) {
                        // 0x696BE0..0x696C0C uses content width for pitch,
                        // passes the packed descriptor as a tTVPRect, then
                        // frees record+0x30 without clearing the field.
                        texture->Update(
                            rect->bgra, TVPTextureFormat::RGBA,
                            signedW32(
                                static_cast<tjs_uint>(rect->contentWidth) << 2),
                            tTVPRect(rect->x, rect->y,
                                     subtractW32(
                                         addW32(rect->x, rect->w), 1),
                                     subtractW32(
                                         addW32(rect->y, rect->h), 1)));
                        TJS::TJSAlignedDealloc(rect->bgra);
                    }
                }
                // Each cached icon entry took its own AddRef above.  Release
                // the page-construction reference like 0x696C20.
                texture->Release();
            }
            return true;
        }
    } // namespace

    bool Player::loadKrkrAtlasSourceLike_0x695DE8(
        detail::MotionNode::SourceState &source,
        ResourceManager *resourceManager,
        const ttstr &moduleKey) {
        // sub_695DE8 @0x695DE8 owns the complete path->module->atlas route and
        // is shared by Player_findSource and the render-time texture getter.
        const auto pieces = detail::splitTtstrLike_0x697D34(
            detail::widen(source.path), TJS_W('/'));
        if(pieces.empty() || pieces[0] != TJS_W("src")) {
            return false;
        }

        // 0x695F04..0x695F8C performs the ResourceManager/map lookup only
        // after the "src" prefix gate. Do not add a null guard that would
        // change the malformed native-dispatch boundary.
        const auto loadedIt = resourceManager->_loadedModules.find(moduleKey);
        if(loadedIt == resourceManager->_loadedModules.end()) {
            source.valid = false;
            return false;
        }
        auto &loadedResource = loadedIt->second;

        // 0x695F9C clears only the object variant.  Every other descriptor
        // field remains live until a later success/failure write reaches it.
        source.object.Clear();
        const ttstr sourceKey = detail::widen(source.path);
        auto sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
        if(sourceIt == loadedResource.krkrSourceEntries.end()) {
            // 0x6960B4..0x6960D0 consumes pieces[1]/pieces[2] without a size
            // check after the sole first-segment test above.
            if(!buildKrkrAtlasGroupLike_0x695DE8(
                   loadedResource, detail::narrow(pieces[1]),
                   detail::narrow(pieces[2]))) {
                return false;
            }
            sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
            // 0x696274..0x696290 converts a missing second lookup to a null
            // record pointer and then immediately dereferences record+0x18.
            // Do not add an end guard: pack failure/empty output is a native
            // invalid-access boundary, not a recoverable false result.
        }

        const auto &packed = sourceIt->second;
        source.valid = true;
        source.originX = static_cast<double>(packed.originX);
        source.originY = static_cast<double>(packed.originY);
        source.blank = false;
        source.width = static_cast<double>(
            packed.textureRect[2] - packed.textureRect[0]);
        source.height = static_cast<double>(
            packed.textureRect[3] - packed.textureRect[1]);
        source.clipLeft = packed.clip[0];
        source.clipTop = packed.clip[1];
        source.clipRight = packed.clip[2];
        source.clipBottom = packed.clip[3];
        source.textureRect = packed.textureRect;
        source.texture = packed.texture;
        return true;
    }

    void Player::findSourceForNodeLike_0x6948E8(detail::MotionNode &node) {
        auto &dst = node.source;
        ResourceManager *resourceManager = nativeRM();
        const ttstr motionContext =
            static_cast<ttstr>(_findMotionContextVariant);
        detail::LoadedResourceRecord *loadedResource = nullptr;
        if(resourceManager) {
            const auto loadedIt =
                resourceManager->_loadedModules.find(motionContext);
            if(loadedIt != resourceManager->_loadedModules.end()) {
                loadedResource = &loadedIt->second;
            }
        }
        const int sourceSpec = resourceManager ? resourceManager->_spec : 0;
        const std::string tracePath = motionContext.AsStdString();
        const std::string rawSource =
            detail::narrow(node.activeSlot().srcValue);
        const std::string rawIcon =
            detail::narrow(node.activeSlot().iconValue);

        if(!rawSource.empty() && rawSource != "blank") {
            if(sourceSpec == 2) {
                // 0x6949E4 clears only the object Variant on the Win route.
                // The remaining fields intentionally retain their prior bytes
                // until the exact branch below overwrites them.
                dst.object.Clear();
                if(!loadedResource) {
                    // 0x694B94 belongs only to the outer module-map miss.
                    dst.valid = false;
                } else {
                    PSB::PSBRawNode groupNode;
                    if(findWinSourceGroupLike_0x6948E8(
                           *loadedResource, rawSource, groupNode)) {
                        // 0x694C74..0x694FC0 completes the cache/load before
                        // 0x694FFC strictly navigates icon/<rawIcon>.
                        dst.texture = loadWinAtlasTextureLike_0x6948E8(
                            groupNode, *loadedResource, rawSource);
                        const PSB::PSBRawNode iconNode =
                            groupNode.GetDictionaryValueStrict("icon")
                                .GetDictionaryValueStrict(rawIcon.c_str());
                        dst.valid = true;
                        dst.originX = static_cast<double>(
                            iconNode.GetDictionaryValueStrict("originX")
                                .GetInt());
                        dst.originY = static_cast<double>(
                            iconNode.GetDictionaryValueStrict("originY")
                                .GetInt());
                        dst.width = static_cast<double>(
                            iconNode.GetDictionaryValueStrict("width")
                                .GetInt());
                        dst.height = static_cast<double>(
                            iconNode.GetDictionaryValueStrict("height")
                                .GetInt());
                        dst.blank = false;
                        dst.clipLeft = 0.0;
                        dst.clipTop = 0.0;
                        dst.clipRight = 1.0;
                        dst.clipBottom = 1.0;
                        const int left =
                            iconNode.GetDictionaryValueStrict("left").GetInt();
                        const int top =
                            iconNode.GetDictionaryValueStrict("top").GetInt();
                        dst.textureRect = {
                            left, top, left + static_cast<int>(dst.width),
                            top + static_cast<int>(dst.height)
                        };
                        detail::logoChainTraceLogf(
                            tracePath, "player.findSource", "0x6948E8",
                            _clampedEvalTime,
                            "spec=win group={} icon={} valid=1 atlas={} "
                            "size={}x{} rect=[{},{},{},{}]",
                            rawSource, rawIcon,
                            static_cast<const void *>(dst.texture), dst.width,
                            dst.height, dst.textureRect[0], dst.textureRect[1],
                            dst.textureRect[2], dst.textureRect[3]);
                        return;
                    }
                }
            } else if(sourceSpec == 1) {
                // 0x694BA4..0x694BCC stores the raw src owner only on the
                // KRKR route; spec=2 never synthesizes or overwrites path.
                dst.path = rawSource;
                if(_d3dDrawMode && loadKrkrAtlasSourceLike_0x695DE8(
                       dst, resourceManager, motionContext)) {
                    // 0x694C0C repeats the success byte written by 0x695DE8.
                    dst.valid = true;
                    detail::logoChainTraceLogf(
                        tracePath, "player.findSource", "0x6948E8",
                        _clampedEvalTime,
                        "spec=krkr-atlas path={} valid=1 atlas={} "
                        "size={}x{} rect=[{},{},{},{}]",
                        dst.path, static_cast<const void *>(dst.texture),
                        dst.width, dst.height, dst.textureRect[0],
                        dst.textureRect[1], dst.textureRect[2],
                        dst.textureRect[3]);
                    return;
                }
            }
        }

        // LABEL_142 / 0x6952E0 first nulls texture, then builds exactly
        // src + "/" + icon for the dispatch fallback.  When src is empty but
        // icon is not, the leading slash remains; an empty path also reaches
        // findSource.
        dst.texture = nullptr;
        std::string fallbackPath = rawSource;
        if(!rawIcon.empty()) {
            fallbackPath += "/" + rawIcon;
        }
        dst.object = findSource(detail::widen(fallbackPath));
        if(dst.object.Type() != tvtObject || !dst.object.AsObjectNoAddRef()) {
            dst.valid = false;
            return;
        }
        dst.valid = true;
        dst.width = detail::motionPropGetDouble(
            dst.object, TJS_W("width"), 0, &detail::widthMemberHint_guess);
        dst.height = detail::motionPropGetDouble(
            dst.object, TJS_W("height"), 0, &detail::heightMemberHint_guess);
        dst.originX = detail::motionPropGetDouble(
            dst.object, TJS_W("originX"), 0, &detail::originXMemberHint_guess);
        dst.originY = detail::motionPropGetDouble(
            dst.object, TJS_W("originY"), 0, &detail::originYMemberHint_guess);
        dst.blank = detail::motionPropGetBool(
            dst.object, TJS_W("blank"), 0, &detail::blankMemberHint_guess);
        const tTJSVariant clipValue = detail::motionPropGet(
            dst.object, TJS_W("clip"), 0, &detail::clipMemberHint_guess);
        if(clipValue.Type() == tvtObject && clipValue.AsObjectNoAddRef()) {
            dst.clipLeft = detail::motionPropGetDouble(
                clipValue, TJS_W("left"), 0, &detail::leftMemberHint_guess);
            dst.clipTop = detail::motionPropGetDouble(
                clipValue, TJS_W("top"), 0, &detail::topMemberHint_guess);
            dst.clipRight = detail::motionPropGetDouble(
                clipValue, TJS_W("right"), 0, &detail::rightMemberHint_guess);
            dst.clipBottom = detail::motionPropGetDouble(
                clipValue, TJS_W("bottom"), 0,
                &detail::bottomMemberHint_guess);
        } else {
            dst.clipLeft = 0.0;
            dst.clipTop = 0.0;
            dst.clipRight = 1.0;
            dst.clipBottom = 1.0;
        }
        dst.textureRect = { 0, 0, static_cast<int>(dst.width),
                            static_cast<int>(dst.height) };
        detail::logoChainTraceLogf(
            tracePath, "player.findSource", "0x6948E8",
            _clampedEvalTime, "spec={} path={} valid=1 blank={} size={}x{}",
            sourceSpec, fallbackPath, dst.blank ? 1 : 0, dst.width,
            dst.height);
    }

    bool Player::isExistMotion(ttstr name) {
        // Player_isExistMotion @0x6D07F4 calls the Player+992 ResourceManager
        // dispatch with (+1012 project key,
        // "motion/<Player+968>/<name>"). It does not probe storage paths or
        // populate a Player-local cache.
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        if(!rm) {
            return false;
        }

        tTJSVariant project = _findMotionContextVariant;
        tTJSVariant path(
            TJS_W("motion/") + _stealthChara + TJS_W("/") + name);
        tTJSVariant *args[] = { &project, &path };
        tTJSVariant result;
        static tjs_uint32 hint = 0;
        if(TJS_FAILED(rm->FuncCall(0, TJS_W("isExistMotion"), &hint,
                                   &result, 2, args, rm))) {
            return false;
        }
	return result.operator bool();
}

    // P3-B (c) (2026-06-05): removed the port-invented by-name
    //   `Player::requireLayerId(ttstr name)` (node-name reuse + by-name alloc).
    //   binary has NO by-name layer-id path anywhere ("requireLayerIdForName"
    //   string: 0 hits; "requireLayerId" only ever called numparams=0). The
    //   render path (emitRenderItem@0x6C4E28 LABEL_28) allocates a FRESH id via
    //   the no-arg RM dispatch FuncCall, gated only by the item+20 latch — it
    //   does not look up or reuse a node's layerId by name. Render now calls
    //   dispatchRequireLayerId() directly (PlayerRenderExecute.cpp).

    void Player::releaseLayerId(tjs_int id) { dispatchReleaseLayerId(id); }

    // P3-B (d): layer-id alloc/release via the Player+992 RM dispatch FuncCall
    //   (see Player.h note). FuncCall routes to the NCB-registered native
    //   ResourceManager::requireLayerId/releaseLayerId, so the result is the
    //   same id — only the call chain matches the binary (3 require sites + 1
    //   release site all go through the dispatch, never a direct native call).
    tjs_int Player::dispatchRequireLayerId() const {
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        if(!rm) {
            return 0;
        }
        // Per-callsite member-hint cache (binary passes a tjs_uint32* hint;
        //   0xFFFFFFFF / 0 = uncached, filled on first lookup).
        static tjs_uint32 hint = 0;
        tTJSVariant result;
        // FuncCall(flag, membername, hint, result, numparams=0, params=NULL,
        //   objthis) — aligned to requireLayerId@0x6B4A6C/0x6C4E28/0x6DE738.
        if(TJS_FAILED(rm->FuncCall(0, TJS_W("requireLayerId"), &hint, &result,
                                   0, nullptr, rm))) {
            return 0;
        }
        return static_cast<tjs_int>(result.AsInteger());
    }

    void Player::dispatchReleaseLayerId(tjs_int id) const {
        iTJSDispatch2 *rm = _resourceManager.Type() == tvtObject
            ? _resourceManager.AsObjectNoAddRef()
            : nullptr;
        if(!rm) {
            return;
        }
        static tjs_uint32 hint = 0;
        tTJSVariant idVar(static_cast<tjs_int>(id));
        tTJSVariant *args[1] = { &idVar };
        // numparams=1 {id}, result discarded — aligned to releaseLayerId via
        //   resetAndReleaseNodes@0x6B56F8.
        rm->FuncCall(0, TJS_W("releaseLayerId"), &hint, nullptr, 1, args, rm);
    }


} // namespace motion
