/**
 * @file src/platform/linux/vaapi_compat.cpp
 * @brief Compatibility functions for prebuilt FFmpeg with older system libva.
 */
#include <cstdint>

extern "C" {
#include <va/va.h>
#if !VA_CHECK_VERSION(1, 9, 0)
  /**
   * @brief Stub vaSyncBuffer when building against libva before 2.9.0.
   *
   * @param dpy VA display.
   * @param buf_id VA buffer ID.
   * @param timeout_ns Sync timeout in nanoseconds.
   * @return VA status code.
   */
  VAStatus
    vaSyncBuffer(
      VADisplay dpy,
      VABufferID buf_id,
      uint64_t timeout_ns
    ) {
    return VA_STATUS_ERROR_UNIMPLEMENTED;
  }
#endif
#if !VA_CHECK_VERSION(1, 21, 0)
  /**
   * @brief Stub vaMapBuffer2 when building against libva before 2.21.0.
   * @details The older vaMapBuffer API does not accept the mapping flags supplied by the new ABI.
   *
   * @param dpy VA display.
   * @param buf_id VA buffer ID.
   * @param pbuf Output mapped buffer pointer.
   * @return VA status code.
   */
  VAStatus
    vaMapBuffer2(
      VADisplay dpy,
      VABufferID buf_id,
      void **pbuf,
      uint32_t
    ) {
    return vaMapBuffer(dpy, buf_id, pbuf);
  }
#endif
}
