/**
 * @file tests/unit/test_input.cpp
 * @brief Tests for input packet batching, retained stream input, and virtual gamepad lifecycle behavior.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// moonlight-common-c includes
extern "C" {
#include <moonlight-common-c/src/Input.h>
}

// local includes
#include "src/config.h"
#include "src/file_handler.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/platform/virtualhid_input.h"
#include "src/utility.h"

namespace {
  /**
   * @brief Count packet diagnostics without including logs from background threads.
   */
  class packet_log_sink_t final: public logging::sink_t {
  public:
    /**
     * @brief Count a synchronous record emitted on the test's calling thread.
     */
    void consume(const logging::message_t &) override {
      if (std::this_thread::get_id() == calling_thread_) {
        ++record_count_;
      }
    }

    /**
     * @brief Read the number of diagnostics emitted on the test's calling thread.
     * @return Number of records observed through the synchronous sink.
     */
    std::size_t record_count() const {
      return record_count_;
    }

  private:
    const std::thread::id calling_thread_ {std::this_thread::get_id()};  ///< Calling thread whose diagnostics are observed.
    std::size_t record_count_ {};  ///< Number of synchronous diagnostics observed.
  };

  /**
   * @brief Protocol metadata for a fixed-size input packet.
   */
  struct input_packet_spec_t {
    std::uint32_t magic;  ///< Packet type identifier.
    std::size_t size;  ///< Complete fixed packet size.
  };

  /**
   * @brief Create raw input bytes with a caller-selected header and buffer size.
   *
   * @param magic Packet type identifier.
   * @param declared_size Packet size declared after the size field.
   * @param actual_size Number of bytes available in the packet buffer.
   * @return Raw packet bytes.
   */
  std::vector<std::uint8_t> make_input_packet(std::uint32_t magic, std::uint32_t declared_size, std::size_t actual_size) {
    std::vector<std::uint8_t> packet(actual_size);
    const NV_INPUT_HEADER header {
      util::endian::big(declared_size),
      util::endian::little(magic),
    };
    if (!packet.empty()) {
      std::memcpy(packet.data(), &header, std::min(packet.size(), sizeof(header)));
    }
    return packet;
  }

  /**
   * @brief Verify batching a repeated packet around a potentially different packet.
   *
   * @tparam Packet Fixed-size protocol packet type.
   * @param original Packet repeated before and after the different packet.
   * @param different Packet that may introduce a batching boundary.
   * @param magic Protocol identifier shared by the packets.
   * @param expected Expected number of dispatched packets.
   */
  template<class Packet>
  void expect_packet_dispatches(input::service_t &inputs_, Packet original, Packet different, std::uint32_t magic, std::size_t expected) {
    SCOPED_TRACE(magic);
    auto stream_input = inputs_.alloc(std::make_shared<safe::mail_raw_t>(), "packet-batching");
    ASSERT_NE(stream_input, nullptr);
    std::size_t dispatch_count = 0;
    input::testing::set_input_packet_hook(inputs_, [magic, &dispatch_count](std::uint32_t dispatched_magic) {
      EXPECT_EQ(dispatched_magic, magic);
      ++dispatch_count;
    });
    for (auto packet : {original, different, original}) {
      packet.header.size = util::endian::big<std::uint32_t>(sizeof(packet) - sizeof(packet.header.size));
      packet.header.magic = util::endian::little(magic);
      std::vector<std::uint8_t> bytes(sizeof(packet));
      std::memcpy(bytes.data(), &packet, sizeof(packet));
      input::passthrough(stream_input, std::move(bytes));
    }
    input::testing::process_queued_messages(stream_input);
    input::testing::set_input_packet_hook(inputs_, {});
    EXPECT_EQ(dispatch_count, expected);
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);
  }

  /**
   * @brief Fixture that installs a fake global virtual input backend.
   */
  class InputGamepadSessionTest: public ::testing::Test {
  protected:
    /**
     * @brief Preserve configuration and install observable fake devices.
     */
    void SetUp() override {
      config_.edit_startup().input.controller = true;
      config_.edit_startup().input.mouse = true;
      config_.edit_startup().input.gamepad = "xseries";

      auto platform_input = platf::input(config_.values(), infrastructure_.scheduler());
      ASSERT_TRUE(platform_input);
      auto &context = platf::virtualhid::get_input_context(platform_input);
      context = platf::virtualhid::input_context_t {config_.values(), lvh::BackendKind::fake};
      context_ = &context;
      runtime_ = context.runtime.get();
      ASSERT_NE(runtime_, nullptr);
      input::testing::set_platform_input(inputs_, std::move(platform_input));
    }

    /**
     * @brief Destroy retained test sessions and restore configuration.
     */
    void TearDown() override {
      input::testing::set_input_packet_hook(inputs_, {});
      input::testing::set_input_task_sink(inputs_, {});
      inputs_.terminate_gamepads();
      input::testing::set_platform_input(inputs_, {});
      context_ = nullptr;
      runtime_ = nullptr;
    }

    /**
     * @brief Access the fake runtime installed for the current test.
     *
     * @return Fake libvirtualhid runtime.
     */
    lvh::Runtime &runtime() const {
      return *runtime_;
    }

    /**
     * @brief Access the shared libvirtualhid input context installed for the test.
     *
     * @return Fake input context.
     */
    platf::virtualhid::input_context_t &context() const {
      return *context_;
    }

    /**
     * @brief Access configuration for input dispatch and persisted backend preferences.
     *
     * @return Fixture-owned configuration store.
     */
    config::store_t &configuration() {
      return config_;
    }

    /**
     * @brief Access the host input service exercised by the current test.
     *
     * @return Fixture-owned host input service.
     */
    input::service_t &inputs() {
      return inputs_;
    }

    /**
     * @brief Access the scheduler for serialization and worker lifecycle assertions.
     *
     * @return Fixture-owned scheduler.
     */
    thread_pool_util::ThreadPool &scheduler() {
      return infrastructure_.scheduler();
    }

    /**
     * @brief Retain the host mailbox when constructing another input service.
     *
     * @return Fixture-owned host mailbox handle.
     */
    safe::mail_t mailbox() const {
      return infrastructure_.mailbox();
    }

    /**
     * @brief Access the cursor state shared with another input service.
     *
     * @return Fixture-owned cursor state.
     */
    bool &cursor() {
      return infrastructure_.cursor();
    }

  private:
    config::store_t config_;  ///< Configuration owned by this fixture.
    runtime::infrastructure_t infrastructure_;  ///< Infrastructure owned by this input fixture.
    input::service_t inputs_ {config_.values(), infrastructure_.scheduler(), infrastructure_.mailbox(), infrastructure_.cursor()};  ///< Fixture-owned host input service.
    platf::virtualhid::input_context_t *context_ = nullptr;  ///< Fake input context installed in the global backend.
    lvh::Runtime *runtime_ = nullptr;  ///< Fake runtime installed in the global input backend.
  };
}  // namespace

