//
// Created by loki on 1/12/20.
//

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <roapi.h>

#include <codecvt>

#include <synchapi.h>

#define INITGUID
#include <propkeydef.h>
#undef INITGUID

#include "src/config.h"
#include "src/main.h"
#include "src/platform/common.h"

// Must be the last included file
// clang-format off
#include "PolicyConfig.h"
// clang-format on

DEFINE_PROPERTYKEY(PKEY_Device_DeviceDesc, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 2);    // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14); // DEVPROP_TYPE_STRING
DEFINE_PROPERTYKEY(PKEY_DeviceInterface_FriendlyName, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 2);

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator    = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient           = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient    = __uuidof(IAudioCaptureClient);

using namespace std::literals;
namespace platf::audio {
constexpr auto SAMPLE_RATE = 48000;

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
using collection_t    = util::safe_ptr<IMMDeviceCollection, Release<IMMDeviceCollection>>;
using audio_client_t  = util::safe_ptr<IAudioClient, Release<IAudioClient>>;
using audio_capture_t = util::safe_ptr<IAudioCaptureClient, Release<IAudioCaptureClient>>;
using wave_format_t   = util::safe_ptr<WAVEFORMATEX, co_task_free<WAVEFORMATEX>>;
using wstring_t       = util::safe_ptr<WCHAR, co_task_free<WCHAR>>;
using handle_t        = util::safe_ptr_v2<void, BOOL, CloseHandle>;
using policy_t        = util::safe_ptr<IPolicyConfig, Release<IPolicyConfig>>;
using prop_t          = util::safe_ptr<IPropertyStore, Release<IPropertyStore>>;

class co_init_t : public deinit_t {
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

class audio_pipe_t {
public:
  static constexpr auto stereo     = 2;
  static constexpr auto channels51 = 6;
  static constexpr auto channels71 = 8;

  using samples_t = std::vector<std::int16_t>;
  using buf_t     = util::buffer_t<std::int16_t>;

  virtual void to_stereo(samples_t &out, const buf_t &in) = 0;
  virtual void to_51(samples_t &out, const buf_t &in)     = 0;
  virtual void to_71(samples_t &out, const buf_t &in)     = 0;
};

class mono_t : public audio_pipe_t {
public:
  void to_stereo(samples_t &out, const buf_t &in) override {
    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end;) {
      *sample_out_p++ = *sample_in_pos * 7 / 10;
      *sample_out_p++ = *sample_in_pos++ * 7 / 10;
    }
  }

  void to_51(samples_t &out, const buf_t &in) override {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += channels51) {
      int left = *sample_in_pos++;

      auto fl = (left * 7 / 10);

      sample_out_p[FRONT_LEFT]    = fl;
      sample_out_p[FRONT_RIGHT]   = fl;
      sample_out_p[FRONT_CENTER]  = fl * 6;
      sample_out_p[LOW_FREQUENCY] = fl / 10;
      sample_out_p[BACK_LEFT]     = left * 4 / 10;
      sample_out_p[BACK_RIGHT]    = left * 4 / 10;
    }
  }

  void to_71(samples_t &out, const buf_t &in) override {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += channels71) {
      int left = *sample_in_pos++;

      auto fl = (left * 7 / 10);

      sample_out_p[FRONT_LEFT]    = fl;
      sample_out_p[FRONT_RIGHT]   = fl;
      sample_out_p[FRONT_CENTER]  = fl * 6;
      sample_out_p[LOW_FREQUENCY] = fl / 10;
      sample_out_p[BACK_LEFT]     = left * 4 / 10;
      sample_out_p[BACK_RIGHT]    = left * 4 / 10;
      sample_out_p[SIDE_LEFT]     = left * 5 / 10;
      sample_out_p[SIDE_RIGHT]    = left * 5 / 10;
    }
  }
};

class stereo_t : public audio_pipe_t {
public:
  void to_stereo(samples_t &out, const buf_t &in) override {
    std::copy_n(std::begin(in), out.size(), std::begin(out));
  }

