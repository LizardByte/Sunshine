//
// Created by loki on 5/16/21.
//
#include <bitset>
#include <sstream>

#include <boost/regex.hpp>

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include "sunshine/platform/common.h"

#include "sunshine/config.h"
#include "sunshine/main.h"
#include "sunshine/thread_safe.h"

namespace platf {
using namespace std::literals;

constexpr pa_channel_position_t position_mapping[] {
  PA_CHANNEL_POSITION_FRONT_LEFT,
  PA_CHANNEL_POSITION_FRONT_RIGHT,
  PA_CHANNEL_POSITION_FRONT_CENTER,
  PA_CHANNEL_POSITION_LFE,
  PA_CHANNEL_POSITION_REAR_LEFT,
  PA_CHANNEL_POSITION_REAR_RIGHT,
  PA_CHANNEL_POSITION_SIDE_LEFT,
  PA_CHANNEL_POSITION_SIDE_RIGHT,
};

std::string to_string(const char *name, const std::uint8_t *mapping, int channels) {
  std::stringstream ss;

  ss << "rate=48000 sink_name="sv << name << " format=s16le channels="sv << channels << " channel_map="sv;
  std::for_each_n(mapping, channels - 1, [&ss](std::uint8_t pos) {
    ss << pa_channel_position_to_string(position_mapping[pos]) << ',';
  });

  ss << pa_channel_position_to_string(position_mapping[mapping[channels - 1]]);

  ss << " sink_properties=device.description="sv << name;
  auto result = ss.str();

  BOOST_LOG(debug) << "null-sink args: "sv << result;
  return result;
}

struct mic_attr_t : public mic_t {
  util::safe_ptr<pa_simple, pa_simple_free> mic;

  capture_e sample(std::vector<std::int16_t> &sample_buf) override {
    auto sample_size = sample_buf.size();

    auto buf = sample_buf.data();
    int status;
    if(pa_simple_read(mic.get(), buf, sample_size * 2, &status)) {
      BOOST_LOG(error) << "pa_simple_read() failed: "sv << pa_strerror(status);

      return capture_e::error;
    }

    return capture_e::ok;
  }
};

std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) {
  auto mic = std::make_unique<mic_attr_t>();

  pa_sample_spec ss { PA_SAMPLE_S16LE, sample_rate, (std::uint8_t)channels };
  pa_channel_map pa_map;

  pa_map.channels = channels;
  std::for_each_n(pa_map.map, pa_map.channels, [mapping](auto &channel) mutable {
    channel = position_mapping[*mapping++];
  });

  pa_buffer_attr pa_attr = {};
  pa_attr.maxlength = frame_size * 8;

  int status;

  const char *audio_sink = "@DEFAULT_MONITOR@";
  if(!config::audio.sink.empty()) {
    audio_sink = config::audio.sink.c_str();
  }

  mic->mic.reset(
    pa_simple_new(nullptr, "sunshine",
      pa_stream_direction_t::PA_STREAM_RECORD, audio_sink,
      "sunshine-record", &ss, &pa_map, &pa_attr, &status));

  if(!mic->mic) {
    auto err_str = pa_strerror(status);
    BOOST_LOG(error) << "pa_simple_new() failed: "sv << err_str;

    log_flush();
    std::abort();
  }

  return mic;
}

namespace pa {
template<bool B, class T>
struct add_const_helper;

template<class T>
struct add_const_helper<true, T> {
  using type = const std::remove_pointer_t<T> *;
};

template<class T>
struct add_const_helper<false, T> {
  using type = const T *;
};

template<class T>
using add_const_t = typename add_const_helper<std::is_pointer_v<T>, T>::type;

template<class T>
void pa_free(T *p) {
  pa_xfree(p);
}
using ctx_t    = util::safe_ptr<pa_context, pa_context_unref>;
using loop_t   = util::safe_ptr<pa_mainloop, pa_mainloop_free>;
using op_t     = util::safe_ptr<pa_operation, pa_operation_unref>;
using string_t = util::safe_ptr<char, pa_free<char>>;

template<class T>
using cb_t = std::function<void(ctx_t::pointer, add_const_t<T> i, int eol)>;

template<class T>
void cb(ctx_t::pointer ctx, add_const_t<T> i, int eol, void *userdata) {
  auto &f = *(cb_t<T> *)userdata;

  // For some reason, pulseaudio calls this callback after disconnecting
  if(i && eol) {
    return;
  }

  f(ctx, i, eol);
}

void cb_i(ctx_t::pointer ctx, std::uint32_t i, void *userdata) {
  auto alarm = (safe::alarm_raw_t<int> *)userdata;

  alarm->ring(i);
}

void ctx_state_cb(ctx_t::pointer ctx, void *userdata) {
  auto &f = *(std::function<void(ctx_t::pointer)> *)userdata;

  f(ctx);
}

void success_cb(ctx_t::pointer ctx, int status, void *userdata) {
  assert(userdata != nullptr);

  auto alarm = (safe::alarm_raw_t<int> *)userdata;
  alarm->ring(status ? 0 : 1);
}

class server_t : public audio_control_t {
  enum ctx_event_e : int {
    ready,
    terminated,
    failed
  };

public:
  loop_t loop;
  ctx_t ctx;

