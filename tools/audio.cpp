/**
 * @file tools/audio.cpp
 * @brief Handles collecting audio device information from Windows.
 */
#define INITGUID
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <roapi.h>

#include <codecvt>
#include <locale>

#include <synchapi.h>

#include <iostream>

#include "src/utility.h"

DEFINE_PROPERTYKEY(PKEY_Device_DeviceDesc, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 2);  // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);  // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_DeviceInterface_FriendlyName, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 2);

using namespace std::literals;

int device_state_filter = DEVICE_STATE_ACTIVE;

namespace audio {
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
  using collection_t = util::safe_ptr<IMMDeviceCollection, Release<IMMDeviceCollection>>;
  using prop_t = util::safe_ptr<IPropertyStore, Release<IPropertyStore>>;
  using device_t = util::safe_ptr<IMMDevice, Release<IMMDevice>>;
  using audio_client_t = util::safe_ptr<IAudioClient, Release<IAudioClient>>;
  using audio_capture_t = util::safe_ptr<IAudioCaptureClient, Release<IAudioCaptureClient>>;
  using wave_format_t = util::safe_ptr<WAVEFORMATEX, co_task_free<WAVEFORMATEX>>;

  using wstring_t = util::safe_ptr<WCHAR, co_task_free<WCHAR>>;

  using handle_t = util::safe_ptr_v2<void, BOOL, CloseHandle>;

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

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

  const wchar_t *
  no_null(const wchar_t *str) {
    return str ? str : L"Unknown";
  }

  struct format_t {
    std::string_view name;
    int channels;
    int channel_mask;
  } formats[] {
    { "Mono"sv,
      1,
      SPEAKER_FRONT_CENTER },
    { "Stereo"sv,
      2,
      SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT },
    { "Quadraphonic"sv,
      4,
      SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT },
    { "Surround 5.1 (Side)"sv,
      6,
      SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY |
        SPEAKER_SIDE_LEFT |
        SPEAKER_SIDE_RIGHT },
    { "Surround 5.1 (Back)"sv,
      6,
      SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY |
        SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT },
    { "Surround 7.1"sv,
      8,
      SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_FRONT_CENTER |
        SPEAKER_LOW_FREQUENCY |
        SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT |
        SPEAKER_SIDE_LEFT |
        SPEAKER_SIDE_RIGHT }
  };

  void
  set_wave_format(audio::wave_format_t &wave_format, const format_t &format) {
    wave_format->nChannels = format.channels;
    wave_format->nBlockAlign = wave_format->nChannels * wave_format->wBitsPerSample / 8;
    wave_format->nAvgBytesPerSec = wave_format->nSamplesPerSec * wave_format->nBlockAlign;

    if (wave_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
      ((PWAVEFORMATEXTENSIBLE) wave_format.get())->dwChannelMask = format.channel_mask;
    }
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
      std::cout << "Couldn't activate Device: [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

      return nullptr;
    }

    wave_format_t wave_format;
    status = audio_client->GetMixFormat(&wave_format);

    if (FAILED(status)) {
      std::cout << "Couldn't acquire Wave Format [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

      return nullptr;
    }

    set_wave_format(wave_format, format);

    status = audio_client->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      0, 0,
      wave_format.get(),
      nullptr);

    if (status) {
      return nullptr;
    }

    return audio_client;
  }

  void
  print_device(device_t &device) {
    audio::wstring_t wstring;
    DWORD device_state;

    device->GetState(&device_state);
    device->GetId(&wstring);

    audio::prop_t prop;
    device->OpenPropertyStore(STGM_READ, &prop);

    prop_var_t adapter_friendly_name;
    prop_var_t device_friendly_name;
    prop_var_t device_desc;

    prop->GetValue(PKEY_Device_FriendlyName, &device_friendly_name.prop);
    prop->GetValue(PKEY_DeviceInterface_FriendlyName, &adapter_friendly_name.prop);
    prop->GetValue(PKEY_Device_DeviceDesc, &device_desc.prop);

    if (!(device_state & device_state_filter)) {
      return;
    }

    std::wstring device_state_string = L"Unknown"s;
    switch (device_state) {
      case DEVICE_STATE_ACTIVE:
        device_state_string = L"Active"s;
        break;
      case DEVICE_STATE_DISABLED:
        device_state_string = L"Disabled"s;
        break;
      case DEVICE_STATE_UNPLUGGED:
        device_state_string = L"Unplugged"s;
        break;
      case DEVICE_STATE_NOTPRESENT:
        device_state_string = L"Not present"s;
        break;
    }

    std::wstring current_format = L"Unknown"s;
    for (const auto &format : formats) {
      // This will fail for any format that's not the mix format for this device,
      // so we can take the first match as the current format to display.
      auto audio_client = make_audio_client(device, format);
      if (audio_client) {
        current_format = converter.from_bytes(format.name.data());
        break;
      }
    }

    std::wcout
      << L"===== Device ====="sv << std::endl
      << L"Device ID          : "sv << wstring.get() << std::endl
      << L"Device name        : "sv << no_null((LPWSTR) device_friendly_name.prop.pszVal) << std::endl
      << L"Adapter name       : "sv << no_null((LPWSTR) adapter_friendly_name.prop.pszVal) << std::endl
      << L"Device description : "sv << no_null((LPWSTR) device_desc.prop.pszVal) << std::endl
      << L"Device state       : "sv << device_state_string << std::endl
      << L"Current format     : "sv << current_format << std::endl
      << std::endl;
  }
}  // namespace audio

void
print_help() {
  std::cout
    << "==== Help ===="sv << std::endl
    << "Usage:"sv << std::endl
    << "    audio-info [Active|Disabled|Unplugged|Not-Present]" << std::endl;
}

int
main(int argc, char *argv[]) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY);

  auto fg = util::fail_guard([]() {
    CoUninitialize();
  });

  if (argc > 1) {
    device_state_filter = 0;
  }

  for (auto x = 1; x < argc; ++x) {
    for (auto p = argv[x]; *p != '\0'; ++p) {
      if (*p == ' ') {
        *p = '-';

        continue;
      }

      *p = std::tolower(*p);
    }

    if (argv[x] == "active"sv) {
      device_state_filter |= DEVICE_STATE_ACTIVE;
    }
    else if (argv[x] == "disabled"sv) {
      device_state_filter |= DEVICE_STATE_DISABLED;
    }
    else if (argv[x] == "unplugged"sv) {
      device_state_filter |= DEVICE_STATE_UNPLUGGED;
    }
    else if (argv[x] == "not-present"sv) {
      device_state_filter |= DEVICE_STATE_NOTPRESENT;
    }
    else {
      print_help();
      return 2;
    }
  }

  HRESULT status;

  audio::device_enum_t device_enum;
  status = CoCreateInstance(
    CLSID_MMDeviceEnumerator,
    nullptr,
    CLSCTX_ALL,
    IID_IMMDeviceEnumerator,
    (void **) &device_enum);

  if (FAILED(status)) {
    std::cout << "Couldn't create Device Enumerator: [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

    return -1;
  }

  audio::collection_t collection;
  status = device_enum->EnumAudioEndpoints(eRender, device_state_filter, &collection);

  if (FAILED(status)) {
    std::cout << "Couldn't enumerate: [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

    return -1;
  }

  UINT count;
  collection->GetCount(&count);

  std::cout << "====== Found "sv << count << " audio devices ======"sv << std::endl;
  for (auto x = 0; x < count; ++x) {
    audio::device_t device;
    collection->Item(x, &device);

    audio::print_device(device);
  }

  return 0;
}