/**
 * @file tests/unit/test_logging_api.cpp
 * @brief Tests for Sunshine logging functions and independently owned sinks.
 */

// test includes
#include <gtest/gtest.h>

// standard includes
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <format>
#include <future>
#include <iomanip>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// lib includes
#include <boost/format.hpp>

// local includes
#include "src/logging.h"

// conditional includes
#ifndef __ANDROID__
  #include <display_device/logging.h>

extern "C" {
  #include <libavutil/log.h>
}
#endif

namespace {
  /**
   * @brief A delivered message and the thread that consumed it.
   */
  struct delivery_t {
    logging::message_t message;  ///< Immutable message supplied to the sink.
    std::thread::id thread;  ///< Thread executing the sink callback.
  };

  /**
   * @brief Record sink deliveries without depending on the logging backend.
   */
  class recording_sink_t final: public logging::sink_t {
  public:
    /**
     * @brief Copy an accepted message for later assertions.
     *
     * @param message Message accepted by this sink.
     */
    void consume(const logging::message_t &message) override {
      std::lock_guard lock {mutex_};
      deliveries_.push_back({message, std::this_thread::get_id()});
      delivered_.notify_all();
    }

    /**
     * @brief Wait for normal asynchronous delivery before explicitly flushing.
     * @param count Minimum number of recorded messages.
     * @return True when the expected messages arrived before the timeout.
     */
    bool wait_for_messages(std::size_t count) {
      std::unique_lock lock {mutex_};
      return delivered_.wait_for(lock, std::chrono::seconds {5}, [this, count]() {
        return deliveries_.size() >= count;
      });
    }

    /**
     * @brief Read a coherent snapshot after synchronous delivery or flushing.
     *
     * @return Recorded messages and callback threads.
     */
    std::vector<delivery_t> snapshot() const {
      std::lock_guard lock {mutex_};
      return deliveries_;
    }

  private:
    mutable std::mutex mutex_;  ///< Protects deliveries from asynchronous callbacks.
    std::condition_variable delivered_;  ///< Signals completed asynchronous deliveries.
    std::vector<delivery_t> deliveries_;  ///< Copies of accepted records.
  };

  /**
   * @brief Fail the first delivery and record subsequent deliveries.
   */
  class failing_sink_t final: public logging::sink_t {
  public:
    /**
     * @brief Simulate a transient backend failure before accepting later messages.
     *
     * @param message Message accepted by this sink.
     */
    void consume(const logging::message_t &message) override {
      if (attempts_.fetch_add(1) == 0) {
        throw std::runtime_error {"transient sink failure"};
      }
      accepted_.consume(message);
    }

    /**
     * @brief Count attempted deliveries including the failed delivery.
     *
     * @return Number of callbacks executed.
     */
    int attempts() const {
      return attempts_.load();
    }

    /**
     * @brief Read messages delivered after the transient failure.
     *
     * @return Successfully accepted records.
     */
    std::vector<delivery_t> snapshot() const {
      return accepted_.snapshot();
    }

  private:
    std::atomic<int> attempts_ {0};  ///< Counts callbacks safely on either delivery thread.
    recording_sink_t accepted_;  ///< Records callbacks after the first failure.
  };

  /**
   * @brief Hold the first asynchronous delivery while more records are queued.
   */
  class gated_sink_t final: public logging::sink_t {
  public:
    /**
     * @brief Bind a release signal used to unblock the consumer.
     *
     * @param release Signal that allows delivery to continue.
     */
    explicit gated_sink_t(std::shared_future<void> release):
        release_ {std::move(release)},
        started_ {started_signal_.get_future().share()} {
    }

    /**
     * @brief Wait on the first callback and record every accepted message.
     *
     * @param message Message accepted by this sink.
     */
    void consume(const logging::message_t &message) override {
      if (!first_started_.exchange(true)) {
        started_signal_.set_value();
        release_.wait();
      }
      accepted_.consume(message);
    }

    /**
     * @brief Obtain the signal indicating that the consumer is blocked.
     *
     * @return Signal set by the first delivery callback.
     */
    std::shared_future<void> started() const {
      return started_;
    }

    /**
     * @brief Read delivered records after the consumer has been released.
     *
     * @return Successfully delivered records.
     */
    std::vector<delivery_t> snapshot() const {
      return accepted_.snapshot();
    }

