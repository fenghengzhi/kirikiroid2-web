// Frida agent for the krkr2 call tracer.
//
// Host-side (frida_tracer.py) calls over RPC:
//   setTargets([offset...])   // one-shot hook install
//   startCase()               // clear buffer, begin recording
//   stopCase() -> events[]    // pause recording, return frozen buffer
//
// Events are plain objects:
//   { kind: "enter", addr: 0x69A754, depth: 0,
//     x: ["0x...", ... x0..x7], d: ["0x...", ... d0..d7 IEEE bits] }
//   { kind: "exit",  addr: 0x69A754, depth: 0,
//     x0: "0x...", d0: "0x..." }
//
// Pointer and float values are serialised as hex strings (JS numbers lose
// precision past 2^53). Offsets (relative to libkrkr2.so base) are used
// instead of absolute addresses so traces are deterministic across ASLR.

'use strict';

const events = [];
let recording = false;
let depth = 0;
let base = null;

function ensureBase() {
    if (base !== null) return base;
    base = Module.findBaseAddress('libkrkr2.so');
    if (base === null) {
        throw new Error('libkrkr2.so not loaded in target process');
    }
    return base;
}

function hex64(nativePtr) {
    // NativePointer.toString() → "0x..." already.
    return nativePtr.toString();
}

function dblBits(nativePtr) {
    // A NativePointer passed via context.dN is the raw 64-bit IEEE-754
    // representation (Frida surfaces FP regs as pointers on arm64).
    return nativePtr.toString();
}

function snapshotArgs(ctx) {
    return {
        x: [ctx.x0, ctx.x1, ctx.x2, ctx.x3,
            ctx.x4, ctx.x5, ctx.x6, ctx.x7].map(hex64),
        d: [ctx.d0, ctx.d1, ctx.d2, ctx.d3,
            ctx.d4, ctx.d5, ctx.d6, ctx.d7].map(dblBits),
    };
}

function hook(offset) {
    const addr = ensureBase().add(offset);
    Interceptor.attach(addr, {
        onEnter(args) {
            if (!recording) return;
            this._depth = depth++;
            const snap = snapshotArgs(this.context);
            events.push({
                kind: 'enter',
                addr: offset,
                depth: this._depth,
                x: snap.x,
                d: snap.d,
            });
        },
        onLeave(retval) {
            if (!recording) return;
            depth--;
            events.push({
                kind: 'exit',
                addr: offset,
                depth: this._depth !== undefined ? this._depth : depth,
                x0: retval.toString(),
                d0: this.context.d0.toString(),
            });
        },
    });
}

rpc.exports = {
    setTargets(addrs) {
        ensureBase();
        addrs.forEach((off) => hook(off));
        return { hooked: addrs.length, base: base.toString() };
    },
    startCase() {
        events.length = 0;
        depth = 0;
        recording = true;
        return true;
    },
    stopCase() {
        recording = false;
        return events.slice();
    },
};