/**
 * @brief Unknown input types emit no diagnostic while recognized packets still log.
 */
TEST(InputPacketPrintTest, UnknownMagicIsASilentNoOp) {
  auto sink = std::make_shared<packet_log_sink_t>();
  auto owner = logging::add_sink(sink, 0, false);
  NV_REL_MOUSE_MOVE_PACKET recognized {};
  recognized.header.magic = util::endian::little<std::uint32_t>(MOUSE_MOVE_REL_MAGIC_GEN5);
  input::print(static_cast<void *>(&recognized));
  ASSERT_EQ(sink->record_count(), 1U);

  for (const auto magic : {0U, 0x12345678U, 0xFFFFFFFFU}) {
    NV_INPUT_HEADER unknown {
      util::endian::big<std::uint32_t>(sizeof(unknown.magic)),
      util::endian::little(magic),
    };
    input::print(static_cast<void *>(&unknown));
    EXPECT_EQ(sink->record_count(), 1U) << magic;
  }
}

TEST(InputPacketValidationTest, RejectsEveryBufferShorterThanTheHeader) {
  for (std::size_t actual_size = 0; actual_size < sizeof(NV_INPUT_HEADER); ++actual_size) {
    const auto packet = make_input_packet(UTF8_TEXT_EVENT_MAGIC, sizeof(std::uint32_t), actual_size);
    EXPECT_FALSE(input::testing::is_valid_input_packet(packet)) << "actual_size=" << actual_size;
  }
}

TEST(InputPacketValidationTest, RejectsDeclaredSizesOutsideTheAvailableBuffer) {
  constexpr std::uint32_t unknown_magic = 0x12345678;
  for (std::uint32_t declared_size = 0; declared_size < sizeof(std::uint32_t); ++declared_size) {
    const auto packet = make_input_packet(unknown_magic, declared_size, sizeof(NV_INPUT_HEADER));
    EXPECT_FALSE(input::testing::is_valid_input_packet(packet)) << "declared_size=" << declared_size;
  }

  const auto truncated_packet = make_input_packet(unknown_magic, sizeof(std::uint32_t) + 1, sizeof(NV_INPUT_HEADER));
  EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_packet));

  const auto overflowing_size = make_input_packet(
    unknown_magic,
    std::numeric_limits<std::uint32_t>::max(),
    sizeof(NV_INPUT_HEADER)
  );
  EXPECT_FALSE(input::testing::is_valid_input_packet(overflowing_size));
}