  private:
    std::shared_future<void> release_;  ///< Signal unblocking the first callback.
    std::promise<void> started_signal_;  ///< Signals entry into the first callback.
    std::shared_future<void> started_;  ///< Allows the producer to await that callback.
    std::atomic<bool> first_started_ {false};  ///< Ensures only the first callback waits.
    recording_sink_t accepted_;  ///< Stores all delivered messages in order.
  };

  /**
   * @brief A custom value supported by both streaming and standard formatting.
   */
  struct custom_value_t {
    std::string_view text;  ///< Text emitted by the custom formatters.
    int *formats = nullptr;  ///< Optional counter for standard formatter invocations.
  };

  /**
   * @brief Stream a custom value through a lazy logging callback.
   *
   * @param out Stream receiving the value.
   * @param value Custom value to print.
   * @return The output stream.
   */
  std::ostream &operator<<(std::ostream &out, const custom_value_t &value) {
    return out << value.text;
  }
}  // namespace

/**
 * @brief Format a custom test value with the standard string formatting options.
 */
template<>
struct std::formatter<custom_value_t>: std::formatter<std::string_view> {
  /**
   * @brief Delegate formatting of the custom value to its text.
   *
   * @tparam FormatContext Type of the destination formatting context.
   * @param value Custom value to print.
   * @param context Destination and formatting state.
   * @return Iterator after the formatted value.
   */
  template<typename FormatContext>
  auto format(const custom_value_t &value, FormatContext &context) const {
    if (value.formats) {
      ++*value.formats;
    }
    return std::formatter<std::string_view>::format(value.text, context);
  }
};

namespace {
  /**
   * @brief Named severity functions and the metadata expected from their overloads.
   */
  struct severity_case_t {
    logging::level_e level;  ///< Expected message severity.
    std::string_view label;  ///< Severity label used by the text formatter.
    void (*emit)();  ///< Emits text, lazy-stream, and standard-format messages.
  };