  void to_51(samples_t &out, const buf_t &in) override {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += channels51) {
      int left  = sample_in_pos[speaker::FRONT_LEFT];
      int right = sample_in_pos[speaker::FRONT_RIGHT];

      sample_in_pos += 2;

      auto fl = (left * 7 / 10);
      auto fr = (right * 7 / 10);

      auto mix = (fl + fr) / 2;

      sample_out_p[FRONT_LEFT]    = fl;
      sample_out_p[FRONT_RIGHT]   = fr;
      sample_out_p[FRONT_CENTER]  = mix;
      sample_out_p[LOW_FREQUENCY] = mix / 2;
      sample_out_p[BACK_LEFT]     = left * 4 / 10;
      sample_out_p[BACK_RIGHT]    = right * 4 / 10;
    }
  }

  void to_71(samples_t &out, const buf_t &in) override {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += channels71) {
      int left  = sample_in_pos[speaker::FRONT_LEFT];
      int right = sample_in_pos[speaker::FRONT_RIGHT];

      sample_in_pos += 2;

      auto fl = (left * 7 / 10);
      auto fr = (right * 7 / 10);

      auto mix = (fl + fr) / 2;

      sample_out_p[FRONT_LEFT]    = fl;
      sample_out_p[FRONT_RIGHT]   = fr;
      sample_out_p[FRONT_CENTER]  = mix;
      sample_out_p[LOW_FREQUENCY] = mix / 2;
      sample_out_p[BACK_LEFT]     = left * 4 / 10;
      sample_out_p[BACK_RIGHT]    = right * 4 / 10;
      sample_out_p[SIDE_LEFT]     = left * 5 / 10;
      sample_out_p[SIDE_RIGHT]    = right * 5 / 10;
    }
  }
};

class surr51_t : public audio_pipe_t {
public:
  void to_stereo(samples_t &out, const buf_t &in) {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += stereo) {
      int left {}, right {};

      left += sample_in_pos[FRONT_LEFT];
      left += sample_in_pos[FRONT_CENTER] * 9 / 10;
      left += sample_in_pos[LOW_FREQUENCY] * 3 / 10;
      left += sample_in_pos[BACK_LEFT] * 7 / 10;
      left += sample_in_pos[BACK_RIGHT] * 3 / 10;

      right += sample_in_pos[FRONT_RIGHT];
      right += sample_in_pos[FRONT_CENTER] * 9 / 10;
      right += sample_in_pos[LOW_FREQUENCY] * 3 / 10;
      right += sample_in_pos[BACK_LEFT] * 3 / 10;
      right += sample_in_pos[BACK_RIGHT] * 7 / 10;

      sample_out_p[0] = left;
      sample_out_p[1] = right;

      sample_in_pos += channels51;
    }
  }

  void to_51(samples_t &out, const buf_t &in) override {
    std::copy_n(std::begin(in), out.size(), std::begin(out));
  }

  void to_71(samples_t &out, const buf_t &in) override {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += channels71) {
      int fl = sample_in_pos[FRONT_LEFT];
      int fr = sample_in_pos[FRONT_RIGHT];
      int bl = sample_in_pos[BACK_LEFT];
      int br = sample_in_pos[BACK_RIGHT];

      auto mix_l = (fl + bl) / 2;
      auto mix_r = (bl + br) / 2;

      sample_out_p[FRONT_LEFT]    = fl;
      sample_out_p[FRONT_RIGHT]   = fr;
      sample_out_p[FRONT_CENTER]  = sample_in_pos[FRONT_CENTER];
      sample_out_p[LOW_FREQUENCY] = sample_in_pos[LOW_FREQUENCY];
      sample_out_p[BACK_LEFT]     = bl;
      sample_out_p[BACK_RIGHT]    = br;
      sample_out_p[SIDE_LEFT]     = mix_l;
      sample_out_p[SIDE_RIGHT]    = mix_r;

      sample_in_pos += channels51;
    }
  }
};

