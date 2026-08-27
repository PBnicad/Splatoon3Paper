// /api/v1/compact — device-tailored trimmed view of splatoon3.ink data.
//
// Derivation logic mirrors the splatoon3.ink frontend stores (MIT):
//   src/stores/time.mjs      — isCurrent / isActive / isUpcoming predicates
//   src/stores/schedules.mjs — per-mode setting pickers, coop merge, events
//   src/stores/splatfests.mjs— currentFest / tricolor window / recent fests
//   src/common/util.mjs      — br2nl
// Chinese names come from /data/locale/zh-CN.json keyed by the same ids,
// with the inline English values as fallback (same as the site's $t fallback).

const GRIZZCO_RANDOM_IDS = new Set(["6e17fbe20efecca9", "747937841598fff7"]);
const REGIONS = ["US", "EU", "JP", "AP"];
const RECENT_FEST_WINDOW_MS = 3 * 24 * 60 * 60 * 1000;

const BR_RE = /<br\s*\/?>/gi;
const br2nl = (s) => (typeof s === "string" ? s.replace(BR_RE, "\n") : s);
const epoch = (iso) => (iso ? Math.floor(Date.parse(iso) / 1000) : null);
const pick = (v, fallback) => (v === undefined || v === null || v === "" ? fallback : v);

const utf8 = new TextEncoder();

/** Truncate by UTF-8 byte budget on a code-point boundary. The firmware's
 *  fixed-size char buffers cut mid-codepoint with snprintf; cut here instead
 *  so the device never renders a broken glyph at the end of long fields. */
function cutUtf8(s, maxBytes) {
  if (typeof s !== "string" || s === "") return s ?? "";
  if (utf8.encode(s).length <= maxBytes) return s;
  let used = 0;
  let out = "";
  for (const cp of s) {
    const b = utf8.encode(cp).length;
    if (used + b > maxBytes) break;
    used += b;
    out += cp;
  }
  return out;
}

const isUpcoming = (startTime, nowMs) =>
  startTime ? Date.parse(startTime) > nowMs : false;

const IMG_HASH_RE = /\/([0-9a-f]{64}_[01])\.png(?:\?.*)?$/i;
const UPCOMING_CAP = 4; // ~8 hours of 2h slots — the 24h list overflowed the panel

export function imgKey(url, kind) {
  if (!url || typeof url !== "string") return "";
  const m = url.match(IMG_HASH_RE);
  return m ? `${kind}:${m[1].toLowerCase()}` : "";
}

/** localized name lookup: locale table keyed by GraphQL/base64 or
 *  __splatoon3ink_id, value like { name } (or a plain string). */
function locName(table, id, fallback) {
  if (!table || !id) return fallback;
  const e = table[id];
  if (!e) return fallback;
  if (typeof e === "string") return e;
  return pick(e.name, fallback);
}

function vsSlot(node, setting, loc) {
  const rule = setting.vsRule;
  const vs = setting.vsStages || [];
  const stages = vs.map((s) => cutUtf8(locName(loc?.stages, s.id, s.name), 39));
  return {
    st: epoch(node.startTime),
    et: epoch(node.endTime),
    rule: rule?.rule ?? null,
    rn: cutUtf8(locName(loc?.rules, rule?.id, rule?.name ?? ""), 27),
    s: stages,
    si: vs.map((s) => imgKey(s.image?.url, "s")),
  };
}

/** site logic: map nodes -> slots, keep only nodes with a non-null setting,
 *  then split into active / upcoming (original order preserved). */
function modeFromNodes(nodes, pickSetting, loc, nowMs) {
  const slots = [];
  for (const node of nodes || []) {
    const setting = pickSetting(node);
    if (!setting || !setting.vsRule || !setting.vsStages) continue;
    slots.push(vsSlot(node, setting, loc));
  }
  if (!slots.length) return null;
  // site predicates, evaluated on epoch seconds: active = start<=now<end
  const active =
    slots.find((s) => s.st * 1000 <= nowMs && s.et * 1000 > nowMs) ?? null;
  const upcoming = slots.filter((s) => s.st * 1000 > nowMs).slice(0, UPCOMING_CAP);
  return { a: active, u: upcoming };
}

