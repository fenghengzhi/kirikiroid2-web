#include "MotionRenderBackend.h"

#include "LayerBitmapIntf.h"
#include "RenderManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#if defined(__EMSCRIPTEN__)
#include <GLES2/gl2.h>
#elif !defined(KRKR2_WASMTIME_HEADLESS)
#include "ogl/ogl_common.h"
#endif

void TVPConsoleLog(const ttstr &message, bool important);

namespace motion::render_backend_guess {

    namespace {
        iTVPRenderManager *privateOpenGLRenderManagerTestOverride_guess =
            nullptr;
    }

    iTVPRenderManager *getPrivateOpenGLRenderManager_guess() {
        if(privateOpenGLRenderManagerTestOverride_guess) {
            return privateOpenGLRenderManagerTestOverride_guess;
        }
        static iTVPRenderManager *manager =
            TVPGetRenderManager(TJS_W("opengl"));
        return manager;
    }

    void setPrivateOpenGLRenderManagerForDifferentialTest_guess(
        iTVPRenderManager *manager) {
        privateOpenGLRenderManagerTestOverride_guess = manager;
    }

    namespace {
        std::uint8_t stencilTestEnabledCache_guess = 0;

        bool sameRect(const tTVPRect &lhs, const tTVPRect &rhs) {
            return lhs.left == rhs.left && lhs.top == rhs.top &&
                lhs.right == rhs.right && lhs.bottom == rhs.bottom;
        }

        tTVPBitmap *makeRepeatedSoftwareBitmap_guess(
            iTVPTexture2D *sourceTexture,
            int &sourceTop,
            int &sourceLeft,
            int sourceWidth,
            int sourceHeight) {
            const int textureWidth =
                static_cast<int>(sourceTexture->GetWidth());
            const int textureHeight =
                static_cast<int>(sourceTexture->GetHeight());

            sourceLeft -= static_cast<int>(
                std::floor(static_cast<double>(sourceLeft) /
                           static_cast<double>(textureWidth))) *
                textureWidth;
            sourceTop -= static_cast<int>(
                std::floor(static_cast<double>(sourceTop) /
                           static_cast<double>(textureHeight))) *
                textureHeight;

            const int horizontalCopies =
                (sourceWidth + textureWidth + sourceLeft - 1) /
                textureWidth;
            const int verticalCopies =
                (sourceHeight + textureHeight + sourceTop - 1) /
                textureHeight;
            if(horizontalCopies == 1 && verticalCopies == 1) {
                return nullptr;
            }

            // The reference helper always allocates a 32-bpp bitmap and
            // repeats raw four-byte pixels, independent of texture format.
            auto *repeatedBitmap = new tTVPBitmap(
                static_cast<tjs_uint>(horizontalCopies * textureWidth),
                static_cast<tjs_uint>(verticalCopies * textureHeight), 32);
            auto *sourcePixels = static_cast<const std::uint8_t *>(
                sourceTexture->GetScanLineForRead(0));
            auto *destinationPixels = static_cast<std::uint8_t *>(
                repeatedBitmap->GetScanLine(0));
            const int sourcePitch = sourceTexture->GetPitch();
            const int destinationPitch = repeatedBitmap->GetPitch();
            const std::size_t rowBytes =
                static_cast<std::size_t>(textureWidth) * 4u;

            for(int y = 0; y < textureHeight; ++y) {
                auto *destinationRow = destinationPixels +
                    static_cast<std::ptrdiff_t>(y) * destinationPitch;
                const auto *sourceRow = sourcePixels +
                    static_cast<std::ptrdiff_t>(y) * sourcePitch;
                for(int x = 0; x < horizontalCopies; ++x) {
                    std::memcpy(destinationRow +
                                    static_cast<std::ptrdiff_t>(x) *
                                        textureWidth * 4,
                                sourceRow, rowBytes);
                }
            }
            for(int y = 1; y < verticalCopies; ++y) {
                // This deliberately mirrors the reference's asymmetric
                // vertical repeat: it copies the original texture bytes,
                // rather than the already-expanded first band.
                std::memcpy(
                    destinationPixels +
                        static_cast<std::ptrdiff_t>(y) * textureHeight *
                            destinationPitch,
                    sourcePixels,
                    static_cast<std::size_t>(sourcePitch) * textureHeight);
            }

            // The helper returns the fresh bitmap without releasing its
            // construction reference. The caller releases the old source
            // texture before asking the current render manager to wrap this
            // bitmap.
            return repeatedBitmap;
        }