class surr71_t : public audio_pipe_t {
public:
  void to_stereo(samples_t &out, const buf_t &in) {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += stereo) {
      int left {}, right {};

      left += sample_in_pos[FRONT_LEFT];
      left += sample_in_pos[FRONT_CENTER] * 9 / 10;
      left += sample_in_pos[LOW_FREQUENCY] * 3 / 10;
      left += sample_in_pos[BACK_LEFT] * 7 / 10;
      left += sample_in_pos[BACK_RIGHT] * 3 / 10;
      left += sample_in_pos[SIDE_LEFT];

      right += sample_in_pos[FRONT_RIGHT];
      right += sample_in_pos[FRONT_CENTER] * 9 / 10;
      right += sample_in_pos[LOW_FREQUENCY] * 3 / 10;
      right += sample_in_pos[BACK_LEFT] * 3 / 10;
      right += sample_in_pos[BACK_RIGHT] * 7 / 10;
      right += sample_in_pos[SIDE_RIGHT];

      sample_out_p[0] = left;
      sample_out_p[1] = right;

      sample_in_pos += channels71;
    }
  }

  void to_51(samples_t &out, const buf_t &in) override {
    using namespace speaker;

    auto sample_in_pos = std::begin(in);
    auto sample_end    = std::begin(out) + out.size();

    for(auto sample_out_p = std::begin(out); sample_out_p != sample_end; sample_out_p += channels51) {
      auto sl = (int)sample_out_p[SIDE_LEFT] * 3 / 10;
      auto sr = (int)sample_out_p[SIDE_RIGHT] * 3 / 10;

      sample_out_p[FRONT_LEFT]    = sample_in_pos[FRONT_LEFT] + sl;
      sample_out_p[FRONT_RIGHT]   = sample_in_pos[FRONT_RIGHT] + sr;
      sample_out_p[FRONT_CENTER]  = sample_in_pos[FRONT_CENTER];
      sample_out_p[LOW_FREQUENCY] = sample_in_pos[LOW_FREQUENCY];
      sample_out_p[BACK_LEFT]     = sample_in_pos[BACK_LEFT] + sl;
      sample_out_p[BACK_RIGHT]    = sample_in_pos[BACK_RIGHT] + sr;

      sample_in_pos += channels71;
    }
  }

  void to_71(samples_t &out, const buf_t &in) override {
    std::copy_n(std::begin(in), out.size(), std::begin(out));
  }
};

static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
struct format_t {
  enum type_e : int {
    none,
    mono,
    stereo,
    surr51,
    surr71,
  } type;

  std::string_view name;
  int channels;
  int channel_mask;
} formats[] {
  {
    format_t::mono,
    "Mono"sv,
    1,
    SPEAKER_FRONT_CENTER,
  },
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

void set_wave_format(audio::wave_format_t &wave_format, const format_t &format) {
  wave_format->nChannels       = format.channels;
  wave_format->nBlockAlign     = wave_format->nChannels * wave_format->wBitsPerSample / 8;
  wave_format->nAvgBytesPerSec = wave_format->nSamplesPerSec * wave_format->nBlockAlign;

  if(wave_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    ((PWAVEFORMATEXTENSIBLE)wave_format.get())->dwChannelMask = format.channel_mask;
  }
}

int init_wave_format(audio::wave_format_t &wave_format, DWORD sample_rate) {
  wave_format->wBitsPerSample = 16;
  wave_format->nSamplesPerSec = sample_rate;
  switch(wave_format->wFormatTag) {
  case WAVE_FORMAT_PCM:
    break;
  case WAVE_FORMAT_IEEE_FLOAT:
    break;
  case WAVE_FORMAT_EXTENSIBLE: {
    auto wave_ex = (PWAVEFORMATEXTENSIBLE)wave_format.get();
    if(IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, wave_ex->SubFormat)) {
      wave_ex->Samples.wValidBitsPerSample = 16;
      wave_ex->SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;
      break;
    }

    BOOST_LOG(error) << "Unsupported Sub Format for WAVE_FORMAT_EXTENSIBLE: [0x"sv << util::hex(wave_ex->SubFormat).to_string_view() << ']';
  }
  default:
    BOOST_LOG(error) << "Unsupported Wave Format: [0x"sv << util::hex(wave_format->wFormatTag).to_string_view() << ']';
    return -1;
  };

  return 0;
}

audio_client_t make_audio_client(device_t &device, const format_t &format, int sample_rate) {
  audio_client_t audio_client;
  auto status = device->Activate(
    IID_IAudioClient,
    CLSCTX_ALL,
    nullptr,
    (void **)&audio_client);

  if(FAILED(status)) {
    BOOST_LOG(error) << "Couldn't activate Device: [0x"sv << util::hex(status).to_string_view() << ']';

    return nullptr;
  }

  wave_format_t wave_format;
  status = audio_client->GetMixFormat(&wave_format);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Couldn't acquire Wave Format [0x"sv << util::hex(status).to_string_view() << ']';

    return nullptr;
  }

  if(init_wave_format(wave_format, sample_rate)) {
    return nullptr;
  }
  set_wave_format(wave_format, format);

  status = audio_client->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
    0, 0,
    wave_format.get(),
    nullptr);

  if(status) {
    BOOST_LOG(debug) << "Couldn't initialize audio client for ["sv << format.name << "]: [0x"sv << util::hex(status).to_string_view() << ']';
    return nullptr;
  }

  return audio_client;
}

