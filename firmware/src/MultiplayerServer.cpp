#include "MultiplayerServer.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cctype>
#include <cstring>

namespace pocketdice {
namespace {

constexpr std::size_t kMaxMessageBytes = 1024;

class LockGuard {
 public:
  explicit LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
    if (mutex_ != nullptr) xSemaphoreTake(mutex_, portMAX_DELAY);
  }
  ~LockGuard() {
    if (mutex_ != nullptr) xSemaphoreGive(mutex_);
  }

 private:
  SemaphoreHandle_t mutex_;
};

}  // namespace

MultiplayerServer::MultiplayerServer(RandomSource& random) : random_(random) {
  mutex_ = xSemaphoreCreateMutex();
}

MultiplayerServer::~MultiplayerServer() {
  if (mutex_ != nullptr) vSemaphoreDelete(mutex_);
}

bool MultiplayerServer::begin() {
  const std::uint32_t chip = static_cast<std::uint32_t>(ESP.getEfuseMac());
  char suffix[5]{};
  std::snprintf(suffix, sizeof(suffix), "%04X", chip & 0xFFFFU);
  ssid_ = String("PocketDice-") + suffix;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apAddress_, apAddress_, IPAddress(255, 255, 255, 0));
  const bool accessPointReady = WiFi.softAP(ssid_.c_str(), nullptr, 1, 0, 8);
  dns_.start(53, "*", apAddress_);

  webFilesReady_ = LittleFS.begin(false, "/web", 10, "web");
  configureRoutes();
  socket_.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg, std::uint8_t* data, std::size_t length) {
    onSocketEvent(server, client, type, arg, data, length);
  });
  http_.addHandler(&socket_);
  http_.begin();
  return accessPointReady;
}

void MultiplayerServer::configureRoutes() {
  http_.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String payload;
    {
      LockGuard lock(mutex_);
      payload = makeSnapshotLocked();
    }
    request->send(200, "application/json", payload);
  });

  const auto redirectToPortal = [this](AsyncWebServerRequest* request) {
    request->redirect(String("http://") + apAddress_.toString() + "/");
  };
  http_.on("/generate_204", HTTP_GET, redirectToPortal);
  http_.on("/gen_204", HTTP_GET, redirectToPortal);
  http_.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
  http_.on("/connecttest.txt", HTTP_GET, redirectToPortal);
  http_.on("/ncsi.txt", HTTP_GET, redirectToPortal);

  if (webFilesReady_) {
    http_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  } else {
    http_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(503, "text/html",
                    "<h1>PocketDice</h1><p>WebUI filesystem is not installed. Run pio run -t uploadfs.</p>");
    });
  }

  http_.onNotFound(redirectToPortal);
}

void MultiplayerServer::tick() {
  dns_.processNextRequest();
  const std::uint32_t now = millis();
  if (now - lastCleanupMs_ >= 1000U) {
    socket_.cleanupClients();
    lastCleanupMs_ = now;
  }
}

void MultiplayerServer::onSocketEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                                      AwsEventType type, void* arg, std::uint8_t* data,
                                      std::size_t length) {
  if (type == WS_EVT_CONNECT) {
    JsonDocument document;
    document["type"] = "server.hello";
    {
      LockGuard lock(mutex_);
      document["revision"] = session_.revision();
    }
    document["maxPlayers"] = kMaxPlayers;
    String payload;
    serializeJson(document, payload);
    client->text(payload);
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    handleDisconnect(client->id());
    return;
  }

  if (type != WS_EVT_DATA || arg == nullptr || data == nullptr) return;
  const auto* info = static_cast<AwsFrameInfo*>(arg);
  if (!info->final || info->index != 0 || info->len != length || info->opcode != WS_TEXT) {
    sendError(client, "invalid-frame", "PocketDice accepts complete text frames only.");
    return;
  }
  if (length == 0 || length > kMaxMessageBytes) {
    sendError(client, "message-too-large", "Message must be between 1 and 1024 bytes.");
    return;
  }
  handleMessage(client, data, length);
}

