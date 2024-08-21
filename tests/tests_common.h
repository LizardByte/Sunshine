/**
 * @file tests/tests_common.h
 * @brief Common declarations.
 */
#pragma once
#include <gtest/gtest.h>

#include <src/globals.h>
#include <src/logging.h>
#include <src/platform/common.h>

template <class T>
struct PlatformTestSuite: testing::Test {
  static std::unique_ptr<platf::deinit_t> &
  get_platform_deinit() {
    static std::unique_ptr<platf::deinit_t> deinit;
    return deinit;
  }

  static void
  SetUpTestSuite() {
    auto &deinit = get_platform_deinit();
    BOOST_LOG(tests) << "Setting up platform test suite";
    deinit = platf::init();
    ASSERT_TRUE(deinit);
  }

  static void
  TearDownTestSuite() {
    auto &deinit = get_platform_deinit();
    deinit = {};
    BOOST_LOG(tests) << "Tore down platform test suite";
  }
};
