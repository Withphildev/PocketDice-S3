#include <Arduino.h>
#include <M5Unified.h>

#include "PocketDiceCore.h"
#include "MultiplayerServer.h"

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
MultiplayerServer multiplayer(randomSource);
RollResult lastRoll{};

void drawIdle() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PocketDice", M5.Display.width() / 2, 24);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.drawString(multiplayer.ssid(), M5.Display.width() / 2, 48);
  M5.Display.drawString(multiplayer.address().toString(), M5.Display.width() / 2, 62);
  M5.Display.drawString("Face: Roll  M5: Shake", M5.Display.width() / 2, 78);
}

void animateShake(const RollSpec& spec) {
  for (int frame = 0; frame < 7; ++frame) {
    M5.Display.fillScreen(frame % 2 == 0 ? TFT_NAVY : TFT_DARKGREY);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.drawNumber(static_cast<int>(randomSource.uniform(spec.sides) + 1),
                          M5.Display.width() / 2 + ((frame % 3) - 1) * 3,
                          M5.Display.height() / 2);
    delay(45);
  }
}

void drawRoll(bool shakeStyle) {
  if (shakeStyle) {
    animateShake(multiplayer.currentSpec());
  }
  if (!multiplayer.rollFromDevice(lastRoll)) return;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setTextSize(5);
  M5.Display.drawNumber(lastRoll.total, M5.Display.width() / 2, M5.Display.height() / 2);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setTextSize(1);
  String formula = String(lastRoll.spec.count) + "d" + String(lastRoll.spec.sides);
  if (lastRoll.spec.modifier > 0) formula += "+";
  if (lastRoll.spec.modifier != 0) formula += String(lastRoll.spec.modifier);
  M5.Display.drawString(formula, M5.Display.width() / 2, M5.Display.height() - 12);
}

}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  multiplayer.begin();
  drawIdle();
}

void loop() {
  M5.update();

  // The front/face button is BtnA on M5StickS3.
  if (M5.BtnA.wasPressed()) drawRoll(false);

  // The top M5 button is BtnB on M5StickS3.
  if (M5.BtnB.wasPressed()) drawRoll(true);

  multiplayer.tick();
  delay(10);
}

