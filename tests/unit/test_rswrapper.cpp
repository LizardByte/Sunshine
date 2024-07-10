/**
 * @file tests/unit/test_rswrapper.cpp
 * @brief Test src/rswrapper.*
 */

extern "C" {
#include <src/rswrapper.h>
}

#include <tests/conftest.cpp>

TEST(ReedSolomonWrapperTests, InitTest) {
  reed_solomon_init();

  // Ensure all function pointers were populated
  ASSERT_NE(reed_solomon_new, nullptr);
  ASSERT_NE(reed_solomon_release, nullptr);
  ASSERT_NE(reed_solomon_encode, nullptr);
  ASSERT_NE(reed_solomon_decode, nullptr);
}

TEST(ReedSolomonWrapperTests, EncodeTest) {
  reed_solomon_init();

  auto rs = reed_solomon_new(1, 1);
  ASSERT_NE(rs, nullptr);

  uint8_t dataShard[16] = {};
  uint8_t fecShard[16] = {};

  // If we picked the incorrect ISA in our wrapper, we should crash here
  uint8_t *shardPtrs[2] = { dataShard, fecShard };
  auto ret = reed_solomon_encode(rs, shardPtrs, 2, sizeof(dataShard));
  ASSERT_EQ(ret, 0);

  reed_solomon_release(rs);
}
