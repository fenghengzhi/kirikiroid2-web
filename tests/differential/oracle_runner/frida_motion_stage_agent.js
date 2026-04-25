// Frida agent for motion_playback staged Android oracle diagnostics.
//
// This is deliberately separate from frida_motion_agent.js. The existing
// agent records the final per-frame layer oracle; this one records a broader
// diagnostic stream split into six stages so engine divergences can be
// localized before changing port code.

'use strict';

const PLAYER_PROGRESS_COMPAT_OFF = 0x6D2A98;
const PLAYER_INIT_NON_EMOTE_OFF  = 0x6B365C;
const PLAYER_PARSE_PARAM_OFF     = 0x6B1718;
const PLAYER_PARSE_PARAM_LIST_OFF = 0x6B202C;
const PLAYER_BIND_PARAM_OFF      = 0x6C4668;
const PLAYER_EVALUATE_TIMELINE_OFF = 0x699AE4;
const PLAYER_SUB_MOTION_OFF      = 0x6BE0C0;
const PLAYER_PHASE3_LAST_OFF     = 0x6C0528;

const STATIC_PARSE_PROJECTION = 'static_parse-semantic-v1';
const STATIC_PARSE_SAMPLE_POINTS = {
    init_non_emote_enter: 'initNonEmoteMotionLike_0x6B365C.enter',
    init_non_emote_leave: 'initNonEmoteMotionLike_0x6B365C.leave',
    parse_parameter_enter: 'appendParameterEntryLike_0x6B1718.enter',
    parse_parameter_leave: 'appendParameterEntryLike_0x6B1718.leave',
    parse_parameter_list_enter: 'parseParameterListLike_0x6B202C.enter',
    parse_parameter_list_leave: 'parseParameterListLike_0x6B202C.leave',
};

const TRACE_FLATTEN_PROJECTION = 'trace_flatten-semantic-v1';
const TRACE_FLATTEN_SAMPLE_POINT = 'progressCompat.phase3-end.pre-cleanup';

const NODE_STRIDE = 2632;
const PARAM_ENTRY_STRIDE = 56;

const STAGE_STATIC_PARSE = 'static_parse';
const STAGE_INIT_MOTION = 'init_motion';
const STAGE_VARIABLE_BINDING = 'variable_binding';
const STAGE_FRAME_SELECTION = 'frame_selection';
const STAGE_SUB_MOTION_DECISION = 'sub_motion_decision';
const STAGE_TRACE_FLATTEN = 'trace_flatten';

const ALL_STAGES = [
    STAGE_STATIC_PARSE,
    STAGE_INIT_MOTION,
    STAGE_VARIABLE_BINDING,
    STAGE_FRAME_SELECTION,
    STAGE_SUB_MOTION_DECISION,
    STAGE_TRACE_FLATTEN,
];

const NODE_OFF = {
    parameterEntry: 8,
    coordinateMode: 24,
    nodeType: 28,
    parentIndex: 36,
    flags: 44,
    activeSlot: 1392,
    active: 1505,
    visible: 1506,
    flipX: 1507,
    flipY: 1508,
    posX: 1512,
    posY: 1520,
    posZ: 1528,
    angleDeg: 1536,
    scaleX: 1544,
    scaleY: 1552,
    slantX: 1560,
    slantY: 1568,
    opacity: 1576,
    stencilType: 52,
};

const PARAM_OFF = {
    id: 0,
    discretization: 20,
    rangeBegin: 24,
    rangeEnd: 32,
    value: 40,
    mode: 48,
};

let base = null;
let hooked = false;
let recording = false;
let events = [];
let frameCounter = 0;
let seqCounter = 0;
let startTimeMs = 0;
let enabledStages = new Set(ALL_STAGES);

let inCompat = false;
let samplesInFrame = [];
let capturedObjthis = null;
let currentFrameId = null;

function ensureBase() {
    if (base !== null) return base;
    base = Module.findBaseAddress('libkrkr2.so');
    if (base === null) {
        throw new Error('libkrkr2.so not loaded in target process');
    }
    return base;
}

function hexOff(offset) {
    return '0x' + offset.toString(16).toUpperCase();
}

