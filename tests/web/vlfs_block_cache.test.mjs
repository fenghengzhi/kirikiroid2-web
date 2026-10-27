import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import vm from "node:vm";
import {fileURLToPath} from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
// An optional source path lets the same cases check a pre-fix or deployed copy.
const vlfsSource = fs.readFileSync(process.argv[2] ??
    path.resolve(testDir, "../../platforms/web/vlfs.js"), "utf8");
const fixture = fs.readFileSync(
    path.resolve(testDir, "../test_files/xp3/kag3_intro.xp3"));
const fixtureBlob = new Blob([fixture]);
const BLOCK_SIZE = 256 * 1024;
const CACHE_BUDGET = 16 * 1024 * 1024;
const CACHE_BLOCKS = CACHE_BUDGET / BLOCK_SIZE;

function loadVLFS() {
    const context = vm.createContext({Blob, Uint8Array, console, window: {}});
    vm.runInContext(vlfsSource, context, {filename: "vlfs.js"});
    return context.window.VLFS;
}

function openFixture(vlfs, name, blob = fixtureBlob) {
    const entry = vlfs.registerBlobFile(name, blob);
    const fd = vlfs.open(name, 0);
    assert.notEqual(fd, -1);
    return {entry, fd};
}

function assertBytes(actual, offset, length) {
    assert.deepEqual(Buffer.from(actual), fixture.subarray(offset, offset + length));
}

function assertCacheValid(vlfs) {
    let total = 0;
    for(const block of vlfs._blockCache.values()) {
        assert.ok(block instanceof Uint8Array, "every cached block must contain data");
        assert.ok(block.length > 0 && block.length <= BLOCK_SIZE);
        total += block.length;
    }
    assert.equal(vlfs.stats().blockCacheBytes, total, "cache byte accounting matches its contents");
    assert.ok(total <= CACHE_BUDGET, "cache remains within its budget");
}

async function fillCache(vlfs, prefix, count) {
    // Reuse real fixture bytes under distinct paths to exercise the shared cache.
    for(let i = 0; i < count; i++) {
        const {fd} = openFixture(vlfs, `/${prefix}-${i}.xp3`);
        try {
            assertBytes(await vlfs.read(fd, 1), 0, 1);
        } finally {
            vlfs.close(fd);
        }
        assertCacheValid(vlfs);
    }
}

function deferred() {
    let resolve;
    const promise = new Promise(done => { resolve = done; });
    return {promise, resolve};
}

test("a full cache preserves cross-block reads and later evictions", async () => {
    const vlfs = loadVLFS();
    const {entry, fd} = openFixture(vlfs, "/target.xp3");
    assertBytes(await vlfs.read(fd, 1), 0, 1);
    await fillCache(vlfs, "initial", CACHE_BLOCKS - 1);
    assert.equal(vlfs.stats().blockCacheBytes, CACHE_BUDGET);
    assert.equal(vlfs._blockCache.keys().next().value, `${entry.id}@0`);
    assert.equal(vlfs._blockCache.has(`${entry.id}@1`), false);

    // Loading the missing second block used to evict the first before assembly.
    const offset = BLOCK_SIZE - 1;
    vlfs.seek(fd, offset, 0);
    assert.equal(vlfs.readCached(fd, 2), null);
    assert.equal(vlfs.seek(fd, 0, 1), offset, "a cache miss leaves the cursor untouched");
    assertBytes(await vlfs.read(fd, 2), offset, 2);
    assert.equal(vlfs.seek(fd, 0, 1), offset + 2);
    assertCacheValid(vlfs);

    vlfs.seek(fd, offset, 0);
    const cached = vlfs.readCached(fd, 2);
    assert.notEqual(cached, null);
    assertBytes(cached, offset, 2);

    // A formerly missing block must not leave an undefined entry that later
    // breaks another read's eviction or cached fast path.
    await fillCache(vlfs, "later", CACHE_BLOCKS + 1);
    assert.equal(vlfs._blockCache.has(`${entry.id}@0`), false);
    vlfs.seek(fd, offset, 0);
    assert.equal(vlfs.readCached(fd, 2), null);
    assertBytes(await vlfs.read(fd, 2), offset, 2);
    assertCacheValid(vlfs);

    // A request below the direct-read threshold can cover three blocks.
    const threeBlockOffset = BLOCK_SIZE - 17;
    const threeBlockLength = BLOCK_SIZE + 35;
    vlfs.seek(fd, threeBlockOffset, 0);
    assertBytes(await vlfs.read(fd, threeBlockLength), threeBlockOffset, threeBlockLength);
    vlfs.seek(fd, threeBlockOffset, 0);
    assertBytes(vlfs.readCached(fd, threeBlockLength), threeBlockOffset, threeBlockLength);
    assertCacheValid(vlfs);
    vlfs.close(fd);
});

