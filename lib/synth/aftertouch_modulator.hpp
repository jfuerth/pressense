#pragma once

namespace synth {

/**
 * @brief Aftertouch-modulatable parameter with dual-knob control
 * 
 * Implements the dual-knob concept:
 * - Primary value: baseline parameter value
 * - Secondary value: aftertouch modulation amount and direction
 * 
 * The secondary knob (atMod) controls how aftertouch affects the parameter:
 * - atMod = 0: no effect (aftertouch doesn't change the value)
 * - atMod > 0: aftertouch increases the value (pressure = louder/brighter)
 * - atMod < 0: aftertouch decreases the value (pressure = softer/darker)
 * 
 * Two modulation modes are supported:
 * - Multiplicative: value = baseline * (1 + aftertouch * atMod)
 *   Use for parameters like filter cutoff where 0 baseline should stay 0
 * - Additive: value = baseline + aftertouch * atMod * range
 *   Use for parameters like vibrato depth that start at 0 but should increase
 */
struct ModulatedParam {
    float baseline = 0.0f;   ///< Base value set by primary knob
    float atMod = 0.0f;      ///< Aftertouch modulation amount [-1.0, 1.0]

    /**
     * @brief Apply multiplicative aftertouch modulation
     * 
     * Formula: baseline * (1 + aftertouch * atMod)
     * 
     * When atMod = 0: result = baseline (no change)
     * When atMod = 1, aftertouch = 1: result = baseline * 2 (doubled)
     * When atMod = -1, aftertouch = 1: result = 0 (fully suppressed)
     * 
     * @param aftertouch Normalized aftertouch value [0.0, 1.0]
     * @return Modulated parameter value
     */
    inline float modulateMultiplicative(float aftertouch) const {
        return baseline * (1.0f + aftertouch * atMod);
    }

    /**
     * @brief Apply additive aftertouch modulation
     * 
     * Formula: baseline + aftertouch * atMod * range
     * 
     * When atMod = 0: result = baseline (no change)
     * When atMod = 1, aftertouch = 1: result = baseline + range
     * When atMod = -1, aftertouch = 1: result = baseline - range
     * 
     * @param aftertouch Normalized aftertouch value [0.0, 1.0]
     * @param range Maximum additive modulation range
     * @return Modulated parameter value
     */
    inline float modulateAdditive(float aftertouch, float range) const {
        return baseline + aftertouch * atMod * range;
    }

    /**
     * @brief Clamp result to valid range after modulation
     * @param value The modulated value
     * @param minVal Minimum allowed value
     * @param maxVal Maximum allowed value
     * @return Clamped value
     */
    static inline float clamp(float value, float minVal, float maxVal) {
        if (value < minVal) return minVal;
        if (value > maxVal) return maxVal;
        return value;
    }
};

/**
 * @brief Convenience function to apply multiplicative modulation with clamping
 * @param param The modulated parameter
 * @param aftertouch Normalized aftertouch [0.0, 1.0]
 * @param minVal Minimum result value
 * @param maxVal Maximum result value
 * @return Clamped modulated value
 */
inline float applyModulation(const ModulatedParam& param, float aftertouch, 
                             float minVal = 0.0f, float maxVal = 1.0f) {
    float result = param.modulateMultiplicative(aftertouch);
    return ModulatedParam::clamp(result, minVal, maxVal);
}

/**
 * @brief Convenience function to apply additive modulation with clamping
 * @param param The modulated parameter
 * @param aftertouch Normalized aftertouch [0.0, 1.0]
 * @param range Maximum additive range
 * @param minVal Minimum result value
 * @param maxVal Maximum result value
 * @return Clamped modulated value
 */
inline float applyAdditiveModulation(const ModulatedParam& param, float aftertouch,
                                     float range, float minVal = 0.0f, float maxVal = 1.0f) {
    float result = param.modulateAdditive(aftertouch, range);
    return ModulatedParam::clamp(result, minVal, maxVal);
}

} // namespace synth
