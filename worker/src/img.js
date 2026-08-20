// Fetch splatnet / site PNGs, resize, convert to 4bpp grayscale "SNI1" for the
// e-ink firmware. Compression lives here so the device only blits pixels.

import { decodePng } from "./png.js";
import { assetPngUrl, buddyUrl, UPSTREAM_UA } from "./upstream.js";

export const SNI_MAGIC = "SNI1";

export const IMG_SIZE = {
  s: [248, 124],
  w: [64, 64],
  g: [72, 72],
  b: [168, 168],
};

const KEY_RE = /^([swgb]):([0-9a-f]{64}_[01]|buddy)$/i;

export function parseImgKey(k) {
  if (typeof k !== "string") return null;
  const m = k.trim().toLowerCase().match(KEY_RE);
  if (!m) return null;
  return { kind: m[1], file: m[2] };
}

function luma(r, g, b, a) {
  const y = 0.299 * r + 0.587 * g + 0.114 * b;
  const bg = 255; // composite over white for e-ink
  return y * (a / 255) + bg * (1 - a / 255);
}

/** Box-filter resize of RGBA → grayscale 0..255.
 *  cover=true: crop the source to dest aspect first (no independent stretch). */
export function resizeGray(src, sw, sh, dw, dh, { cover = false } = {}) {
  let ox = 0, oy = 0, rw = sw, rh = sh;
  if (cover && sw > 0 && sh > 0 && dw > 0 && dh > 0) {
    if (dw * sh > dh * sw) {
      rh = (sw * dh) / dw;
      oy = (sh - rh) / 2;
    } else if (dh * sw > dw * sh) {
      rw = (sh * dw) / dh;
      ox = (sw - rw) / 2;
    }
  }
  const out = new Uint8Array(dw * dh);
  for (let dy = 0; dy < dh; dy++) {
    const y0 = oy + (dy * rh) / dh;
    const y1 = oy + ((dy + 1) * rh) / dh;
    for (let dx = 0; dx < dw; dx++) {
      const x0 = ox + (dx * rw) / dw;
      const x1 = ox + ((dx + 1) * rw) / dw;
      let acc = 0, wt = 0;
      const xs = Math.floor(x0);
      const xe = Math.min(sw, Math.ceil(x1));
      const ys = Math.floor(y0);
      const ye = Math.min(sh, Math.ceil(y1));
      for (let y = ys; y < ye; y++) {
        const yA = Math.max(y0, y);
        const yB = Math.min(y1, y + 1);
        const yh = yB - yA;
        if (yh <= 0) continue;
        for (let x = xs; x < xe; x++) {
          const xA = Math.max(x0, x);
          const xB = Math.min(x1, x + 1);
          const xw = xB - xA;
          if (xw <= 0) continue;
          const i = (y * sw + x) * 4;
          const wgt = xw * yh;
          acc += luma(src[i], src[i + 1], src[i + 2], src[i + 3]) * wgt;
          wt += wgt;
        }
      }
      out[dy * dw + dx] = wt > 0 ? Math.max(0, Math.min(255, Math.round(acc / wt))) : 255;
    }
  }
  return out;
}

/** Pack 8-bit gray (0=black) into 4bpp high-nibble-first, white=15. */
export function pack4bpp(gray, w, h) {
  const n = w * h;
  const out = new Uint8Array((n + 1) >> 1);
  for (let i = 0; i < n; i++) {
    const nibble = gray[i] >> 4;
    if ((i & 1) === 0) out[i >> 1] = nibble << 4;
    else out[i >> 1] |= nibble;
  }
  return out;
}

export function encodeSni1(gray, w, h) {
  const pix = pack4bpp(gray, w, h);
  const buf = new Uint8Array(8 + pix.length);
  buf[0] = 83; buf[1] = 78; buf[2] = 73; buf[3] = 49; // SNI1
  buf[4] = w & 255;
  buf[5] = (w >> 8) & 255;
  buf[6] = h & 255;
  buf[7] = (h >> 8) & 255;
  buf.set(pix, 8);
  return buf;
}

export async function compressPngToSni(pngBytes, dw, dh) {
  const { w, h, rgba } = await decodePng(pngBytes);
  const gray = resizeGray(rgba, w, h, dw, dh, { cover: true });
  return encodeSni1(gray, dw, dh);
}

export async function fetchAndCompress(key) {
  const parsed = parseImgKey(key);
  if (!parsed) throw new Error("bad key");
  const [dw, dh] = IMG_SIZE[parsed.kind];
  const url = parsed.kind === "b" ? buddyUrl() : assetPngUrl(parsed.kind, parsed.file);
  const res = await fetch(url, {
    method: "GET",
    headers: { "User-Agent": UPSTREAM_UA, Accept: "image/png" },
    redirect: "manual",
    cf: { cacheEverything: true, cacheTtl: 86400 },
  });
  if (res.status >= 300 && res.status < 400) {
    throw new Error("asset redirected");
  }
  if (!res.ok) throw new Error(`asset ${res.status}`);
  const png = new Uint8Array(await res.arrayBuffer());
  if (png.length > 800000) throw new Error("asset too large");
  return compressPngToSni(png, dw, dh);
}
