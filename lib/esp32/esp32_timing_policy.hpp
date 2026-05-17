#pragma once

#include <cstdint>

// ESP-IDF includes for cycle counter and clock frequency
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_cpu.h>
#include <esp_clk_tree.h>
#else
#include <xtensa/core-macros.h>
#endif

namespace esp32 {

/**
 * @brief ESP32 timing policy using CPU cycle counter
 * 
 * Uses the Xtensa or RISC-V cycle counter for high-resolution timing.
 * 
 * At 240MHz (typical ESP32-S3), one cycle = ~4.17ns.
 * The underlying 32-bit hardware counter wraps every ~18 seconds at 240MHz,
 * but this policy tracks wraps to provide a non-wrapping 64-bit value.
 * 
 * IMPORTANT: now() must be called at least once every 18 seconds to detect
 * wraps correctly. This is easily satisfied by audio profiling (called every
 * few milliseconds). Also, this policy should only be used from a single core
 * since each ESP32 core has its own cycle counter.
 * 
 * Note: For accurate cycle-to-time conversion, divide by CPU frequency:
 *   time_us = cycles / (cpu_freq_mhz)
 */
struct Esp32TimingPolicy {
    /**
     * @brief Get current cycle count as non-wrapping 64-bit value
     * 
     * Tracks wraps of the underlying 32-bit counter by detecting when
     * the counter value decreases.
     */
    static uint64_t now() noexcept {
        static uint64_t highBits = 0;
        static uint32_t lastCount = 0;
        
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        uint32_t currentCount = esp_cpu_get_cycle_count();
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
        uint32_t currentCount = esp_cpu_get_ccount();
#else
        uint32_t currentCount = XTHAL_GET_CCOUNT();
#endif
        
        // Detect wrap: counter decreased means it wrapped
        if (currentCount < lastCount) {
            highBits += 0x1'0000'0000ULL;
        }
        lastCount = currentCount;
        
        return highBits | currentCount;
    }
    
    static constexpr const char* unitName() noexcept {
        return "cycles";
    }
    
    /**
     * @brief Convert cycles to microseconds
     * 
     * Uses the current CPU frequency for accurate conversion.
     * Caches the frequency on first call for efficiency.
     */
    static uint64_t toMicroseconds(uint64_t cycles) noexcept {
        // Cache CPU frequency in MHz (won't change at runtime)
        static uint32_t cpuFreqMhz = 0;
        if (cpuFreqMhz == 0) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
            cpuFreqMhz = esp_clk_cpu_freq() / 1'000'000;
#else
            cpuFreqMhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
#endif
        }
        return cycles / cpuFreqMhz;
    }
};

} // namespace esp32
