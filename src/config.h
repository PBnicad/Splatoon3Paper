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
constexpr const char* AnarchyTitle = "蛮颓比赛";
constexpr const char* NavRegular = "涂地";
constexpr const char* NavAnarchy = "真格";
constexpr const char* NavX = "x";
constexpr const char* NavEvents = "活动";
constexpr const char* NavFest = "祭典";
constexpr const char* NavSalmon = "打工";
constexpr const char* NavGear = "装备";
constexpr const char* NavSettings = "设置";
constexpr const char* GearTitle = "装备商店";
constexpr const char* MonthlyGear = "月度奖励装备";
constexpr const char* SettingsTitle = "设置";
constexpr const char* WifiSetup = "Wi-Fi 设置";
constexpr const char* AboutTitle = "关于";
constexpr const char* AboutHint = "项目介绍与开源地址";
constexpr const char* AboutGithub = "开源地址";
constexpr const char* GithubUrl = "github.com/PBnicad/Splatoon3Paper";
constexpr const char* TapBack = "点此返回";
constexpr const char* CurrentNetwork = "当前网络";
constexpr const char* NoWifi = "未配网";
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
constexpr const char* Offline = "离线";
constexpr const char* NoWifiLine1 = "未配置网络";
constexpr const char* NoWifiLine2 = "请到设置页配置 Wi-Fi";
constexpr const char* WelcomeHi = "欢迎使用";
constexpr const char* WelcomeBody1 = "这是一台斯普拉遁3日程墨水屏。";
constexpr const char* WelcomeBody2 = "连上 Wi-Fi 后，会自动拉取对战、";
constexpr const char* WelcomeBody3 = "打工、活动与祭典的最新日程。";
constexpr const char* StartWifi = "开始配网";
constexpr const char* Later = "稍后再说";
constexpr const char* Scanning = "正在扫描附近的网络…";
constexpr const char* PickWifi = "选择 Wi-Fi";
constexpr const char* Connecting = "正在连接…";
constexpr const char* ConnectOk = "连接成功";
constexpr const char* ConnectFail = "连接失败";
constexpr const char* CheckPass = "请检查密码后重试";
constexpr const char* RetryPass = "重新输入";
constexpr const char* OtherNet = "换一个网络";
constexpr const char* Refresh = "刷新";
constexpr const char* Back = "返回";
constexpr const char* Connect = "连接";
constexpr const char* EnterPass = "输入密码";
constexpr const char* OpenNet = "开放网络";
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
  bool autoFetch() const { return autoFetch_; }
  void setAutoFetch(bool on) {
    autoFetch_ = on;
    prefs_.putBool("autofetch", on);
  }
  bool onboarded() const { return onboarded_; }
  void setOnboarded(bool on) {
    onboarded_ = on;
    prefs_.putBool("onboarded", on);
  }

 private:
  Preferences prefs_;
  char ssid_[33] = {0};
  char pass_[65] = {0};
  bool autoFetch_ = true;
  bool onboarded_ = false;
};