function attachAt(offset, name, callbacks) {
    try {
        Interceptor.attach(ensureBase().add(offset), callbacks);
    } catch (e) {
        throw new Error(
            'failed to hook ' + name + ' at libkrkr2.so+' +
            hexOff(offset) + ': ' + e
        );
    }
}

function stageEnabled(stage) {
    return enabledStages.has(stage);
}

function ptrHex(value) {
    if (value === null || value === undefined) return null;
    try {
        const p = ptr(value);
        if (p.isNull()) return null;
        return p.toString();
    } catch (e) {
        return String(value);
    }
}

function ptrToNumber(value) {
    try {
        return parseInt(ptr(value).toString(), 16);
    } catch (e) {
        return 0;
    }
}

function readS32(p, off) {
    try { return p.add(off).readS32(); } catch (e) { return null; }
}

function readU8(p, off) {
    try { return p.add(off).readU8(); } catch (e) { return null; }
}

function readBool(p, off) {
    const v = readU8(p, off);
    return v === null ? null : v !== 0;
}

function readDouble(p, off) {
    try {
        const v = p.add(off).readDouble();
        return Number.isFinite(v) ? v : null;
    } catch (e) {
        return null;
    }
}

function readPointer(p, off) {
    try {
        const q = p.add(off).readPointer();
        return q.isNull() ? null : q;
    } catch (e) {
        return null;
    }
}

function readArgInt(arg) {
    try { return arg.toInt32(); } catch (e) {}
    try { return parseInt(arg.toString(), 16) | 0; } catch (e) {}
    return null;
}

function readD0(ctx) {
    try {
        const raw = ctx.d0.toString();
        if (raw.indexOf('0x') === 0 || raw.indexOf('0X') === 0) return null;
        const v = parseFloat(raw);
        return Number.isFinite(v) ? v : null;
    } catch (e) {
        return null;
    }
}

function readD0Raw(ctx) {
    try { return ctx.d0.toString(); } catch (e) { return null; }
}

function emit(stage, kind, payload) {
    if (!recording || !stageEnabled(stage)) return;
    const ev = payload || {};
    ev.schema = 'motion-stage-oracle-v1-event';
    ev.stage = stage;
    ev.kind = kind;
    ev.seq = seqCounter++;
    ev.timeMs = Date.now() - startTimeMs;
    if (inCompat) {
        ev.frameId = currentFrameId;
        if (stage !== STAGE_TRACE_FLATTEN) {
            ev.objthis = ptrHex(capturedObjthis);
        }
    }
    events.push(ev);
}

function emitStaticParse(kind, semanticPayload, diagnostics) {
    if (!recording || !stageEnabled(STAGE_STATIC_PARSE)) return;
    const diag = diagnostics || {};
    if (inCompat) {
        diag.frameId = currentFrameId;
        diag.objthis = ptrHex(capturedObjthis);
    }
    const ev = semanticPayload || {};
    ev.schema = 'motion-stage-oracle-v1-event';
    ev.stage = STAGE_STATIC_PARSE;
    ev.kind = kind;
    ev.projection = STATIC_PARSE_PROJECTION;
    ev.samplePoint = STATIC_PARSE_SAMPLE_POINTS[kind] || kind;
    ev.diagnostics = diag;
    ev.seq = seqCounter++;
    ev.timeMs = Date.now() - startTimeMs;
    events.push(ev);
}

function safeUtf16(ptrValue, length) {
    try {
        if (!ptrValue || ptrValue.isNull()) return null;
        if (length !== undefined && length !== null) {
            if (length < 0 || length > 2048) return null;
            return ptrValue.readUtf16String(length);
        }
        return ptrValue.readUtf16String();
    } catch (e) {
        return null;
    }
}

function readVariantString(vstr) {
    if (!vstr || vstr.isNull()) return '';

    const candidates = [
        { lenOff: 60, longOff: 8, shortOff: 16 },
        { lenOff: 64, longOff: 8, shortOff: 16 },
        { lenOff: 56, longOff: 8, shortOff: 16 },
        { lenOff: 52, longOff: 4, shortOff: 12 },
    ];

    for (const c of candidates) {
        const len = readS32(vstr, c.lenOff);
        if (len === null || len < 0 || len > 2048) continue;
        const longPtr = readPointer(vstr, c.longOff);
        if (longPtr !== null && len > 21) {
            const s = safeUtf16(longPtr, len);
            if (s !== null) return s;
        }
        const s = safeUtf16(vstr.add(c.shortOff), len);
        if (s !== null) return s;
    }
    return null;
}