const wchar_t *no_null(const wchar_t *str) {
  return str ? str : L"Unknown";
}

format_t::type_e validate_device(device_t &device, int sample_rate) {
  for(const auto &format : formats) {
    // Ensure WaveFromat is compatible
    auto audio_client = make_audio_client(device, format, sample_rate);

    BOOST_LOG(debug) << format.name << ": "sv << (!audio_client ? "unsupported"sv : "supported"sv);

    if(audio_client) {
      return format.type;
    }
  }

  return format_t::none;
}

device_t default_device(device_enum_t &device_enum) {
  device_t device;
  HRESULT status;
  status = device_enum->GetDefaultAudioEndpoint(
    eRender,
    eConsole,
    &device);


  if(FAILED(status)) {
    BOOST_LOG(error) << "Couldn't create audio Device [0x"sv << util::hex(status).to_string_view() << ']';

    return nullptr;
  }

  return device;
}

class mic_wasapi_t : public mic_t {
public:
  capture_e sample(std::vector<std::int16_t> &sample_out) override {
    auto sample_size = sample_out.size() / channels_out * channels_in;
    while(sample_buf_pos - std::begin(sample_buf) < sample_size) {
      //FIXME: Use IAudioClient3 instead of IAudioClient, that would allows for adjusting the latency of the audio samples
      auto capture_result = _fill_buffer();

      if(capture_result != capture_e::ok) {
        return capture_result;
      }
    }

    switch(channels_out) {
    case 2:
      pipe->to_stereo(sample_out, sample_buf);
      break;
    case 6:
      pipe->to_51(sample_out, sample_buf);
      break;
    case 8:
      pipe->to_71(sample_out, sample_buf);
      break;
    default:
      BOOST_LOG(error) << "converting to ["sv << channels_out << "] channels is not supported"sv;
      return capture_e::error;
    }

    // The excess samples should be in front of the queue
    std::move(&sample_buf[sample_size], sample_buf_pos, std::begin(sample_buf));
    sample_buf_pos -= sample_size;

    return capture_e::ok;
  }


