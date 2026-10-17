/**
 * @file tests/unit/test_nvenc_utils.cpp
 * @brief Tests for NVENC SDK 13.1 format conversion and native handle forwarding.
 */

#ifdef _WIN32
  // test includes
  #include "../tests_common.h"

  // standard includes
  #include <array>
  #include <bit>
  #include <cstdint>
  #include <cstring>
  #include <limits>
  #include <tuple>
  #include <utility>

  // local includes
  #include "src/nvenc/nvenc_base.h"
  #include "src/nvenc/nvenc_d3d11.h"
  #include "src/nvenc/nvenc_utils.h"

namespace {
  using namespace nvenc_1301;

  /**
   * @brief Driver observations used to verify opaque encoder-session cleanup.
   */
  struct driver_state_t {
    NVENCSTATUS open_status = NV_ENC_SUCCESS;  ///< Status returned by the fake session-open callback.
    std::uintptr_t opened_device = 0;  ///< Address of the native device forwarded to NVENC.
    int opens = 0;  ///< Session-open calls observed by the driver.
    int destructions = 0;  ///< Session-destroy calls observed by the driver.
    GUID selected_preset {};  ///< Preset GUID forwarded to the driver.
    int preset_queries = 0;  ///< Preset configuration calls observed by the driver.
  };

  driver_state_t driver;  ///< Fake driver state and opaque session identity.

  /**
   * @brief Return an opaque session and record the underlying native device.
   * @param params NVENC parameters supplied by the base encoder.
   * @param encoder Receives the fake driver's opaque session identity.
   * @return Configured session-open status.
   */
  NVENCSTATUS NVENCAPI open_session(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *params, void **encoder) {  // NOSONAR(cpp:S5008): NVENC's session-open callback requires a void double pointer for its opaque encoder handle.
    driver.opened_device = std::bit_cast<std::uintptr_t>(params->device);
    ++driver.opens;
    *encoder = &driver;
    return driver.open_status;
  }

  /**
   * @brief Stop initialization after the session has been opened.
   * @param encoder Opaque session supplied by NVENC.
   * @param count Receives the number of supported codec GUIDs.
   * @return An error that forces the base encoder to release the session.
   */
  NVENCSTATUS NVENCAPI fail_guid_count(void *encoder, std::uint32_t *count) {  // NOSONAR(cpp:S5008): NVENC's GUID-count callback requires a void pointer for its opaque encoder handle.
    EXPECT_EQ(encoder, &driver);
    *count = 0;
    return NV_ENC_ERR_GENERIC;
  }

  /**
   * @brief Verify and release the opaque session returned by the fake driver.
   * @param encoder Opaque session identity being destroyed.
   * @return Success after recording the destruction.
   */
  NVENCSTATUS NVENCAPI destroy_session(void *encoder) {  // NOSONAR(cpp:S5008): NVENC's session-destroy callback requires a void pointer for its opaque encoder handle.
    EXPECT_EQ(encoder, &driver);
    ++driver.destructions;
    return NV_ENC_SUCCESS;
  }

  /**
   * @brief Base encoder with a fake driver and a typed D3D11 or CUDA device.
   */
  class fake_session_encoder_t: public nvenc_base {
  public:
    /**
     * @brief Install fake driver callbacks for one native device type.
     * @param cuda Whether the test uses a CUDA context instead of a D3D11 device.
     * @param observe_preset Whether the driver should continue through preset selection.
     * @note Unused session parameter types are inferred from the SDK callback signatures.
     */
    explicit fake_session_encoder_t(bool cuda, bool observe_preset = false):
        nvenc_base(cuda ? NV_ENC_DEVICE_TYPE_CUDA : NV_ENC_DEVICE_TYPE_DIRECTX) {
      nvenc = std::make_shared<NV_ENCODE_API_FUNCTION_LIST>();
      nvenc->nvEncOpenEncodeSessionEx = &open_session;
      nvenc->nvEncGetEncodeGUIDCount = &fail_guid_count;
      nvenc->nvEncDestroyEncoder = &destroy_session;
      if (observe_preset) {
        nvenc->nvEncGetEncodeGUIDCount = [](auto *, std::uint32_t *count) {
          *count = 1;
          return NV_ENC_SUCCESS;
        };
        nvenc->nvEncGetEncodeGUIDs = [](auto *, GUID *guids, std::uint32_t, std::uint32_t *count) {
          *guids = NV_ENC_CODEC_H264_GUID;
          *count = 1;
          return NV_ENC_SUCCESS;
        };
        nvenc->nvEncGetEncodeCaps = [](auto *, GUID, NV_ENC_CAPS_PARAM *params, int *value) {
          *value = params->capsToQuery == NV_ENC_CAPS_WIDTH_MAX || params->capsToQuery == NV_ENC_CAPS_HEIGHT_MAX ? 4096 : 0;
          return NV_ENC_SUCCESS;
        };
        nvenc->nvEncGetEncodePresetConfigEx = [](auto *, GUID, GUID preset, NV_ENC_TUNING_INFO, NV_ENC_PRESET_CONFIG *) {
          driver.selected_preset = preset;
          ++driver.preset_queries;
          return NV_ENC_ERR_GENERIC;
        };
      }
      constexpr std::uintptr_t device_address = 0x1234;
      if (cuda) {
        device = std::bit_cast<CUcontext>(device_address);
      } else {
        device = std::bit_cast<ID3D11Device *>(device_address);
      }
    }

  protected:
    bool init_library() override {
      return false;
    }

