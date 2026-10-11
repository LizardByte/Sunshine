/**
 * @file tests/tests_events.h
 * @brief Declarations for SunshineEventListener.
 */
#pragma once

// test includes
#include "tests_common.h"

// standard includes
#include <memory>
#include <mutex>
#include <string>

/**
 * @brief Buffer Sunshine logs and test events while the test program runs.
 */
struct SunshineEventListener: BufferedTestEventListener {
  /**
   * @brief Register the synchronous sink for the test program.
   * @param unit_test Test program whose events are being buffered.
   */
  void OnTestProgramStart(const testing::UnitTest &unit_test) override {
    static_cast<void>(unit_test);
    sink_owner = logging::add_sink(sink, 0, false);
  }

  /**
   * @brief Remove the sink when the test program finishes.
   * @param unit_test Test program whose logging registration is ending.
   */
  void OnTestProgramEnd(const testing::UnitTest &unit_test) override {
    static_cast<void>(unit_test);
    sink_owner.reset();
  }

protected:
  /**
   * @brief Emit a test event through the application logging API.
   * @param message Test event text to buffer.
   */
  void logTestEvent(const std::string &message) override {
    logging::tests(message);
  }

  /**
   * @brief Read the buffered output for the current test.
   * @return Formatted log output collected by the listener.
   */
  [[nodiscard]] std::string bufferedTestOutput() const override {
    return sink->output();
  }

  /**
   * @brief Clear output collected for the preceding test.
   */
  void clearBufferedTestOutput() override {
    sink->clear();
  }

private:
  /**
   * @brief Collect formatted records independently of the logging backend.
   */
  class buffering_sink_t final: public logging::sink_t {
  public:
    /**
     * @brief Append a log record to the synchronized output buffer.
     * @param message Completed record to format and retain.
     */
    void consume(const logging::message_t &message) override {
      std::lock_guard lock {mutex};
      buffer += logging::format_message(message);
      buffer += '\n';
    }

    /**
     * @brief Obtain a consistent copy of buffered records.
     * @return Formatted log lines retained by the sink.
     */
    [[nodiscard]] std::string output() const {
      std::lock_guard lock {mutex};
      return buffer;
    }

    /**
     * @brief Discard previously buffered records.
     */
    void clear() {
      std::lock_guard lock {mutex};
      buffer.clear();
    }

  private:
    mutable std::mutex mutex;  ///< Protects output against concurrent application logging.
    std::string buffer;  ///< Formatted records for the current test.
  };

  std::shared_ptr<buffering_sink_t> sink {std::make_shared<buffering_sink_t>()};  ///< Sink receiving test and application records.
  std::unique_ptr<logging::deinit_t> sink_owner;  ///< Registration removed before the sink is destroyed.
};
