//
// Created by loki on 6/21/19.
//

#ifndef SUNSHINE_COMMON_H
#define SUNSHINE_COMMON_H

#include <bitset>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include "src/main.h"
#include "src/thread_safe.h"
#include "src/utility.h"

extern "C" {
#include <moonlight-common-c/src/Limelight.h>
}

struct sockaddr;
struct AVFrame;
struct AVBufferRef;
struct AVHWFramesContext;

// Forward declarations of boost classes to avoid having to include boost headers
// here, which results in issues with Windows.h and WinSock2.h include order.
namespace boost {
namespace asio {
namespace ip {
class address;
} // namespace ip
} // namespace asio
namespace filesystem {
class path;
}
namespace process {
class child;
class group;
template<typename Char>
class basic_environment;
typedef basic_environment<char> environment;
} // namespace process
} // namespace boost
namespace video {
struct config_t;
}

namespace platf {
constexpr auto MAX_GAMEPADS = 32;

constexpr std::uint16_t DPAD_UP      = 0x0001;
constexpr std::uint16_t DPAD_DOWN    = 0x0002;
constexpr std::uint16_t DPAD_LEFT    = 0x0004;
constexpr std::uint16_t DPAD_RIGHT   = 0x0008;
constexpr std::uint16_t START        = 0x0010;
constexpr std::uint16_t BACK         = 0x0020;
constexpr std::uint16_t LEFT_STICK   = 0x0040;
constexpr std::uint16_t RIGHT_STICK  = 0x0080;
constexpr std::uint16_t LEFT_BUTTON  = 0x0100;
constexpr std::uint16_t RIGHT_BUTTON = 0x0200;
constexpr std::uint16_t HOME         = 0x0400;
constexpr std::uint16_t A            = 0x1000;
constexpr std::uint16_t B            = 0x2000;
constexpr std::uint16_t X            = 0x4000;
constexpr std::uint16_t Y            = 0x8000;

struct rumble_t {
  KITTY_DEFAULT_CONSTR(rumble_t)

  rumble_t(std::uint16_t id, std::uint16_t lowfreq, std::uint16_t highfreq)
      : id { id }, lowfreq { lowfreq }, highfreq { highfreq } {}

  std::uint16_t id;
  std::uint16_t lowfreq;
  std::uint16_t highfreq;
};
using rumble_queue_t = safe::mail_raw_t::queue_t<rumble_t>;

namespace speaker {
enum speaker_e {
  FRONT_LEFT,
  FRONT_RIGHT,
  FRONT_CENTER,
  LOW_FREQUENCY,
  BACK_LEFT,
  BACK_RIGHT,
  SIDE_LEFT,
  SIDE_RIGHT,
  MAX_SPEAKERS,
};

constexpr std::uint8_t map_stereo[] {
  FRONT_LEFT, FRONT_RIGHT
};
constexpr std::uint8_t map_surround51[] {
  FRONT_LEFT,
  FRONT_RIGHT,
  FRONT_CENTER,
  LOW_FREQUENCY,
  BACK_LEFT,
  BACK_RIGHT,
};
constexpr std::uint8_t map_surround71[] {
  FRONT_LEFT,
  FRONT_RIGHT,
  FRONT_CENTER,
  LOW_FREQUENCY,
  BACK_LEFT,
  BACK_RIGHT,
  SIDE_LEFT,
  SIDE_RIGHT,
};
} // namespace speaker

enum class mem_type_e {
  system,
  vaapi,
  dxgi,
  cuda,
  unknown
};

enum class pix_fmt_e {
  yuv420p,
  yuv420p10,
  nv12,
  p010,
  unknown
};

inline std::string_view from_pix_fmt(pix_fmt_e pix_fmt) {
  using namespace std::literals;
#define _CONVERT(x)  \
  case pix_fmt_e::x: \
    return #x##sv
  switch(pix_fmt) {
    _CONVERT(yuv420p);
    _CONVERT(yuv420p10);
    _CONVERT(nv12);
    _CONVERT(p010);
    _CONVERT(unknown);
  }
#undef _CONVERT

  return "unknown"sv;
}

// Dimensions for touchscreen input
struct touch_port_t {
  int offset_x, offset_y;
  int width, height;
};

struct gamepad_state_t {
  std::uint16_t buttonFlags;
  std::uint8_t lt;
  std::uint8_t rt;
  std::int16_t lsX;
  std::int16_t lsY;
  std::int16_t rsX;
  std::int16_t rsY;
};

class deinit_t {
public:
  virtual ~deinit_t() = default;
};

struct img_t {
public:
  img_t() = default;

  img_t(img_t &&)                 = delete;
  img_t(const img_t &)            = delete;
  img_t &operator=(img_t &&)      = delete;
  img_t &operator=(const img_t &) = delete;

  std::uint8_t *data {};
  std::int32_t width {};
  std::int32_t height {};
  std::int32_t pixel_pitch {};
  std::int32_t row_pitch {};

  virtual ~img_t() = default;
};

struct sink_t {
  // Play on host PC
  std::string host;

  // On macOS and Windows, it is not possible to create a virtual sink
  // Therefore, it is optional
  struct null_t {
    std::string stereo;
    std::string surround51;
    std::string surround71;
  };
  std::optional<null_t> null;
};

struct hwdevice_t {
  void *data {};
  AVFrame *frame {};

  virtual int convert(platf::img_t &img) {
    return -1;
  }