void MultiplayerServer::handleMessage(AsyncWebSocketClient* client, const std::uint8_t* data,
                                      std::size_t length) {
  JsonDocument document;
  const auto error = deserializeJson(document, data, length);
  if (error) {
    sendError(client, "invalid-json", "Message is not valid JSON.");
    return;
  }
  const JsonObjectConst message = document.as<JsonObjectConst>();
  const char* type = message["type"] | "";
  if (std::strcmp(type, "player.join") == 0) {
    handleJoin(client, message);
  } else if (std::strcmp(type, "roll.request") == 0) {
    handleRoll(client, message);
  } else if (std::strcmp(type, "turn.advance") == 0) {
    handleAdvanceTurn(client);
  } else {
    sendError(client, "unknown-message", "Message type is not supported.");
  }
}

void MultiplayerServer::handleJoin(AsyncWebSocketClient* client, JsonObjectConst message) {
  Player joined{};
  bool reconnected = false;
  const char* requestedToken = message["reconnectToken"] | "";

  {
    LockGuard lock(mutex_);
    const Player* existing = session_.playerByToken(requestedToken);
    if (existing != nullptr) {
      joined = *existing;
      reconnected = true;
      if (!bindClientLocked(client->id(), joined.id)) {
        sendError(client, "server-busy", "No connection slot is available.");
        return;
      }
      session_.setConnected(joined.id, true);
    } else {
      if (session_.playerCount() >= kMaxPlayers) {
        sendError(client, "session-full", "This PocketDice table already has six players.");
        return;
      }
      bool connectionSlotAvailable = false;
      for (const auto& binding : clients_) {
        if (binding.clientId == 0 || binding.clientId == client->id()) {
          connectionSlotAvailable = true;
          break;
        }
      }
      if (!connectionSlotAvailable) {
        sendError(client, "server-busy", "No connection slot is available.");
        return;
      }
      char name[24]{};
      sanitizeName(message["name"] | "", name);
      char token[33]{};
      makeReconnectToken(token);
      const std::uint32_t id = nextPlayerId_++;
      if (!session_.addPlayer(id, name, token) || !bindClientLocked(client->id(), id)) {
        sendError(client, "join-failed", "The player could not be added.");
        return;
      }
      joined = *session_.playerById(id);
    }
  }

  sendWelcome(client, joined, reconnected);
  broadcastSnapshot();
}

void MultiplayerServer::handleRoll(AsyncWebSocketClient* client, JsonObjectConst message) {
  String payload;
  const char* failureCode = nullptr;
  const char* failureMessage = nullptr;

  {
    LockGuard lock(mutex_);
    const std::uint32_t playerId = playerForClientLocked(client->id());
    if (playerId == 0) {
      failureCode = "join-required";
      failureMessage = "Join the table before rolling.";
    } else if (playerId != session_.activePlayerId()) {
      failureCode = "not-your-turn";
      failureMessage = "Wait until your player board is highlighted.";
    }

    RollSpec spec{};
    if (failureCode == nullptr && !FormulaParser::parse(message["formula"] | "", spec)) {
      failureCode = "invalid-formula";
      failureMessage = "Use a formula such as d20 or 2d6+3.";
    }

    std::int32_t modifier = spec.modifier;
    if (failureCode == nullptr && message["modifierIds"].is<JsonArrayConst>()) {
      std::size_t count = 0;
      const char* seen[kMaxModifiers]{};
      for (JsonVariantConst item : message["modifierIds"].as<JsonArrayConst>()) {
        std::int16_t value = 0;
        const char* modifierId = item.as<const char*>();
        bool duplicate = false;
        for (std::size_t index = 0; index < count; ++index) {
          if (modifierId != nullptr && std::strcmp(seen[index], modifierId) == 0) duplicate = true;
        }
        if (duplicate || count >= kMaxModifiers || !modifierValue(modifierId, value)) {
          failureCode = "invalid-modifier";
          failureMessage = "Modifiers must be known, unique, and limited to eight.";
          break;
        }
        seen[count++] = modifierId;
        modifier += value;
      }
    }
    if (failureCode == nullptr && (modifier < -999 || modifier > 999)) {
      failureCode = "modifier-out-of-range";
      failureMessage = "The combined modifier must stay between -999 and 999.";
    }

    const char* commandId = message["commandId"] | "";
    if (failureCode == nullptr && !session_.acceptCommand(hashCommandId(commandId))) {
      failureCode = "duplicate-command";
      failureMessage = "This roll command was already processed.";
    }

    if (failureCode == nullptr) {
      spec.modifier = static_cast<std::int16_t>(modifier);
      RollResult result{};
      if (!DiceEngine::roll(spec, random_, result)) {
        failureCode = "roll-failed";
        failureMessage = "The roll could not be generated.";
      } else {
        currentSpec_ = spec;
        session_.recordRoll(result);
        payload = makeRollEventLocked(result);
      }
    }
  }

  if (failureCode != nullptr) {
    sendError(client, failureCode, failureMessage);
  } else {
    socket_.textAll(payload);
  }
}

