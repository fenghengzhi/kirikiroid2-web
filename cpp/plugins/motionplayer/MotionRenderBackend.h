#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "ComplexRect.h"

class iTVPRenderManager;
class iTVPRenderMethod;
class iTVPTexture2D;

namespace motion::render_backend_guess {

    using CubicBezierBasisTable_guess =
        std::vector<std::vector<double>>;

    // The four references construct this cache immediately before the
    // process-global default Bezier-patch point vector in the same native
    // initialization bundle.  Its definition therefore lives beside the NCB
    // registration state in main.cpp; keeping only the declaration here also
    // preserves that cross-helper ownership after the recovered source split.
    extern std::map<int, CubicBezierBasisTable_guess>
        cubicBezierBasisCache_guess;

    iTVPRenderMethod *selectRenderMethod_guess(
        int blendLowNibble,
        std::uint32_t packedColor,
        bool alphaOpAdd);

    iTVPRenderMethod *selectAlphaTestRenderMethod_guess(
        int blendLowNibble,
        std::uint32_t packedColor,
        bool alphaOpAdd);

    void beginStencil_guess(iTVPTexture2D *target, bool enabled);
    void applyStencilState_guess(std::uint8_t maskRef,
                                 std::uint8_t writeRef);
    void endStencil_guess(bool enabled);

    using MeshSubmitCallback_guess = std::function<void(
        iTVPTexture2D *,
        const std::vector<tTVPPointD> &,
        const std::vector<tTVPPointD> &)>;

    const std::vector<std::vector<double>> &
    cubicBezierBasisTable_guess(int division);

    std::vector<tTVPPointD> tessellateBezierPatch_guess(
        const std::vector<tTVPPointD> &controlPoints,
        int divisionX,
        int divisionY);

    bool buildAndSubmitMeshTriangles_guess(
        tTVPRect &computedBounds,
        iTVPTexture2D *sourceTexture,
        const tTVPRect &sourceRect,
        const std::vector<tTVPPointD> &boundsPoints,
        const std::vector<tTVPPointD> &meshPoints,
        int divisionX,
        int divisionY,
        const MeshSubmitCallback_guess &submit);

    class TriangleBatch_guess final {
    public:
        explicit TriangleBatch_guess(iTVPRenderManager *manager);
        TriangleBatch_guess(const TriangleBatch_guess &) = delete;
        TriangleBatch_guess &operator=(const TriangleBatch_guess &) = delete;

        void setStencilState_guess(std::uint8_t maskRef,
                                   std::uint8_t writeRef);
        iTVPRenderMethod *selectMethod_guess(
            int blendLowNibble,
            std::uint32_t packedColor,
            bool alphaOpAdd,
            bool alphaTest);
        void appendTriangles_guess(
            iTVPRenderMethod *method,
            iTVPTexture2D *sourceTexture,
            iTVPTexture2D *targetTexture,
            iTVPTexture2D *referenceTexture,
            const tTVPRect &clipRect,
            const tTVPPointD *sourceVertices,
            const tTVPPointD *destinationVertices,
            std::size_t vertexCount,
            std::uint32_t packedColor);
        void flush_guess();

    private:
        iTVPRenderMethod *_method = nullptr;
        iTVPTexture2D *_sourceTexture = nullptr;
        iTVPTexture2D *_targetTexture = nullptr;
        iTVPTexture2D *_referenceTexture = nullptr;
        std::vector<tTVPPointD> _destinationVertices;
        std::vector<tTVPPointD> _sourceVertices;
        tTVPRect _clipRect{0, 0, 0, 0};
        iTVPRenderManager *_manager = nullptr;
        std::uint8_t _maskRef = 0;
        std::uint8_t _writeRef = 0;
        std::uint32_t _packedColor = 0xffffffffu;
        int _blendLowNibble = -1;
        bool _alphaOpAdd = false;
        bool _alphaTest = false;
    };

} // namespace motion::render_backend_guess