  const std::array severity_cases {
    severity_case_t {logging::level_e::verbose, "Verbose", []() {
                       logging::verbose("literal {braces}");
                       logging::verbose([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::verbose("formatted {}", 17);
                     }},
    severity_case_t {logging::level_e::debug, "Debug", []() {
                       logging::debug("literal {braces}");
                       logging::debug([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::debug("formatted {}", 17);
                     }},
    severity_case_t {logging::level_e::info, "Info", []() {
                       logging::info("literal {braces}");
                       logging::info([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::info("formatted {}", 17);
                     }},
    severity_case_t {logging::level_e::warning, "Warning", []() {
                       logging::warning("literal {braces}");
                       logging::warning([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::warning("formatted {}", 17);
                     }},
    severity_case_t {logging::level_e::error, "Error", []() {
                       logging::error("literal {braces}");
                       logging::error([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::error("formatted {}", 17);
                     }},
    severity_case_t {logging::level_e::fatal, "Fatal", []() {
                       logging::fatal("literal {braces}");
                       logging::fatal([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::fatal("formatted {}", 17);
                     }},
    severity_case_t {logging::test_level, "Tests", []() {
                       logging::tests("literal {braces}");
                       logging::tests([](std::ostream &out) {
                         out << "stream";
                       });
                       logging::tests("formatted {}", 17);
                     }},
  };  ///< All production severities and the linked test severity.

  /**
   * @brief Check named logging overloads and text formatting at each severity.
   */
  class LoggingSeverityTest: public testing::TestWithParam<severity_case_t> {};

  /**
   * @brief Check sink behavior with both synchronous and asynchronous delivery.
   */
  class LoggingSinkTest: public testing::TestWithParam<bool> {};
}  // namespace

/**
 * @brief Every named severity function forwards all three supported message forms.
 */
TEST_P(LoggingSeverityTest, NamedFunctionsPreserveSeverityAndMessageForms) {
  const auto test_case = GetParam();
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);

  EXPECT_TRUE(logging::is_enabled(test_case.level));
  test_case.emit();
  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 3U);
  EXPECT_EQ(messages[0].message.text, "literal {braces}");
  EXPECT_EQ(messages[1].message.text, "stream");
  EXPECT_EQ(messages[2].message.text, "formatted 17");
  for (const auto &delivery : messages) {
    EXPECT_EQ(delivery.message.level, test_case.level);
    EXPECT_EQ(delivery.thread, std::this_thread::get_id());
  }
}

/**
 * @brief Formatting uses the supplied timestamp and severity without adding a newline.
 */
TEST_P(LoggingSeverityTest, FormatsSuppliedMetadataAndPreservesMessageText) {
  std::tm local_time {};
  local_time.tm_year = 124;
  local_time.tm_mon = 0;
  local_time.tm_mday = 2;
  local_time.tm_hour = 3;
  local_time.tm_min = 4;
  local_time.tm_sec = 5;
  local_time.tm_isdst = -1;
  const auto timestamp = std::chrono::system_clock::from_time_t(std::mktime(&local_time)) + std::chrono::milliseconds {123};
  const logging::message_t message {GetParam().level, timestamp, "first line\nsecond line"};

  EXPECT_EQ(logging::format_message(message), std::format("[2024-01-02 03:04:05.123]: {}: first line\nsecond line", GetParam().label));
}

INSTANTIATE_TEST_SUITE_P(AllSeverities, LoggingSeverityTest, testing::ValuesIn(severity_cases));

/**
 * @brief Rejected records do not invoke builders, and the scoped setting is restored.
 */
TEST(LoggingApiTest, RejectedRecordsDoNotEvaluateLazyBuilders) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  int calls = 0;
  int formats = 0;
  {
    logging::testing::scoped_disable_t disabled;
    EXPECT_FALSE(logging::is_enabled(logging::level_e::info));
    logging::log(logging::level_e::info, [&calls](std::ostream &out) {
      ++calls;
      out << "filtered generic message";
    });
    logging::info([&calls](std::ostream &out) {
      ++calls;
      out << "filtered named message";
    });
    logging::log(logging::level_e::info, "filtered text");
    logging::log(logging::level_e::info, "{}", custom_value_t {"filtered generic format", &formats});
    logging::info("{}", custom_value_t {"filtered named format", &formats});
    {
      logging::testing::scoped_disable_t nested;
      EXPECT_FALSE(logging::is_enabled(logging::level_e::info));
    }
    EXPECT_FALSE(logging::is_enabled(logging::level_e::info));
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(formats, 0);
    EXPECT_TRUE(sink->snapshot().empty());
  }

  EXPECT_TRUE(logging::is_enabled(logging::level_e::info));
  logging::info([&calls](std::ostream &out) {
    ++calls;
    out << "accepted after restoration";
  });
  EXPECT_EQ(calls, 1);
  ASSERT_EQ(sink->snapshot().size(), 1U);
  EXPECT_EQ(sink->snapshot().front().message.text, "accepted after restoration");
}

/**
 * @brief Accepted builders execute once and preserve existing stream formatting.
 */
TEST(LoggingApiTest, LazyBuilderPreservesManipulatorsBoostFormatAndCustomValues) {
  auto first = std::make_shared<recording_sink_t>();
  auto second = std::make_shared<recording_sink_t>();
  auto first_owner = logging::add_sink(first, 0, false);
  auto second_owner = logging::add_sink(second, 0, false);
  int calls = 0;

  logging::log(logging::level_e::info, [&calls](std::ostream &out) {
    ++calls;
    out << std::hex << std::setfill('0') << std::setw(4) << 42 << ' '
        << std::boolalpha << true << std::endl
        << boost::format("%1%: %2%") % "Boost" % 7 << ' '
        << custom_value_t {"custom"};
  });

  EXPECT_EQ(calls, 1);
  ASSERT_EQ(first->snapshot().size(), 1U);
  ASSERT_EQ(second->snapshot().size(), 1U);
  EXPECT_EQ(first->snapshot().front().message.text, "002a true\nBoost: 7 custom");
  EXPECT_EQ(second->snapshot().front().message.text, first->snapshot().front().message.text);
}

/**
 * @brief Standard format arguments retain width, numeric, and user-defined formatting.
 */
TEST(LoggingApiTest, StandardFormatOverloadUsesTypedFormatting) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);

  logging::log(logging::level_e::warning, "{:04x} {:.2f} {:>8}", 42, 1.25, custom_value_t {"custom"});

  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message.level, logging::level_e::warning);
  EXPECT_EQ(messages.front().message.text, "002a 1.25   custom");
}

/**
 * @brief A throwing builder discards partial text without disabling subsequent records.
 */
TEST(LoggingApiTest, BuilderExceptionDoesNotPublishPartialMessage) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);

  EXPECT_THROW(logging::info([](std::ostream &out) {
                 out << "partial text that must be discarded";
                 throw std::runtime_error {"builder failure"};
               }),
               std::runtime_error);
  EXPECT_TRUE(sink->snapshot().empty());

  logging::info("record after builder failure");
  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message.text, "record after builder failure");
}

/**
 * @brief All sinks receive one message timestamp while keeping their delivery policy.
 */
TEST(LoggingApiTest, SynchronousAndAsynchronousSinksReceiveIdenticalMetadata) {
  auto synchronous = std::make_shared<recording_sink_t>();
  auto asynchronous = std::make_shared<recording_sink_t>();
  auto synchronous_owner = logging::add_sink(synchronous, 0, false);
  auto asynchronous_owner = logging::add_sink(asynchronous);
  const auto before = std::chrono::system_clock::now();

  logging::error("shared message metadata");
  const auto after = std::chrono::system_clock::now();
  ASSERT_TRUE(asynchronous->wait_for_messages(1));
  logging::log_flush();

  const auto immediate = synchronous->snapshot();
  const auto deferred = asynchronous->snapshot();
  ASSERT_EQ(immediate.size(), 1U);
  ASSERT_EQ(deferred.size(), 1U);
  EXPECT_EQ(immediate.front().message.level, logging::level_e::error);
  EXPECT_EQ(deferred.front().message.level, immediate.front().message.level);
  EXPECT_EQ(deferred.front().message.text, immediate.front().message.text);
  EXPECT_EQ(deferred.front().message.timestamp, immediate.front().message.timestamp);
  EXPECT_GE(immediate.front().message.timestamp, before);
  EXPECT_LE(immediate.front().message.timestamp, after);
  EXPECT_EQ(immediate.front().thread, std::this_thread::get_id());
  EXPECT_NE(deferred.front().thread, std::this_thread::get_id());
}

/**
 * @brief Each sink filters independently without preventing other sinks from receiving.
 */
TEST(LoggingApiTest, SinkMinimumSeverityOnlyFiltersThatSink) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, static_cast<int>(logging::level_e::warning), false);

  logging::verbose("filtered verbose");
  logging::info("filtered info");
  logging::warning("accepted warning");
  logging::fatal("accepted fatal");
  logging::tests("accepted test severity");

  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 3U);
  EXPECT_EQ(messages[0].message.level, logging::level_e::warning);
  EXPECT_EQ(messages[1].message.level, logging::level_e::fatal);
  EXPECT_EQ(messages[2].message.level, logging::test_level);
}