test("another fd can evict a block while a cross-block read awaits its next block", {timeout: 5000}, async () => {
    const vlfs = loadVLFS();
    const started = deferred();
    const resume = deferred();
    const delayedBlob = new Blob([fixture]);
    const slice = delayedBlob.slice.bind(delayedBlob);
    delayedBlob.slice = (start, end) => {
        const chunk = slice(start, end);
        if(start === BLOCK_SIZE) {
            const arrayBuffer = chunk.arrayBuffer.bind(chunk);
            chunk.arrayBuffer = async () => {
                started.resolve();
                await resume.promise;
                return arrayBuffer();
            };
        }
        return chunk;
    };
    const {entry, fd} = openFixture(vlfs, "/delayed.xp3", delayedBlob);
    assertBytes(await vlfs.read(fd, 1), 0, 1);
    await fillCache(vlfs, "initial", CACHE_BLOCKS - 1);

    const offset = BLOCK_SIZE - 1;
    vlfs.seek(fd, offset, 0);
    const reading = vlfs.read(fd, 2);
    await started.promise;
    try {
        // Even a freshly touched first block is evicted during this await.
        await fillCache(vlfs, "interleaved", CACHE_BLOCKS);
        assert.equal(vlfs._blockCache.has(`${entry.id}@0`), false);
    } finally {
        resume.resolve();
    }
    assertBytes(await reading, offset, 2);
    assert.equal(vlfs.seek(fd, 0, 1), offset + 2);
    assertCacheValid(vlfs);
    vlfs.close(fd);
});

test("zero-length reads and the partial final block preserve cursor and bytes", async () => {
    const vlfs = loadVLFS();
    const {fd} = openFixture(vlfs, "/tail.xp3");
    const offset = fixture.length - 19;
    vlfs.seek(fd, offset, 0);
    assertBytes(await vlfs.read(fd, 0), offset, 0);
    assert.equal(vlfs.seek(fd, 0, 1), offset);
    assert.equal(vlfs._blockCache.size, 0, "a zero-length read does not load a block");
    assert.equal(vlfs.stats().blockCacheBytes, 0);
    assertBytes(await vlfs.read(fd, 100), offset, 19);
    assert.equal(vlfs.seek(fd, 0, 1), fixture.length);
    assertBytes(await vlfs.read(fd, 100), fixture.length, 0);
    assertBytes(vlfs.readCached(fd, 100), fixture.length, 0);

    vlfs.seek(fd, offset, 0);
    assertBytes(vlfs.readCached(fd, 0), offset, 0);
    assert.equal(vlfs.seek(fd, 0, 1), offset);
    assertBytes(vlfs.readCached(fd, 100), offset, 19);
    assert.equal(vlfs.seek(fd, 0, 1), fixture.length);
    assertCacheValid(vlfs);
    vlfs.close(fd);
});

test("a short source block rejects without advancing or poisoning a later retry", async () => {
    const vlfs = loadVLFS();
    const shortBlob = new Blob([fixture]);
    const slice = shortBlob.slice.bind(shortBlob);
    // Use the existing bytes but omit the final byte of the requested block.
    shortBlob.slice = (start, end) => slice(start, end - 1);
    const {fd} = openFixture(vlfs, "/short.xp3", shortBlob);
    const offset = BLOCK_SIZE + 13;
    vlfs.seek(fd, offset, 0);
    await assert.rejects(vlfs.read(fd, 32), /short or invalid block read/);
    assert.equal(vlfs.seek(fd, 0, 1), offset);
    assert.equal(vlfs._blockCache.size, 0);
    assert.equal(vlfs.stats().blockCacheBytes, 0);
    assertCacheValid(vlfs);

    shortBlob.slice = slice;
    assertBytes(await vlfs.read(fd, 32), offset, 32);
    assert.equal(vlfs.seek(fd, 0, 1), offset + 32);
    assertCacheValid(vlfs);
    vlfs.close(fd);
});

test("large reads bypass the block cache without changing returned data", async () => {
    const vlfs = loadVLFS();
    const {fd} = openFixture(vlfs, "/direct.xp3");
    const length = 2 * BLOCK_SIZE;
    assert.equal(vlfs.readCached(fd, length), null);
    assertBytes(await vlfs.read(fd, length), 0, length);
    assert.equal(vlfs.seek(fd, 0, 1), length);
    assert.equal(vlfs.stats().blockCacheBytes, 0);
    assert.equal(vlfs._blockCache.size, 0);
    vlfs.close(fd);
});
