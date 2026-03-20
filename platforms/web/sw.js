/* KrKr2 Web — minimal service worker for PWA installability.
 * Uses network-only fetch so engine assets always stay fresh after rebuild. */
self.addEventListener('install', function () {
    self.skipWaiting();
});

self.addEventListener('activate', function (event) {
    event.waitUntil(self.clients.claim());
});

self.addEventListener('fetch', function (event) {
    event.respondWith(fetch(event.request));
});
