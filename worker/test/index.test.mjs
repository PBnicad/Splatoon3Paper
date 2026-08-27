// End-to-end tests of the worker fetch handler with a stubbed Cache API and
// upstream fetch — focused on the /api/v1/compact ETag / 304 contract.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import worker from "../src/index.js";

const FIXTURES = {
  "/data/schedules.json": "schedules.json",
  "/data/locale/zh-CN.json": "locale-zh-CN.json",
  "/data/festivals.json": "festivals.json",
  "/data/gear.json": "gear.json",
  "/data/coop.json": "coop.json",
};
const fix = (p) =>
  readFileSync(new URL(`./fixtures/${FIXTURES[p]}`, import.meta.url), "utf8");

function makeCache() {
  const map = new Map();
  return {
    async match(req) {
      return map.get(new Request(req.url, { method: "GET" }).url) ?? null;
    },
    async put(req, res) {
      map.set(req.url, res);
    },
  };
}

function withEnv(fn) {
  return async () => {
    const cache = makeCache();
    const pending = [];
    const ctx = { waitUntil: (p) => pending.push(p.catch(() => {})) };
    const prevCaches = globalThis.caches;
    const prevFetch = globalThis.fetch;
    globalThis.caches = { default: cache };
    globalThis.fetch = async (url) => {
      const path = new URL(url).pathname;
      if (!FIXTURES[path]) throw new Error(`unexpected upstream ${path}`);
      return new Response(fix(path), { status: 200, headers: { ETag: `"u-${path.length}"` } });
    };
    try {
      await fn({ cache, ctx, pending });
    } finally {
      globalThis.caches = prevCaches;
      globalThis.fetch = prevFetch;
    }
  };
}

const get = (url, headers = {}) =>
  new Request(url, { method: "GET", headers });

test("compact MISS returns 200 with a strong ETag and a parseable body", withEnv(async ({ ctx }) => {
  const res = await worker.fetch(
    get("https://api.splatoon.icu/api/v1/compact"),
    {},
    ctx,
  );
  assert.equal(res.status, 200);
  assert.equal(res.headers.get("X-Cache"), "MISS");
  const etag = res.headers.get("ETag");
  assert.match(etag, /^"c-[0-9a-f]{24}"$/);
  const body = await res.json();
  assert.ok(body.modes?.regular, "regular mode present");
}));

test("compact HIT with matching If-None-Match returns 304 with no body", withEnv(async ({ ctx, pending }) => {
  const first = await worker.fetch(
    get("https://api.splatoon.icu/api/v1/compact"),
    {},
    ctx,
  );
  const etag = first.headers.get("ETag");
  await Promise.all(pending.splice(0));

  const again = await worker.fetch(
    get("https://api.splatoon.icu/api/v1/compact", { "If-None-Match": etag }),
    {},
    ctx,
  );
  assert.equal(again.status, 304);
  assert.equal(again.headers.get("ETag"), etag);
  assert.equal(await again.text(), "");
}));

test("compact HIT with a stale If-None-Match still returns 200 + HIT", withEnv(async ({ ctx, pending }) => {
  await worker.fetch(get("https://api.splatoon.icu/api/v1/compact"), {}, ctx);
  await Promise.all(pending.splice(0));

  const res = await worker.fetch(
    get("https://api.splatoon.icu/api/v1/compact", { "If-None-Match": '"old"' }),
    {},
    ctx,
  );
  assert.equal(res.status, 200);
  assert.equal(res.headers.get("X-Cache"), "HIT");
  assert.match(res.headers.get("ETag"), /^"c-[0-9a-f]{24}"$/);
}));

test("healthz stays trivially alive", async () => {
  const res = await worker.fetch(get("https://api.splatoon.icu/healthz"), {}, { waitUntil() {} });
  assert.equal(res.status, 200);
  assert.ok((await res.json()).ok);
});