        // The scalar conversion sites in all four references use the same
        // signed-int32 toward-zero saturation profile. Android arm64 also
        // auto-vectorizes a middle portion of the outer-point scan through a
        // signed-int64 conversion followed by a narrowing instruction; that
        // compiler-only path differs outside the int32 coordinate domain.
        int pointCoordinateToSignedInt_guess(double value) {
            constexpr double lower = -0x1p31;
            constexpr double upper = 0x1p31;
            if(std::isnan(value)) {
                return 0;
            }
            if(value >= upper) {
                return std::numeric_limits<std::int32_t>::max();
            }
            if(value <= lower) {
                return std::numeric_limits<std::int32_t>::min();
            }
            return static_cast<int>(value);
        }

        int wrappingIncrementSignedInt32_guess(int value) {
            const std::uint32_t incremented =
                static_cast<std::uint32_t>(value) + 1u;
            std::int32_t signedIncremented;
            std::memcpy(
                &signedIncremented, &incremented,
                sizeof(signedIncremented));
            return static_cast<int>(signedIncremented);
        }

        int wrappingMultiplySignedInt32_guess(int lhs, int rhs) {
            const std::uint32_t product =
                static_cast<std::uint32_t>(lhs) *
                static_cast<std::uint32_t>(rhs);
            std::int32_t signedProduct;
            std::memcpy(&signedProduct, &product, sizeof(signedProduct));
            return static_cast<int>(signedProduct);
        }

        int pointLowerBound_guess(double value) {
            return pointCoordinateToSignedInt_guess(value);
        }

        int pointUpperBound_guess(double value) {
            return pointCoordinateToSignedInt_guess(value + 1.0);
        }

        void initializeOuterBoundsFromFirstPoint_guess(
            const tTVPPointD &point,
            int &left,
            int &top,
            int &right,
            int &bottom) {
            left = pointLowerBound_guess(point.x);
            top = pointLowerBound_guess(point.y);
            right = wrappingIncrementSignedInt32_guess(left);
            bottom = wrappingIncrementSignedInt32_guess(top);
        }

        void includePointInBounds_guess(
            const tTVPPointD &point,
            int &left,
            int &top,
            int &right,
            int &bottom) {
            left = std::min(left, pointLowerBound_guess(point.x));
            top = std::min(top, pointLowerBound_guess(point.y));
            right = std::max(right, pointUpperBound_guess(point.x));
            bottom = std::max(bottom, pointUpperBound_guess(point.y));
        }

        bool boundsIntersectRect_guess(
            int left,
            int top,
            int right,
            int bottom,
            const tTVPRect &rect) {
            return left < right && top < bottom &&
                rect.left < rect.right && rect.top < rect.bottom &&
                bottom > rect.top && right > rect.left &&
                left < rect.right && top < rect.bottom;
        }
    }

    const std::vector<std::vector<double>> &
    cubicBezierBasisTable_guess(int division) {
        const auto found = cubicBezierBasisCache_guess.find(division);
        if(found != cubicBezierBasisCache_guess.end()) {
            return found->second;
        }

        auto &table = cubicBezierBasisCache_guess[division];
        // The source expression performs the signed-int addition before the
        // vector size conversion. At INT_MAX this is deliberately the native
        // undefined-overflow boundary; the two arm64 compilers widen it
        // differently, while both 32-bit targets retain the 0x80000000 word.
        table.resize(division + 1);
        if(division < 0) {
            return table;
        }

        for(std::int64_t index = 0;; ++index) {
            const double t = static_cast<double>(index) /
                static_cast<double>(division);
            const double oneMinusT = 1.0 - t;
            const double oneMinusTSquared = oneMinusT * oneMinusT;
            auto &row = table[static_cast<std::size_t>(index)];
            // The parentheses preserve the discrete scalar FMUL sequence used
            // by all four references; none of these products is reassociated.
            row.push_back(oneMinusT * (oneMinusT * oneMinusT));
            row.push_back((t * oneMinusTSquared) * 3.0);
            row.push_back((t * (t * oneMinusT)) * 3.0);
            row.push_back(t * (t * t));
            if(index == division) {
                break;
            }
        }
        return table;
    }

