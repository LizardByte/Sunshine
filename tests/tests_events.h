/**
 * @file tests/tests_events.h
 * @brief Declarations for SunshineEventListener.
 */
#pragma once
#include "tests_common.h"

struct SunshineEventListener: BufferedTestEventListener {
  SunshineEventListener() {
    sink = boost::make_shared<sink_t>();
    sink_buffer = boost::make_shared<std::stringstream>();
    sink->locked_backend()->add_stream(sink_buffer);
    sink->set_formatter(&logging::formatter);
  }

  void OnTestProgramStart(const testing::UnitTest &unit_test) override {
    static_cast<void>(unit_test);
    boost::log::core::get()->add_sink(sink);
  }

  void OnTestProgramEnd(const testing::UnitTest &unit_test) override {
    static_cast<void>(unit_test);
    boost::log::core::get()->remove_sink(sink);
  }

protected:
  void logTestEvent(const std::string &message) override {
    BOOST_LOG(tests) << message;
  }

  [[nodiscard]] std::string bufferedTestOutput() const override {
    return sink_buffer->str();
  }

  void clearBufferedTestOutput() override {
    sink_buffer->str("");
    sink_buffer->clear();
  }

private:
  using sink_t = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;
  boost::shared_ptr<sink_t> sink;
  boost::shared_ptr<std::stringstream> sink_buffer;
};
