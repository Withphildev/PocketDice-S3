#include "PocketDiceCore.h"

#include <cctype>
#include <cstring>
#include <limits>

namespace pocketdice {
namespace {

void copyText(char* destination, std::size_t capacity, const char* source) {
  if (capacity == 0) return;
  if (source == nullptr) source = "";
  std::strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = '\0';
}

void skipSpaces(const char*& cursor) {
  while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
}

bool readUnsigned(const char*& cursor, std::uint32_t& value, bool& found) {
  value = 0;
  found = false;
  while (std::isdigit(static_cast<unsigned char>(*cursor))) {
    found = true;
    const std::uint32_t digit = static_cast<std::uint32_t>(*cursor - '0');
    if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) return false;
    value = value * 10U + digit;
    ++cursor;
  }
  return true;
}

}  // namespace

bool DiceEngine::isValid(const RollSpec& spec) {
  return spec.count >= 1 && spec.count <= kMaxDice && spec.sides >= 2 &&
         spec.sides <= 1000 && spec.modifier >= -999 && spec.modifier <= 999;
}

bool DiceEngine::roll(const RollSpec& spec, RandomSource& random, RollResult& out) {
  if (!isValid(spec)) return false;
  out = {};
  out.spec = spec;
  out.diceCount = spec.count;
  for (std::size_t index = 0; index < spec.count; ++index) {
    const auto value = static_cast<std::uint16_t>(random.uniform(spec.sides) + 1U);
    out.dice[index] = value;
    out.naturalTotal += value;
  }
  out.modifierTotal = spec.modifier;
  out.total = out.naturalTotal + out.modifierTotal;
  return true;
}

bool FormulaParser::parse(const char* formula, RollSpec& out) {
  if (formula == nullptr) return false;
  const char* cursor = formula;
  skipSpaces(cursor);

  std::uint32_t count = 0;
  bool hasCount = false;
  if (!readUnsigned(cursor, count, hasCount)) return false;
  skipSpaces(cursor);
  if (*cursor != 'd' && *cursor != 'D') return false;
  ++cursor;
  skipSpaces(cursor);

  std::uint32_t sides = 0;
  bool hasSides = false;
  if (!readUnsigned(cursor, sides, hasSides) || !hasSides) return false;
  skipSpaces(cursor);

  std::int32_t modifier = 0;
  if (*cursor == '+' || *cursor == '-') {
    const bool negative = *cursor == '-';
    ++cursor;
    skipSpaces(cursor);
    std::uint32_t magnitude = 0;
    bool hasModifier = false;
    if (!readUnsigned(cursor, magnitude, hasModifier) || !hasModifier || magnitude > 999U) return false;
    modifier = negative ? -static_cast<std::int32_t>(magnitude)
                        : static_cast<std::int32_t>(magnitude);
    skipSpaces(cursor);
  }
  if (*cursor != '\0') return false;

  if (!hasCount) count = 1;
  if (count > 255U || sides > 65535U) return false;
  const RollSpec parsed{static_cast<std::uint8_t>(count), static_cast<std::uint16_t>(sides),
                        static_cast<std::int16_t>(modifier)};
  if (!DiceEngine::isValid(parsed)) return false;
  out = parsed;
  return true;
}

bool ModifierStack::add(const char* label, std::int16_t value) {
  if (count_ >= kMaxModifiers || value < -999 || value > 999) return false;
  copyText(items_[count_].label, sizeof(items_[count_].label), label);
  items_[count_].value = value;
  ++count_;
  return true;
}

void ModifierStack::clear() {
  count_ = 0;
  for (auto& item : items_) item = {};
}

std::int32_t ModifierStack::total() const {
  std::int32_t result = 0;
  for (std::size_t index = 0; index < count_; ++index) result += items_[index].value;
  return result;
}

int SessionState::findPlayer(std::uint32_t id) const {
  for (std::size_t index = 0; index < playerCount_; ++index) {
    if (players_[index].id == id) return static_cast<int>(index);
  }
  return -1;
}

bool SessionState::addPlayer(std::uint32_t id, const char* name, const char* reconnectToken) {
  if (id == 0 || findPlayer(id) >= 0 || playerCount_ >= kMaxPlayers) return false;
  auto& next = players_[playerCount_++];
  next.id = id;
  copyText(next.name, sizeof(next.name), name);
  copyText(next.reconnectToken, sizeof(next.reconnectToken), reconnectToken);
  next.connected = true;
  if (activePlayerId_ == 0) activePlayerId_ = id;
  touch();
  return true;
}

bool SessionState::setConnected(std::uint32_t id, bool connected) {
  const int index = findPlayer(id);
  if (index < 0) return false;
  if (players_[index].connected != connected) {
    players_[index].connected = connected;
    touch();
  }
  return true;
}

bool SessionState::setActivePlayer(std::uint32_t id) {
  if (findPlayer(id) < 0) return false;
  if (activePlayerId_ != id) {
    activePlayerId_ = id;
    touch();
  }
  return true;
}

bool SessionState::advanceTurn() {
  if (playerCount_ == 0) return false;
  const int current = findPlayer(activePlayerId_);
  const std::size_t next = current < 0 ? 0 : (static_cast<std::size_t>(current) + 1U) % playerCount_;
  activePlayerId_ = players_[next].id;
  touch();
  return true;
}

bool SessionState::acceptCommand(std::uint64_t commandId) {
  if (commandId == 0) return false;
  for (std::size_t index = 0; index < commandCount_; ++index) {
    if (commands_[index] == commandId) return false;
  }
  commands_[commandCursor_] = commandId;
  commandCursor_ = (commandCursor_ + 1U) % kCommandHistory;
  if (commandCount_ < kCommandHistory) ++commandCount_;
  return true;
}

void SessionState::recordRoll(const RollResult& result) {
  lastRoll_ = result;
  touch();
}

}  // namespace pocketdice

