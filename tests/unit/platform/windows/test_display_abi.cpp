/**
 * @file tests/unit/platform/windows/test_display_abi.cpp
 * @brief Verify the Windows kernel ABI of Sunshine's D3DKMT declarations.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <bit>
  #include <cstddef>
  #include <type_traits>

  // local includes
  #include <src/platform/windows/display.h>

/**
 * @brief Adapter requests retain the sizes and field offsets expected by D3DKMT.
 */
TEST(D3dkmtAbiTests, AdapterRequestsMatchKernelLayouts) {
  using display = platf::dxgi::display_base_t;
  using open_request = display::D3DKMT_OPENADAPTERFROMLUID;
  using query_request = display::D3DKMT_QUERYADAPTERINFO;
  using close_request = display::D3DKMT_CLOSEADAPTER;

  EXPECT_TRUE(std::is_standard_layout_v<open_request>);
  EXPECT_EQ(sizeof(open_request), 12U);
  EXPECT_EQ(alignof(open_request), 4U);
  EXPECT_EQ(offsetof(open_request, AdapterLuid), 0U);
  EXPECT_EQ(offsetof(open_request, hAdapter), 8U);

  EXPECT_TRUE(std::is_standard_layout_v<query_request>);
  EXPECT_EQ(sizeof(query_request), sizeof(void *) == 8 ? 24U : 16U);
  EXPECT_EQ(alignof(query_request), alignof(void *));
  EXPECT_EQ(offsetof(query_request, hAdapter), 0U);
  EXPECT_EQ(offsetof(query_request, Type), 4U);
  EXPECT_EQ(offsetof(query_request, pPrivateDriverData), 8U);
  EXPECT_EQ(offsetof(query_request, PrivateDriverDataSize), 8U + sizeof(void *));

  EXPECT_TRUE(std::is_standard_layout_v<close_request>);
  EXPECT_EQ(sizeof(close_request), 4U);
  EXPECT_EQ(alignof(close_request), 4U);
  EXPECT_EQ(offsetof(close_request, hAdapter), 0U);
}

/**
 * @brief WDDM 2.7 capability fields map to their documented kernel flag bits.
 *
 * Inspect the object representation without reading an inactive union member.
 */
TEST(D3dkmtAbiTests, WddmCapabilitiesUseKernelBitPositions) {
  using capabilities = platf::dxgi::display_base_t::D3DKMT_WDDM_2_7_CAPS;
  EXPECT_TRUE(std::is_standard_layout_v<capabilities>);
  EXPECT_EQ(sizeof(capabilities), 4U);
  EXPECT_EQ(alignof(capabilities), 4U);

  capabilities caps {};
  caps.HwSchSupported = 1;
  EXPECT_EQ(std::bit_cast<UINT>(caps), 0x01U);

  caps = {};
  caps.HwSchEnabled = 1;
  EXPECT_EQ(std::bit_cast<UINT>(caps), 0x02U);

  caps = {};
  caps.HwSchEnabledByDefault = 1;
  EXPECT_EQ(std::bit_cast<UINT>(caps), 0x04U);

  caps = {};
  caps.IndependentVidPnVSyncControl = 1;
  EXPECT_EQ(std::bit_cast<UINT>(caps), 0x08U);
}

/**
 * @brief Scheduling priority values retain the representation expected by D3DKMT.
 */
TEST(D3dkmtAbiTests, SchedulingPrioritiesMatchKernelValues) {
  using display = platf::dxgi::display_base_t;
  EXPECT_EQ(sizeof(display::D3DKMT_SCHEDULINGPRIORITYCLASS), 4U);
  EXPECT_EQ(display::D3DKMT_SCHEDULINGPRIORITYCLASS_IDLE, 0);
  EXPECT_EQ(display::D3DKMT_SCHEDULINGPRIORITYCLASS_BELOW_NORMAL, 1);
  EXPECT_EQ(display::D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL, 2);
  EXPECT_EQ(display::D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL, 3);
  EXPECT_EQ(display::D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH, 4);
  EXPECT_EQ(display::D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME, 5);
}
#endif