/**
 * @brief Destroying one sink registration preserves independent sink registrations.
 */
TEST(LoggingApiTest, RemovingOneOwnerLeavesOtherSinksRegistered) {
  auto first = std::make_shared<recording_sink_t>();
  auto second = std::make_shared<recording_sink_t>();
  auto first_owner = logging::add_sink(first, 0, false);
  auto second_owner = logging::add_sink(second, 0, false);

  logging::info("both owners active");
  first_owner.reset();
  logging::info("second owner active");

  ASSERT_EQ(first->snapshot().size(), 1U);
  const auto remaining = second->snapshot();
  ASSERT_EQ(remaining.size(), 2U);
  EXPECT_EQ(remaining[1].message.text, "second owner active");
}

/**
 * @brief An asynchronous owner waits for its consumer and drains queued owned messages.
 */
TEST(LoggingApiTest, AsynchronousRemovalDrainsPendingMessages) {
  std::promise<void> release;
  auto sink = std::make_shared<gated_sink_t>(release.get_future().share());
  auto owner = logging::add_sink(sink);
  logging::debug("queued {}", 0);
  const auto started = sink->started().wait_for(std::chrono::seconds {5});
  EXPECT_EQ(started, std::future_status::ready);
  if (started != std::future_status::ready) {
    release.set_value();
    owner.reset();
    return;
  }

  for (int index = 1; index < 32; ++index) {
    auto text = std::format("queued {}", index);
    logging::log(logging::level_e::debug, std::string_view {text});
    text.assign("producer buffer overwritten");
  }

  std::promise<void> removed;
  auto completion = removed.get_future();
  std::jthread remover {[owner = std::move(owner), &removed]() mutable {
    owner.reset();
    removed.set_value();
  }};
  EXPECT_EQ(completion.wait_for(std::chrono::milliseconds {50}), std::future_status::timeout);
  release.set_value();
  remover.join();

  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 32U);
  for (std::size_t index = 0; index < messages.size(); ++index) {
    EXPECT_EQ(messages[index].message.text, std::format("queued {}", index));
  }
}

/**
 * @brief A registration retains its sink until queued work and ownership are released.
 */
