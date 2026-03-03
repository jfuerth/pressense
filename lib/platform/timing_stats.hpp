#pragma once

#include <cstdint>
#include <limits>

namespace platform {

/**
 * @brief Simple timing statistics accumulator
 * 
 * Tracks min/max/sum for performance measurements.
 * Call reset() periodically to report current statistics.
 */
struct TimingStats {
    uint32_t sampleCount = 0;
    uint32_t minTime = std::numeric_limits<uint32_t>::max();
    uint32_t maxTime = 0;
    uint64_t totalTime = 0;  // Use 64-bit to avoid overflow
    
    /**
     * @brief Record a single timing measurement
     * @param time Time in microseconds
     */
    inline void record(uint32_t time) {
        sampleCount++;
        if (time < minTime) minTime = time;
        if (time > maxTime) maxTime = time;
        totalTime += time;
    }
    
    /**
     * @brief Get average time in microseconds
     */
    inline uint32_t getAverage() const {
        if (sampleCount == 0) return 0;
        return static_cast<uint32_t>(totalTime / sampleCount);
    }
    
    /**
     * @brief Reset all statistics
     */
    inline void reset() {
        sampleCount = 0;
        minTime = std::numeric_limits<uint32_t>::max();
        maxTime = 0;
        totalTime = 0;
    }
};

} // namespace platform
