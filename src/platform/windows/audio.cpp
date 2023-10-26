/**
 * @file src/platform/windows/audio.cpp
 * @brief todo
 */
#define INITGUID
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <roapi.h>

#include <codecvt>

#include <synchapi.h>

#include <newdev.h>

#include <avrt.h>

#include "src/config.h"
#include "src/main.h"
#include "src/platform/common.h"

// Must be the last included file
// clang-format off
#include "PolicyConfig.h"
// clang-format on

DEFINE_PROPERTYKEY(PKEY_Device_DeviceDesc, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 2);  // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);  // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_DeviceInterface_FriendlyName, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 2);

#if defined(__x86_64) || defined(_M_AMD64)
  #define STEAM_DRIVER_SUBDIR L"x64"
#elif defined(__i386) || defined(_M_IX86)
  #define STEAM_DRIVER_SUBDIR L"x86"
#else
  #warning No known Steam audio driver for this architecture
#endif

using namespace std::literals;
namespace platf::audio {
  constexpr auto SAMPLE_RATE = 48000;

  constexpr auto STEAM_AUDIO_DRIVER_PATH = L"%CommonProgramFiles(x86)%\\Steam\\drivers\\Windows10\\" STEAM_DRIVER_SUBDIR L"\\SteamStreamingSpeakers.inf";

  template <class T>
  void
  Release(T *p) {
    p->Release();
  }

  template <class T>
  void
  co_task_free(T *p) {
    CoTaskMemFree((LPVOID) p);
  }

  using device_enum_t = util::safe_ptr<IMMDeviceEnumerator, Release<IMMDeviceEnumerator>>;
  using device_t = util::safe_ptr<IMMDevice, Release<IMMDevice>>;
  using collection_t = util::safe_ptr<IMMDeviceCollection, Release<IMMDeviceCollection>>;
  using audio_client_t = util::safe_ptr<IAudioClient, Release<IAudioClient>>;
  using audio_capture_t = util::safe_ptr<IAudioCaptureClient, Release<IAudioCaptureClient>>;
  using wave_format_t = util::safe_ptr<WAVEFORMATEX, co_task_free<WAVEFORMATEX>>;
  using wstring_t = util::safe_ptr<WCHAR, co_task_free<WCHAR>>;
  using handle_t = util::safe_ptr_v2<void, BOOL, CloseHandle>;
  using policy_t = util::safe_ptr<IPolicyConfig, Release<IPolicyConfig>>;
  using prop_t = util::safe_ptr<IPropertyStore, Release<IPropertyStore>>;

  class co_init_t: public deinit_t {
  public:
    co_init_t() {
      CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY);
    }

