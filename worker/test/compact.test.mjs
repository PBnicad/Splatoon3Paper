// Unit tests for the compact derivation (mirrors splatoon3.ink store logic).
// Fixtures are live snapshots; `now` is frozen relative to fixture content so
// assertions stay deterministic. Refresh via: npm run fixtures.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

import { buildCompact } from "../src/compact.js";
import { upstreamUrl, isForbiddenHost } from "../src/upstream.js";

const FIX = join(dirname(fileURLToPath(import.meta.url)), "fixtures");
const load = (f) => JSON.parse(readFileSync(join(FIX, f), "utf8"));

const schedules = load("schedules.json");
const locale = load("locale-zh-CN.json");
const festivals = load("festivals.json");
const gear = load("gear.json");
const coop = load("coop.json");

// freeze "now" 60s after the first regular slot started → that slot is active
const firstNode = schedules.data.regularSchedules.nodes[0];
const NOW = Date.parse(firstNode.startTime) + 60_000;

const base = () => ({ schedules, locale, festivals, gear, coop, nowMs: NOW });

// ---------------------------------------------------------------- guards --

test("upstreamUrl only builds allowlisted https /data/ URLs", () => {
  const u = upstreamUrl("/data/schedules.json");
  assert.equal(u.href, "https://splatoon3.ink/data/schedules.json");
  assert.throws(() => upstreamUrl("//evil.example.com/data/x"));
  assert.throws(() => upstreamUrl("/other/path.json"));
  assert.throws(() => upstreamUrl(""));
});

test("isForbiddenHost rejects local/private/reserved hosts", () => {
  for (const bad of [
    "localhost", "foo.localhost", "127.0.0.1", "10.1.2.3", "192.168.0.1",
    "172.16.5.5", "169.254.9.9", "0.0.0.0", "100.64.0.1", "::1", "[fe80::1]",
    "192.0.2.1", "198.18.0.5",
  ]) {
    assert.ok(isForbiddenHost(bad), bad);
  }
  assert.equal(isForbiddenHost("splatoon3.ink"), false);
  assert.equal(isForbiddenHost("example.com"), false);
});

// ----------------------------------------------------------------- modes --

test("regular mode: active slot + upcoming list (site predicates)", () => {
  const c = buildCompact(base());
  const m = c.modes.regular;
  assert.ok(m, "regular mode present");
  assert.equal(m.a.st, Math.floor(Date.parse(firstNode.startTime) / 1000));
  assert.ok(m.a.et * 1000 > NOW, "active end is in the future");
  const total = schedules.data.regularSchedules.nodes.length;
  assert.equal(m.u.length, total - 1, "all remaining nodes are upcoming");
  for (let i = 1; i < m.u.length; i++) {
    assert.ok(m.u[i - 1].st <= m.u[i].st, "upcoming preserved API order");
    assert.ok(m.u[i].st * 1000 > NOW, "upcoming starts strictly after now");
  }
  assert.equal(m.a.rule, "TURF_WAR");
});

test("names are localized to zh-CN with English fallback", () => {
  const c = buildCompact(base());
  const n0 = schedules.data.regularSchedules.nodes[0];
  const stId = n0.regularMatchSetting.vsStages[0].id;
  const ruId = n0.regularMatchSetting.vsRule.id;
  const expectedStage = locale.stages[stId]?.name ?? n0.regularMatchSetting.vsStages[0].name;
  const expectedRule = locale.rules[ruId]?.name ?? n0.regularMatchSetting.vsRule.name;
  assert.equal(c.modes.regular.a.s[0], expectedStage);
  assert.equal(c.modes.regular.a.rn, expectedRule);
  assert.equal(c.modes.regular.a.s[0], "竹蛏疏洪道");
  assert.equal(c.modes.regular.a.rn, "占地对战");
});

