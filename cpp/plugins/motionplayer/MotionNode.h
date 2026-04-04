//
// Persistent per-node state for MotionPlayer rendering pipeline.
// Aligned to libkrkr2.so 2632-byte node structure in std::deque.
//
// PSB key → node offset mapping (from IDA decompilation of sub_6B3C78 at 0x6B3C78):
//   "label"            → node+0   (name)
//   "type"             → node+28  (nodeType)
//   "coordinate"       → node+24  (coordinateMode)
//   "inheritMask"      → node+40  (inheritFlags, bits 2-8, default 0x1FC)
//   "groundCorrection" → node+47  (bool)
//   "transformOrder"   → node+84..96 (4 ints, default [0,1,2,3])
//   "frameList"        → node+64  (PSB variant for keyframes)
//   "meshTransform"    → node+2000 (meshType)
//   "stencilType"      → node+52
//   parentIndex        → node+36  (set during tree walk)
//   node+344, node+880 → clip slot done flags (initialized to 1=done)
//
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace PSB {
    class PSBDictionary;
}

namespace motion {
    class Player;
}

namespace motion::detail {

    struct MotionNode {
        // Identity (from PSB, set once during tree build)
        int index = 0;
        int parentIndex = -1;          // node+36
        int nodeType = 0;              // node+28
        int coordinateMode = 0;        // node+24
        int inheritFlags = 0x1FC;      // node+40 (bits 2-8, default all-set)
        uint8_t flags = 0;             // node+42 (bit 0x40 = skip in parent walk)
        bool groundCorrection = false; // node+47
        int transformOrder[4] = {0, 1, 2, 3}; // node+84..96
        bool hasSource = false;        // has any frame with non-empty src
        std::string layerName;         // "label" from PSB
        int meshType = 0;             // "meshTransform" from PSB (node+2000)
        int meshFlags = 0;            // "meshSyncChildMask" from PSB (node+2004)
        int stencilType = 0;          // "stencilType" from PSB

        // Clip slot state — aligned to sub_6B4A6C: both slots start done=true.
        // Set to false when evaluateLayerContent finds an active frame (type!=0).
        bool slotDone = true;          // node+344 / node+880 (current slot)

        // PSB reference (for evaluateLayerContent calls)
        std::shared_ptr<const PSB::PSBDictionary> psbNode;

        // Accumulated state (built during updateLayers inheritance loop)
        // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C) main loop
        struct AccumulatedState {
            bool visible = false;
            bool active = false;
            bool flipX = false;
            bool flipY = false;
            double posX = 0.0;
            double posY = 0.0;
            double posZ = 0.0;
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            int opacity = 255;         // 0-255 integer, matching libkrkr2.so int math
            // 2x2 matrix (local × parent accumulated)
            double m11 = 1.0;
            double m12 = 0.0;
            double m21 = 0.0;
            double m22 = 1.0;
        } accumulated;

        // Previous position (for delta computation in post-loop)
        double prevPosX = 0.0;
        double prevPosY = 0.0;
        double prevPosZ = 0.0;

        // Draw flag (computed in post-loop visibility pass, sub_6BD8DC)
        bool drawFlag = false;
        int forceVisible = 0;              // node+1996
        int visibleAncestorIndex = -1;     // replaces pointer at node+1952

        // Child Player for nodeType=3 (Motion) and nodeType=4 (Particle).
        // Aligned to sub_6BE0C0 (0x6BE0C0): creates child Player instances.
        std::shared_ptr<Player> childPlayer;
        bool childNeedsInit = true;  // true until first play() call

        // Shape type for nodeType=1 (from PSB "shape" key, sub_6B3C78 case 1)
        int shapeType = 0;             // node+32: 0=point, 1=circle, 2=rect, 3=quad

        // Shape AABB for nodeType=7 (sub_6BDCC0 at 0x6BDCC0)
        float shapeAABB[4] = {};       // node+2144: minX, minY, maxX, maxY

        // Shape geometry for nodeType=1 (sub_6BDE94 at 0x6BDE94)
        int shapeGeomType = 0;         // node+1664: stored shape type
        double shapeVertices[16] = {}; // node+1672..1784: shape geometry

        // Vertex-computed position (sub_6BC4F0 at 0x6BC4F0)
        double vertexPosX = 0.0;       // node+152
        double vertexPosY = 0.0;       // node+160
        double vertexPosZ = 0.0;       // node+168

        // Vertex output (sub_6BC4F0)
        float vertices[8] = {};        // node+1856..1884: 4 corners x 2 floats

