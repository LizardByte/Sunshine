//
// Created by loki on 1/12/20.
//

#include <roapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <codecvt>

#include <synchapi.h>

#include "sunshine/config.h"
#include "sunshine/main.h"
#include "sunshine/platform/common.h"

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator    = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient           = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient    = __uuidof(IAudioCaptureClient);

using namespace std::literals;
namespace platf::audio {
template<class T>
void Release(T *p) {
  p->Release();
}

template<class T>
void co_task_free(T *p) {
  CoTaskMemFree((LPVOID)p);
}

using device_enum_t   = util::safe_ptr<IMMDeviceEnumerator, Release<IMMDeviceEnumerator>>;
using device_t        = util::safe_ptr<IMMDevice, Release<IMMDevice>>;
using audio_client_t  = util::safe_ptr<IAudioClient, Release<IAudioClient>>;
using audio_capture_t = util::safe_ptr<IAudioCaptureClient, Release<IAudioCaptureClient>>;
using wave_format_t   = util::safe_ptr<WAVEFORMATEX, co_task_free<WAVEFORMATEX>>;
using handle_t = util::safe_ptr_v2<void, BOOL, CloseHandle>;

class co_init_t : public deinit_t {
public:
  co_init_t() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_SPEED_OVER_MEMORY);
  }

  ~co_init_t() override {
    CoUninitialize();
  }
};

class mic_wasapi_t : public mic_t {
public:
  capture_e sample(std::vector<std::int16_t> &sample_in) override {
    while(sample_buf_pos - std::begin(sample_buf) < sample_in.size()) {
      //FIXME: Use IAudioClient3 instead of IAudioClient, that would allows for adjusting the latency of the audio samples
      auto capture_result = _fill_buffer();

      if(capture_result != capture_e::ok) {
        return capture_result;
      }
    }

    std::copy_n(std::begin(sample_buf), sample_in.size(), std::begin(sample_in));

    // The excess samples should be in front of the queue
    std::move(&sample_buf[sample_in.size()], sample_buf_pos, std::begin(sample_buf));
    sample_buf_pos -= sample_in.size();

    return capture_e::ok;
  }


  int init(std::uint32_t sample_rate) {
    audio_event.reset(CreateEventA(nullptr, FALSE, FALSE, nullptr));
    if(!audio_event) {
      BOOST_LOG(error) << "Couldn't create Event handle"sv;

      return -1;
    }

    HRESULT status;

    device_enum_t::pointer device_enum_p{};
    status = CoCreateInstance(
      CLSID_MMDeviceEnumerator,
      nullptr,
      CLSCTX_ALL,
      IID_IMMDeviceEnumerator,
      (void **) &device_enum_p);
    device_enum.reset(device_enum_p);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't create Device Enumerator [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    device_t::pointer device_p{};

    if(config::audio.sink.empty()) {
      status = device_enum->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        &device_p);
    }
    else {
      std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
      auto wstring_device_id = converter.from_bytes(config::audio.sink);

      status = device_enum->GetDevice(wstring_device_id.c_str(), &device_p);
    }
    device.reset(device_p);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't create audio Device [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    audio_client_t::pointer audio_client_p{};
    status = device->Activate(
      IID_IAudioClient,
      CLSCTX_ALL,
      nullptr,
      (void **) &audio_client_p);
    audio_client.reset(audio_client_p);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't activate audio Device [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    wave_format_t::pointer wave_format_p{};
    status = audio_client->GetMixFormat(&wave_format_p);
    wave_format.reset(wave_format_p);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't acquire Wave Format [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    wave_format->nChannels = 2;
    wave_format->wBitsPerSample = 16;
    wave_format->nSamplesPerSec = sample_rate;
    wave_format->nBlockAlign = wave_format->nChannels * wave_format->wBitsPerSample / 8;
    wave_format->nAvgBytesPerSec = wave_format->nSamplesPerSec * wave_format->nBlockAlign;

    switch(wave_format->wFormatTag) {
      case WAVE_FORMAT_PCM:
        break;
      case WAVE_FORMAT_IEEE_FLOAT:
        wave_format->wFormatTag = WAVE_FORMAT_PCM;
        break;
      case WAVE_FORMAT_EXTENSIBLE: {
        auto wave_ex = (PWAVEFORMATEXTENSIBLE) wave_format.get();
        if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, wave_ex->SubFormat)) {
          wave_ex->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
          wave_ex->Samples.wValidBitsPerSample = 16;
          break;
        }

        BOOST_LOG(error) << "Unsupported Sub Format for WAVE_FORMAT_EXTENSIBLE: [0x"sv << util::hex(wave_ex->SubFormat).to_string_view() << ']';
        return -1;
      }
      default:
        BOOST_LOG(error) << "Unsupported Wave Format: [0x"sv << util::hex(wave_format->wFormatTag).to_string_view() << ']';
        return -1;
    };

    REFERENCE_TIME default_latency;
    audio_client->GetDevicePeriod(&default_latency, nullptr);
    default_latency_ms = default_latency / 1000;

    status = audio_client->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      0, 0,
      wave_format.get(),
      nullptr);

    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't initialize audio client [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    std::uint32_t frames;
    status = audio_client->GetBufferSize(&frames);
    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't acquire the number of audio frames [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    sample_buf = util::buffer_t<std::int16_t> { frames };
    sample_buf_pos = std::begin(sample_buf);

    audio_capture_t::pointer audio_capture_p {};
    status = audio_client->GetService(IID_IAudioCaptureClient, (void**)&audio_capture_p);
    audio_capture.reset(audio_capture_p);
    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't initialize audio capture client [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    status = audio_client->SetEventHandle(audio_event.get());
    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't set event handle [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    status = audio_client->Start();
    if (FAILED(status)) {
      BOOST_LOG(error) << "Couldn't start recording [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    return 0;
  }

  ~mic_wasapi_t() override {
    if(audio_client) {
      audio_client->Stop();
    }
  }
private:
  capture_e _fill_buffer() {
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

    std::uint32_t packet_size{};
    for (
      status = audio_capture->GetNextPacketSize(&packet_size);
      SUCCEEDED(status) && packet_size > 0;
      status = audio_capture->GetNextPacketSize(&packet_size)
      ) {
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

      sample_aligned.uninitialized = std::end(sample_buf) - sample_buf_pos;
      auto n = std::min(sample_aligned.uninitialized, block_aligned.audio_sample_size * wave_format->nChannels);

      if (buffer_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        std::fill_n(sample_buf_pos, n, 0);
      } else {
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
  wave_format_t wave_format;
  audio_capture_t audio_capture;

  REFERENCE_TIME default_latency_ms;

  util::buffer_t<std::int16_t> sample_buf;
  std::int16_t *sample_buf_pos;
};
}

namespace platf {
std::unique_ptr<mic_t> microphone(std::uint32_t sample_rate) {
  auto mic = std::make_unique<audio::mic_wasapi_t>();

  if(mic->init(sample_rate)) {
    return nullptr;
  }

  return mic;
}

std::unique_ptr<deinit_t> init() {
  return std::make_unique<platf::audio::co_init_t>();
}
}
