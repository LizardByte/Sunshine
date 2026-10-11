/**
 * @file tests/tests_environment.h
 * @brief Declarations for SunshineEnvironment.
 */
#pragma once

// test includes
#include "tests_common.h"

struct SunshineEnvironment: testing::Environment {
  void SetUp() override {
    deinit_log = logging::init(0, "test_sunshine.log");
  }

  void TearDown() override {
    deinit_log = {};
  }

  std::unique_ptr<logging::deinit_t> deinit_log;
};
