#pragma once

#include <cstdint>
#include "pico/time.h"

namespace rp2350 {

/**
 * @brief RP2350 timing policy using Pico SDK's time_us_64()
 * 
 * Uses the Pico SDK's microsecond timer for reliable timing.
 * This is simpler and more portable than the DWT cycle counter,
 * and provides sufficient precision for audio profiling.
 * 
 * The timer is 64-bit and won't wrap for ~584,000 years.
 */
struct Rp2350TimingPolicy {
    /**
     * @brief Get current time in microseconds
     */
    static uint64_t now() noexcept {
        return time_us_64();
    }
    
    static constexpr const char* unitName() noexcept {
        return "us";
    }
    
    /**
     * @brief Convert microseconds to microseconds (identity)
     */
    static constexpr uint64_t toMicroseconds(uint64_t us) noexcept {
        return us;
    }
};

} // namespace rp2350
