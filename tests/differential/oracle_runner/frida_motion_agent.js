// Frida agent for the motion_playback oracle family.
//
// Unlike `frida_agent.js` (which captures AAPCS64 register snapshots at
// generic call boundaries for the other 4 adapter families), this agent
// captures per-frame per-layer state from libkrkr2's `Motion.Player`
// *during natural playback driven on the cocos2d GL thread* — the host
// never calls Motion.Player methods directly, so there's no thread-
// affinity SIGSEGV risk.
//
// Hook: `Player_progressCompat` @ libkrkr2.so+0x6D2A98 (TJS wrapper
// for `Motion.Player::progress(delta)`). The reason we use this and
// not `Player_updateLayers` (+0x6BB33C): progressCompat is called
// exactly ONCE per TJS-driven frame, from the top-level Player only;
// each Player has a deque of sub-Motion.Player instances whose
// updateLayers ALSO fire per frame, and updateLayers doesn't
// distinguish top-level vs. sub. Hooking progressCompat side-steps
// that — the top-level is whoever TJS's startup.tjs holds the
// reference to, which is also the only Player whose progress()
// bubbles through the TJS call chain. onLeave is safe: progressCompat
// internally runs `progress_inner → updateLayers → calcBounds →
// dispatchEvents`, so by onLeave every Node's accum block
// (node+1505..1576) is coherent.
//
// Host-side (frida_motion_tracer.py) calls over RPC:
//   setup()           -> { base: "0x...", nodeStride: 2632 }
//   startRecord()
//   stopRecord()      -> [{ frameId, player, layers: [...] }, ...]
//   diagDump(playerPtrHex) -> { bytes: [...], strings: {...} }
//
// `diagDump` is an escape hatch used during bring-up to verify the Node
// container layout inside `player+208..256`. Once the walker is confirmed
// the host stops calling it.

'use strict';

const PLAYER_PROGRESS_COMPAT_OFF = 0x6D2A98;  // TJS Motion.Player.progress wrapper
const PLAYER_UPDATE_LAYERS_OFF   = 0x6BB33C;  // void Player_updateLayers(Player*)
const NODE_STRIDE = 2632;

// Node accum-block offsets (confirmed by ida-deep-analyzer; see
// analysis/player_updateLayers_accum.md).
const NODE_OFF = {
    nodeType: 28,       // int32
    active:   1505,     // u8
    visible:  1506,     // u8
    flipX:    1507,     // u8
    flipY:    1508,     // u8
    posX:     1512,     // double
    posY:     1520,     // double
    posZ:     1528,     // double
    angleDeg: 1536,     // double
    scaleX:   1544,     // double
    scaleY:   1552,     // double
    slantX:   1560,     // double
    slantY:   1568,     // double
    opacity:  1576,     // int32
    blendMode: 52,      // int32 (stencilType proxy)
};

let base = null;
let hooked = false;
let recording = false;
let events = [];
let frameCounter = 0;

function ensureBase() {
    if (base !== null) return base;
    base = Module.findBaseAddress('libkrkr2.so');
    if (base === null) {
        throw new Error('libkrkr2.so not loaded in target process');
    }
    return base;
}

// Read a Node's accum fields. Returns plain object matching the oracle
// schema expected by motion_playback_port.cpp (LAYER_FIELDS_*). Fields
// with special encoding (label, currentImage) are filled by the walker
// that owns the labelMap context.
function readNodeAccum(nodePtr) {
    return {
        nodeType: nodePtr.add(NODE_OFF.nodeType).readS32(),
        active:   nodePtr.add(NODE_OFF.active).readU8()  !== 0,
        visible:  nodePtr.add(NODE_OFF.visible).readU8() !== 0,
        flipX:    nodePtr.add(NODE_OFF.flipX).readU8()   !== 0,
        flipY:    nodePtr.add(NODE_OFF.flipY).readU8()   !== 0,
        posX:     nodePtr.add(NODE_OFF.posX).readDouble(),
        posY:     nodePtr.add(NODE_OFF.posY).readDouble(),
        posZ:     nodePtr.add(NODE_OFF.posZ).readDouble(),
        angleDeg: nodePtr.add(NODE_OFF.angleDeg).readDouble(),
        scaleX:   nodePtr.add(NODE_OFF.scaleX).readDouble(),
        scaleY:   nodePtr.add(NODE_OFF.scaleY).readDouble(),
        slantX:   nodePtr.add(NODE_OFF.slantX).readDouble(),
        slantY:   nodePtr.add(NODE_OFF.slantY).readDouble(),
        opacity:  nodePtr.add(NODE_OFF.opacity).readS32(),
        blendMode: nodePtr.add(NODE_OFF.blendMode).readS32(),
    };
}

