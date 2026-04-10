#pragma once

/**
 * @file timing.hpp
 * @brief Platform-agnostic high-resolution timing abstraction for performance measurement
 * 
 * This header provides a unified interface for high-resolution timing across platforms
 * by including the appropriate platform-specific implementation. Each platform defines
 * its own platform::PlatformTimer and platform::IntervalTimer type aliases.
 * 
 * Platform implementations live in separate files:
 * - ESP32: lib/esp32/esp32_timing.hpp
 * - Linux: lib/linux/linux_timing.hpp
 * 
 * Usage:
 *   #ifdef FEATURE_PERFORMANCE_TIMING
 *   platform::PlatformTimer timer;  // Captures CPU frequency on construction
 *   uint64_t start = platform::PlatformTimer::getCycles();
 *   // ... code to measure ...
 *   uint64_t elapsed = platform::PlatformTimer::getCycles() - start;
 *   uint32_t microseconds = timer.cyclesToMicroseconds(elapsed);
 *   #endif
 * 
 * Convenience wrapper for simpler timing:
 *   #ifdef FEATURE_PERFORMANCE_TIMING
 *   platform::IntervalTimer<> timer;  // Label is optional compile-time string
 *   // ... code to measure ...
 *   uint32_t cycles = timer.elapsed();  // Returns elapsed since construction or last call
 *   #endif
 * 
 * The abstraction has zero runtime overhead - all calls inline to platform-specific
 * instructions with no virtual dispatch.
 */

#if defined(ESP_PLATFORM)
    #include <esp32_timing.hpp>
    // platform::PlatformTimer and platform::IntervalTimer defined in esp32_timing.hpp
#elif defined(__linux__)
    #include <linux_timing.hpp>
    // platform::PlatformTimer and platform::IntervalTimer defined in linux_timing.hpp
#else
    #error "Unknown platform: timing.hpp requires ESP_PLATFORM or __linux__ to be defined. Add support for your platform in lib/<platform>/"
#endif