TEST(InputPacketValidationTest, ValidatesUnicodePacketBoundsWithoutOverflow) {
  const auto empty_packet = make_input_packet(UTF8_TEXT_EVENT_MAGIC, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER));
  EXPECT_TRUE(input::testing::is_valid_input_packet(empty_packet));

  constexpr std::uint32_t maximum_declared_size = sizeof(std::uint32_t) + UTF8_TEXT_EVENT_MAX_COUNT;
  const auto maximum_packet = make_input_packet(
    UTF8_TEXT_EVENT_MAGIC,
    maximum_declared_size,
    sizeof(std::uint32_t) + maximum_declared_size
  );
  EXPECT_TRUE(input::testing::is_valid_input_packet(maximum_packet));

  const auto oversized_text = make_input_packet(
    UTF8_TEXT_EVENT_MAGIC,
    maximum_declared_size + 1,
    sizeof(std::uint32_t) + maximum_declared_size + 1
  );
  EXPECT_FALSE(input::testing::is_valid_input_packet(oversized_text));

  const auto truncated_text = make_input_packet(UTF8_TEXT_EVENT_MAGIC, maximum_declared_size, sizeof(NV_INPUT_HEADER));
  EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_text));
}

TEST(InputPacketValidationTest, EnforcesEveryFixedPacketSize) {
  constexpr std::array packet_specs {
    input_packet_spec_t {MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET)},
    input_packet_spec_t {MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET)},
    input_packet_spec_t {SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET)},
    input_packet_spec_t {SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET)},
    input_packet_spec_t {KEY_DOWN_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET)},
    input_packet_spec_t {KEY_UP_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET)},
    input_packet_spec_t {MULTI_CONTROLLER_MAGIC_GEN5, sizeof(NV_MULTI_CONTROLLER_PACKET)},
    input_packet_spec_t {SS_TOUCH_MAGIC, sizeof(SS_TOUCH_PACKET)},
    input_packet_spec_t {SS_PEN_MAGIC, sizeof(SS_PEN_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_ARRIVAL_MAGIC, sizeof(SS_CONTROLLER_ARRIVAL_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_TOUCH_MAGIC, sizeof(SS_CONTROLLER_TOUCH_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_MOTION_MAGIC, sizeof(SS_CONTROLLER_MOTION_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_BATTERY_MAGIC, sizeof(SS_CONTROLLER_BATTERY_PACKET)},
  };

  for (const auto &[magic, size] : packet_specs) {
    const auto declared_size = static_cast<std::uint32_t>(size - sizeof(std::uint32_t));
    const auto valid_packet = make_input_packet(magic, declared_size, size);
    EXPECT_TRUE(input::testing::is_valid_input_packet(valid_packet)) << "magic=" << magic;

    const auto trailing_bytes = make_input_packet(magic, declared_size, size + 8);
    EXPECT_TRUE(input::testing::is_valid_input_packet(trailing_bytes)) << "magic=" << magic;

    const auto wrong_declared_size = make_input_packet(magic, declared_size - 1, size);
    EXPECT_FALSE(input::testing::is_valid_input_packet(wrong_declared_size)) << "magic=" << magic;

    const auto oversized_declared_size = make_input_packet(magic, declared_size + 1, size + 1);
    EXPECT_FALSE(input::testing::is_valid_input_packet(oversized_declared_size)) << "magic=" << magic;

    const auto truncated_packet = make_input_packet(magic, declared_size, size - 1);
    EXPECT_FALSE(input::testing::is_valid_input_packet(truncated_packet)) << "magic=" << magic;
  }
}

TEST(InputPacketValidationTest, PreservesUnknownPacketHandlingWithinDeclaredBounds) {
  constexpr std::uint32_t unknown_magic = 0x12345678;
  const auto packet = make_input_packet(unknown_magic, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER));
  EXPECT_TRUE(input::testing::is_valid_input_packet(packet));
}

TEST_F(InputGamepadSessionTest, RejectsMalformedBatchablePacketsAtQueueIngress) {
  ASSERT_FALSE(scheduler().running());
  const std::shared_ptr<input::input_t> empty_input;
  EXPECT_EQ(input::testing::queued_input_packet_count(empty_input), 0);

  auto stream_input = inputs().alloc(std::make_shared<safe::mail_raw_t>(), "packet-validation-client");
  ASSERT_NE(stream_input, nullptr);

  auto short_header = make_input_packet(SS_PEN_MAGIC, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER) - 1);
  input::passthrough(stream_input, std::move(short_header));
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);

  constexpr std::array batchable_packet_specs {
    input_packet_spec_t {MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET)},
    input_packet_spec_t {SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET)},
    input_packet_spec_t {SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET)},
    input_packet_spec_t {MULTI_CONTROLLER_MAGIC_GEN5, sizeof(NV_MULTI_CONTROLLER_PACKET)},
    input_packet_spec_t {SS_TOUCH_MAGIC, sizeof(SS_TOUCH_PACKET)},
    input_packet_spec_t {SS_PEN_MAGIC, sizeof(SS_PEN_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_TOUCH_MAGIC, sizeof(SS_CONTROLLER_TOUCH_PACKET)},
    input_packet_spec_t {SS_CONTROLLER_MOTION_MAGIC, sizeof(SS_CONTROLLER_MOTION_PACKET)},
  };

  std::size_t valid_packet_count = 0;
  for (const auto &[magic, size] : batchable_packet_specs) {
    const auto declared_size = static_cast<std::uint32_t>(size - sizeof(std::uint32_t));

    auto truncated_packet = make_input_packet(magic, declared_size, size - 1);
    input::passthrough(stream_input, std::move(truncated_packet));
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;

    auto valid_packet = make_input_packet(magic, declared_size, size);
    input::passthrough(stream_input, std::move(valid_packet));
    ++valid_packet_count;
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;

    auto oversized_declared_size = make_input_packet(magic, declared_size + 1, size + 1);
    input::passthrough(stream_input, std::move(oversized_declared_size));
    EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), valid_packet_count) << "magic=" << magic;
  }
}

