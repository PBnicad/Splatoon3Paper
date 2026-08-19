#include "model.h"

#include <ArduinoJson.h>
#include <string.h>

#include "config.h"

static void scopy(char* dst, size_t cap, JsonVariantConst v) {
  const char* s = v.as<const char*>();
  snprintf(dst, cap, "%s", s ? s : "");
}

static void parseSlot(Slot& s, JsonVariantConst v) {
  if (v.isNull()) return;
  s.st = v["st"] | 0u;
  s.et = v["et"] | 0u;
  scopy(s.rn, sizeof(s.rn), v["rn"]);
  JsonVariantConst ss = v["s"];
  if (ss.is<JsonArray>()) {
    if (ss.size() > 0) scopy(s.s1, sizeof(s.s1), ss[0]);
    if (ss.size() > 1) scopy(s.s2, sizeof(s.s2), ss[1]);
  }
}

static void parseTeam(Team& t, JsonVariantConst v) {
  if (v.isNull()) return;
  scopy(t.n, sizeof(t.n), v["n"]);
  JsonVariantConst c = v["c"];
  if (c.is<JsonArray>() && c.size() >= 3) {
    t.r = c[0] | 0;
    t.g = c[1] | 0;
    t.b = c[2] | 0;
  }
  t.win = v["win"] | false;
  auto ratio = [](JsonVariantConst v, const char* key, int16_t& out, bool& has) {
    JsonVariantConst x = v[key];
    has = !x.isNull();
    if (has) out = (int16_t)lroundf(x.as<float>() * 100);  // percent ×100
  };
  ratio(v, "vr", t.vr, t.hasVr);
  ratio(v, "hr", t.hr, t.hasHr);
  ratio(v, "ocr", t.ocr, t.hasOcr);
  ratio(v, "ccr", t.ccr, t.hasCcr);
  ratio(v, "tcr", t.tcr, t.hasTcr);
}

static void parseShift(Shift& s, JsonVariantConst v) {
  if (v.isNull()) return;
  s.st = v["st"] | 0u;
  s.et = v["et"] | 0u;
  scopy(s.stage, sizeof(s.stage), v["stage"]);
  scopy(s.boss, sizeof(s.boss), v["boss"]);
  JsonVariantConst ws = v["w"];
  if (ws.is<JsonArray>()) {
    for (int i = 0; i < ws.size() && i < 4; ++i) scopy(s.w[i], sizeof(s.w[i]), ws[i]);
  }
  s.big = v["big"] | false;
  s.mys = v["mys"] | false;
  s.gmys = v["gmys"] | false;
}

static void parseFestHist(FestHist& f, JsonVariantConst v) {
  if (v.isNull()) return;
  f.present = true;
  f.st = v["st"] | 0u;
  f.et = v["et"] | 0u;
  scopy(f.title, sizeof(f.title), v["title"]);
  JsonVariantConst ts = v["teams"];
  if (ts.is<JsonArray>()) {
    for (int i = 0; i < ts.size() && i < 3; ++i) parseTeam(f.teams[i], ts[i]);
    f.nTeams = ts.size() > 3 ? 3 : ts.size();
  }
}

const ModeSlots* Model::findMode(const char* key) const {
  for (int i = 0; i < nModes; ++i) {
    if (strcmp(modes[i].label, key) == 0) return &modes[i];
  }
  return nullptr;
}

