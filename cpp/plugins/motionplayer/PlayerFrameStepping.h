// PlayerFrameStepping.h — M1/P3+P4 binary-aligned node-deque frame-stream
// slot/cursor structure + forward/back frame cursor stepping.
//
// SCOPE (M1 staged re-architecture, phases P3 + P4 — see
//   analysis/Player_progress_frame_stepping_M1_plan.md):
//
// P3 (this header): the node-deque frame-stream / cursor data model that the
// binary's progress core walks. It layers on top of the P2 parsed-frame slot
// (ParsedFrameSlotLike_0x6926B4, PlayerFrameStep.h):
//   * NodeFrameStreamsLike    — per-node: two 536-byte parsed-frame slots
//                               (node+320 / node+856), the active-slot cursor
//                               (node+1392), the frameList source (node+64),
//                               nodeType (node+28), timelineDirty (node+1996),
//                               and the per-node active gate (node+8).
//   * FrameStreamCursorLike   — a player-level frame stream cursor: the layer
//                               stream (cursor=player+916, curTime=+920,
//                               nextTime=+928) and the root stream
//                               (cursor=+568, curTime=+576, nextTime=+584).
//   * TimelineSeekStateLike   — the player-level scalars these routines read /
//                               mutate: clampedEvalTime (+456), frameTickCount
//                               (+1120), motionStopGate (+1093), motionCompleted
//                               (+483), syncWaiting (+1098), emoteListFlag
//                               (+1092). Mirrors the live Player.h members but is
//                               kept isolated so the P4 routines are pure and
//                               unit-testable without a full Player.
//
// P4 (PlayerFrameStepping.cpp): the cursor stepping routines, ported 1:1 from:
//   * Player_advanceNodeFrames @ 0x6B7E44 -> advanceNodeFramesLike_0x6B7E44
//   * Player_advanceRootAndNodes @ 0x6B6ADC -> advanceRootAndNodesLike_0x6B6ADC
//   * Player_rewindRootAndNodes @ 0x6B9A3C -> rewindRootAndNodesLike_0x6B9A3C
//   * Player_reseekTimelineCursors @ 0x6B86C8 -> reseekTimelineCursorsLike_0x6B86C8
//
// They call the P2 parseFrame/mergeFrameContent (...Like_0x6926B4 / 0x692AB0).
//
// INDEPENDENCE: these are free functions, exercised only by the motionplayer-dll
// unit test. They are NOT wired into the live frame-progress path
// (PlayerFrameProgress.cpp / PlayerTimeline.cpp / PlayerUpdateLayerEval.cpp are
// untouched), so the logo differential stays 0-mismatch.
//
// PLATFORM_BOUNDARY: in libkrkr2.so each frame stream is a TJS Array dispatch
// (iTJSDispatch2) of per-frame dicts, walked through PropGet wrappers. The local
// port has no live iTJSDispatch2 motion tree, so — exactly like P2 — a frame
// stream is sourced from a PSB::PSBList of PSB::PSBDictionary frames. The cursor
// indexing, two-slot ping-pong seek, completion/sync/align gating and merge
// dispatch are reproduced verbatim; only the leaf "read frame[i]['time']" goes
// through PSB instead of dispatch.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "PlayerFrameStep.h"  // ParsedFrameSlotLike_0x6926B4, parse/merge

namespace PSB {
    class PSBList;
    class PSBDictionary;
}

namespace motion {
    namespace detail {

        // ------------------------------------------------------------------
        // P3: player-level scalar state read / written by the stepping routines.
        // Offsets are relative to Player* (a1) in libkrkr2.so.
        // ------------------------------------------------------------------
        struct TimelineSeekStateLike {
            // +456 clampedEvalTime — the time anchor every stream seeks toward.
            double clampedEvalTime = 0.0;
            // +1120 frameTickCount — main playback cursor (action/sync/align
            //        gates snap it to the gated frame time).
            double frameTickCount = 0.0;
            // +1093 motionStopGate — when set, action/sync/align frame content is
            //        processed (sets completion / sync). 1-byte (LDRB).
            std::uint8_t motionStopGate = 0;
            // +483 motionCompleted — set when an "align" frame is hit while the
            //        stop gate is active.
            std::uint8_t motionCompleted = 0;
            // +1098 syncWaiting — set when a "sync" frame is hit while the stop
            //        gate is active (also fires sub_6B6294).
            std::uint8_t syncWaiting = 0;
            // +1092 emoteListFlag — selects the node-type findSource mask
            //        (6145 when clear, 6153 when set). 1-byte (LDRB).
            std::uint8_t emoteListFlag = 0;
        };