TEST_P(LoggingSinkTest, OwnerRetainsSinkUntilRemoval) {
  auto sink = std::make_shared<recording_sink_t>();
  const std::weak_ptr<recording_sink_t> retained = sink;
  auto owner = logging::add_sink(sink, 0, GetParam());
  logging::info("sink is retained");
  logging::log_flush();
  ASSERT_EQ(sink->snapshot().size(), 1U);

  sink.reset();
  EXPECT_FALSE(retained.expired());
  owner.reset();
  EXPECT_TRUE(retained.expired());
}

/**
 * @brief Suppressed backend exceptions do not stop future messages or other sinks.
 */
TEST_P(LoggingSinkTest, BackendExceptionDoesNotPreventLaterRecords) {
  auto failing = std::make_shared<failing_sink_t>();
  auto healthy = std::make_shared<recording_sink_t>();
  auto failing_owner = logging::add_sink(failing, 0, GetParam());
  auto healthy_owner = logging::add_sink(healthy, 0, false);

  EXPECT_NO_THROW(logging::warning("first backend attempt fails"));
  EXPECT_NO_THROW(logging::warning("later backend attempt succeeds"));
  logging::log_flush();

  EXPECT_EQ(failing->attempts(), 2);
  const auto delivered = failing->snapshot();
  ASSERT_EQ(delivered.size(), 1U);
  EXPECT_EQ(delivered.front().message.text, "later backend attempt succeeds");
  EXPECT_EQ(healthy->snapshot().size(), 2U);
}

INSTANTIATE_TEST_SUITE_P(DeliveryPolicies, LoggingSinkTest, testing::Bool());

/**
 * @brief Periodic integer samples retain integer extrema and a fractional average.
 */
TEST(LoggingApiTest, PeriodicIntegerLoggerPreservesFormattingAndReset) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  logging::min_max_avg_periodic_logger<int> logger {logging::level_e::debug, "integer samples", "ms", std::chrono::seconds {0}};
  ASSERT_TRUE(logger.is_enabled());

  logger.collect_and_log(7);
  std::this_thread::sleep_for(std::chrono::milliseconds {5});
  logger.collect_and_log(std::function<int()> {[]() {
    return 9;
  }});
  logger.reset();
  logger.collect_and_log(10);
  std::this_thread::sleep_for(std::chrono::milliseconds {5});
  logger.collect_and_log(12);

  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 2U);
  EXPECT_EQ(messages[0].message.level, logging::level_e::debug);
  EXPECT_EQ(messages[0].message.text, "integer samples (min/max/avg): 7ms/7ms/7.00ms");
  EXPECT_EQ(messages[1].message.text, "integer samples (min/max/avg): 10ms/10ms/10.00ms");
}

/**
 * @brief Floating periodic samples preserve two fractional digits for every aggregate.
 */
TEST(LoggingApiTest, PeriodicFloatingLoggerPreservesTwoDecimalPlaces) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  logging::min_max_avg_periodic_logger<double> logger {logging::level_e::debug, "floating samples", "ms", std::chrono::seconds {0}};

  logger.collect_and_log(1.234);
  std::this_thread::sleep_for(std::chrono::milliseconds {5});
  logger.collect_and_log(5.0);

  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message.text, "floating samples (min/max/avg): 1.23ms/1.23ms/1.23ms");
}

/**
 * @brief Elapsed-time logging retains the millisecond units and caller-supplied points.
 */
TEST(LoggingApiTest, TimeDeltaLoggerPreservesElapsedMilliseconds) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  logging::time_delta_periodic_logger logger {logging::level_e::debug, "elapsed", std::chrono::seconds {0}};
  ASSERT_TRUE(logger.is_enabled());
  const auto start = std::chrono::steady_clock::now();

  logger.first_point(start);
  logger.second_point_and_log(start + std::chrono::microseconds {1250});
  std::this_thread::sleep_for(std::chrono::milliseconds {5});
  logger.second_point_and_log(start + std::chrono::microseconds {2500});
  logger.reset();
  logger.first_point_now();
  logger.second_point_now_and_log();

  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message.text, "elapsed (min/max/avg): 1.25ms/1.25ms/1.25ms");
}

/**
 * @brief Disabled periodic loggers do not evaluate sample functions or emit records.
 */
