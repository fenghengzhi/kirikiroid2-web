// PlayerFrameStepping.cpp — M1/P3+P4 binary-aligned node-deque frame cursor
// stepping. Independent of the live frame-progress path (unit-test only).
//
// 1:1 ports of:
//   Player_advanceNodeFrames     @ libkrkr2.so 0x6B7E44
//   Player_advanceRootAndNodes   @ libkrkr2.so 0x6B6ADC
//   Player_rewindRootAndNodes    @ libkrkr2.so 0x6B9A3C
//   Player_reseekTimelineCursors @ libkrkr2.so 0x6B86C8
//
// See PlayerFrameStepping.h for the scope rationale and PLATFORM_BOUNDARY notes.
//
// Binary frame-stream read helpers (each is an iTJSDispatch2 PropGet wrapper in
// the binary; here they read PSB::PSBList / PSB::PSBDictionary as the P2
// stand-in):
//   sub_56C694  count       -> list->size()
//   PropGet[i]  frames[i]   -> frameDictAt(list, i)
//   sub_662668  frame.time  -> frameTime()
//   sub_6635DC  frame.type  -> frameType()
//   sub_6636D4  content.<k> -> frameContentBool()
//   sub_A0FB64  copy content-> root content snapshot
//
// DEFERRED (PLATFORM_BOUNDARY / deep dispatch, see inline notes):
//   * the variable-track deque (player+1312..1368) two-slot seed
//     (sub_6B786C / sub_6B7A70) — libstdc++ deque of opaque 160-byte track
//     records with their own dispatch frame streams; not reproducible from the
//     PSB stand-in. reseek skips it.
//   * the +280 aux singly-linked list pass (sub_6B9650) — opaque.
//   * Motion_Player_findSource (slot+348/+356) — deep layer-source resolution;
//     gated identically to the binary but the resolution body is deferred.
//   * sub_6B638C action runner — TJS action dispatch; gated identically, body
//     deferred. We record that an action/sync/align frame WAS hit (so cursor +
//     completion/sync state stays binary-faithful) without running the action.

#include "PlayerFrameStepping.h"

#include "psbfile/PSBValue.h"

#include <utility>

namespace motion {
    namespace detail {

        namespace {

            // sub_56C694: frame-stream element count.
            int streamCount(const std::shared_ptr<PSB::PSBList> &frames) {
                if(!frames) return 0;
                return static_cast<int>(frames->size());
            }

            // PropGet[index] -> frame dict (the binary fetches a TJS object).
            std::shared_ptr<PSB::PSBDictionary>
            frameDictAt(const std::shared_ptr<PSB::PSBList> &frames, int index) {
                if(!frames || index < 0 ||
                   index >= static_cast<int>(frames->size())) {
                    return nullptr;
                }
                return std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frames)[index]);
            }

