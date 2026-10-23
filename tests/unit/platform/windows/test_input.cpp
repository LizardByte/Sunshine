/**
 * @file tests/unit/platform/windows/test_input.cpp
 * @brief Tests for ViGEm rumble scaling and DualShock 4 report bytes.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <array>
  #include <cstddef>
  #include <cstdint>
  #include <map>
  #include <tuple>

  // local includes
  #include <src/platform/windows/input.h>

TEST(WindowsRumbleTest, PreservesTheEstablishedRangeForEveryAmplitude) {
  for (unsigned strength = 0; strength <= 255; ++strength) {
    EXPECT_EQ(platf::vigem::scale_rumble(static_cast<std::uint8_t>(strength)), strength * 256);
  }
}

/**
 * @brief Driver-free DualShock 4 touch report and pointer state.
 */
class WindowsTouchTest: public testing::Test {
private:
  DS4_REPORT_EX report_ {};  ///< Current device report.
  std::map<std::uint32_t, std::uint8_t> pointers_;  ///< Allocated client pointer IDs.
  std::byte available_ {0x3};  ///< Both touch slots start available.

protected:
  /**
   * @brief Read the current DualShock 4 touch report.
   *
   * @return Const reference to the touch report.
   */
  const DS4_TOUCH &report() const {
    return report_.Report.sCurrentTouch;
  }

  /**
   * @brief Read the allocated client pointer IDs.
   *
   * @return Const reference to the pointer-to-slot mapping.
   */
  const std::map<std::uint32_t, std::uint8_t> &pointers() const {
    return pointers_;
  }

  /**
   * @brief Read the mask of free touch slots.
   *
   * @return Byte mask of available slots.
   */
  std::byte available() const {
    return available_;
  }

  /**
   * @brief Set report counters before testing their byte wraparound.
   */
  void prepare_counter_wraparound() {
    report_.Report.sCurrentTouch.bPacketCounter = 0xFF;
    report_.Report.sCurrentTouch.bIsUpTrackingNum1 = 0xFF;
  }

  /**
   * @brief Mark both report slots up before each test.
   */
  void SetUp() override {
    report_.Report.sCurrentTouch.bIsUpTrackingNum1 = 0x80;
    report_.Report.sCurrentTouch.bIsUpTrackingNum2 = 0x80;
  }

  /**
   * @brief Apply a client touch event without submitting it to a driver.
   *
   * @param type Client event type.
   * @param id Client pointer ID.
   * @param x Normalized horizontal coordinate.
   * @param y Normalized vertical coordinate.
   * @return Whether the resulting report should be submitted.
   */
  bool touch(std::uint8_t type, std::uint32_t id, float x = 0.0f, float y = 0.0f) {
    return platf::vigem::update_touch(report_, pointers_, available_, {{0, 0}, type, id, x, y, 1.0f});
  }
};

TEST_F(WindowsTouchTest, AllocatesBothSlotsAndPacksCoordinatesInWireOrder) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10, 1.0f, 1.0f));
  EXPECT_EQ(pointers().at(10), 0);
  EXPECT_EQ(available(), std::byte {0x2});
  EXPECT_EQ(report().bIsUpTrackingNum1, 1);
  const std::array<std::uint8_t, 3> first {0x80, 0xF7, 0x3A};
  EXPECT_TRUE(std::equal(first.begin(), first.end(), report().bTouchData1));

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 20, 0.5f, 0.5f));
  EXPECT_EQ(pointers().at(20), 1);
  EXPECT_EQ(available(), std::byte {0});
  EXPECT_EQ(report().bIsUpTrackingNum2, 1);
  const std::array<std::uint8_t, 3> second {0xC0, 0x73, 0x1D};
  EXPECT_TRUE(std::equal(second.begin(), second.end(), report().bTouchData2));
  EXPECT_EQ(report().bPacketCounter, 2);

  EXPECT_FALSE(touch(LI_TOUCH_EVENT_DOWN, 30));
  EXPECT_EQ(pointers().size(), 2U);
  EXPECT_EQ(report().bPacketCounter, 2);
}

TEST_F(WindowsTouchTest, MovesReleasesCancelsAndReusesTheCorrectSlot) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 20));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_MOVE, 20, 1.0f, 1.0f));
  EXPECT_EQ(report().bTouchData2[0], 0x80);
  EXPECT_EQ(report().bTouchData1[0], 0);
  EXPECT_EQ(available(), std::byte {0});

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_UP, 10));
  EXPECT_EQ(report().bIsUpTrackingNum1, 0x81);
  EXPECT_EQ(available(), std::byte {0x1});
  EXPECT_FALSE(pointers().contains(10));

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_CANCEL, 20));
  EXPECT_EQ(report().bIsUpTrackingNum2, 0x81);
  EXPECT_EQ(available(), std::byte {0x3});
  EXPECT_TRUE(pointers().empty());

  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 30));
  EXPECT_EQ(pointers().at(30), 0);
  EXPECT_EQ(report().bIsUpTrackingNum1, 2);
}

