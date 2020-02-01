//
// Created by loki on 1/24/20.
//

#include <roapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <synchapi.h>

#define INITGUID
#include <propkeydef.h>
#undef INITGUID

#include <iostream>

#include "sunshine/utility.h"

DEFINE_PROPERTYKEY(PKEY_Device_DeviceDesc,            0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 2);     // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName,          0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);    // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_DeviceInterface_FriendlyName, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 2);

using namespace std::literals;
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator    = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient           = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient    = __uuidof(IAudioCaptureClient);

int device_state_filter = DEVICE_STATE_ACTIVE;
namespace audio {
template<class T>
void Release(T *p) {
  p->Release();
}

template<class T>
void co_task_free(T *p) {
  CoTaskMemFree((LPVOID)p);
}

using device_enum_t   = util::safe_ptr<IMMDeviceEnumerator, Release<IMMDeviceEnumerator>>;
using collection_t    = util::safe_ptr<IMMDeviceCollection, Release<IMMDeviceCollection>>;
using prop_t          = util::safe_ptr<IPropertyStore, Release<IPropertyStore>>;
using device_t        = util::safe_ptr<IMMDevice, Release<IMMDevice>>;
using audio_client_t  = util::safe_ptr<IAudioClient, Release<IAudioClient>>;
using audio_capture_t = util::safe_ptr<IAudioCaptureClient, Release<IAudioCaptureClient>>;
using wave_format_t   = util::safe_ptr<WAVEFORMATEX, co_task_free<WAVEFORMATEX>>;

using wstring_t = util::safe_ptr<WCHAR, co_task_free<WCHAR>>;

using handle_t = util::safe_ptr_v2<void, BOOL, CloseHandle>;

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

const wchar_t *no_null(const wchar_t *str) {
  return str ? str : L"Unknown";
}
void print_device(device_t &device) {
  HRESULT status;

  audio::wstring_t::pointer wstring_p {};
  DWORD device_state;

  device->GetState(&device_state);
  device->GetId(&wstring_p);
  audio::wstring_t wstring { wstring_p };

  audio::prop_t::pointer prop_p {};
  device->OpenPropertyStore(STGM_READ, &prop_p);
  audio::prop_t prop { prop_p };

  prop_var_t adapter_friendly_name;
  prop_var_t device_friendly_name;
  prop_var_t device_desc;

  prop->GetValue(PKEY_Device_FriendlyName, &device_friendly_name.prop);
  prop->GetValue(PKEY_DeviceInterface_FriendlyName, &adapter_friendly_name.prop);
  prop->GetValue(PKEY_Device_DeviceDesc, &device_desc.prop);

  if(!(device_state & device_state_filter)) {
    return;
  }

  std::wstring device_state_string = L"Unknown"s;
  switch(device_state) {
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

  std::wcout
    << L"===== Device ====="sv << std::endl
    << L"Device ID          : "sv << wstring.get() << std::endl
    << L"Device name        : "sv << no_null((LPWSTR)device_friendly_name.prop.pszVal) << std::endl
    << L"Adapter name       : "sv << no_null((LPWSTR)adapter_friendly_name.prop.pszVal) << std::endl
    << L"Device description : "sv << no_null((LPWSTR)device_desc.prop.pszVal) << std::endl
    << L"Device state       : "sv << device_state_string << std::endl << std::endl;

  if(device_state != DEVICE_STATE_ACTIVE) {
    return;
  }

  // Ensure WaveFromat is compatible
  audio_client_t::pointer audio_client_p{};
  status = device->Activate(
    IID_IAudioClient,
    CLSCTX_ALL,
    nullptr,
    (void **) &audio_client_p);
  audio_client_t audio_client { audio_client_p };

  if (FAILED(status)) {
    std::cout << "Couldn't activate Device: [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

    return;
  }

  wave_format_t::pointer wave_format_p{};
  status = audio_client->GetMixFormat(&wave_format_p);
  wave_format_t wave_format { wave_format_p };

  if (FAILED(status)) {
    std::cout << "Couldn't acquire Wave Format [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

    return;
  }

  switch(wave_format->wFormatTag) {
    case WAVE_FORMAT_PCM:
      break;
    case WAVE_FORMAT_IEEE_FLOAT:
      break;
    case WAVE_FORMAT_EXTENSIBLE: {
      auto wave_ex = (PWAVEFORMATEXTENSIBLE) wave_format.get();
      if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, wave_ex->SubFormat)) {
        break;
      }

      std::cout << "Unsupported Sub Format for WAVE_FORMAT_EXTENSIBLE: [0x"sv << util::hex(wave_ex->SubFormat).to_string_view() << ']' << std::endl;
    }
    default:
      std::cout << "Unsupported Wave Format: [0x"sv << util::hex(wave_format->wFormatTag).to_string_view() << ']' << std::endl;
  };
}
}

void print_help() {
  std::cout
    << "==== Help ===="sv << std::endl
    << "Usage:"sv << std::endl
    << "    audio-info [Active|Disabled|Unplugged|Not-Present]" << std::endl;
}

int main(int argc, char *argv[]) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY);

  auto fg = util::fail_guard([]() {
    CoUninitialize();
  });

  if(argc > 1) {
    device_state_filter = 0;
  }

  for(auto x = 1; x < argc; ++x) {
    for(auto p = argv[x]; *p != '\0'; ++p) {
      if(*p == ' ') {
        *p = '-';

        continue;
      }

      *p = std::tolower(*p);
    }

    if(argv[x] == "active"sv) {
      device_state_filter |= DEVICE_STATE_ACTIVE;
    }
    else if(argv[x] == "disabled"sv) {
      device_state_filter |= DEVICE_STATE_DISABLED;
    }
    else if(argv[x] == "unplugged"sv) {
      device_state_filter |= DEVICE_STATE_UNPLUGGED;
    }
    else if(argv[x] == "not-present"sv) {
      device_state_filter |= DEVICE_STATE_NOTPRESENT;
    }
    else {
      print_help();
      return 2;
    }
  }

  HRESULT status;

  audio::device_enum_t::pointer device_enum_p{};
  status = CoCreateInstance(
    CLSID_MMDeviceEnumerator,
    nullptr,
    CLSCTX_ALL,
    IID_IMMDeviceEnumerator,
    (void **) &device_enum_p);
  audio::device_enum_t device_enum { device_enum_p };

  if (FAILED(status)) {
    std::cout << "Couldn't create Device Enumerator: [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

    return -1;
  }

  audio::collection_t::pointer collection_p {};
  status = device_enum->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, &collection_p);
  audio::collection_t collection { collection_p };

  if (FAILED(status)) {
    std::cout << "Couldn't enumerate: [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;

    return -1;
  }

  UINT count;
  collection->GetCount(&count);

  std::cout << "====== Found "sv << count << " potential audio devices ======"sv << std::endl;
  for(auto x = 0; x < count; ++x) {
    audio::device_t::pointer device_p {};
    collection->Item(x, &device_p);
    audio::device_t device { device_p };

    audio::print_device(device);
  }

  return 0;
}