            // sub_662668: frame["time"] as double.
            double frameTime(const std::shared_ptr<PSB::PSBDictionary> &frame) {
                if(!frame) return 0.0;
                auto v = (*frame)["time"];
                if(auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
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
                return 0.0;
            }

            // sub_6635DC: frame["type"] as int.
            int frameType(const std::shared_ptr<PSB::PSBDictionary> &frame) {
                if(!frame) return 0;
                auto v = (*frame)["type"];
                if(auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                    return n->getValue<int>();
                }
                return 0;
            }

            std::shared_ptr<PSB::PSBDictionary>
            frameContent(const std::shared_ptr<PSB::PSBDictionary> &frame) {
                if(!frame) return nullptr;
                return std::dynamic_pointer_cast<PSB::PSBDictionary>(
                    (*frame)["content"]);
            }

            // sub_6636D4: content["align"|"sync"] truthiness.
            bool contentBool(const std::shared_ptr<PSB::PSBDictionary> &content,
                             const char *key) {
                if(!content) return false;
                auto v = (*content)[key];
                if(auto n = std::dynamic_pointer_cast<PSB::PSBNumber>(v)) {
                    return n->getValue<int>() != 0;
                }
                if(auto b = std::dynamic_pointer_cast<PSB::PSBBool>(v)) {
                    return b->value;
                }
                return false;
            }

            // The two findSource node-type masks: 6145 (0x1801) when the emote
            // list flag (player+1092) is clear, 6153 (0x1809) when set
            // (advance/rewind/advanceNode: `!a1+1092 ? 6145 : 6153`).
            bool findSourceGate(const NodeFrameStreamsLike &node,
                                std::uint8_t emoteListFlag) {
                if(node.timelineDirty) return true;
                const int mask = emoteListFlag ? 6153 : 6145;
                return (mask & (1 << node.nodeType)) != 0;
            }

            // Motion_Player_findSource(node+200, player, slot+356, slot+348)
            // READS the active slot src/icon and WRITES node+200. This pure
            // stepping model records that write; the live Player resolves the
            // variant, atlas pointer and geometry in findSourceForNodeLike_0x6948E8.
            void findSourceLike(NodeFrameStreamsLike &node) {
                const auto &slot = node.active_slot();
                node.sourceSrc = slot.src;
                node.sourceIcon = slot.icon;
                node.sourceResolved = !slot.src.empty();
            }

            // Apply the action/sync/align gate that advance/rewind/reseek run when
            // a layer-stream frame's type == 1 (sub_6B638C + the +1093 stop-gate
            // block). The binary, for the layer/root *root* streams, reads the
            // current frame's content and, if motionStopGate is set:
            //   * if content["align"]: motionCompleted=1; clampedEvalTime =
            //     frameTickCount = curTime
            //   * if content["sync"]:  syncWaiting=1; clampedEvalTime =
            //     frameTickCount = curTime; sub_6B6294(player)
            // then, regardless, if content["action"] -> sub_6B638C action run.
            // sub_6B6294 / sub_6B638C are DEFERRED (TJS dispatch); the cursor +
            // completion/sync scalar effects are reproduced.
            void applyLayerActionGate(
                TimelineSeekStateLike &state, double layerCurTime,
                const std::shared_ptr<PSB::PSBDictionary> &content) {
                if(state.motionStopGate) {
                    // 0x6B8AC0 / 0x6B6DD8 / 0x6B9D2C: align gate.
                    // layerCurTime == player+920; compared to player+456.
                    if(layerCurTime == state.clampedEvalTime &&
                       contentBool(content, "align")) {
                        state.motionCompleted = 1;
                        state.clampedEvalTime = layerCurTime;
                        state.frameTickCount = layerCurTime;
                    }
                    // 0x6B8AFC / 0x6B6E14 / 0x6B9D64: sync gate.
                    if(state.motionStopGate && contentBool(content, "sync")) {
                        state.syncWaiting = 1;
                        state.clampedEvalTime = layerCurTime;
                        state.frameTickCount = layerCurTime;
                        // sub_6B6294(player) — DEFERRED.
                    }
                }
                // content["action"] -> sub_6B638C(player, frame, &action)
                // DEFERRED: action dispatch body. (Presence is honoured by the
                // caller advancing past the frame; no scalar side-effect here.)
            }

            // mergeFrameContent's content arg: in the binary
            // Player_mergeFrameContent(slot, nodeType, node+64) re-reads
            // frameList[slot.frameIndex]["content"] internally. The P2 port takes
            // the content dict pre-fetched, so resolve it per-slot here.
            std::shared_ptr<PSB::PSBDictionary>
            slotContent(const NodeFrameStreamsLike &node,
                        const ParsedFrameSlotLike_0x6926B4 &slot) {
                return frameContent(
                    frameDictAt(node.frameList,
                                static_cast<int>(slot.frameIndex)));
            }

        } // namespace