TEST_F(InputGamepadSessionTest, SerializesArrivalAndStatePacketsForOneController) {
  ASSERT_FALSE(scheduler().running());
  configuration().edit_startup().input.gamepad = "auto";

  auto stream_input = inputs().alloc(std::make_shared<safe::mail_raw_t>(), "simultaneous-controller-packets");
  ASSERT_NE(stream_input, nullptr);
  std::vector<std::shared_ptr<input::input_t>> scheduled;
  input::testing::set_input_task_sink(inputs(), [&](std::shared_ptr<input::input_t> input) {
    scheduled.push_back(std::move(input));
  });

  SS_CONTROLLER_ARRIVAL_PACKET arrival {};
  arrival.header.size = util::endian::big<std::uint32_t>(sizeof(arrival) - sizeof(arrival.header.size));
  arrival.header.magic = util::endian::little(SS_CONTROLLER_ARRIVAL_MAGIC);
  arrival.controllerNumber = 0;
  arrival.type = LI_CTYPE_NINTENDO;

  NV_MULTI_CONTROLLER_PACKET state {};
  state.header.size = util::endian::big<std::uint32_t>(sizeof(state) - sizeof(state.header.size));
  state.header.magic = util::endian::little(MULTI_CONTROLLER_MAGIC_GEN5);
  state.controllerNumber = 0;
  state.activeGamepadMask = 1;

  std::vector<std::uint8_t> arrival_bytes(sizeof(arrival));
  std::memcpy(arrival_bytes.data(), &arrival, sizeof(arrival));
  input::passthrough(stream_input, std::move(arrival_bytes));
  ASSERT_EQ(scheduled.size(), 1);
  EXPECT_EQ(scheduled.front(), stream_input);

  std::promise<void> arrival_started;
  std::promise<void> release_arrival;
  const auto release_signal = release_arrival.get_future().share();
  input::testing::set_input_packet_hook(inputs(), [&](std::uint32_t magic) {
    if (magic == SS_CONTROLLER_ARRIVAL_MAGIC) {
      arrival_started.set_value();
      release_signal.wait();
    }
  });

  std::jthread worker {input::testing::process_queued_messages, stream_input};
  const auto arrival_status = arrival_started.get_future().wait_for(std::chrono::seconds {5});
  if (arrival_status != std::future_status::ready) {
    release_arrival.set_value();
    worker.join();
    FAIL() << "Controller arrival packet did not reach the dispatch hook";
  }

  std::vector<std::uint8_t> state_bytes(sizeof(state));
  std::memcpy(state_bytes.data(), &state, sizeof(state));
  input::passthrough(stream_input, std::move(state_bytes));
  EXPECT_EQ(scheduled.size(), 1);
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 1);

  auto other_input = inputs().alloc(std::make_shared<safe::mail_raw_t>(), "independent-input-stream");
  if (!other_input) {
    release_arrival.set_value();
    worker.join();
    FAIL() << "Could not create independent input stream";
  }
  input::passthrough(other_input, make_input_packet(0x12345678, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER)));
  EXPECT_EQ(scheduled.size(), 2);
  if (scheduled.size() == 2) {
    EXPECT_EQ(scheduled.back(), other_input);
  }
  const auto active_before_gamepad = runtime().active_device_count();
  release_arrival.set_value();
  worker.join();
  input::testing::set_input_packet_hook(inputs(), {});
  input::testing::process_queued_messages(other_input);

  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);
  EXPECT_EQ(input::testing::queued_input_packet_count(other_input), 0);
  EXPECT_EQ(runtime().active_device_count(), active_before_gamepad + 1);
  const auto gamepad_id = input::testing::gamepad_id(stream_input, 0);
  ASSERT_GE(gamepad_id, 0);
  const auto *adapter = platf::virtualhid::gamepad_adapter_for_testing(context(), gamepad_id);
  ASSERT_NE(adapter, nullptr);
  ASSERT_NE(adapter->gamepad(), nullptr);
  EXPECT_EQ(adapter->gamepad()->profile().gamepad_kind, lvh::GamepadProfileKind::switch_pro);
}

