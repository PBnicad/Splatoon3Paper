# 部署手册

分两步：先部署 Cloudflare Worker（得到 api.splatoon.icu），再烧录并配置设备。

## 1. Cloudflare Worker

前置条件：

- 拥有 `splatoon.icu` 域名，且域名已托管（nameserver 接入）到你的 Cloudflare 账号；
- 本机装有 Node.js（≥18）。

步骤：

```bash
cd worker
npm install                 # 安装 wrangler
npx wrangler login          # 浏览器授权（首次）
npm test                    # 可选：跑 fixture 单测，确认 compact 逻辑正常
npx wrangler deploy         # 部署；wrangler.toml 里的 routes 会自动创建
                            # api.splatoon.icu 自定义域 + DNS 记录
```

> 若提示域名不在当前账号/路由冲突：先注释 `wrangler.toml` 里的 `routes` 段完成首次
> deploy，然后到 Cloudflare 控制台 → Workers → 该 Worker → Settings → Domains & Routes
> → Add Custom Domain 填 `api.splatoon.icu`。

验证：

```bash
curl -s https://api.splatoon.icu/healthz
curl -s https://api.splatoon.icu/api/v1/compact | head -c 400   # 应为中文精简 JSON
curl -sI https://api.splatoon.icu/data/schedules.json           # 透传 + X-Cache 头
```

本地调试（不部署）：`cd worker && npx wrangler dev`，然后访问
`http://127.0.0.1:8787/api/v1/compact`。

安全说明：Worker 仅向固定白名单主机 `https://splatoon3.ink` 的 `/data/` 路径发起
https 请求，并显式拒绝 localhost/环回/私网/保留地址，不存在被当作开放代理或内网
探测器的面（见 `worker/src/upstream.js`）。

## 2. 设备

1. USB 连接 M5Paper（串口驱动 CP2104/CH9102，装不上见
   [官方文档](https://docs.m5stack.com/en/core/M5Paper)）。
2. 烧录固件与字库：

   ```bash
   pio run -t upload      # 失败(超时)时：按住侧面按键再按 RESET 进下载模式重试
   pio run -t uploadfs    # 中文字库（LittleFS）
   ```

3. 配网（串口监视器里输入，保存后自动重启）：

   ```
   pio device monitor
   > wifi 你的SSID 你的密码
   ```

4. 重启后自动：连 WiFi → NTP 授时（写入 RTC）→ 拉取 compact → 渲染涂地页。

## 3. 日常维护

- **证书轮换**：设备持续离线且串口打印 `TLS fail` 时，更新 `src/certs.h`
  （GTS Root R4 / ISRG Root X1 双根已内置轮试，一般无需动）。
- **新增/修改界面文案**：`python tools/extract_ui_strings.py && python tools/make_font.py
  && pio run -t uploadfs`。
- **上游结构变化**：`node tools/fetch_fixtures.mjs` 拉最新样本 → `cd worker && npm test`
  定位 → 改 `worker/src/compact.js`。
- **改 Worker 后**：`npx wrangler deploy`（边缘缓存 10 分钟内自动过渡）。
