// motionsim — Offline static simulator for KrKr2 motion (.mtn) files.
//
// Purpose: given a .mtn file and a clip label, statically compute the
// per-frame per-node "accumulated" state (position, scale, angle, matrix,
// opacity) that libkrkr2.so Player_updateLayers would produce at 0x6BB33C.
// Output is a TSV intended for diffing against the Web port's runtime trace
// (see PlayerUpdateLayers.cpp `phase2.accum_final` logging).
//
// Design (per /Users/bytedance/.claude/plans/reference-xp3-logo-test-xp3-mtn-linked-chipmunk.md):
//
//   REUSED from motionplayer (PSB format parsing is shared truth, not under test):
//     - motion::detail::loadMotionSnapshot      → parse .mtn → PSBDictionary tree
//     - motion::internal::evaluateLayerContent  → framesel selection + interpolation
//                                                 (already traced/validated, not the current
//                                                 suspected bug surface)
//     - motion::internal::findPSBResourceBySourceName → clipW/clipH/origin lookup
//
//   INDEPENDENTLY RE-IMPLEMENTED from analysis/player_updateLayers_accum.md
//   (the authoritative reversed pseudocode):
//     - Flat node tree construction (parent chain, inheritFlags, type, coordinateMode,
//       transformOrder read)  ← do not #include NodeTree.cpp
//     - Phase-2 accumulator (parent × local matrix, pos transform, opacity, attribute
//       inherit bits, DEPENDENT 4-phase matrix path)  ← do not #include
//       PlayerUpdateLayers.cpp
//
// Coverage (v1, limited to m2logo.mtn's known usage):
//     nodeType ∈ {0, 2, 3},  coordinateMode == 0 (2D),  independentLayerInherit == false,
//     no mesh, no camera, no groundCorrection. Out-of-scope cases are noted in comments
//     and the per-node row is marked with a coverage_warn column.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "BinaryStream.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"
#include "SysInitIntf.h"
#include "GraphicsLoaderIntf.h"

#include "psbfile/PSBFile.h"
#include "psbfile/PSBValue.h"

#include "motionplayer/PlayerInternal.h"
#include "motionplayer/RuntimeSupport.h"
#include "motionplayer/MotionNode.h"

namespace fs = std::filesystem;

namespace {

    // ---------------------------------------------------------------------
    // Local PSB dictionary accessors (duplicated here so this file has no
    // dependency on NodeTree.cpp's anonymous namespace — keeps the node tree
    // construction an independent re-implementation).
    // ---------------------------------------------------------------------

    std::optional<double>
    dictNum(const std::shared_ptr<const PSB::PSBDictionary> &dic,
            const char *key) {
        if(!dic) return std::nullopt;
        auto val = (*dic)[key];
        if(auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(val)) {
            switch(n->numberType) {
                case PSB::PSBNumberType::Float:
                    return n->getValue<float>();
                case PSB::PSBNumberType::Double:
                    return n->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(n->getValue<int>());
                case PSB::PSBNumberType::Long:
                default:
                    return static_cast<double>(n->getValue<tjs_int64>());
            }
        }
        if(auto b = std::dynamic_pointer_cast<PSB::PSBBool>(val)) {
            return b->value ? 1.0 : 0.0;
        }
        return std::nullopt;
    }

    std::string
    dictStr(const std::shared_ptr<const PSB::PSBDictionary> &dic,
            const char *key) {
        if(!dic) return {};
        if(auto s = std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
            return s->value;
        }
        return {};
    }

