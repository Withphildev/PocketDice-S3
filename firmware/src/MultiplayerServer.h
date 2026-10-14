#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PocketDiceCore.h"

namespace pocketdice {

class MultiplayerServer {
 public:
  explicit MultiplayerServer(RandomSource& random);
  ~MultiplayerServer();

  bool begin();
  void tick();
  bool rollFromDevice(RollResult& out);
  RollSpec currentSpec() const;

  const String& ssid() const { return ssid_; }
  IPAddress address() const { return apAddress_; }
  std::size_t playerCount() const;

 private:
  struct ClientBinding {
    std::uint32_t clientId{0};
    std::uint32_t playerId{0};
  };

  void configureRoutes();
  void onSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                     AwsEventType type, void* arg, std::uint8_t* data, std::size_t length);
  void handleMessage(AsyncWebSocketClient* client, const std::uint8_t* data, std::size_t length);
  void handleJoin(AsyncWebSocketClient* client, JsonObjectConst message);
  void handleRoll(AsyncWebSocketClient* client, JsonObjectConst message);
  void handleAdvanceTurn(AsyncWebSocketClient* client);
  void handleDisconnect(std::uint32_t clientId);

  String makeSnapshotLocked() const;
  String makeRollEventLocked(const RollResult& result) const;
  void broadcastSnapshot();
  void sendError(AsyncWebSocketClient* client, const char* code, const char* message) const;
  void sendWelcome(AsyncWebSocketClient* client, const Player& player, bool reconnected) const;

  std::uint32_t playerForClientLocked(std::uint32_t clientId) const;
  bool bindClientLocked(std::uint32_t clientId, std::uint32_t playerId);
  bool playerHasClientLocked(std::uint32_t playerId) const;
  void clearClientLocked(std::uint32_t clientId);
  static std::uint64_t hashCommandId(const char* commandId);
  static bool modifierValue(const char* id, std::int16_t& value);
  static void makeReconnectToken(char (&token)[33]);
  static void sanitizeName(const char* source, char (&name)[24]);

  RandomSource& random_;
  SessionState session_;
  RollSpec currentSpec_{1, 20, 0};
  mutable SemaphoreHandle_t mutex_{nullptr};
  DNSServer dns_;
  AsyncWebServer http_{80};
  AsyncWebSocket socket_{"/ws"};
  ClientBinding clients_[8]{};
  String ssid_;
  IPAddress apAddress_{192, 168, 4, 1};
  std::uint32_t nextPlayerId_{1};
  std::uint32_t lastCleanupMs_{0};
  bool webFilesReady_{false};
};

}  // namespace pocketdice

