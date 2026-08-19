// Upstream access for the splatoon3.ink data files.
//
// Security posture (SSRF):
//  - only https: URLs are ever constructed;
//  - the upstream host is a fixed allowlist (currently exactly one public
//    host: splatoon3.ink) — request paths are the only variable part;
//  - an explicit isForbiddenHost() assertion runs before every fetch and
//    rejects localhost / *.localhost / loopback / private / reserved
//    addresses, so even a future edit that widens the allowlist cannot turn
//    this worker into an internal-network probe.

export const UPSTREAM_ORIGIN = "https://splatoon3.ink";
export const UPSTREAM_UA =
  "M5Paper-Splatoon/1.0 (+https://api.splatoon.icu; data credit: splatoon3.ink)";

const UPSTREAM_HOSTS = new Set(["splatoon3.ink"]);

const IPV4_FORBIDDEN = [
  [0x00000000, 0xff000000], // 0.0.0.0/8            "this network"
  [0x0a000000, 0xff000000], // 10.0.0.0/8           private
  [0x64400000, 0xffc00000], // 100.64.0.0/10        shared (CGNAT)
  [0x7f000000, 0xff000000], // 127.0.0.0/8          loopback
  [0xa9fe0000, 0xffff0000], // 169.254.0.0/16       link-local
  [0xac100000, 0xfff00000], // 172.16.0.0/12        private
  [0xc0000000, 0xffffff00], // 192.0.0.0/24         reserved (DS-Lite etc.)
  [0xc0000200, 0xfffffffc], // 192.0.2.0/24         TEST-NET-1 (doc)
  [0xc6120000, 0xfffe0000], // 198.18.0.0/15        benchmark
  [0xc6336400, 0xfffffffc], // 198.51.100.0/24      TEST-NET-2 (doc)
  [0xcb007100, 0xffffff00], // 203.0.113.0/24       TEST-NET-3 (doc)
  [0xc0a80000, 0xffff0000], // 192.168.0.0/16       private
  [0xe0000000, 0xf0000000], // 224.0.0.0/4          multicast
  [0xf0000000, 0xf0000000], // 240.0.0.0/4          reserved
];

function ipv4ToLong(host) {
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(host)
    ? host.split(".").map(Number)
    : null;
  if (!m) return null;
  if (m.some((p) => Number.isNaN(p) || p > 255)) return null;
  return ((m[0] << 24) | (m[1] << 16) | (m[2] << 8) | m[3]) >>> 0;
}

function isForbiddenHost(hostname) {
  const h = hostname.toLowerCase().replace(/\.$/, "");
  if (h === "localhost" || h.endsWith(".localhost") || h.endsWith(".local") || h.endsWith(".internal")) {
    return true;
  }
  // IPv6 literals: the allowlist already refuses them; treat any bracket
  // literal as forbidden for defense in depth.
  if (h.includes(":")) return true;
  const n = ipv4ToLong(h);
  if (n !== null) {
    for (const [net, mask] of IPV4_FORBIDDEN) {
      if ((n & mask) === (net & mask)) return true;
    }
  }
  return false;
}

/** Build the https URL for an upstream data path, refusing anything that is
 *  not the fixed allowlisted public host. Throws on violation. */
export function upstreamUrl(path) {
  if (typeof path !== "string" || path === "") {
    throw new Error("upstream path required");
  }
  const u = new URL(path, UPSTREAM_ORIGIN);
  if (u.protocol !== "https:") throw new Error("upstream must be https");
  if (!u.pathname.startsWith("/data/")) throw new Error("upstream path must start with /data/");
  if (!UPSTREAM_HOSTS.has(u.hostname)) {
    throw new Error(`upstream host not allowlisted: ${u.hostname}`);
  }
  if (isForbiddenHost(u.hostname)) {
    throw new Error(`upstream host forbidden: ${u.hostname}`);
  }
  u.username = "";
  u.password = "";
  return u;
}

/** Fetch an upstream JSON file with Cloudflare edge caching.
 *  Returns { notModified, etag } for a 304, else { json, etag }. */
export async function fetchUpstreamJson(path, { etag } = {}) {
  const url = upstreamUrl(path);
  const headers = { "User-Agent": UPSTREAM_UA, Accept: "application/json" };
  if (etag) headers["If-None-Match"] = etag;
  const res = await fetch(url, {
    method: "GET",
    headers,
    redirect: "error", // never follow redirects to other hosts
    cf: { cacheEverything: true, cacheTtl: 600 },
  });
  if (res.status === 304) return { notModified: true, etag: res.headers.get("etag") || etag };
  if (!res.ok) throw new Error(`upstream ${path} -> ${res.status}`);
  return { json: await res.json(), etag: res.headers.get("etag") };
}

export { isForbiddenHost };
