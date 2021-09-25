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

#include "sunshine/thread_safe.h"
#include "sunshine/utility.h"

struct sockaddr;
struct AVFrame;

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

  img_t(img_t &&)      = delete;
  img_t(const img_t &) = delete;
  img_t &operator=(img_t &&) = delete;
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

  // On Windows, it is not possible to create a virtual sink
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
  virtual int set_frame(AVFrame *frame) {
    std::abort(); // ^ This function must never be called
    return -1;
  };

  virtual void set_colorspace(std::uint32_t colorspace, std::uint32_t color_range) {};

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
   * When display has a new image ready, this callback will be called with the new image.
   * 
   * On Break Request -->
   *    Returns nullptr
   * 
   * On Success -->
   *    Returns the image object that should be filled next.
   *    This may or may not be the image send with the callback
   */
  using snapshot_cb_t = std::function<std::shared_ptr<img_t>(std::shared_ptr<img_t> &img)>;

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
 * framerate --> The peak number of images per second
 * 
 * Returns display_t based on hwdevice_type
 */
std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, int framerate);

// A list of names of displays accepted as display_name with the mem_type_e
std::vector<std::string> display_names(mem_type_e hwdevice_type);

input_t input();
void move_mouse(input_t &input, int deltaX, int deltaY);
void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y);
void button_mouse(input_t &input, int button, bool release);
void scroll(input_t &input, int distance);
void keyboard(input_t &input, uint16_t modcode, bool release);
void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state);

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
