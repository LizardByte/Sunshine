#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>

using QpcClock = std::chrono::steady_clock;
using QpcDuration = QpcClock::duration;

template<class Duration>
static inline uint64_t CToQpc(Duration duration) {
  return static_cast<uint64_t>(
    std::chrono::duration_cast<QpcDuration>(duration).count()
  );
}

static inline uint64_t TimePointToQpc(std::chrono::steady_clock::time_point time_point) {
  return static_cast<uint64_t>(
    std::chrono::duration_cast<QpcDuration>(time_point.time_since_epoch()).count()
  );
}

template<class Duration = std::chrono::microseconds>
static inline Duration QpcToC(uint64_t qpc) {
  return std::chrono::duration_cast<Duration>(
    QpcDuration(static_cast<QpcDuration::rep>(qpc))
  );
}

static inline uint64_t QpcFreq() {
  using period = QpcDuration::period;

  // steady_clock ticks per second.
  return static_cast<uint64_t>(period::den / period::num);
}

static inline uint64_t QpcNow() {
  return static_cast<uint64_t>(QpcClock::now().time_since_epoch().count());
}

static inline uint64_t UsToQpc(int64_t us) {
  auto duration = std::chrono::microseconds(us);
  return static_cast<uint64_t>(
    std::chrono::duration_cast<QpcDuration>(duration).count()
  );
}

static inline uint64_t QpcToUs(uint64_t qpc) {
  QpcDuration duration(static_cast<QpcDuration::rep>(qpc));

  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(duration).count()
  );
}

static inline double QpcToMsD(double qpc) {
  using period = QpcDuration::period;

  const double seconds =
    qpc * static_cast<double>(period::num) /
    static_cast<double>(period::den);

  return seconds * 1000.0;
}

static inline double QpcToMs(int64_t qpc) {
  return QpcToMsD(static_cast<double>(qpc));
}

static inline uint64_t MsToQpc(double ms) {
  const auto ns = static_cast<int64_t>(
    ms >= 0.0 ? ms * 1'000'000.0 + 0.5 : ms * 1'000'000.0 - 0.5
  );

  return static_cast<uint64_t>(
    std::chrono::duration_cast<QpcDuration>(
      std::chrono::nanoseconds(ns)
    )
      .count()
  );
}

static inline void YieldCPU() {
#if defined(__aarch64__) || defined(__arm64__)
  __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(__i386__)
  __asm__ __volatile__("pause");
#else
  // no-op fallback
#endif
}

// Sleep until approximately targetQpc, then busy-wait the rest.
// slackQpc is how early to stop sleeping.
static inline void SleepUntilQpc(uint64_t targetQpc, int64_t sleepSlackUs = 1000) {
  const uint64_t sleepSlackQpc = UsToQpc(sleepSlackUs);

  for (;;) {
    const uint64_t now = QpcNow();
    if (now >= targetQpc) {
      break;
    }

    const uint64_t remainQpc = targetQpc - now;
    if (remainQpc > sleepSlackQpc) {
      const uint64_t sleepQpc = remainQpc - sleepSlackQpc;
      const uint64_t totalNs = QpcToUs(sleepQpc) * 1000ULL;

      struct timespec ts;
      ts.tv_sec = (time_t) (totalNs / 1000000000ULL);
      ts.tv_nsec = (long) (totalNs % 1000000000ULL);

      if (ts.tv_sec > 0 || ts.tv_nsec > 0) {
        while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
      }
      continue;
    }

    // yield/busy-wait the last 1ms
    while (QpcNow() < targetQpc) {
      YieldCPU();
    }
    break;
  }
}
