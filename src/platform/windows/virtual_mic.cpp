/**
 * @file src/platform/windows/virtual_mic.cpp
 * @brief Windows WASAPI virtual microphone output implementation.
 *
 * Locates a virtual audio cable render device (VB-Cable "CABLE Input" or a
 * user-configured device name) and streams 16-bit PCM audio to it using the
 * WASAPI shared-mode render API.  Host applications that monitor the
 * corresponding capture device ("CABLE Output") will hear the client mic.
 */

#define INITGUID

// standard includes
#include <cstring>
#include <string>

// platform includes
#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <propsys.h>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "virtual_mic.h"

// PKEY_Device_FriendlyName — same GUID definition used in audio.cpp
DEFINE_PROPERTYKEY(PKEY_VirtualMic_Device_FriendlyName,
  0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);

namespace platf::virtual_mic {
  using namespace std::literals;

  // COM interface helpers — cast stored void* back to the right type
  static inline IMMDevice *as_device(void *p) { return static_cast<IMMDevice *>(p); }
  static inline IAudioClient *as_audio_client(void *p) { return static_cast<IAudioClient *>(p); }
  static inline IAudioRenderClient *as_render_client(void *p) { return static_cast<IAudioRenderClient *>(p); }

  virtual_mic_output_t::~virtual_mic_output_t() {
    active_ = false;

    if (render_client_) {
      as_render_client(render_client_)->Release();
      render_client_ = nullptr;
    }
    if (audio_client_) {
      as_audio_client(audio_client_)->Release();
      audio_client_ = nullptr;
    }
    if (device_) {
      as_device(device_)->Release();
      device_ = nullptr;
    }
  }

  void *virtual_mic_output_t::find_device(const std::string &name) {
    HRESULT hr;

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "CoCreateInstance(MMDeviceEnumerator) failed: 0x"sv << std::hex << hr;
      return nullptr;
    }