void MultiplayerServer::handleAdvanceTurn(AsyncWebSocketClient* client) {
  bool allowed = false;
  {
    LockGuard lock(mutex_);
    const std::uint32_t playerId = playerForClientLocked(client->id());
    allowed = session_.playerCount() > 0 && playerId == session_.player(0).id;
    if (allowed) session_.advanceTurn();
  }
  if (!allowed) {
    sendError(client, "host-required", "Only the table host can advance the turn.");
    return;
  }
  broadcastSnapshot();
}

void MultiplayerServer::handleDisconnect(std::uint32_t clientId) {
  {
    LockGuard lock(mutex_);
    const std::uint32_t playerId = playerForClientLocked(clientId);
    clearClientLocked(clientId);
    if (playerId != 0 && !playerHasClientLocked(playerId)) session_.setConnected(playerId, false);
  }
  broadcastSnapshot();
}

bool MultiplayerServer::rollFromDevice(RollResult& out) {
  String payload;
  {
    LockGuard lock(mutex_);
    if (!DiceEngine::roll(currentSpec_, random_, out)) return false;
    session_.recordRoll(out);
    payload = makeRollEventLocked(out);
  }
  socket_.textAll(payload);
  return true;
}

RollSpec MultiplayerServer::currentSpec() const {
  LockGuard lock(mutex_);
  return currentSpec_;
}

std::size_t MultiplayerServer::playerCount() const {
  LockGuard lock(mutex_);
  return session_.playerCount();
}

String MultiplayerServer::makeSnapshotLocked() const {
  JsonDocument document;
  document["type"] = "session.snapshot";
  document["revision"] = session_.revision();
  JsonObject state = document["session"].to<JsonObject>();
  state["activePlayerId"] = session_.activePlayerId();
  state["hostPlayerId"] = session_.playerCount() == 0 ? 0 : session_.player(0).id;
  JsonArray players = state["players"].to<JsonArray>();
  for (std::size_t index = 0; index < session_.playerCount(); ++index) {
    const Player& player = session_.player(index);
    JsonObject item = players.add<JsonObject>();
    item["id"] = player.id;
    item["name"] = player.name;
    item["connected"] = player.connected;
  }
  const RollResult& lastRoll = session_.lastRoll();
  if (lastRoll.diceCount > 0) {
    JsonObject roll = state["lastRoll"].to<JsonObject>();
    roll["sides"] = lastRoll.spec.sides;
    roll["naturalTotal"] = lastRoll.naturalTotal;
    roll["modifierTotal"] = lastRoll.modifierTotal;
    roll["total"] = lastRoll.total;
  }
  String payload;
  serializeJson(document, payload);
  return payload;
}

