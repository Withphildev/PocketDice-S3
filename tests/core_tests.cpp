#include "PocketDiceCore.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace pocketdice;

class SequenceRandom final : public RandomSource {
 public:
  std::uint32_t uniform(std::uint32_t upperExclusive) override {
    const std::uint32_t value = sequence_[cursor_++ % 4];
    return value % upperExclusive;
  }

 private:
  std::uint32_t sequence_[4]{0, 5, 19, 2};
  std::size_t cursor_{0};
};

void testFormulaParser() {
  RollSpec spec{};
  assert(FormulaParser::parse("d20", spec));
  assert(spec.count == 1 && spec.sides == 20 && spec.modifier == 0);
  assert(FormulaParser::parse(" 2d6 + 3 ", spec));
  assert(spec.count == 2 && spec.sides == 6 && spec.modifier == 3);
  assert(FormulaParser::parse("4D8-2", spec));
  assert(spec.count == 4 && spec.sides == 8 && spec.modifier == -2);
  assert(!FormulaParser::parse("0d6", spec));
  assert(!FormulaParser::parse("21d6", spec));
  assert(!FormulaParser::parse("d1", spec));
  assert(!FormulaParser::parse("2d6+", spec));
}

void testRollAndModifiers() {
  SequenceRandom random;
  RollResult result{};
  assert(DiceEngine::roll({2, 6, 3}, random, result));
  assert(result.dice[0] == 1 && result.dice[1] == 6);
  assert(result.naturalTotal == 7 && result.total == 10);

  ModifierStack modifiers;
  assert(modifiers.add("Sword", 4));
  assert(modifiers.add("Blessing", 2));
  assert(modifiers.add("Poison", -1));
  assert(modifiers.total() == 5);
  modifiers.clear();
  assert(modifiers.size() == 0 && modifiers.total() == 0);
}

void testSession() {
  SessionState session;
  assert(session.addPlayer(1, "Aria", "token-a"));
  assert(session.addPlayer(2, "Bram", "token-b"));
  assert(!session.addPlayer(2, "Duplicate", "token-c"));
  assert(session.activePlayerId() == 1);
  assert(session.advanceTurn() && session.activePlayerId() == 2);
  assert(session.acceptCommand(42));
  assert(!session.acceptCommand(42));
  assert(session.setConnected(2, false));

  for (std::uint32_t id = 3; id <= 6; ++id) {
    assert(session.addPlayer(id, "Player", "token"));
  }
  assert(session.playerCount() == 6);
  assert(!session.addPlayer(7, "Too many", "token"));
}

int main() {
  testFormulaParser();
  testRollAndModifiers();
  testSession();
  std::cout << "PocketDiceCore: all tests passed\n";
  return 0;
}

