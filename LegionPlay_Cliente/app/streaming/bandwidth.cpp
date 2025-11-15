#include "bandwidth.h"

using namespace std::chrono;

BandwidthTracker::BandwidthTracker(uint32_t windowSeconds, uint32_t bucketIntervalMs)
  : windowSeconds(seconds(windowSeconds)),
    bucketIntervalMs(bucketIntervalMs)
{
    if (bucketIntervalMs <= 0) {
        bucketIntervalMs = 250;
    }
    bucketCount = (windowSeconds * 1000) / bucketIntervalMs;
    buckets.resize(bucketCount);
}

// Add bytes recorded at the current time.
void BandwidthTracker::AddBytes(size_t bytes) {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = steady_clock::now();
    updateBucket(bytes, now);
}

// We don't want to average the entire window used for peak,
// so average only the newest 25% of complete buckets
double BandwidthTracker::GetAverageMbps() {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = steady_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch());
    int currentIndex = (ms.count() / bucketIntervalMs) % bucketCount;
    int maxBuckets = bucketCount / 4;
    size_t totalBytes = 0;
    steady_clock::time_point oldestBucket = now;

    // Sum bytes from 25% most recent buckets as long as they are completed
    for (int i = 0; i < maxBuckets; i++) {
        int idx = (currentIndex - i + bucketCount) % bucketCount;
        const Bucket &bucket = buckets[idx];
        if (isValid(bucket, now) && (now - bucket.start >= milliseconds(bucketIntervalMs))) {
            totalBytes += bucket.bytes;
            if (bucket.start < oldestBucket) {
                oldestBucket = bucket.start;
            }
        }
    }

    double elapsed = duration<double>(now - oldestBucket).count();
    if (elapsed <= 0.0) {
        return 0.0;
    }

    return totalBytes * 8.0 / 1000000.0 / elapsed;
}

double BandwidthTracker::GetPeakMbps() {
    std::lock_guard<std::mutex> lock(mtx);
    auto now = steady_clock::now();
    double peak = 0.0;
    for (const auto& bucket : buckets) {
        if (isValid(bucket, now)) {
            double throughput = getBucketMbps(bucket);
            if (throughput > peak) {
                peak = throughput;
            }
        }
    }
    return peak;
}

unsigned int BandwidthTracker::GetWindowSeconds() {
    return windowSeconds.count();
}

/// private methods

inline double BandwidthTracker::getBucketMbps(const Bucket &bucket) const {
    return bucket.bytes * 8.0 / 1000000.0 / (bucketIntervalMs / 1000.0);
}

// Check if a bucket's data is still valid (within the window)
inline bool BandwidthTracker::isValid(const Bucket &bucket, steady_clock::time_point now) const {
    return (now - bucket.start) <= windowSeconds;
}

void BandwidthTracker::updateBucket(size_t bytes, steady_clock::time_point now) {
    auto ms          = duration_cast<milliseconds>(now.time_since_epoch()).count();
    int bucketIndex  = (ms / bucketIntervalMs) % bucketCount;
    auto aligned_ms  = ms - (ms % bucketIntervalMs);
    auto bucketStart = steady_clock::time_point(milliseconds(aligned_ms));

    Bucket &bucket = buckets[bucketIndex];

    if (now - bucket.start > windowSeconds) {
        bucket.bytes = 0;
        bucket.start = bucketStart;
    }

    if (bucket.start != bucketStart) {
        bucket.bytes = bytes;
        bucket.start = bucketStart;
    }
    else {
        bucket.bytes += bytes;
    }
}
