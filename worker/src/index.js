// M5Paper Splatoon3 schedule reverse proxy.
//
// Endpoints:
//   GET /api/v1/compact   — device-tailored trimmed JSON (edge-cached 10 min)
//   GET /data/<file>      — transparent, cached passthrough of splatoon3.ink data
//   GET /healthz          — liveness probe
//
// Caching strategy: Cloudflare edge cache in front of every upstream fetch;
// on upstream failure a 7-day "shadow" copy is served so the e-ink device
// always gets usable data.

import { fetchUpstreamJson, upstreamUrl, UPSTREAM_UA } from "./upstream.js";
import { buildCompact } from "./compact.js";
import { fetchAndCompress } from "./img.js";

const DATA_PATH_RE = /^\/data\/[A-Za-z0-9][A-Za-z0-9._\-/]*$/;
const EDGE_TTL = 600; // seconds; upstream data changes at most hourly
const SHADOW_TTL = 7 * 24 * 3600;

function shadowUrl(url) {
  const u = new URL(url);
  u.searchParams.set("__shadow", "1");
  return u;
}

async function cacheGet(cache, url) {
  try {
    return await cache.match(new Request(url, { method: "GET" }));
  } catch {
    return null;
  }
}

function jsonReply(obj, status, headers = {}) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8", ...headers },
  });
}

/** Re-serve a cached/stale response with an X-Cache header. Responses read
 *  back from the Cache API have immutable headers, so rebuild instead of
 *  mutating. */
function rewrap(res, cacheStatus) {
  const headers = new Headers();
  for (const [k, v] of res.headers) headers.set(k, v);
  headers.set("X-Cache", cacheStatus);
  return new Response(res.body, { status: res.status, headers });
}

// ---------------------------------------------------------------- /data/* --
async function handlePassthrough(request, url, ctx) {
  if (!DATA_PATH_RE.test(url.pathname)) {
    return jsonReply({ error: "forbidden path" }, 403);
  }
  const cache = caches.default;
  const hit = await cacheGet(cache, url);
  if (hit) return rewrap(hit, "HIT");
  const upstream = upstreamUrl(url.pathname + url.search);
  try {
    const res = await fetch(upstream, {
      method: "GET",
      headers: { "User-Agent": UPSTREAM_UA },
      redirect: "manual", // never follow redirects; 3xx rejected below
      cf: { cacheEverything: true, cacheTtl: EDGE_TTL },
    });
    if (res.status >= 300 && res.status < 400) {
      throw new Error(`upstream redirected to ${res.headers.get("location")}`);
    }
    if (res.ok) {
      const out = new Response(res.body, res);
      out.headers.set("Cache-Control", `public, max-age=${EDGE_TTL}`);
      out.headers.set("X-Cache", "MISS");
      const fresh = out.clone();
      fresh.headers.set("Cache-Control", `public, max-age=${EDGE_TTL}`);
      const stale = out.clone();
      stale.headers.set("Cache-Control", `public, max-age=${SHADOW_TTL}`);
      ctx.waitUntil(Promise.all([
        cache.put(new Request(url), fresh),
        cache.put(new Request(shadowUrl(url)), stale),
      ]));
      return out;
    }
    throw new Error(`upstream ${res.status}`);
  } catch (e) {
    const stale = await cacheGet(cache, shadowUrl(url));
    if (stale) return rewrap(stale, "STALE");
    return jsonReply({ error: "upstream unavailable" }, 502);
  }
}

// ---------------------------------------------------- /api/v1/compact --
const UPSTREAM_FILES = [
  ["schedules", "/data/schedules.json"],
  ["locale", "/data/locale/zh-CN.json"],
  ["festivals", "/data/festivals.json"],
  ["gear", "/data/gear.json"],
  ["coop", "/data/coop.json"],
];

/** Strong ETag from the body, so the device's If-None-Match turns the
 *  hourly poll into a 304 with no body transfer. */
