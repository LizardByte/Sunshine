/**
 * @file tests/tests_events.h
 * @brief Declarations for SunshineEventListener.
 */
#pragma once
#include "tests_common.h"

struct SunshineEventListener: testing::EmptyTestEventListener {
  SunshineEventListener() {
    sink = boost::make_shared<sink_t>();
    sink_buffer = boost::make_shared<std::stringstream>();
    sink->locked_backend()->add_stream(sink_buffer);
    sink->set_formatter(&logging::formatter);
  }

  void OnTestProgramStart(const testing::UnitTest &unit_test) override {
    boost::log::core::get()->add_sink(sink);
  }

  void OnTestProgramEnd(const testing::UnitTest &unit_test) override {
    boost::log::core::get()->remove_sink(sink);
  }

  void OnTestStart(const testing::TestInfo &test_info) override {
    BOOST_LOG(tests) << "From " << test_info.file() << ":" << test_info.line();
    BOOST_LOG(tests) << "  " << test_info.test_suite_name() << "/" << test_info.name() << " started";
  }

  void OnTestPartResult(const testing::TestPartResult &test_part_result) override {
    std::string file = test_part_result.file_name();
    BOOST_LOG(tests) << "At " << file << ":" << test_part_result.line_number();

    auto result_text = test_part_result.passed()            ? "Success" :
                       test_part_result.nonfatally_failed() ? "Non-fatal failure" :
                       test_part_result.fatally_failed()    ? "Failure" :
                                                              "Skip";

    std::string summary = test_part_result.summary();
    std::string message = test_part_result.message();
    BOOST_LOG(tests) << "  " << result_text << ": " << summary;
    if (message != summary) {
      BOOST_LOG(tests) << "  " << message;
    }
  }

  void OnTestEnd(const testing::TestInfo &test_info) override {
    auto &result = *test_info.result();

    auto result_text = result.Passed()  ? "passed" :
                       result.Skipped() ? "skipped" :
                                          "failed";
    BOOST_LOG(tests) << test_info.test_suite_name() << "/" << test_info.name() << " " << result_text;

    if (result.Failed()) {
      std::cout << sink_buffer->str();
    }

    sink_buffer->str("");
    sink_buffer->clear();
  }

  using sink_t = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;
  boost::shared_ptr<sink_t> sink;
  boost::shared_ptr<std::stringstream> sink_buffer;
};
