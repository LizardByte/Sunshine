/**
 * @file tests/unit/test_vaapi_compat.cpp
 * @brief Verify the system libva compatibility functions used by prebuilt FFmpeg.
 */

// test includes
#include "../tests_common.h"

#if defined(__linux__) || defined(__FreeBSD__)

  // standard includes
  #include <cstdint>

extern "C" {
  #include <va/va.h>
}

  #if !VA_CHECK_VERSION(1, 21, 0)
/**
 * @brief Compatibility entry point supplied for prebuilt FFmpeg.
 * @param display VA display.
 * @param buffer VA buffer ID.
 * @param output Output mapped buffer pointer.
 * @param flags Mapping flags ignored by the legacy API.
 * @return VA status code from the legacy mapping API.
 */
extern "C" VAStatus vaMapBuffer2(VADisplay display, VABufferID buffer, void **output, uint32_t flags);

TEST(VaapiCompatibilityTest, InvalidDisplayReturnsLegacyMappingError) {
  void *output = nullptr;
  EXPECT_EQ(vaMapBuffer2(nullptr, VA_INVALID_ID, &output, 0), VA_STATUS_ERROR_INVALID_DISPLAY);
  EXPECT_EQ(vaMapBuffer2(nullptr, VA_INVALID_ID, &output, UINT32_MAX), VA_STATUS_ERROR_INVALID_DISPLAY);
}

namespace {
  VADisplay mapped_display;  ///< Display passed to the legacy mapping API.
  VABufferID mapped_buffer;  ///< Buffer passed to the legacy mapping API.
  void **mapped_output;  ///< Output pointer passed to the legacy mapping API.
  VAStatus mapping_status;  ///< Status returned by the legacy mapping API.
}  // namespace

/**
 * @brief Record the legacy mapping arguments and return the configured status.
 * @param display VA display.
 * @param buffer VA buffer ID.
 * @param output Output mapped buffer pointer.
 * @return Configured VA status code.
 */
extern "C" VAStatus sunshine_test_vaMapBuffer(VADisplay display, VABufferID buffer, void **output) {
  mapped_display = display;
  mapped_buffer = buffer;
  mapped_output = output;
  return mapping_status;
}

    // Compile the production shims with isolated symbols so the fake mapping API
    // cannot intercept VA-API calls elsewhere in test_sunshine.
    #define vaMapBuffer sunshine_test_vaMapBuffer
    #define vaMapBuffer2 sunshine_test_vaMapBuffer2
    #define vaSyncBuffer sunshine_test_vaSyncBuffer
    #include "src/platform/linux/vaapi_compat.cpp"
    #undef vaSyncBuffer
    #undef vaMapBuffer2
    #undef vaMapBuffer

TEST(VaapiCompatibilityTest, MapBufferPreservesArgumentsAndStatus) {
  int display_storage = 0;
  VADisplay display = &display_storage;
  void *output = nullptr;
  for (const auto status : {VA_STATUS_SUCCESS, VA_STATUS_ERROR_INVALID_BUFFER}) {
    mapping_status = status;
    EXPECT_EQ(sunshine_test_vaMapBuffer2(display, 42, &output, UINT32_MAX), status);
    EXPECT_EQ(mapped_display, display);
    EXPECT_EQ(mapped_buffer, 42);
    EXPECT_EQ(mapped_output, &output);
  }
}

    #if !VA_CHECK_VERSION(1, 9, 0)
TEST(VaapiCompatibilityTest, SyncBufferReportsUnsupportedOperation) {
  EXPECT_EQ(sunshine_test_vaSyncBuffer(nullptr, VA_INVALID_ID, 0), VA_STATUS_ERROR_UNIMPLEMENTED);
}
    #endif
  #endif
#endif