        // Origin offsets (from PSB source icon, sub_6BC4F0)
        double originX = 0.0;          // node+248
        double originY = 0.0;          // node+256
        double clipW = 0.0;            // node+232
        double clipH = 0.0;            // node+240

        // Anchor node data for nodeType=10 (sub_6C0528 at 0x6C0528)
        int anchorType = 0;            // node+2376: "anchor" from PSB
        double anchorDamping = 1.0;    // node+2432: damping factor
        double anchorOpaScale = 1.0;   // node+2440: opacity damping scale
        // Anchor color channel scales (4 channels × gamma factor, node+2448..2504)
        double anchorColorScale[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};

        // Camera constraint for nodeType=9 (sub_6BC000 at 0x6BC000)
        int cameraConstraintType = 0;  // node+2376: "anchor" type

        // Particle child Players for nodeType=4 (sub_6BF0DC at 0x6BF0DC)
        // Each particle is a child Player instance with its own PSB/timelines.
        std::vector<std::shared_ptr<Player>> particleChildren;
        int particleType = 0;          // node+2164: particle subtype
        int particleMaxNum = 0;        // node+2168: max particle count
        double particleAccelRatio = 0; // node+2192
        bool particleInheritAngle = false; // node+2172
        int particleInheritVelocity = 0;   // node+2176
        int particleFlyDirection = 0;      // node+2180
        int particleApplyZoomToVelocity = 0; // node+2184
        bool particleDeleteOutside = false;  // node+2188
        // Per-particle state (position, velocity, angle, lifetime)
        struct ParticleState {
            double posX = 0, posY = 0, posZ = 0;
            double velX = 0, velY = 0;
            double angle = 0;
            double zoom = 1.0;
            bool alive = false;
        };
        std::vector<ParticleState> particleStates;

        // Particle emitter state for nodeType=6 (sub_6BEDD0 at 0x6BEDD0)
        bool emitterActive = false;    // node+2380
        double emitterTimer = 0.0;     // node+2392
        std::string emitterDtgt;       // resolved dtgt path (node+2384 stores ttstr)
        bool emitterOffsetActive = false; // node+2400
        double emitterOffsetX = 0.0;   // node+2408
        double emitterOffsetY = 0.0;   // node+2416
        double emitterOffsetZ = 0.0;   // node+2424

        // Delta position (post-loop: accumulated - prev, node+176/184/192)
        double deltaPosX = 0.0;
        double deltaPosY = 0.0;
        double deltaPosZ = 0.0;

        // Clip origin offsets (from clip slot+376/384, used by sub_6BC4F0)
        double clipOriginX = 0.0;      // node+248 local
        double clipOriginY = 0.0;      // node+256 local

        // Parent clip region index (replaces node+1936 pointer)
        int parentClipIndex = -1;

        // Anchor enabled flag (node+200 in sub_6C0528 context)
        bool anchorEnabled = false;

        // Color bytes for anchor damping (node+100..115: 4 sets of RGBA)
        uint8_t colorBytes[16] = {
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF,
            0x80, 0x80, 0x80, 0xFF
        };

        // Particle trigger type (from interpolatedCache, used by sub_6BEDD0)
        int prtTrigger = 0;

        // Per-frame interpolated data cached from evaluateLayerContent.
        // These are the fields needed by buildRenderListFromNodes that
        // come from FrameContentState (which lives in Player.cpp's
        // anonymous namespace and can't be referenced here).
        struct InterpolatedCache {
            std::string src;
            double width = 0.0;
            double height = 0.0;
            double opacity = 1.0;  // 0.0-1.0 as from PSB
            double angle = 0.0;
            double scaleX = 1.0;
            double scaleY = 1.0;
            double slantX = 0.0;
            double slantY = 0.0;
            bool flipX = false;
            bool flipY = false;
            double x = 0.0;
            double y = 0.0;
            double ox = 0.0;
            double oy = 0.0;
            int blendMode = 16;
            int colorR = 0x80;
            int colorG = 0x80;
            int colorB = 0x80;
            int colorA = 0xFF;
            bool hasTransformOrder = false;
            int transformOrder[4] = {0, 1, 2, 3};
            std::string action;
            bool hasSync = false;
            // Particle data from FrameContentState (mask 0x100000)
            int prtTrigger = 0;
            double prtFmin = 10.0;
            double prtF = 10.0;
            double prtVmin = 0.0;
            double prtV = 0.0;
            double prtAmin = 0.0;
            double prtA = 0.0;
            double prtZmin = 1.0;
            double prtZ = 1.0;
            double prtRange = 0.0;
        } interpolatedCache;
    };

} // namespace motion::detail