  int init(std::uint32_t sample_rate, std::uint32_t frame_size, std::uint32_t channels_out) {
    audio_event.reset(CreateEventA(nullptr, FALSE, FALSE, nullptr));
    if(!audio_event) {
      BOOST_LOG(error) << "Couldn't create Event handle"sv;

      return -1;
    }

    HRESULT status;

    status = CoCreateInstance(
      CLSID_MMDeviceEnumerator,
      nullptr,
      CLSCTX_ALL,
      IID_IMMDeviceEnumerator,
      (void **)&device_enum);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't create Device Enumerator [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    auto device = default_device(device_enum);
    if(!device) {
      return -1;
    }

    for(auto &format : formats) {
      BOOST_LOG(debug) << "Trying audio format ["sv << format.name << ']';
      audio_client = make_audio_client(device, format, sample_rate);

      if(audio_client) {
        BOOST_LOG(debug) << "Found audio format ["sv << format.name << ']';
        channels_in        = format.channels;
        this->channels_out = channels_out;

        switch(channels_in) {
        case 1:
          pipe = std::make_unique<mono_t>();
          break;
        case 2:
          pipe = std::make_unique<stereo_t>();
          break;
        case 6:
          pipe = std::make_unique<surr51_t>();
          break;
        case 8:
          pipe = std::make_unique<surr71_t>();
          break;
        default:
          BOOST_LOG(error) << "converting from ["sv << channels_in << "] channels is not supported"sv;
          return -1;
        }
        break;
      }
    }

    if(!audio_client) {
      BOOST_LOG(error) << "Couldn't find supported format for audio"sv;
      return -1;
    }

    REFERENCE_TIME default_latency;
    audio_client->GetDevicePeriod(&default_latency, nullptr);
    default_latency_ms = default_latency / 1000;

    std::uint32_t frames;
    status = audio_client->GetBufferSize(&frames);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't acquire the number of audio frames [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    // *2 --> needs to fit double
    sample_buf     = util::buffer_t<std::int16_t> { std::max(frames, frame_size) * 2 * channels_in };
    sample_buf_pos = std::begin(sample_buf);

    status = audio_client->GetService(IID_IAudioCaptureClient, (void **)&audio_capture);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't initialize audio capture client [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    status = audio_client->SetEventHandle(audio_event.get());
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't set event handle [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    status = audio_client->Start();
    if(FAILED(status)) {
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
    switch(status) {
    case WAIT_OBJECT_0:
      break;
    case WAIT_TIMEOUT:
      return capture_e::timeout;
    default:
      BOOST_LOG(error) << "Couldn't wait for audio event: [0x"sv << util::hex(status).to_string_view() << ']';
      return capture_e::error;
    }

    std::uint32_t packet_size {};
    for(
      status = audio_capture->GetNextPacketSize(&packet_size);
      SUCCEEDED(status) && packet_size > 0;
      status = audio_capture->GetNextPacketSize(&packet_size)) {
      DWORD buffer_flags;
      status = audio_capture->GetBuffer(
        (BYTE **)&sample_aligned.samples,
        &block_aligned.audio_sample_size,
        &buffer_flags,
        nullptr, nullptr);

      switch(status) {
      case S_OK:
        break;
      case AUDCLNT_E_DEVICE_INVALIDATED:
        return capture_e::reinit;
      default:
        BOOST_LOG(error) << "Couldn't capture audio [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }

      sample_aligned.uninitialized = std::end(sample_buf) - sample_buf_pos;
      auto n                       = std::min(sample_aligned.uninitialized, block_aligned.audio_sample_size * channels_in);

      if(buffer_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        std::fill_n(sample_buf_pos, n, 0);
      }
      else {
        std::copy_n(sample_aligned.samples, n, sample_buf_pos);
      }

      sample_buf_pos += n;

      audio_capture->ReleaseBuffer(block_aligned.audio_sample_size);
    }

    if(status == AUDCLNT_E_DEVICE_INVALIDATED) {
      return capture_e::reinit;
    }

    if(FAILED(status)) {
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

  REFERENCE_TIME default_latency_ms;

  util::buffer_t<std::int16_t> sample_buf;
  std::int16_t *sample_buf_pos;

  // out --> our audio output
  int channels_out;
  // in --> our wasapi input
  int channels_in;

  std::unique_ptr<audio_pipe_t> pipe;
};

class audio_control_t : public ::platf::audio_control_t {
public:
  std::optional<sink_t> sink_info() override {
    auto virtual_adapter_name = L"Steam Streaming Speakers"sv;

    sink_t sink;

    audio::device_enum_t device_enum;
    auto status = CoCreateInstance(
      CLSID_MMDeviceEnumerator,
      nullptr,
      CLSCTX_ALL,
      IID_IMMDeviceEnumerator,
      (void **)&device_enum);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't create Device Enumerator: [0x"sv << util::hex(status).to_string_view() << ']';

      return std::nullopt;
    }

    auto device = default_device(device_enum);
    if(!device) {
      return std::nullopt;
    }

    audio::wstring_t wstring;
    device->GetId(&wstring);

    sink.host = converter.to_bytes(wstring.get());

    collection_t collection;
    status = device_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't enumerate: [0x"sv << util::hex(status).to_string_view() << ']';

      return std::nullopt;
    }

    UINT count;
    collection->GetCount(&count);

    std::string virtual_device_id = config::audio.virtual_sink;
    for(auto x = 0; x < count; ++x) {
      audio::device_t device;
      collection->Item(x, &device);

      auto type = validate_device(device, SAMPLE_RATE);
      if(type == format_t::none) {
        continue;
      }

      audio::wstring_t wstring;
      device->GetId(&wstring);

      audio::prop_t prop;
      device->OpenPropertyStore(STGM_READ, &prop);

      prop_var_t adapter_friendly_name;
      prop_var_t device_friendly_name;
      prop_var_t device_desc;

      prop->GetValue(PKEY_Device_FriendlyName, &device_friendly_name.prop);
      prop->GetValue(PKEY_DeviceInterface_FriendlyName, &adapter_friendly_name.prop);
      prop->GetValue(PKEY_Device_DeviceDesc, &device_desc.prop);

      auto adapter_name = no_null((LPWSTR)adapter_friendly_name.prop.pszVal);
      BOOST_LOG(verbose)
        << L"===== Device ====="sv << std::endl
        << L"Device ID          : "sv << wstring.get() << std::endl
        << L"Device name        : "sv << no_null((LPWSTR)device_friendly_name.prop.pszVal) << std::endl
        << L"Adapter name       : "sv << adapter_name << std::endl
        << L"Device description : "sv << no_null((LPWSTR)device_desc.prop.pszVal) << std::endl
        << std::endl;

      if(virtual_device_id.empty() && adapter_name == virtual_adapter_name) {
        virtual_device_id = converter.to_bytes(wstring.get());
      }
    }

    if(!virtual_device_id.empty()) {
      sink.null = std::make_optional(sink_t::null_t {
        "virtual-"s.append(formats[format_t::stereo - 1].name) + virtual_device_id,
        "virtual-"s.append(formats[format_t::surr51 - 1].name) + virtual_device_id,
        "virtual-"s.append(formats[format_t::surr71 - 1].name) + virtual_device_id,
      });
    }

    return sink;
  }