  struct {
    std::uint32_t stereo     = PA_INVALID_INDEX;
    std::uint32_t surround51 = PA_INVALID_INDEX;
    std::uint32_t surround71 = PA_INVALID_INDEX;
  } index;

  std::unique_ptr<safe::event_t<ctx_event_e>> events;
  std::unique_ptr<std::function<void(ctx_t::pointer)>> events_cb;

  std::thread worker;
  int init() {
    events = std::make_unique<safe::event_t<ctx_event_e>>();
    loop.reset(pa_mainloop_new());
    ctx.reset(pa_context_new(pa_mainloop_get_api(loop.get()), "sunshine"));

    events_cb = std::make_unique<std::function<void(ctx_t::pointer)>>([this](ctx_t::pointer ctx) {
      switch(pa_context_get_state(ctx)) {
      case PA_CONTEXT_READY:
        events->raise(ready);
        break;
      case PA_CONTEXT_TERMINATED:
        BOOST_LOG(debug) << "Pulseadio context terminated"sv;
        events->raise(terminated);
        break;
      case PA_CONTEXT_FAILED:
        BOOST_LOG(debug) << "Pulseadio context failed"sv;
        events->raise(failed);
        break;
      case PA_CONTEXT_CONNECTING:
        BOOST_LOG(debug) << "Connecting to pulseaudio"sv;
      case PA_CONTEXT_UNCONNECTED:
      case PA_CONTEXT_AUTHORIZING:
      case PA_CONTEXT_SETTING_NAME:
        break;
      }
    });

    pa_context_set_state_callback(ctx.get(), ctx_state_cb, events_cb.get());

    auto status = pa_context_connect(ctx.get(), nullptr, PA_CONTEXT_NOFLAGS, nullptr);
    if(status) {
      BOOST_LOG(error) << "Couldn't connect to pulseaudio: "sv << pa_strerror(status);
      return -1;
    }

    worker = std::thread {
      [](loop_t::pointer loop) {
        int retval;
        auto status = pa_mainloop_run(loop, &retval);

        if(status < 0) {
          BOOST_LOG(fatal) << "Couldn't run pulseaudio main loop"sv;

          log_flush();
          std::abort();
        }
      },
      loop.get()
    };

    auto event = events->pop();
    if(event == failed) {
      return -1;
    }

    return 0;
  }

  int load_null(const char *name, const std::uint8_t *channel_mapping, int channels) {
    auto alarm = safe::make_alarm<int>();

    op_t op {
      pa_context_load_module(
        ctx.get(),
        "module-null-sink",
        to_string(name, channel_mapping, channels).c_str(),
        cb_i,
        alarm.get()),
    };

    alarm->wait();
    return *alarm->status();
  }

  int unload_null(std::uint32_t i) {
    if(i == PA_INVALID_INDEX) {
      return 0;
    }

    auto alarm = safe::make_alarm<int>();

    op_t op {
      pa_context_unload_module(ctx.get(), i, success_cb, alarm.get())
    };

    alarm->wait();

    if(*alarm->status()) {
      BOOST_LOG(error) << "Couldn't unload null-sink with index ["sv << i << "]: "sv << pa_strerror(pa_context_errno(ctx.get()));
      return -1;
    }

    return 0;
  }