TEST(LoggingApiTest, DisabledPeriodicLoggersDoNotEvaluateSamples) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  logging::testing::scoped_disable_t disabled;
  logging::min_max_avg_periodic_logger<int> samples {logging::level_e::debug, "disabled samples", "ms", std::chrono::seconds {0}};
  logging::time_delta_periodic_logger elapsed {logging::level_e::debug, "disabled elapsed", std::chrono::seconds {0}};
  int calls = 0;

  EXPECT_FALSE(samples.is_enabled());
  samples.collect_and_log(std::function<int()> {[&calls]() {
    return ++calls;
  }});
  samples.collect_and_log(1);
  samples.reset();
  EXPECT_FALSE(elapsed.is_enabled());
  const auto point = std::chrono::steady_clock::now();
  elapsed.first_point(point);
  elapsed.second_point_and_log(point + std::chrono::milliseconds {1});
  elapsed.first_point_now();
  elapsed.second_point_now_and_log();
  elapsed.reset();

  EXPECT_EQ(calls, 0);
  EXPECT_TRUE(sink->snapshot().empty());
}

#ifndef __ANDROID__
/**
 * @brief FFmpeg messages retain their mapped severity and text through the shared source.
 */
TEST(LoggingApiTest, FfmpegCallbackPreservesSeverityMapping) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  const auto previous_level = av_log_get_level();
  logging::setup_av_logging(0);
  const std::array levels {
    std::pair {AV_LOG_FATAL, logging::level_e::error},
    std::pair {AV_LOG_ERROR, logging::level_e::error},
    std::pair {AV_LOG_WARNING, logging::level_e::warning},
    std::pair {AV_LOG_INFO, logging::level_e::info},
    std::pair {AV_LOG_VERBOSE, logging::level_e::debug},
    std::pair {AV_LOG_DEBUG, logging::level_e::verbose},
  };
  for (std::size_t index = 0; index < levels.size(); ++index) {
    av_log(nullptr, levels[index].first, "FFmpeg bridge %zu\n", index);
  }
  av_log_set_level(previous_level);
  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), levels.size());
  for (std::size_t index = 0; index < levels.size(); ++index) {
    EXPECT_EQ(messages[index].message.level, levels[index].second);
    EXPECT_EQ(messages[index].message.text, std::format("FFmpeg bridge {}\n", index));
  }
}

/**
 * @brief libdisplaydevice messages preserve every severity while unknown levels are ignored.
 */
TEST(LoggingApiTest, DisplayDeviceCallbackPreservesSeverityMapping) {
  auto sink = std::make_shared<recording_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  using display_level_e = display_device::Logger::LogLevel;
  auto &logger = display_device::Logger::get();
  const std::array levels {
    std::pair {display_level_e::verbose, logging::level_e::verbose},
    std::pair {display_level_e::debug, logging::level_e::debug},
    std::pair {display_level_e::info, logging::level_e::info},
    std::pair {display_level_e::warning, logging::level_e::warning},
    std::pair {display_level_e::error, logging::level_e::error},
    std::pair {display_level_e::fatal, logging::level_e::fatal},
  };
  auto previous_level = display_level_e::fatal;
  for (const auto &entry : levels) {
    if (logger.isLogLevelEnabled(entry.first)) {
      previous_level = entry.first;
      break;
    }
  }
  logging::setup_libdisplaydevice_logging(0);
  for (std::size_t index = 0; index < levels.size(); ++index) {
    logger.write(levels[index].first, std::format("Display device bridge {}", index));
  }
  logger.write(static_cast<display_level_e>(99), "unsupported display device severity");
  logger.setLogLevel(previous_level);
  const auto messages = sink->snapshot();
  ASSERT_EQ(messages.size(), levels.size());
  for (std::size_t index = 0; index < levels.size(); ++index) {
    EXPECT_EQ(messages[index].message.level, levels[index].second);
    EXPECT_EQ(messages[index].message.text, std::format("Display device bridge {}", index));
  }
}
#endif

/**
 * @brief Null sinks and unsupported severity values fail before accepting records.
 */
TEST(LoggingApiTest, RejectsNullSinksAndInvalidSeverities) {
  EXPECT_THROW(static_cast<void>(logging::add_sink(std::shared_ptr<logging::sink_t> {})), std::invalid_argument);
  const auto invalid = static_cast<logging::level_e>(99);
  EXPECT_THROW(logging::is_enabled(invalid), std::invalid_argument);
  EXPECT_THROW(logging::log(invalid, "invalid text message"), std::invalid_argument);
  EXPECT_THROW(logging::log(invalid, "invalid formatted {}", 1), std::invalid_argument);
  int calls = 0;
  EXPECT_THROW(logging::log(invalid, [&](std::ostream &) {
                 ++calls;
               }),
               std::invalid_argument);
  EXPECT_EQ(calls, 0);
  EXPECT_THROW(logging::format_message({invalid, std::chrono::system_clock::now(), "invalid format severity"}), std::invalid_argument);
}