function readTtstr(ttstrPtr) {
    try {
        const p = ptr(ttstrPtr);
        if (p.isNull()) return '';
        const vstr = p.readPointer();
        return readVariantString(vstr);
    } catch (e) {
        return null;
    }
}

function readParameterEntry(entryPtr, index) {
    const p = ptr(entryPtr);
    const idPtr = p.add(PARAM_OFF.id);
    return {
        index: index,
        ptr: ptrHex(p),
        idPtr: ptrHex(idPtr),
        id: readTtstr(idPtr),
        discretization: readS32(p, PARAM_OFF.discretization),
        rangeBegin: readDouble(p, PARAM_OFF.rangeBegin),
        rangeEnd: readDouble(p, PARAM_OFF.rangeEnd),
        value: readDouble(p, PARAM_OFF.value),
        mode: readS32(p, PARAM_OFF.mode),
    };
}

function readParameterTable(playerPtr) {
    const player = ptr(playerPtr);
    const begin = readPointer(player, 384);
    const end = readPointer(player, 392);
    const out = {
        begin: ptrHex(begin),
        end: ptrHex(end),
        stride: PARAM_ENTRY_STRIDE,
        count: 0,
        entries: [],
    };
    if (begin === null || end === null) return out;

    const beginN = ptrToNumber(begin);
    const endN = ptrToNumber(end);
    if (endN < beginN) {
        out.error = 'invalid parameter vector begin/end';
        return out;
    }
    const span = endN - beginN;
    if (span % PARAM_ENTRY_STRIDE !== 0) {
        out.error = 'parameter vector span is not 56-byte aligned';
        out.span = span;
        return out;
    }
    const count = span / PARAM_ENTRY_STRIDE;
    out.count = count;
    if (count > 256) {
        out.error = 'parameter vector unexpectedly large';
        return out;
    }
    for (let i = 0; i < count; i++) {
        out.entries.push(readParameterEntry(begin.add(i * PARAM_ENTRY_STRIDE), i));
    }
    return out;
}

function semanticParameterTable(raw) {
    const entries = raw && raw.entries ? raw.entries : [];
    return {
        count: raw && typeof raw.count === 'number' ? raw.count : entries.length,
        entries: entries.map((entry) => ({
            index: entry.index,
            id: entry.id,
            discretization: entry.discretization !== 0,
            rangeBegin: entry.rangeBegin,
            rangeEnd: entry.rangeEnd,
            value: entry.value,
            mode: entry.mode,
        })),
    };
}

function parameterTableDiagnostics(raw) {
    const diag = {
        begin: raw ? raw.begin : null,
        end: raw ? raw.end : null,
        stride: raw ? raw.stride : null,
    };
    if (raw && raw.error) diag.error = raw.error;
    if (raw && raw.span !== undefined) diag.span = raw.span;
    if (raw && raw.entries && raw.entries.length > 0) {
        diag.entries = raw.entries.map((entry) => ({
            index: entry.index,
            ptr: entry.ptr,
            idPtr: entry.idPtr,
        }));
    }
    return diag;
}

function parameterTableChanges(before, after) {
    const changes = [];
    const b = before && before.entries ? before.entries : [];
    const a = after && after.entries ? after.entries : [];
    const n = Math.max(b.length, a.length);
    for (let i = 0; i < n; i++) {
        const bi = b[i] || null;
        const ai = a[i] || null;
        if (bi === null || ai === null) {
            changes.push({ index: i, before: bi, after: ai, reason: 'entry_added_or_removed' });
            continue;
        }
        if (bi.mode !== ai.mode || bi.value !== ai.value) {
            changes.push({
                index: i,
                id: ai.id !== null ? ai.id : bi.id,
                beforeMode: bi.mode,
                afterMode: ai.mode,
                beforeValue: bi.value,
                afterValue: ai.value,
            });
        }
    }
    return changes;
}