    std::shared_ptr<PSB::PSBList>
    dictList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
             const char *key) {
        if(!dic) return nullptr;
        return std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key]);
    }

    // ---------------------------------------------------------------------
    // SimNode — independent node struct for the simulator.
    // Mirrors fields read by libkrkr2.so Player_updateLayers phase2.
    // ---------------------------------------------------------------------

    struct Accum {
        // node+1512/1520/1528 (pos)
        double posX = 0.0, posY = 0.0, posZ = 0.0;
        // node+1544/1552 (scale), +1536 (angle), +1560/1568 (slant)
        double scaleX = 1.0, scaleY = 1.0;
        double angle = 0.0;
        double slantX = 0.0, slantY = 0.0;
        // node+120/128/136/144 — local 2×2 matrix (computed by sub_699940,
        // optionally left-multiplied by parent matrix in DEPENDENT path)
        double m11 = 1.0, m21 = 0.0, m12 = 0.0, m22 = 1.0;
        // node+1576 (opacity, 0..255 int)
        int opacity = 255;
        bool visible = false;
        bool active = false;
        // Flip bits are XORed through in the accumulation, tracked for completeness
        bool flipX = false, flipY = false;
    };

    struct SimNode {
        int index = 0;
        int parentIndex = -1;
        int nodeType = 0;
        int coordinateMode = 0;
        unsigned inheritFlags = 0x1FC;
        std::string label;
        std::array<int, 4> transformOrder{0, 1, 2, 3};
        bool hasTransformOrder = false;
        int meshType = 0;
        int meshFlags = 0;
        int meshDivision = 0;

        std::shared_ptr<const PSB::PSBDictionary> psbNode;

        // Populated during simulation:
        motion::internal::FrameContentState state;
        Accum accum;
        std::string coverageWarn;  // non-empty → row annotation of unsupported path

        // Source metadata (for TSV's clipW/clipH/origin columns).
        int clipW = 0, clipH = 0;
        double originX = 0.0, originY = 0.0;
    };

    // ---------------------------------------------------------------------
    // Flat node tree construction — independent DFS of PSB children arrays.
    // Mirrors Player_buildNodeTree @0x6B51F0 / walkTree @0x6B4A6C semantics:
    //   - node 0 = synthetic root (no PSB dict)
    //   - top-level layers from clip.layerList (or snapshot.layerList fallback)
    //     attach under synthetic root as parent
    //   - children arrays recurse DFS, parentIdx = thisIdx
    //   - every PSB dict becomes its own node (no label dedup)
    // ---------------------------------------------------------------------

    void buildNodeDFS(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                      int parentIdx, std::vector<SimNode> &out) {
        if(!dic) return;
        SimNode node;
        node.index = static_cast<int>(out.size());
        node.parentIndex = parentIdx;
        node.psbNode = dic;
        node.label = dictStr(dic, "label");
        if(auto v = dictNum(dic, "type"))
            node.nodeType = static_cast<int>(*v);
        if(auto v = dictNum(dic, "coordinate"))
            node.coordinateMode = static_cast<int>(*v);
        if(auto v = dictNum(dic, "inheritMask"))
            node.inheritFlags = static_cast<unsigned>(*v);
        if(auto v = dictNum(dic, "meshTransform"))
            node.meshType = static_cast<int>(*v);
        if(auto v = dictNum(dic, "meshSyncChildMask"))
            node.meshFlags = static_cast<int>(*v);
        if(auto v = dictNum(dic, "meshDivision"))
            node.meshDivision = static_cast<int>(*v);
        if(auto toList = dictList(dic, "transformOrder")) {
            for(int i = 0; i < 4 && i < static_cast<int>(toList->size()); ++i) {
                if(auto num = std::dynamic_pointer_cast<PSB::PSBNumber>(
                       (*toList)[i])) {
                    switch(num->numberType) {
                        case PSB::PSBNumberType::Int:
                            node.transformOrder[i] = num->getValue<int>();
                            break;
                        default:
                            node.transformOrder[i] = static_cast<int>(
                                num->getValue<tjs_int64>());
                            break;
                    }
                }
            }
            node.hasTransformOrder = true;
        }

        const int thisIdx = node.index;
        out.push_back(std::move(node));

        if(auto children = dictList(dic, "children")) {
            for(int i = 0; i < static_cast<int>(children->size()); ++i) {
                auto child = std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*children)[i]);
                buildNodeDFS(child, thisIdx, out);
            }
        }
    }

    std::vector<SimNode>
    buildFlatNodeTree(const motion::detail::MotionSnapshot &snapshot,
                      const std::string &clipLabel) {
        std::vector<SimNode> nodes;
        SimNode root;  // synthetic root at index 0
        root.index = 0;
        root.parentIndex = -1;
        nodes.push_back(std::move(root));

        const std::vector<std::shared_ptr<const PSB::PSBDictionary>> *src =
            nullptr;
        if(!clipLabel.empty()) {
            auto it = snapshot.clipIndexByLabel.find(clipLabel);
            if(it != snapshot.clipIndexByLabel.end()) {
                const int idx = it->second;
                if(idx >= 0 &&
                   idx < static_cast<int>(snapshot.clipList.size())) {
                    src = &snapshot.clipList[idx].layerList;
                }
            }
        }
        if(!src) src = &snapshot.layerList;

        for(const auto &dic : *src) {
            if(!dic) continue;
            buildNodeDFS(dic, 0, nodes);
        }
        return nodes;
    }

    // ---------------------------------------------------------------------
    // sub_699940 — Local matrix computation from state (independent).
    // Aligned to analysis/player_updateLayers_accum.md appendix: iterate
    // transformOrder[4] applying flip/rotate/scale/slant ops 0/1/2/3.
    // Result written to accum.m11/m21/m12/m22 (2×2 row-major: [m11 m12; m21 m22]).
    // ---------------------------------------------------------------------

    void computeLocalMatrix(SimNode &node) {
        double m11 = 1.0, m12 = 0.0, m21 = 0.0, m22 = 1.0;
        const int order[4] = {
            node.transformOrder[0], node.transformOrder[1],
            node.transformOrder[2], node.transformOrder[3]
        };
        for(int i = 0; i < 4; ++i) {
            switch(order[i]) {
                case 0: {  // flip
                    if(node.accum.flipX) {
                        m11 = -m11; m12 = -m12;
                    }
                    if(node.accum.flipY) {
                        m21 = -m21; m22 = -m22;
                    }
                    break;
                }
                case 1: {  // rotate (angle in degrees)
                    const double rad = node.accum.angle *
                                       3.14159265358979323846 / 180.0;
                    const double c = std::cos(rad), s = std::sin(rad);
                    // rotation × current
                    const double n11 = c * m11 - s * m21;
                    const double n12 = c * m12 - s * m22;
                    const double n21 = s * m11 + c * m21;
                    const double n22 = s * m12 + c * m22;
                    m11 = n11; m12 = n12; m21 = n21; m22 = n22;
                    break;
                }
                case 2: {  // scale (row-wise)
                    m11 *= node.accum.scaleX;
                    m12 *= node.accum.scaleX;
                    m21 *= node.accum.scaleY;
                    m22 *= node.accum.scaleY;
                    break;
                }
                case 3: {  // slant — off-diagonal additive
                    const double sx = node.accum.slantX;
                    const double sy = node.accum.slantY;
                    // [1 sx; sy 1] × current
                    const double n11 = m11 + sx * m21;
                    const double n12 = m12 + sx * m22;
                    const double n21 = sy * m11 + m21;
                    const double n22 = sy * m12 + m22;
                    m11 = n11; m12 = n12; m21 = n21; m22 = n22;
                    break;
                }
                default:
                    break;  // unknown op — skip
            }
        }
        node.accum.m11 = m11;
        node.accum.m12 = m12;
        node.accum.m21 = m21;
        node.accum.m22 = m22;
    }

    // Root offset — set from --root-pos CLI flag. Seeded into nodes[0].accum.pos
    // at the start of each phase-2 pass so that the output matches the Web port
    // which applies (player.x, player.y) as the root-level translation.
    double g_rootPosX = 0.0;
    double g_rootPosY = 0.0;

    // ---------------------------------------------------------------------
    // Phase-2 accumulator — independent re-implementation.
    // Directly translates the pseudocode in
    //   analysis/player_updateLayers_accum.md § "phase-2"
    // for each non-root node, in DFS pre-order.
    //
    // Coverage: nodeType ∈ {0, 2, 3}; coordinateMode == 0; no mesh;
    // no groundCorrection; independentLayerInherit == false (DEPENDENT
    // 4-phase matrix path). Anything else marks coverageWarn and is left
    // unchanged from state+defaults.
    // ---------------------------------------------------------------------

    void runPhase2(std::vector<SimNode> &nodes) {
        if(nodes.empty()) return;

        // Root (index 0): libkrkr2.so initializes root's accum from root's
        // delta block. For a static snapshot with no velocity/delta, that's
        // all zeros + identity. Web port's traced root is always pos=(0,0,0)
        // m=identity. Mirror that.
        nodes[0].accum = Accum{};
        nodes[0].accum.visible = true;
        nodes[0].accum.active = true;
        nodes[0].accum.opacity = 255;
        nodes[0].accum.posX = g_rootPosX;
        nodes[0].accum.posY = g_rootPosY;

        for(size_t i = 1; i < nodes.size(); ++i) {
            SimNode &node = nodes[i];
            const motion::internal::FrameContentState &state = node.state;

            // 2.1 Parent lookup with joinTarget (0x00400000) walk — per
            //     pseudocode 0x6BB598..0x6BB5BC. Skip-parent if its
            //     inheritMask bit 22 set. Never skip past root.
            int parentIdx = node.parentIndex;
            while(parentIdx > 0 &&
                  parentIdx < static_cast<int>(nodes.size())) {
                if((nodes[parentIdx].inheritFlags & 0x00400000) == 0) break;
                parentIdx = nodes[parentIdx].parentIndex;
            }
            if(parentIdx < 0 ||
               parentIdx >= static_cast<int>(nodes.size())) {
                parentIdx = 0;
            }
            const SimNode &parent = nodes[parentIdx];

            // 2.2 Out-of-scope coverage guards.
            if(node.nodeType != 0 && node.nodeType != 2 && node.nodeType != 3) {
                node.coverageWarn = "nodeType_unsupported";
            }
            if(node.coordinateMode != 0) {
                node.coverageWarn = "coordinateMode_3D_unsupported";
            }

            // 2.3 If !state.visible: skip accumulation, mark invisible.
            if(!state.visible) {
                node.accum = Accum{};
                node.accum.visible = false;
                node.accum.active = false;
                node.accum.opacity = 0;
                continue;
            }

            // 2.4 Initialize node.accum from interpolated state (pre-delta).
            //     The Web port's populateTransformStateFromFrameState does
            //     the same — accum is seeded with the evaluated frame state,
            //     then parent transform is applied below.
            node.accum = Accum{};
            node.accum.visible = true;
            node.accum.active = true;
            node.accum.posX = state.x;
            node.accum.posY = state.y;
            node.accum.posZ = state.z;
            node.accum.scaleX = state.scaleX;
            node.accum.scaleY = state.scaleY;
            node.accum.angle = state.angle;
            node.accum.slantX = state.slantX;
            node.accum.slantY = state.slantY;
            node.accum.opacity = static_cast<int>(
                std::clamp(state.opacity, 0.0, 1.0) * 255.0 + 0.5);
            node.accum.flipX = state.flipX;
            node.accum.flipY = state.flipY;

            // 2.5 Position transform via parent's matrix (0x6BB744..0x6BB7E4).
            //     coordinateMode == 0 (2D) branch only. localX/Y/Z come from
            //     accum.pos (which we just seeded from state.x/y/z).
            const double lx = node.accum.posX;
            const double ly = node.accum.posY;
            const double lz = node.accum.posZ;
            if(parent.coordinateMode != 0) {
                node.coverageWarn = "parent_coordinateMode_3D_unsupported";
                // Best-effort: fall through to 2D branch
            }
            node.accum.posX =
                parent.accum.m11 * lx + parent.accum.m12 * ly + parent.accum.posX;
            node.accum.posY =
                parent.accum.m21 * lx + parent.accum.m22 * ly + parent.accum.posY;
            node.accum.posZ = lz + parent.accum.posZ;

            // 2.6 Opacity second multiply (0x6BB808..0x6BB830).
            //     v47 = parent when (inheritFlags & 0x400); else v47 = root
            //     (only when !independentLayerInherit — assumed false here).
            const SimNode &opaSrc =
                (node.inheritFlags & 0x400u) ? parent : nodes[0];
            node.accum.opacity = static_cast<int>(
                static_cast<long long>(opaSrc.accum.opacity) *
                node.accum.opacity / 255);

            // 2.7 Matrix/attribute accumulation (0x6BB83C..0x6BBB6C).
            const unsigned f = node.inheritFlags;
            const bool allInheritSet = ((~f) & 0x1FCu) == 0u;
            const SimNode &rootNode = nodes[0];

            if(allInheritSet) {
                // SIMPLE path (0x6BB848..0x6BB8EC):
                // Per pseudocode, order is:
                //   1. sub_699940 — build local matrix from state-seeded
                //      scalars (before any parent-merge)
                //   2. attribute merge (XOR / + / ×) with parent.accum
                //   3. node.m = parent.m × local.m
                // This keeps local.m a "state-only" transform so the final
                // node.m chains as grandparent.m × parent_local.m × local.m
                // through recursion.
                computeLocalMatrix(node);
                const double lm11 = node.accum.m11, lm12 = node.accum.m12;
                const double lm21 = node.accum.m21, lm22 = node.accum.m22;
                node.accum.flipX ^= parent.accum.flipX;
                node.accum.flipY ^= parent.accum.flipY;
                node.accum.angle += parent.accum.angle;
                node.accum.scaleX *= parent.accum.scaleX;
                node.accum.scaleY *= parent.accum.scaleY;
                node.accum.slantX += parent.accum.slantX;
                node.accum.slantY += parent.accum.slantY;
                // parent × local
                node.accum.m11 = parent.accum.m11 * lm11 +
                                 parent.accum.m12 * lm21;
                node.accum.m21 = parent.accum.m21 * lm11 +
                                 parent.accum.m22 * lm21;
                node.accum.m12 = parent.accum.m11 * lm12 +
                                 parent.accum.m12 * lm22;
                node.accum.m22 = parent.accum.m21 * lm12 +
                                 parent.accum.m22 * lm22;
            } else {
                // COMPLEX path (0x6BB8F4..0x6BBB6C). Per
                // analysis/player_updateLayers_accum.md and cross-verified
                // against Web's PlayerUpdateLayers.cpp at lines 876-940 which
                // reads merged accum scalars via applyLocalTransform(node):
                //
                //   Step-A  (per-bit merge) runs BEFORE sub_699940.
                //   Phase A (undo root)     — no-op here since root=identity.
                //   Phase B sub_699940 reads MERGED accum.scalars to build local.
                //   Phase C (re-apply root) — no-op here.
                //   Phase D node.m = parent.m × local.m.
                //
                // Consequence: for a child whose 0x020 bit is SET, its final
                // matrix carries parent.scale TWICE — once via Step-A baking
                // parent.state-scale into accum.scaleX (then into local.m)
                // and once more via Phase D's parent.m multiply. This matches
                // libkrkr2.so and the Web port exactly; I misread it earlier.
                // For a bit that is NOT set, Step-A writes state.scaleX and
                // the double-apply reduces to the normal parent×state chain.

                // Step-A: per-bit parent merge into accum. When bit unset,
                // accum.scalar stays at state-seeded value.
                if(f & 0x004u) node.accum.flipX ^= parent.accum.flipX;
                if(f & 0x008u) node.accum.flipY ^= parent.accum.flipY;
                if(f & 0x010u) node.accum.angle += parent.accum.angle;
                if(f & 0x020u) node.accum.scaleX *= parent.accum.scaleX;
                if(f & 0x040u) node.accum.scaleY *= parent.accum.scaleY;
                if(f & 0x080u) node.accum.slantX += parent.accum.slantX;
                if(f & 0x100u) node.accum.slantY += parent.accum.slantY;
                (void)rootNode;  // root undo/reapply dance is a no-op for
                                 // identity root; left out deliberately until
                                 // we hit a non-trivial root case.

                // Phase B: sub_699940 using post-Step-A accum.scalars.
                computeLocalMatrix(node);
                const double lm11 = node.accum.m11, lm12 = node.accum.m12;
                const double lm21 = node.accum.m21, lm22 = node.accum.m22;

                // Phase D: parent × local matrix multiply.
                node.accum.m11 = parent.accum.m11 * lm11 +
                                 parent.accum.m12 * lm21;
                node.accum.m21 = parent.accum.m21 * lm11 +
                                 parent.accum.m22 * lm21;
                node.accum.m12 = parent.accum.m11 * lm12 +
                                 parent.accum.m12 * lm22;
                node.accum.m22 = parent.accum.m21 * lm12 +
                                 parent.accum.m22 * lm22;
            }
        }
    }

    // ---------------------------------------------------------------------
    // Per-node source lookup (clipW/clipH/origin) via findPSBResourceBySourceName.
    // ---------------------------------------------------------------------

    void populateSourceMetadata(SimNode &node,
                                const motion::detail::MotionSnapshot &snapshot) {
        if(node.state.src.empty()) return;
        int w = 0, h = 0;
        double ox = 0.0, oy = 0.0;
        std::vector<std::uint8_t> decomp;
        const auto *res = motion::internal::findPSBResourceBySourceName(
            snapshot, node.state.src, w, h, decomp, ox, oy);
        if(res) {
            node.clipW = w;
            node.clipH = h;
            node.originX = ox;
            node.originY = oy;
        }
    }

    // ---------------------------------------------------------------------
    // Tool runtime scope — mirrors mtndump. Required for PSB load because
    // loadMotionSnapshot eventually calls TVPCreateBinaryStreamForRead etc.
    // ---------------------------------------------------------------------

    class ToolRuntimeScope {
    public:
        ToolRuntimeScope() {
            const auto cwd = fs::current_path().string();
            TVPNativeProjectDir = ttstr(cwd);
            TVPProjectDir = TVPNormalizeStorageName(TVPNativeProjectDir);
            TVPInitScriptEngine();
            TVPInitializeBaseSystems();
            TVPSystemInit();
        }
    };

    // ---------------------------------------------------------------------
    // Time grid parsing.
    // ---------------------------------------------------------------------

    std::vector<double> parseTimeList(const std::string &csv) {
        std::vector<double> out;
        size_t i = 0;
        while(i < csv.size()) {
            size_t j = csv.find(',', i);
            if(j == std::string::npos) j = csv.size();
            try {
                out.push_back(std::stod(csv.substr(i, j - i)));
            } catch(...) {}
            i = j + 1;
        }
        return out;
    }

    std::vector<double>
    generateTimeGrid(double dt,
                     const motion::detail::MotionSnapshot &snapshot,
                     const std::string &clipLabel) {
        double total = 600.0;
        if(!clipLabel.empty()) {
            auto it = snapshot.clipIndexByLabel.find(clipLabel);
            if(it != snapshot.clipIndexByLabel.end() &&
               it->second >= 0 &&
               it->second < static_cast<int>(snapshot.clipList.size())) {
                total = snapshot.clipList[it->second].totalFrames;
            }
        }
        if(total <= 0.0) total = 600.0;
        std::vector<double> grid;
        for(double t = 0.0; t <= total + 1e-9; t += dt) {
            grid.push_back(t);
        }
        return grid;
    }

}  // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program(PROGRAM_NAME, VERSION);
    program.add_argument("file")
        .help("input .mtn / .psb motion file");
    program.add_argument("-s", "--seed")
        .help("decrypt seed for encrypted PSB (0 = plain, default: 0)")
        .default_value(0)
        .scan<'i', int>();
    program.add_argument("--clip")
        .help("clip label to simulate (e.g. 'back_white' for m2logo.mtn)")
        .default_value(std::string{});
    program.add_argument("--time-grid")
        .help("time step in frames (default: 1.0 ≈ 1/60 second)")
        .default_value(1.0)
        .scan<'g', double>();
    program.add_argument("--times")
        .help("comma-separated time points (overrides --time-grid)")
        .default_value(std::string{});
    program.add_argument("-o", "--output")
        .help("TSV output path")
        .default_value(std::string{"./motionsim_expected.tsv"});
    program.add_argument("--root-pos")
        .help("root.accum.pos offset 'X,Y' — typically (player.x, player.y). "
              "For m2logo.mtn via startup.tjs this is '960,540'.")
        .default_value(std::string{"0,0"});

    try {
        program.parse_args(argc, argv);
    } catch(const std::exception &err) {
        std::cerr << err.what() << '\n' << program;
        return 1;
    }

    auto logger = spdlog::stdout_color_mt("motionsim");
    spdlog::set_default_logger(logger);
    spdlog::stdout_color_mt("core");
    spdlog::stdout_color_mt("tjs2");
    spdlog::stdout_color_mt("plugin");
    spdlog::set_pattern("%^%v%$");

    ToolRuntimeScope runtime;

    const fs::path inputPath = fs::path(program.get<std::string>("file"));
    if(!fs::exists(inputPath) || fs::is_directory(inputPath)) {
        spdlog::error("input not a file: {}", inputPath.string());
        return 1;
    }

    const tjs_int seed =
        static_cast<tjs_int>(program.get<int>("--seed"));
    auto snapshot = motion::detail::loadMotionSnapshot(
        ttstr(inputPath.string()), seed);
    if(!snapshot) {
        spdlog::error("failed to load motion snapshot (wrong seed?): {}",
                      inputPath.string());
        return 2;
    }

    const std::string clipLabel = program.get<std::string>("--clip");
    {
        const auto rp = parseTimeList(program.get<std::string>("--root-pos"));
        if(rp.size() >= 2) {
            g_rootPosX = rp[0];
            g_rootPosY = rp[1];
        }
    }
    auto nodes = buildFlatNodeTree(*snapshot, clipLabel);
    spdlog::info("built flat node tree: {} nodes (including synthetic root)",
                 nodes.size());

    // One-shot mesh scan — report any node with meshType != 0 so we know
    // whether mesh deformation (sub_69AE74 path) is reachable for this .mtn.
    int meshNodeCount = 0;
    for(const auto &n : nodes) {
        if(n.meshType != 0 || n.meshFlags != 0 || n.meshDivision != 0) {
            ++meshNodeCount;
            spdlog::info(
                "  mesh node: idx={} label={} type={} meshType={} "
                "meshFlags=0x{:x} meshDivision={}",
                n.index,
                n.label.empty() ? std::string("<unnamed>") : n.label,
                n.nodeType, n.meshType, n.meshFlags, n.meshDivision);
        }
    }
    spdlog::info("mesh scan: {} node(s) with non-zero mesh fields",
                 meshNodeCount);

    const std::string timesCsv = program.get<std::string>("--times");
    std::vector<double> timeGrid;
    if(!timesCsv.empty()) {
        timeGrid = parseTimeList(timesCsv);
    } else {
        timeGrid = generateTimeGrid(
            program.get<double>("--time-grid"), *snapshot, clipLabel);
    }
    spdlog::info("simulating {} time points (clip={})", timeGrid.size(),
                 clipLabel.empty() ? std::string("<root>") : clipLabel);

    const fs::path outPath = fs::path(program.get<std::string>("--output"));
    fs::create_directories(outPath.parent_path().empty()
                               ? fs::path(".")
                               : outPath.parent_path());
    std::ofstream tsv(outPath);
    if(!tsv.is_open()) {
        spdlog::error("cannot open output: {}", outPath.string());
        return 3;
    }

    // TSV header
    tsv << "time\tnodeIdx\tlabel\ttype\tparentIdx\tinheritFlags"
           "\tvisible\tactive"
           "\tstate.posX\tstate.posY\tstate.scaleX\tstate.scaleY"
           "\tstate.angle\tstate.opacity"
           "\taccum.posX\taccum.posY\taccum.posZ"
           "\taccum.scaleX\taccum.scaleY\taccum.angle\taccum.opacity"
           "\taccum.m11\taccum.m21\taccum.m12\taccum.m22"
           "\tclipW\tclipH\toriginX\toriginY"
           "\tcoverageWarn\n";

    for(double t : timeGrid) {
        // First pass: evaluate framesel per node (REUSED — via
        // motion::internal::evaluateLayerContent). This is the framesel
        // interpolation that is already exhaustively traced on the Web side
        // and is not the current suspected bug surface.
        for(size_t i = 1; i < nodes.size(); ++i) {
            SimNode &node = nodes[i];
            node.state = motion::internal::evaluateLayerContent(
                node.psbNode, t, node.nodeType);
            if(!node.state.src.empty()) {
                populateSourceMetadata(node, *snapshot);
            }
        }

        // Second pass: phase-2 accumulator (INDEPENDENT re-impl).
        runPhase2(nodes);

        // Third pass: dump TSV row per non-root node.
        for(size_t i = 1; i < nodes.size(); ++i) {
            const SimNode &n = nodes[i];
            tsv << t << '\t'
                << n.index << '\t'
                << (n.label.empty() ? std::string("<unnamed>") : n.label)
                << '\t' << n.nodeType
                << '\t' << n.parentIndex
                << '\t' << "0x" << std::hex << n.inheritFlags << std::dec
                << '\t' << (n.accum.visible ? 1 : 0)
                << '\t' << (n.accum.active ? 1 : 0)
                << '\t' << n.state.x
                << '\t' << n.state.y
                << '\t' << n.state.scaleX
                << '\t' << n.state.scaleY
                << '\t' << n.state.angle
                << '\t' << n.state.opacity
                << '\t' << n.accum.posX
                << '\t' << n.accum.posY
                << '\t' << n.accum.posZ
                << '\t' << n.accum.scaleX
                << '\t' << n.accum.scaleY
                << '\t' << n.accum.angle
                << '\t' << n.accum.opacity
                << '\t' << n.accum.m11
                << '\t' << n.accum.m21
                << '\t' << n.accum.m12
                << '\t' << n.accum.m22
                << '\t' << n.clipW
                << '\t' << n.clipH
                << '\t' << n.originX
                << '\t' << n.originY
                << '\t' << (n.coverageWarn.empty() ? std::string("ok")
                                                   : n.coverageWarn)
                << '\n';
        }
    }

    tsv.close();
    spdlog::info("wrote {}", outPath.string());
    return 0;
}