TEST_F(WindowsTouchTest, CancelAllPreservesCoordinatesAndReleasesBothSlots) {
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10, 1.0f, 1.0f));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 20, 0.5f, 0.5f));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_CANCEL_ALL, 0));
  EXPECT_EQ(report().bIsUpTrackingNum1, 0x81);
  EXPECT_EQ(report().bIsUpTrackingNum2, 0x81);
  EXPECT_EQ(report().bTouchData1[0], 0x80);
  EXPECT_EQ(report().bTouchData2[0], 0xC0);
  EXPECT_EQ(report().bPacketCounter, 3);
  EXPECT_EQ(available(), std::byte {0x3});
  EXPECT_TRUE(pointers().empty());
}

TEST_F(WindowsTouchTest, RejectsUnknownPointersAndUnsupportedEventsWithoutUpdatingTheReport) {
  EXPECT_FALSE(touch(LI_TOUCH_EVENT_MOVE, 10));
  EXPECT_FALSE(touch(LI_TOUCH_EVENT_UP, 10));
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10));
  EXPECT_FALSE(touch(0xFF, 10));
  EXPECT_EQ(report().bPacketCounter, 1);
  EXPECT_EQ(report().bIsUpTrackingNum1, 1);
  EXPECT_EQ(pointers().at(10), 0);
}

TEST_F(WindowsTouchTest, PreservesByteCounterWraparound) {
  prepare_counter_wraparound();
  ASSERT_TRUE(touch(LI_TOUCH_EVENT_DOWN, 10));
  EXPECT_EQ(report().bPacketCounter, 0);
  EXPECT_EQ(report().bIsUpTrackingNum1, 0x80);
}

/**
 * @brief Initial status, client state, percentage, and expected status and level.
 */
using BatteryParam = std::tuple<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t>;

/**
 * @brief Battery report cases including special status and unknown percentage.
 */
struct WindowsBatteryTest: testing::TestWithParam<BatteryParam> {};

TEST_P(WindowsBatteryTest, PreservesTheExpectedStatusAndBatteryLevelBytes) {
  const auto &[initial, state, percentage, expected_status, expected_level] = GetParam();
  DS4_REPORT_EX report {};
  report.Report.bBatteryLvlSpecial = initial;
  report.Report.bBatteryLvl = 42;
  platf::vigem::update_battery(report, {{0, 0}, state, percentage});
  EXPECT_EQ(report.Report.bBatteryLvlSpecial, expected_status);
  EXPECT_EQ(report.Report.bBatteryLvl, expected_level);
}

INSTANTIATE_TEST_SUITE_P(
  BatteryStates,
  WindowsBatteryTest,
  testing::Values(
    BatteryParam {0x1F, LI_BATTERY_STATE_CHARGING, LI_BATTERY_PERCENTAGE_UNKNOWN, 0x15, 42},
    BatteryParam {0x1F, LI_BATTERY_STATE_DISCHARGING, LI_BATTERY_PERCENTAGE_UNKNOWN, 0x05, 42},
    BatteryParam {0x1F, LI_BATTERY_STATE_CHARGING, 75, 0x18, 191},
    BatteryParam {0x1F, LI_BATTERY_STATE_DISCHARGING, 75, 0x05, 191},
    BatteryParam {0x13, LI_BATTERY_STATE_CHARGING, 50, 0x15, 127},
    BatteryParam {0x13, LI_BATTERY_STATE_DISCHARGING, LI_BATTERY_PERCENTAGE_UNKNOWN, 0x03, 42},
    BatteryParam {0x13, LI_BATTERY_STATE_FULL, LI_BATTERY_PERCENTAGE_UNKNOWN, 0x1B, 255},
    BatteryParam {0x13, LI_BATTERY_STATE_NOT_PRESENT, LI_BATTERY_PERCENTAGE_UNKNOWN, 0x1F, 42},
    BatteryParam {0x13, LI_BATTERY_STATE_NOT_CHARGING, 50, 0x1F, 127},
    BatteryParam {0x13, LI_BATTERY_STATE_UNKNOWN, LI_BATTERY_PERCENTAGE_UNKNOWN, 0x13, 42},
    BatteryParam {0x03, LI_BATTERY_STATE_UNKNOWN, 50, 0x03, 127},
    BatteryParam {0x1B, LI_BATTERY_STATE_UNKNOWN, 75, 0x1B, 191},
    BatteryParam {0x13, LI_BATTERY_STATE_UNKNOWN, 0, 0x10, 0},
    BatteryParam {0x13, LI_BATTERY_STATE_UNKNOWN, 4, 0x10, 10},
    BatteryParam {0x13, LI_BATTERY_STATE_UNKNOWN, 5, 0x11, 12},
    BatteryParam {0x13, LI_BATTERY_STATE_UNKNOWN, 95, 0x1A, 242},
    BatteryParam {0x13, LI_BATTERY_STATE_UNKNOWN, 100, 0x1A, 255}
  )
);
#endif