    std::vector<tTVPPointD> tessellateBezierPatch_guess(
        const std::vector<tTVPPointD> &controlPoints,
        int divisionX,
        int divisionY) {
        const auto &basisX = cubicBezierBasisTable_guess(divisionX);
        const auto &basisY = cubicBezierBasisTable_guess(divisionY);
        std::vector<tTVPPointD> result;
        if(divisionY < 0) {
            return result;
        }

        for(std::int64_t y = 0;; ++y) {
            if(divisionX >= 0) {
                for(std::int64_t x = 0;; ++x) {
                    tTVPPointD point{0.0, 0.0};
                    for(int controlIndex = 0; controlIndex != 16;
                        ++controlIndex) {
                        const double weight =
                            basisY[static_cast<std::size_t>(y)]
                                  [static_cast<std::size_t>(controlIndex / 4)] *
                            basisX[static_cast<std::size_t>(x)]
                                  [static_cast<std::size_t>(controlIndex % 4)];
                        // References issue FMUL(weight, coordinate) followed
                        // by FADD(old accumulator, product), never FMA.
                        point.x = point.x +
                            weight * controlPoints[controlIndex].x;
                        point.y = point.y +
                            weight * controlPoints[controlIndex].y;
                    }
                    result.push_back(point);
                    if(x == divisionX) {
                        break;
                    }
                }
            }
            if(y == divisionY) {
                break;
            }
        }
        return result;
    }

