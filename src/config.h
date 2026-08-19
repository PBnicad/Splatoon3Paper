// Firmware-wide configuration & persisted settings (NVS via Preferences).

#pragma once

#include <Preferences.h>

// --- fixed deployment constants -----------------------------------------
constexpr const char* kApiHost = "api.splatoon.icu";
constexpr const char* kCompactPath = "/api/v1/compact";
// splatoon3.ink publishes fresh data ~hourly (:10-:20 past the hour)
constexpr uint32_t kFetchAtMinute = 25;       // past each hour
constexpr uint32_t kRetrySecs = 180;          // offline retry backoff
constexpr uint32_t kMaxModelAgeSec = 14 * 24 * 3600; // cache guard

// --- UI strings (official splatoon3.ink zh-CN terms) ---------------------
namespace ui {
constexpr const char* AppTitle = "斯普拉遁3 · 日程";
constexpr const char* ModeRegular = "一般比赛";
constexpr const char* ModeSeries = "蛮颓 · 系列";
constexpr const char* ModeOpen = "蛮颓 · 开放";
constexpr const char* ModeX = "X比赛";
constexpr const char* ModeFestOpen = "祭典 · 常规";
constexpr const char* ModeFestPro = "祭典 · 挑战";
constexpr const char* SalmonTitle = "鲑鱼跑";
constexpr const char* BigRun = "大型跑";
constexpr const char* Eggstra = "团队打工竞赛";
constexpr const char* EventsTitle = "活动比赛";
constexpr const char* FestTitle = "祭典";
constexpr const char* NowOpen = "举行中！";
constexpr const char* Next = "下次";
constexpr const char* Future = "下下次";
constexpr const char* Remaining = "还剩";
constexpr const char* Within = "内";
constexpr const char* Weapons = "发放武器";
constexpr const char* Tricolor = "三色夺宝攻击";
constexpr const char* Votes = "得票率";
constexpr const char* Conch = "法螺获得率";
constexpr const char* Open = "开放";
constexpr const char* Pro = "挑战";
constexpr const char* Won = "获胜";
constexpr const char* GearTitle = "装备商店";
constexpr const char* MonthlyGear = "月度奖励装备";
constexpr const char* Offline = "离线";
constexpr const char* Updated = "更新";
constexpr const char* NextRefresh = "下次刷新";
constexpr const char* Attribution = "data: splatoon3.ink";
constexpr const char* NoWifiLine1 = "未配置网络";
constexpr const char* NoWifiLine2 = "串口发送: wifi SSID 密码";
constexpr const char* SleepHint = "触摸唤醒";
constexpr const char* KingSalmonid = "王鲑";
constexpr const char* RandomWeapon = "随机";
constexpr const char* GrizzcoRandom = "熊先生随机";
constexpr const char* None = "暂无";
constexpr const char* Results = "结算";
constexpr const char* Midterm = "中期";
constexpr const char* Days = "天";
constexpr const char* Hours = "小时";
constexpr const char* Minutes = "分";
}  // namespace ui

// --- persisted settings ---------------------------------------------------
class Config {
 public:
  void begin();
  bool wifiConfigured() const { return ssid_[0] != 0; }
  const char* ssid() const { return ssid_; }
  const char* password() const { return pass_; }
  void setWifi(const char* ssid, const char* pass);

 private:
  Preferences prefs_;
  char ssid_[33] = {0};
  char pass_[65] = {0};
};
