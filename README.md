# M5Paper · 斯普拉遁3 日程墨水屏

把 [splatoon3.ink](https://splatoon3.ink) 的日程功能移植到 M5Paper v1.1
（540×960 墨水屏、16 级灰度、GT911 触摸）的独立固件。显示对战日程、
鲑鱼跑（含大型跑/团队打工竞赛）、活动比赛、祭典与鱿鱼商店，全部中文化。

<p align="center">
  <img src="tools/preview/live-p0.png" width="270" alt="涂地页实机画面">
</p>

```
M5Paper ──HTTPS──> Cloudflare Worker (api.splatoon.icu) ──HTTPS──> splatoon3.ink/data/*
                        │
                        ├─ /api/v1/compact   设备专用精简视图（~7KB，全中文）
                        └─ /data/*           纯透传反代 + 边缘缓存 + 故障影子缓存
```

- **数据**：splatoon3.ink 公开数据（[Data Access wiki](https://github.com/misenhower/splatoon3.ink/wiki/Data-Access)）。
  注意数据**不在 GitHub 仓库里**（仓库无 data/ 目录），上游实际是 splatoon3.ink 自身的 S3+Cloudflare，
  因此 Worker 直接代理 splatoon3.ink。
- **逻辑**：按 misenhower/splatoon3.ink（MIT）前端 store 逻辑在 Worker 端复刻
  （当前/未来时段判定、蛮颓系列/开放拆分、活动 timePeriods 聚合、打工合并排序、
  祭典 dedupe 与近期结果窗口），见 `worker/src/compact.js` 与 `worker/API.md`。
- **中文**：名称取自官方 `locale/zh-CN.json`（按 id 映射，英文兜底）；
  界面文案取自站点 zh-CN i18n；字库由 `tools/make_font.py` 离线生成（Noto Sans SC，
  1248 字符 4bpp 灰度点阵，刷入 LittleFS）。

## 页面（触摸左右 1/6 翻页，底部圆点跳页）

| 页 | 内容 |
|---|---|
| 1 总览 | 时钟/电量/WiFi/更新状态；四个模式当前时段卡（祭典期间按站点逻辑替换为祭典常规/挑战）；活动/祭典横幅；鲑鱼跑当前班次 |
| 2 时段 | 按模式分页签的未来 12 槽列表（每 2 小时一档，约 24 小时） |
| 3 鲑鱼跑 | 当前班次（地图/王鲑/4 武器/倒计时）、未来班次、大型跑 ▲ 标记、团队打工竞赛 |
| 4 活动/祭典 | 活动比赛详情（描述/规则/各场次）；祭典状态或下次祭典、三色占地、近期结算（得票率/获胜队） |
| 5 商店 | 鱿鱼限时装备（价格/下架时间/主能力）、打工月度奖励装备 |

其他交互：点头部时钟区立即刷新；点右下角署名区息屏（触摸唤醒）。
每分钟局部刷新头部（时钟+换挡倒计时）；每 10 次快速刷新自动做一次全刷去残影。

## 目录结构

```
worker/        Cloudflare Worker（wrangler）；npm test 跑 fixture 单测
tools/         make_font.py（字库）、fetch_fixtures.mjs、extract_ui_strings.py
data/          LittleFS 内容（font24/40/96.bin）→ pio run -t uploadfs
src/           固件（M5Unified + M5GFX + ArduinoJson v7）
docs/deploy.md 部署手册（Worker + 设备）
```

## 构建 / 烧录

```bash
pio run                    # 编译固件
pio run -t upload          # 烧录固件（需 USB 连接，失败时按住侧面键+RESET 进下载模式）
pio run -t uploadfs        # 刷入字库（LittleFS）
pio device monitor         # 串口 115200
```

首次配网（串口命令，保存到 NVS 后自动重启）：

```
wifi 你的SSID 你的密码
```

其他串口命令：`refetch`（强制刷新）、`page N`、`sleep`（息屏）、`status`（含固件版本）。

## 版本与发版

- 固件版本唯一来源：`platformio.ini` 的 `custom_fw_version`，构建时经
  `scripts/version.py` 注入 `FW_VERSION` 宏；启动串口横幅与「设置 → 关于」页会显示
  `vX.Y.Z`。
- 发版：把版本号改好并合并到 master 后，推送对应 tag 即自动构建发布：

  ```bash
  git tag v1.0.0 && git push origin v1.0.0
  ```

  GitHub Actions（release.yml）会校验 tag 与 `custom_fw_version` 一致，编译固件 +
  LittleFS 字库镜像，打包成 Release 附件：四个 bin（bootloader / partitions /
  firmware / littlefs）+ `FLASHING.md` 烧录说明。

字库更新流程：改了固件里的 UI 文案后：

```bash
python tools/extract_ui_strings.py   # 收集 src/ 中文文案进 tools/ui-strings.txt
python tools/make_font.py            # 重新生成 data/font*.bin（字符集含 zh-CN locale 全部名称）
pio run -t uploadfs
```

## 部署

见 `docs/deploy.md`（需要拥有 splatoon.icu 并托管在 Cloudflare，Worker 绑定自定义域
`api.splatoon.icu`；workers.dev 默认域在国内基本不可达）。

## 取数礼仪 / 署名

- 设备与 Worker 对 splatoon3.ink 的请求 ≤1 次/小时（数据本身每小时才更新），
  Worker 边缘缓存 10 分钟，自定义 User-Agent 标明来源。
- 屏幕页脚与 API 响应均标注 `data: splatoon3.ink`。
- 站点要求使用其数据的衍生品保持免费；逻辑复刻自 MIT 许可的官方前端仓库。

## 硬件 / 环境

M5Paper v1.1：ESP32-D0WDQ6-V3 @240MHz、16MB Flash（自定义分区：4MB app + 12MB FS）、
8MB PSRAM（画布与 JSON 缓冲）、BM8563 RTC（断网授时兜底）、GT911 触摸（G36 可深睡唤醒）。
PlatformIO `espressif32` + Arduino core 2.0.17；TLS 钉扎 GTS Root R4 与 ISRG Root X1
双根证书轮试（Cloudflare 轮换签发 CA 时自动切换，均失败则更新 `src/certs.h`）。