        // ------------------------------------------------------------------
        // P3: a player-level frame stream cursor.
        //   layer stream  -> cursor=player+916, curTime=+920, nextTime=+928
        //   root  stream  -> cursor=player+568, curTime=+576, nextTime=+584
        // The binary stores the stream itself as a TJS Array dispatch (layer @
        // player+1072, root @ player+548). Here it is a PSB list of frame dicts.
        // ------------------------------------------------------------------
        struct FrameStreamCursorLike {
            // The frame stream source (TJS Array dispatch in the binary).
            std::shared_ptr<PSB::PSBList> frames;
            int frameCursor = 0;     // +916 / +568
            double curTime = 0.0;    // +920 / +576
            double nextTime = 0.0;   // +928 / +584
            // root stream only: the current frame's "content" snapshot (+616).
            std::shared_ptr<PSB::PSBDictionary> rootContent;  // player+616
        };

        // ------------------------------------------------------------------
        // P3: per-node frame-stream state inside the node-deque (2632B node,
        // player+200 base). Holds the binary's two parsed-frame slots and the
        // active-slot cursor.
        // ------------------------------------------------------------------
        struct NodeFrameStreamsLike {
            // node+8: a pointer to the node's child timeline object (non-null for
            // type-3 Motion child nodes). In the root walk
            // (advanceRootAndNodes/rewindRootAndNodes) a non-null node+8 routes
            // the node through advanceNodeFrames (which seeks to the CHILD's eval
            // time, (node+8)+40); a null node+8 takes the inline per-node seek that
            // uses the player's clampedEvalTime instead. Modelled here as a flag +
            // the child eval time.
            bool hasChild = false;        // node+8 != null
            double childEvalTime = 0.0;   // *(double*)((node+8) + 40)
            // active gate. The root walk skips nodes with active==0.
            bool active = true;
            // node+28: nodeType (drives the source-gate mask in merge/findSource).
            int nodeType = 0;
            // node+1996: timelineDirty. When set (or the node-type mask matches)
            //            findSource runs after a seek.
            int timelineDirty = 0;
            // node+64: frameList — the per-node frame stream (TJS Array dispatch
            //          in the binary; PSB list of frame dicts here).
            std::shared_ptr<PSB::PSBList> frameList;
            // node+320 (slot0) / node+856 (slot1): the two parsed-frame slots.
            ParsedFrameSlotLike_0x6926B4 slots[2];
            // node+1392: active-slot index. Toggled by (x & 1) == 0 each step.
            int activeSlotIndex = 0;

            ParsedFrameSlotLike_0x6926B4 &active_slot() {
                return slots[activeSlotIndex];
            }
            ParsedFrameSlotLike_0x6926B4 &other_slot() {
                return slots[activeSlotIndex ^ 1];
            }
        };

        // Aggregate the player-level streams + node-deque the routines walk.
        // (In libkrkr2.so these all live inside the single Player object; here we
        // bundle them so the P4 free functions take one argument and stay pure.)
        struct PlayerFrameStreamsLike {
            TimelineSeekStateLike state;
            FrameStreamCursorLike layerStream;  // player+1072/+916/+920/+928
            FrameStreamCursorLike rootStream;   // player+548/+568/+576/+584/+616
            // node-deque (player+200, 2632B stride). Index 0 is the root node.
            std::vector<NodeFrameStreamsLike> nodes;
        };

        // ------------------------------------------------------------------
        // P4 routines.
        // ------------------------------------------------------------------

        // Player_advanceNodeFrames @ 0x6B7E44.
        // Forward (and corrective backward) per-node frame seek toward
        // state.clampedEvalTime, then merge both slots + (gated) findSource.
        void advanceNodeFramesLike_0x6B7E44(NodeFrameStreamsLike &node,
                                            const TimelineSeekStateLike &state);

        // Player_advanceRootAndNodes @ 0x6B6ADC.
        // Forward advance of the layer stream cursor, then the root stream cursor,
        // then every node via advanceNodeFramesLike. action/sync/align gating on
        // the layer stream uses state.motionStopGate.
        void advanceRootAndNodesLike_0x6B6ADC(PlayerFrameStreamsLike &p);

        // Player_rewindRootAndNodes @ 0x6B9A3C.
        // Backward analogue: decrements the layer/root cursors while their curTime
        // is still ahead of clampedEvalTime, then rewinds every node.
        void rewindRootAndNodesLike_0x6B9A3C(PlayerFrameStreamsLike &p);

        // Player_reseekTimelineCursors @ 0x6B86C8.
        // Full (non-incremental) re-seek of the layer + root streams to
        // clampedEvalTime via a fresh linear scan, with action/sync/align gating,
        // then a per-node advance. Called on firstFrame seed and loop wraparound.
        // (The variable-track deque and the +280 aux list are PLATFORM_BOUNDARY
        // deferred — see .cpp.)
        void reseekTimelineCursorsLike_0x6B86C8(PlayerFrameStreamsLike &p);

    } // namespace detail
} // namespace motion
