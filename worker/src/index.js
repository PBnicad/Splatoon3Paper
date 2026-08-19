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

// ---------------------------------------------------------------- /data/* --
async function handlePassthrough(request, url, ctx) {
  if (!DATA_PATH_RE.test(url.pathname)) {
    return jsonReply({ error: "forbidden path" }, 403);
  }
  const cache = caches.default;
  const hit = await cacheGet(cache, url);
  if (hit) {
    const res = hit.clone();
    res.headers.set("X-Cache", "HIT");
    return res;
  }
  const upstream = upstreamUrl(url.pathname + url.search);
  try {
    const res = await fetch(upstream, {
      method: "GET",
      headers: { "User-Agent": UPSTREAM_UA },
      redirect: "error",
      cf: { cacheEverything: true, cacheTtl: EDGE_TTL },
    });
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
    if (stale) {
      const res = stale.clone();
      res.headers.set("X-Cache", "STALE");
      return res;
    }
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

async function handleCompact(url, ctx) {
  const cache = caches.default;
  const hit = await cacheGet(cache, url);
  if (hit && !url.searchParams.has("nocache")) {
    const res = hit.clone();
    res.headers.set("X-Cache", "HIT");
    return res;
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
    const out = jsonReply(compact, 200, {
      "Cache-Control": "public, max-age=300",
      "X-Cache": "MISS",
    });
    const fresh = out.clone();
    fresh.headers.set("Cache-Control", "public, max-age=300");
    const stale = out.clone();
    stale.headers.set("Cache-Control", `public, max-age=${SHADOW_TTL}`);
    ctx.waitUntil(Promise.all([
      cache.put(new Request(url), fresh),
      cache.put(new Request(shadowUrl(url)), stale),
    ]));
    return out;
  } catch (e) {
    const stale = await cacheGet(cache, shadowUrl(url));
    if (stale) {
      const res = stale.clone();
      res.headers.set("X-Cache", "STALE");
      return res;
    }
    return jsonReply({ error: "upstream unavailable", detail: String(e?.message || e) }, 502);
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
      return handleCompact(url, ctx);
    }
    if (url.pathname.startsWith("/data/")) {
      return handlePassthrough(request, url, ctx);
    }
    if (url.pathname === "/") {
      return jsonReply({
        name: "splatoon3-m5paper-proxy",
        endpoints: ["/api/v1/compact", "/data/{schedules,festivals,gear,coop,locale/*}.json", "/healthz"],
        attribution: "data: splatoon3.ink",
      });
    }
    return jsonReply({ error: "not found" }, 404);
  },
};
