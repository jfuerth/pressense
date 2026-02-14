#pragma once

#include <cstdint>

namespace midi {

/**
 * @brief Platform-agnostic interface for capacitive touch key scanning
 * 
 * Implementations provide hardware-specific scanning logic and return
 * raw sensor readings. The MIDI keyboard controller converts these
 * readings into musical events.
 */
class KeyScanner {
public:
    virtual ~KeyScanner() = default;
    
    /**
     * @brief Get the current scan readings for all keys
     * @return Pointer to array of raw sensor values (higher = more capacitance)
     * @note The pointer remains valid until the next scan() call or object destruction
     * @note Array size is getKeyCount()
     */
    virtual const uint16_t* getScanReadings() const = 0;
    
    /**
     * @brief Get the number of keys supported by this scanner
     * @return Number of keys
     */
    virtual uint8_t getKeyCount() const = 0;
};

} // namespace midi
