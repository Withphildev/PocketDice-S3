#include <Arduino.h>
#include <M5Unified.h>

#include "PocketDiceCore.h"
#include "MultiplayerServer.h"
#include "DieRenderer.h"

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
  for (int frame = 0; frame < 8; ++frame) {
    drawDie(spec.sides, static_cast<std::uint16_t>(randomSource.uniform(spec.sides) + 1),
            false, frame);
    delay(55);
  }
}

void drawRoll(bool shakeStyle) {
  if (shakeStyle) {
    animateShake(multiplayer.currentSpec());
  }
  if (!multiplayer.rollFromDevice(lastRoll)) return;
  drawDie(lastRoll.spec.sides,
          lastRoll.diceCount ? lastRoll.dice[0] : lastRoll.naturalTotal, true);
  if (lastRoll.modifierTotal != 0) {
    M5.Display.setTextDatum(top_right);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setTextSize(1);
    const String total = String("Total ") + lastRoll.total;
    M5.Display.drawString(total, M5.Display.width() - 3, 3);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("[PocketDice] boot");

  auto config = M5.config();
  config.serial_baudrate = 115200;
  M5.begin(config);
  Serial.printf("[PocketDice] M5 board=%d displays=%u\n", static_cast<int>(M5.getBoard()),
                static_cast<unsigned>(M5.getDisplayCount()));

  M5.Display.setRotation(3);
  M5.Display.setBrightness(90);
  M5.Display.fillScreen(TFT_NAVY);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PocketDice", M5.Display.width() / 2, M5.Display.height() / 2 - 10);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Starting...", M5.Display.width() / 2, M5.Display.height() / 2 + 14);
  Serial.printf("[PocketDice] display=%dx%d\n", M5.Display.width(), M5.Display.height());

  M5.Power.setExtOutput(false);
  Serial.println("[PocketDice] starting multiplayer services");
  const bool accessPointReady = multiplayer.begin();
  Serial.printf("[PocketDice] AP=%s SSID=%s IP=%s\n", accessPointReady ? "ready" : "failed",
                multiplayer.ssid().c_str(), multiplayer.address().toString().c_str());
  drawIdle();
  Serial.println("[PocketDice] ready");
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

