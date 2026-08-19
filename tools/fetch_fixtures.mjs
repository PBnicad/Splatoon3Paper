#!/usr/bin/env node
// Fetch live splatoon3.ink data files into worker/test/fixtures/ for offline
// unit tests. One-shot dev tooling — run at most when fixtures need refresh
// (upstream data changes hourly; tests freeze time so fixtures stay valid).

import { mkdir, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const OUT = join(ROOT, "worker", "test", "fixtures");

const FILES = [
  ["schedules.json", "/data/schedules.json"],
  ["locale-zh-CN.json", "/data/locale/zh-CN.json"],
  ["festivals.json", "/data/festivals.json"],
  ["gear.json", "/data/gear.json"],
  ["coop.json", "/data/coop.json"],
];

const UA = "M5Paper-Splatoon-dev (fixture fetch; data credit: splatoon3.ink)";

await mkdir(OUT, { recursive: true });
for (const [name, path] of FILES) {
  const res = await fetch(new URL(path, "https://splatoon3.ink"), {
    headers: { "User-Agent": UA, Accept: "application/json" },
  });
  if (!res.ok) throw new Error(`${path} -> ${res.status}`);
  const text = await res.text();
  JSON.parse(text); // validate
  await writeFile(join(OUT, name), text);
  console.log(`${name}: ${text.length} bytes`);
}