  std::optional<sink_t> sink_info() override {
    constexpr auto stereo     = "sink-sunshine-stereo";
    constexpr auto surround51 = "sink-sunshine-surround51";
    constexpr auto surround71 = "sink-sunshine-surround71";

    auto alarm = safe::make_alarm<int>();

    sink_t sink;

    // If hardware sink with more channels found, set that as host
    int channels = 0;
    // Count of all virtual sinks that are created by us
    int nullcount = 0;

    cb_t<pa_sink_info *> f = [&](ctx_t::pointer ctx, const pa_sink_info *sink_info, int eol) {
      if(!sink_info) {
        if(!eol) {
          BOOST_LOG(error) << "Couldn't get pulseaudio sink info: "sv << pa_strerror(pa_context_errno(ctx));

          alarm->ring(-1);
        }

        alarm->ring(0);
        return;
      }

      if(sink_info->active_port != nullptr) {
        sink.host = sink_info->name;
        channels  = sink_info->channel_map.channels;
      }

      // Ensure Sunshine won't create a sink that already exists.
      if(!std::strcmp(sink_info->name, stereo)) {
        index.stereo = sink_info->owner_module;

        ++nullcount;
      }
      else if(!std::strcmp(sink_info->name, surround51)) {
        index.surround51 = sink_info->owner_module;

        ++nullcount;
      }
      else if(!std::strcmp(sink_info->name, surround71)) {
        index.surround71 = sink_info->owner_module;

        ++nullcount;
      }
    };

    op_t op { pa_context_get_sink_info_list(ctx.get(), cb<pa_sink_info *>, &f) };

    if(!op) {
      BOOST_LOG(error) << "Couldn't create card info operation: "sv << pa_strerror(pa_context_errno(ctx.get()));

      return std::nullopt;
    }

    alarm->wait();

    if(*alarm->status()) {
      return std::nullopt;
    }

    if(!channels) {
      BOOST_LOG(warning) << "Couldn't find an active sink"sv;
    }

    if(index.stereo == PA_INVALID_INDEX) {
      index.stereo = load_null(stereo, speaker::map_stereo, sizeof(speaker::map_stereo));
      if(index.stereo == PA_INVALID_INDEX) {
        BOOST_LOG(warning) << "Couldn't create virtual sink for stereo: "sv << pa_strerror(pa_context_errno(ctx.get()));
      }
      else {
        ++nullcount;
      }
    }

    if(index.surround51 == PA_INVALID_INDEX) {
      index.surround51 = load_null(surround51, speaker::map_surround51, sizeof(speaker::map_surround51));
      if(index.surround51 == PA_INVALID_INDEX) {
        BOOST_LOG(warning) << "Couldn't create virtual sink for surround-51: "sv << pa_strerror(pa_context_errno(ctx.get()));
      }
      else {
        ++nullcount;
      }
    }

    if(index.surround71 == PA_INVALID_INDEX) {
      index.surround71 = load_null(surround71, speaker::map_surround71, sizeof(speaker::map_surround71));
      if(index.surround71 == PA_INVALID_INDEX) {
        BOOST_LOG(warning) << "Couldn't create virtual sink for surround-71: "sv << pa_strerror(pa_context_errno(ctx.get()));
      }
      else {
        ++nullcount;
      }
    }

    if(nullcount == 3) {
      sink.null = std::make_optional(sink_t::null_t { stereo, surround51, surround71 });
    }

    return std::make_optional(std::move(sink));
  }

  std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) override {
    return ::platf::microphone(mapping, channels, sample_rate, frame_size);
  }

  int set_sink(const std::string &sink) override {
    auto alarm = safe::make_alarm<int>();

    op_t op {
      pa_context_set_default_sink(
        ctx.get(), sink.c_str(), success_cb, alarm.get()),
    };

    if(!op) {
      BOOST_LOG(error) << "Couldn't create set default-sink operation: "sv << pa_strerror(pa_context_errno(ctx.get()));
      return -1;
    }

    alarm->wait();
    if(*alarm->status()) {
      BOOST_LOG(error) << "Couldn't set default-sink ["sv << sink << "]: "sv << pa_strerror(pa_context_errno(ctx.get()));

      return -1;
    }

    return 0;
  }

  ~server_t() override {
    unload_null(index.stereo);
    unload_null(index.surround51);
    unload_null(index.surround71);

    if(worker.joinable()) {
      pa_context_disconnect(ctx.get());

      KITTY_WHILE_LOOP(auto event = events->pop(), event != terminated && event != failed, {
        event = events->pop();
      })

      pa_mainloop_quit(loop.get(), 0);
      worker.join();
    }
  }
};
} // namespace pa

std::unique_ptr<audio_control_t> audio_control() {
  auto audio = std::make_unique<pa::server_t>();

  if(audio->init()) {
    return nullptr;
  }

  return audio;
}
} // namespace platf