    bool create_and_register_input_buffer() override {
      ADD_FAILURE() << "Initialization should stop before registering an input buffer";
      return false;
    }
  };

  /**
   * @brief Device type and session-open status exercised by the fake driver.
   */
  struct NvencNativeHandleTest: testing::TestWithParam<std::tuple<bool, NVENCSTATUS>> {};

  /**
   * @brief Native encoder exposing its typed completion event for lifetime checks.
   */
  class completion_event_encoder_t: public nvenc_d3d11 {
  public:
    completion_event_encoder_t():
        nvenc_d3d11(NV_ENC_DEVICE_TYPE_DIRECTX, {}) {
    }

    ID3D11Texture2D *get_input_texture() override {
      return nullptr;
    }

    /**
     * @brief Borrow the event registered for asynchronous encoding.
     * @return Native completion event owned by this encoder.
     */
    completion_event_t *completion_event() const {
      return async_event_handle;
    }

  protected:
    bool create_and_register_input_buffer() override {
      return false;
    }
  };
}  // namespace

/**
 * @brief Preset selection forwards every supported GUID and both boundary fallbacks to the driver.
 */
TEST(NvencPresetTest, PreservesGuidMappingsAndBoundaryFallbacks) {
  using namespace nvenc_1301;
  const std::array cases {
    std::pair {0, NV_ENC_PRESET_P1_GUID},
    std::pair {1, NV_ENC_PRESET_P1_GUID},
    std::pair {2, NV_ENC_PRESET_P2_GUID},
    std::pair {3, NV_ENC_PRESET_P3_GUID},
    std::pair {4, NV_ENC_PRESET_P4_GUID},
    std::pair {5, NV_ENC_PRESET_P5_GUID},
    std::pair {6, NV_ENC_PRESET_P6_GUID},
    std::pair {7, NV_ENC_PRESET_P7_GUID},
    std::pair {8, NV_ENC_PRESET_P7_GUID},
    std::pair {-1, NV_ENC_PRESET_P7_GUID},
    std::pair {std::numeric_limits<int>::max(), NV_ENC_PRESET_P7_GUID},
  };
  for (const auto &[number, expected] : cases) {
    driver = {};
    fake_session_encoder_t encoder {false, true};
    nvenc::nvenc_config config {};
    config.quality_preset = number;
    video::config_t client_config {};
    client_config.width = 64;
    client_config.height = 64;
    const auto colorspace = video::colorspace_from_client_config(client_config, false);
    EXPECT_FALSE(encoder.create_encoder(config, client_config, colorspace, platf::pix_fmt_e::nv12));
    EXPECT_EQ(driver.preset_queries, 1);
    EXPECT_EQ(std::memcmp(&driver.selected_preset, &expected, sizeof(GUID)), 0) << number;
    EXPECT_EQ(driver.destructions, 1);
  }
}

TEST(NvencCompletionEventTest, TypedEventCanBeSignaledAndIsClosedWithTheEncoder) {
  nvenc_1301::completion_event_t *event = nullptr;
  {
    completion_event_encoder_t encoder;
    event = encoder.completion_event();
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);
    ASSERT_TRUE(SetEvent(event));
    EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_OBJECT_0);
  }
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_FAILED);
  EXPECT_EQ(GetLastError(), ERROR_INVALID_HANDLE);
}

TEST_P(NvencNativeHandleTest, PreservesNativeDeviceAndReleasesReturnedSessionOnFailure) {
  const auto &[cuda, status] = GetParam();
  driver = {};
  driver.open_status = status;
  fake_session_encoder_t encoder {cuda};
  video::config_t client_config {};
  client_config.width = 64;
  client_config.height = 64;
  const auto colorspace = video::colorspace_from_client_config(client_config, false);
  EXPECT_FALSE(encoder.create_encoder({}, client_config, colorspace, platf::pix_fmt_e::nv12));
  EXPECT_EQ(driver.opened_device, 0x1234U);
  EXPECT_EQ(driver.opens, 1);
  EXPECT_EQ(driver.destructions, 1);
}

INSTANTIATE_TEST_SUITE_P(
  DeviceTypesAndOpenStatuses,
  NvencNativeHandleTest,
  testing::Combine(testing::Bool(), testing::Values(NV_ENC_SUCCESS, NV_ENC_ERR_GENERIC))
);

TEST(NvencFormatMappingTest, MapsSupportedFormatsAndRejectsUnsupportedOnes) {
  using enum platf::pix_fmt_e;
  using enum nvenc_1301::NV_ENC_BUFFER_FORMAT;
  const std::array cases {
    std::pair {nv12, NV_ENC_BUFFER_FORMAT_NV12},
    std::pair {p010, NV_ENC_BUFFER_FORMAT_YUV420_10BIT},
    std::pair {ayuv, NV_ENC_BUFFER_FORMAT_AYUV},
    std::pair {yuv444p, NV_ENC_BUFFER_FORMAT_YUV444},
    std::pair {yuv444p16, NV_ENC_BUFFER_FORMAT_YUV444_10BIT},
    std::pair {unknown, NV_ENC_BUFFER_FORMAT_UNDEFINED},
    std::pair {y410, NV_ENC_BUFFER_FORMAT_UNDEFINED},
    std::pair {yuv420p, NV_ENC_BUFFER_FORMAT_UNDEFINED},
    std::pair {yuv420p10, NV_ENC_BUFFER_FORMAT_UNDEFINED},
  };
  for (const auto &[format, expected] : cases) {
    EXPECT_EQ(nvenc_1301::nvenc_format_from_sunshine_format(format), expected) << std::to_underlying(format);
  }
}
#endif