TEST_F(InputGamepadSessionTest, YieldsAfterBoundedPacketBatch) {
  ASSERT_FALSE(scheduler().running());
  auto stream_input = inputs().alloc(std::make_shared<safe::mail_raw_t>(), "bounded-input-batch");
  ASSERT_NE(stream_input, nullptr);

  std::vector<std::shared_ptr<input::input_t>> scheduled;
  input::testing::set_input_task_sink(inputs(), [&](std::shared_ptr<input::input_t> input) {
    scheduled.push_back(std::move(input));
  });

  constexpr std::uint32_t unknown_magic = 0x12345678;
  constexpr std::size_t packet_count = 33;
  for (std::size_t n = 0; n < packet_count; ++n) {
    input::passthrough(stream_input, make_input_packet(unknown_magic, sizeof(std::uint32_t), sizeof(NV_INPUT_HEADER)));
  }
  ASSERT_EQ(scheduled.size(), 1);

  input::testing::process_queued_messages(scheduled.front());
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 1);
  ASSERT_EQ(scheduled.size(), 2);
  input::testing::process_queued_messages(scheduled.back());
  EXPECT_EQ(input::testing::queued_input_packet_count(stream_input), 0);
  EXPECT_EQ(scheduled.size(), 2);
}

TEST_F(InputGamepadSessionTest, BatchesContinuousPacketsWithoutCrossingStateChanges) {
  ASSERT_FALSE(scheduler().running());
  configuration().edit_startup().input.mouse = false;
  configuration().edit_startup().input.controller = false;
  configuration().edit_startup().input.native_pen_touch = false;
  input::testing::set_input_task_sink(inputs(), [](std::shared_ptr<input::input_t>) {
  });

  NV_MULTI_CONTROLLER_PACKET controller {};
  expect_packet_dispatches(inputs(), controller, controller, MULTI_CONTROLLER_MAGIC_GEN5, 1);
  auto changed_controller = controller;
  changed_controller.controllerNumber = 1;
  expect_packet_dispatches(inputs(), controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 2);
  changed_controller = controller;
  changed_controller.activeGamepadMask = 1;
  expect_packet_dispatches(inputs(), controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 3);
  changed_controller = controller;
  changed_controller.buttonFlags = 1;
  expect_packet_dispatches(inputs(), controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 3);
  changed_controller = controller;
  changed_controller.buttonFlags2 = 1;
  expect_packet_dispatches(inputs(), controller, changed_controller, MULTI_CONTROLLER_MAGIC_GEN5, 3);

  SS_TOUCH_PACKET touch {};
  touch.eventType = LI_TOUCH_EVENT_MOVE;
  expect_packet_dispatches(inputs(), touch, touch, SS_TOUCH_MAGIC, 1);
  auto changed_touch = touch;
  changed_touch.pointerId = 1;
  expect_packet_dispatches(inputs(), touch, changed_touch, SS_TOUCH_MAGIC, 2);
  changed_touch = touch;
  changed_touch.eventType = LI_TOUCH_EVENT_DOWN;
  expect_packet_dispatches(inputs(), touch, changed_touch, SS_TOUCH_MAGIC, 3);
  changed_touch.eventType = LI_TOUCH_EVENT_HOVER;
  expect_packet_dispatches(inputs(), touch, changed_touch, SS_TOUCH_MAGIC, 3);

  SS_PEN_PACKET pen {};
  pen.eventType = LI_TOUCH_EVENT_MOVE;
  expect_packet_dispatches(inputs(), pen, pen, SS_PEN_MAGIC, 1);
  auto changed_pen = pen;
  changed_pen.eventType = LI_TOUCH_EVENT_DOWN;
  expect_packet_dispatches(inputs(), pen, changed_pen, SS_PEN_MAGIC, 3);
  changed_pen.eventType = LI_TOUCH_EVENT_HOVER;
  expect_packet_dispatches(inputs(), pen, changed_pen, SS_PEN_MAGIC, 3);
  changed_pen = pen;
  changed_pen.penButtons = 1;
  expect_packet_dispatches(inputs(), pen, changed_pen, SS_PEN_MAGIC, 3);
  changed_pen = pen;
  changed_pen.toolType = 1;
  expect_packet_dispatches(inputs(), pen, changed_pen, SS_PEN_MAGIC, 3);

  SS_CONTROLLER_TOUCH_PACKET controller_touch {};
  controller_touch.eventType = LI_TOUCH_EVENT_MOVE;
  expect_packet_dispatches(inputs(), controller_touch, controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 1);
  auto changed_controller_touch = controller_touch;
  changed_controller_touch.controllerNumber = 1;
  expect_packet_dispatches(inputs(), controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 2);
  changed_controller_touch = controller_touch;
  changed_controller_touch.pointerId = 1;
  expect_packet_dispatches(inputs(), controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 2);
  changed_controller_touch = controller_touch;
  changed_controller_touch.eventType = LI_TOUCH_EVENT_DOWN;
  expect_packet_dispatches(inputs(), controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 3);
  changed_controller_touch.eventType = LI_TOUCH_EVENT_HOVER;
  expect_packet_dispatches(inputs(), controller_touch, changed_controller_touch, SS_CONTROLLER_TOUCH_MAGIC, 3);

  SS_CONTROLLER_MOTION_PACKET motion {};
  expect_packet_dispatches(inputs(), motion, motion, SS_CONTROLLER_MOTION_MAGIC, 1);
  auto changed_motion = motion;
  changed_motion.controllerNumber = 1;
  expect_packet_dispatches(inputs(), motion, changed_motion, SS_CONTROLLER_MOTION_MAGIC, 2);
  changed_motion = motion;
  changed_motion.motionType = 1;
  expect_packet_dispatches(inputs(), motion, changed_motion, SS_CONTROLLER_MOTION_MAGIC, 2);
}