function bankaraPicker(mode) {
  return (node) =>
    (node.bankaraMatchSettings || []).find((s) => s.bankaraMode === mode) ?? null;
}
function festPicker(mode) {
  return (node) => (node.festMatchSettings || []).find((s) => s.festMode === mode) ?? null;
}

/** Events (活动比赛): aggregate timePeriods to an overall window, keep the
 *  per-period list, keep events that have not fully ended. */
function buildEvents(data, loc, nowMs) {
  const out = [];
  for (const node of data.eventSchedules?.nodes || []) {
    const ls = node.leagueMatchSetting;
    const ev = ls?.leagueMatchEvent;
    if (!ls || !ev || !ls.vsRule) continue;
    const periods = (node.timePeriods || [])
      .map((p) => ({ st: epoch(p.startTime), et: epoch(p.endTime) }))
      .filter((p) => p.st !== null && p.et !== null);
    if (!periods.length) continue;
    const st = Math.min(...periods.map((p) => p.st));
    const et = Math.max(...periods.map((p) => p.et));
    if (et * 1000 <= nowMs) continue; // fully past — site only lists currentSchedules
    // locale.events is keyed by the base64 GraphQL id; schedules.json only
    // carries leagueMatchEventId, so rebuild "LeagueMatchEvent-<id>".
    const locKey =
      ev.id ?? btoa(`LeagueMatchEvent-${ev.leagueMatchEventId ?? ""}`);
    const le = loc?.events?.[locKey];
    out.push({
      st,
      et,
      n: cutUtf8(pick(le?.name, ev.name ?? ""), 46),      // firmware: char n[48]
      d: cutUtf8(br2nl(pick(le?.desc, ev.desc ?? "")), 190),   // char d[200]
      r: cutUtf8(br2nl(pick(le?.regulation, ev.regulation ?? "")), 250), // char r[260]
      rn: cutUtf8(locName(loc?.rules, ls.vsRule.id, ls.vsRule.name ?? ""), 27), // char rn[28]
      s: (ls.vsStages || []).map((x) => cutUtf8(locName(loc?.stages, x.id, x.name), 39)), // char s[40]
      si: (ls.vsStages || []).map((x) => imgKey(x.image?.url, "s")),
      p: periods.slice(0, 4),
    });
  }
  return out;
}

/** Salmon Run: merge regular + bigRun nodes, sort by start (site logic),
 *  flag mystery weapon sets; Eggstra Work kept in its own list. */
function coopShift(node, isBigRun, loc) {
  const s = node.setting || {};
  const weapons = s.weapons || [];
  // weapon names are localized for the device, but the two "random" slots
  // must stay recognizable — emit fixed zh sentinels for them
  const weaponName = (x) => {
    if (GRIZZCO_RANDOM_IDS.has(x?.__splatoon3ink_id)) return "熊先生随机";
    if (x?.name === "Random") return "随机";
    return locName(loc?.weapons, x?.__splatoon3ink_id, x?.name ?? "");
  };
  return {
    st: epoch(node.startTime),
    et: epoch(node.endTime),
    stage: cutUtf8(locName(loc?.stages, s.coopStage?.id, s.coopStage?.name ?? ""), 39),
    si: imgKey(s.coopStage?.thumbnailImage?.url || s.coopStage?.image?.url, "s"),
    boss: s.boss ? cutUtf8(locName(loc?.bosses, s.boss.id, s.boss.name ?? ""), 23) : null,
    w: weapons.map((x) => cutUtf8(weaponName(x), 31)),
    wi: weapons.map((x) => imgKey(x?.image?.url, "w")),
    big: !!isBigRun,
    mys: weapons.some((x) => x?.name === "Random"),
    gmys: weapons.some((x) => GRIZZCO_RANDOM_IDS.has(x?.__splatoon3ink_id)),
  };
}

