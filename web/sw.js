/* =====================================================================
   DaemonLink Service Worker
   - Precaches the static shell so the console works in airplane mode.
   - Strategy:
       * Navigation requests  -> network-first, fall back to cached shell.
       * Same-origin assets   -> cache-first, fall back to network.
       * Cross-origin / other -> straight to network (no caching).
   - Bump CACHE_VERSION whenever the shell changes; old caches are purged
     on `activate`.
   ===================================================================== */
"use strict";

const CACHE_VERSION = "daemonlink-shell-v2";
const SHELL_ASSETS = [
  "./",
  "./index.html",
  "./manifest.json",
  "./icon.svg",
  "./icon-maskable.svg",
];

// -------- install: warm the cache ------------------------------------
self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_VERSION).then((cache) => cache.addAll(SHELL_ASSETS))
  );
  // Skip waiting so a fresh SW takes over without requiring a tab close.
  self.skipWaiting();
});

// -------- activate: drop stale caches --------------------------------
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(
        keys
          .filter((k) => k !== CACHE_VERSION)
          .map((k) => caches.delete(k))
      )
    )
  );
  self.clients.claim();
});

// -------- fetch: routing ---------------------------------------------
self.addEventListener("fetch", (event) => {
  const req = event.request;

  // Only handle GET. POST/PUT/etc. are passthrough.
  if (req.method !== "GET") return;

  const url = new URL(req.url);

  // Web Serial uses no HTTP traffic, but if anything else hits a
  // foreign origin we just let it through untouched.
  if (url.origin !== self.location.origin) return;

  // Navigation request (e.g. user opens the PWA): try network first
  // so an updated index.html is fetched when online, but fall back
  // to the cached shell when offline.
  if (req.mode === "navigate") {
    event.respondWith(
      fetch(req)
        .then((res) => {
          // Refresh the cache in the background.
          const copy = res.clone();
          caches.open(CACHE_VERSION).then((c) => c.put("./index.html", copy));
          return res;
        })
        .catch(() =>
          caches.match("./index.html", { ignoreSearch: true })
        )
    );
    return;
  }

  // Static asset: cache-first, network fallback (and populate cache
  // for future offline boots).
  event.respondWith(
    caches.match(req).then((cached) => {
      if (cached) return cached;
      return fetch(req).then((res) => {
        // Only cache successful basic responses.
        if (res.ok && res.type === "basic") {
          const copy = res.clone();
          caches.open(CACHE_VERSION).then((c) => c.put(req, copy));
        }
        return res;
      });
    })
  );
});

// -------- message channel: allow the page to force-update the SW ----
self.addEventListener("message", (event) => {
  if (event.data === "SKIP_WAITING") self.skipWaiting();
});
