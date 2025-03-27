/**
 * @file tests/tests_common.h
 * @brief Common declarations.
 */
#pragma once
#include <gtest/gtest.h>
#include <src/globals.h>
#include <src/logging.h>
#include <src/platform/common.h>

struct PlatformTestSuite: testing::Test {
  static void SetUpTestSuite() {
    ASSERT_FALSE(platf_deinit);
    BOOST_LOG(tests) << "Setting up platform test suite";
    platf_deinit = platf::init();
    ASSERT_TRUE(platf_deinit);
  }

  static void TearDownTestSuite() {
    ASSERT_TRUE(platf_deinit);
    platf_deinit = {};
    BOOST_LOG(tests) << "Tore down platform test suite";
  }

private:
  inline static std::unique_ptr<platf::deinit_t> platf_deinit;
};