    // Virtual cable INPUT is a render device from WASAPI's perspective
    IMMDeviceCollection *collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr)) {
      BOOST_LOG(error) << "EnumAudioEndpoints failed: 0x"sv << std::hex << hr;
      return nullptr;
    }

    UINT count = 0;
    collection->GetCount(&count);

    IMMDevice *found = nullptr;

    for (UINT i = 0; i < count && found == nullptr; ++i) {
      IMMDevice *dev = nullptr;
      if (FAILED(collection->Item(i, &dev))) continue;

      IPropertyStore *props = nullptr;
      if (FAILED(dev->OpenPropertyStore(STGM_READ, &props))) {
        dev->Release();
        continue;
      }

      PROPVARIANT pv;
      PropVariantInit(&pv);
      if (SUCCEEDED(props->GetValue(PKEY_VirtualMic_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) {
        // Convert wide string to UTF-8
        char narrow[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, pv.pwszVal, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
        std::string dev_name(narrow);

        bool match;
        if (name.empty()) {
          // Auto-detect: look for VB-Cable input side
          match = dev_name.find("CABLE Input") != std::string::npos ||
                  dev_name.find("VB-Audio") != std::string::npos;
        } else {
          match = dev_name.find(name) != std::string::npos;
        }

        if (match) {
          BOOST_LOG(info) << "Found virtual mic device: "sv << dev_name;
          found = dev;
          dev = nullptr;  // transfer ownership
        }
      }

      PropVariantClear(&pv);
      props->Release();
      if (dev) dev->Release();
    }

    collection->Release();
    return found;
  }

  int virtual_mic_output_t::init(const std::string &device_name, int channels, int sample_rate) {
    channels_ = channels;
    sample_rate_ = sample_rate;

    // Ensure COM is available on this thread (idempotent if already initialised)
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    device_ = find_device(device_name);
    if (!device_) {
      if (device_name.empty()) {
        BOOST_LOG(error) << "No virtual audio cable found. "
                            "Install VB-Cable from https://vb-audio.com/Cable/"sv;
      } else {
        BOOST_LOG(error) << "Virtual mic device not found: "sv << device_name;
      }
      return -1;
    }

    // Activate IAudioClient
    IAudioClient *client = nullptr;
    HRESULT hr = as_device(device_)->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                              nullptr, reinterpret_cast<void **>(&client));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "IAudioClient::Activate failed: 0x"sv << std::hex << hr;
      return -1;
    }
    audio_client_ = client;

    // Describe desired format: 16-bit PCM, native byte order
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = static_cast<WORD>(channels_);
    fmt.nSamplesPerSec = static_cast<DWORD>(sample_rate_);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * (fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;

    // Check if the device supports our format (shared mode)
    WAVEFORMATEX *closest = nullptr;
    hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &fmt, &closest);
    if (FAILED(hr) && hr != AUDCLNT_E_UNSUPPORTED_FORMAT) {
      BOOST_LOG(error) << "IsFormatSupported failed: 0x"sv << std::hex << hr;
      if (closest) CoTaskMemFree(closest);
      return -1;
    }
    // Use closest format if the device suggested one (e.g. float output)
    WAVEFORMATEX *use_fmt = (closest != nullptr) ? closest : &fmt;

    // Initialise in shared mode with a 100ms buffer
    constexpr REFERENCE_TIME buf_duration = 1000000;  // 100ms in 100-ns units
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, buf_duration, 0, use_fmt, nullptr);
    if (closest) CoTaskMemFree(closest);

    if (FAILED(hr)) {
      BOOST_LOG(error) << "IAudioClient::Initialize failed: 0x"sv << std::hex << hr;
      return -1;
    }

    hr = client->GetBufferSize(&buffer_frames_);
    if (FAILED(hr)) {
      BOOST_LOG(error) << "IAudioClient::GetBufferSize failed: 0x"sv << std::hex << hr;
      return -1;
    }

    // Get render client
    IAudioRenderClient *render = nullptr;
    hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(&render));
    if (FAILED(hr)) {
      BOOST_LOG(error) << "GetService(IAudioRenderClient) failed: 0x"sv << std::hex << hr;
      return -1;
    }
    render_client_ = render;

    // Start the stream
    hr = client->Start();
    if (FAILED(hr)) {
      BOOST_LOG(error) << "IAudioClient::Start failed: 0x"sv << std::hex << hr;
      return -1;
    }

    active_ = true;
    BOOST_LOG(info) << "Virtual mic WASAPI client started ("sv
                    << channels_ << "ch, "sv << sample_rate_ << "Hz)"sv;
    return 0;
  }

  int virtual_mic_output_t::write_pcm(const opus_int16 *data, int frames) {
    if (!active_ || !render_client_ || !audio_client_) return -1;

    IAudioClient *client = as_audio_client(audio_client_);
    IAudioRenderClient *render = as_render_client(render_client_);

    // How many frames are free in the WASAPI buffer?
    UINT32 padding = 0;
    HRESULT hr = client->GetCurrentPadding(&padding);
    if (FAILED(hr)) {
      BOOST_LOG(warning) << "GetCurrentPadding failed: 0x"sv << std::hex << hr;
      return -1;
    }

    UINT32 available = buffer_frames_ - padding;
    UINT32 write_frames = static_cast<UINT32>(frames);
    if (write_frames > available) {
      // Drop oldest audio rather than block — keeps latency bounded
      write_frames = available;
    }
    if (write_frames == 0) return 0;

    BYTE *buf = nullptr;
    hr = render->GetBuffer(write_frames, &buf);
    if (FAILED(hr)) {
      BOOST_LOG(warning) << "IAudioRenderClient::GetBuffer failed: 0x"sv << std::hex << hr;
      return -1;
    }

    // Copy 16-bit interleaved PCM
    const size_t bytes = write_frames * static_cast<size_t>(channels_) * sizeof(opus_int16);
    std::memcpy(buf, data, bytes);

    render->ReleaseBuffer(write_frames, 0);
    return 0;
  }

}  // namespace platf::virtual_mic