String MultiplayerServer::makeRollEventLocked(const RollResult& result) const {
  JsonDocument document;
  document["type"] = "roll.result";
  document["revision"] = session_.revision();
  JsonObject roll = document["result"].to<JsonObject>();
  roll["sides"] = result.spec.sides;
  roll["naturalTotal"] = result.naturalTotal;
  roll["modifierTotal"] = result.modifierTotal;
  roll["total"] = result.total;
  JsonArray dice = roll["dice"].to<JsonArray>();
  for (std::size_t index = 0; index < result.diceCount; ++index) dice.add(result.dice[index]);
  String payload;
  serializeJson(document, payload);
  return payload;
}

void MultiplayerServer::broadcastSnapshot() {
  String payload;
  {
    LockGuard lock(mutex_);
    payload = makeSnapshotLocked();
  }
  socket_.textAll(payload);
}

void MultiplayerServer::sendError(AsyncWebSocketClient* client, const char* code,
                                  const char* message) const {
  JsonDocument document;
  document["type"] = "error";
  document["code"] = code;
  document["message"] = message;
  String payload;
  serializeJson(document, payload);
  client->text(payload);
}

void MultiplayerServer::sendWelcome(AsyncWebSocketClient* client, const Player& player,
                                    bool reconnected) const {
  JsonDocument document;
  document["type"] = "player.welcome";
  document["playerId"] = player.id;
  document["reconnectToken"] = player.reconnectToken;
  document["reconnected"] = reconnected;
  String payload;
  serializeJson(document, payload);
  client->text(payload);
}

std::uint32_t MultiplayerServer::playerForClientLocked(std::uint32_t clientId) const {
  for (const auto& binding : clients_) {
    if (binding.clientId == clientId) return binding.playerId;
  }
  return 0;
}

bool MultiplayerServer::bindClientLocked(std::uint32_t clientId, std::uint32_t playerId) {
  clearClientLocked(clientId);
  for (auto& binding : clients_) {
    if (binding.clientId == 0) {
      binding.clientId = clientId;
      binding.playerId = playerId;
      return true;
    }
  }
  return false;
}

bool MultiplayerServer::playerHasClientLocked(std::uint32_t playerId) const {
  for (const auto& binding : clients_) {
    if (binding.clientId != 0 && binding.playerId == playerId) return true;
  }
  return false;
}

void MultiplayerServer::clearClientLocked(std::uint32_t clientId) {
  for (auto& binding : clients_) {
    if (binding.clientId == clientId) binding = {};
  }
}

std::uint64_t MultiplayerServer::hashCommandId(const char* commandId) {
  if (commandId == nullptr) return 0;
  const std::size_t length = std::strlen(commandId);
  if (length < 8 || length > 64) return 0;
  std::uint64_t hash = 1469598103934665603ULL;
  while (*commandId != '\0') {
    hash ^= static_cast<std::uint8_t>(*commandId++);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool MultiplayerServer::modifierValue(const char* id, std::int16_t& value) {
  if (id == nullptr) return false;
  if (std::strcmp(id, "weapon") == 0) value = 4;
  else if (std::strcmp(id, "power") == 0) value = 3;
  else if (std::strcmp(id, "blessing") == 0) value = 2;
  else if (std::strcmp(id, "wound") == 0) value = -1;
  else return false;
  return true;
}

void MultiplayerServer::makeReconnectToken(char (&token)[33]) {
  static constexpr char hex[] = "0123456789abcdef";
  for (std::size_t index = 0; index < 16; ++index) {
    const std::uint8_t value = static_cast<std::uint8_t>(esp_random());
    token[index * 2] = hex[value >> 4];
    token[index * 2 + 1] = hex[value & 0x0F];
  }
  token[32] = '\0';
}

void MultiplayerServer::sanitizeName(const char* source, char (&name)[24]) {
  std::size_t written = 0;
  if (source != nullptr) {
    for (; *source != '\0' && written < sizeof(name) - 1; ++source) {
      const unsigned char character = static_cast<unsigned char>(*source);
      if (std::isalnum(character) || character == ' ' || character == '-' || character == '_') {
        name[written++] = static_cast<char>(character);
      }
    }
  }
  while (written > 0 && name[written - 1] == ' ') --written;
  name[written] = '\0';
  if (written == 0) std::strcpy(name, "Player");
}

}  // namespace pocketdice