TEST_F(InputGamepadSessionTest, ReusesGamepadsAcrossPauseAndDestroysThemOnTermination) {
  const std::string session_id = "paired-client-certificate";
  auto first_mail = std::make_shared<safe::mail_raw_t>();
  auto first = inputs().alloc(first_mail, session_id);
  ASSERT_NE(first, nullptr);
  const auto active_devices_before_gamepad = runtime().active_device_count();

  const platf::gamepad_arrival_t metadata {LI_CTYPE_XBOX, 0, 0};
  const auto original_id = input::testing::alloc_gamepad(first, 0, metadata);
  ASSERT_GE(original_id, 0);
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad + 1);

  const std::weak_ptr<input::input_t> paused = first;
  first.reset();
  EXPECT_FALSE(paused.expired());
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad + 1);

  auto resumed_mail = std::make_shared<safe::mail_raw_t>();
  auto resumed = inputs().alloc(resumed_mail, session_id);
  ASSERT_NE(resumed, nullptr);
  EXPECT_EQ(paused.lock(), resumed);
  EXPECT_EQ(input::testing::gamepad_id(resumed, 0), original_id);
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad + 1);

  inputs().terminate_gamepads(session_id);
  EXPECT_EQ(input::testing::gamepad_id(resumed, 0), -1);
  EXPECT_EQ(runtime().active_device_count(), active_devices_before_gamepad);

  auto replacement = inputs().alloc(std::make_shared<safe::mail_raw_t>(), session_id);
  EXPECT_NE(replacement, resumed);
}

TEST_F(InputGamepadSessionTest, RefreshesSharedVirtualInputAfterLicenseStateChanges) {
  ASSERT_NE(context().keyboard, nullptr);
  ASSERT_NE(context().mouse, nullptr);
  const auto original_keyboard_id = context().keyboard->device_id();
  const auto original_mouse_id = context().mouse->device_id();
  const auto active_devices = runtime().active_device_count();

  inputs().refresh_virtual_input();

  ASSERT_NE(context().keyboard, nullptr);
  ASSERT_NE(context().mouse, nullptr);
  EXPECT_NE(context().keyboard->device_id(), original_keyboard_id);
  EXPECT_NE(context().mouse->device_id(), original_mouse_id);
  EXPECT_EQ(runtime().active_device_count(), active_devices);
}

