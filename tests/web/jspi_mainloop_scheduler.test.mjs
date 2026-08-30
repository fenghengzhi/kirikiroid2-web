import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import {fileURLToPath} from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const libraryPath = path.resolve(
    testDir, "../../platforms/web/jspi_jsc_mainloop_fix.js");
const source = fs.readFileSync(libraryPath, "utf8");

let library;
let tickImplementation;
let scheduledFrames = 0;
let handledError = null;
const mainLoop = {
  func: null,
  scheduler: null,
};

const context = vm.createContext({
  addToLibrary(value) {
    library = value;
  },
  MainLoop: mainLoop,
  setMainLoop(iterFunc) {
    mainLoop.func = iterFunc;
    mainLoop.scheduler = () => {
      ++scheduledFrames;
    };
    // Match Emscripten: setMainLoop schedules the first RAF immediately.
    mainLoop.scheduler();
  },
  handleException(error) {
    handledError = error;
  },
  wasmTable: {
    get() {
      return () => {};
    },
  },
  WebAssembly: {
    promising() {
      return (...args) => tickImplementation(...args);
    },
  },
  URLSearchParams,
  performance: {now: () => 0},
  location: {search: "?fps=60"},
  requestAnimationFrame() {},
  Promise,
  Map,
  Number,
  Object,
  document: {},
});

vm.runInContext(source, context, {filename: libraryPath});
assert.ok(library, "Emscripten user library was not registered");

let resolveFirst;
tickImplementation = () => new Promise((resolve) => {
  resolveFirst = resolve;
});
library.emscripten_set_main_loop_arg(1, 0, 0, false);

assert.equal(scheduledFrames, 1, "setMainLoop must schedule the first RAF");
const firstTick = mainLoop.func();
assert.ok(firstTick instanceof Promise);
mainLoop.scheduler();
mainLoop.scheduler();
assert.equal(
    scheduledFrames, 1,
    "a pending JSPI tick must not schedule overlapping RAF callbacks");

resolveFirst();
await firstTick;
await Promise.resolve();
assert.equal(
    scheduledFrames, 2,
    "the next RAF must be scheduled after the original tick resolves");

let replacementSchedules = 0;
mainLoop.scheduler = () => {
  ++replacementSchedules;
};

let resolveSecond;
tickImplementation = () => new Promise((resolve) => {
  resolveSecond = resolve;
});
context.__tvpRafT = 17;
const secondTick = mainLoop.func();
mainLoop.scheduler();
assert.equal(
    replacementSchedules, 0,
    "a timing-mode scheduler replacement must remain behind the Promise gate");

resolveSecond();
await secondTick;
await Promise.resolve();
assert.equal(
    replacementSchedules, 1,
    "the latest timing-mode scheduler must run after tick completion");

let rejectThird;
tickImplementation = () => new Promise((_resolve, reject) => {
  rejectThird = reject;
});
context.__tvpRafT = 34;
const thirdTick = mainLoop.func();
mainLoop.scheduler();
const expectedError = new Error("tick failed");
rejectThird(expectedError);
await assert.rejects(thirdTick, /tick failed/);
await Promise.resolve();
assert.equal(handledError, expectedError);
assert.equal(
    replacementSchedules, 1,
    "a rejected tick must not schedule another frame");

console.log("JSPI main-loop Promise scheduler tests passed");