    iTVPRenderMethod *selectRenderMethod_guess(
        int blendLowNibble,
        std::uint32_t packedColor,
        bool alphaOpAdd) {
        // Each switch arm in all four references owns two independent,
        // zero-initialized function-local statics. The method pointer itself
        // is the unsynchronized lazy-init sentinel: it is published before
        // EnumParameterID runs, so an exception or racing reader observes the
        // still-zero color ID and initialization is not retried.
        switch(blendLowNibble) {
            case 1: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsAddBlend_color");
                    colorId = method->EnumParameterID("color");
                }
                method->SetParameterColor4B(colorId, packedColor);
                return method;
            }
            case 2:
            case 5: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsSubBlend_color");
                    colorId = method->EnumParameterID("color");
                }
                method->SetParameterColor4B(colorId, packedColor);
                return method;
            }
            case 3: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsMulBlend_color");
                    colorId = method->EnumParameterID("color");
                }
                method->SetParameterColor4B(colorId, packedColor);
                return method;
            }
            case 4: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsScreenBlend_color");
                    colorId = method->EnumParameterID("color");
                }
                method->SetParameterColor4B(colorId, packedColor);
                return method;
            }
            default:
                if(alphaOpAdd) {
                    static iTVPRenderMethod *method = nullptr;
                    static int colorId = 0;
                    if(!method) {
                        method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                            "AlphaBlend_color_a");
                        colorId = method->EnumParameterID("color");
                    }
                    method->SetParameterColor4B(colorId, packedColor);
                    return method;
                } else {
                    static iTVPRenderMethod *method = nullptr;
                    static int colorId = 0;
                    if(!method) {
                        method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                            "AlphaBlend_color");
                        colorId = method->EnumParameterID("color");
                    }
                    method->SetParameterColor4B(colorId, packedColor);
                    return method;
                }
        }
    }

    iTVPRenderMethod *selectAlphaTestRenderMethod_guess(
        int blendLowNibble,
        std::uint32_t packedColor,
        bool alphaOpAdd) {
        // The alpha-test selector is a separate native function. Each arm has
        // three independent BSS-zero statics and the same method-null sentinel
        // protocol; there are no __cxa_guard calls or retry flags. Publication
        // order is method, color ID, then alpha-threshold ID.
        switch(blendLowNibble) {
            case 1: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                static int thresholdId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsAddBlend_color_AlphaTest");
                    colorId = method->EnumParameterID("color");
                    thresholdId = method->EnumParameterID("alpha_threshold");
                }
                method->SetParameterColor4B(colorId, packedColor);
                method->SetParameterOpa(thresholdId, 64);
                return method;
            }
            case 2:
            case 5: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                static int thresholdId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsSubBlend_color_AlphaTest");
                    colorId = method->EnumParameterID("color");
                    thresholdId = method->EnumParameterID("alpha_threshold");
                }
                method->SetParameterColor4B(colorId, packedColor);
                method->SetParameterOpa(thresholdId, 64);
                return method;
            }
            case 3: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                static int thresholdId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsMulBlend_color_AlphaTest");
                    colorId = method->EnumParameterID("color");
                    thresholdId = method->EnumParameterID("alpha_threshold");
                }
                method->SetParameterColor4B(colorId, packedColor);
                method->SetParameterOpa(thresholdId, 64);
                return method;
            }
            case 4: {
                static iTVPRenderMethod *method = nullptr;
                static int colorId = 0;
                static int thresholdId = 0;
                if(!method) {
                    method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                        "PsScreenBlend_color_AlphaTest");
                    colorId = method->EnumParameterID("color");
                    thresholdId = method->EnumParameterID("alpha_threshold");
                }
                method->SetParameterColor4B(colorId, packedColor);
                method->SetParameterOpa(thresholdId, 64);
                return method;
            }
            default:
                if(alphaOpAdd) {
                    static iTVPRenderMethod *method = nullptr;
                    static int colorId = 0;
                    static int thresholdId = 0;
                    if(!method) {
                        method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                            "AlphaBlend_color_a_AlphaTest");
                        colorId = method->EnumParameterID("color");
                        thresholdId =
                            method->EnumParameterID("alpha_threshold");
                    }
                    method->SetParameterColor4B(colorId, packedColor);
                    method->SetParameterOpa(thresholdId, 64);
                    return method;
                } else {
                    static iTVPRenderMethod *method = nullptr;
                    static int colorId = 0;
                    static int thresholdId = 0;
                    if(!method) {
                        method = getPrivateOpenGLRenderManager_guess()->GetRenderMethod(
                            "AlphaBlend_color_AlphaTest");
                        colorId = method->EnumParameterID("color");
                        thresholdId =
                            method->EnumParameterID("alpha_threshold");
                    }
                    method->SetParameterColor4B(colorId, packedColor);
                    method->SetParameterOpa(thresholdId, 64);
                    return method;
                }
        }
    }

    void beginStencil_guess(iTVPTexture2D *target, bool enabled) {
        if(!enabled) {
            return;
        }
        getPrivateOpenGLRenderManager_guess()->BeginStencil(target);
#if !defined(KRKR2_WASMTIME_HEADLESS)
        glDisable(GL_DEPTH_TEST);
        glStencilMask(255);
        glClearStencil(0);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
        glDepthMask(GL_FALSE);
        glDisable(GL_STENCIL_TEST);
#endif
        stencilTestEnabledCache_guess = 0;
    }

    void applyStencilState_guess(std::uint8_t maskRef,
                                 std::uint8_t writeRef) {
#if !defined(KRKR2_WASMTIME_HEADLESS)
        if(writeRef) {
            if(!stencilTestEnabledCache_guess) {
                glEnable(GL_STENCIL_TEST);
                stencilTestEnabledCache_guess = 1;
            }
            glStencilFunc(GL_LEQUAL, writeRef, 255);
            if(maskRef) {
                glStencilMask(maskRef);
                glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);
            } else {
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            }
        } else if(maskRef) {
            if(!stencilTestEnabledCache_guess) {
                glEnable(GL_STENCIL_TEST);
                stencilTestEnabledCache_guess = 1;
            }
            glStencilMask(maskRef);
            glStencilFunc(GL_ALWAYS, maskRef, 255);
            glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
        } else if(stencilTestEnabledCache_guess) {
            glDisable(GL_STENCIL_TEST);
            stencilTestEnabledCache_guess = 0;
        }
