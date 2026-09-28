#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <string>

static inline std::string cf_string_to_std_string(CFStringRef str) {
  if (!str) {
    return {};
  }

  CFIndex length = CFStringGetLength(str);
  CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;

  std::string result;
  result.resize((size_t) max_size);

  if (!CFStringGetCString(str, result.data(), max_size, kCFStringEncodingUTF8)) {
    return {};
  }

  result.resize(strlen(result.c_str()));
  return result;
}

static inline void log_cf_object(const char *label, CFTypeRef object) {
  if (!object) {
    BOOST_LOG(debug) << label << ": <null>";
    return;
  }

  CFStringRef description = CFCopyDescription(object);
  if (!description) {
    BOOST_LOG(debug) << label << ": <no description>";
    return;
  }

  BOOST_LOG(debug) << label << ": " << cf_string_to_std_string(description);
  CFRelease(description);
}

// like NSLog(@"%@", object)
static inline std::string cf_desc_to_std_string(CFTypeRef object) {
  CFStringRef description = CFCopyDescription(object);
  if (!description) {
    return "<no description>";
  }

  std::string out = cf_string_to_std_string(description);
  if (description) {
    CFRelease(description);
  }

  return out;
}

static inline std::string cm_time_to_std_string(CMTime time) {
  CFStringRef description = CMTimeCopyDescription(kCFAllocatorDefault, time);
  std::string out = cf_string_to_std_string(description);
  if (description) {
    CFRelease(description);
  }

  return out;
}

static inline void log_sample_buffer(CMSampleBufferRef sample_buffer) {
  if (!sample_buffer) {
    BOOST_LOG(debug) << "CMSampleBuffer: <null>";
    return;
  }

  log_cf_object("CMSampleBuffer", sample_buffer);
}

static inline std::string chrono_to_std_string(std::chrono::steady_clock::time_point tp) {
    using namespace std::chrono;

    static auto epoch = steady_clock::now();

    auto elapsed = duration_cast<microseconds>(tp - epoch);
    const auto h = duration_cast<hours>(elapsed);
    elapsed -= h;
    const auto m = duration_cast<minutes>(elapsed);
    elapsed -= m;
    const auto s = duration_cast<seconds>(elapsed);
    elapsed -= s;

    std::ostringstream os;
    os << std::setfill('0')
       << std::setw(2) << h.count() << ':'
       << std::setw(2) << m.count() << ':'
       << std::setw(2) << s.count() << '.'
       << std::setw(6) << elapsed.count();

    return os.str();
}
