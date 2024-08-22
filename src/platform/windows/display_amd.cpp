/**
 * @file src/platform/windows/display_amd.cpp
 * @brief Display capture implementation using AMD Direct Capture
 */

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include "display.h"
#include "misc.h"
#include "src/config.h"
#include "src/main.h"
#include "src/video.h"

#include <AMF/components/DisplayCapture.h>
#include <AMF/components/VideoConverter.h>
#include <AMF/core/Trace.h>

namespace platf {
  using namespace std::literals;
}

static void
free_frame(AVFrame *frame) {
  av_frame_free(&frame);
}

using frame_t = util::safe_ptr<AVFrame, free_frame>;

namespace platf::dxgi {
  amd_capture_t::amd_capture_t() {
  }

  amd_capture_t::~amd_capture_t() {
  }

   /**
   * @brief Get the next frame from the producer thread.
   * If not available, the capture thread blocks until one is, or the wait times out.
   * @param timeout how long to wait for the next frame
   * @param out a texture containing the frame just captured
   * @param out_time the timestamp of the frame just captured
   */
  capture_e
  amd_capture_t::next_frame(std::chrono::milliseconds timeout, amf::AMFData** out) {
    // BOOST_LOG(error) << "****** NEXT FRAME **********";
    // this CONSUMER runs in the capture thread
    // release_frame();
    // Poll for the next frame
    AMF_RESULT result;
    auto capture_start = std::chrono::steady_clock::now();
    do {
      result = captureComp->QueryOutput(out);
      if (result == AMF_REPEAT) {
        Sleep(2);

        // Check for capture timeout expiration
        if (std::chrono::steady_clock::now() - capture_start >= timeout) {
          captureComp->Flush();
          return platf::capture_e::timeout;
        }
      }
    } while (result != AMF_OK);

    // if (result != AMF_OK) {
    //   BOOST_LOG(error) << "****** NEXT FRAME NOT AMF_OK **********";
    //   BOOST_LOG(error) << "DisplayCapture::QueryOutput() failed: "sv << result;
    //   return capture_e::timeout;
    // }

    return capture_e::ok;
  }

  capture_e
  amd_capture_t::release_frame() {
    return capture_e::ok;
  }

  int
  amd_capture_t::init(display_base_t *display, const ::video::config_t &config, int output_index) {
    DXGI_ADAPTER_DESC adapter_desc;
    display->adapter->GetDesc(&adapter_desc);

    // Bail if this is not an AMD GPU
    if (adapter_desc.VendorId != 0x1002) {
      return -1;
    }

    // // FIXME: Don't use Direct Capture for a SDR P010 stream. The output is very dim.
    // // This seems like a possible bug in VideoConverter when upconverting 8-bit to 10-bit.
    // if (config.dynamicRange && !display->is_hdr()) {
    //   BOOST_LOG(info) << "AMD Direct Capture is disabled while 10-bit stream is in SDR mode"sv;
    //   return -1;
    // }

    // Create the capture context
    auto result = amf_factory->CreateContext(&context);

    // amf::AMFTrace* traceAMF;
    // amf_factory->GetTrace(&traceAMF);
    // traceAMF->SetGlobalLevel(AMF_TRACE_DEBUG);
    // traceAMF->EnableWriter(AMF_TRACE_WRITER_FILE, true);
    // traceAMF->SetWriterLevel(AMF_TRACE_WRITER_FILE, AMF_TRACE_DEBUG);
    // traceAMF->SetPath(L"D:/amflog.txt");

    // amf::AMFDebug* debugAMF;
    // amf_factory->GetDebug(&debugAMF);
    // debugAMF->AssertsEnable(false);

    if (result != AMF_OK) {
      BOOST_LOG(error) << "CreateContext() failed: "sv << result;
      return -1;
    }

    // Associate the context with our ID3D11Device. This will enable multithread protection on the device.
    result = context->InitDX11(display->device.get());
    if (result != AMF_OK) {
      BOOST_LOG(error) << "InitDX11() failed: "sv << result;
      return -1;
    }

    // Create the DisplayCapture component
    result = amf_factory->CreateComponent(context, AMFDisplayCapture, &(captureComp));
    if (result != AMF_OK) {
      BOOST_LOG(error) << "CreateComponent(AMFDisplayCapture) failed: "sv << result;
      return -1;
    }

    // Set parameters for non-blocking capture
    captureComp->SetProperty(AMF_DISPLAYCAPTURE_MONITOR_INDEX, output_index);
    captureComp->SetProperty(AMF_DISPLAYCAPTURE_FRAMERATE, AMFConstructRate(config.framerate, 1));
    captureComp->SetProperty(AMF_DISPLAYCAPTURE_MODE, AMF_DISPLAYCAPTURE_MODE_WAIT_FOR_PRESENT);

    // Initialize capture
    result = captureComp->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    if (result != AMF_OK) {
      BOOST_LOG(error) << "DisplayCapture::Init() failed: "sv << result;
      return -1;
    }

    captureComp->GetProperty(AMF_DISPLAYCAPTURE_FORMAT, &(capture_format));
    captureComp->GetProperty(AMF_DISPLAYCAPTURE_RESOLUTION, &(resolution));

    BOOST_LOG(info) << "Desktop resolution ["sv << resolution.width << 'x' << resolution.height << ']';
    BOOST_LOG(info) << "Desktop format ["sv << capture_format << ']';


    // Direct Capture allows fixed rate capture, but the pacing is quite bad. We prefer our own pacing instead.
    // self_pacing_capture = false;

    BOOST_LOG(info) << "Using AMD Direct Capture API for display capture"sv;

    return 0;
  }

}  // namespace platf::dxgi