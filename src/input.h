/**
 * @file src/input.h
 * @brief todo
 */
#pragma once

#include <functional>

#include "platform/common.h"
#include "thread_safe.h"

namespace input {
  struct input_t;

  void
  print(void *input);
  void
  reset(std::shared_ptr<input_t> &input);
  void
  passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);

  [[nodiscard]] std::unique_ptr<platf::deinit_t>
  init();

  std::shared_ptr<input_t>
  alloc(safe::mail_t mail);

  struct touch_port_t: public platf::touch_port_t {
    int env_width, env_height;

    // Offset x and y coordinates of the client
    float client_offsetX, client_offsetY;

    float scalar_inv;

    explicit
    operator bool() const {
      return width != 0 && height != 0 && env_width != 0 && env_height != 0;
    }
  };

  std::pair<float, float>
  scale_client_contact_area(const std::pair<float, float> &val, uint16_t rotation, const std::pair<float, float> &scalar);
}  // namespace input