  /**
   * implementations must take ownership of 'frame'
   */
  virtual int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
    BOOST_LOG(error) << "Illegal call to hwdevice_t::set_frame(). Did you forget to override it?";
    return -1;
  };

  virtual void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) {};

  /**
   * Implementations may set parameters during initialization of the hwframes context
   */
  virtual void init_hwframes(AVHWFramesContext *frames) {};

  /**
   * Implementations may make modifications required before context derivation
   */
  virtual int prepare_to_derive_context(int hw_device_type) {
    return 0;
  };

  virtual ~hwdevice_t() = default;
};

enum class capture_e : int {
  ok,
  reinit,
  timeout,
  error
};

class display_t {
public:
  /**
   * When display has a new image ready or a timeout occurs, this callback will be called with the image.
   * If a frame was captured, frame_captured will be true. If a timeout occurred, it will be false.
   * 
   * On Break Request -->
   *    Returns nullptr
   * 
   * On Success -->
   *    Returns the image object that should be filled next.
   *    This may or may not be the image send with the callback
   */
  using snapshot_cb_t = std::function<std::shared_ptr<img_t>(std::shared_ptr<img_t> &img, bool frame_captured)>;

  display_t() noexcept : offset_x { 0 }, offset_y { 0 } {}

  /**
   * snapshot_cb --> the callback
   * std::shared_ptr<img_t> img --> The first image to use
   * bool *cursor --> A pointer to the flag that indicates wether the cursor should be captured as well
   * 
   * Returns either:
   *    capture_e::ok when stopping
   *    capture_e::error on error
   *    capture_e::reinit when need of reinitialization
   */
  virtual capture_e capture(snapshot_cb_t &&snapshot_cb, std::shared_ptr<img_t> img, bool *cursor) = 0;

  virtual std::shared_ptr<img_t> alloc_img() = 0;

  virtual int dummy_img(img_t *img) = 0;

  virtual std::shared_ptr<hwdevice_t> make_hwdevice(pix_fmt_e pix_fmt) {
    return std::make_shared<hwdevice_t>();
  }

  virtual bool is_hdr() {
    return false;
  }

  virtual bool get_hdr_metadata(SS_HDR_METADATA &metadata) {
    std::memset(&metadata, 0, sizeof(metadata));
    return false;
  }

  virtual ~display_t() = default;

  // Offsets for when streaming a specific monitor. By default, they are 0.
  int offset_x, offset_y;
  int env_width, env_height;

  int width, height;
};

class mic_t {
public:
  virtual capture_e sample(std::vector<std::int16_t> &frame_buffer) = 0;

  virtual ~mic_t() = default;
};

class audio_control_t {
public:
  virtual int set_sink(const std::string &sink) = 0;

  virtual std::unique_ptr<mic_t> microphone(const std::uint8_t *mapping, int channels, std::uint32_t sample_rate, std::uint32_t frame_size) = 0;

  virtual std::optional<sink_t> sink_info() = 0;

  virtual ~audio_control_t() = default;
};

void freeInput(void *);

using input_t = util::safe_ptr<void, freeInput>;

std::filesystem::path appdata();

std::string get_mac_address(const std::string_view &address);

std::string from_sockaddr(const sockaddr *const);
std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const);

std::unique_ptr<audio_control_t> audio_control();

/**
 * display_name --> The name of the monitor that SHOULD be displayed
 *    If display_name is empty --> Use the first monitor that's compatible you can find
 *    If you require to use this parameter in a seperate thread --> make a copy of it.
 * 
 * config --> Stream configuration
 * 
 * Returns display_t based on hwdevice_type
 */
std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);

// A list of names of displays accepted as display_name with the mem_type_e
std::vector<std::string> display_names(mem_type_e hwdevice_type);

boost::process::child run_unprivileged(const std::string &cmd, boost::filesystem::path &working_dir, boost::process::environment &env, FILE *file, std::error_code &ec, boost::process::group *group);

enum class thread_priority_e : int {
  low,
  normal,
  high,
  critical
};
void adjust_thread_priority(thread_priority_e priority);

// Allow OS-specific actions to be taken to prepare for streaming
void streaming_will_start();
void streaming_will_stop();

bool restart_supported();
bool restart();

struct batched_send_info_t {
  const char *buffer;
  size_t block_size;
  size_t block_count;

  std::uintptr_t native_socket;
  boost::asio::ip::address &target_address;
  uint16_t target_port;
};
bool send_batch(batched_send_info_t &send_info);

enum class qos_data_type_e : int {
  audio,
  video
};
std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t native_socket, boost::asio::ip::address &address, uint16_t port, qos_data_type_e data_type);

input_t input();
void move_mouse(input_t &input, int deltaX, int deltaY);
void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y);
void button_mouse(input_t &input, int button, bool release);
void scroll(input_t &input, int distance);
void hscroll(input_t &input, int distance);
void keyboard(input_t &input, uint16_t modcode, bool release);
void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state);
void unicode(input_t &input, char *utf8, int size);

int alloc_gamepad(input_t &input, int nr, rumble_queue_t rumble_queue);
void free_gamepad(input_t &input, int nr);

#define SERVICE_NAME "Sunshine"
#define SERVICE_TYPE "_nvstream._tcp"

namespace publish {
[[nodiscard]] std::unique_ptr<deinit_t> start();
}

[[nodiscard]] std::unique_ptr<deinit_t> init();

std::vector<std::string_view> &supported_gamepads();
} // namespace platf

#endif //SUNSHINE_COMMON_H
