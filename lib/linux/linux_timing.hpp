#pragma once

#include <cstdint>
#include <chrono>

namespace linux {

/**
 * @brief Portable high-resolution timer using std::chrono
 * 
 * Uses std::chrono::high_resolution_clock which typically provides nanosecond
 * resolution. Simpler and more stable than CPU cycle counters since it's not
 * affected by CPU frequency scaling.
 */
struct ChronoTimer {
    /**
     * @brief Get current time in nanoseconds since epoch
     * @return uint64_t Current time in nanoseconds
     */
    static inline uint64_t getCycles() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
    /**
     * @brief Conversion factor (nanoseconds to microseconds)
     */
    static constexpr uint32_t cyclesPerMicrosecond = 1000;
    
    /**
     * @brief Convert nanoseconds to microseconds
     * @param cycles Elapsed time in nanoseconds
     * @return uint32_t Time in microseconds
     */
    static inline uint32_t cyclesToMicroseconds(uint64_t cycles) {
        return static_cast<uint32_t>(cycles / cyclesPerMicrosecond);
    }
    
    /**
     * @brief Estimated overhead of getCycles() call in nanoseconds
     */
    static constexpr uint32_t overheadCycles = 50;
};

/**
 * @brief Convenience wrapper for measuring time intervals
 * @tparam Label Compile-time string for documentation (unused for now, may be used for telemetry later)
 */
template<const char* Label = nullptr>
class IntervalTimer {
public:
    IntervalTimer() : startCycles_(ChronoTimer::getCycles()) {}
    
    /**
     * @brief Get elapsed cycles since construction or last call to elapsed()
     * @return uint64_t Elapsed cycles (in nanoseconds)
     */
    inline uint64_t elapsed() {
        uint64_t now = ChronoTimer::getCycles();
        uint64_t result = now - startCycles_;
        startCycles_ = now;
        return result;
    }
    
private:
    uint64_t startCycles_;
};

} // namespace linux

// Define platform alias
namespace platform {
    using PlatformTimer = linux::ChronoTimer;
    template<const char* Label = nullptr>
    using IntervalTimer = linux::IntervalTimer<Label>;
}