function walkDeque(playerPtr, stride) {
    const player = ptr(playerPtr);
    const startCurPtr = player.add(200).readPointer();
    const startFirstPtr = player.add(208).readPointer();
    const startLastPtr = player.add(216).readPointer();
    const startNodePtr = player.add(224).readPointer();
    const finishCurPtr = player.add(232).readPointer();
    const finishNodePtr = player.add(256).readPointer();

    const startCur = ptrToNumber(startCurPtr);
    const startFirst = ptrToNumber(startFirstPtr);
    const startLast = ptrToNumber(startLastPtr);
    const startNode = ptrToNumber(startNodePtr);
    const finishCur = ptrToNumber(finishCurPtr);
    const finishNode = ptrToNumber(finishNodePtr);

    if (startCur === 0 || finishCur === 0) return null;
    if (startNode === 0 || finishNode === 0) return null;
    if (finishNode < startNode) return null;

    let bufElems = 1;
    if (startLast > startFirst) {
        const span = startLast - startFirst;
        if (span % stride === 0 && span > 0) {
            bufElems = Math.floor(span / stride);
        }
    }
    if (bufElems < 1) bufElems = 1;

    const nodes = [];
    let mapIter = startNodePtr;
    let safety = 0;
    while (ptrToNumber(mapIter) <= ptrToNumber(finishNodePtr)) {
        if (++safety > 4096) break;
        const blockPtr = mapIter.readPointer();
        const blockRaw = ptrToNumber(blockPtr);
        if (blockRaw === 0) break;
        let first = 0;
        let last = bufElems;
        if (ptrToNumber(mapIter) === startNode) {
            first = Math.floor((startCur - blockRaw) / stride);
        }
        if (ptrToNumber(mapIter) === finishNode) {
            last = Math.floor((finishCur - blockRaw) / stride);
        }
        for (let k = first; k < last; k++) {
            nodes.push(blockPtr.add(k * stride));
        }
        mapIter = mapIter.add(8);
    }
    return nodes;
}

function readNodeAccum(nodePtr) {
    const node = ptr(nodePtr);
    return {
        nodeType: readS32(node, NODE_OFF.nodeType),
        active: readBool(node, NODE_OFF.active),
        visible: readBool(node, NODE_OFF.visible),
        flipX: readBool(node, NODE_OFF.flipX),
        flipY: readBool(node, NODE_OFF.flipY),
        posX: readDouble(node, NODE_OFF.posX),
        posY: readDouble(node, NODE_OFF.posY),
        posZ: readDouble(node, NODE_OFF.posZ),
        angleDeg: readDouble(node, NODE_OFF.angleDeg),
        scaleX: readDouble(node, NODE_OFF.scaleX),
        scaleY: readDouble(node, NODE_OFF.scaleY),
        slantX: readDouble(node, NODE_OFF.slantX),
        slantY: readDouble(node, NODE_OFF.slantY),
        opacity: readS32(node, NODE_OFF.opacity),
        stencilType: readS32(node, NODE_OFF.stencilType),
    };
}

function readNodeBrief(nodePtr, index) {
    const node = ptr(nodePtr);
    const paramEntry = readPointer(node, NODE_OFF.parameterEntry);
    let param = null;
    if (paramEntry !== null) {
        param = {
            ptr: ptrHex(paramEntry),
            mode: readS32(paramEntry, PARAM_OFF.mode),
            value: readDouble(paramEntry, PARAM_OFF.value),
            id: readTtstr(paramEntry.add(PARAM_OFF.id)),
        };
    }
    return {
        index: index,
        ptr: ptrHex(node),
        parameterEntry: ptrHex(paramEntry),
        parameter: param,
        coordinateMode: readS32(node, NODE_OFF.coordinateMode),
        nodeType: readS32(node, NODE_OFF.nodeType),
        parentIndex: readS32(node, NODE_OFF.parentIndex),
        flags: readU8(node, NODE_OFF.flags),
        activeSlot: readS32(node, NODE_OFF.activeSlot),
        active: readBool(node, NODE_OFF.active),
        visible: readBool(node, NODE_OFF.visible),
        opacity: readS32(node, NODE_OFF.opacity),
    };
}

