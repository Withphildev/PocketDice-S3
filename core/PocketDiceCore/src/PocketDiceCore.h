#pragma once

#include <cstddef>
#include <cstdint>

namespace pocketdice {

constexpr std::size_t kMaxPlayers = 6;
constexpr std::size_t kMaxDice = 20;
constexpr std::size_t kMaxModifiers = 8;
constexpr std::size_t kCommandHistory = 16;

struct RollSpec {
  std::uint8_t count{1};
  std::uint16_t sides{20};
  std::int16_t modifier{0};
};

struct RollResult {
  RollSpec spec{};
  std::uint16_t dice[kMaxDice]{};
  std::uint8_t diceCount{0};
  std::int32_t naturalTotal{0};
  std::int32_t modifierTotal{0};
  std::int32_t total{0};
};

class RandomSource {
 public:
  virtual ~RandomSource() = default;
  virtual std::uint32_t uniform(std::uint32_t upperExclusive) = 0;
};

class DiceEngine {
 public:
  static bool isValid(const RollSpec& spec);
  static bool roll(const RollSpec& spec, RandomSource& random, RollResult& out);
};

class FormulaParser {
 public:
  // Accepts NdS, dS, NdS+M, and NdS-M with optional ASCII spaces.
  static bool parse(const char* formula, RollSpec& out);
};

struct Modifier {
  char label[24]{};
  std::int16_t value{0};
};

class ModifierStack {
 public:
  bool add(const char* label, std::int16_t value);
  void clear();
  std::int32_t total() const;
  std::size_t size() const { return count_; }
  const Modifier& at(std::size_t index) const { return items_[index]; }

 private:
  Modifier items_[kMaxModifiers]{};
  std::size_t count_{0};
};

struct Player {
  std::uint32_t id{0};
  char name[24]{};
  char reconnectToken[33]{};
  bool connected{false};
};

class SessionState {
 public:
  bool addPlayer(std::uint32_t id, const char* name, const char* reconnectToken);
  bool setConnected(std::uint32_t id, bool connected);
  bool setActivePlayer(std::uint32_t id);
  bool advanceTurn();
  bool acceptCommand(std::uint64_t commandId);
  void recordRoll(const RollResult& result);

  std::size_t playerCount() const { return playerCount_; }
  const Player& player(std::size_t index) const { return players_[index]; }
  const Player* playerById(std::uint32_t id) const;
  const Player* playerByToken(const char* reconnectToken) const;
  std::uint32_t activePlayerId() const { return activePlayerId_; }
  std::uint32_t revision() const { return revision_; }
  const RollResult& lastRoll() const { return lastRoll_; }

 private:
  int findPlayer(std::uint32_t id) const;
  void touch() { ++revision_; }

  Player players_[kMaxPlayers]{};
  std::size_t playerCount_{0};
  std::uint32_t activePlayerId_{0};
  std::uint32_t revision_{0};
  RollResult lastRoll_{};
  std::uint64_t commands_[kCommandHistory]{};
  std::size_t commandCount_{0};
  std::size_t commandCursor_{0};
};

}  // namespace pocketdice