#else
        (void)maskRef;
        (void)writeRef;
#endif
    }

    void endStencil_guess(bool enabled) {
        if(enabled) {
            getPrivateOpenGLRenderManager_guess()->EndStencil();
        }
    }

    bool buildAndSubmitMeshTriangles_guess(
        tTVPRect &computedBounds,
        iTVPTexture2D *sourceTexture,
        const tTVPRect &sourceRect,
        const std::vector<tTVPPointD> &boundsPoints,
        const std::vector<tTVPPointD> &meshPoints,
        int divisionX,
        int divisionY,
        const MeshSubmitCallback_guess &submit) {
        // The reference keeps this as a manually balanced raw reference.
        // Its normal false/success exits release explicitly, but exception
        // unwinding only destroys the temporary vectors and leaks this AddRef.
        sourceTexture->AddRef();

        const int sourceWidth = sourceRect.right - sourceRect.left;
        const int sourceHeight = sourceRect.bottom - sourceRect.top;
        int sourceLeft = sourceRect.left;
        int sourceTop = sourceRect.top;
        if(sourceLeft < 0 || sourceTop < 0 ||
           sourceRect.right >
               static_cast<int>(sourceTexture->GetWidth()) ||
           sourceRect.bottom >
               static_cast<int>(sourceTexture->GetHeight())) {
            if(TVPIsSoftwareRenderManager()) {
                auto *repeatedBitmap = makeRepeatedSoftwareBitmap_guess(
                    sourceTexture, sourceTop, sourceLeft,
                    sourceWidth, sourceHeight);
                if(repeatedBitmap) {
                    sourceTexture->Release();
                    sourceTexture =
                        getPrivateOpenGLRenderManager_guess()->CreateTexture2D(
                            repeatedBitmap);
                }
            } else {
                TVPConsoleLog(TJS_W(
                    "Repeat texture for opengl is not implemented yet."),
                    true);
            }
        }

        std::vector<double> sourceColumns;
        if(divisionX >= 0) {
            for(int x = 0;; ++x) {
                sourceColumns.push_back(
                    static_cast<double>(sourceWidth) * x /
                        static_cast<double>(divisionX) +
                    sourceLeft);
                if(x >= divisionX) {
                    break;
                }
            }
        }
        std::vector<double> sourceRows;
        if(divisionY >= 0) {
            for(int y = 0;; ++y) {
                sourceRows.push_back(
                    static_cast<double>(sourceHeight) * y /
                        static_cast<double>(divisionY) +
                    sourceTop);
                if(y >= divisionY) {
                    break;
                }
            }
        }

        const int cellCount =
            wrappingMultiplySignedInt32_guess(divisionX, divisionY);
        std::vector<int> selectedCells;
        selectedCells.reserve(static_cast<std::size_t>(cellCount));

        int boundsLeft;
        int boundsTop;
        int boundsRight;
        int boundsBottom;
        initializeOuterBoundsFromFirstPoint_guess(
            boundsPoints[0], boundsLeft, boundsTop,
            boundsRight, boundsBottom);
        if(&boundsPoints != &meshPoints) {
            for(std::size_t i = 1; i < boundsPoints.size(); ++i) {
                includePointInBounds_guess(
                    boundsPoints[i], boundsLeft, boundsTop,
                    boundsRight, boundsBottom);
            }

            if(computedBounds.left >= computedBounds.right ||
               computedBounds.top >= computedBounds.bottom) {
                sourceTexture->Release();
                return false;
            }
            if(boundsLeft < boundsRight && boundsTop < boundsBottom &&
               computedBounds.bottom >= boundsBottom &&
               computedBounds.right >= boundsRight &&
               computedBounds.left <= boundsLeft &&
               computedBounds.top <= boundsTop) {
                for(int cell = 0; cell < cellCount; ++cell) {
                    selectedCells.push_back(cell);
                }
            } else if(boundsBottom > computedBounds.top &&
                      boundsRight > computedBounds.left &&
                      boundsLeft < boundsRight &&
                      boundsTop < boundsBottom &&
                      boundsLeft < computedBounds.right &&
                      boundsTop < computedBounds.bottom) {
                // Partial overlap is resolved per cell below.
            } else {
                sourceTexture->Release();
                return false;
            }
        }

        if(selectedCells.empty()) {
            std::vector<std::uint8_t> pointInside;
            pointInside.reserve(meshPoints.size());
            bool firstPoint = true;
            for(const auto &point : meshPoints) {
                pointInside.push_back(
                    computedBounds.left <= point.x &&
                    computedBounds.right > point.x &&
                    computedBounds.top <= point.y &&
                    computedBounds.bottom > point.y);
                if(firstPoint) {
                    boundsLeft = pointLowerBound_guess(point.x);
                    boundsTop = pointLowerBound_guess(point.y);
                    boundsRight = pointUpperBound_guess(point.x);
                    boundsBottom = pointUpperBound_guess(point.y);
                    firstPoint = false;
                } else {
                    includePointInBounds_guess(
                        point, boundsLeft, boundsTop,
                        boundsRight, boundsBottom);
                }
            }

            const int pointColumns = divisionX + 1;
            for(int cell = 0; cell < cellCount; ++cell) {
                const int y = cell / divisionX;
                const int x = cell % divisionX;
                const int p00 = x + y * pointColumns;
                const int p10 = p00 + 1;
                const int p01 = x + (y + 1) * pointColumns;
                const int p11 = p01 + 1;
                if(pointInside[static_cast<std::size_t>(p00)] ||
                   pointInside[static_cast<std::size_t>(p10)] ||
                   pointInside[static_cast<std::size_t>(p01)] ||
                   pointInside[static_cast<std::size_t>(p11)]) {
                    selectedCells.push_back(cell);
                    continue;
                }

                int cellLeft = pointLowerBound_guess(meshPoints[p00].x);
                int cellTop = pointLowerBound_guess(meshPoints[p00].y);
                int cellRight = pointUpperBound_guess(meshPoints[p00].x);
                int cellBottom = pointUpperBound_guess(meshPoints[p00].y);
                includePointInBounds_guess(
                    meshPoints[p10], cellLeft, cellTop,
                    cellRight, cellBottom);
                includePointInBounds_guess(
                    meshPoints[p01], cellLeft, cellTop,
                    cellRight, cellBottom);
                includePointInBounds_guess(
                    meshPoints[p11], cellLeft, cellTop,
                    cellRight, cellBottom);
                if(boundsIntersectRect_guess(
                       cellLeft, cellTop, cellRight, cellBottom,
                       computedBounds)) {
                    selectedCells.push_back(cell);
                    // Native folds the admitted cell AABB into the bounds
                    // accumulated by the preceding full point scan. This is
                    // normally idempotent but is part of the exact data flow.
                    boundsLeft = std::min(boundsLeft, cellLeft);
                    boundsTop = std::min(boundsTop, cellTop);
                    boundsRight = std::max(boundsRight, cellRight);
                    boundsBottom = std::max(boundsBottom, cellBottom);
                }
            }
        }

        if(selectedCells.empty()) {
            sourceTexture->Release();
            return false;
        }

        std::vector<tTVPPointD> destinationVertices;
        std::vector<tTVPPointD> sourceVertices;
        destinationVertices.reserve(selectedCells.size() * 6u);
        sourceVertices.reserve(selectedCells.size() * 6u);
        const int pointColumns = divisionX + 1;
        for(const int cell : selectedCells) {
            const int y = cell / divisionX;
            const int x = cell % divisionX;
            const int p00 = x + y * pointColumns;
            const int p10 = p00 + 1;
            const int p01 = x + (y + 1) * pointColumns;
            const int p11 = p01 + 1;
            const double sourceX0 = sourceColumns[x];
            const double sourceX1 = sourceColumns[x + 1];
            const double sourceY0 = sourceRows[y];
            const double sourceY1 = sourceRows[y + 1];
            sourceVertices.insert(sourceVertices.end(), {
                {sourceX0, sourceY0}, {sourceX1, sourceY0},
                {sourceX0, sourceY1}, {sourceX1, sourceY0},
                {sourceX0, sourceY1}, {sourceX1, sourceY1},
            });
            destinationVertices.insert(destinationVertices.end(), {
                meshPoints[p00], meshPoints[p10], meshPoints[p01],
                meshPoints[p10], meshPoints[p01], meshPoints[p11],
            });
        }

        submit(sourceTexture, sourceVertices, destinationVertices);
        sourceTexture->Release();
        computedBounds.left = boundsLeft;
        computedBounds.top = boundsTop;
        computedBounds.right = boundsRight;
        computedBounds.bottom = boundsBottom;
        return true;
    }

    TriangleBatch_guess::TriangleBatch_guess(iTVPRenderManager *manager)
        : _manager(manager) {}

    void TriangleBatch_guess::setStencilState_guess(
        std::uint8_t writeRef,
        std::uint8_t maskRef) {
        if(_maskRef == maskRef && _writeRef == writeRef) {
            return;
        }
        flush_guess();
        _maskRef = maskRef;
        _writeRef = writeRef;
        applyStencilState_guess(maskRef, writeRef);
    }

    iTVPRenderMethod *TriangleBatch_guess::selectMethod_guess(
        int blendLowNibble,
        std::uint32_t packedColor,
        bool alphaOpAdd,
        bool alphaTest) {
        if(_packedColor == packedColor &&
           _blendLowNibble == blendLowNibble &&
           _alphaOpAdd == alphaOpAdd && _alphaTest == alphaTest) {
            return _method;
        }
        flush_guess();
        _packedColor = packedColor;
        _blendLowNibble = blendLowNibble;
        _alphaOpAdd = alphaOpAdd;
        _alphaTest = alphaTest;
        _method = alphaTest
            ? selectAlphaTestRenderMethod_guess(
                  blendLowNibble, packedColor, alphaOpAdd)
            : selectRenderMethod_guess(
                  blendLowNibble, packedColor, alphaOpAdd);
        return _method;
    }

    void TriangleBatch_guess::appendTriangles_guess(
        iTVPRenderMethod *method,
        iTVPTexture2D *sourceTexture,
        iTVPTexture2D *targetTexture,
        iTVPTexture2D *referenceTexture,
        const tTVPRect &clipRect,
        const tTVPPointD *sourceVertices,
        const tTVPPointD *destinationVertices,
        std::size_t vertexCount,
        std::uint32_t packedColor) {
        // The native batch key deliberately omits the reference texture.  A
        // reference change by itself therefore remains in the current batch;
        // the cached reference is replaced only when another key opens a new
        // batch.
        if(_method != method || _sourceTexture != sourceTexture ||
           _targetTexture != targetTexture ||
           !sameRect(_clipRect, clipRect) ||
           _packedColor != packedColor) {
            flush_guess();
            _method = method;
            _sourceTexture = sourceTexture;
            _targetTexture = targetTexture;
            _referenceTexture = referenceTexture;
            _clipRect = clipRect;
            // packedColor belongs to selectMethod_guess's cache key.  The
            // append helper compares it but does not write it.
        }
        _sourceVertices.insert(
            _sourceVertices.end(), sourceVertices,
            sourceVertices + vertexCount);
        _destinationVertices.insert(
            _destinationVertices.end(), destinationVertices,
            destinationVertices + vertexCount);
    }

    void TriangleBatch_guess::flush_guess() {
        if(_destinationVertices.empty()) {
            return;
        }
        tRenderTexQuadArray::Element textures[] = {
            tRenderTexQuadArray::Element(
                _sourceTexture, _sourceVertices.data())
        };
        _manager->OperateTriangles(
            _method,
            static_cast<int>(_destinationVertices.size() / 3u),
            _targetTexture,
            _referenceTexture,
            _clipRect,
            _destinationVertices.data(),
            tRenderTexQuadArray(textures));
        _destinationVertices.clear();
        _sourceVertices.clear();
    }

} // namespace motion::render_backend_guess
