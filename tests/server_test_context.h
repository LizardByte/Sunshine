/**
 * @file tests/server_test_context.h
 * @brief Instance-owned server dependencies for HTTP and authorization tests.
 */
#pragma once

// local includes
#include <src/audio.h>
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
    config::store_t configuration;  ///< Startup settings and synchronized credentials owned by this fixture.
    runtime::infrastructure_t infrastructure;  ///< Application infrastructure owned by this context.
    safe::mail_t host_mail {infrastructure.mailbox()};  ///< Host mailbox shared by this context's services.
    bool &cursor {infrastructure.cursor()};  ///< Host cursor policy owned by the infrastructure.
    audio::service_t audios {configuration.values(), host_mail};  ///< Shared inactive audio control.
    display_device::manager_t displays;  ///< Display settings and retry owner.
    platf::runtime_t platform {configuration.values(), host_mail, infrastructure.scheduler(), infrastructure.lifetime(), displays
#ifdef _WIN32
                               ,
                               infrastructure.preferences()
#endif
    };  ///< Inactive native capture and application resources.
    input::service_t inputs {configuration.values(), infrastructure.scheduler(), host_mail, cursor};  ///< Inactive host input owner.
    proc::proc_t processes {configuration.values(), displays, inputs};  ///< Application process owner.
    video::service_t videos {configuration.values(), host_mail, cursor, displays, platform};  ///< Inactive encoder and capture owner.
    stream::service_t streams {
      configuration.values(),
      host_mail,
      stream::host_services_t {
        .scheduler = infrastructure.scheduler(),
        .inputs = inputs,
        .videos = videos,
        .processes = processes,
        .displays = displays,
        .platform = platform,
        .audios = audios,
      },
    };  ///< Stream broadcast owner retaining the underlying services after the temporary binding view is destroyed.
    rtsp_stream::service_t rtsp {configuration.values(), streams, inputs, videos, host_mail};  ///< RTSP listener and launch-session owner.
    http::state_t http_state {configuration};  ///< Host HTTP identity and origin policy.
    nvhttp::service_t gamestream {configuration.values(), http_state, processes, displays, videos, rtsp, host_mail};  ///< Pairing and authorization owner.
  };
}  // namespace test_utils
