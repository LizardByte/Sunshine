#pragma once

#include "platform/common.h"

namespace upnp {
  [[nodiscard]] std::unique_ptr<platf::deinit_t>
  start();
}