bool modelParse(Model& m, const char* json, size_t len) {
  memset(&m, 0, sizeof(m));
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    Serial.printf("[model] json error: %s\n", err.c_str());
    return false;
  }

  m.gen = doc["gen"] | 0u;
  m.nf = doc["nf"] | 0u;

  struct ModeDef { const char* key; const char* label; };
  static const ModeDef defs[6] = {
      {"regular", ui::ModeRegular}, {"series", ui::ModeSeries},
      {"open", ui::ModeOpen},       {"x", ui::ModeX},
      {"festOpen", ui::ModeFestOpen}, {"festPro", ui::ModeFestPro},
  };
  JsonVariantConst modes = doc["modes"];
  for (const auto& d : defs) {
    JsonVariantConst mv = modes[d.key];
    if (mv.isNull()) continue;
    ModeSlots& ms = m.modes[m.nModes];
    ms.label = d.label;
    parseSlot(ms.a, mv["a"]);
    ms.hasA = !mv["a"].isNull();
    JsonVariantConst u = mv["u"];
    if (u.is<JsonArray>()) {
      for (int i = 0; i < u.size() && i < 12; ++i) parseSlot(ms.u[i], u[i]);
      ms.nu = u.size() > 12 ? 12 : u.size();
    }
    ++m.nModes;
  }

  JsonVariantConst fest = doc["fest"];
  if (!fest.isNull()) {
    m.fest.present = true;
    scopy(m.fest.state, sizeof(m.fest.state), fest["s"]);
    m.fest.st = fest["st"] | 0u;
    m.fest.et = fest["et"] | 0u;
    m.fest.mt = fest["mt"] | 0u;
    scopy(m.fest.title, sizeof(m.fest.title), fest["title"]);
    JsonVariantConst ts = fest["teams"];
    if (ts.is<JsonArray>()) {
      for (int i = 0; i < ts.size() && i < 3; ++i) parseTeam(m.fest.teams[i], ts[i]);
      m.fest.nTeams = ts.size() > 3 ? 3 : ts.size();
    }
    JsonVariantConst tri = fest["tri"];
    if (tri.is<JsonArray>()) {
      for (int i = 0; i < tri.size() && i < 2; ++i)
        scopy(m.fest.tri[i], sizeof(m.fest.tri[i]), tri[i]);
      m.fest.nTri = tri.size() > 2 ? 2 : tri.size();
    }
  }

  parseFestHist(m.festNext, doc["fests"]["next"]);
  JsonVariantConst recent = doc["fests"]["recent"];
  if (recent.is<JsonArray>()) {
    for (int i = 0; i < recent.size() && i < 2; ++i)
      parseFestHist(m.festRecent[m.nFestRecent++], recent[i]);
  }

  JsonVariantConst events = doc["events"];
  if (events.is<JsonArray>()) {
    for (int i = 0; i < events.size() && i < 4; ++i) {
      EventItem& e = m.events[m.nEvents];
      JsonVariantConst v = events[i];
      e.st = v["st"] | 0u;
      e.et = v["et"] | 0u;
      scopy(e.n, sizeof(e.n), v["n"]);
      scopy(e.d, sizeof(e.d), v["d"]);
      scopy(e.r, sizeof(e.r), v["r"]);
      scopy(e.rn, sizeof(e.rn), v["rn"]);
      JsonVariantConst ss = v["s"];
      if (ss.is<JsonArray>()) {
        if (ss.size() > 0) scopy(e.s1, sizeof(e.s1), ss[0]);
        if (ss.size() > 1) scopy(e.s2, sizeof(e.s2), ss[1]);
      }
      JsonVariantConst ps = v["p"];
      if (ps.is<JsonArray>()) {
        for (int k = 0; k < ps.size() && k < 10; ++k) {
          e.p[e.np].st = ps[k]["st"] | 0u;
          e.p[e.np].et = ps[k]["et"] | 0u;
          ++e.np;
        }
      }
      ++m.nEvents;
    }
  }

  JsonVariantConst shifts = doc["coop"]["shifts"];
  if (shifts.is<JsonArray>()) {
    for (int i = 0; i < shifts.size() && i < 8; ++i)
      parseShift(m.shifts[m.nShifts++], shifts[i]);
  }
  JsonVariantConst egg = doc["coop"]["eggstra"];
  if (egg.is<JsonArray>()) {
    for (int i = 0; i < egg.size() && i < 4; ++i)
      parseShift(m.eggstra[m.nEggstra++], egg[i]);
  }

  JsonVariantConst gear = doc["gear"];
  if (gear.is<JsonArray>()) {
    for (int i = 0; i < gear.size() && i < 6; ++i) {
      GearItem& g = m.gear[m.nGear];
      JsonVariantConst v = gear[i];
      scopy(g.n, sizeof(g.n), v["n"]);
      g.p = v["p"] | 0;
      g.et = v["et"] | 0u;
      scopy(g.pn, sizeof(g.pn), v["pn"]);
      ++m.nGear;
    }
  }

  JsonVariantConst monthly = doc["monthly"];
  if (!monthly.isNull()) {
    m.hasMonthly = true;
    scopy(m.monthly, sizeof(m.monthly), monthly["n"]);
  }

  return m.nModes > 0 || m.nShifts > 0 || m.nEvents > 0;
}
