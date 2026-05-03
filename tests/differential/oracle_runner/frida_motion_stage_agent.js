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
const PLAYER_DRAW_COMPAT_OFF     = 0x6D5FB8;
const PLAYER_DRAW_D3D_OFF        = 0x6D5B90;
const PLAYER_DRAW_SLA_OFF        = 0x6D5658;
const PLAYER_RENDER_PREPARE_OFF  = 0x6D5164;
const PLAYER_APPLY_TRANSLATE_OFF = 0x6D5264;
const PLAYER_BUILD_ITEMS_OFF     = 0x6C2334;
const PLAYER_BUILD_COMMANDS_OFF  = 0x6C4E28;
const PLAYER_RENDER_EXECUTE_OFF  = 0x6C7440;
const PLAYER_UPDATE_LAYER_AFTER_DRAW_OFF = 0x6CE7D8;
const LAYER_FILL_RECT_OFF        = 0x80EBAC;
const LAYER_SAVE_LAYER_IMAGE_OFF = 0x80963C;
const LAYER_CLASS_ID_OFF = 0x1ADE668;
const LAYER_NATIVE_MAIN_IMAGE_OFF = 280;
const BITMAP_NATIVE_IMPL_OFF = 88;

const STATIC_PARSE_PROJECTION = 'static_parse-semantic-v1';
const STATIC_PARSE_SAMPLE_POINTS = {
    init_non_emote_enter: 'initNonEmoteMotionLike_0x6B365C.enter',
    init_non_emote_leave: 'initNonEmoteMotionLike_0x6B365C.leave',
    parse_parameter_enter: 'appendParameterEntryLike_0x6B1718.enter',
    parse_parameter_leave: 'appendParameterEntryLike_0x6B1718.leave',
    parse_parameter_list_enter: 'parseParameterListLike_0x6B202C.enter',
    parse_parameter_list_leave: 'parseParameterListLike_0x6B202C.leave',
};

const INIT_MOTION_PROJECTION = 'init-motion-semantic-v1';
const INIT_MOTION_SAMPLE_POINTS = {
    init_non_emote_enter: 'initNonEmoteMotionLike_0x6B365C.enter',
    init_non_emote_leave: 'initNonEmoteMotionLike_0x6B365C.leave',
};

const TRACE_FLATTEN_PROJECTION = 'trace_flatten-semantic-v1';
const TRACE_FLATTEN_SAMPLE_POINT = 'progressCompat.phase3-end.pre-cleanup';
const FRAME_SELECTION_SPEC = __FRAME_SELECTION_PROJECTION_JSON__;
const FRAME_SELECTION_PROJECTION = FRAME_SELECTION_SPEC.projection;
const FRAME_SELECTION_SAMPLE_POINT = FRAME_SELECTION_SPEC.samplePoint;
const FRAME_SELECTION_NODE_FIELDS = FRAME_SELECTION_SPEC.nodeFields || [];

const NODE_STRIDE = 2632;
const PARAM_ENTRY_STRIDE = 56;

const STAGE_STATIC_PARSE = 'static_parse';
const STAGE_INIT_MOTION = 'init_motion';
const STAGE_VARIABLE_BINDING = 'variable_binding';
const STAGE_FRAME_SELECTION = 'frame_selection';
const STAGE_SUB_MOTION_DECISION = 'sub_motion_decision';
const STAGE_TRACE_FLATTEN = 'trace_flatten';
const STAGE_DRAW_DISPATCH = 'draw_dispatch';
const STAGE_RENDER_PREPARE = 'render_prepare';
const STAGE_RENDER_COMMANDS = 'render_commands';
const STAGE_RENDER_EXECUTE = 'render_execute';
const STAGE_LAYER_SAVE = 'layer_save';
const STAGE_LAYER_RAW_PROBE = 'layer_raw_probe';

const ALL_STAGES = [
    STAGE_STATIC_PARSE,
    STAGE_INIT_MOTION,
    STAGE_VARIABLE_BINDING,
    STAGE_FRAME_SELECTION,
    STAGE_SUB_MOTION_DECISION,
    STAGE_TRACE_FLATTEN,
];

