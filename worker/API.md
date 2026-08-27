# api.splatoon.icu — 设备端 API 契约

Cloudflare Worker 反向代理 [splatoon3.ink](https://splatoon3.ink) 的公开数据
（[Data Access wiki](https://github.com/misenhower/splatoon3.ink/wiki/Data-Access)），
并按站点前端逻辑（MIT）加工出设备专用精简视图。
上游数据每小时更新一次，本代理边缘缓存 10 分钟；上游故障时回退 7 天影子缓存。

## 端点

| 路径 | 说明 |
|---|---|
| `GET /api/v1/compact` | 设备专用精简 JSON（见下），`Cache-Control: max-age=300`，带强 `ETag`（body SHA-256 前 24 hex），`If-None-Match` 命中时返回 `304` 空体 |
| `GET /api/v1/img?k=` | Worker 压缩后的 4bpp 灰度图（SNI1）。`k` 形如 `s:{64hex}_{0\|1}` / `w:` / `g:` / `b:buddy` |
| `GET /data/<file>` | splatoon3.ink 原样透传（仅 `/data/` 白名单路径），`max-age=600` |
| `GET /healthz` | 存活探测 |

响应头 `X-Cache: HIT|MISS|STALE` 表明边缘缓存/影子缓存命中情况。
`?nocache=1` 可跳过 compact 边缘缓存强制重建（缓存键不含该参数）。

## /api/v1/compact 结构（v1）

所有时间均为 **UTC epoch 秒**（键名 `st`/`et` = start/end）。文本已合并
`locale/zh-CN.json` 官方中文名，缺失时回退英文原名。`nf` = 所有活跃时段中
最早的结束时间（下一次"换挡"），设备可据此安排刷新。

```jsonc
{
  "v": 1,
  "gen": 1755571234,          // 生成时刻
  "nf": 1755578400,           // 下一次时段切换（所有模式/打工取最早）
  "atr": "data: splatoon3.ink",

  "modes": {                  // 对战模式；祭典期间额外出现 festOpen/festPro
    "regular": { "a": Slot, "u": [Slot, ...] },   // 涂地
    "series":  { "a": Slot, "u": [Slot, ...] },   // 蛮颓 系列 (bankaraMode=CHALLENGE)
    "open":    { "a": Slot, "u": [Slot, ...] },   // 蛮颓 开放 (bankaraMode=OPEN)
    "x":       { "a": Slot, "u": [Slot, ...] },   // X 赛
    "festOpen":{ "a": Slot, "u": [Slot, ...] },   // 祭典(常规) 仅祭典期间存在
    "festPro": { "a": Slot, "u": [Slot, ...] }    // 祭典(专业) 仅祭典期间存在
  },
  // Slot = { "st":…, "et":…, "rule":"TURF_WAR", "rn":"占地对战", "s":["图1","图2"],
  //          "si":["s:{hash}_1", ...] }
  // a = 当前时段；u = 未来最多 4 槽（约 8 小时，避免把屏撑爆）

  "fest": null,               // 进行中/即将开始的祭典 (schedules.currentFest)
  // fest = { "s":"FIRST_HALF|SECOND_HALF", "st":…, "et":…, "mt":…(中期),
  //          "title":"…", "teams":[{"n":"队名","c":[r,g,b]}×3], "tri":["三色图"] }

  "fests": {                  // 来自 festivals.json（四区去重）
    "next":  null,            // 下一个未开始的祭典（可能提前数月）
    "recent": []              // 结束未满 3 天的祭典 + 结果
  },
  // recent 条目 team = { n, c, win, vr(票数%), hr(海螺%), ocr(开放贡献%),
  //                      ccr(专业贡献%), tcr(三色贡献%|null) }

  "events": [                 // 活动比赛（未完全结束的）
  //  { "st":…, "et":…,        // 所有 timePeriods 的 min/max
  //    "n":"名称", "d":"描述", "r":"规则说明(已去<br>)",
  //    "rn":"真格鱼虎对战", "s":["图1","图2"],
  //    "p":[{"st":…,"et":…}] } // 各场次窗口
  ],

  "coop": {                   // 打工（未结束班次，按开始时间升序）
    "shifts": [ /* Shift */ ],     // 常规 + 大型跑 合并排序
    "eggstra": [ /* Shift */ ]     // 团队打工 (teamContestSchedules)
  },
  // Shift = { "st":…, "et":…, "stage":"新卷堡", "boss":"横纲"|null,
  //           "w":["武器×4"], "big":bool(大型跑),
  //           "mys":bool(含随机武器), "gmys":bool(含熊先生随机) }

  "gear": [ // 鱿鱼商店限时装备(≤6)
  //  { "n":"名称", "p":价格, "et":下架时间, "k":"HeadGear|ClothingGear|ShoesGear",
  //    "pn":"主能力" }
  ],
  "monthly": { "n":"打工月度奖励装备" } | null
}
```

## 与站点逻辑的对齐点

- 时段判定：`active = start<=now<end`，`upcoming = start>now`（顺序保持）。
- 蛮颓拆分按 `bankaraMatchSettings[].bankaraMode`；祭典按 `festMatchSettings[].festMode`。
- 祭典期间站点首页用祭典框替换常规框——compact 两种都下发，由设备决定展示替换。
- 活动比赛整体窗口 = `timePeriods` min/max；完全过去的活动被丢弃。
- 打工 = `regularSchedules + bigRunSchedules` 合并按 `startTime` 排序；
  `teamContestSchedules` 独立为 eggstra。
- 名称本地化：`locale/zh-CN.json` 按 id（base64 或 `__splatoon3ink_id`）映射，
  活动条目键为 `base64("LeagueMatchEvent-" + leagueMatchEventId)`，英文兜底。
- 文本 `desc/regulation` 去除 `<br />`（站点 br2nl 正则）。
- 所有文本字段按设备端定长缓冲做 UTF-8 字节预算截断（码点边界对齐），
  避免固件 `snprintf` 从多字节字符中间截断产生乱码。

## 安全

- 上游固定 `https://splatoon3.ink`，路径必须以 `/data/` 开头； Worker 永远
  不会向 localhost/环回/私网/保留地址或任何其他主机发起请求（allowlist +
  显式拒绝双重校验，见 `src/upstream.js`）。
- 不接受来自请求的任何目标主机参数，不存在开放代理面。