// libstdc++ std::deque<Node> layout inside Player. The deque header is
// the 48-byte "nodeAllocator/deque" region at player+184..+232 (see
// analysis/Player_Class_Layout_libkrkr2so.md:158). Layout:
//   player+184  _M_map            (Node**)   — map: array of block ptrs
//   player+192  _M_map_size       (size_t)
//   player+200  _M_start._M_cur   (Node*)    — same as rootNodePtr (doc:159)
//   player+208  _M_start._M_first (Node*)
//   player+216  _M_start._M_last  (Node*)
//   player+224  _M_start._M_node  (Node**)   — pointer into map
//   player+232  _M_finish._M_cur  (Node*)
//   player+240  _M_finish._M_first (Node*)
//   player+248  _M_finish._M_last  (Node*)
//   player+256  _M_finish._M_node  (Node**)
//
// libstdc++ block size: __deque_buf_size(n) = n<512 ? 512/n : 1. Node
// is 2632 bytes, so BUF_SIZE = 1 (each map entry holds exactly 1 Node).
// Node count is therefore (finish._M_node - start._M_node), with the
// partial-block offsets usually zero since BUF_SIZE=1 means each block
// is fully occupied.
function walkDeque(playerPtr, stride) {
    const startCurPtr    = playerPtr.add(200).readPointer();
    const startFirstPtr  = playerPtr.add(208).readPointer();
    const startLastPtr   = playerPtr.add(216).readPointer();
    const startNodePtr   = playerPtr.add(224).readPointer();
    const finishCurPtr   = playerPtr.add(232).readPointer();
    const finishFirstPtr = playerPtr.add(240).readPointer();
    const finishNodePtr  = playerPtr.add(256).readPointer();

    const startCur   = parseInt(startCurPtr.toString(), 16);
    const startFirst = parseInt(startFirstPtr.toString(), 16);
    const startLast  = parseInt(startLastPtr.toString(), 16);
    const startNode  = parseInt(startNodePtr.toString(), 16);
    const finishCur  = parseInt(finishCurPtr.toString(), 16);
    const finishNode = parseInt(finishNodePtr.toString(), 16);

    if (startCur === 0 || finishCur === 0) return null;
    if (startNode === 0 || finishNode === 0) return null;
    if (finishNode < startNode) return null;

    // Deque block size in elements. For Node (2632 B) libstdc++ uses 1.
    let bufElems = 1;
    if (startLast > startFirst) {
        const span = startLast - startFirst;
        if (span % stride === 0 && span > 0) {
            bufElems = Math.floor(span / stride);
        }
    }
    if (bufElems < 1) bufElems = 1;

    // Walk the map from start._M_node through finish._M_node, deref each
    // block pointer, emit Nodes at stride offsets within each block.
    const nodes = [];
    let mapIter = startNodePtr;
    let safety = 0;
    while (parseInt(mapIter.toString(), 16) <=
           parseInt(finishNodePtr.toString(), 16)) {
        if (++safety > 4096) break;
        const blockPtr = mapIter.readPointer();
        const blockRaw = parseInt(blockPtr.toString(), 16);
        if (blockRaw === 0) break;
        let first = 0;
        let last = bufElems;
        if (parseInt(mapIter.toString(), 16) === startNode) {
            first = Math.floor((startCur - blockRaw) / stride);
        }
        if (parseInt(mapIter.toString(), 16) === finishNode) {
            last = Math.floor((finishCur - blockRaw) / stride);
        }
        for (let k = first; k < last; k++) {
            nodes.push(blockPtr.add(k * stride));
        }
        mapIter = mapIter.add(8);
    }
    return nodes;
}

// Walk the Player's Node deque and dump each node's accum-block state.
// Top-level Player owns the "player position" root + all animated
// layers from the .mtn timeline (bg, slide, body parts, etc.). Sub-
// Motion.Player children (when the motion uses nested sub-motions) have
// their own deque — walked separately via a second updateLayers hit in
// the same progressCompat window.
function walkNodes(playerPtr) {
    try {
        const nodes = walkDeque(playerPtr, NODE_STRIDE);
        if (nodes !== null && nodes.length > 0) {
            const layers = [];
            for (let i = 0; i < nodes.length; i++) {
                const accum = readNodeAccum(nodes[i]);
                accum.index = i;
                accum.label = '';
                accum.currentImage = '';
                layers.push(accum);
            }
            return { layout: 'deque', layers: layers };
        }
    } catch (e) {
        return { layout: 'deque-error', error: String(e), layers: [] };
    }
    // Fallback: single root node at player+200.
    const rootPtr = playerPtr.add(200).readPointer();
    const rootN = parseInt(rootPtr.toString(), 16);
    if (rootN === 0) return { layout: 'empty', layers: [] };
    const accum = readNodeAccum(rootPtr);
    accum.index = 0;
    accum.label = '';
    accum.currentImage = '';
    return { layout: 'root-only', layers: [accum] };
}

