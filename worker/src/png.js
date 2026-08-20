// Minimal 8-bit PNG decoder (non-interlaced). Inflates IDAT via
// DecompressionStream in Workers, or node:zlib when running tests.

const PNG_SIG = [137, 80, 78, 71, 13, 10, 26, 10];

function u32(b, o) {
  return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
}

async function inflateZlib(u8) {
  const ds = new DecompressionStream("deflate");
  const out = new Blob([u8]).stream().pipeThrough(ds);
  return new Uint8Array(await new Response(out).arrayBuffer());
}

function paeth(a, b, c) {
  const p = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

function unfilter(raw, w, h, bpp) {
  const stride = w * bpp;
  const out = new Uint8Array(stride * h);
  let src = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[src++];
    const row = y * stride;
    const prev = (y - 1) * stride;
    for (let i = 0; i < stride; i++) {
      const x = raw[src++];
      const a = i >= bpp ? out[row + i - bpp] : 0;
      const b = y > 0 ? out[prev + i] : 0;
      const c = y > 0 && i >= bpp ? out[prev + i - bpp] : 0;
      let v = x;
      if (f === 1) v = (x + a) & 255;
      else if (f === 2) v = (x + b) & 255;
      else if (f === 3) v = (x + ((a + b) >> 1)) & 255;
      else if (f === 4) v = (x + paeth(a, b, c)) & 255;
      else if (f !== 0) throw new Error(`png filter ${f}`);
      out[row + i] = v;
    }
  }
  return out;
}

/** Decode a PNG to {w,h,rgba Uint8Array} (4 bytes/pixel, opaque if no alpha). */
export async function decodePng(buf) {
  const b = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  for (let i = 0; i < 8; i++) {
    if (b[i] !== PNG_SIG[i]) throw new Error("not a png");
  }
  let w = 0, h = 0, depth = 0, ctype = 0;
  let plte = null;
  const idats = [];
  let off = 8;
  while (off + 12 <= b.length) {
    const len = u32(b, off);
    const type = String.fromCharCode(b[off + 4], b[off + 5], b[off + 6], b[off + 7]);
    const data = b.subarray(off + 8, off + 8 + len);
    if (type === "IHDR") {
      w = u32(data, 0);
      h = u32(data, 4);
      depth = data[8];
      ctype = data[9];
      if (data[10] !== 0 || data[12] !== 0) throw new Error("png interlace/compress");
      if (depth !== 8) throw new Error(`png depth ${depth}`);
    } else if (type === "PLTE") {
      plte = data;
    } else if (type === "IDAT") {
      idats.push(data);
    } else if (type === "IEND") {
      break;
    }
    off += 12 + len;
  }
  if (!w || !h) throw new Error("png ihdr");
  let idatLen = 0;
  for (const d of idats) idatLen += d.length;
  const idat = new Uint8Array(idatLen);
  let p = 0;
  for (const d of idats) {
    idat.set(d, p);
    p += d.length;
  }
  const inflated = await inflateZlib(idat);
  const bpp = ctype === 2 ? 3 : ctype === 6 ? 4 : ctype === 0 ? 1 : ctype === 4 ? 2 : ctype === 3 ? 1 : 0;
  if (!bpp) throw new Error(`png color ${ctype}`);
  const raw = unfilter(inflated, w, h, bpp);
  const rgba = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const s = i * bpp;
    let r, g, bl, a = 255;
    if (ctype === 6) {
      r = raw[s]; g = raw[s + 1]; bl = raw[s + 2]; a = raw[s + 3];
    } else if (ctype === 2) {
      r = raw[s]; g = raw[s + 1]; bl = raw[s + 2];
    } else if (ctype === 0) {
      r = g = bl = raw[s];
    } else if (ctype === 4) {
      r = g = bl = raw[s]; a = raw[s + 1];
    } else {
      const idx = raw[s] * 3;
      if (!plte || idx + 2 >= plte.length) {
        r = g = bl = 0;
      } else {
        r = plte[idx]; g = plte[idx + 1]; bl = plte[idx + 2];
      }
    }
    const o = i * 4;
    rgba[o] = r; rgba[o + 1] = g; rgba[o + 2] = bl; rgba[o + 3] = a;
  }
  return { w, h, rgba };
}
