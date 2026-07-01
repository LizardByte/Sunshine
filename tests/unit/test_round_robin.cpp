/**
 * @file tests/unit/test_round_robin.cpp
 * @brief Test src/round_robin.h.
 */
#include "../tests_common.h"

#include <src/round_robin.h>

#include <vector>

// ========== Basic iteration tests ==========

TEST(RoundRobinTests, WrapsAroundOnIncrement) {
  std::vector<int> data = {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  EXPECT_EQ(*rr, 10);
  ++rr;
  EXPECT_EQ(*rr, 20);
  ++rr;
  EXPECT_EQ(*rr, 30);
  ++rr;
  // Should wrap around to the beginning
  EXPECT_EQ(*rr, 10);
}

TEST(RoundRobinTests, WrapsAroundOnDecrement) {
  std::vector<int> data = {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  // Decrement from the start should wrap to end
  --rr;
  EXPECT_EQ(*rr, 30);
  --rr;
  EXPECT_EQ(*rr, 20);
  --rr;
  EXPECT_EQ(*rr, 10);
}

TEST(RoundRobinTests, PostIncrement) {
  std::vector<int> data = {1, 2, 3};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  auto prev = rr++;
  EXPECT_EQ(*prev, 1);
  EXPECT_EQ(*rr, 2);
}

TEST(RoundRobinTests, PostDecrement) {
  std::vector<int> data = {1, 2, 3};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  ++rr;  // move to 2

  auto prev = rr--;
  EXPECT_EQ(*prev, 2);
  EXPECT_EQ(*rr, 1);
}

// ========== Arithmetic operator tests ==========

TEST(RoundRobinTests, PlusEqualsAdvancesMultipleSteps) {
  std::vector<int> data = {10, 20, 30, 40, 50};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  rr += 3;
  EXPECT_EQ(*rr, 40);
}

TEST(RoundRobinTests, PlusEqualsWrapsAround) {
  std::vector<int> data = {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  rr += 5;  // wraps: 10->20->30->10->20->30... position 5 mod 3 = 2
  EXPECT_EQ(*rr, 30);
}

TEST(RoundRobinTests, MinusEqualsRewindsMultipleSteps) {
  std::vector<int> data = {10, 20, 30, 40, 50};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  rr += 4;  // at 50
  rr -= 2;  // back to 30
  EXPECT_EQ(*rr, 30);
}

TEST(RoundRobinTests, PlusOperatorDoesNotModifyOriginal) {
  std::vector<int> data = {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  auto rr2 = rr + 2;
  EXPECT_EQ(*rr, 10);  // original unchanged
  EXPECT_EQ(*rr2, 30);
}

TEST(RoundRobinTests, MinusOperatorDoesNotModifyOriginal) {
  std::vector<int> data = {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  rr += 2;  // at 30

  auto rr2 = rr - 1;
  EXPECT_EQ(*rr, 30);  // original unchanged
  EXPECT_EQ(*rr2, 20);
}

// ========== Comparison operator tests ==========

TEST(RoundRobinTests, EqualityWhenSamePosition) {
  std::vector<int> data = {10, 20, 30};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  EXPECT_TRUE(rr1 == rr2);
  EXPECT_FALSE(rr1 != rr2);
}

TEST(RoundRobinTests, InequalityWhenDifferentPosition) {
  std::vector<int> data = {10, 20, 30};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  ++rr2;

  EXPECT_FALSE(rr1 == rr2);
  EXPECT_TRUE(rr1 != rr2);
}

// ========== Difference operator tests ==========

TEST(RoundRobinTests, DifferenceOperator) {
  std::vector<int> data = {10, 20, 30, 40, 50};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  rr2 += 3;

  auto diff = rr2 - rr1;
  EXPECT_EQ(diff, 3);
}

// ========== Single element tests ==========

TEST(RoundRobinTests, SingleElementAlwaysReturnsSame) {
  std::vector<int> data = {42};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  EXPECT_EQ(*rr, 42);
  ++rr;
  EXPECT_EQ(*rr, 42);
  ++rr;
  EXPECT_EQ(*rr, 42);
}

// ========== Multiple full cycles ==========

TEST(RoundRobinTests, MultipleFullCycles) {
  std::vector<int> data = {1, 2, 3};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  // Go around twice
  for (int cycle = 0; cycle < 2; ++cycle) {
    EXPECT_EQ(*rr, 1);
    ++rr;
    EXPECT_EQ(*rr, 2);
    ++rr;
    EXPECT_EQ(*rr, 3);
    ++rr;
  }
}

// ========== Pointer dereference test ==========

TEST(RoundRobinTests, ArrowOperator) {
  struct Item {
    int value;
    std::string name;
  };

  std::vector<Item> data = {{1, "one"}, {2, "two"}, {3, "three"}};
  auto rr = round_robin_util::make_round_robin<Item>(data.begin(), data.end());

  EXPECT_EQ(rr->value, 1);
  EXPECT_EQ(rr->name, "one");
  ++rr;
  EXPECT_EQ(rr->value, 2);
  EXPECT_EQ(rr->name, "two");
}
