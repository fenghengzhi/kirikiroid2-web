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

        bool findWinSourceGroup_guess(
            detail::LoadedResourceRecord &loadedResource,
            const std::string &group, PSB::PSBRawNode &groupNode) {
            const PSB::PSBRawNode root(loadedResource.file);
            const PSB::PSBRawNode sourceRoot =
                root.GetDictionaryValueStrict("source");
            // The four-reference find-source family uses the non-throwing
            // group lookup and transfers to the spec-1/fallback route on miss.
            return sourceRoot.GetDictionaryValue(group.c_str(), groupNode);
        }

        iTVPTexture2D *loadWinAtlasTexture_guess(
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
            // resource chunk leaves it untouched in
            // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_599AC4,
            // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_4DD9D8,
            // Kirikiroid2_1.3.9_iOS_arm64!sub_1000EDF78, and
            // Kirikiroid2_1.3.9_iOS_armv7!sub_EA1F0.
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
            auto [cached, inserted] =
                loadedResource.winSourceTextures.try_emplace(groupKey);
            (void)inserted;
            cached->second.setTexture(texture);
            // The map value retains the texture; releasing its construction
            // reference leaves the nested map as sole owner.
            texture->Release();
            return cached->second.texture;
        }

        struct KrkrAtlasRecord_guess;

        // The four atlas functions store the rect immediately after the raw
        // node (offset 0x10 on 64-bit, 0x08 on 32-bit), pass that subobject to
        // ImagePacker, and recover the record through the explicit pointer
        // following rect_xywhf. Tail scalars remain uninitialized until pass 2.
        struct KrkrAtlasRect_guess : ImagePacker::rect_xywhf {
            KrkrAtlasRecord_guess *record;
            int contentWidth;
            int contentHeight;
            std::uint8_t *bgra;

            KrkrAtlasRect_guess(
                int x, int y, int width, int height) :
                ImagePacker::rect_xywhf(x, y, width, height) {}
        };

        // Cleanup walks a contiguous value vector, destroys sourceKey and
        // releases iconNode, but never reads/frees rect.bgra. A user-declared
        // default destructor keeps vector growth copy-shaped.
        struct KrkrAtlasRecord_guess {
            PSB::PSBRawNode iconNode;
            KrkrAtlasRect_guess rect;
            std::string sourceKey;

            KrkrAtlasRecord_guess(
                const PSB::PSBRawNode &node, int width, int height,
                std::string key) :
                iconNode(node),
                rect(0, 0, addW32(width, 1), addW32(height, 1)),
                sourceKey(std::move(key)) {}

            ~KrkrAtlasRecord_guess() = default;
        };

        // Atlas-path four-reference mapping:
        // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6931C8 (inline),
        // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_570F54 (inline),
        // Kirikiroid2_1.3.9_iOS_arm64!sub_1000F5510 called by sub_1000F4098,
        // and Kirikiroid2_1.3.9_iOS_armv7!sub_F1F6A called by sub_F0BE4.
        // The standalone iOS helpers are shared with ObjSource materialization;
        // this evidence chain is rooted in the four atlas callers. The original
        // source name is stripped, hence `_guess`.
        void decodePsbRL8_guess(
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

        // Atlas-path four-reference mapping:
        // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6931C8 (inline),
        // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_571DA4 called by
        // sub_570F54, Kirikiroid2_1.3.9_iOS_arm64!sub_1000F5474 called by
        // sub_1000F4098, and Kirikiroid2_1.3.9_iOS_armv7!sub_F1F10 called by
        // sub_F0BE4. The standalone helpers are shared with ObjSource
        // materialization; this evidence chain is rooted in the atlas callers.
        // The original source name is stripped.
        void decodePsbRL32_guess(
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

        void decodeKrkrAtlasRecord_guess(
            PSB::PSBRawNode &persistentNode,
            KrkrAtlasRecord_guess &record) {
            auto &rect = record.rect;
            // A per-record lookup result is constructed while the function-wide
            // persistent node remains live. "pal" is tested on that previous
            // value before the current record node is copied into it; this
            // one-record-lagged gate occurs in all four references.
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
            const std::string &requestedGroup,
            const std::string &requestedIcon) {
            const PSB::PSBRawNode root(loadedResource.file);
            // One raw-node scratch remains alive across the requested-icon
            // probe, source enumeration, and packed-record loop. Assignments
            // release its previous owner before installing the next node.
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
            // Release the temporary icon root before testing the lookup result
            // and entering the failure cleanup path.
            if(!requestedIconFound) {
                return false;
            }

            // Records are appended by value. Observed ABI strides are 0x40
            // (Android arm64), 0x2C (Android armv7), 0x50 (iOS arm64), and
            // 0x34 (iOS armv7). Growth copies each raw owner/string, then
            // destroys the old range; there is no per-record heap owner layer.
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

            // Android completes every potentially reallocating value-vector
            // append before publishing rect subobject pointers.  It then
            // decodes all records in a second, encounter-order pass.
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
            // The four atlas callers ignore ImagePacker::pack's boolean result:
            // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6931C8@0x693C94
            // calls the same binary's sub_A6DA58@0xA6DA58;
            // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_570F54@0x5717A8
            // calls the same binary's sub_79436C@0x79436C;
            // Kirikiroid2_1.3.9_iOS_arm64!sub_1000F4098@0x1000F4C30 calls the
            // same binary's sub_100054E20@0x100054E20; and
            // Kirikiroid2_1.3.9_iOS_armv7!sub_F0BE4@0xF15FA calls the same
            // binary's sub_53EB8@0x53EB8.
            // A failure therefore leaves no packed cache entry and reaches the
            // same unchecked second lookup below.
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
                auto *texture = TVPGetRenderManager()->CreateTexture2D(
                    nullptr, signedW32(atlasStride),
                    static_cast<unsigned int>(bin.size.w),
                    static_cast<unsigned int>(bin.size.h),
                    TVPTextureFormat::RGBA,
                    RENDER_CREATE_TEXTURE_FLAG_STATIC);
                for(auto *baseRect : bin.rects) {
                    auto *rect =
                        static_cast<KrkrAtlasRect_guess *>(baseRect);
                    auto *record = rect->record;
                    detail::PackedSourceAtlasEntry entry;
                    entry.setTexture(texture);
                    // Copy-assign the record node into the persistent scratch
                    // before any metadata reads.
                    iconNode = record->iconNode;
                    entry.originX =
                        iconNode.GetDictionaryValueStrict("originX").GetInt();
                    entry.originY =
                        iconNode.GetDictionaryValueStrict("originY").GetInt();
                    // Store atlas x/y followed by inclusive right/bottom in the
                    // four-int descriptor.
                    entry.textureRect = {
                        rect->x,
                        rect->y,
                        subtractW32(addW32(rect->x, rect->w), 1),
                        subtractW32(addW32(rect->y, rect->h), 1),
                    };
                    // Pass the same raw-node storage as source and out. A hit
                    // descends iconNode in place; a miss leaves it unchanged.
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
                        // Use content width for pitch, pass the packed descriptor
                        // as a tTVPRect, then free bgra without clearing it.
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
                // Each cached icon entry took its own AddRef; release the
                // page-construction reference.
                texture->Release();
            }
            return true;
        }
    } // namespace

    bool Player::loadKrkrAtlasSource_guess(
        detail::MotionNode::SourceState &source,
        ResourceManager *resourceManager,
        const ttstr &moduleKey) {
        // Four-reference mapping:
        // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6931C8,
        // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_570F54,
        // Kirikiroid2_1.3.9_iOS_arm64!sub_1000F4098, and
        // Kirikiroid2_1.3.9_iOS_armv7!sub_F0BE4. This owns the complete
        // path->module->atlas route shared by find-source and the render-time
        // texture getter.
        const auto pieces = detail::splitTtstr_guess(
            detail::widen(source.path), TJS_W('/'));
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
        const ttstr sourceKey = detail::widen(source.path);
        auto sourceIt = loadedResource.krkrSourceEntries.find(sourceKey);
        if(sourceIt == loadedResource.krkrSourceEntries.end()) {
            // Consume pieces[1]/pieces[2] without a size check after the sole
            // first-segment test above.
            if(!buildKrkrAtlasGroup_guess(
                   loadedResource, detail::narrow(pieces[1]),
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

    void Player::findSourceForNode_guess(detail::MotionNode &node) {
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
                // The Win route clears only the object Variant.
                // The remaining fields intentionally retain their prior bytes
                // until the exact branch below overwrites them.
                dst.object.Clear();
                if(!loadedResource) {
                    // This invalid write belongs only to the outer module-map
                    // miss.
                    dst.valid = false;
                } else {
                    PSB::PSBRawNode groupNode;
                    if(findWinSourceGroup_guess(
                           *loadedResource, rawSource, groupNode)) {
                        // Complete cache/load before strictly navigating
                        // icon/<rawIcon>.
                        dst.texture = loadWinAtlasTexture_guess(
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
                            tracePath, "player.findSource", "four-ref",
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
                // Store the raw src owner only on the KRKR route; spec=2 never
                // synthesizes or overwrites path.
                dst.path = rawSource;
                if(_d3dDrawMode && loadKrkrAtlasSource_guess(
                       dst, resourceManager, motionContext)) {
                    // The caller repeats the success byte written by the atlas
                    // resolver.
                    dst.valid = true;
                    detail::logoChainTraceLogf(
                        tracePath, "player.findSource", "four-ref",
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

        // The fallback first nulls texture, then builds exactly src + "/" +
        // icon. When src is empty but icon is not, the leading slash remains;
        // an empty path also reaches findSource.
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
            tracePath, "player.findSource", "four-ref",
            _clampedEvalTime, "spec={} path={} valid=1 blank={} size={}x{}",
            sourceSpec, fallbackPath, dst.blank ? 1 : 0, dst.width,
            dst.height);
    }

    bool Player::isExistMotion(ttstr name) {
        // Current callbacks are
        // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6CDBD4@0x6CDBD4,
        // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_5942F4@0x5942F4,
        // Kirikiroid2_1.3.9_iOS_arm64!sub_10011F558@0x10011F558, and
        // Kirikiroid2_1.3.9_iOS_armv7!sub_11E054@0x11E054.
        // All four call the retained ResourceManager dispatch with
        // {_findMotionContextVariant, "motion/<stealthChara>/<name>"}, convert
        // its result to bool, and neither probe storage nor populate a
        // Player-local cache.
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