        // ==================================================================
        // Player_advanceNodeFrames @ 0x6B7E44
        // ------------------------------------------------------------------
        // Per-node forward (with corrective backward) frame seek toward the
        // CHILD timeline's eval time (node.childEvalTime == *(node+8)+40), then
        // merge both slots and (gated) findSource. Called by the root walk only
        // for nodes whose node+8 child pointer is non-null.
        // ------------------------------------------------------------------
        void advanceNodeFramesLike_0x6B7E44(NodeFrameStreamsLike &node,
                                            const TimelineSeekStateLike &state) {
            const int slot_idx = node.activeSlotIndex;     // 0x6B7E84: node+1392
            const double t = node.childEvalTime;           // 0x6B7E90: *(node+8)+40
            const int count = streamCount(node.frameList); // 0x6B7EF0: sub_56C694

            // 0x6B7F04..0x6B7F10: v9=false; cur=&slots[slot_idx];
            // other=&slots[slot_idx^1]; v12 = count - 2.
            bool seeked = false;
            ParsedFrameSlotLike_0x6926B4 *cur = &node.slots[slot_idx];
            ParsedFrameSlotLike_0x6926B4 *other = &node.slots[slot_idx ^ 1];
            const int limit = count - 2;

            // Forward seek loop @ 0x6B7F14: while cur.frameIndex < count-2 &&
            // t >= other.time, advance: toggle activeSlot, parse other.frame+1
            // into cur, swap roles.
            for(;;) {
                ParsedFrameSlotLike_0x6926B4 *v13 = cur;     // 0x6B7F14
                const int v14 = cur->frameIndex;             // 0x6B7F18: *cur
                if(v14 >= limit || t < other->time) {        // 0x6B7F2C
                    cur = v13;
                    break;
                }
                seeked = true;                               // 0x6B7F34
                node.activeSlotIndex ^= 1;                   // 0x6B7F40
                // 0x6B7F54: parseFrame(cur, frameList, other.frameIndex + 1).
                parseFrameLike_0x6926B4(
                    *cur, frameDictAt(node.frameList, other->frameIndex + 1),
                    static_cast<std::uint32_t>(other->frameIndex + 1),
                    node.nodeType);
                // 0x6B7F58: swap roles (v10=other; v11=cur(=v13)).
                ParsedFrameSlotLike_0x6926B4 *tmp = cur;
                cur = other;
                other = tmp;
                (void)v14;
            }

            // 0x6B7F6C: if cur.time > t -> corrective backward seek.
            if(cur->time > t) {
                int v14 = cur->frameIndex;
                for(;;) {
                    node.activeSlotIndex ^= 1;               // 0x6B7F98
                    // 0x6B7FA4: parseFrame(other, frameList, cur.frameIndex - 1).
                    parseFrameLike_0x6926B4(
                        *other, frameDictAt(node.frameList, v14 - 1),
                        static_cast<std::uint32_t>(v14 - 1), node.nodeType);
                    if(other->time <= t) {                   // 0x6B7FB0
                        break;
                    }
                    // 0x6B7F78: swap, continue.
                    v14 = other->frameIndex;
                    ParsedFrameSlotLike_0x6926B4 *tmp = cur;
                    cur = other;
                    other = tmp;
                }
            } else if(!seeked) {
                // 0x6B7F70: no movement -> skip merge/findSource.
                return;
            }

            // Merge block @ 0x6B7FB4. node+44 dirty = 1 (not modelled separately).
            // node+346 == slots[0].mergedFlag; node+882 == slots[1].mergedFlag.
            if(!node.slots[0].mergedFlag) {                  // 0x6B7FC0
                mergeFrameContentLike_0x692AB0(
                    node.slots[0], node.nodeType, slotContent(node, node.slots[0]));
            }
            if(!node.slots[1].mergedFlag) {                  // 0x6B7FD4
                mergeFrameContentLike_0x692AB0(
                    node.slots[1], node.nodeType, slotContent(node, node.slots[1]));
            }
            // 0x6B7FF0: findSource gate (node+1996 || type-mask).
            if(findSourceGate(node, state.emoteListFlag)) {
                findSourceLike(node);
            }
        }