function walkNodes(playerPtr) {
    try {
        const nodes = walkDeque(playerPtr, NODE_STRIDE);
        if (nodes !== null && nodes.length > 0) {
            const layers = [];
            for (let i = 0; i < nodes.length; i++) {
                const accum = readNodeAccum(nodes[i]);
                accum.index = i;
                layers.push(accum);
            }
            return { layout: 'deque', layers: layers, nodeCount: nodes.length };
        }
    } catch (e) {
        return { layout: 'deque-error', error: String(e), layers: [], nodeCount: 0 };
    }
    const rootPtr = readPointer(ptr(playerPtr), 200);
    if (rootPtr === null) return { layout: 'empty', layers: [], nodeCount: 0 };
    const accum = readNodeAccum(rootPtr);
    accum.index = 0;
    return { layout: 'root-only', layers: [accum], nodeCount: 1 };
}

function playerOverview(playerPtr) {
    const player = ptr(playerPtr);
    const walked = walkNodes(player);
    return {
        player: ptrHex(player),
        nodeLayout: walked.layout,
        nodeCount: walked.nodeCount,
        parameterTable: readParameterTable(player),
        playing: readBool(player, 1099),
        currentTime: readDouble(player, 456),
        frameTickCount: readDouble(player, 1120),
        frameLastTime: readDouble(player, 1128),
    };
}

function snapshotMotionSubNodes(playerPtr) {
    const nodes = walkDeque(playerPtr, NODE_STRIDE);
    const out = [];
    if (nodes === null) return out;
    for (let i = 0; i < nodes.length; i++) {
        const nodeType = readS32(nodes[i], NODE_OFF.nodeType);
        if (nodeType === 3) {
            out.push(readNodeBrief(nodes[i], i));
        }
    }
    return out;
}

function snapshotEvalNode(nodePtr) {
    return readNodeBrief(ptr(nodePtr), -1);
}

function classifySubMotion(beforeNode, afterNode, childSampleDelta) {
    const node = afterNode || beforeNode || {};
    const param = node.parameter || {};
    const mode = param.mode === null || param.mode === undefined ? 0 : param.mode;
    const flags = node.flags === null || node.flags === undefined ? 0 : node.flags;
    if (childSampleDelta > 0) return 'play_or_update_child';
    if (((mode & 5) !== 0) || flags !== 0) return 'gate_open_no_child_sample';
    if (node.visible === false && mode === 0) return 'skip_invisible';
    return 'skip_gate_closed';
}

function compareMotionSubSnapshots(before, after, childSampleDelta) {
    const out = [];
    const byIndex = {};
    for (const b of before || []) byIndex[b.index] = { before: b, after: null };
    for (const a of after || []) {
        if (!byIndex[a.index]) byIndex[a.index] = { before: null, after: a };
        else byIndex[a.index].after = a;
    }
    Object.keys(byIndex).sort((a, b) => Number(a) - Number(b)).forEach((k) => {
        const item = byIndex[k];
        out.push({
            index: Number(k),
            before: item.before,
            after: item.after,
            decision: classifySubMotion(item.before, item.after, childSampleDelta),
        });
    });
    return out;
}

function currentSamplePlayers() {
    return samplesInFrame.map((s) => s.player.toString());
}

