#pragma once

#include <cstdint>

namespace rp2350 {

/**
 * @brief Cortex-M33 DWT cycle counter
 *
 * The 1 us resolution of time_us_64() is far too coarse for anything inside the
 * audio loop: at 300 MHz one microsecond is 300 cycles, which is the same order
 * as a whole biquad update. This reads the core's own cycle counter instead,
 * giving single-cycle resolution.
 *
 * Registers are addressed directly rather than through CMSIS so this works
 * regardless of which core headers the SDK build happens to expose. The
 * addresses are fixed by the ARMv8-M architecture.
 *
 * CYCCNT is 32 bits and wraps every ~14 s at 300 MHz, so only use it for
 * intervals well under that; subtraction of unsigned values handles a single
 * wrap correctly.
 */
class CycleCounter {
public:
    /**
     * @brief Enable the cycle counter
     * @return true if the counter is actually running
     *
     * DWT is optional in the architecture and can be absent or locked out by a
     * debugger, so the result is checked rather than assumed.
     */
    static bool enable() {
        *demcr() |= TRCENA;
        *dwtCtrl() |= CYCCNTENA;
        // Confirm it ticks; if DWT is unimplemented the write reads back as 0.
        uint32_t first = *cyccnt();
        for (volatile int i = 0; i < 16; ++i) { }
        return *cyccnt() != first;
    }

    /**
     * @brief Current cycle count
     */
    static inline uint32_t now() {
        return *cyccnt();
    }

private:
    static constexpr uint32_t TRCENA = 1u << 24;
    static constexpr uint32_t CYCCNTENA = 1u << 0;

    static volatile uint32_t* demcr()   { return reinterpret_cast<volatile uint32_t*>(0xE000EDFCu); }
    static volatile uint32_t* dwtCtrl() { return reinterpret_cast<volatile uint32_t*>(0xE0001000u); }
    static volatile uint32_t* cyccnt()  { return reinterpret_cast<volatile uint32_t*>(0xE0001004u); }
};

} // namespace rp2350