  std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) override {
    auto mic = std::make_unique<mic_wasapi_t>();

    if(mic->init(sample_rate, frame_size, channels)) {
      return nullptr;
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
  std::optional<std::wstring> set_format(const std::string &sink) {
    std::string_view sv { sink.c_str(), sink.size() };

    format_t::type_e type = format_t::none;
    // sink format:
    // [virtual-(format name)]device_id
    auto prefix = "virtual-"sv;
    if(sv.find(prefix) == 0) {
      sv = sv.substr(prefix.size(), sv.size() - prefix.size());

      for(auto &format : formats) {
        auto &name = format.name;
        if(sv.find(name) == 0) {
          type = format.type;
          sv   = sv.substr(name.size(), sv.size() - name.size());

          break;
        }
      }
    }

    auto wstring_device_id = converter.from_bytes(sv.data());

    if(type == format_t::none) {
      // wstring_device_id does not contain virtual-(format name)
      // It's a simple deviceId, just pass it back
      return std::make_optional(std::move(wstring_device_id));
    }

    wave_format_t wave_format;
    auto status = policy->GetMixFormat(wstring_device_id.c_str(), &wave_format);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't acquire Wave Format [0x"sv << util::hex(status).to_string_view() << ']';

      return std::nullopt;
    }

    if(init_wave_format(wave_format, SAMPLE_RATE)) {
      return std::nullopt;
    }
    set_wave_format(wave_format, formats[(int)type - 1]);

    WAVEFORMATEXTENSIBLE p {};
    status = policy->SetDeviceFormat(wstring_device_id.c_str(), wave_format.get(), (WAVEFORMATEX *)&p);

    // Surround 5.1 might contain side-{left, right} instead of speaker in the back
    // Try again with different speaker mask.
    if(status == 0x88890008 && type == format_t::surr51) {
      set_wave_format(wave_format, surround_51_side_speakers);
      status = policy->SetDeviceFormat(wstring_device_id.c_str(), wave_format.get(), (WAVEFORMATEX *)&p);
    }
    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't set Wave Format [0x"sv << util::hex(status).to_string_view() << ']';

      return std::nullopt;
    }

    return std::make_optional(std::move(wstring_device_id));
  }

  int set_sink(const std::string &sink) override {
    auto wstring_device_id = set_format(sink);
    if(!wstring_device_id) {
      return -1;
    }

    int failure {};
    for(int x = 0; x < (int)ERole_enum_count; ++x) {
      auto status = policy->SetDefaultEndpoint(wstring_device_id->c_str(), (ERole)x);
      if(status) {
        BOOST_LOG(warning) << "Couldn't set ["sv << sink << "] to role ["sv << x << ']';

        ++failure;
      }
    }

    return failure;
  }

  int init() {
    auto status = CoCreateInstance(
      CLSID_CPolicyConfigClient,
      nullptr,
      CLSCTX_ALL,
      IID_IPolicyConfig,
      (void **)&policy);

    if(FAILED(status)) {
      BOOST_LOG(error) << "Couldn't create audio policy config: [0x"sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    return 0;
  }

  ~audio_control_t() override {}

  policy_t policy;
};
} // namespace platf::audio

namespace platf {

// It's not big enough to justify it's own source file :/
namespace dxgi {
int init();
}

std::unique_ptr<audio_control_t> audio_control() {
  auto control = std::make_unique<audio::audio_control_t>();

  if(control->init()) {
    return nullptr;
  }

  return control;
}

std::unique_ptr<deinit_t> init() {
  if(dxgi::init()) {
    return nullptr;
  }
  return std::make_unique<platf::audio::co_init_t>();
}
} // namespace platf
