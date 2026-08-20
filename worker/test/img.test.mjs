import { test } from "node:test";
import assert from "node:assert/strict";
import { compressPngToSni, parseImgKey, IMG_SIZE, resizeGray, containSize } from "../src/img.js";
import { assetPngUrl, buddyUrl } from "../src/upstream.js";

// 1x1 red PNG
const RED = Buffer.from(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==",
  "base64",
);

test("parseImgKey accepts hashed splatnet keys", () => {
  const k = parseImgKey("s:9b1c17b2075479d0397d2fb96efbc6fa3a28900712920e5fe1e9dfc59c6abc5c_1");
  assert.equal(k.kind, "s");
  assert.ok(parseImgKey("b:buddy"));
  assert.ok(parseImgKey("b:buddy2"));
  assert.equal(parseImgKey("s:../x"), null);
  assert.equal(parseImgKey("http://evil"), null);
});

test("buddy and asset URLs stay on splatoon3.ink", () => {
  assert.equal(buddyUrl().hostname, "splatoon3.ink");
  const u = assetPngUrl("w", "7175449ebf69cd8c6125538e08682750b71f39403dc0ca336d58c64a48c4cc18_0");
  assert.equal(u.hostname, "splatoon3.ink");
  assert.match(u.pathname, /weapon_illust/);
});

test("compressPngToSni emits SNI1 4bpp", async () => {
  const out = await compressPngToSni(RED, 2, 2);
  assert.equal(String.fromCharCode(...out.subarray(0, 4)), "SNI1");
  assert.equal(out[4] | (out[5] << 8), 2);
  assert.equal(out[6] | (out[7] << 8), 2);
  assert.ok(out.length > 8);
});

test("canonical sizes exist for every kind", () => {
  for (const k of ["s", "w", "g", "b"]) {
    assert.equal(IMG_SIZE[k].length, 2);
  }
});

test("contain size keeps original aspect", () => {
  const [dw, dh] = containSize(131, 175, 320, 320);
  assert.equal(dh, 320);
  assert.ok(Math.abs(dw / dh - 131 / 175) < 0.01);
  const [a, b] = containSize(175, 131, 320, 320);
  assert.equal(a, 320);
  assert.ok(Math.abs(a / b - 175 / 131) < 0.01);
});

test("contain resize does not stretch", () => {
  // 6×2: left 3 black, right 3 white. Dest 3×1 is the contained size in a 3×3 box.
  const src = new Uint8Array(6 * 2 * 4);
  for (let y = 0; y < 2; y++) {
    for (let x = 0; x < 6; x++) {
      const o = (y * 6 + x) * 4;
      const v = x < 3 ? 0 : 255;
      src[o] = src[o + 1] = src[o + 2] = v;
      src[o + 3] = 255;
    }
  }
  const out = resizeGray(src, 6, 2, 3, 1);
  assert.ok(out[0] < 40, "left stays black");
  assert.ok(out[2] > 215, "right stays white");
});

test("cover crop keeps aspect instead of stretching", () => {
  // 6×2: left 3 cols black, right 3 white. Dest 2×2 is square so cover crops
  // the center 2×2 (cols 2–3): black | white, no blending of the far sides.
  const src = new Uint8Array(6 * 2 * 4);
  for (let y = 0; y < 2; y++) {
    for (let x = 0; x < 6; x++) {
      const o = (y * 6 + x) * 4;
      const v = x < 3 ? 0 : 255;
      src[o] = src[o + 1] = src[o + 2] = v;
      src[o + 3] = 255;
    }
  }
  const covered = resizeGray(src, 6, 2, 2, 2, { cover: true });
  assert.ok(covered[0] < 40, "cover left is black (col 2)");
  assert.ok(covered[1] > 215, "cover right is white (col 3)");
});
