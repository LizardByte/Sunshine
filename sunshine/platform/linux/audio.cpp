//
// Created by loki on 5/16/21.
//
#include <bitset>
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

struct mic_attr_t : public mic_t {
  pa_sample_spec ss;
  util::safe_ptr<pa_simple, pa_simple_free> mic;

  explicit mic_attr_t(pa_sample_format format, std::uint32_t sample_rate,
    std::uint8_t channels) : ss { format, sample_rate, channels }, mic {} {}

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

std::unique_ptr<mic_t> microphone(std::uint32_t sample_rate, std::uint32_t) {
  auto mic = std::make_unique<mic_attr_t>(PA_SAMPLE_S16LE, sample_rate, 2);

  int status;

  const char *audio_sink = "@DEFAULT_MONITOR@";
  if(!config::audio.sink.empty()) {
    audio_sink = config::audio.sink.c_str();
  }

  mic->mic.reset(
    pa_simple_new(nullptr, "sunshine",
      pa_stream_direction_t::PA_STREAM_RECORD, audio_sink,
      "sunshine-record", &mic->ss, nullptr, nullptr, &status));

  if(!mic->mic) {
    auto err_str = pa_strerror(status);
    BOOST_LOG(error) << "pa_simple_new() failed: "sv << err_str;

    log_flush();
    std::abort();
  }

  return mic;
}

namespace pa {
template<class T>
void pa_free(T *p) {
  pa_xfree(p);
}
using ctx_t    = util::safe_ptr<pa_context, pa_context_unref>;
using loop_t   = util::safe_ptr<pa_mainloop, pa_mainloop_free>;
using op_t     = util::safe_ptr<pa_operation, pa_operation_unref>;
using string_t = util::safe_ptr<char, pa_free<char>>;

template<class T>
using cb_t = std::function<void(ctx_t::pointer, const T *, int eol)>;

template<class T>
void cb(ctx_t::pointer ctx, const T *i, int eol, void *userdata) {
  auto &f = *(cb_t<T> *)userdata;

  // For some reason, pulseaudio calls this callback after disconnecting
  if(i && eol) {
    return;
  }

  f(ctx, i, eol);
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

profile_t make(pa_card_profile_info2 *profile) {
  return profile_t {
    profile->name,
    profile->description,
    profile->available == 1
  };
}

card_t make(const pa_card_info *card) {
  boost::regex stereo_expr {
    ".*output(?!.*surround).*stereo.*"
  };

  boost::regex surround51_expr {
    ".*output.*surround(.*(^\\d|51).*|$)"
  };

  boost::regex surround71_expr {
    ".*output.*surround.*7.?1.*"
  };

  std::vector<profile_t> stereo;
  std::vector<profile_t> surround51;
  std::vector<profile_t> surround71;

  std::for_each_n(card->profiles2, card->n_profiles, [&](pa_card_profile_info2 *profile) {
    if(boost::regex_match(profile->name, stereo_expr)) {
      stereo.emplace_back(make(profile));
    }
    if(boost::regex_match(profile->name, surround51_expr)) {
      surround51.emplace_back(make(profile));
    }
    if(boost::regex_match(profile->name, surround71_expr)) {
      surround71.emplace_back(make(profile));
    }
  });

  std::optional<profile_t> active_profile;
  if(card->active_profile2->name != "off"sv) {
    active_profile = make(card->active_profile2);
  }

  return card_t {
    card->name,
    card->driver,
    std::move(active_profile),
    std::move(stereo),
    std::move(surround51),
    std::move(surround71),
  };
}

const card_t *active_card(const std::vector<card_t> &cards) {
  for(auto &card : cards) {
    if(card.active_profile) {
      return &card;
    }
  }

  return nullptr;
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

  std::vector<card_t> card_info() override {
    auto alarm = safe::make_alarm<bool>();

    std::vector<card_t> cards;
    cb_t<pa_card_info> f = [&cards, alarm](ctx_t::pointer ctx, const pa_card_info *card_info, int eol) {
      if(!card_info) {
        if(!eol) {
          BOOST_LOG(error) << "Couldn't get pulseaudio card info: "sv << pa_strerror(pa_context_errno(ctx));
        }

        alarm->ring(true);
        return;
      }

      cards.emplace_back(make(card_info));
    };

    op_t op { pa_context_get_card_info_list(ctx.get(), cb<pa_card_info>, &f) };

    if(!op) {
      BOOST_LOG(error) << "Couldn't create card info operation: "sv << pa_strerror(pa_context_errno(ctx.get()));

      return {};
    }

    alarm->wait();

    return cards;
  }

  std::unique_ptr<mic_t> create_mic(std::uint32_t sample_rate, std::uint32_t frame_size) override {
    return microphone(sample_rate, frame_size);
  }

  int set_output(const card_t &card, const profile_t &profile) override {
    auto alarm = safe::make_alarm<int>();

    op_t op { pa_context_set_card_profile_by_name(
      ctx.get(), card.name.c_str(), profile.name.c_str(), success_cb, alarm.get())
    };

    if(!op) {
      BOOST_LOG(error) << "Couldn't create set profile operation: "sv << pa_strerror(pa_context_errno(ctx.get()));
      return -1;
    }

    alarm->wait();
    if(*alarm->status()) {
      BOOST_LOG(error) << "Couldn't set profile ["sv << profile.name << "]: "sv << pa_strerror(pa_context_errno(ctx.get()));

      return -1;
    }

    return 0;
  }

  ~server_t() override {
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

std::unique_ptr<deinit_t> init() {
  pa::server_t server;
  if(server.init()) {
    return std::make_unique<deinit_t>();
  }

  auto cards = server.card_info();

  for(auto &card : cards) {
    BOOST_LOG(info) << "---- CARD ----"sv;
    BOOST_LOG(info) << "Name: ["sv << card.name << ']';
    BOOST_LOG(info) << "Description: ["sv << card.description << ']';

    if(card.active_profile) {
      BOOST_LOG(info) << "Active profile:"sv;
      BOOST_LOG(info) << "  Name: [" << card.active_profile->name << ']';
      BOOST_LOG(info) << "  Description: ["sv << card.active_profile->description << ']';
      BOOST_LOG(info) << "  Available: ["sv << card.active_profile->available << ']';
      BOOST_LOG(info);
    }


    BOOST_LOG(info) << "  -- stereo --"sv;
    for(auto &profile : card.stereo) {
      BOOST_LOG(info) << "  "sv << profile.name << ": "sv << profile.description << " ("sv << profile.available << ')';
    }

    BOOST_LOG(info) << "  -- surround 5.1 --"sv;
    for(auto &profile : card.surround51) {
      BOOST_LOG(info) << "  "sv << profile.name << ": "sv << profile.description << " ("sv << profile.available << ')';
    }

    BOOST_LOG(info) << "  -- surround 7.1 --"sv;
    for(auto &profile : card.surround71) {
      BOOST_LOG(info) << "  "sv << profile.name << ": "sv << profile.description << " ("sv << profile.available << ')';
    }
  }

  return std::make_unique<deinit_t>();
}
} // namespace platf