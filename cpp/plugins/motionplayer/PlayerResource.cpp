// PlayerResource.cpp — Resource management: unload, findMotion, layerId
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "MotionRenderBackend.h"
#include "RenderManager.h"
#include "SourceCache.h"
#include "tjsUtils.h"
#include "../../core/visual/ogl/imagepacker.h"

#include <algorithm>
#include <memory>

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

        struct DispatchRelease_guess {
            void operator()(iTJSDispatch2 *dispatch) const noexcept {
                if(dispatch) {
                    dispatch->Release();
                }
            }
        };

        using RetainedDispatch_guess =
            std::unique_ptr<iTJSDispatch2, DispatchRelease_guess>;

        RetainedDispatch_guess retainVariantObject_guess(
            const tTJSVariant &owner) {
            tTJSVariant copy(owner);
            RetainedDispatch_guess dispatch(copy.AsObject());
            copy.Clear();
            return dispatch;
        }

        bool findWinSourceGroup_guess(
            const PSB::PSBRawNode &root, const ttstr &liveGroupKey,
            PSB::PSBRawNode &groupNode) {
            const PSB::PSBRawNode sourceRoot =
                root.GetDictionaryValueStrict("source");
            // Convert only after retaining root and strictly resolving source.
            // This UTF-8 key temporary is destroyed before sourceRoot; neither
            // owner is reused as the later texture-cache ttstr key.
            const std::string group = detail::narrow(liveGroupKey);
            // The four-reference find-source family uses the non-throwing
            // group lookup and transfers to the spec-1/fallback route on miss.
            return sourceRoot.GetDictionaryValue(group.c_str(), groupNode);
        }

        iTVPTexture2D *loadWinAtlasTexture_guess(
            const PSB::PSBRawNode &groupNode,
            detail::LoadedResourceRecord &loadedResource,
            const ttstr &liveGroupKey) {
            // Both probes borrow the caller's live ttstr object. Besides
            // preserving its backing/cached hash, this means a render-manager
            // callback may replace the slot between the miss and operator[],
            // publishing the just-built texture under the new key.
            if(const auto it =
                   loadedResource.winSourceTextures.find(liveGroupKey);
               it != loadedResource.winSourceTextures.end()) {
                return it->second.texture;
            }

            const PSB::PSBRawNode textureNode =
                groupNode.GetDictionaryValueStrict("texture");
            // These two conversions are performed and discarded; width/height
            // below do not fall back to them.
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
            // The caller passes an uninitialized 32-bit size slot. A null
            // resource chunk makes every four-reference GetResource helper
            // return null before touching that slot.
            std::uint32_t sourceSize;
            const std::uint8_t *sourcePixels =
                textureNode.GetDictionaryValueStrict("pixel")
                    .GetResource(sourceSize);
            // The strict-lookup temporary is released before sourceSize/pixels
            // are consumed; textureNode keeps the raw owner and borrowed chunk
            // storage alive through the upload.

            // Both operations use 32-bit registers before TJSAlignedAlloc.
            // Keep the wrap before the allocator instead of widening through
            // host size_t.
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
                    // Free before constructing the unsupported-format
                    // exception argument.
                    TJS::TJSAlignedDealloc(bgra);
                    TVPThrowExceptionMessage(
                        TJS_W("MotionPlayer.findSource: Unsupported texture format '%1'"),
                        ttstr(type));
                }
                // This path reads [alpha,luminance] and writes
                // [luminance,luminance,luminance,alpha]. Its signed W32
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
            // Free before map insertion. There is no texture-null guard: the
            // slot is still inserted/replaced before the unconditional
            // construction-reference Release.
            TJS::TJSAlignedDealloc(bgra);
            // The original uses operator[] after the explicit miss probe.  A
            // new node is published with a null mapped pointer before texture
            // assignment; allocation/rehash failure therefore occurs while
            // the texture still owns only its construction reference.
            // That reference is a raw local in all four exception ABIs: an
            // unwind from operator[] (or from releasing a replaced texture)
            // does not run a compensating texture Release.
            auto &cached = loadedResource.winSourceTextures[liveGroupKey];
            cached.setTexture(texture);
            // The map value retains the texture; releasing its construction
            // reference leaves the nested map as sole owner. This is a normal-
            // path operation, not a scope guard; a throwing Release is not
            // retried by the surrounding unwind cleanup either.
            texture->Release();
            return cached.texture;
        }

        struct KrkrAtlasRecord_guess;

        // The four atlas functions embed the packer rect between their two
        // owning endpoint members, pass that rect subobject to ImagePacker, and
        // recover the full record through the explicit back-pointer following
        // rect_xywhf. Tail scalars remain uninitialized until pass 2.
        struct KrkrAtlasRect_guess : ImagePacker::rect_xywhf {
            KrkrAtlasRecord_guess *record;
            int contentWidth;
            int contentHeight;
            std::uint8_t *bgra;

            KrkrAtlasRect_guess(
                int x, int y, int width, int height) :
                ImagePacker::rect_xywhf(x, y, width, height) {}
        };

        // Native member order follows the bundled STL family. Android's old
        // libstdc++ records are iconNode/rect/sourceKey; iOS libc++ records are
        // sourceKey/rect/iconNode. That changes vector-growth copy order and
        // reverse destruction order, so the Web libc++ build must use the iOS
        // shape. Cleanup never reads/frees rect.bgra. A user-declared default
        // destructor keeps vector growth copy-shaped.
        struct KrkrAtlasRecord_guess {
#if defined(_LIBCPP_VERSION)
            std::string sourceKey;
            KrkrAtlasRect_guess rect;
            PSB::PSBRawNode iconNode;
#else
            PSB::PSBRawNode iconNode;
            KrkrAtlasRect_guess rect;
            std::string sourceKey;
#endif

            KrkrAtlasRecord_guess(
                const PSB::PSBRawNode &node, int width, int height,
                std::string key) :
#if defined(_LIBCPP_VERSION)
                sourceKey(std::move(key)),
                rect(0, 0, addW32(width, 1), addW32(height, 1)),
                iconNode(node) {}
#else
                iconNode(node),
                rect(0, 0, addW32(width, 1), addW32(height, 1)),
                sourceKey(std::move(key)) {}
#endif

            ~KrkrAtlasRecord_guess() = default;
        };

        // Android inlines this byte-RL loop into the atlas loader; both iOS
        // references retain a shared helper also used by ObjSource texture
        // materialization. The original source name is stripped, hence
        // `_guess`.
        void decodePsbRL8_guess(
            std::uint8_t *destination, const std::uint8_t *source,
            std::uint32_t sourceSize) {
            // The top bit makes the 32-bit size negative and skips all work.
            // Positive streams have no packet-boundary or output-capacity
            // checks: the final packet may over-read and either form may write
            // past the destination supplied by its caller.
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

        // Android arm64 inlines the pixel-RL loop; the other three references
        // retain a shared helper also used by ObjSource materialization. The
        // original source name is stripped.
        void decodePsbRL32_guess(
            std::uint8_t *destination, const std::uint8_t *source,
            std::uint32_t sourceSize) {
            // As with RL8, size is first reinterpreted as signed int32 and no
            // packet or destination bounds are checked. Run packets read a
            // possibly unaligned four-byte pixel at source+1.
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

        void decodeKrkrAtlasRecord_guess(
            PSB::PSBRawNode &persistentNode,
            KrkrAtlasRecord_guess &record) {
            auto &rect = record.rect;
            // A per-record lookup result is constructed while the function-wide
            // persistent node remains live. "pal" is tested on that previous
            // value before the current record node is copied into it. Source
            // enumeration leaves the final enumerated icon in this holder, so
            // for nonempty records [r0, ..., rn-1] the palette-mode sequence is
            // [pal(rn-1), pal(r0), ..., pal(rn-2)]. The assignment below still
            // makes all pixel/palette payload reads come from the current record;
            // only the branch mode is rotated by one record. All four references
            // preserve this boundary behavior.
            PSB::PSBRawNode scratch;
            const bool hasPalette =
                persistentNode.ContainsDictionaryKey("pal");
            // Both dimensions are sign-extended for the logical product, while
            // allocation sizes deliberately truncate that product to 32 bits.
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
                    decodePsbRL8_guess(
                        indexes, pixelData, resourceSize);
                } else {
                    // Copy the PSB resource length rather than the destination
                    // pixel count.
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
                    decodePsbRL32_guess(
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
            // Gate on the signed low 32 bits, but compare the scan index against
            // the full signed product.
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
                // An entirely transparent image drops its pixel buffer and
                // replaces the padded rectangle with 2x2. Stored content width
                // and height are not rewritten.
                TJS::TJSAlignedDealloc(rect.bgra);
                rect.bgra = nullptr;
                rect.w = 2;
                rect.h = 2;
            }
        }

        bool buildKrkrAtlasGroup_guess(
            detail::LoadedResourceRecord &loadedResource,
            const PSB::PSBRawNode &root,
            detail::MotionNode::SourceState &source,
            const std::string &requestedGroup,
            const std::string &requestedIcon) {
            // One raw-node scratch remains alive across the requested-icon
            // probe, source enumeration, and packed-record loop. Assignments
            // release its previous owner before installing the next node.
            PSB::PSBRawNode iconNode;
            const PSB::PSBRawNode sourceRoot =
                root.GetDictionaryValueStrict("source");
            // Cache-miss invalidation occurs only after strict source-root
            // acquisition. A throwing source lookup preserves the old flag;
            // every later ordinary miss/failure leaves it false.
            source.valid = false;
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
            // Release the temporary icon root before testing the lookup result
            // and entering the failure cleanup path.
            if(!requestedIconFound) {
                return false;
            }

            // Records are appended by value. Observed ABI strides are 0x40
            // (Android arm64), 0x2C (Android armv7), 0x50 (iOS arm64), and
            // 0x34 (iOS armv7). Growth follows the STL-specific member order
            // above, copies each raw owner/string, then destroys the old range;
            // there is no per-record heap owner layer.
            std::vector<KrkrAtlasRecord_guess> records;
            // This outer key vector is constructed after `records` and remains
            // alive across record decoding, packing, and atlas upload. Its
            // strings are released immediately before the record vector.
            const std::vector<std::string> groupKeys =
                sourceRoot.GetDictionaryKeys();
            for(const auto &group : groupKeys) {
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

            // Do not reset iconNode here. Each enumeration assignment replaced
            // it, so a nonempty record set deliberately leaves the last icon as
            // the first record's palette-mode seed.

            // Android completes every potentially reallocating value-vector
            // append before publishing rect subobject pointers.  It then
            // decodes all records in a second, encounter-order pass. Each decode
            // replaces iconNode only after testing the previous value, advancing
            // the rotated palette-mode state for the next record.
            std::vector<ImagePacker::rect_xywhf *> recordPointers;
            for(auto &record : records) {
                record.rect.record = &record;
                record.rect.contentWidth = subtractW32(record.rect.w, 1);
                record.rect.contentHeight = subtractW32(record.rect.h, 1);
                recordPointers.push_back(&record.rect);
                decodeKrkrAtlasRecord_guess(iconNode, record);
            }

            std::vector<ImagePacker::bin> bins;
            const int maxSide = static_cast<int>(TVPMaxTextureSize);
            // All four atlas implementations ignore ImagePacker::pack's
            // boolean result.  A failure therefore leaves no packed cache
            // entry and reaches the same unchecked second lookup below.
            (void)ImagePacker::pack(
                recordPointers.data(), static_cast<int>(recordPointers.size()),
                maxSide, bins);

            for(auto &bin : bins) {
                // ImagePacker::pack appends one 0x0 bin for an empty record
                // set. The atlas function still creates that empty texture and
                // releases its construction reference.
                const tjs_uint atlasStride =
                    static_cast<tjs_uint>(bin.size.w) << 2;
                // Each record writes metadata, uploads its own packed sub-rect,
                // and frees its BGRA buffer before advancing.
                auto *renderManager =
                    render_backend_guess::getPrivateOpenGLRenderManager_guess();
                auto *texture = renderManager->CreateTexture2D(
                    nullptr, signedW32(atlasStride),
                    static_cast<unsigned int>(bin.size.w),
                    static_cast<unsigned int>(bin.size.h),
                    TVPTextureFormat::RGBA,
                    RENDER_CREATE_TEXTURE_FLAG_STATIC);
                // The page-construction reference remains a raw local until
                // the normal page tail. No four-reference landing pad releases
                // it if insertion, replacement, metadata access, or Update
                // unwinds out of this loop.
                for(auto *baseRect : bin.rects) {
                    auto *rect =
                        static_cast<KrkrAtlasRect_guess *>(baseRect);
                    auto *record = rect->record;
                    // Native code establishes this owner before source-key
                    // conversion and operator[]. If either later step throws,
                    // unwind cleanup observes the current record node here.
                    iconNode = record->iconNode;
                    // operator[] publishes or finds the persistent descriptor
                    // before any field is written.  The following mutations
                    // deliberately are not transactional: a throwing metadata
                    // getter leaves the new/existing cache entry partially
                    // updated, exactly as in all four references. Entries that
                    // already passed setTexture keep their retained references,
                    // while the separate page-construction reference leaks.
                    auto &entry = loadedResource.krkrSourceEntries[
                        detail::widen(record->sourceKey)];
                    entry.setTexture(texture);
                    entry.originX =
                        iconNode.GetDictionaryValueStrict("originX").GetInt();
                    entry.originY =
                        iconNode.GetDictionaryValueStrict("originY").GetInt();
                    // Store atlas x/y followed by inclusive right/bottom in the
                    // four-int descriptor.
                    entry.textureRect = tTVPRect(
                        rect->x,
                        rect->y,
                        subtractW32(addW32(rect->x, rect->w), 1),
                        subtractW32(addW32(rect->y, rect->h), 1));
                    // Pass the same raw-node storage as source and out. A hit
                    // descends iconNode in place; a miss leaves it unchanged.
                    if(iconNode.GetDictionaryValue("clip", iconNode)) {
                        // Each getter result is committed before asking for the
                        // next field. If a later lookup/conversion throws, the
                        // already-written prefix remains visible in the cache.
                        entry.clip[0] = iconNode
                            .GetDictionaryValueStrict("left").GetDouble();
                        entry.clip[1] = iconNode
                            .GetDictionaryValueStrict("top").GetDouble();
                        entry.clip[2] = iconNode
                            .GetDictionaryValueStrict("right").GetDouble();
                        entry.clip[3] = iconNode
                            .GetDictionaryValueStrict("bottom").GetDouble();
                    } else {
                        entry.clip = {0.0, 0.0, 1.0, 1.0};
                    }
                    if(rect->bgra != nullptr) {
                        // Use content width for pitch and pass the exact
                        // persistent descriptor subobject, then free bgra
                        // without clearing it.
                        texture->Update(
                            rect->bgra, TVPTextureFormat::RGBA,
                            signedW32(
                                static_cast<tjs_uint>(rect->contentWidth) << 2),
                            entry.textureRect);
                        TJS::TJSAlignedDealloc(rect->bgra);
                    }
                }
                // Each cached icon entry took its own AddRef; release the
                // page-construction reference only on normal loop completion.
                texture->Release();
            }
            return true;
        }
    } // namespace

    bool Player::loadKrkrAtlasSource_guess(
        detail::MotionNode::SourceState &source,
        ResourceManager *resourceManager,
        const ttstr &moduleKey) {
        // This owns the complete path -> module -> atlas route shared by the
        // four-reference find-source and render-time texture-getter paths.
        const auto pieces = detail::splitTtstr_guess(
            source.path, TJS_W('/'));
        if(pieces.empty() || pieces[0] != TJS_W("src")) {
            return false;
        }

        // Perform the ResourceManager/map lookup only after the "src" prefix
        // gate. Do not add a null guard that changes the malformed dispatch
        // boundary.
        const auto loadedIt = resourceManager->_loadedModules.find(moduleKey);
        if(loadedIt == resourceManager->_loadedModules.end()) {
            source.valid = false;
            return false;
        }
        auto &loadedResource = loadedIt->second;

        // Clear only the object variant. Every other descriptor field remains
        // live until a later success/failure write reaches it.
        source.object.Clear();
        // The four native loaders retain this root owner before the cache probe,
        // even on a hit, and release it only after projection/return cleanup.
        const PSB::PSBRawNode root(loadedResource.file);
        // Keep a reference to the persistent field. Atlas construction can
        // reenter script and replace it; the native post-build retry observes
        // that replacement even though `pieces` remains the entry snapshot.
        const ttstr &sourceKey = source.path;
        auto sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
        if(sourceIt == loadedResource.krkrSourceEntries.end()) {
            // Consume pieces[1]/pieces[2] without a size check after the sole
            // first-segment test above.
            if(!buildKrkrAtlasGroup_guess(
                   loadedResource, root, source,
                   detail::narrow(pieces[1]),
                   detail::narrow(pieces[2]))) {
                return false;
            }
            sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
            // A missing second lookup becomes a null record pointer and is
            // immediately dereferenced. Do not add an end guard: pack failure
            // or empty output is a native invalid-access boundary.
        }

        const auto &packed = sourceIt->second;
        source.valid = true;
        source.originX = static_cast<double>(packed.originX);
        source.originY = static_cast<double>(packed.originY);
        source.blank = false;
        source.width = static_cast<double>(packed.textureRect.get_width());
        source.height = static_cast<double>(packed.textureRect.get_height());
        source.clipLeft = packed.clip[0];
        source.clipTop = packed.clip[1];
        source.clipRight = packed.clip[2];
        source.clipBottom = packed.clip[3];
        source.textureRect = {
            packed.textureRect.left,
            packed.textureRect.top,
            packed.textureRect.right,
            packed.textureRect.bottom,
        };
        source.texture = packed.texture;
        return true;
    }

    void Player::findSourceForNode_guess(detail::MotionNode &node) {
        auto &dst = node.source;
        // The resolver starts with strict AsObject(). It AddRefs a nonnull
        // ResourceManager dispatch, but none of the four native normal/unwind
        // tails Releases that added reference. Every entry with a nonnull
        // dispatch therefore leaks one reference before copying the context
        // Variant; a typed-null Object has no dispatch to AddRef.
        iTJSDispatch2 *resourceManagerDispatch =
            _findSourceResourceManager.AsObject();
        tTJSVariant motionContextArgument(_findMotionContextVariant);
        ResourceManager *resourceManager =
            ncbInstanceAdaptor<ResourceManager>::GetNativeInstance(
                resourceManagerDispatch);
        const ttstr &sourceValue = node.activeSlot().srcValue;
        const ttstr &iconValue = node.activeSlot().iconValue;
        const bool sourceHasBacking =
            sourceValue.AsVariantStringNoAddRef() != nullptr;
        int sourceSpec = 0;

        // Native tests the backing pointer, not Length. An allocated-empty src
        // therefore enters spec routing, unlike a null-backed src. Context is
        // still only a Variant owner at this point; generic/blank routes never
        // perform its potentially dispatching string conversion.
        if(sourceHasBacking && sourceValue != TJS_W("blank")) {
            // There is no friendly null-native recovery after a nonblank src.
            sourceSpec = resourceManager->_spec;
            if(sourceSpec == 2) {
                // The Win route clears only the object Variant.
                // The remaining fields intentionally retain their prior bytes
                // until the exact branch below overwrites them.
                dst.object.Clear();
                const ttstr motionContext =
                    static_cast<ttstr>(motionContextArgument);
                const auto loadedIt =
                    resourceManager->_loadedModules.find(motionContext);
                if(loadedIt == resourceManager->_loadedModules.end()) {
                    // This invalid write belongs only to the outer module-map
                    // miss.
                    dst.valid = false;
                } else {
                    auto &loadedResource = loadedIt->second;
                    // Native retains an independent root owner before the
                    // strict source-root lookup and keeps it alive across the
                    // group node, texture cache work and icon projection.
                    const PSB::PSBRawNode winRoot(loadedResource.file);
                    PSB::PSBRawNode groupNode;
                    if(findWinSourceGroup_guess(
                           winRoot, sourceValue, groupNode)) {
                        // Complete cache/load before strictly navigating
                        // icon/<live icon argument>.
                        dst.texture = loadWinAtlasTexture_guess(
                            groupNode, loadedResource, sourceValue);
                        PSB::PSBRawNode iconNode;
                        {
                            // The icon dictionary owner and UTF-8 live-icon
                            // key are both destroyed before valid=true. The
                            // selected iconNode remains as the sole nested raw
                            // owner until projection completes.
                            const PSB::PSBRawNode iconRoot =
                                groupNode.GetDictionaryValueStrict("icon");
                            const std::string rawIcon =
                                detail::narrow(iconValue);
                            iconNode = iconRoot.GetDictionaryValueStrict(
                                rawIcon.c_str());
                        }
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
                            left, top,
                            static_cast<int>(
                                dst.width + static_cast<double>(left)),
                            static_cast<int>(
                                dst.height + static_cast<double>(top))
                        };
                        return;
                    }
                }
            } else if(sourceSpec == 1) {
                // Store the raw src owner only on the KRKR route; spec=2 never
                // synthesizes or overwrites path.
                dst.path = sourceValue;
                // The persistent useD3D byte gates this atlas attempt. A
                // failed/disabled attempt continues through the same generic
                // src/icon findSource fallback below.
                if(_d3dDrawMode) {
                    const ttstr motionContext =
                        static_cast<ttstr>(motionContextArgument);
                    if(loadKrkrAtlasSource_guess(
                           dst, resourceManager, motionContext)) {
                        // The caller repeats the success byte written by the
                        // atlas resolver.
                        dst.valid = true;
                        return;
                    }
                }
            }
        }

        // The fallback first nulls texture, then checks the icon's backing
        // pointer rather than Length. A nonnull allocated-empty icon still
        // appends '/', while a null src plus backed icon produces '/<icon>'.
        dst.texture = nullptr;
        ttstr fallbackPath(sourceValue);
        if(iconValue.AsVariantStringNoAddRef() != nullptr) {
            fallbackPath += TJS_W("/");
            fallbackPath += iconValue;
        }
        const tjs_error findStatus = dispatchFindSource_guess(
            resourceManagerDispatch, motionContextArgument,
            fallbackPath, dst.object);
        // Native tests the raw return against zero, not TJS_FAILED(), and the
        // result target already aliases dst.object. A nonzero status can thus
        // leave a dispatch-written partial object while forcing valid=false.
        if(findStatus != TJS_S_OK || dst.object.Type() == tvtVoid) {
            dst.valid = false;
            return;
        }
        // This store precedes strict Object conversion. A non-Void wrong type,
        // typed-null Object, or any later getter/conversion exception therefore
        // leaves valid=true plus the successfully committed field prefix.
        dst.valid = true;
        // The independent accessor AddRefs the strict Object receiver, then its
        // setup Variant is destroyed. It survives re-entrant getters that
        // replace dst.object and is the sole receiver owner for the complete
        // property sequence.
        ncbPropAccessor sourceObject{tTJSVariant(dst.object)};
        iTJSDispatch2 *sourceDispatch = sourceObject.GetDispatch();
        // All named-property helpers deliberately ignore the PropGet status,
        // convert the resulting (possibly still Void) Variant, and destroy it
        // before the destination store. The hint words are process-wide slots,
        // not per-node or per-call caches.
        dst.width = detail::motionPropGetDouble(
            sourceDispatch, TJS_W("width"), 0,
            &detail::widthMemberHint_guess);
        dst.height = detail::motionPropGetDouble(
            sourceDispatch, TJS_W("height"), 0,
            &detail::heightMemberHint_guess);
        dst.originX = detail::motionPropGetDouble(
            sourceDispatch, TJS_W("originX"), 0,
            &detail::originXMemberHint_guess);
        dst.originY = detail::motionPropGetDouble(
            sourceDispatch, TJS_W("originY"), 0,
            &detail::originYMemberHint_guess);
        dst.blank = detail::motionPropGetBool(
            sourceDispatch, TJS_W("blank"), 0,
            &detail::blankMemberHint_guess);
        const tTJSVariant clipValue = detail::motionPropGet(
            sourceDispatch, TJS_W("clip"), 0,
            &detail::clipMemberHint_guess);
        if(clipValue.Type() == tvtObject) {
            // Object includes typed-null here. The second strict accessor owns
            // the clip receiver independently until all four reads finish.
            ncbPropAccessor clipObject{tTJSVariant(clipValue)};
            iTJSDispatch2 *clipDispatch = clipObject.GetDispatch();
            // Raw UTF-16LE inspection in all four references confirms the full
            // names left/top/right/bottom; IDA's occasional l/t/r/b rendering
            // is only a truncated wide-string display artifact.
            dst.clipLeft = detail::motionPropGetDouble(
                clipDispatch, TJS_W("left"), 0,
                &detail::leftMemberHint_guess);
            dst.clipTop = detail::motionPropGetDouble(
                clipDispatch, TJS_W("top"), 0,
                &detail::topMemberHint_guess);
            dst.clipRight = detail::motionPropGetDouble(
                clipDispatch, TJS_W("right"), 0,
                &detail::rightMemberHint_guess);
            dst.clipBottom = detail::motionPropGetDouble(
                clipDispatch, TJS_W("bottom"), 0,
                &detail::bottomMemberHint_guess);
        } else {
            // This branch writes the complete default quartet before touching
            // textureRect; it is not four independently dispatching getters.
            dst.clipLeft = 0.0;
            dst.clipTop = 0.0;
            dst.clipRight = 1.0;
            dst.clipBottom = 1.0;
        }
        // References write zero left/top, then use their target FP-to-signed
        // conversion instruction for width/height. static_cast<int> matches
        // finite in-range truncation toward zero; nonfinite/out-of-range target
        // instruction behavior remains an explicit portability boundary.
        dst.textureRect = { 0, 0, static_cast<int>(dst.width),
                            static_cast<int>(dst.height) };
    }

    bool Player::isExistMotion(ttstr name) {
        // The first parameter aliases the persistent context member itself;
        // the dispatch can therefore replace it in place.  Call status is not
        // the result boundary: the output Variant is always converted to bool.
        // Native code finishes the path Variant before strictly resolving the
        // borrowed ResourceManager receiver; it does not retain another owner.
        tTJSVariant path(
            TJS_W("motion/") + _stealthChara + TJS_W("/") + name);
        iTJSDispatch2 *rm = _resourceManager.AsObjectNoAddRef();
        tTJSVariant *args[] = { &_findMotionContextVariant, &path };
        tTJSVariant result;
        static tjs_uint32 isExistMotionMemberHint_guess = 0;
        (void)rm->FuncCall(0, TJS_W("isExistMotion"),
                           &isExistMotionMemberHint_guess,
                           &result, 2, args, rm);
        return result.operator bool();
    }

    // The render path allocates a fresh layer id through the ResourceManager's
    // no-argument dispatch call, gated only by the prepared item's require-layer
    // latch. It does not look up or reuse a node id by layer name.

    void Player::releaseLayerId(tjs_int id) { dispatchReleaseLayerId(id); }

    // Allocation and release both route through the retained ResourceManager
    // dispatch and its NCB-registered methods. The Player never shortcuts this
    // boundary through a cached native ResourceManager pointer.
    tjs_int Player::dispatchRequireLayerId(tjs_uint32 *hint) const {
        auto rm = retainVariantObject_guess(_resourceManager);
        static tjs_uint32 defaultHint_guess = 0;
        if(!hint) {
            hint = &defaultHint_guess;
        }
        tTJSVariant result;
        (void)rm->FuncCall(0, TJS_W("requireLayerId"), hint, &result,
                           0, nullptr, rm.get());
        return static_cast<tjs_int>(result.AsInteger());
    }

    void Player::dispatchReleaseLayerId(
        tjs_int id, tjs_uint32 *hint) const {
        auto rm = retainVariantObject_guess(_resourceManager);
        static tjs_uint32 defaultHint_guess = 0;
        if(!hint) {
            hint = &defaultHint_guess;
        }
        tTJSVariant idVar(static_cast<tjs_int>(id));
        tTJSVariant *args[1] = { &idVar };
        (void)rm->FuncCall(0, TJS_W("releaseLayerId"), hint,
                           nullptr, 1, args, rm.get());
    }


} // namespace motion