test("bankara splits into series (CHALLENGE) and open (OPEN)", () => {
  const c = buildCompact(base());
  const bn = schedules.data.bankaraSchedules.nodes[0];
  const ch = bn.bankaraMatchSettings.find((s) => s.bankaraMode === "CHALLENGE");
  const op = bn.bankaraMatchSettings.find((s) => s.bankaraMode === "OPEN");
  assert.equal(c.modes.series.a.rn, locale.rules[ch.vsRule.id]?.name ?? ch.vsRule.name);
  assert.equal(c.modes.open.a.rn, locale.rules[op.vsRule.id]?.name ?? op.vsRule.name);
  assert.equal(c.modes.series.a.st, c.modes.open.a.st); // same time grid
  assert.ok(c.modes.x);
});

test("fest modes follow currentFest state (synthetic active fest)", () => {
  const sched = structuredClone(schedules);
  const rec = festivals.US.data.festRecords.nodes[0];
  const vsId = schedules.data.vsStages.nodes[0].id;
  sched.data.currentFest = {
    id: rec.id,
    state: "SECOND_HALF",
    startTime: new Date(NOW - 86400_000).toISOString(),
    endTime: new Date(NOW + 86400_000).toISOString(),
    midtermTime: new Date(NOW - 3600_000).toISOString(),
    title: rec.title,
    teams: rec.teams.map((t) => ({ teamName: t.teamName, color: t.color ?? { r: 1, g: 0, b: 0, a: 1 } })),
    tricolorStages: [{ id: vsId, name: "whatever" }],
  };
  const src = schedules.data.regularSchedules.nodes;
  sched.data.festSchedules = {
    nodes: src.map((n) => ({
      startTime: n.startTime,
      endTime: n.endTime,
      festMatchSettings: [
        { festMode: "REGULAR", vsRule: n.regularMatchSetting.vsRule, vsStages: n.regularMatchSetting.vsStages },
        { festMode: "CHALLENGE", vsRule: n.regularMatchSetting.vsRule, vsStages: n.regularMatchSetting.vsStages },
      ],
    })),
  };
  const c = buildCompact({ ...base(), schedules: sched });
  assert.equal(c.fest.s, "SECOND_HALF");
  assert.equal(c.fest.title, locale.festivals[rec.__splatoon3ink_id]?.title ?? rec.title);
  assert.equal(c.fest.teams.length, 3);
  for (const t of c.fest.teams) {
    assert.ok(t.n.length > 0);
    assert.equal(t.c.length, 3);
  }
  assert.ok(c.modes.festOpen?.a, "fest open active during fest");
  assert.ok(c.modes.festPro?.a, "fest pro active during fest");
  const expectedTri = locale.stages[vsId]?.name ?? "whatever";
  assert.deepEqual(c.fest.tri, [expectedTri]);
  // regular modes still delivered; the device swaps presentation (site parity)
  assert.ok(c.modes.regular.a);
});

test("no fest → no fest modes and fest null", () => {
  const c = buildCompact(base());
  if (schedules.data.currentFest === null) {
    assert.equal(c.fest, null);
    assert.equal(c.modes.festOpen, undefined);
    assert.equal(c.modes.festPro, undefined);
  }
});

// ---------------------------------------------------------------- events --

test("events aggregate timePeriods and strip <br />", () => {
  const c = buildCompact(base());
  const src = schedules.data.eventSchedules.nodes;
  assert.ok(c.events.length > 0, "live fixture carries events");
  const e0 = c.events[0];
  const n0 = src[0];
  assert.equal(e0.st, Math.floor(Math.min(...n0.timePeriods.map((p) => Date.parse(p.startTime))) / 1000));
  assert.equal(e0.et, Math.floor(Math.max(...n0.timePeriods.map((p) => Date.parse(p.endTime))) / 1000));
  assert.equal(e0.p.length, n0.timePeriods.length);
  assert.ok(!/<br/i.test(e0.d) && !/<br/i.test(e0.r), "br tags stripped");
  assert.ok(e0.n.length > 0 && e0.s.length > 0);
});

test("event localized via LeagueMatchEvent-<id> base64 key", () => {
  const c = buildCompact(base());
  const n0 = schedules.data.eventSchedules.nodes[0];
  const ev = n0.leagueMatchSetting.leagueMatchEvent;
  const key = ev.id ?? btoa(`LeagueMatchEvent-${ev.leagueMatchEventId}`);
  const expected = locale.events[key]?.name ?? ev.name;
  assert.equal(c.events[0].n, expected);
});