        // ==================================================================
        // Player_advanceRootAndNodes @ 0x6B6ADC
        // ------------------------------------------------------------------
        // Forward advance of the layer stream cursor, then the root stream
        // cursor, then every node. (Variable-track deque DEFERRED.)
        // ------------------------------------------------------------------
        void advanceRootAndNodesLike_0x6B6ADC(PlayerFrameStreamsLike &p) {
            TimelineSeekStateLike &state = p.state;
            FrameStreamCursorLike &layer = p.layerStream;

            // ---- Layer stream forward advance @ 0x6B6B74 ----
            const int layerCount = streamCount(layer.frames);
            if(layerCount >= 1) {
                // 0x6B6B80..: while cursor < count-2 && clampedEvalTime >=
                // nextTime: advance cursor, refresh curTime/nextTime, run the
                // type==1 action/align/sync gate.
                const int i = layerCount - 2;
                while(layer.frameCursor < i) {                 // 0x6B6B8C
                    if(state.clampedEvalTime < layer.nextTime) // 0x6B6BC8
                        break;
                    layer.frameCursor = layer.frameCursor + 1; // 0x6B6BD0
                    // 0x6B6C68: curTime = frames[cursor].time.
                    auto cf = frameDictAt(layer.frames, layer.frameCursor);
                    layer.curTime = frameTime(cf);
                    // 0x6B6D0C: nextTime = frames[cursor+1].time.
                    auto nf = frameDictAt(layer.frames, layer.frameCursor + 1);
                    layer.nextTime = frameTime(nf);
                    // 0x6B6D2C: if frames[cursor].type == 1 -> action gate.
                    if(frameType(cf) == 1) {
                        applyLayerActionGate(state, layer.curTime,
                                             frameContent(cf));
                    }
                }
            }

            // ---- Root stream forward advance @ 0x6B6F38 ----
            FrameStreamCursorLike &root = p.rootStream;
            const int rootCount = streamCount(root.frames);
            const int j = rootCount - 2;
            while(root.frameCursor < j) {                      // 0x6B6F48
                if(state.clampedEvalTime < root.nextTime)      // 0x6B6F70
                    break;
                root.frameCursor = root.frameCursor + 1;       // 0x6B6F78
                // 0x6B7034: content snapshot = frames[cursor].content (+616).
                auto cf = frameDictAt(root.frames, root.frameCursor);
                root.rootContent = frameContent(cf);
                root.curTime = root.nextTime;                  // 0x6B7044
                // 0x70E4: nextTime = frames[cursor+1].time.
                auto nf = frameDictAt(root.frames, root.frameCursor + 1);
                root.nextTime = frameTime(nf);
            }

            // ---- Variable-track deque (player+1312..1368) ----
            // DEFERRED (PLATFORM_BOUNDARY): opaque 160-byte track records seeded
            // via sub_6B786C/sub_6B7A70. Skipped.

            // ---- Node-deque walk @ 0x6B7364 (LABEL_86) ----
            // For each node (index 1..N): if node+8 child non-null -> advanceNode;
            // else inline forward seek (toward clampedEvalTime), merge, findSource.
            for(std::size_t n = 1; n < p.nodes.size(); ++n) {
                NodeFrameStreamsLike &node = p.nodes[n];
                if(node.hasChild) {                            // 0x6B73B4
                    advanceNodeFramesLike_0x6B7E44(node, state);
                    continue;
                }
                // ---- inline per-node forward seek @ 0x6B7440 ----
                const int count = streamCount(node.frameList); // sub_56C694
                int slot_idx = node.activeSlotIndex;           // node+1392
                ParsedFrameSlotLike_0x6926B4 *cur = &node.slots[slot_idx];
                ParsedFrameSlotLike_0x6926B4 *other = &node.slots[slot_idx ^ 1];
                const int limit = count - 2;                   // v46
                bool seeked = false;                           // v47
                // Binary compares slot+0 as a SIGNED int (`*(_DWORD*)v45 <
                // v46`); cur->frameIndex is uint32 here, so cast to int to avoid
                // unsigned promotion of the negative `limit` (count-2 == -2 for an
                // empty stream would otherwise become 4294967294 and loop).
                if(static_cast<int>(cur->frameIndex) < limit) {  // 0x6B745C
                    for(;;) {
                        ParsedFrameSlotLike_0x6926B4 *v49 = cur;
                        if(state.clampedEvalTime < other->time) // 0x6B7484
                            break;
                        node.activeSlotIndex ^= 1;              // 0x6B7494
                        // 0x6B74A8: parseFrame(cur, frameList, other.frameIndex+1).
                        parseFrameLike_0x6926B4(
                            *cur,
                            frameDictAt(node.frameList, other->frameIndex + 1),
                            static_cast<std::uint32_t>(other->frameIndex + 1),
                            node.nodeType);
                        // 0x6B74B0: if (other.mask & 0x40000) action dispatch.
                        if((other->mask & 0x40000) != 0) {
                            // sub_6B638C(player, frame, &other.act) — DEFERRED.
                        }
                        seeked = true;                          // 0x6B74F4
                        const bool more =
                            static_cast<int>(other->frameIndex) < limit; // 0x6B74F8
                        cur = other;                            // v45 = v48
                        other = v49;                            // v48 = v49
                        if(!more)
                            break;
                    }
                    if(seeked) {
                        // Merge block @ 0x6B72BC (LABEL_88).
                        if(!node.slots[0].mergedFlag)
                            mergeFrameContentLike_0x692AB0(
                                node.slots[0], node.nodeType, slotContent(node, node.slots[0]));
                        if(!node.slots[1].mergedFlag)
                            mergeFrameContentLike_0x692AB0(
                                node.slots[1], node.nodeType, slotContent(node, node.slots[1]));
                        if(findSourceGate(node, state.emoteListFlag))
                            findSourceLike(node);
                    }
                }
            }
        }

