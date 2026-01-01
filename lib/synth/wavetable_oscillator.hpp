#ifndef WAVETABLE_OSCILLATOR_HPP
#define WAVETABLE_OSCILLATOR_HPP

#include <cmath>

namespace synth {

/**
 * @brief Wavetable oscillator with runtime-morphable waveforms
 * 
 * Supports blending between sawtooth, triangle, and square waves.
 * Uses a cached wavetable for efficient sample generation.
 */
class WavetableOscillator {
public:
    static constexpr size_t TABLE_SIZE = 256;
    
    WavetableOscillator(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate) {
        updateWavetable(0.0f);  // Start with sawtooth
    }
    
    /**
     * @brief Update the wavetable based on the shape parameter
     * @param shape Waveform morph parameter: 0.0=sawtooth, 0.5=triangle, 1.0=square
     * 
     * This regenerates the wavetable and should be called when timbre changes,
     * not every sample.
     */
    inline void updateWavetable(float shape) {
        // Clamp shape to [0, 1]
        if (shape < 0.0f) shape = 0.0f;
        if (shape > 1.0f) shape = 1.0f;
        
        for (size_t i = 0; i < TABLE_SIZE; ++i) {
            float t = static_cast<float>(i) / TABLE_SIZE;
            
            // Generate base waveforms
            float saw = 2.0f * t - 1.0f;
            float triangle = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
            float square = (t < 0.5f) ? 1.0f : -1.0f;
            
            // Blend between waveforms based on shape parameter
            float sample;
            if (shape < 0.5f) {
                // Blend sawtooth → triangle
                float blend = shape * 2.0f;
                sample = saw * (1.0f - blend) + triangle * blend;
            } else {
                // Blend triangle → square
                float blend = (shape - 0.5f) * 2.0f;
                sample = triangle * (1.0f - blend) + square * blend;
            }
            
            wavetable_[i] = sample;
        }
    }
    
    /**
     * @brief Generate the next audio sample
     * @param frequency Frequency in Hz
     * @return Audio sample in range [-1.0, 1.0]
     */
    inline float nextSample(float frequency) {
        // Convert phase [0, 1) to table index
        float tablePos = phase_ * TABLE_SIZE;
        size_t index0 = static_cast<size_t>(tablePos) % TABLE_SIZE;
        size_t index1 = (index0 + 1) % TABLE_SIZE;
        
        // Linear interpolation between table entries
        float frac = tablePos - std::floor(tablePos);
        float sample = wavetable_[index0] * (1.0f - frac) + wavetable_[index1] * frac;
        
        // Update phase
        phase_ += frequency / sampleRate_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        }
        
        return sample;
    }
    
    /**
     * @brief Reset the oscillator phase (typically on note trigger)
     */
    inline void reset() {
        phase_ = 0.0f;
    }
    
    /**
     * @brief Get current phase
     */
    inline float getPhase() const {
        return phase_;
    }
    
    /**
     * @brief Set phase directly
     */
    inline void setPhase(float phase) {
        phase_ = phase;
        while (phase_ >= 1.0f) phase_ -= 1.0f;
        while (phase_ < 0.0f) phase_ += 1.0f;
    }

private:
    float wavetable_[TABLE_SIZE];
    float phase_ = 0.0f;
    float sampleRate_;
};

} // namespace synth

#endif // WAVETABLE_OSCILLATOR_HPP
