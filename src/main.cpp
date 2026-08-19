/* M5Paper v1.1 basic example (PlatformIO / Arduino).
 *
 * Ported from the official M5Unified examples:
 *   Button : https://github.com/m5stack/M5Unified/blob/master/examples/Basic/Button/Button.ino
 *   Touch  : https://github.com/m5stack/M5Unified/blob/master/examples/Basic/Touch/DragDrop/DragDrop.ino
 *
 * Per the official Button.ino board list, M5Paper provides BtnA / BtnB / BtnC
 * (the three buttons on the bottom edge) and GT911 capacitive touch.
 */

#include <M5Unified.h>

static constexpr const int colors[] =
    { TFT_WHITE, TFT_CYAN, TFT_RED, TFT_YELLOW, TFT_BLUE, TFT_GREEN };

static constexpr const char* const names[] =
    { "none", "wasHold", "wasClicked", "wasPressed", "wasReleased", "wasDecideClickCount" };

static constexpr const char* const touch_state_name[16] =
    { "none"
    , "touch"
    , "touch_end"
    , "touch_begin"
    , "___"
    , "hold"
    , "hold_end"
    , "hold_begin"
    , "___"
    , "flick"
    , "flick_end"
    , "flick_begin"
    , "___"
    , "drag"
    , "drag_end"
    , "drag_begin"
    };

void setup(void)
{
  M5.begin();

  /// For models with EPD : refresh control
  M5.Display.setEpdMode(epd_mode_t::epd_fastest); // fastest but very-low quality.

  if (M5.Display.width() < M5.Display.height())
  { /// Landscape mode.
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }

  M5.Display.setTextSize(3);
  M5.Display.print("Hello M5Paper!\n");
  M5.Display.setTextSize(2);
  M5.Display.print("BtnA/B/C: fill a color strip\nTouch: draw dots\n");
}

void loop(void)
{
  M5.delay(1);

  M5.update();

  //------------------- Button test (M5Paper: BtnA, BtnB, BtnC)
  int w = M5.Display.width() / 3;
  int h = M5.Display.height();
  M5.Display.startWrite();

  for (int i = 0; i < 3; ++i)
  {
    auto& btn = i == 0 ? M5.BtnA : i == 1 ? M5.BtnB : M5.BtnC;
    int state = btn.wasHold() ? 1
              : btn.wasClicked() ? 2
              : btn.wasPressed() ? 3
              : btn.wasReleased() ? 4
              : btn.wasDecideClickCount() ? 5
              : 0;
    if (state)
    {
      M5_LOGI("Btn%c:%s  count:%d", 'A' + i, names[state], btn.getClickCount());
      M5.Display.fillRect(w * i, 0, w - 1, h, colors[state]);
    }
  }
  M5.Display.endWrite();

  //------------------- Touch test (GT911)
  auto count = M5.Touch.getCount();
  if (!count)
  {
    return;
  }

  static m5::touch_state_t prev_state;
  auto t = M5.Touch.getDetail();
  if (prev_state != t.state)
  {
    prev_state = t.state;
    M5_LOGI("%s", touch_state_name[t.state]);
  }

  M5.Display.startWrite();
  for (std::size_t i = 0; i < count; ++i)
  {
    auto td = M5.Touch.getDetail(i);
    M5.Display.fillCircle(td.x, td.y, 8, TFT_BLACK);
  }
  M5.Display.endWrite();
}