        // ==================================================================
        // Player_rewindRootAndNodes @ 0x6B9A3C
        // ------------------------------------------------------------------
        // Backward analogue of advanceRootAndNodes.
        // ------------------------------------------------------------------
        void rewindRootAndNodesLike_0x6B9A3C(PlayerFrameStreamsLike &p) {
            TimelineSeekStateLike &state = p.state;
            FrameStreamCursorLike &layer = p.layerStream;

            // ---- Layer stream backward rewind @ 0x6B9AE8 ----
            // while (count != 0 && layer.curTime > clampedEvalTime) { ... }
            if(streamCount(layer.frames) != 0 &&
               layer.curTime > state.clampedEvalTime) {
                do {
                    layer.frameCursor = layer.frameCursor - 1; // 0x6B9B24
                    // 0x6B9BBC: curTime = frames[cursor].time.
                    auto cf = frameDictAt(layer.frames, layer.frameCursor);
                    layer.curTime = frameTime(cf);
                    // 0x6B9C60: nextTime = frames[cursor+1].time.
                    auto nf = frameDictAt(layer.frames, layer.frameCursor + 1);
                    layer.nextTime = frameTime(nf);
                    // 0x6B9C80: if frames[cursor].type == 1 -> action gate.
                    if(frameType(cf) == 1) {
                        applyLayerActionGate(state, layer.curTime,
                                             frameContent(cf));
                    }
                } while(layer.curTime > state.clampedEvalTime); // 0x6B9E28
            }

            // ---- Root stream backward rewind @ 0x6B9E84 ----
            FrameStreamCursorLike &root = p.rootStream;
            if(root.curTime > state.clampedEvalTime) {
                double t;
                do {
                    root.frameCursor = root.frameCursor - 1;   // 0x6B9EA8
                    // 0x6B9F6C: content snapshot = frames[cursor].content (+616).
                    auto cf = frameDictAt(root.frames, root.frameCursor);
                    root.rootContent = frameContent(cf);
                    root.nextTime = root.curTime;              // 0x6B9F7C: +584=+576
                    t = frameTime(cf);                         // 0x6B9F94
                    root.curTime = t;                          // 0x6B9F98: +576
                } while(t > state.clampedEvalTime);            // 0x6B9FC4
            }

            // ---- Variable-track deque DEFERRED (same as advance). ----

            // ---- Node-deque walk @ 0x6BA158 ----
            for(std::size_t n = 1; n < p.nodes.size(); ++n) {
                NodeFrameStreamsLike &node = p.nodes[n];
                if(node.hasChild) {                            // 0x6BA1A8
                    advanceNodeFramesLike_0x6B7E44(node, state);
                    continue;
                }
                // ---- inline per-node backward seek @ 0x6BA1CC ----
                int slot_idx = node.activeSlotIndex;           // node+1392
                // 0x6BA1D4: gate on active slot's +328 clipStartTime.
                if(node.slots[slot_idx].clipStartTime > state.clampedEvalTime) {
                    ParsedFrameSlotLike_0x6926B4 *cur = &node.slots[slot_idx];
                    ParsedFrameSlotLike_0x6926B4 *other =
                        &node.slots[slot_idx ^ 1];
                    int v28 = slot_idx;
                    for(;;) {
                        node.activeSlotIndex = (v28 & 1) == 0 ? 1 : 0; // 0x6BA21C
                        // 0x6BA234: parseFrame(other, frameList, cur.frameIndex-1).
                        parseFrameLike_0x6926B4(
                            *other,
                            frameDictAt(node.frameList, cur->frameIndex - 1),
                            static_cast<std::uint32_t>(cur->frameIndex - 1),
                            node.nodeType);
                        // 0x6BA23C: if (other.mask & 0x40000) action dispatch.
                        if((other->mask & 0x40000) != 0) {
                            // sub_6B638C(player, frame, &other.act) — DEFERRED.
                        }
                        if(other->time <= state.clampedEvalTime) // 0x6BA284
                            break;
                        // 0x6BA208: swap, continue.
                        v28 = node.activeSlotIndex;
                        ParsedFrameSlotLike_0x6926B4 *tmp = cur;
                        cur = other;
                        other = tmp;
                    }
                    // Merge block @ 0x6BA288.
                    if(!node.slots[0].mergedFlag)
                        mergeFrameContentLike_0x692AB0(
                            node.slots[0], node.nodeType, slotContent(node, node.slots[0]));
                    if(!node.slots[1].mergedFlag)
                        mergeFrameContentLike_0x692AB0(
                            node.slots[1], node.nodeType, slotContent(node, node.slots[1]));
                    if(findSourceGate(node, state.emoteListFlag))
                        findSourceLike(node);
                }
            }
        }