function buildCoop(data, loc, nowMs) {
  const cg = data.coopGroupingSchedule || {};
  const shifts = [
    ...(cg.regularSchedules?.nodes || []).map((n) => coopShift(n, false, loc)),
    ...(cg.bigRunSchedules?.nodes || []).map((n) => coopShift(n, true, loc)),
  ]
    .filter((c) => c.st !== null && c.et !== null && c.et * 1000 > nowMs)
    .sort((a, b) => a.st - b.st);
  const eggstra = (cg.teamContestSchedules?.nodes || [])
    .map((n) => coopShift(n, false, loc))
    .filter((c) => c.st !== null && c.et !== null && c.et * 1000 > nowMs);
  return { shifts, eggstra };
}

function rgb255(color) {
  if (!color) return [0, 0, 0];
  return [
    Math.round((color.r ?? 0) * 255),
    Math.round((color.g ?? 0) * 255),
    Math.round((color.b ?? 0) * 255),
  ];
}

function decodeFestShortId(graphqlId) {
  // base64 "RmVzdC1VUzpKVUVBLTAwMTIz" -> "Fest-US:JUEA-00123" -> "JUEA-00123"
  try {
    const decoded = atob(graphqlId);
    const idx = decoded.indexOf(":");
    return idx >= 0 ? decoded.slice(idx + 1) : decoded;
  } catch {
    return null;
  }
}

function festTeam(t, locTeam) {
  return {
    n: cutUtf8(pick(locTeam?.teamName, t.teamName ?? ""), 27),
    c: rgb255(t.color),
  };
}

/** currentFest (祭典进行中): state / window / midterm / teams / tricolor. */
function buildCurrentFest(cf, loc) {
  if (!cf) return null;
  const shortId = decodeFestShortId(cf.id);
  const lf = shortId ? loc?.festivals?.[shortId] : null;
  const triSrc = cf.tricolorStages?.length
    ? cf.tricolorStages
    : cf.tricolorStage
      ? [cf.tricolorStage]
      : [];
  return {
    s: cf.state ?? null,
    st: epoch(cf.startTime),
    et: epoch(cf.endTime),
    mt: epoch(cf.midtermTime),
    title: cutUtf8(pick(lf?.title, cf.title ?? ""), 71),
    teams: (cf.teams || []).map((t, i) =>
      festTeam(t, Array.isArray(lf?.teams) ? lf.teams[i] : lf?.teams?.[String(i)]),
    ),
    tri: triSrc.map((x) => cutUtf8(locName(loc?.stages, x.id, x.name ?? ""), 39)),
  };
}

/** festivals.json: dedupe across regions (site HomeView logic), report the
 *  next upcoming fest and fests that ended within the last 3 days. */
function buildFestHistory(festivalsData, loc, nowMs) {
  if (!festivalsData) return { next: null, recent: [] };
  const seen = new Set();
  const all = [];
  for (const r of REGIONS) {
    for (const node of festivalsData[r]?.data?.festRecords?.nodes || []) {
      const key = node.__splatoon3ink_id || node.id;
      if (!key || seen.has(key)) continue;
      seen.add(key);
      all.push(node);
    }
  }
  const trim = (node) => {
    const lf = loc?.festivals?.[node.__splatoon3ink_id];
    const teams = (node.teams || []).map((t, i) => {
      const lt = Array.isArray(lf?.teams) ? lf.teams[i] : lf?.teams?.[String(i)];
      const res = t.result || {};
      const pct = (x) => (typeof x === "number" ? Math.round(x * 10000) / 100 : null);
      return {
        ...festTeam(t, lt),
        win: !!res.isWinner,
        vr: pct(res.voteRatio),
        hr: pct(res.horagaiRatio),
        ocr: pct(res.regularContributionRatio),
        ccr: pct(res.challengeContributionRatio),
        tcr: pct(res.tricolorContributionRatio),
      };
    });
    return {
      st: epoch(node.startTime),
      et: epoch(node.endTime),
      title: cutUtf8(pick(lf?.title, node.title ?? ""), 71),
      teams,
    };
  };
  const upcoming = all
    .filter((n) => isUpcoming(n.startTime, nowMs))
    .sort((a, b) => Date.parse(a.startTime) - Date.parse(b.startTime));
  const recent = all
    .filter(
      (n) =>
        n.endTime &&
        Date.parse(n.endTime) <= nowMs &&
        nowMs - Date.parse(n.endTime) < RECENT_FEST_WINDOW_MS,
    )
    .sort((a, b) => Date.parse(b.endTime) - Date.parse(a.endTime))
    .map(trim);
  return { next: upcoming.length ? trim(upcoming[0]) : null, recent };
}