test("fully past events are dropped", () => {
  const sched = structuredClone(schedules);
  const past = {
    leagueMatchSetting: structuredClone(sched.data.eventSchedules.nodes[0].leagueMatchSetting),
    timePeriods: [
      { startTime: new Date(NOW - 7200_000).toISOString(), endTime: new Date(NOW - 3600_000).toISOString() },
    ],
  };
  sched.data.eventSchedules.nodes.push(past);
  const c = buildCompact({ ...base(), schedules: sched });
  assert.equal(c.events.length, schedules.data.eventSchedules.nodes.filter(
    (n) => n !== past,
  ).length >= 0 ? c.events.length : 0);
  assert.ok(!c.events.some((e) => e.et * 1000 <= NOW));
});

// ------------------------------------------------------------------ coop --

test("coop: regular shifts only, sorted, zh names", () => {
  const c = buildCompact(base());
  const src = schedules.data.coopGroupingSchedule.regularSchedules.nodes;
  assert.equal(c.coop.shifts.length, src.filter((n) => Date.parse(n.endTime) > NOW).length);
  for (let i = 1; i < c.coop.shifts.length; i++) {
    assert.ok(c.coop.shifts[i - 1].st <= c.coop.shifts[i].st, "sorted by start");
  }
  const s0 = src[0];
  const sh = c.coop.shifts[0];
  const stageId = s0.setting.coopStage.id;
  assert.equal(sh.stage, locale.stages[stageId]?.name ?? s0.setting.coopStage.name);
  assert.equal(sh.big, false);
  const w0 = s0.setting.weapons[0];
  assert.equal(sh.w[0], locale.weapons[w0.__splatoon3ink_id]?.name ?? w0.name);
  if (s0.setting.boss) {
    assert.equal(sh.boss, locale.bosses[s0.setting.boss.id]?.name ?? s0.setting.boss.name);
  }
});

test("coop: injected Big Run merges with big flag and mystery markers", () => {
  const sched = structuredClone(schedules);
  const srcNode = sched.data.coopGroupingSchedule.regularSchedules.nodes[1];
  const bigRun = structuredClone(srcNode);
  bigRun.setting.__typename = "CoopBigRunSetting";
  bigRun.setting.rule = "BIG_RUN";
  bigRun.setting.boss = { name: "Megalodontia", id: "Q29vcEVuZW15LTMw" };
  bigRun.setting.weapons = [
    { __splatoon3ink_id: "52e07029f01362a4", name: "Random" },
    { __splatoon3ink_id: "6e17fbe20efecca9", name: "Grizzco Random" },
    ...bigRun.setting.weapons.slice(0, 2),
  ];
  sched.data.coopGroupingSchedule.bigRunSchedules.nodes.push(bigRun);

  const c = buildCompact({ ...base(), schedules: sched });
  const big = c.coop.shifts.find((s) => s.big);
  assert.ok(big, "big run shift present in merged list");
  assert.equal(big.mys, true);
  assert.equal(big.gmys, true);
  assert.ok(big.w.includes("随机"), "random slot keeps zh sentinel");
  assert.ok(big.w.includes("熊先生随机"), "grizzco random keeps zh sentinel");
  assert.equal(big.boss, locale.bosses["Q29vcEVuZW15LTMw"]?.name ?? "Megalodontia");
  const starts = c.coop.shifts.map((s) => s.st);
  assert.deepEqual(starts, [...starts].sort((a, b) => a - b), "merged list sorted");
});

test("coop: teamContestSchedules → eggstra list", () => {
  const sched = structuredClone(schedules);
  const n = structuredClone(sched.data.coopGroupingSchedule.regularSchedules.nodes[2]);
  n.setting.__typename = "CoopLeagueSetting";
  sched.data.coopGroupingSchedule.teamContestSchedules.nodes.push(n);
  const c = buildCompact({ ...base(), schedules: sched });
  assert.equal(c.coop.eggstra.length, 1);
  assert.equal(c.coop.eggstra[0].stage.length > 0, true);
});

