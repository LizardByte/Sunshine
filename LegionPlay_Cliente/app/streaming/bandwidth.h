#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

/**
 * @brief The BandwidthTracker class tracks network bandwidth usage over a sliding time window (default 10s).
 *
 * Byte totals are grouped into fixed time interval buckets (default 250ms). This provides an element of smoothing
 * and deals well with spikes.
 *
 * GetAverageMbps() is calculated using the 25% most recent fully completed buckets. The default settings will
 * return an average of the past 2.5s of data, ignoring the in-progress bucket. Using only 2.5s of data for the
 * average provides a good balance of reactivity and smoothness.
 *
 * GetPeakMbps() returns the peak bandwidth seen during any one bucket interval across the full time window.
 *
 * All public methods are thread safe. A typical use case is calling AddBytes() in a data processing thread while
 * calling GetAverageMbps() from a UI thread.
 *
 * Example usage:
 * @code
 *   BandwidthTracker bwTracker(10, 250); // 10-second window, 250ms buckets
 *   bwTracker.AddBytes(64000);
 *   bwTracker.AddBytes(128000);
 *   double avg = bwTracker.GetAverageMbps();
 *   double peak = bwTracker.GetPeakMbps();
 * @endcode
 */
class BandwidthTracker
{
public:
    /**
     * @brief Constructs a new BandwidthTracker object.
     *
     * Initializes the tracker to maintain statistics over a sliding window of time.
     * The window is divided into buckets of fixed duration (bucketIntervalMs).
     *
     * @param windowSeconds The duration of the tracking window in seconds. Default is 10 seconds.
     * @param bucketIntervalMs The interval for each bucket in milliseconds. Default is 250 ms.
     */
    BandwidthTracker(std::uint32_t windowSeconds = 10, std::uint32_t bucketIntervalMs = 250);

    /**
     * @brief Record bytes that were received or sent.
     *
     * This method updates the corresponding bucket for the current time interval with the new data.
     * It is thread-safe. Bytes are associated with the bucket for "now" and it is not possible to
     * submit data for old buckets. This function should be called as needed at the time the bytes
     * were received. Callers should not maintain their own byte totals.
     *
     * @param bytes The number of bytes to add.
     */
    void AddBytes(size_t bytes);

    /**
     * @brief Computes and returns the average bandwidth in Mbps for the most recent 25% of buckets.
     *
     * @return The average bandwidth in megabits per second.
     */
    double GetAverageMbps();

    /**
     * @brief Returns the peak bandwidth in Mbps observed in any single bucket within the current window.
     *
     * This value represents the highest instantaneous throughput measured over one bucket interval.
     *
     * @return The peak bandwidth in megabits per second.
     */
    double GetPeakMbps();

    /**
     * @brief Retrieves the duration of the tracking window.
     *
     * This is useful when displaying the length of the peak, e.g.
     * @code
     *   printf("Bitrate: %.1f Mbps Peak (%us): %.1f\n",
     *          bw.getAverageMbps(), bw.GetWindowSeconds(), bw.getPeakMbps());
     * @endcode
     *
     * @return The window duration in seconds.
     */
    unsigned int GetWindowSeconds();

private:
    /**
     * @brief A structure representing a single time bucket.
     *
     * Each bucket holds the start time of the interval and the total number of bytes recorded during that interval.
     */
    struct Bucket {
        std::chrono::steady_clock::time_point start{}; ///< The start time of the bucket's interval.
        size_t bytes = 0;                              ///< The number of bytes recorded in this bucket.
    };

    const std::chrono::seconds windowSeconds;          ///< T he duration of the tracking window.
    const int bucketIntervalMs;                        ///< The duration of each bucket (in milliseconds).
    std::uint32_t bucketCount;                         ///< The total number of buckets covering the window.
    std::vector<Bucket> buckets;                       ///< Fixed-size circular buffer of buckets.
    std::mutex mtx;                                    ///< Mutex to ensure thread-safe access.

    bool isValid(const Bucket &bucket, std::chrono::steady_clock::time_point now) const;
    void updateBucket(size_t bytes, std::chrono::steady_clock::time_point now);
    double getBucketMbps(const Bucket &bucket) const;
};