function installHook() {
    if (hooked) return;
    ensureBase();

    attachAt(PLAYER_PROGRESS_COMPAT_OFF, 'Player_progressCompat', {
        onEnter(args) {
            inCompat = true;
            samplesInFrame = [];
            capturedObjthis = args[3];
            currentFrameId = frameCounter;
        },
        onLeave(retval) {
            const objthis = capturedObjthis;
            const samples = samplesInFrame;
            inCompat = false;
            capturedObjthis = null;
            currentFrameId = null;

            if (!recording || !stageEnabled(STAGE_TRACE_FLATTEN)) {
                samplesInFrame = [];
                return;
            }

            const flatLayers = [];
            const diagnosticPlayers = [];
            let layoutTag = 'pre-cleanup';
            let walkError = null;
            for (const sample of samples) {
                const layerStart = flatLayers.length;
                for (const l of sample.layers) {
                    const out = Object.assign({}, l);
                    out.index = flatLayers.length;
                    flatLayers.push(out);
                }
                diagnosticPlayers.push({
                    ptr: sample.player.toString(),
                    layout: sample.layout || null,
                    layerStart: layerStart,
                    layerCount: sample.layers.length,
                    error: sample.error || null,
                });
                if (sample.layout && sample.layout !== 'deque') {
                    layoutTag = sample.layout;
                }
                walkError = walkError || sample.error;
            }
            emit(STAGE_TRACE_FLATTEN, 'frame', {
                projection: TRACE_FLATTEN_PROJECTION,
                samplePoint: TRACE_FLATTEN_SAMPLE_POINT,
                frameId: frameCounter++,
                playerCount: samples.length,
                layers: flatLayers,
                diagnostics: {
                    objthis: objthis ? objthis.toString() : null,
                    topPlayer: samples.length > 0 ? samples[0].player.toString() : null,
                    layout: layoutTag,
                    players: diagnosticPlayers,
                    error: walkError,
                },
            });
            samplesInFrame = [];
        },
    });

    attachAt(PLAYER_PHASE3_LAST_OFF, 'Player_phase3_last', {
        onEnter(args) {
            this.player = args[0];
        },
        onLeave() {
            if (!inCompat || !recording) return;
            const player = this.player;
            try {
                const w = walkNodes(player);
                samplesInFrame.push({
                    player: player,
                    layout: w.layout,
                    layers: w.layers,
                    error: w.error || null,
                });
            } catch (e) {
                samplesInFrame.push({
                    player: player,
                    layout: 'sample-error',
                    layers: [],
                    error: String(e),
                });
            }
        },
    });

    attachAt(PLAYER_INIT_NON_EMOTE_OFF, 'Player_initNonEmoteMotion', {
        onEnter(args) {
            this.player = args[0];
            emitStaticParse('init_non_emote_enter', {}, {
                addr: PLAYER_INIT_NON_EMOTE_OFF,
                player: ptrHex(args[0]),
            });
            emit(STAGE_INIT_MOTION, 'init_non_emote_enter', {
                addr: PLAYER_INIT_NON_EMOTE_OFF,
                player: ptrHex(args[0]),
            });
        },
        onLeave(retval) {
            const overview = playerOverview(this.player);
            const rawParameterTable = overview.parameterTable;
            emitStaticParse('init_non_emote_leave', {
                parameterTable: semanticParameterTable(rawParameterTable),
            }, {
                addr: PLAYER_INIT_NON_EMOTE_OFF,
                retval: ptrHex(retval),
                player: ptrHex(this.player),
                parameterTable: parameterTableDiagnostics(rawParameterTable),
            });
            emit(STAGE_INIT_MOTION, 'init_non_emote_leave', {
                addr: PLAYER_INIT_NON_EMOTE_OFF,
                retval: ptrHex(retval),
                overview: overview,
            });
        },
    });

    attachAt(PLAYER_PARSE_PARAM_OFF, 'parse_motion_parameter', {
        onEnter(args) {
            this.arg0 = args[0];
            this.arg1 = args[1];
            emitStaticParse('parse_parameter_enter', {}, {
                addr: PLAYER_PARSE_PARAM_OFF,
                x0: ptrHex(args[0]),
                x1: ptrHex(args[1]),
            });
        },
        onLeave(retval) {
            emitStaticParse('parse_parameter_leave', {}, {
                addr: PLAYER_PARSE_PARAM_OFF,
                x0: ptrHex(this.arg0),
                x1: ptrHex(this.arg1),
                retval: ptrHex(retval),
            });
        },
    });

    attachAt(PLAYER_PARSE_PARAM_LIST_OFF, 'parse_motion_parameter_list', {
        onEnter(args) {
            this.arg0 = args[0];
            this.arg1 = args[1];
            emitStaticParse('parse_parameter_list_enter', {}, {
                addr: PLAYER_PARSE_PARAM_LIST_OFF,
                x0: ptrHex(args[0]),
                x1: ptrHex(args[1]),
            });
        },
        onLeave(retval) {
            emitStaticParse('parse_parameter_list_leave', {}, {
                addr: PLAYER_PARSE_PARAM_LIST_OFF,
                x0: ptrHex(this.arg0),
                x1: ptrHex(this.arg1),
                retval: ptrHex(retval),
            });
        },
    });

    attachAt(PLAYER_BIND_PARAM_OFF, 'bind_parameter_value', {
        onEnter(args) {
            this.player = args[0];
            this.labelPtr = args[1];
            this.mode = readArgInt(args[2]);
            this.value = readD0(this.context);
            this.valueRaw = readD0Raw(this.context);
            this.before = readParameterTable(args[0]);
            emit(STAGE_VARIABLE_BINDING, 'bind_parameter_enter', {
                addr: PLAYER_BIND_PARAM_OFF,
                player: ptrHex(args[0]),
                labelPtr: ptrHex(args[1]),
                label: readTtstr(args[1]),
                mode: this.mode,
                value: this.value,
                valueRaw: this.valueRaw,
                parameterTableBefore: this.before,
            });
        },
        onLeave(retval) {
            const after = readParameterTable(this.player);
            emit(STAGE_VARIABLE_BINDING, 'bind_parameter_leave', {
                addr: PLAYER_BIND_PARAM_OFF,
                player: ptrHex(this.player),
                labelPtr: ptrHex(this.labelPtr),
                label: readTtstr(this.labelPtr),
                mode: this.mode,
                value: this.value,
                valueRaw: this.valueRaw,
                retval: ptrHex(retval),
                changedEntries: parameterTableChanges(this.before, after),
                parameterTableAfter: after,
            });
        },
    });

    attachAt(PLAYER_EVALUATE_TIMELINE_OFF, 'Player_evaluateTimeline', {
        onEnter(args) {
            this.node = args[0];
            this.dirtyArg = readArgInt(args[1]);
            this.time = readD0(this.context);
            this.timeRaw = readD0Raw(this.context);
            this.before = snapshotEvalNode(args[0]);
        },
        onLeave(retval) {
            emit(STAGE_FRAME_SELECTION, 'evaluate_timeline', {
                addr: PLAYER_EVALUATE_TIMELINE_OFF,
                node: ptrHex(this.node),
                dirtyArg: this.dirtyArg,
                time: this.time,
                timeRaw: this.timeRaw,
                retval: readArgInt(retval),
                before: this.before,
                after: snapshotEvalNode(this.node),
            });
        },
    });

    attachAt(PLAYER_SUB_MOTION_OFF, 'Player_subMotionDecision', {
        onEnter(args) {
            this.player = args[0];
            this.samplesBefore = currentSamplePlayers();
            this.before = snapshotMotionSubNodes(args[0]);
        },
        onLeave(retval) {
            const samplesAfter = currentSamplePlayers();
            const childSampleDelta = Math.max(0, samplesAfter.length - this.samplesBefore.length);
            const after = snapshotMotionSubNodes(this.player);
            emit(STAGE_SUB_MOTION_DECISION, 'sub_motion_decision', {
                addr: PLAYER_SUB_MOTION_OFF,
                player: ptrHex(this.player),
                retval: ptrHex(retval),
                childSamplesBefore: this.samplesBefore,
                childSamplesAfter: samplesAfter,
                childSampleDelta: childSampleDelta,
                decisions: compareMotionSubSnapshots(this.before, after, childSampleDelta),
            });
        },
    });

    hooked = true;
}