    ~co_init_t() override {
      CoUninitialize();
    }
  };

  class prop_var_t {
  public:
    prop_var_t() {
      PropVariantInit(&prop);
    }

    ~prop_var_t() {
      PropVariantClear(&prop);
    }

    PROPVARIANT prop;
  };

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
  struct format_t {
    enum type_e : int {
      none,
      stereo,
      surr51,
      surr71,
    } type;

    std::string_view name;
    int channels;
    int channel_mask;
  } formats[] {
    {
      format_t::stereo,
      "Stereo"sv,
      2,
      SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT,
    },
    {
      format_t::surr51,
      "Surround 5.1"sv,
      6,
      SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY |
        SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT,
    },
    {
      format_t::surr71,
      "Surround 7.1"sv,
      8,
      SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY |
        SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT |
        SPEAKER_SIDE_LEFT |
        SPEAKER_SIDE_RIGHT,
    },
  };

  static format_t surround_51_side_speakers {
    format_t::surr51,
    "Surround 5.1"sv,
    6,
    SPEAKER_FRONT_LEFT |
      SPEAKER_FRONT_RIGHT |
      SPEAKER_FRONT_CENTER |
      SPEAKER_LOW_FREQUENCY |
      SPEAKER_SIDE_LEFT |
      SPEAKER_SIDE_RIGHT,
  };

  WAVEFORMATEXTENSIBLE
  create_wave_format(const format_t &format) {
    WAVEFORMATEXTENSIBLE wave_format;

    wave_format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave_format.Format.nChannels = format.channels;
    wave_format.Format.nSamplesPerSec = SAMPLE_RATE;
    wave_format.Format.wBitsPerSample = 16;
    wave_format.Format.nBlockAlign = wave_format.Format.nChannels * wave_format.Format.wBitsPerSample / 8;
    wave_format.Format.nAvgBytesPerSec = wave_format.Format.nSamplesPerSec * wave_format.Format.nBlockAlign;
    wave_format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

    wave_format.Samples.wValidBitsPerSample = 16;
    wave_format.dwChannelMask = format.channel_mask;
    wave_format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    return wave_format;
  }

  int
  set_wave_format(audio::wave_format_t &wave_format, const format_t &format) {
    wave_format->nSamplesPerSec = SAMPLE_RATE;
    wave_format->wBitsPerSample = 16;

    switch (wave_format->wFormatTag) {
      case WAVE_FORMAT_PCM:
        break;
      case WAVE_FORMAT_IEEE_FLOAT:
        break;
      case WAVE_FORMAT_EXTENSIBLE: {
        auto wave_ex = (PWAVEFORMATEXTENSIBLE) wave_format.get();
        wave_ex->Samples.wValidBitsPerSample = 16;
        wave_ex->dwChannelMask = format.channel_mask;
        wave_ex->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        break;
      }
      default:
        BOOST_LOG(error) << "Unsupported Wave Format: [0x"sv << util::hex(wave_format->wFormatTag).to_string_view() << ']';
        return -1;
    };

    wave_format->nChannels = format.channels;
    wave_format->nBlockAlign = wave_format->nChannels * wave_format->wBitsPerSample / 8;
    wave_format->nAvgBytesPerSec = wave_format->nSamplesPerSec * wave_format->nBlockAlign;

    return 0;
  }

  audio_client_t
  make_audio_client(device_t &device, const format_t &format) {
    audio_client_t audio_client;
    auto status = device->Activate(
      IID_IAudioClient,
      CLSCTX_ALL,
      nullptr,
      (void **) &audio_client);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't activate Device: [0x"sv << util::hex(status).to_string_view() << ']';

      return nullptr;
    }

    WAVEFORMATEXTENSIBLE wave_format = create_wave_format(format);

    status = audio_client->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,  // Enable automatic resampling to 48 KHz
      0, 0,
      (LPWAVEFORMATEX) &wave_format,
      nullptr);

    if (status) {
      BOOST_LOG(debug) << "Couldn't initialize audio client for ["sv << format.name << "]: [0x"sv << util::hex(status).to_string_view() << ']';
      return nullptr;
    }

    return audio_client;
  }

  const wchar_t *
  no_null(const wchar_t *str) {
    return str ? str : L"Unknown";
  }

  bool
  validate_device(device_t &device) {
    bool valid = false;

    // Check for any valid format
    for (const auto &format : formats) {
      auto audio_client = make_audio_client(device, format);

      BOOST_LOG(debug) << format.name << ": "sv << (!audio_client ? "unsupported"sv : "supported"sv);

      if (audio_client) {
        valid = true;
      }
    }

    return valid;
  }

  device_t
  default_device(device_enum_t &device_enum) {
    device_t device;
    HRESULT status;
    status = device_enum->GetDefaultAudioEndpoint(
      eRender,
      eConsole,
      &device);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't get default audio endpoint [0x"sv << util::hex(status).to_string_view() << ']';

      return nullptr;
    }

    return device;
  }

  class audio_notification_t: public ::IMMNotificationClient {
  public:
    audio_notification_t() {}

    // IUnknown implementation (unused by IMMDeviceEnumerator)
    ULONG STDMETHODCALLTYPE
    AddRef() {
      return 1;
    }

    ULONG STDMETHODCALLTYPE
    Release() {
      return 1;
    }

    HRESULT STDMETHODCALLTYPE
    QueryInterface(REFIID riid, VOID **ppvInterface) {
      if (IID_IUnknown == riid) {
        AddRef();
        *ppvInterface = (IUnknown *) this;
        return S_OK;
      }
      else if (__uuidof(IMMNotificationClient) == riid) {
        AddRef();
        *ppvInterface = (IMMNotificationClient *) this;
        return S_OK;
      }
      else {
        *ppvInterface = NULL;
        return E_NOINTERFACE;
      }
    }

    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE
    OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) {
      if (flow == eRender) {
        default_render_device_changed_flag.store(true);
      }
      return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    OnDeviceAdded(LPCWSTR pwstrDeviceId) { return S_OK; }

    HRESULT STDMETHODCALLTYPE
    OnDeviceRemoved(LPCWSTR pwstrDeviceId) { return S_OK; }

    HRESULT STDMETHODCALLTYPE
    OnDeviceStateChanged(
      LPCWSTR pwstrDeviceId,
      DWORD dwNewState) { return S_OK; }

    HRESULT STDMETHODCALLTYPE
    OnPropertyValueChanged(
      LPCWSTR pwstrDeviceId,
      const PROPERTYKEY key) { return S_OK; }

    /**
     * @brief Checks if the default rendering device changed and resets the change flag
     *
     * @return true if the device changed since last call
     */
    bool
    check_default_render_device_changed() {
      return default_render_device_changed_flag.exchange(false);
    }

  private:
    std::atomic_bool default_render_device_changed_flag;
  };

  class mic_wasapi_t: public mic_t {
  public:
    capture_e
    sample(std::vector<std::int16_t> &sample_out) override {
      auto sample_size = sample_out.size();

      // Refill the sample buffer if needed
      while (sample_buf_pos - std::begin(sample_buf) < sample_size) {
        auto capture_result = _fill_buffer();
        if (capture_result != capture_e::ok) {
          return capture_result;
        }
      }

      // Fill the output buffer with samples
      std::copy_n(std::begin(sample_buf), sample_size, std::begin(sample_out));

      // Move any excess samples to the front of the buffer
      std::move(&sample_buf[sample_size], sample_buf_pos, std::begin(sample_buf));
      sample_buf_pos -= sample_size;

      return capture_e::ok;
    }

    int
    init(std::uint32_t sample_rate, std::uint32_t frame_size, std::uint32_t channels_out) {
      audio_event.reset(CreateEventA(nullptr, FALSE, FALSE, nullptr));
      if (!audio_event) {
        BOOST_LOG(error) << "Couldn't create Event handle"sv;

        return -1;
      }

      HRESULT status;

      status = CoCreateInstance(
        CLSID_MMDeviceEnumerator,
        nullptr,
        CLSCTX_ALL,
        IID_IMMDeviceEnumerator,
        (void **) &device_enum);

      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't create Device Enumerator [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      status = device_enum->RegisterEndpointNotificationCallback(&endpt_notification);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't register endpoint notification [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      auto device = default_device(device_enum);
      if (!device) {
        return -1;
      }

      for (auto &format : formats) {
        if (format.channels != channels_out) {
          BOOST_LOG(debug) << "Skipping audio format ["sv << format.name << "] with channel count ["sv << format.channels << " != "sv << channels_out << ']';
          continue;
        }

        BOOST_LOG(debug) << "Trying audio format ["sv << format.name << ']';
        audio_client = make_audio_client(device, format);

        if (audio_client) {
          BOOST_LOG(debug) << "Found audio format ["sv << format.name << ']';
          channels = channels_out;
          break;
        }
      }

      if (!audio_client) {
        BOOST_LOG(error) << "Couldn't find supported format for audio"sv;
        return -1;
      }

      REFERENCE_TIME default_latency;
      audio_client->GetDevicePeriod(&default_latency, nullptr);
      default_latency_ms = default_latency / 1000;

      std::uint32_t frames;
      status = audio_client->GetBufferSize(&frames);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't acquire the number of audio frames [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      // *2 --> needs to fit double
      sample_buf = util::buffer_t<std::int16_t> { std::max(frames, frame_size) * 2 * channels_out };
      sample_buf_pos = std::begin(sample_buf);

      status = audio_client->GetService(IID_IAudioCaptureClient, (void **) &audio_capture);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't initialize audio capture client [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      status = audio_client->SetEventHandle(audio_event.get());
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't set event handle [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      {
        DWORD task_index = 0;
        mmcss_task_handle = AvSetMmThreadCharacteristics("Pro Audio", &task_index);
        if (!mmcss_task_handle) {
          BOOST_LOG(error) << "Couldn't associate audio capture thread with Pro Audio MMCSS task [0x" << util::hex(GetLastError()).to_string_view() << ']';
        }
      }

      status = audio_client->Start();
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't start recording [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      return 0;
    }

    ~mic_wasapi_t() override {
      if (device_enum) {
        device_enum->UnregisterEndpointNotificationCallback(&endpt_notification);
      }

      if (audio_client) {
        audio_client->Stop();
      }

      if (mmcss_task_handle) {
        AvRevertMmThreadCharacteristics(mmcss_task_handle);
      }
    }

  private:
    capture_e
    _fill_buffer() {
      HRESULT status;

      // Total number of samples
      struct sample_aligned_t {
        std::uint32_t uninitialized;
        std::int16_t *samples;
      } sample_aligned;

      // number of samples / number of channels
      struct block_aligned_t {
        std::uint32_t audio_sample_size;
      } block_aligned;

      // Check if the default audio device has changed
      if (endpt_notification.check_default_render_device_changed()) {
        // Invoke the audio_control_t's callback if it wants one
        if (default_endpt_changed_cb) {
          (*default_endpt_changed_cb)();
        }

        // Reinitialize to pick up the new default device
        return capture_e::reinit;
      }

      status = WaitForSingleObjectEx(audio_event.get(), default_latency_ms, FALSE);
      switch (status) {
        case WAIT_OBJECT_0:
          break;
        case WAIT_TIMEOUT:
          return capture_e::timeout;
        default:
          BOOST_LOG(error) << "Couldn't wait for audio event: [0x"sv << util::hex(status).to_string_view() << ']';
          return capture_e::error;
      }

      std::uint32_t packet_size {};
      for (
        status = audio_capture->GetNextPacketSize(&packet_size);
        SUCCEEDED(status) && packet_size > 0;
        status = audio_capture->GetNextPacketSize(&packet_size)) {
        DWORD buffer_flags;
        status = audio_capture->GetBuffer(
          (BYTE **) &sample_aligned.samples,
          &block_aligned.audio_sample_size,
          &buffer_flags,
          nullptr, nullptr);

        switch (status) {
          case S_OK:
            break;
          case AUDCLNT_E_DEVICE_INVALIDATED:
            return capture_e::reinit;
          default:
            BOOST_LOG(error) << "Couldn't capture audio [0x"sv << util::hex(status).to_string_view() << ']';
            return capture_e::error;
        }

        if (buffer_flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
          BOOST_LOG(debug) << "Audio capture signaled buffer discontinuity";
        }

        sample_aligned.uninitialized = std::end(sample_buf) - sample_buf_pos;
        auto n = std::min(sample_aligned.uninitialized, block_aligned.audio_sample_size * channels);

        if (n < block_aligned.audio_sample_size * channels) {
          BOOST_LOG(warning) << "Audio capture buffer overflow";
        }

        if (buffer_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
          std::fill_n(sample_buf_pos, n, 0);
        }
        else {
          std::copy_n(sample_aligned.samples, n, sample_buf_pos);
        }

        sample_buf_pos += n;

        audio_capture->ReleaseBuffer(block_aligned.audio_sample_size);
      }

      if (status == AUDCLNT_E_DEVICE_INVALIDATED) {
        return capture_e::reinit;
      }

      if (FAILED(status)) {
        return capture_e::error;
      }

      return capture_e::ok;
    }

  public:
    handle_t audio_event;

    device_enum_t device_enum;
    device_t device;
    audio_client_t audio_client;
    audio_capture_t audio_capture;

    audio_notification_t endpt_notification;
    std::optional<std::function<void()>> default_endpt_changed_cb;

    REFERENCE_TIME default_latency_ms;

    util::buffer_t<std::int16_t> sample_buf;
    std::int16_t *sample_buf_pos;
    int channels;

    HANDLE mmcss_task_handle = NULL;
  };

  class audio_control_t: public ::platf::audio_control_t {
  public:
    std::optional<sink_t>
    sink_info() override {
      auto virtual_adapter_name = L"Steam Streaming Speakers"sv;

      sink_t sink;

      auto device = default_device(device_enum);
      if (!device) {
        return std::nullopt;
      }

      audio::wstring_t wstring;
      device->GetId(&wstring);

      sink.host = converter.to_bytes(wstring.get());

      collection_t collection;
      auto status = device_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't enumerate: [0x"sv << util::hex(status).to_string_view() << ']';

        return std::nullopt;
      }

      UINT count;
      collection->GetCount(&count);

      // If the sink isn't a device name, we'll assume it's a device ID
      auto virtual_device_id = find_device_id_by_name(config::audio.virtual_sink).value_or(converter.from_bytes(config::audio.virtual_sink));
      auto virtual_device_found = false;

      for (auto x = 0; x < count; ++x) {
        audio::device_t device;
        collection->Item(x, &device);

        if (!validate_device(device)) {
          continue;
        }

        audio::wstring_t wstring;
        device->GetId(&wstring);
        std::wstring device_id { wstring.get() };

        audio::prop_t prop;
        device->OpenPropertyStore(STGM_READ, &prop);

        prop_var_t adapter_friendly_name;
        prop_var_t device_friendly_name;
        prop_var_t device_desc;

        prop->GetValue(PKEY_Device_FriendlyName, &device_friendly_name.prop);
        prop->GetValue(PKEY_DeviceInterface_FriendlyName, &adapter_friendly_name.prop);
        prop->GetValue(PKEY_Device_DeviceDesc, &device_desc.prop);

        auto adapter_name = no_null((LPWSTR) adapter_friendly_name.prop.pszVal);
        BOOST_LOG(verbose)
          << L"===== Device ====="sv << std::endl
          << L"Device ID          : "sv << wstring.get() << std::endl
          << L"Device name        : "sv << no_null((LPWSTR) device_friendly_name.prop.pszVal) << std::endl
          << L"Adapter name       : "sv << adapter_name << std::endl
          << L"Device description : "sv << no_null((LPWSTR) device_desc.prop.pszVal) << std::endl
          << std::endl;

        if (virtual_device_id.empty() && adapter_name == virtual_adapter_name) {
          virtual_device_id = std::move(device_id);
          virtual_device_found = true;
          break;
        }
        else if (virtual_device_id == device_id) {
          virtual_device_found = true;
          break;
        }
      }

      if (virtual_device_found) {
        auto name_suffix = converter.to_bytes(virtual_device_id);
        sink.null = std::make_optional(sink_t::null_t {
          "virtual-"s.append(formats[format_t::stereo - 1].name) + name_suffix,
          "virtual-"s.append(formats[format_t::surr51 - 1].name) + name_suffix,
          "virtual-"s.append(formats[format_t::surr71 - 1].name) + name_suffix,
        });
      }
      else if (!virtual_device_id.empty()) {
        BOOST_LOG(warning) << "Unable to find the specified virtual sink: "sv << virtual_device_id;
      }

      return sink;
    }

    /**
     * @brief Gets information encoded in the raw sink name
     *
     * @param sink The raw sink name
     *
     * @return A pair of type and the real sink name
     */
    std::pair<format_t::type_e, std::string_view>
    get_sink_info(const std::string &sink) {
      std::string_view sv { sink.c_str(), sink.size() };

      // sink format:
      // [virtual-(format name)]device_id
      auto prefix = "virtual-"sv;
      if (sv.find(prefix) == 0) {
        sv = sv.substr(prefix.size(), sv.size() - prefix.size());

        for (auto &format : formats) {
          auto &name = format.name;
          if (sv.find(name) == 0) {
            return std::make_pair(format.type, sv.substr(name.size(), sv.size() - name.size()));
          }
        }
      }

      return std::make_pair(format_t::none, sv);
    }

    std::unique_ptr<mic_t>
    microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) override {
      auto mic = std::make_unique<mic_wasapi_t>();

      if (mic->init(sample_rate, frame_size, channels)) {
        return nullptr;
      }

      // If this is a virtual sink, set a callback that will change the sink back if it's changed
      auto sink_info = get_sink_info(assigned_sink);
      if (sink_info.first != format_t::none) {
        mic->default_endpt_changed_cb = [this] {
          BOOST_LOG(info) << "Resetting sink to ["sv << assigned_sink << "] after default changed";
          set_sink(assigned_sink);
        };
      }

      return mic;
    }

    /**
     * If the requested sink is a virtual sink, meaning no speakers attached to
     * the host, then we can seamlessly set the format to stereo and surround sound.
     *
     * Any virtual sink detected will be prefixed by:
     *    virtual-(format name)
     * If it doesn't contain that prefix, then the format will not be changed
     */
    std::optional<std::wstring>
    set_format(const std::string &sink) {
      auto sink_info = get_sink_info(sink);

      // If the sink isn't a device name, we'll assume it's a device ID
      auto wstring_device_id = find_device_id_by_name(sink).value_or(converter.from_bytes(sink_info.second.data()));

      if (sink_info.first == format_t::none) {
        // wstring_device_id does not contain virtual-(format name)
        // It's a simple deviceId, just pass it back
        return std::make_optional(std::move(wstring_device_id));
      }

      wave_format_t wave_format;
      auto status = policy->GetMixFormat(wstring_device_id.c_str(), &wave_format);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't acquire Wave Format [0x"sv << util::hex(status).to_string_view() << ']';

        return std::nullopt;
      }

      set_wave_format(wave_format, formats[(int) sink_info.first - 1]);

      WAVEFORMATEXTENSIBLE p {};
      status = policy->SetDeviceFormat(wstring_device_id.c_str(), wave_format.get(), (WAVEFORMATEX *) &p);

      // Surround 5.1 might contain side-{left, right} instead of speaker in the back
      // Try again with different speaker mask.
      if (status == 0x88890008 && sink_info.first == format_t::surr51) {
        set_wave_format(wave_format, surround_51_side_speakers);
        status = policy->SetDeviceFormat(wstring_device_id.c_str(), wave_format.get(), (WAVEFORMATEX *) &p);
      }
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't set Wave Format [0x"sv << util::hex(status).to_string_view() << ']';

        return std::nullopt;
      }

      return std::make_optional(std::move(wstring_device_id));
    }

    int
    set_sink(const std::string &sink) override {
      auto wstring_device_id = set_format(sink);
      if (!wstring_device_id) {
        return -1;
      }

      int failure {};
      for (int x = 0; x < (int) ERole_enum_count; ++x) {
        auto status = policy->SetDefaultEndpoint(wstring_device_id->c_str(), (ERole) x);
        if (status) {
          // Depending on the format of the string, we could get either of these errors
          if (status == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || status == E_INVALIDARG) {
            BOOST_LOG(warning) << "Audio sink not found: "sv << sink;
          }
          else {
            BOOST_LOG(warning) << "Couldn't set ["sv << sink << "] to role ["sv << x << "]: 0x"sv << util::hex(status).to_string_view();
          }

          ++failure;
        }
      }

      // Remember the assigned sink name, so we have it for later if we need to set it
      // back after another application changes it
      if (!failure) {
        assigned_sink = sink;
      }

      return failure;
    }

    /**
     * @brief Find the audio device ID given a user-specified name.
     * @param name The name provided by the user.
     * @return The matching device ID, or nothing if not found.
     */
    std::optional<std::wstring>
    find_device_id_by_name(const std::string &name) {
      if (name.empty()) {
        return std::nullopt;
      }

      collection_t collection;
      auto status = device_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't enumerate: [0x"sv << util::hex(status).to_string_view() << ']';

        return std::nullopt;
      }

      UINT count;
      collection->GetCount(&count);

      auto wstring_name = converter.from_bytes(name.data());

      for (auto x = 0; x < count; ++x) {
        audio::device_t device;
        collection->Item(x, &device);

        if (!validate_device(device)) {
          continue;
        }

        audio::wstring_t wstring_id;
        device->GetId(&wstring_id);

        audio::prop_t prop;
        device->OpenPropertyStore(STGM_READ, &prop);

        prop_var_t adapter_friendly_name;
        prop_var_t device_friendly_name;
        prop_var_t device_desc;

        prop->GetValue(PKEY_Device_FriendlyName, &device_friendly_name.prop);
        prop->GetValue(PKEY_DeviceInterface_FriendlyName, &adapter_friendly_name.prop);
        prop->GetValue(PKEY_Device_DeviceDesc, &device_desc.prop);

        auto adapter_name = no_null((LPWSTR) adapter_friendly_name.prop.pszVal);
        auto device_name = no_null((LPWSTR) device_friendly_name.prop.pszVal);
        auto device_description = no_null((LPWSTR) device_desc.prop.pszVal);

        // Match the user-specified name against any of the user-visible strings
        if (std::wcscmp(wstring_name.c_str(), adapter_name) == 0 ||
            std::wcscmp(wstring_name.c_str(), device_name) == 0 ||
            std::wcscmp(wstring_name.c_str(), device_description) == 0) {
          return std::make_optional(std::wstring { wstring_id.get() });
        }
      }

      return std::nullopt;
    }

    /**
     * @brief Resets the default audio device from Steam Streaming Speakers.
     */
    void
    reset_default_device() {
      auto steam_device_id = find_device_id_by_name("Steam Streaming Speakers"s);
      if (!steam_device_id) {
        return;
      }

      {
        // Get the current default audio device (if present)
        auto current_default_dev = default_device(device_enum);
        if (!current_default_dev) {
          return;
        }

        audio::wstring_t current_default_id;
        current_default_dev->GetId(&current_default_id);

        // If Steam Streaming Speakers are already not default, we're done.
        if (*steam_device_id != current_default_id.get()) {
          return;
        }
      }

      // Disable the Steam Streaming Speakers temporarily to allow the OS to pick a new default.
      auto hr = policy->SetEndpointVisibility(steam_device_id->c_str(), FALSE);
      if (FAILED(hr)) {
        BOOST_LOG(warning) << "Failed to disable Steam audio device: "sv << util::hex(hr).to_string_view();
        return;
      }

      // Get the newly selected default audio device
      auto new_default_dev = default_device(device_enum);

      // Enable the Steam Streaming Speakers again
      hr = policy->SetEndpointVisibility(steam_device_id->c_str(), TRUE);
      if (FAILED(hr)) {
        BOOST_LOG(warning) << "Failed to enable Steam audio device: "sv << util::hex(hr).to_string_view();
        return;
      }

      // If there's now no audio device, the Steam Streaming Speakers were the only device available.
      // There's no other device to set as the default, so just return.
      if (!new_default_dev) {
        return;
      }

      audio::wstring_t new_default_id;
      new_default_dev->GetId(&new_default_id);

      // Set the new default audio device
      for (int x = 0; x < (int) ERole_enum_count; ++x) {
        policy->SetDefaultEndpoint(new_default_id.get(), (ERole) x);
      }

      BOOST_LOG(info) << "Successfully reset default audio device"sv;
    }

    /**
     * @brief Installs the Steam Streaming Speakers driver, if present.
     * @return `true` if installation was successful.
     */
    bool
    install_steam_audio_drivers() {
#ifdef STEAM_DRIVER_SUBDIR
      // MinGW's libnewdev.a is missing DiInstallDriverW() even though the headers have it,
      // so we have to load it at runtime. It's Vista or later, so it will always be available.
      auto newdev = LoadLibraryExW(L"newdev.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!newdev) {
        BOOST_LOG(error) << "newdev.dll failed to load"sv;
        return false;
      }
      auto fg = util::fail_guard([newdev]() {
        FreeLibrary(newdev);
      });

      auto fn_DiInstallDriverW = (decltype(DiInstallDriverW) *) GetProcAddress(newdev, "DiInstallDriverW");
      if (!fn_DiInstallDriverW) {
        BOOST_LOG(error) << "DiInstallDriverW() is missing"sv;
        return false;
      }

      // Get the current default audio device (if present)
      auto old_default_dev = default_device(device_enum);

      // Install the Steam Streaming Speakers driver
      WCHAR driver_path[MAX_PATH] = {};
      ExpandEnvironmentStringsW(STEAM_AUDIO_DRIVER_PATH, driver_path, ARRAYSIZE(driver_path));
      if (fn_DiInstallDriverW(nullptr, driver_path, 0, nullptr)) {
        BOOST_LOG(info) << "Successfully installed Steam Streaming Speakers"sv;

        // Wait for 5 seconds to allow the audio subsystem to reconfigure things before
        // modifying the default audio device or enumerating devices again.
        Sleep(5000);

        // If there was a previous default device, restore that original device as the
        // default output device just in case installing the new one changed it.
        if (old_default_dev) {
          audio::wstring_t old_default_id;
          old_default_dev->GetId(&old_default_id);

          for (int x = 0; x < (int) ERole_enum_count; ++x) {
            policy->SetDefaultEndpoint(old_default_id.get(), (ERole) x);
          }
        }

        return true;
      }
      else {
        auto err = GetLastError();
        switch (err) {
          case ERROR_ACCESS_DENIED:
            BOOST_LOG(warning) << "Administrator privileges are required to install Steam Streaming Speakers"sv;
            break;
          case ERROR_FILE_NOT_FOUND:
          case ERROR_PATH_NOT_FOUND:
            BOOST_LOG(info) << "Steam audio drivers not found. This is expected if you don't have Steam installed."sv;
            break;
          default:
            BOOST_LOG(warning) << "Failed to install Steam audio drivers: "sv << err;
            break;
        }

        return false;
      }
#else
      BOOST_LOG(warning) << "Unable to install Steam Streaming Speakers on unknown architecture"sv;
      return false;
#endif
    }

    int
    init() {
      auto status = CoCreateInstance(
        CLSID_CPolicyConfigClient,
        nullptr,
        CLSCTX_ALL,
        IID_IPolicyConfig,
        (void **) &policy);

      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't create audio policy config: [0x"sv << util::hex(status).to_string_view() << ']';

        return -1;
      }

      status = CoCreateInstance(
        CLSID_MMDeviceEnumerator,
        nullptr,
        CLSCTX_ALL,
        IID_IMMDeviceEnumerator,
        (void **) &device_enum);

      if (FAILED(status)) {
        BOOST_LOG(error) << "Couldn't create Device Enumerator: [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }

      return 0;
    }

    ~audio_control_t() override {}

    policy_t policy;
    audio::device_enum_t device_enum;
    std::string assigned_sink;
  };
}  // namespace platf::audio

namespace platf {

  // It's not big enough to justify it's own source file :/
  namespace dxgi {
    int
    init();
  }

  std::unique_ptr<audio_control_t>
  audio_control() {
    auto control = std::make_unique<audio::audio_control_t>();

    if (control->init()) {
      return nullptr;
    }

    // Install Steam Streaming Speakers if needed. We do this during audio_control() to ensure
    // the sink information returned includes the new Steam Streaming Speakers device.
    if (config::audio.install_steam_drivers && !control->find_device_id_by_name("Steam Streaming Speakers"s)) {
      // This is best effort. Don't fail if it doesn't work.
      control->install_steam_audio_drivers();
    }

    return control;
  }

  std::unique_ptr<deinit_t>
  init() {
    if (dxgi::init()) {
      return nullptr;
    }

    // Initialize COM
    auto co_init = std::make_unique<platf::audio::co_init_t>();

    // If Steam Streaming Speakers are currently the default audio device,
    // change the default to something else (if another device is available).
    audio::audio_control_t audio_ctrl;
    if (audio_ctrl.init() == 0) {
      audio_ctrl.reset_default_device();
    }

    return co_init;
  }
}  // namespace platf
