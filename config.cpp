#include "config.h"

#define CA_DIR SUNSHINE_ASSETS_DIR "/demoCA"
#define PRIVATE_KEY_FILE CA_DIR    "/cakey.pem"
#define CERTIFICATE_FILE CA_DIR    "/cacert.pem"


namespace config {
using namespace std::literals;
video_t video {
  16, // max_b_frames
  24, // gop_size
  35, // crf
};

stream_t stream {
  2s // ping_timeout
};

nvhttp_t nvhttp {
  PRIVATE_KEY_FILE,
  CERTIFICATE_FILE,

  "03904e64-51da-4fb3-9afd-a9f7ff70fea4", // unique_id
  "devices.xml" // file_devices
};
}