rpc.exports = {
    setup() {
        installHook();
        return {
            base: ensureBase().toString(),
            stages: ALL_STAGES,
            nodeStride: NODE_STRIDE,
            parameterEntryStride: PARAM_ENTRY_STRIDE,
            offsets: {
                progressCompat: PLAYER_PROGRESS_COMPAT_OFF,
                initNonEmote: PLAYER_INIT_NON_EMOTE_OFF,
                parseParameter: PLAYER_PARSE_PARAM_OFF,
                parseParameterList: PLAYER_PARSE_PARAM_LIST_OFF,
                bindParameter: PLAYER_BIND_PARAM_OFF,
                evaluateTimeline: PLAYER_EVALUATE_TIMELINE_OFF,
                subMotionDecision: PLAYER_SUB_MOTION_OFF,
                phase3Last: PLAYER_PHASE3_LAST_OFF,
            },
        };
    },
    startRecord(stageNames) {
        const requested = Array.isArray(stageNames) ? stageNames : ALL_STAGES;
        enabledStages = new Set(requested);
        enabledStages.add(STAGE_TRACE_FLATTEN);
        events = [];
        frameCounter = 0;
        seqCounter = 0;
        startTimeMs = Date.now();
        recording = true;
        return true;
    },
    stopRecord() {
        recording = false;
        return events.slice();
    },
    eventCount() {
        return frameCounter;
    },
    rawEventCount() {
        return events.length;
    },
};
