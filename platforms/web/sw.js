/* KrKr2 Web — offline-capable service worker with precaching.
 *
 * BUILD_VERSION is replaced by CMake at configure time with a unique build id.
 * If not replaced (e.g. during development), falls back to a static string.
 * Changing this value triggers a new SW install and cache refresh. */
var CACHE_VERSION = '@KRKR2_BUILD_VERSION@';
if (CACHE_VERSION.charAt(0) === '@') CACHE_VERSION = 'dev-20260323';
var CACHE_NAME = 'krkr2-v' + CACHE_VERSION;
var ENGINE_ASSET_SUFFIX = '?v=' + encodeURIComponent(CACHE_VERSION);

/* Assets to precache during install.
 * These are relative to the SW scope (same directory as sw.js). */
var PRECACHE_ASSETS = [
    './',
    './index.html',
    './index.js' + ENGINE_ASSET_SUFFIX,
    './index.wasm' + ENGINE_ASSET_SUFFIX,
    './assets.zip' + ENGINE_ASSET_SUFFIX,
    './vlfs.js' + ENGINE_ASSET_SUFFIX,
    './manifest.webmanifest',
    './pwa/icon-192.png',
    './pwa/icon-512.png'
];

/* External resources to cache on first fetch (e.g. CDN libraries). */
var RUNTIME_CACHE_ORIGINS = [
    'https://cdn.jsdelivr.net'
];

self.addEventListener('install', function (event) {
    /* NOTE: deliberately NO self.skipWaiting() here.
     * Auto-activating mid-session deletes the old cache while a running page
     * still holds the old index.js in memory; the engine then fetches the NEW
     * index.wasm on game start and crashes on the js/wasm version mismatch
     * (e.g. "Cannot read properties of undefined (reading 'version')" in
     * _emscripten_glTexImage2D). The page (pwa-sw.html) prompts the user and
     * posts 'skipWaiting' for a consented, reload-coupled switchover. */
    event.waitUntil(
        caches.open(CACHE_NAME).then(function (cache) {
            console.log('[SW] Precaching ' + PRECACHE_ASSETS.length + ' assets (v' + CACHE_VERSION + ')');
            return cache.addAll(PRECACHE_ASSETS);
        })
    );
});

self.addEventListener('activate', function (event) {
    event.waitUntil(
        caches.keys().then(function (names) {
            return Promise.all(
                names
                    .filter(function (name) { return name.startsWith('krkr2-v') && name !== CACHE_NAME; })
                    .map(function (name) {
                        console.log('[SW] Deleting old cache:', name);
                        return caches.delete(name);
                    })
            );
        }).then(function () {
            return self.clients.claim();
        })
    );
});

self.addEventListener('fetch', function (event) {
    var request = event.request;

    /* Only handle GET requests */
    if (request.method !== 'GET') return;

    /* Navigation requests (HTML): network-first so updates propagate quickly,
     * but fall back to cache for offline access. */
    if (request.mode === 'navigate') {
        event.respondWith(
            fetch(request).then(function (response) {
                var clone = response.clone();
                caches.open(CACHE_NAME).then(function (cache) { cache.put(request, clone); });
                return response;
            }).catch(function () {
                return caches.match(request).then(function (cached) {
                    return cached || caches.match('./index.html');
                });
            })
        );
        return;
    }

    /* Same-origin assets: cache-first (WASM, JS, data are large & immutable per build) */
    var url = new URL(request.url);
    var isSameOrigin = url.origin === self.location.origin;

    /* CDN resources: cache on first fetch for offline */
    var isRuntimeCacheable = RUNTIME_CACHE_ORIGINS.some(function (origin) {
        return url.origin === origin;
    });

    if (isSameOrigin || isRuntimeCacheable) {
        event.respondWith(
            caches.match(request).then(function (cached) {
                if (cached) return cached;
                return fetch(request).then(function (response) {
                    if (response.ok) {
                        var clone = response.clone();
                        caches.open(CACHE_NAME).then(function (cache) { cache.put(request, clone); });
                    }
                    return response;
                });
            })
        );
        return;
    }

    /* All other requests: network only */
});

/* Listen for messages from the page */
self.addEventListener('message', function (event) {
    if (event.data === 'skipWaiting') {
        self.skipWaiting();
    }
});