const RENDER_STAGES = [
    STAGE_DRAW_DISPATCH,
    STAGE_RENDER_PREPARE,
    STAGE_RENDER_COMMANDS,
    STAGE_RENDER_EXECUTE,
    STAGE_LAYER_SAVE,
    STAGE_LAYER_RAW_PROBE,
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

const PLAYER_OFF = {
    internalAssignRequested: 613,
    d3dDrawMode: 909,
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
let lastCompletedFrameId = null;
let lastCompletedTopPlayer = null;
let currentRenderFrameId = null;
let currentRenderPlayer = null;
let drawIdCounter = 0;
let activeDrawContexts = [];
let recordRenderStepCheckpoints = false;
let recordLayerRawProbes = false;
let nativeInstanceSupportCache = {};
let bitmapBufferFunctionCache = {};
let bitmapPitchFunctionCache = {};

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

function readU32(p, off) {
    try { return p.add(off).readU32(); } catch (e) { return null; }
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

function readFloat(p, off) {
    try {
        const v = p.add(off).readFloat();
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

function getCachedNativeFunction(cache, fnPtr, retType, argTypes) {
    const key = ptrHex(fnPtr);
    if (!key) return null;
    if (!cache[key]) {
        cache[key] = new NativeFunction(fnPtr, retType, argTypes);
    }
    return cache[key];
}

function readVariantObject(variantPtr) {
    try {
        const variant = ptr(variantPtr);
        if (variant.isNull()) {
            return {
                object: null,
                objThis: null,
                type: null,
                error: 'null variant',
            };
        }
        const type = readS32(variant, 16);
        const object = readPointer(variant, 0);
        if (object === null) {
            return {
                object: null,
                objThis: null,
                type: type,
                error: 'variant object is null',
            };
        }
        return {
            object: object,
            objThis: readPointer(variant, 8),
            type: type,
            error: null,
        };
    } catch (e) {
        return {
            object: null,
            objThis: null,
            type: null,
            error: String(e),
        };
    }
}

function ptrEqual(a, b) {
    if (!a || !b) return false;
    try {
        return ptr(a).equals(ptr(b));
    } catch (e) {
        return false;
    }
}

function resolveLayerNativeInstance(layerObject) {
    const obj = ptr(layerObject);
    if (obj.isNull()) return { layer: null, error: 'null layer object' };
    try {
        const vtable = obj.readPointer();
        const fnPtr = vtable.add(200).readPointer();
        const fn = getCachedNativeFunction(
            nativeInstanceSupportCache, fnPtr,
            'int64', ['pointer', 'int64', 'uint32', 'pointer']);
        if (!fn) return { layer: null, error: 'NativeInstanceSupport missing' };
        const out = Memory.alloc(Process.pointerSize);
        out.writePointer(NULL);
        const classId = ensureBase().add(LAYER_CLASS_ID_OFF).readU32();
        const hr = fn(obj, 2, classId, out);
        const layer = out.readPointer();
        if ((Number(hr) & 0x80000000) !== 0 || layer.isNull()) {
            return {
                layer: null,
                error: 'NativeInstanceSupport failed',
                hresult: String(hr),
                classId: classId,
            };
        }
        return { layer: layer, error: null, classId: classId };
    } catch (e) {
        return { layer: null, error: String(e) };
    }
}

function readNativeLayerImageSnapshot(nativeLayer, layerObject) {
    const native = nativeLayer ? ptr(nativeLayer) : NULL;
    if (native.isNull()) {
        return {
            ok: false,
            error: 'null native layer',
            diagnostics: {
                layerObject: ptrHex(layerObject),
                nativeLayer: null,
            },
        };
    }
    try {
        const mainImage = readPointer(native, LAYER_NATIVE_MAIN_IMAGE_OFF);
        if (mainImage === null) return { ok: false, error: 'layer has no main image' };
        const bitmapImpl = readPointer(mainImage, BITMAP_NATIVE_IMPL_OFF);
        if (bitmapImpl === null) {
            return { ok: false, error: 'main image has no bitmap impl' };
        }
        const width = readU32(bitmapImpl, 12);
        const height = readU32(bitmapImpl, 16);
        if (!width || !height || width <= 0 || height <= 0 ||
            width > 8192 || height > 8192) {
            return {
                ok: false,
                error: 'invalid bitmap dimensions',
                width: width,
                height: height,
            };
        }
        const vtable = bitmapImpl.readPointer();
        const bufferFn = getCachedNativeFunction(
            bitmapBufferFunctionCache,
            vtable.add(56).readPointer(),
            'pointer', ['pointer']);
        const pitchFn = getCachedNativeFunction(
            bitmapPitchFunctionCache,
            vtable.add(80).readPointer(),
            'int', ['pointer']);
        const buffer = bufferFn(bitmapImpl);
        const pitch = pitchFn(bitmapImpl);
        if (!buffer || buffer.isNull()) {
            return { ok: false, error: 'bitmap buffer is null' };
        }
        if (!pitch || pitch < width * 4 || pitch > width * 16) {
            return {
                ok: false,
                error: 'invalid bitmap pitch',
                width: width,
                height: height,
                pitch: pitch,
            };
        }
        const packedSize = width * height * 4;
        const packed = Memory.alloc(packedSize);
        for (let y = 0; y < height; y++) {
            Memory.copy(
                packed.add(y * width * 4),
                buffer.add(y * pitch),
                width * 4);
        }
        return {
            ok: true,
            width: width,
            height: height,
            pitch: pitch,
            pixelFormat: 'bgra32',
            data: packed.readByteArray(packedSize),
            diagnostics: {
                layerObject: ptrHex(layerObject),
                nativeLayer: ptrHex(native),
                mainImage: ptrHex(mainImage),
                bitmapImpl: ptrHex(bitmapImpl),
                buffer: ptrHex(buffer),
            },
        };
    } catch (e) {
        return {
            ok: false,
            error: String(e),
            diagnostics: {
                layerObject: ptrHex(layerObject),
                nativeLayer: ptrHex(native),
            },
        };
    }
}

function readLayerImageSnapshot(layerObject) {
    const resolved = resolveLayerNativeInstance(layerObject);
    if (!resolved.layer) {
        return {
            ok: false,
            error: resolved.error || 'no layer',
            diagnostics: {
                layerObject: ptrHex(layerObject),
                nativeLayer: null,
                classId: resolved.classId || null,
                hresult: resolved.hresult || null,
            },
        };
    }
    const snapshot = readNativeLayerImageSnapshot(resolved.layer, layerObject);
    if (snapshot.diagnostics) {
        snapshot.diagnostics.classId = resolved.classId || null;
    }
    return snapshot;
}

function sendRenderImageCheckpoint(player, layerObject, phase, samplePoint) {
    if (!recordRenderStepCheckpoints || !recording ||
        !stageEnabled(STAGE_RENDER_EXECUTE)) {
        return;
    }
    const frameId = renderFrameIdFor(player);
    if (frameId === null || frameId === undefined) return;
    const snapshot = readLayerImageSnapshot(layerObject);
    const payload = {
        type: 'render_image_checkpoint',
        source: 'android-frida-layer-main-image',
        phase: phase,
        samplePoint: samplePoint,
        frameId: frameId,
        player: ptrHex(player),
        layerObject: ptrHex(layerObject),
        ok: snapshot.ok === true,
        width: snapshot.width || null,
        height: snapshot.height || null,
        pitch: snapshot.pitch || null,
        pixelFormat: snapshot.pixelFormat || 'bgra32',
        diagnostics: snapshot.diagnostics || {},
    };
    if (!snapshot.ok) {
        payload.error = snapshot.error || 'snapshot failed';
        send(payload);
        return;
    }
    send(payload, snapshot.data);
}

function sendLayerRawProbe(player, layerObject, nativeLayer, samplePoint,
                           semanticPayload, diagnostics) {
    if (!recordLayerRawProbes || !recording ||
        !stageEnabled(STAGE_LAYER_RAW_PROBE)) {
        return;
    }
    let frameId = renderFrameIdFor(player);
    const snapshot = nativeLayer
        ? readNativeLayerImageSnapshot(nativeLayer, layerObject)
        : readLayerImageSnapshot(layerObject);
    const diag = {};
    const snapshotDiag = snapshot.diagnostics || {};
    for (const k of Object.keys(snapshotDiag)) diag[k] = snapshotDiag[k];
    if (diagnostics) {
        for (const k of Object.keys(diagnostics)) diag[k] = diagnostics[k];
    }
    const payload = semanticPayload || {};
    payload.schema = 'motion-render-stage-oracle-v1-event';
    payload.source = 'android-frida-layer-main-image-raw-probe';
    payload.stage = STAGE_LAYER_RAW_PROBE;
    payload.kind = 'raw_probe';
    payload.samplePoint = samplePoint || 'layer_raw_probe';
    if (frameId !== null && frameId !== undefined) {
        payload.frameId = frameId;
    }
    payload.player = ptrHex(player || currentRenderPlayer);
    payload.ok = snapshot.ok === true;
    payload.width = snapshot.width || null;
    payload.height = snapshot.height || null;
    payload.pitch = snapshot.pitch || null;
    payload.pixelFormat = snapshot.pixelFormat || 'bgra32';
    payload.nativeLayer = diag.nativeLayer || ptrHex(nativeLayer);
    payload.mainImage = diag.mainImage || null;
    payload.bitmapImpl = diag.bitmapImpl || null;
    payload.buffer = diag.buffer || null;
    payload.diagnostics = diag;
    payload.seq = seqCounter++;
    payload.timeMs = Date.now() - startTimeMs;
    if (!snapshot.ok) {
        payload.error = snapshot.error || 'snapshot failed';
        events.push(payload);
        send({
            type: 'layer_raw_probe',
            ok: false,
            seq: payload.seq,
            samplePoint: payload.samplePoint,
            frameId: payload.frameId,
            error: payload.error,
        });
        return;
    }
    events.push(payload);
    send({
        type: 'layer_raw_probe',
        ok: true,
        seq: payload.seq,
        samplePoint: payload.samplePoint,
        frameId: payload.frameId,
        width: snapshot.width,
        height: snapshot.height,
        pitch: snapshot.pitch,
        pixelFormat: snapshot.pixelFormat || 'bgra32',
    }, snapshot.data);
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

function emitInitMotion(kind, semanticPayload, diagnostics) {
    if (!recording || !stageEnabled(STAGE_INIT_MOTION)) return;
    const diag = diagnostics || {};
    if (inCompat) {
        diag.frameId = currentFrameId;
        diag.objthis = ptrHex(capturedObjthis);
    }
    const ev = semanticPayload || {};
    ev.schema = 'motion-stage-oracle-v1-event';
    ev.stage = STAGE_INIT_MOTION;
    ev.kind = kind;
    ev.projection = INIT_MOTION_PROJECTION;
    ev.samplePoint = INIT_MOTION_SAMPLE_POINTS[kind] || kind;
    ev.diagnostics = diag;
    ev.seq = seqCounter++;
    ev.timeMs = Date.now() - startTimeMs;
    events.push(ev);
}

function semanticFrameSelectionNode(raw) {
    if (raw === null || raw === undefined) return null;
    const out = {};
    for (const field of FRAME_SELECTION_NODE_FIELDS) {
        out[field] = Object.prototype.hasOwnProperty.call(raw, field)
            ? raw[field]
            : null;
    }
    return out;
}

function emitFrameSelection(kind, semanticPayload, diagnostics) {
    if (!recording || !stageEnabled(STAGE_FRAME_SELECTION)) return;
    const diag = diagnostics || {};
    if (inCompat) {
        diag.objthis = ptrHex(capturedObjthis);
    }
    const ev = semanticPayload || {};
    ev.schema = 'motion-stage-oracle-v1-event';
    ev.stage = STAGE_FRAME_SELECTION;
    ev.kind = kind;
    ev.projection = FRAME_SELECTION_PROJECTION;
    ev.samplePoint = FRAME_SELECTION_SAMPLE_POINT;
    ev.diagnostics = diag;
    ev.seq = seqCounter++;
    ev.timeMs = Date.now() - startTimeMs;
    if (inCompat) {
        ev.frameId = currentFrameId;
    }
    events.push(ev);
}

function renderFrameIdFor(player) {
    if (currentRenderFrameId !== null && currentRenderFrameId !== undefined) {
        return currentRenderFrameId;
    }
    if (lastCompletedFrameId === null || lastCompletedFrameId === undefined) {
        return null;
    }
    if (lastCompletedTopPlayer === null || lastCompletedTopPlayer === undefined) {
        return lastCompletedFrameId;
    }
    if (player === null || player === undefined) {
        return lastCompletedFrameId;
    }
    return ptrHex(player) === ptrHex(lastCompletedTopPlayer)
        ? lastCompletedFrameId
        : null;
}

function emitRender(stage, kind, semanticPayload, diagnostics, samplePoint) {
    if (!recording || !stageEnabled(stage)) return;
    let player = currentRenderPlayer;
    if (!player && diagnostics && diagnostics.player) {
        try { player = ptr(diagnostics.player); } catch (e) { player = null; }
    }
    const frameId = renderFrameIdFor(player);
    if (frameId === null || frameId === undefined) return;
    const diag = diagnostics || {};
    const ev = semanticPayload || {};
    ev.schema = 'motion-render-stage-oracle-v1-event';
    ev.stage = stage;
    ev.kind = kind;
    ev.samplePoint = samplePoint || kind;
    ev.frameId = frameId;
    ev.player = ptrHex(player);
    ev.diagnostics = diag;
    ev.seq = seqCounter++;
    ev.timeMs = Date.now() - startTimeMs;
    events.push(ev);
}

function currentDrawContextFor(player) {
    if (activeDrawContexts.length === 0) return null;
    const playerHex = ptrHex(player);
    for (let i = activeDrawContexts.length - 1; i >= 0; --i) {
        const ctx = activeDrawContexts[i];
        if (!ctx) continue;
        if (!playerHex || ctx.playerHex === playerHex) return ctx;
    }
    return null;
}

function drawPathSummary(ctx) {
    const route = ctx.route || (ctx.steps.length === 0 ? 'no_target' : 'failed');
    return {
        route: route,
        steps: ctx.steps.slice(),
        prepareCalled: ctx.prepareCalled,
        prepareOk: ctx.prepareOk,
        d3dDrawModeAfterPrepare: ctx.d3dDrawModeAfterPrepare,
        renderToCanvasCalled: ctx.renderToCanvasCalled,
        updateLayerAfterDrawCalled: ctx.updateLayerAfterDrawCalled,
        internalAssignRequested: ctx.internalAssignRequested,
    };
}

function beginDrawContext(player, argVariant) {
    const targetVariant = readVariantObject(argVariant);
    const ctx = {
        drawId: drawIdCounter++,
        player: player,
        playerHex: ptrHex(player),
        argVariant: argVariant,
        targetObject: targetVariant.object,
        targetObjThis: targetVariant.objThis,
        targetVariantType: targetVariant.type,
        targetVariantError: targetVariant.error,
        steps: [],
        emittedSteps: {},
        route: null,
        prepareCalled: false,
        prepareOk: null,
        d3dDrawModeAfterPrepare: null,
        renderToCanvasCalled: false,
        updateLayerAfterDrawCalled: false,
        internalAssignRequested: null,
    };
    activeDrawContexts.push(ctx);
    return ctx;
}

function finishDrawContext(ctx) {
    if (!ctx) return;
    for (let i = activeDrawContexts.length - 1; i >= 0; --i) {
        if (activeDrawContexts[i] === ctx) {
            activeDrawContexts.splice(i, 1);
            break;
        }
    }
}

function setDrawRoute(ctx, route) {
    if (ctx && route) ctx.route = route;
}

function emitDrawStep(ctx, drawStep, outcome, route, extra) {
    if (!ctx) return;
    const stepIndex = ctx.steps.length;
    ctx.steps.push(drawStep);
    ctx.emittedSteps[drawStep] = true;
    if (route) ctx.route = route;
    const payload = {
        drawId: ctx.drawId,
        stepIndex: stepIndex,
        drawStep: drawStep,
        outcome: outcome,
    };
    if (route) payload.route = route;
    if (extra) {
        for (const k of Object.keys(extra)) payload[k] = extra[k];
    }
    emitRender(STAGE_DRAW_DISPATCH, 'draw_step', payload, {
        addr: PLAYER_DRAW_COMPAT_OFF,
        player: ctx.playerHex,
        argVariant: ptrHex(ctx.argVariant),
        targetVariantType: ctx.targetVariantType,
        targetObject: ptrHex(ctx.targetObject),
        targetObjThis: ptrHex(ctx.targetObjThis),
        targetError: ctx.targetVariantError,
    }, 'Player_drawCompat_0x6D5FB8.' + drawStep);
}

function ensureDrawTargetCheckMisses(ctx) {
    if (!ctx) return;
    if (!ctx.emittedSteps.target_check_d3d) {
        emitDrawStep(ctx, 'target_check_d3d', 'miss');
    }
    if (!ctx.emittedSteps.target_check_sla) {
        emitDrawStep(ctx, 'target_check_sla', 'miss');
    }
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

function semanticPlayerOverview(raw) {
    raw = raw || {};
    return {
        nodeCount: raw.nodeCount || 0,
        parameterTable: semanticParameterTable(raw.parameterTable),
        playing: raw.playing,
        currentTime: raw.currentTime,
    };
}

function playerOverviewDiagnostics(raw) {
    raw = raw || {};
    return {
        player: raw.player,
        nodeLayout: raw.nodeLayout,
        frameTickCount: raw.frameTickCount,
        frameLastTime: raw.frameLastTime,
        parameterTable: parameterTableDiagnostics(raw.parameterTable),
    };
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

function semanticEvalNode(nodePtr) {
    return semanticFrameSelectionNode(snapshotEvalNode(nodePtr));
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

function readRectF(p, off) {
    return [
        readFloat(p, off),
        readFloat(p, off + 4),
        readFloat(p, off + 8),
        readFloat(p, off + 12),
    ];
}

function readRectS32(p, off) {
    return [
        readS32(p, off),
        readS32(p, off + 4),
        readS32(p, off + 8),
        readS32(p, off + 12),
    ];
}

function readRenderItem(itemPtr, index) {
    const item = ptr(itemPtr);
    const paintBox = readRectF(item, 184);
    const buildClipRect = readRectF(item, 216);
    return {
        index: index,
        item: ptrHex(item),
        flags: {
            flag16: readU8(item, 16),
            flag17: readU8(item, 17),
            flag18: readU8(item, 18),
            drawFlag19: readU8(item, 19),
            layerResolved20: readU8(item, 20),
            clipValid21: readU8(item, 21),
        },
        layerIds: {
            primary: readS32(item, 52),
            secondary: readS32(item, 56),
        },
        sortKey64: readDouble(item, 64),
        paintBox: paintBox,
        clipRect: buildClipRect,
        buildClipRect: buildClipRect,
        viewportRect: readRectF(item, 200),
        diagnostics: {
            itemPlus184PaintBox: paintBox,
            itemPlus216BuildClipRect: buildClipRect,
        },
        sourceGate232: readU32(item, 232),
        stencilType244: readU32(item, 244),
        parentItem264: ptrHex(readPointer(item, 264)),
        meshType280: readS32(item, 280),
        leafLayerVariantTag320: readU32(item, 320),
        composedLayerVariantTag340: readU32(item, 340),
    };
}

function readRenderItemVector(vectorPtr, limit) {
    const vec = ptr(vectorPtr);
    const out = {
        vector: ptrHex(vec),
        begin: null,
        end: null,
        count: 0,
        items: [],
    };
    try {
        const begin = vec.readPointer();
        const end = vec.add(8).readPointer();
        out.begin = ptrHex(begin);
        out.end = ptrHex(end);
        const beginN = ptrToNumber(begin);
        const endN = ptrToNumber(end);
        if (beginN === 0 || endN === 0 || endN < beginN) {
            out.error = 'invalid render item vector begin/end';
            return out;
        }
        const span = endN - beginN;
        if (span % 8 !== 0) {
            out.error = 'render item vector span is not pointer aligned';
            out.span = span;
            return out;
        }
        const count = span / 8;
        out.count = count;
        const n = Math.min(count, limit || 256);
        for (let i = 0; i < n; i++) {
            const itemPtr = begin.add(i * 8).readPointer();
            if (!itemPtr.isNull()) {
                out.items.push(readRenderItem(itemPtr, i));
            } else {
                out.items.push({ index: i, item: null });
            }
        }
        if (count > n) out.truncated = count - n;
    } catch (e) {
        out.error = String(e);
    }
    return out;
}

function readRenderLists(mainListPtr, auxListPtr) {
    return {
        mainList: mainListPtr ? readRenderItemVector(mainListPtr, 256) : null,
        auxList: auxListPtr ? readRenderItemVector(auxListPtr, 256) : null,
    };
}

function enterRenderContext(player) {
    return {
        frameId: currentRenderFrameId,
        player: currentRenderPlayer,
        nextFrameId: lastCompletedFrameId,
    };
}

function applyRenderContext(ctx, player) {
    currentRenderFrameId = ctx.nextFrameId;
    currentRenderPlayer = player;
}

function leaveRenderContext(ctx) {
    currentRenderFrameId = ctx.frameId;
    currentRenderPlayer = ctx.player;
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
            const completedFrameId = currentFrameId;
            const completedTopPlayer =
                samples.length > 0 ? samples[0].player : capturedObjthis;
            inCompat = false;
            capturedObjthis = null;
            currentFrameId = null;
            lastCompletedFrameId = completedFrameId;
            lastCompletedTopPlayer = completedTopPlayer;

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
                frameId: completedFrameId,
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
            frameCounter++;
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

    attachAt(PLAYER_DRAW_COMPAT_OFF, 'Player_drawCompat', {
        onEnter(args) {
            this.player = args[0];
            this.argVariant = args[1];
            this.ctx = enterRenderContext(this.player);
            applyRenderContext(this.ctx, this.player);
            this.drawCtx = beginDrawContext(this.player, this.argVariant);
            sendLayerRawProbe(
                this.player, this.drawCtx.targetObject, null,
                'Player_drawCompat_0x6D5FB8.enter',
                { drawId: this.drawCtx.drawId },
                {
                    addr: PLAYER_DRAW_COMPAT_OFF,
                    targetObject: ptrHex(this.drawCtx.targetObject),
                    targetObjThis: ptrHex(this.drawCtx.targetObjThis),
                    targetError: this.drawCtx.targetVariantError,
                });
            emitRender(STAGE_DRAW_DISPATCH, 'draw_enter', {
                drawId: this.drawCtx.drawId,
            }, {
                addr: PLAYER_DRAW_COMPAT_OFF,
                player: ptrHex(this.player),
                argVariant: ptrHex(this.argVariant),
                targetVariantType: this.drawCtx.targetVariantType,
                targetObject: ptrHex(this.drawCtx.targetObject),
                targetObjThis: ptrHex(this.drawCtx.targetObjThis),
                targetError: this.drawCtx.targetVariantError,
                rawArgs: {
                    arg0: ptrHex(args[0]),
                    arg1: ptrHex(args[1]),
                    arg2: ptrHex(args[2]),
                    arg3: ptrHex(args[3]),
                },
            }, 'Player_drawCompat_0x6D5FB8.enter');
        },
        onLeave() {
            sendLayerRawProbe(
                this.player,
                this.drawCtx ? this.drawCtx.targetObject : null,
                null,
                'Player_drawCompat_0x6D5FB8.leave',
                {
                    drawId: this.drawCtx ? this.drawCtx.drawId : null,
                },
                {
                    addr: PLAYER_DRAW_COMPAT_OFF,
                    targetObject: this.drawCtx
                        ? ptrHex(this.drawCtx.targetObject) : null,
                    targetObjThis: this.drawCtx
                        ? ptrHex(this.drawCtx.targetObjThis) : null,
                    targetError: this.drawCtx
                        ? this.drawCtx.targetVariantError : null,
                });
            emitRender(STAGE_DRAW_DISPATCH, 'draw_leave', {
                drawId: this.drawCtx ? this.drawCtx.drawId : null,
                route: this.drawCtx
                    ? drawPathSummary(this.drawCtx).route
                    : 'failed',
                drawPath: this.drawCtx ? drawPathSummary(this.drawCtx) : null,
            }, {
                addr: PLAYER_DRAW_COMPAT_OFF,
                player: ptrHex(this.player),
                argVariant: ptrHex(this.argVariant),
                targetVariantType: this.drawCtx
                    ? this.drawCtx.targetVariantType : null,
                targetObject: this.drawCtx
                    ? ptrHex(this.drawCtx.targetObject) : null,
                targetObjThis: this.drawCtx
                    ? ptrHex(this.drawCtx.targetObjThis) : null,
                targetError: this.drawCtx
                    ? this.drawCtx.targetVariantError : null,
            }, 'Player_drawCompat_0x6D5FB8.leave');
            finishDrawContext(this.drawCtx);
            leaveRenderContext(this.ctx);
        },
    });

    attachAt(PLAYER_DRAW_D3D_OFF, 'Player_drawD3D', {
        onEnter(args) {
            this.player = args[0];
            const ctx = currentDrawContextFor(this.player);
            if (!ctx) return;
            emitDrawStep(ctx, 'target_check_d3d', 'hit', 'd3d_adaptor');
        },
    });

    attachAt(PLAYER_DRAW_SLA_OFF, 'Player_DrawSLA', {
        onEnter(args) {
            this.player = args[0];
            const ctx = currentDrawContextFor(this.player);
            if (!ctx) return;
            if (!ctx.emittedSteps.target_check_d3d) {
                emitDrawStep(ctx, 'target_check_d3d', 'miss');
            }
            emitDrawStep(
                ctx, 'target_check_sla', 'hit', 'separate_layer_adaptor');
        },
    });

    attachAt(PLAYER_RENDER_PREPARE_OFF, 'Player_renderPrepare', {
        onEnter(args) {
            this.player = args[0];
            this.mainList = args[1];
            this.auxList = args[2];
            emitRender(STAGE_RENDER_PREPARE, 'prepare_enter', {}, {
                addr: PLAYER_RENDER_PREPARE_OFF,
                player: ptrHex(this.player),
                mainListPtr: ptrHex(this.mainList),
                auxListPtr: ptrHex(this.auxList),
                arg3: ptrHex(args[3]),
                arg4: readArgInt(args[4]),
                arg5: readArgInt(args[5]),
            }, 'sub_6D5164.enter');
        },
        onLeave(retval) {
            const ctx = currentDrawContextFor(this.player);
            const ok = readArgInt(retval) !== 0;
            if (ctx) {
                ensureDrawTargetCheckMisses(ctx);
                ctx.prepareCalled = true;
                ctx.prepareOk = ok;
                emitDrawStep(
                    ctx,
                    'prepare_render_items',
                    ok ? 'ok' : 'empty',
                    ok ? null : 'prepare_empty',
                    { prepareOk: ok });
                if (ok) {
                    const d3dDrawMode = readBool(
                        this.player, PLAYER_OFF.d3dDrawMode);
                    ctx.d3dDrawModeAfterPrepare = d3dDrawMode;
                    emitDrawStep(
                        ctx,
                        'branch_after_prepare',
                        d3dDrawMode ? 'shared_d3d' : 'ordinary',
                        d3dDrawMode
                            ? 'shared_d3d_after_prepare'
                            : 'ordinary_layer',
                        { d3dDrawModeAfterPrepare: d3dDrawMode });
                }
            }
            emitRender(STAGE_RENDER_PREPARE, 'prepare_leave', {
                ok: ok ? 1 : 0,
                renderLists: readRenderLists(this.mainList, this.auxList),
            }, {
                addr: PLAYER_RENDER_PREPARE_OFF,
                player: ptrHex(this.player),
                retval: ptrHex(retval),
            }, 'sub_6D5164.leave');
        },
    });

    attachAt(PLAYER_APPLY_TRANSLATE_OFF, 'Player_applyTranslateOffset', {
        onEnter(args) {
            this.player = args[0];
            this.mainList = args[1];
            emitRender(STAGE_RENDER_PREPARE, 'apply_translate_enter', {}, {
                addr: PLAYER_APPLY_TRANSLATE_OFF,
                player: ptrHex(this.player),
                mainListPtr: ptrHex(this.mainList),
                arg2: ptrHex(args[2]),
            }, 'sub_6D5264.enter');
        },
        onLeave(retval) {
            const ctx = currentDrawContextFor(this.player);
            if (ctx) {
                emitDrawStep(ctx, 'apply_translate_offset', 'done');
            }
            emitRender(STAGE_RENDER_PREPARE, 'apply_translate_leave', {
                renderLists: readRenderLists(this.mainList, null),
            }, {
                addr: PLAYER_APPLY_TRANSLATE_OFF,
                player: ptrHex(this.player),
                retval: ptrHex(retval),
            }, 'sub_6D5264.leave');
        },
    });

    attachAt(PLAYER_BUILD_ITEMS_OFF, 'Player_buildRenderItems', {
        onEnter(args) {
            this.player = args[0];
            this.mainList = args[1];
            this.auxList = args[2];
            emitRender(STAGE_RENDER_COMMANDS, 'build_items_enter', {}, {
                addr: PLAYER_BUILD_ITEMS_OFF,
                player: ptrHex(this.player),
                mainListPtr: ptrHex(this.mainList),
                auxListPtr: ptrHex(this.auxList),
                defaultColor: readArgInt(args[3]),
                arg4: readArgInt(args[4]),
                arg5: readArgInt(args[5]),
            }, 'sub_6C2334.enter');
        },
        onLeave(retval) {
            emitRender(STAGE_RENDER_COMMANDS, 'build_items_leave', {
                renderLists: readRenderLists(this.mainList, this.auxList),
            }, {
                addr: PLAYER_BUILD_ITEMS_OFF,
                player: ptrHex(this.player),
                retval: ptrHex(retval),
            }, 'sub_6C2334.leave');
        },
    });

    attachAt(PLAYER_BUILD_COMMANDS_OFF, 'Player_buildRenderCommands', {
        onEnter(args) {
            this.player = args[0];
            this.mainList = args[1];
            this.auxList = args[2];
            emitRender(STAGE_RENDER_COMMANDS, 'build_commands_enter', {
                renderLists: readRenderLists(this.mainList, this.auxList),
            }, {
                addr: PLAYER_BUILD_COMMANDS_OFF,
                player: ptrHex(this.player),
                mainListPtr: ptrHex(this.mainList),
                auxListPtr: ptrHex(this.auxList),
                arg3: ptrHex(args[3]),
            }, 'sub_6C4E28.enter');
        },
        onLeave(retval) {
            emitRender(STAGE_RENDER_COMMANDS, 'build_commands_leave', {
                renderLists: readRenderLists(this.mainList, this.auxList),
            }, {
                addr: PLAYER_BUILD_COMMANDS_OFF,
                player: ptrHex(this.player),
                retval: ptrHex(retval),
            }, 'sub_6C4E28.leave');
        },
    });

    attachAt(PLAYER_RENDER_EXECUTE_OFF, 'Player_renderExecute', {
        onEnter(args) {
            this.player = args[0];
            this.targetVariant = args[1];
            this.targetVariantObject = readVariantObject(this.targetVariant);
            this.target = this.targetVariantObject.object;
            this.drawCtx = currentDrawContextFor(this.player);
            this.targetMatchesDrawArg = this.drawCtx
                ? ptrEqual(this.target, this.drawCtx.targetObject)
                : null;
            this.mainList = args[2];
            this.auxList = args[3];
            sendLayerRawProbe(
                this.player, this.target, null,
                'sub_6C7440.enter',
                {},
                {
                    addr: PLAYER_RENDER_EXECUTE_OFF,
                    targetVariant: ptrHex(this.targetVariant),
                    target: ptrHex(this.target),
                    targetObjThis: ptrHex(this.targetVariantObject.objThis),
                    targetError: this.targetVariantObject.error,
                    drawTarget: this.drawCtx
                        ? ptrHex(this.drawCtx.targetObject) : null,
                    targetMatchesDrawArg: this.targetMatchesDrawArg,
                });
            sendRenderImageCheckpoint(
                this.player, this.target, 'execute_pre',
                'sub_6C7440.enter.after-target-resolve');
            emitRender(STAGE_RENDER_EXECUTE, 'execute_enter', {
                renderLists: readRenderLists(this.mainList, this.auxList),
            }, {
                addr: PLAYER_RENDER_EXECUTE_OFF,
                player: ptrHex(this.player),
                targetVariant: ptrHex(this.targetVariant),
                targetVariantType: this.targetVariantObject.type,
                target: ptrHex(this.target),
                targetObjThis: ptrHex(this.targetVariantObject.objThis),
                targetError: this.targetVariantObject.error,
                drawTarget: this.drawCtx
                    ? ptrHex(this.drawCtx.targetObject) : null,
                targetMatchesDrawArg: this.targetMatchesDrawArg,
                mainListPtr: ptrHex(this.mainList),
                auxListPtr: ptrHex(this.auxList),
            }, 'sub_6C7440.enter');
        },
        onLeave(retval) {
            const ctx = currentDrawContextFor(this.player);
            if (ctx) {
                ctx.renderToCanvasCalled = true;
                emitDrawStep(ctx, 'render_to_canvas', 'done', 'ordinary_layer');
            }
            emitRender(STAGE_RENDER_EXECUTE, 'execute_leave', {
                renderLists: readRenderLists(this.mainList, this.auxList),
                retval: ptrHex(retval),
            }, {
                addr: PLAYER_RENDER_EXECUTE_OFF,
                player: ptrHex(this.player),
                targetVariant: ptrHex(this.targetVariant),
                targetVariantType: this.targetVariantObject.type,
                target: ptrHex(this.target),
                targetObjThis: ptrHex(this.targetVariantObject.objThis),
                targetError: this.targetVariantObject.error,
                drawTarget: this.drawCtx
                    ? ptrHex(this.drawCtx.targetObject) : null,
                targetMatchesDrawArg: this.targetMatchesDrawArg,
            }, 'sub_6C7440.leave');
            sendLayerRawProbe(
                this.player, this.target, null,
                'sub_6C7440.leave',
                {},
                {
                    addr: PLAYER_RENDER_EXECUTE_OFF,
                    targetVariant: ptrHex(this.targetVariant),
                    target: ptrHex(this.target),
                    targetObjThis: ptrHex(this.targetVariantObject.objThis),
                    targetError: this.targetVariantObject.error,
                    drawTarget: this.drawCtx
                        ? ptrHex(this.drawCtx.targetObject) : null,
                    targetMatchesDrawArg: this.targetMatchesDrawArg,
                });
            sendRenderImageCheckpoint(
                this.player, this.target, 'execute_post',
                'sub_6C7440.leave.before-return');
        },
    });

    attachAt(PLAYER_UPDATE_LAYER_AFTER_DRAW_OFF, 'Player_updateLayerAfterDraw', {
        onEnter(args) {
            this.player = args[0];
            this.targetVariant = args[1];
            this.targetVariantObject = readVariantObject(this.targetVariant);
            this.target = this.targetVariantObject.object;
            this.internalAssignRequested = readBool(
                this.player, PLAYER_OFF.internalAssignRequested);
            sendLayerRawProbe(
                this.player, this.target, null,
                'updateLayerAfterDraw_0x6CE7D8.enter',
                {
                    internalAssignRequested:
                        this.internalAssignRequested === true,
                },
                {
                    addr: PLAYER_UPDATE_LAYER_AFTER_DRAW_OFF,
                    targetVariant: ptrHex(this.targetVariant),
                    target: ptrHex(this.target),
                    targetObjThis: ptrHex(this.targetVariantObject.objThis),
                    targetError: this.targetVariantObject.error,
                });
        },
        onLeave() {
            sendLayerRawProbe(
                this.player, this.target, null,
                'updateLayerAfterDraw_0x6CE7D8.leave',
                {
                    internalAssignRequested:
                        this.internalAssignRequested === true,
                },
                {
                    addr: PLAYER_UPDATE_LAYER_AFTER_DRAW_OFF,
                    targetVariant: ptrHex(this.targetVariant),
                    target: ptrHex(this.target),
                    targetObjThis: ptrHex(this.targetVariantObject.objThis),
                    targetError: this.targetVariantObject.error,
                });
            const ctx = currentDrawContextFor(this.player);
            if (!ctx) return;
            ctx.updateLayerAfterDrawCalled = true;
            ctx.internalAssignRequested = this.internalAssignRequested;
            emitDrawStep(
                ctx,
                'update_layer_after_draw',
                'done',
                'ordinary_layer',
                { internalAssignRequested: this.internalAssignRequested });
        },
    });

    attachAt(LAYER_FILL_RECT_OFF, 'Layer_fillRect', {
        onEnter(args) {
            this.nativeLayer = args[0];
            this.player = currentRenderPlayer || lastCompletedTopPlayer;
        },
        onLeave() {
            sendLayerRawProbe(
                this.player, null, this.nativeLayer,
                'fillRect_0x80EBAC.leave',
                {},
                {
                    addr: LAYER_FILL_RECT_OFF,
                    nativeLayerArg: ptrHex(this.nativeLayer),
                });
        },
    });

    attachAt(LAYER_SAVE_LAYER_IMAGE_OFF, 'Layer_saveLayerImage', {
        onEnter(args) {
            this.nativeLayer = args[0];
            this.player = currentRenderPlayer || lastCompletedTopPlayer;
            this.mainImageAtEnter =
                readPointer(this.nativeLayer, LAYER_NATIVE_MAIN_IMAGE_OFF);
            sendLayerRawProbe(
                this.player, null, this.nativeLayer,
                'saveLayerImage_0x80963C.enter',
                {},
                {
                    addr: LAYER_SAVE_LAYER_IMAGE_OFF,
                    nativeLayerArg: ptrHex(this.nativeLayer),
                    saveLayerImageMainImageA1Plus280:
                        ptrHex(this.mainImageAtEnter),
                });
        },
        onLeave() {
            const mainImageAtLeave =
                readPointer(this.nativeLayer, LAYER_NATIVE_MAIN_IMAGE_OFF);
            sendLayerRawProbe(
                this.player, null, this.nativeLayer,
                'saveLayerImage_0x80963C.leave',
                {},
                {
                    addr: LAYER_SAVE_LAYER_IMAGE_OFF,
                    nativeLayerArg: ptrHex(this.nativeLayer),
                    saveLayerImageMainImageA1Plus280:
                        ptrHex(mainImageAtLeave),
                    saveLayerImageMainImageA1Plus280Enter:
                        ptrHex(this.mainImageAtEnter),
                    mainImagePointerStable:
                        ptrEqual(this.mainImageAtEnter, mainImageAtLeave),
                });
        },
    });

    attachAt(PLAYER_INIT_NON_EMOTE_OFF, 'Player_initNonEmoteMotion', {
        onEnter(args) {
            this.player = args[0];
            emitStaticParse('init_non_emote_enter', {}, {
                addr: PLAYER_INIT_NON_EMOTE_OFF,
                player: ptrHex(args[0]),
            });
            emitInitMotion('init_non_emote_enter', {}, {
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
            emitInitMotion('init_non_emote_leave', {
                overview: semanticPlayerOverview(overview),
            }, {
                addr: PLAYER_INIT_NON_EMOTE_OFF,
                retval: ptrHex(retval),
                player: ptrHex(this.player),
                overview: playerOverviewDiagnostics(overview),
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
            this.before = semanticEvalNode(args[0]);
        },
        onLeave(retval) {
            emitFrameSelection('evaluate_timeline', {
                dirtyArg: this.dirtyArg,
                time: this.time,
                retval: readArgInt(retval),
                before: this.before,
                after: semanticEvalNode(this.node),
            }, {
                addr: PLAYER_EVALUATE_TIMELINE_OFF,
                node: ptrHex(this.node),
                timeRaw: this.timeRaw,
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
            renderStages: RENDER_STAGES,
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
                drawCompat: PLAYER_DRAW_COMPAT_OFF,
                drawD3D: PLAYER_DRAW_D3D_OFF,
                drawSLA: PLAYER_DRAW_SLA_OFF,
                renderPrepare: PLAYER_RENDER_PREPARE_OFF,
                applyTranslate: PLAYER_APPLY_TRANSLATE_OFF,
                buildRenderItems: PLAYER_BUILD_ITEMS_OFF,
                buildRenderCommands: PLAYER_BUILD_COMMANDS_OFF,
                renderExecute: PLAYER_RENDER_EXECUTE_OFF,
                updateLayerAfterDraw: PLAYER_UPDATE_LAYER_AFTER_DRAW_OFF,
                layerFillRect: LAYER_FILL_RECT_OFF,
                layerSaveLayerImage: LAYER_SAVE_LAYER_IMAGE_OFF,
                layerClassId: LAYER_CLASS_ID_OFF,
            },
        };
    },
    startRecord(stageNames, options) {
        const requested = Array.isArray(stageNames) ? stageNames : ALL_STAGES;
        enabledStages = new Set(requested);
        enabledStages.add(STAGE_TRACE_FLATTEN);
        recordRenderStepCheckpoints =
            !!(options && options.recordRenderStepCheckpoints);
        recordLayerRawProbes =
            !!(options && options.recordLayerRawProbes);
        events = [];
        frameCounter = 0;
        seqCounter = 0;
        startTimeMs = Date.now();
        lastCompletedFrameId = null;
        lastCompletedTopPlayer = null;
        currentRenderFrameId = null;
        currentRenderPlayer = null;
        drawIdCounter = 0;
        activeDrawContexts = [];
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
