import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const buildDir = path.resolve(process.argv[2] || "out/web/release");
const read = name => fs.readFileSync(path.join(buildDir, name));
const html = read("index.html").toString("utf8");
const glue = read("index.js").toString("utf8");
const serviceWorker = read("sw.js").toString("utf8");
const vlfs = read("vlfs.js").toString("utf8");
const wasm = read("index.wasm");

const htmlVersion = html.match(/BUILD_VERSION\s*=\s*["']([^"']+)["']/)?.[1];
const swVersion = serviceWorker.match(
    /CACHE_VERSION\s*=\s*["']([^"']+)["']/)?.[1];
assert.ok(htmlVersion, "index.html does not expose its build version");
assert.equal(swVersion, htmlVersion, "HTML and Service Worker versions differ");

const escapedVersion = htmlVersion.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
assert.match(
    html,
    new RegExp(`src=["']?index\\.js\\?v=${escapedVersion}(?:["' >])`),
    "Emscripten index.js script tag is not build-versioned");
for(const asset of ["vlfs.js", "assets.zip", "index.wasm", "index.worker.js"]) {
    assert.ok(
        html.includes(`${asset}?v=${htmlVersion}`),
        `${asset} is not bound to the HTML build version`);
}
assert.match(
    html,
    /searchParams\.set\(["']_krkr2_update["'],/,
    "Force Update does not cache-bust the HTML navigation");
assert.match(
    html,
    /location\.replace\([A-Za-z_$][\w$]*\.toString\(\)\)/,
    "Force Update does not navigate to its cache-busted URL");

assert.match(serviceWorker, /ENGINE_ASSET_SUFFIX\s*=/);
assert.match(
    serviceWorker,
    /request\.cache\s*===\s*["']no-store["']/,
    "Service Worker does not bypass CacheStorage for OPFS-managed archives");
for(const asset of ["index.js", "index.wasm", "vlfs.js", "assets.zip"]) {
    assert.ok(
        serviceWorker.includes(`'./${asset}' + ENGINE_ASSET_SUFFIX`),
        `Service Worker does not precache versioned ${asset}`);
}
assert.match(html, /["']If-None-Match["']\s*:/,
    "HTML does not conditionally validate the complete ZIP cache");
assert.match(html, /VLFS\.restoreZipCache\(/,
    "HTML does not restore a 304-validated ZIP from OPFS");
assert.match(vlfs, /ZIP_CACHE_SCHEMA_VERSION\s*=\s*2/,
    "VLFS complete ZIP cache schema is not enabled");
assert.match(vlfs, /_writeZipEntryToCache\(/,
    "VLFS does not persist all ZIP entry methods into OPFS");

const module = new WebAssembly.Module(wasm);
const continuationImports = WebAssembly.Module.imports(module).filter(
    item => item.module === "env" &&
        item.kind === "function" &&
        item.name.includes("WaveSoundContinuation"));
assert.deepEqual(
    continuationImports.map(item => item.name).sort(),
    [
      "TVPCompleteWaveSoundContinuation",
      "__asyncjs__TVPWaitWaveSoundContinuation",
    ]);
for(const {name} of continuationImports) {
    assert.ok(
        glue.includes(`function ${name}(`),
        `index.js does not define Wasm function import ${name}`);
}
assert.match(
    glue,
    /wasmImports=\{TVPCompleteWaveSoundContinuation[,}]/,
    "index.js does not publish TVPCompleteWaveSoundContinuation to Wasm");

const headers = fs.readFileSync(
    path.resolve(testDir, "../../_headers"), "utf8");
for(const asset of ["index.js", "index.wasm", "index.worker.js", "sw.js"]) {
    assert.match(
        headers,
        new RegExp(`/${asset.replace(".", "\\.")}\\n` +
            String.raw`\s+Cache-Control: .*max-age=0|` +
            `/${asset.replace(".", "\\.")}\\n` +
            String.raw`\s+Cache-Control: .*no-store`),
        `_headers does not force ${asset} revalidation`);
}

console.log(`Web release asset pair verified: ${htmlVersion}`);