TEST_F(InputGamepadSessionTest, IndependentHostServicesDoNotShareRetainedSessionsOrGamepadSlots) {
  runtime::infrastructure_t other_infrastructure;
  input::service_t other_inputs {configuration().values(), other_infrastructure.scheduler(), other_infrastructure.mailbox(), other_infrastructure.cursor()};
  auto other_platform = platf::input(configuration().values(), other_infrastructure.scheduler());
  ASSERT_TRUE(other_platform);
  auto &other_context = platf::virtualhid::get_input_context(other_platform);
  other_context = platf::virtualhid::input_context_t {configuration().values(), lvh::BackendKind::fake};
  input::testing::set_platform_input(other_inputs, std::move(other_platform));

  auto first = inputs().alloc(std::make_shared<safe::mail_raw_t>(), "same-client");
  auto second = other_inputs.alloc(std::make_shared<safe::mail_raw_t>(), "same-client");
  ASSERT_NE(first, second);
  const platf::gamepad_arrival_t metadata {LI_CTYPE_XBOX, 0, 0};
  EXPECT_EQ(input::testing::alloc_gamepad(first, 0, metadata), 0);
  EXPECT_EQ(input::testing::alloc_gamepad(second, 0, metadata), 0);

  inputs().terminate_gamepads("same-client");
  EXPECT_EQ(input::testing::gamepad_id(first, 0), -1);
  EXPECT_EQ(input::testing::gamepad_id(second, 0), 0);
  EXPECT_EQ(other_inputs.alloc(std::make_shared<safe::mail_raw_t>(), "same-client"), second);
}

TEST_F(InputGamepadSessionTest, RetargetsRelativeMouseFromLatestStreamGeometry) {
  auto session_mail = std::make_shared<safe::mail_raw_t>();
  auto stream_input = inputs().alloc(session_mail, "relative-mouse-client");
  ASSERT_NE(stream_input, nullptr);

  input::touch_port_t left_viewport {
    {
      .offset_x = -1920,
      .offset_y = 0,
      .width = 1920,
      .height = 1080,
      .logical_width = 1920,
      .logical_height = 1080,
      .env_offset_x = -1920,
      .env_offset_y = 0,
    },
    3840,
    1080,
    0.0F,
    0.0F,
    1.0F,
    1.0F,
    3840,
    1080,
  };

  const auto initial_mouse_id = context().mouse->device_id();
  session_mail->event<input::touch_port_t>(mail::touch_port)->raise(left_viewport);
  input::testing::send_relative_mouse_packet(stream_input, 12, -7);
  const auto left_mouse_id = context().mouse->device_id();
  EXPECT_NE(left_mouse_id, initial_mouse_id);
  auto mouse_event = context().mouse->last_submitted_event();
  EXPECT_EQ(mouse_event.kind, lvh::MouseEventKind::relative_motion);
  EXPECT_EQ(mouse_event.x, 12);
  EXPECT_EQ(mouse_event.y, -7);
  EXPECT_EQ(context().mouse_desktop.offset_x, -1920);
  EXPECT_EQ(context().mouse_desktop.width, 3840);
  EXPECT_EQ(context().mouse_viewport.offset_x, -1920);
  EXPECT_EQ(context().mouse_viewport.width, 1920);

  left_viewport.offset_x = 0;
  session_mail->event<input::touch_port_t>(mail::touch_port)->raise(left_viewport);
  input::testing::send_relative_mouse_packet(stream_input, -5, 9);
  EXPECT_NE(context().mouse->device_id(), left_mouse_id);
  mouse_event = context().mouse->last_submitted_event();
  EXPECT_EQ(mouse_event.kind, lvh::MouseEventKind::relative_motion);
  EXPECT_EQ(mouse_event.x, -5);
  EXPECT_EQ(mouse_event.y, 9);
  EXPECT_EQ(context().mouse_viewport.offset_x, 0);
}

