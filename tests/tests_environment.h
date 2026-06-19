/**
 * @file tests/tests_environment.h
 * @brief Declarations for SunshineEnvironment.
 */
#pragma once
#include "tests_common.h"

struct SunshineEnvironment: testing::Environment {
  void SetUp() override {
    mail::man = std::make_shared<safe::mail_raw_t>();
    deinit_log = logging::init(0, "test_sunshine.log");
  }

  void TearDown() override {
    deinit_log = {};
    mail::man = {};
  }

  std::unique_ptr<logging::deinit_t> deinit_log;
};