        // ==================================================================
        // Player_reseekTimelineCursors @ 0x6B86C8
        // ------------------------------------------------------------------
        // Full re-seek of the layer + root streams to clampedEvalTime via a
        // fresh linear scan, with action/sync/align gating, then per-node
        // advance. (Variable-track deque + aux list + per-node init DEFERRED.)
        // ------------------------------------------------------------------
        void reseekTimelineCursorsLike_0x6B86C8(PlayerFrameStreamsLike &p) {
            TimelineSeekStateLike &state = p.state;

            // ---- Layer stream re-seek @ 0x6B8760 ----
            FrameStreamCursorLike &layer = p.layerStream;
            const int layerCount = streamCount(layer.frames);
            if(layerCount >= 1) {
                // 0x6B8770: linear scan i: 0..count-1 until frames[i].time
                // brackets clampedEvalTime. v8: continue flag.
                int i = 0;
                for(; i < layerCount; ++i) {
                    auto cf = frameDictAt(layer.frames, i);
                    const double v6 = frameTime(cf);           // 0x6B8810
                    const double v7 = state.clampedEvalTime;   // 0x6B8814
                    int v8;
                    if(v6 <= v7) {                             // 0x6B881C
                        if(v6 < v7) {                          // 0x6B882C
                            ++i;                               // 0x6B8830
                            v8 = 1;
                        } else {
                            v8 = 0;
                        }
                    } else {
                        v8 = 0;                                // 0x6B8820
                        --i;                                   // 0x6B8824
                    }
                    if(!v8)                                    // 0x6B885C
                        break;
                }
                // 0x6B8874: cursor = min(i, count-2).
                layer.frameCursor = (layerCount - 2 >= i) ? i : (layerCount - 2);
                // 0x6B891C: curTime = (double)(int)frames[cursor].time.
                auto curF = frameDictAt(layer.frames, layer.frameCursor);
                layer.curTime = static_cast<double>(
                    static_cast<int>(frameTime(curF)));
                // 0x6B89D0: nextTime = frames[cursor+1].time.
                auto nextF = frameDictAt(layer.frames, layer.frameCursor + 1);
                const int v13 = static_cast<int>(frameTime(nextF));
                layer.nextTime = static_cast<double>(v13);
                // 砖6/Stage B 修正: the precise-frame gate keys on the CURSOR
                // frame, NOT frames[cursor+1]. Byte-verified at 0x6B89D4..0x6B8A28:
                //   D0 = +456 (clampedEvalTime); D1 = +920 (curTime);
                //   6B89D4 FCMP D0,D1; B.NE skip      -> gate: +456 == curTime
                //   6B89DC propGetInt(&var_C0,"type") -> var_C0 = v99 bound to v100
                //   6B8A28 (*(v100+32))(v100,"content") -> content of v100
                // where v100 = PropGetByNum(+916) (0x6B879C) = the CURSOR frame.
                // The earlier port read frames[cursor+1] (nextF) for both type and
                // content, which is wrong: nextF only supplies +928 (nextTime).
                if(layer.curTime == state.clampedEvalTime &&
                   frameType(curF) == 1) {
                    applyLayerActionGate(state, layer.curTime,
                                         frameContent(curF));
                }
            }

            // ---- Root stream re-seek @ 0x6B8C1C ----
            FrameStreamCursorLike &root = p.rootStream;
            const int rootCount = streamCount(root.frames);
            if(rootCount) {
                int j = 0;
                if(rootCount >= 1) {                           // 0x6B8C28
                    for(; j < rootCount; ++j) {
                        auto cf = frameDictAt(root.frames, j);
                        const double v23 = frameTime(cf);      // 0x6B8CD0
                        const double v24 = state.clampedEvalTime;
                        int v25;
                        if(v24 == v23) {                       // 0x6B8CDC
                            v25 = 0;
                        } else if(v23 <= v24) {                // 0x6B8CEC
                            v25 = 1;
                        } else {
                            v25 = 0;                           // 0x6B8CF0
                            --j;
                        }
                        if(!v25)                               // 0x6B8D1C
                            break;
                    }
                    root.frameCursor = j;                      // 0x6B8D44
                } else {
                    j = root.frameCursor;                      // 0x6B8D30
                }
            }
            // 0x6B8D50: cursor = min(cursor, count-2).
            {
                int j = root.frameCursor;
                root.frameCursor = (j >= rootCount - 2) ? (rootCount - 2) : j;
            }
            // 0x6B8E20: content snapshot = frames[cursor].content (+616).
            {
                auto cf = frameDictAt(root.frames, root.frameCursor);
                root.rootContent = frameContent(cf);
                // 0x6B8E48: curTime = frames[cursor].time.
                root.curTime = frameTime(cf);
                // 0x6B8F08: nextTime = frames[cursor+1].time.
                auto nf = frameDictAt(root.frames, root.frameCursor + 1);
                root.nextTime = frameTime(nf);
            }

            // ---- Variable-track deque re-seed (player+1312..1368) ----
            // DEFERRED (PLATFORM_BOUNDARY): per-track 2-slot seed via
            // sub_6B786C/sub_6B7A70 over opaque 160-byte track records.

            // ---- Per-node advance @ 0x6B91B0 ----
            // Player_initNodeTimeline_guess(player, node) per node — modelled as
            // the same per-node advance the forward path runs.
            for(std::size_t n = 1; n < p.nodes.size(); ++n) {
                NodeFrameStreamsLike &node = p.nodes[n];
                if(node.hasChild) {
                    advanceNodeFramesLike_0x6B7E44(node, state);
                }
                // Non-child nodes: Player_initNodeTimeline_guess body DEFERRED
                // (the binary seeds the node's slot cursors against
                // clampedEvalTime; reproduced for child nodes via advanceNode,
                // deferred for the inline init path).
            }

            // ---- Player_pruneHM3_byNodeIdentity + +280 aux list ----
            // DEFERRED (opaque): HM3 prune and the sub_6B9650 aux-list pass.
        }

    } // namespace detail
} // namespace motion
