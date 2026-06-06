#pragma once

#include <cmath>

namespace synth {

// Define PI constant (not always available in <cmath>)
constexpr float LFO_PI = 3.14159265358979323846f;

/**
 * @brief Low Frequency Oscillator for modulation effects
 * 
 * Generates a sine wave oscillation for vibrato (pitch) and tremolo (amplitude).
 * Output range is [-depth, +depth] for bipolar modulation.
 * All methods are inline for optimal performance in the audio generation loop.
 */
class Lfo {
public:
    Lfo(float sampleRate = 44100.0f)
        : sampleRate_(sampleRate) {
        updateIncrement();
    }

    /**
     * @brief Set LFO rate (frequency)
     * @param rateHz Oscillation frequency in Hz (typically 0.1 - 20 Hz)
     */
    inline void setRate(float rateHz) {
        rate_ = rateHz;
        if (rate_ < 0.0f) rate_ = 0.0f;
        if (rate_ > 50.0f) rate_ = 50.0f;  // Reasonable upper limit
        updateIncrement();
    }

    /**
     * @brief Get current LFO rate
     */
    inline float getRate() const {
        return rate_;
    }

    /**
     * @brief Set LFO depth (modulation amount)
     * @param depth Modulation depth [0.0, 1.0]
     *              For vibrato: depth in semitones
     *              For tremolo: depth as amplitude multiplier
     */
    inline void setDepth(float depth) {
        depth_ = depth;
        if (depth_ < 0.0f) depth_ = 0.0f;
        if (depth_ > 1.0f) depth_ = 1.0f;
    }

    /**
     * @brief Get current LFO depth
     */
    inline float getDepth() const {
        return depth_;
    }

    /**
     * @brief Reset LFO phase to zero
     */
    inline void reset() {
        phase_ = 0.0f;
    }

    /**
     * @brief Generate the next LFO sample
     * @return Modulation value in range [-depth, +depth]
     */
    inline float nextSample() {
        // Generate sine wave
        float value = std::sin(phase_ * 2.0f * LFO_PI) * depth_;
        
        // Advance phase
        phase_ += phaseIncrement_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        }
        
        return value;
    }

    /**
     * @brief Get current modulation value for unipolar applications
     * @return Modulation value in range [0, depth] (offset sine)
     */
    inline float nextSampleUnipolar() {
        float bipolar = nextSample();
        return (bipolar + depth_) * 0.5f;
    }

private:
    inline void updateIncrement() {
        phaseIncrement_ = rate_ / sampleRate_;
    }

    float sampleRate_;
    float rate_ = 5.0f;      // Default 5 Hz
    float depth_ = 0.0f;     // Default off
    float phase_ = 0.0f;     // Current phase [0, 1)
    float phaseIncrement_ = 0.0f;
};

} // namespace synth
