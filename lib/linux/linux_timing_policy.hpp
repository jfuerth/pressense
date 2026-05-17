#pragma once

// Enable POSIX extensions for clock_gettime
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <cstdint>
#include <time.h>

namespace linux_platform {

/**
 * @brief Linux timing policy using clock_gettime(CLOCK_MONOTONIC)
 * 
 * Uses the monotonic clock for stable timing unaffected by system time changes.
 * Returns nanoseconds for maximum precision without the complexity of TSC.
 * 
 * Note: CLOCK_MONOTONIC typically has ~1ns resolution on modern Linux kernels.
 */
struct LinuxTimingPolicy {
    static uint64_t now() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + 
               static_cast<uint64_t>(ts.tv_nsec);
    }
    
    static constexpr const char* unitName() noexcept {
        return "ns";
    }
    
    /**
     * @brief Convert nanoseconds to microseconds
     */
    static constexpr uint64_t toMicroseconds(uint64_t ns) noexcept {
        return ns / 1000;
    }
};

} // namespace linux_platform
