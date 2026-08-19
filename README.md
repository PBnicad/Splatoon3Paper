# M5Paper v1.1 · PlatformIO 工程

基于官方 M5Stack 库的 [M5Paper](https://docs.m5stack.com/en/core/M5Paper) v1.1 开发工程。

## 硬件

| 项目 | 规格 |
|---|---|
| SoC | ESP32-D0WDQ6-V3 @ 240MHz |
| Flash | 16MB（`default_16MB.csv` 分区表） |
| PSRAM | 8MB Quad-SPI（`-DBOARD_HAS_PSRAM`） |
| 屏幕 | 4.7" 540×960 墨水屏（IT8951），16 级灰度 |
| 触摸 | GT911 电容触摸 |
| 其他 | SHT30 温湿度、BM8563 RTC、FM24C02 EEPROM、1150mAh 电池 |

## 库选型说明（为什么不是 M5EPD）

M5Paper 老的专用库 [M5EPD](https://github.com/m5stack/M5EPD) 已于 2025-07 归档，
仓库首页明确标注 **Deprecated — Use M5GFX & M5Unified**。
本工程采用官方现行推荐组合：

- [M5Unified](https://github.com/m5stack/M5Unified) —— 系统 / 按键 / 触摸 / 电源 / RTC
- [M5GFX](https://github.com/m5stack/M5GFX) —— 显示驱动（内置 IT8951 墨水屏 + GT911 触摸支持，
  自动识别 `board_M5Paper`）

PlatformIO 官方 espressif32 平台没有 M5Paper 专用板型，因此以 `esp32dev` 为基础，
在 `platformio.ini` 中覆写为 16MB Flash + 8MB QSPI PSRAM 的真实硬件参数。

## 常用命令

```bash
pio run                    # 编译
pio run -t upload          # 编译并烧录
pio device monitor         # 串口监视（115200）
pio run -t upload -t monitor # 烧录后直接进监视
```

`src/main.cpp` 移植自官方示例，包含三键（BtnA/B/C）与触摸的基础演示：

- [examples/Basic/Button/Button.ino](https://github.com/m5stack/M5Unified/blob/master/examples/Basic/Button/Button.ino)
- [examples/Basic/Touch/DragDrop/DragDrop.ino](https://github.com/m5stack/M5Unified/blob/master/examples/Basic/Touch/DragDrop/DragDrop.ino)

更多官方示例见本地 `.pio/libdeps/m5paper/M5Unified/examples/`（HowToUse、Rtc、Imu 等）。

## 烧录注意事项

- 串口芯片有 CP2104 和 CH9102 两种批次，驱动装不上时按
  [官方文档](https://docs.m5stack.com/en/core/M5Paper) 安装 CP210X / CP34X 驱动。
- 烧录失败（超时 / Failed to write to target RAM）：按住 G0 再按 RESET 进入下载模式后重试。
- 墨水屏刷新慢是正常的；对画质有要求时把 `setEpdMode(epd_mode_t::epd_fastest)`
  换成 `epd_quality`。

## 常用外设提示

- RTC（BM8563）：`M5.Rtc`，官方示例 `examples/Basic/Rtc`
- 电池/睡眠：`M5.Power`（电量、`deepSleep()` 等，见官方 HowToUse 示例）
- SHT30 温湿度：M5Unified 不含此传感器，可另装官方 Unit 库
  `m5stack/Unit-SENSOR` 或 `adafruit/Adafruit SHT31 Sensor`