TEST_F(InputGamepadSessionTest, LicensedBackendChoiceIsSerializedWithoutChangingStartupSettings) {
  const auto path = std::filesystem::temp_directory_path() / "sunshine_test_runtime_gamepad_policy.conf";  // NOSONAR(cpp:S5443): safe for tests
  std::filesystem::remove(path);
  const auto remove_file = util::fail_guard([&path]() {
    std::filesystem::remove(path);
  });
  configuration().edit_startup().sunshine.config_file = path.string();
  scheduler().start(1);
  const auto stop_worker = util::fail_guard([this]() {
    scheduler().stop();
    scheduler().join();
  });

  EXPECT_FALSE(inputs().select_all_gamepad_drivers_if_licensed(configuration(), false));
  EXPECT_TRUE(inputs().select_all_gamepad_drivers_if_licensed(configuration(), true));
  EXPECT_EQ(inputs().configuration_snapshot().gamepad_driver, config::GAMEPAD_DRIVER_ALL);
  EXPECT_EQ(context().gamepad_driver, config::GAMEPAD_DRIVER_ALL);
  EXPECT_TRUE(configuration().values().input.gamepad_driver.empty());
  EXPECT_EQ(file_handler::read_file(path.string().c_str()), "gamepad_driver = all\n");
  EXPECT_FALSE(inputs().select_all_gamepad_drivers_if_licensed(configuration(), true));
  auto snapshot = inputs().configuration_snapshot();
  snapshot.gamepad_driver = config::GAMEPAD_DRIVER_NONE;
  EXPECT_EQ(inputs().configuration_snapshot().gamepad_driver, config::GAMEPAD_DRIVER_ALL);
}

/**
 * @brief An uninitialized backend reports no concrete gamepad choices before and after worker startup.
 */
TEST_F(InputGamepadSessionTest, UninitializedGamepadProbeWorksBeforeAndAfterSchedulerStarts) {
  input::service_t inactive {configuration().values(), scheduler(), mailbox(), cursor()};
  EXPECT_TRUE(inactive.probe_gamepads());

  scheduler().start(1);
  const auto stop_worker = util::fail_guard([this]() {
    scheduler().stop();
    scheduler().join();
  });
  EXPECT_TRUE(inactive.probe_gamepads());
}

TEST_F(InputGamepadSessionTest, LicensedBackendChoiceHonorsSavedPreferencesAndPersistenceFailures) {
  const auto path = std::filesystem::temp_directory_path() / "sunshine_test_existing_gamepad_policy.conf";  // NOSONAR(cpp:S5443): safe for tests
  const auto remove_file = util::fail_guard([&path]() {
    std::filesystem::remove(path);
  });
  configuration().edit_startup().sunshine.config_file = path.string();
  ASSERT_EQ(file_handler::write_file(path.string().c_str(), "gamepad_driver = vigembus\n"), 0);
  EXPECT_FALSE(inputs().select_all_gamepad_drivers_if_licensed(configuration(), true));
  EXPECT_TRUE(inputs().configuration_snapshot().gamepad_driver.empty());
  EXPECT_EQ(file_handler::read_file(path.string().c_str()), "gamepad_driver = vigembus\n");

  configuration().edit_startup().sunshine.config_file.clear();
  EXPECT_FALSE(inputs().select_all_gamepad_drivers_if_licensed(configuration(), true));
  EXPECT_TRUE(inputs().configuration_snapshot().gamepad_driver.empty());
}

TEST_F(InputGamepadSessionTest, LicensedBackendChoiceCanPrecedePlatformInitialization) {
  const auto path = std::filesystem::temp_directory_path() / "sunshine_test_inactive_gamepad_policy.conf";  // NOSONAR(cpp:S5443): safe for tests
  std::filesystem::remove(path);
  const auto remove_file = util::fail_guard([&path]() {
    std::filesystem::remove(path);
  });
  configuration().edit_startup().sunshine.config_file = path.string();
  input::service_t inactive {configuration().values(), scheduler(), mailbox(), cursor()};

  EXPECT_TRUE(inactive.select_all_gamepad_drivers_if_licensed(configuration(), true));
  EXPECT_EQ(inactive.configuration_snapshot().gamepad_driver, config::GAMEPAD_DRIVER_ALL);
  EXPECT_TRUE(inputs().configuration_snapshot().gamepad_driver.empty());
}

TEST_F(InputGamepadSessionTest, ExplicitStartupBackendChoiceRemainsSelectedAfterLicensing) {
  config::store_t explicit_config;
  explicit_config.edit_startup().input.gamepad_driver = config::GAMEPAD_DRIVER_NONE;
  input::service_t explicit_inputs {explicit_config.values(), scheduler(), mailbox(), cursor()};

  EXPECT_FALSE(explicit_inputs.select_all_gamepad_drivers_if_licensed(explicit_config, true));
  EXPECT_EQ(explicit_inputs.configuration_snapshot().gamepad_driver, config::GAMEPAD_DRIVER_NONE);
}