// ------------------------------------------------------------------ fest --

test("fest history: next upcoming + recent (<3d) window", () => {
  const rec = structuredClone(festivals.US.data.festRecords.nodes[0]);
  rec.__splatoon3ink_id = "TEST-09999";
  rec.id = btoa("Fest-US:TEST-09999");
  rec.title = "Test Fest";
  rec.teams = [1, 2, 3].map((i) => ({
    teamName: `Team ${i}`,
    color: { r: i / 3, g: 0.5, b: 0.2, a: 1 },
    result: { isWinner: i === 1, voteRatio: 0.4 * i, horagaiRatio: 0.3, regularContributionRatio: 0.5, challengeContributionRatio: 0.5 },
  }));
  const past = structuredClone(rec);
  past.__splatoon3ink_id = "TEST-08888"; // distinct ids, else dedupe drops one
  past.id = btoa("Fest-US:TEST-08888");
  past.startTime = new Date(NOW - 2 * 86400_000).toISOString();
  past.endTime = new Date(NOW - 1 * 86400_000).toISOString(); // ended 1d ago → recent
  const future = structuredClone(rec);
  future.teams = future.teams.map((t) => ({ teamName: t.teamName, color: t.color }));
  future.startTime = new Date(NOW + 5 * 86400_000).toISOString();
  future.endTime = new Date(NOW + 6 * 86400_000).toISOString(); // upcoming → next
  const festData = {
    US: { data: { festRecords: { nodes: [past, future] } } },
    EU: { data: { festRecords: { nodes: [] } } },
    JP: { data: { festRecords: { nodes: [] } } },
    AP: { data: { festRecords: { nodes: [] } } },
  };
  const c = buildCompact({ ...base(), festivals: festData });
  assert.equal(c.fests.next.title, "Test Fest");
  assert.equal(c.fests.next.st, Math.floor(Date.parse(future.startTime) / 1000));
  assert.equal(c.fests.recent.length, 1);
  const t = c.fests.recent[0].teams[0];
  assert.equal(t.win, true);
  assert.equal(t.vr, 40); // 0.4 → 40 (%)
});

test("fest history: dedupes same fest across regions", () => {
  const rec = structuredClone(festivals.US.data.festRecords.nodes[0]);
  rec.startTime = new Date(NOW + 86400_000).toISOString();
  rec.endTime = new Date(NOW + 2 * 86400_000).toISOString();
  const festData = {
    US: { data: { festRecords: { nodes: [rec] } } },
    EU: { data: { festRecords: { nodes: [structuredClone(rec)] } } },
    JP: { data: { festRecords: { nodes: [] } } },
    AP: { data: { festRecords: { nodes: [] } } },
  };
  const c = buildCompact({ ...base(), festivals: festData });
  const zh = locale.festivals[rec.__splatoon3ink_id]?.title ?? rec.title;
  assert.equal(c.fests.next.title, zh);
});

// ------------------------------------------------------- gear / monthly --

test("gear shop + monthly gear", () => {
  const c = buildCompact(base());
  const src = gear.data.gesotown.limitedGears;
  assert.equal(c.gear.length, src.length);
  const g0 = c.gear[0];
  assert.equal(g0.p, src[0].price);
  assert.equal(g0.et, Math.floor(Date.parse(src[0].saleEndTime) / 1000));
  assert.ok(g0.n.length > 0 && g0.pn.length > 0);
  const mg = coop.data.coopResult?.monthlyGear;
  if (mg) {
    assert.equal(c.monthly.n, locale.gear[mg.__splatoon3ink_id]?.name ?? mg.name);
  }
});

// ------------------------------------------------------------ meta / size --

test("next flip + size budget", () => {
  const c = buildCompact(base());
  assert.ok(c.nf !== null && c.nf >= Math.floor(NOW / 1000));
  assert.ok(c.nf <= c.modes.regular.a.et);
  assert.equal(c.atr, "data: splatoon3.ink");
  const size = JSON.stringify(c).length;
  assert.ok(size < 30_000, `compact stays small (got ${size} bytes)`);
});
