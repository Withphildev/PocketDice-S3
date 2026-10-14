#include <Arduino.h>
#include <M5Unified.h>

#include "PocketDiceCore.h"

using namespace pocketdice;

namespace {

class EspRandom final : public RandomSource {
 public:
  std::uint32_t uniform(std::uint32_t upperExclusive) override {
    if (upperExclusive == 0) return 0;
    const std::uint32_t limit = UINT32_MAX - (UINT32_MAX % upperExclusive);
    std::uint32_t value = 0;
    do {
      value = esp_random();
    } while (value >= limit);
    return value % upperExclusive;
  }
};

EspRandom randomSource;
RollSpec selectedDie{1, 20, 0};
RollResult lastRoll{};

void drawIdle() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PocketDice", M5.Display.width() / 2, 24);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Face: Roll", M5.Display.width() / 2, 55);
  M5.Display.drawString("M5: Shake Roll", M5.Display.width() / 2, 72);
}

void drawRoll(bool shakeStyle) {
  if (!DiceEngine::roll(selectedDie, randomSource, lastRoll)) return;
  if (shakeStyle) {
    for (int frame = 0; frame < 7; ++frame) {
      M5.Display.fillScreen(frame % 2 == 0 ? TFT_NAVY : TFT_DARKGREY);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextDatum(middle_center);
      M5.Display.setTextSize(3);
      M5.Display.drawNumber(static_cast<int>(randomSource.uniform(selectedDie.sides) + 1),
                            M5.Display.width() / 2 + ((frame % 3) - 1) * 3,
                            M5.Display.height() / 2);
      delay(45);
    }
  }
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setTextSize(5);
  M5.Display.drawNumber(lastRoll.total, M5.Display.width() / 2, M5.Display.height() / 2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.drawString("d20", M5.Display.width() / 2, M5.Display.height() - 12);
}

}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  drawIdle();
}

void loop() {
  M5.update();

  // The front/face button is BtnA on M5StickS3.
  if (M5.BtnA.wasPressed()) drawRoll(false);

  // The top M5 button is BtnB on M5StickS3.
  if (M5.BtnB.wasPressed()) drawRoll(true);

  delay(10);
}

