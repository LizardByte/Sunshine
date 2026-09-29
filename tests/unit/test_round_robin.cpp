/**
 * @file tests/unit/test_round_robin.cpp
 * @brief Tests for the round-robin iterator.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <string>
#include <vector>

// local includes
#include <src/round_robin.h>

TEST(RoundRobinIterationTests, WrapsAroundOnIncrement) {
  std::vector data {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  EXPECT_EQ(*rr, 10);
  ++rr;
  EXPECT_EQ(*rr, 20);
  ++rr;
  EXPECT_EQ(*rr, 30);
  ++rr;
  EXPECT_EQ(*rr, 10);
}

TEST(RoundRobinIterationTests, WrapsAroundOnDecrement) {
  std::vector data {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  --rr;
  EXPECT_EQ(*rr, 30);
  --rr;
  EXPECT_EQ(*rr, 20);
  --rr;
  EXPECT_EQ(*rr, 10);
}

TEST(RoundRobinIterationTests, PostIncrement) {
  std::vector data {1, 2, 3};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  const auto prev = rr++;
  EXPECT_EQ(*prev, 1);
  EXPECT_EQ(*rr, 2);
}

TEST(RoundRobinIterationTests, PostDecrement) {
  std::vector data {1, 2, 3};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  ++rr;

  const auto prev = rr--;
  EXPECT_EQ(*prev, 2);
  EXPECT_EQ(*rr, 1);
}

TEST(RoundRobinArithmeticTests, PlusEqualsAdvancesMultipleSteps) {
  std::vector data {10, 20, 30, 40, 50};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  rr += 3;
  EXPECT_EQ(*rr, 40);
}

TEST(RoundRobinArithmeticTests, PlusEqualsWrapsAround) {
  std::vector data {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  rr += 5;
  EXPECT_EQ(*rr, 30);
}

TEST(RoundRobinArithmeticTests, MinusEqualsRewindsMultipleSteps) {
  std::vector data {10, 20, 30, 40, 50};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  rr += 4;
  rr -= 2;
  EXPECT_EQ(*rr, 30);
}

TEST(RoundRobinArithmeticTests, PlusOperatorDoesNotModifyOriginal) {
  std::vector data {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  const auto rr2 = rr + 2;
  EXPECT_EQ(*rr, 10);
  EXPECT_EQ(*rr2, 30);
}

TEST(RoundRobinArithmeticTests, MinusOperatorDoesNotModifyOriginal) {
  std::vector data {10, 20, 30};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  rr += 2;

  const auto rr2 = rr - 1;
  EXPECT_EQ(*rr, 30);
  EXPECT_EQ(*rr2, 20);
}

TEST(RoundRobinComparisonTests, EqualityWhenSamePosition) {
  std::vector data {10, 20, 30};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  EXPECT_TRUE(rr1 == rr2);
  EXPECT_FALSE(rr1 != rr2);
}

TEST(RoundRobinComparisonTests, InequalityWhenDifferentPosition) {
  std::vector data {10, 20, 30};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  ++rr2;

  EXPECT_FALSE(rr1 == rr2);
  EXPECT_TRUE(rr1 != rr2);
}

TEST(RoundRobinComparisonTests, InequalityWhenValuesMatchAtDifferentPositions) {
  std::vector data {10, 10};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  ++rr2;

  EXPECT_NE(rr1, rr2);
}

TEST(RoundRobinComparisonTests, OrdersByPosition) {
  std::vector data {10, 20, 30};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  ++rr2;

  EXPECT_LT(rr1, rr2);
  EXPECT_LE(rr1, rr2);
  EXPECT_GT(rr2, rr1);
  EXPECT_GE(rr2, rr1);
}

TEST(RoundRobinArithmeticTests, DifferenceOperator) {
  std::vector data {10, 20, 30, 40, 50};
  auto rr1 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  auto rr2 = round_robin_util::make_round_robin<int>(data.begin(), data.end());
  rr2 += 3;

  const auto diff = rr2 - rr1;
  EXPECT_EQ(diff, 3);
}

TEST(RoundRobinIterationTests, SingleElementAlwaysReturnsSame) {
  std::vector data {42};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  EXPECT_EQ(*rr, 42);
  ++rr;
  EXPECT_EQ(*rr, 42);
  ++rr;
  EXPECT_EQ(*rr, 42);
}

TEST(RoundRobinIterationTests, MultipleFullCycles) {
  std::vector data {1, 2, 3};
  auto rr = round_robin_util::make_round_robin<int>(data.begin(), data.end());

  for (int cycle = 0; cycle < 2; ++cycle) {
    EXPECT_EQ(*rr, 1);
    ++rr;
    EXPECT_EQ(*rr, 2);
    ++rr;
    EXPECT_EQ(*rr, 3);
    ++rr;
  }
}

TEST(RoundRobinAccessTests, ArrowOperator) {
  struct item_t {
    int value;
    std::string name;
  };

  std::vector<item_t> data {{1, "one"}, {2, "two"}, {3, "three"}};
  auto rr = round_robin_util::make_round_robin<item_t>(data.begin(), data.end());

  EXPECT_EQ(rr->value, 1);
  EXPECT_EQ(rr->name, "one");
  ++rr;
  EXPECT_EQ(rr->value, 2);
  EXPECT_EQ(rr->name, "two");
}