// Coordination between the two hooks. `inCompat` is true while we're
// inside a progressCompat invocation (i.e. TJS just called
// Motion.Player.progress on a top-level Player). During that window,
// Player_updateLayers fires once for the top-level Player (which only
// owns the "player position" root node) and once for each sub-
// Motion.Player child (which owns the actual animated body-part layers
// from the .mtn timeline). We record EVERY player seen inside the
// window — at progressCompat onLeave we walk all of them and flatten
// their nodes into a single per-frame layer list, matching the schema
// motion_playback_port.cpp produces.
let inCompat = false;
let playersInFrame = [];
let capturedObjthis = null;

function installHook() {
    if (hooked) return;
    const compatAddr = ensureBase().add(PLAYER_PROGRESS_COMPAT_OFF);
    const updateAddr = ensureBase().add(PLAYER_UPDATE_LAYERS_OFF);

    // progressCompat is NCB's TJS methodCompat thunk; signature is
    //   methodCompat(tTJSVariant *result,
    //                tjs_int numparams,
    //                tTJSVariant **params,
    //                iTJSDispatch2 *objthis)
    // x0=result (often NULL), x3=objthis. Use objthis as the stable
    // per-Player-TJS-object identity for case segmentation — it maps
    // 1:1 with the Motion.Player TJS instance from startup.tjs.
    Interceptor.attach(compatAddr, {
        onEnter(args) {
            inCompat = true;
            playersInFrame = [];
            capturedObjthis = args[3];
        },
        onLeave(retval) {
            inCompat = false;
            if (!recording) {
                playersInFrame = [];
                capturedObjthis = null;
                return;
            }
            const objthis = capturedObjthis;
            const players = playersInFrame;
            playersInFrame = [];
            capturedObjthis = null;

            // Walk each observed Player's node deque and concatenate
            // into a flat layer list. Each layer keeps its source
            // player pointer in case the host adapter wants to cross-
            // check, but the primary index is global (order of update).
            const flatLayers = [];
            let layoutTag = 'ok';
            let walkError = null;
            for (const p of players) {
                try {
                    const w = walkNodes(p);
                    for (const l of w.layers) {
                        l.sourcePlayer = p.toString();
                        l.index = flatLayers.length;
                        flatLayers.push(l);
                    }
                    if (w.layout !== 'vec@208') layoutTag = w.layout;
                } catch (e) {
                    walkError = walkError || String(e);
                }
            }
            events.push({
                frameId: frameCounter++,
                objthis: objthis ? objthis.toString() : null,
                topPlayer: players.length > 0 ? players[0].toString() : null,
                playerCount: players.length,
                layout: layoutTag,
                layers: flatLayers,
                error: walkError,
            });
        },
    });

    // Player_updateLayers(Player *this_) — the accum-state producer.
    // Fires once per Player per frame. Within a progressCompat window
    // we expect 1 + N hits: one for the top-level Player and one for
    // each sub-Motion.Player child owned by it. We record each hit's
    // Player* — the onLeave of progressCompat then walks all of them.
    Interceptor.attach(updateAddr, {
        onEnter(args) {
            if (!inCompat) return;
            playersInFrame.push(args[0]);
        },
    });
    hooked = true;
}

rpc.exports = {
    setup() {
        installHook();
        return {
            base: ensureBase().toString(),
            nodeStride: NODE_STRIDE,
            hookOffset: PLAYER_PROGRESS_COMPAT_OFF,
        };
    },
    startRecord() {
        events = [];
        frameCounter = 0;
        recording = true;
        return true;
    },
    stopRecord() {
        recording = false;
        return events.slice();
    },
    eventCount() {
        return events.length;
    },
    // Escape hatch for Step-A bring-up: dump 256 bytes starting at
    // playerPtr so the host can inspect container layout without re-
    // deploying the agent.
    diagDump(playerPtrHex) {
        const p = ptr(playerPtrHex);
        const bytes = p.readByteArray(256);
        // Also read a few candidate vector-header interpretations.
        const at = (off) => {
            try { return p.add(off).readPointer().toString(); }
            catch (e) { return '<unreadable>'; }
        };
        return {
            bytes: Array.from(new Uint8Array(bytes)),
            probes: {
                rootNode_at_200: at(200),
                vec208_start: at(208),
                vec208_finish: at(216),
                vec208_end: at(224),
                deque208_map: at(208),
                deque208_mapsize: p.add(216).readU64().toString(),
                deque208_start_cur: at(224),
            },
        };
    },
};
