#pragma once

#include <cstdint>
#include <xtensa/hal.h>
#include "esp_private/esp_clk.h"

namespace esp32 {

/**
 * @brief ESP32 high-resolution cycle counter timer
 * 
 * Uses the Xtensa CPU cycle counter (CCOUNT register) for minimal overhead
 * timing measurements. Provides approximately 4.17ns resolution at 240MHz.
 */
struct Esp32CycleTimer {
    /**
     * @brief Construct timer and capture actual CPU frequency
     */
    Esp32CycleTimer() : cyclesPerMicrosecond(esp_clk_cpu_freq() / 1000000) {}
    
    /**
     * @brief Get current CPU cycle count
     * @return uint32_t Current cycle count (wraps every ~18 seconds @ 240MHz)
     * 
     * Note: This is a 32-bit counter that wraps around. For measuring intervals
     * longer than a few seconds, use esp_timer_get_time() instead.
     */
    static inline uint32_t getCycles() {
        return xthal_get_ccount();
    }
    
    /**
     * @brief CPU frequency in cycles per microsecond (captured at construction)
     * Typical ESP32 runs at 80, 160, or 240 MHz
     */
    const uint32_t cyclesPerMicrosecond;
    
    /**
     * @brief Convert cycle count to microseconds
     * @param cycles Number of elapsed cycles
     * @return uint32_t Time in microseconds
     */
    inline uint32_t cyclesToMicroseconds(uint32_t cycles) const {
        return cycles / cyclesPerMicrosecond;
    }
    
    /**
     * @brief Estimated overhead of getCycles() call in cycles
     */
    static constexpr uint32_t overheadCycles = 2;
};

/**
 * @brief Convenience wrapper for measuring time intervals
 * @tparam Label Compile-time string for documentation (unused for now, may be used for telemetry later)
 */
template<const char* Label = nullptr>
class IntervalTimer {
public:
    IntervalTimer() : startCycles_(Esp32CycleTimer::getCycles()) {}
    
    /**
     * @brief Get elapsed cycles since construction or last call to elapsed()
     * @return uint32_t Elapsed cycles
     */
    inline uint32_t elapsed() {
        uint32_t now = Esp32CycleTimer::getCycles();
        uint32_t result = now - startCycles_;
        startCycles_ = now;
        return result;
    }
    
private:
    uint32_t startCycles_;
};

} // namespace esp32

// Define platform alias in esp32 namespace
namespace platform {
    using PlatformTimer = esp32::Esp32CycleTimer;
    template<const char* Label = nullptr>
    using IntervalTimer = esp32::IntervalTimer<Label>;
}
