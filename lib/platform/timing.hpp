#pragma once

#include <cstdint>

/**
 * @file timing.hpp
 * @brief Platform-agnostic high-resolution timing abstraction for performance measurement
 * 
 * This file provides a compile-time polymorphic interface for high-resolution timing
 * across different platforms. Each platform provides an optimized implementation:
 * - ESP32: CPU cycle counter (xthal_get_ccount) - ~4ns resolution @ 240MHz
 * - Linux: TSC on x86/x64 (__rdtsc) or std::chrono for fallback
 * 
 * Usage:
 *   #ifdef FEATURE_PERFORMANCE_TIMING
 *   uint64_t start = PlatformTimer::getCycles();
 *   // ... code to measure ...
 *   uint64_t elapsed = PlatformTimer::getCycles() - start;
 *   uint32_t microseconds = PlatformTimer::cyclesToMicroseconds(elapsed);
 *   #endif
 * 
 * The abstraction has zero runtime overhead - all calls inline to platform-specific
 * instructions with no virtual dispatch.
 */

#if defined(ESP_PLATFORM)

// ESP32: Use Xtensa CPU cycle counter
#include <xtensa/hal.h>

namespace platform {

struct Esp32Timer {
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
     * @brief CPU frequency in cycles per microsecond
     * Typical ESP32 runs at 240MHz = 240 cycles/µs
     */
    static constexpr uint32_t cyclesPerMicrosecond = 240;
    
    /**
     * @brief Convert cycle count to microseconds
     * @param cycles Number of elapsed cycles
     * @return uint32_t Time in microseconds
     */
    static inline uint32_t cyclesToMicroseconds(uint32_t cycles) {
        return cycles / cyclesPerMicrosecond;
    }
    
    /**
     * @brief Estimated overhead of getCycles() call in cycles
     */
    static constexpr uint32_t overheadCycles = 2;
};

using PlatformTimer = Esp32Timer;

} // namespace platform

#elif defined(__x86_64__) || defined(__i386__)

// Linux x86/x64: Use Time Stamp Counter (TSC)
#include <x86intrin.h>

namespace platform {

struct LinuxTscTimer {
    /**
     * @brief Get current Time Stamp Counter value
     * @return uint64_t Current TSC value
     * 
     * Note: TSC frequency varies by CPU. Modern CPUs typically run at 2-4GHz.
     * This implementation assumes a typical 2.4GHz CPU for microsecond conversion.
     * For precise timing, calibrate cyclesPerMicrosecond at runtime.
     */
    static inline uint64_t getCycles() {
        return __rdtsc();
    }
    
    /**
     * @brief Estimated CPU frequency in cycles per microsecond
     * Default assumes 2.4GHz CPU = 2400 cycles/µs
     * Note: This should ideally be calibrated at runtime for precision
     */
    static constexpr uint32_t cyclesPerMicrosecond = 2400;
    
    /**
     * @brief Convert cycle count to microseconds
     * @param cycles Number of elapsed cycles
     * @return uint32_t Time in microseconds (approximate)
     */
    static inline uint32_t cyclesToMicroseconds(uint64_t cycles) {
        return static_cast<uint32_t>(cycles / cyclesPerMicrosecond);
    }
    
    /**
     * @brief Estimated overhead of getCycles() call in cycles
     */
    static constexpr uint32_t overheadCycles = 20;
};

using PlatformTimer = LinuxTscTimer;

} // namespace platform

#else

// Fallback: Use C++11 std::chrono for other platforms
#include <chrono>

namespace platform {

struct ChronoTimer {
    /**
     * @brief Get current time in nanoseconds since epoch
     * @return uint64_t Current time in nanoseconds
     * 
     * Note: This uses std::chrono::high_resolution_clock which typically
     * provides nanosecond resolution but with higher overhead than
     * direct cycle counters.
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

using PlatformTimer = ChronoTimer;

} // namespace platform

#endif
