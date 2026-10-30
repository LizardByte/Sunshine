/**
 * @file tests/unit/platform/linux/test_cuda.cpp
 * @brief Tests for Linux CUDA capture selection.
 */

#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <array>
  #include <cstdint>
  #include <cstdlib>

  // local includes
  #include "src/platform/linux/cuda.h"
  #include "src/utility.h"

  #ifdef SUNSHINE_BUILD_CUDA
    // lib includes
    #include <ffnvcodec/dynlink_loader.h>
  #endif

namespace {
  /**
   * @brief Expected virtual desktop selection for an NvFBC status.
   */
  struct nvfbc_virtual_desktop_case_t {
    std::uint32_t output_count;  ///< Physical outputs reported by NvFBC.
    bool capture_possible;  ///< Whether NvFBC reports capture support.
    std::uint32_t screen_width;  ///< Virtual desktop width.
    std::uint32_t screen_height;  ///< Virtual desktop height.
    bool expected;  ///< Whether Sunshine should offer the virtual desktop.
  };
}  // namespace

TEST(NvFbcDisplaySelectionTest, OffersVirtualDesktopOnlyForValidNoOutputCapture) {
  constexpr std::array cases {
    nvfbc_virtual_desktop_case_t {1U, true, 1920U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 1920U, 1080U, true},
    nvfbc_virtual_desktop_case_t {0U, false, 1920U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 0U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 1920U, 0U, false},
  };

  for (const auto &[output_count, capture_possible, screen_width, screen_height, expected] : cases) {
    EXPECT_EQ(cuda::nvfbc::should_offer_virtual_desktop(output_count, capture_possible, screen_width, screen_height), expected)
      << "output_count=" << output_count
      << ", capture_possible=" << capture_possible
      << ", screen_width=" << screen_width
      << ", screen_height=" << screen_height;
  }
}

  #ifdef SUNSHINE_BUILD_CUDA
namespace {
  /**
   * @brief How many times a fake loader below has been asked to fill a table.
   */
  int table_loads = 0;

  /**
   * @brief Stand-in for `cuInit` that always succeeds.
   *
   * @return `CUDA_SUCCESS`.
   */
  CUresult fake_cu_init(unsigned int) {
    return CUDA_SUCCESS;
  }

  /**
   * @brief Fill a table the way `cuda_load_functions` does, without needing the CUDA driver.
   *
   * Like the real loader, it frees the table it is given and starts a new, empty one.
   *
   * @param table Table to fill.
   * @return 0.
   */
  int fake_load_table(CudaFunctions **table) {
    ++table_loads;
    cuda_free_functions(table);
    *table = static_cast<CudaFunctions *>(std::calloc(1, sizeof(CudaFunctions)));
    (*table)->cuInit = fake_cu_init;
    return 0;
  }

  /**
   * @brief Stand-in for a loader on a machine where the CUDA driver cannot be loaded.
   *
   * @return -1.
   */
  int failing_load_table(CudaFunctions **) {
    ++table_loads;
    return -1;
  }
}  // namespace

TEST(CudaInitTest, LoadsTheFunctionTableOnlyOnce) {
  CudaFunctions *table = nullptr;
  auto free_table = util::fail_guard([&table] {
    cuda_free_functions(&table);
  });
  table_loads = 0;

  ASSERT_EQ(cuda::init(&table, fake_load_table), 0);
  // Every new stream calls cuda::init() again, while the streams already running keep calling
  // through the table. Loading it again would empty it under them.
  ASSERT_EQ(cuda::init(&table, fake_load_table), 0);

  EXPECT_EQ(table_loads, 1);
}

TEST(CudaInitTest, RetriesLoadingAfterAFailure) {
  CudaFunctions *table = nullptr;
  auto free_table = util::fail_guard([&table] {
    cuda_free_functions(&table);
  });
  table_loads = 0;

  EXPECT_NE(cuda::init(&table, failing_load_table), 0);
  EXPECT_EQ(cuda::init(&table, fake_load_table), 0);

  EXPECT_EQ(table_loads, 2);
}
  #endif  // SUNSHINE_BUILD_CUDA

#endif  // __linux__