function buildGear(gearData, loc) {
  const gears = gearData?.data?.gesotown?.limitedGears || [];
  return gears.map((g) => {
    const gear = g.gear || {};
    const power = gear.primaryGearPower || {};
    return {
      n: cutUtf8(locName(loc?.gear, gear.__splatoon3ink_id, gear.name ?? ""), 43),
      p: g.price ?? null,
      et: epoch(g.saleEndTime),
      k: gear.__typename ?? null,
      pn: cutUtf8(locName(loc?.powers, power.__splatoon3ink_id, power.name ?? ""), 25),
      i: imgKey(gear.image?.url, "g"),
    };
  });
}

function buildMonthlyGear(coopData, loc) {
  const mg = coopData?.data?.coopResult?.monthlyGear;
  if (!mg) return null;
  return { n: cutUtf8(locName(loc?.gear, mg.__splatoon3ink_id, mg.name ?? ""), 43) };
}

/**
 * @param {object} p
 * @param p.schedules  parsed /data/schedules.json
 * @param p.locale     parsed /data/locale/zh-CN.json (nullable)
 * @param p.festivals  parsed /data/festivals.json (nullable)
 * @param p.gear       parsed /data/gear.json (nullable)
 * @param p.coop       parsed /data/coop.json (nullable)
 * @param p.nowMs      evaluation time (epoch ms)
 */
export function buildCompact({ schedules, locale, festivals, gear, coop, nowMs }) {
  const data = schedules?.data || {};
  const loc = locale || {};
  const modes = {};
  const define = (key, nodes, picker) => {
    const m = modeFromNodes(nodes, picker, loc, nowMs);
    if (m) modes[key] = m;
  };
  define("regular", data.regularSchedules?.nodes, (n) => n.regularMatchSetting);
  define("series", data.bankaraSchedules?.nodes, bankaraPicker("CHALLENGE"));
  define("open", data.bankaraSchedules?.nodes, bankaraPicker("OPEN"));
  define("x", data.xSchedules?.nodes, (n) => n.xMatchSetting);
  define("festOpen", data.festSchedules?.nodes, festPicker("REGULAR"));
  define("festPro", data.festSchedules?.nodes, festPicker("CHALLENGE"));

  // earliest end among all active slots — the next schedule flip
  let nf = null;
  for (const m of Object.values(modes)) {
    if (m.a && (nf === null || m.a.et < nf)) nf = m.a.et;
  }
  const coopNow = buildCoop(data, loc, nowMs);
  for (const c of [...coopNow.shifts, ...coopNow.eggstra]) {
    if (c.st * 1000 <= nowMs && (nf === null || c.et < nf)) nf = c.et;
  }

  return {
    v: 1,
    gen: Math.floor(nowMs / 1000),
    nf,
    modes,
    fest: buildCurrentFest(data.currentFest, loc),
    fests: buildFestHistory(festivals, loc, nowMs),
    events: buildEvents(data, loc, nowMs),
    coop: coopNow,
    gear: buildGear(gear, loc),
    monthly: buildMonthlyGear(coop, loc),
    atr: "data: splatoon3.ink",
  };
}
