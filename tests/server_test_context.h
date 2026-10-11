/**
 * @file tests/server_test_context.h
 * @brief Instance-owned server dependencies for HTTP and authorization tests.
 */
#pragma once

// local includes
#include <src/display_device.h>
#include <src/globals.h>
#include <src/httpcommon.h>
#include <src/input.h>
#include <src/nvhttp.h>
#include <src/process.h>
#include <src/rtsp.h>
#include <src/stream.h>
#include <src/video.h>

namespace test_utils {
  /**
   * @brief Assemble inactive server services without probing host input or encoders.
   */
  struct server_context_t {
    runtime::infrastructure_t infrastructure;  ///< Application infrastructure owned by this context.
    safe::mail_t host_mail {infrastructure.mailbox()};  ///< Host mailbox shared by this context's services.
    bool &cursor {infrastructure.cursor()};  ///< Host cursor policy owned by the infrastructure.
    display_device::manager_t displays;  ///< Display settings and retry owner.
    platf::runtime_t platform {host_mail, infrastructure.scheduler(), infrastructure.lifetime(), displays
#ifdef _WIN32
                               ,
                               infrastructure.preferences()
#endif
    };  ///< Inactive native capture and application resources.
    input::service_t inputs {infrastructure.scheduler(), host_mail, cursor};  ///< Inactive host input owner.
    proc::proc_t processes {displays, inputs};  ///< Application process owner.
    video::service_t videos {host_mail, cursor, displays, platform};  ///< Inactive encoder and capture owner.
    stream::service_t streams {host_mail, infrastructure.scheduler(), inputs, videos, processes, displays, platform};  ///< Stream broadcast owner.
    rtsp_stream::service_t rtsp {streams, inputs, videos, host_mail};  ///< RTSP listener and launch-session owner.
    http::state_t http_state;  ///< Host HTTP identity and origin policy.
    nvhttp::service_t gamestream {http_state, processes, displays, videos, rtsp, host_mail};  ///< Pairing and authorization owner.
  };
}  // namespace test_utils