async function bodyEtag(text) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  const hex = [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
  return `"c-${hex.slice(0, 24)}"`;
}

function notModified(etag) {
  return new Response(null, {
    status: 304,
    headers: { ETag: etag, "Cache-Control": "public, max-age=300" },
  });
}

async function handleCompact(request, url, ctx) {
  const inm = request.headers.get("If-None-Match");
  const cache = caches.default;
  // Canonical cache key: ?nocache must not fork the cache namespace.
  const cacheUrl = new URL(url);
  cacheUrl.searchParams.delete("nocache");
  const hit = await cacheGet(cache, cacheUrl);
  if (hit && !url.searchParams.has("nocache")) {
    if (inm && inm === hit.headers.get("ETag")) return notModified(inm);
    return rewrap(hit, "HIT");
  }
  try {
    const parts = await Promise.all(
      UPSTREAM_FILES.map(async ([key, path]) => {
        const r = await fetchUpstreamJson(path);
        return [key, r.json];
      }),
    );
    const input = Object.fromEntries(parts);
    const compact = buildCompact({ ...input, nowMs: Date.now() });
    const body = JSON.stringify(compact);
    const etag = await bodyEtag(body);
    if (inm && inm === etag) return notModified(etag);
    const out = new Response(body, {
      status: 200,
      headers: {
        "Content-Type": "application/json; charset=utf-8",
        "Cache-Control": "public, max-age=300",
        ETag: etag,
        "X-Cache": "MISS",
      },
    });
    const fresh = out.clone();
    const stale = out.clone();
    stale.headers.set("Cache-Control", `public, max-age=${SHADOW_TTL}`);
    ctx.waitUntil(Promise.all([
      cache.put(new Request(cacheUrl, { method: "GET" }), fresh),
      cache.put(new Request(shadowUrl(cacheUrl), { method: "GET" }), stale),
    ]));
    return out;
  } catch (e) {
    const stale = await cacheGet(cache, shadowUrl(cacheUrl));
    if (stale) return rewrap(stale, "STALE");
    return jsonReply({ error: "upstream unavailable", detail: String(e?.message || e) }, 502);
  }
}

// ----------------------------------------------------------- /api/v1/img --
async function handleImg(url, ctx) {
  const key = url.searchParams.get("k") || "";
  const cache = caches.default;
  const hit = await cacheGet(cache, url);
  if (hit) return rewrap(hit, "HIT");
  try {
    const body = await fetchAndCompress(key);
    const headers = {
      "Content-Type": "application/octet-stream",
      "Cache-Control": "public, max-age=86400",
      "X-Cache": "MISS",
    };
    const out = new Response(body, { status: 200, headers });
    ctx.waitUntil(cache.put(new Request(url), out.clone()));
    return out;
  } catch (e) {
    return jsonReply({ error: "img failed", detail: String(e?.message || e) }, 502);
  }
}

// ------------------------------------------------------------------ main --
export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (request.method !== "GET" && request.method !== "HEAD") {
      return jsonReply({ error: "method not allowed" }, 405);
    }
    if (url.pathname === "/healthz") {
      return jsonReply({ ok: true, ts: Math.floor(Date.now() / 1000) });
    }
    if (url.pathname === "/api/v1/compact" || url.pathname === "/api/v1/compact/") {
      return handleCompact(request, url, ctx);
    }
    if (url.pathname === "/api/v1/img" || url.pathname === "/api/v1/img/") {
      return handleImg(url, ctx);
    }
    if (url.pathname.startsWith("/data/")) {
      return handlePassthrough(request, url, ctx);
    }
    if (url.pathname === "/") {
      return jsonReply({
        name: "splatoon3-m5paper-proxy",
        endpoints: ["/api/v1/compact", "/api/v1/img?k=", "/data/{schedules,festivals,gear,coop,locale/*}.json", "/healthz"],
        attribution: "data: splatoon3.ink",
      });
    }
    return jsonReply({ error: "not found" }, 404);
  },
};
