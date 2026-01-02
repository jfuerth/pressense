#ifndef BIQUAD_FILTER_HPP
#define BIQUAD_FILTER_HPP

#include <cmath>

namespace synth {

/**
 * @brief Digital biquad filter (2nd order IIR filter)
 * 
 * Implements lowpass, highpass, bandpass, notch, and allpass filters.
 * Uses Direct Form II Transposed structure for better numerical stability.
 * Coefficients are calculated using Robert Bristow-Johnson's formulas.
 * 
 * All methods are inline for optimal performance in the audio generation loop.
 * Coefficient calculation is lazy - only recalculated when parameters change.
 */
class BiquadFilter {
public:
    enum class Mode {
        LOWPASS,
        HIGHPASS,
        BANDPASS,
        NOTCH,
        ALLPASS
    };
    
    BiquadFilter(float sampleRate = 44100.0f)
        : sampleRate_(sampleRate) {
        reset();
        updateCoefficients();
    }
    
    /**
     * @brief Set filter mode
     */
    inline void setMode(Mode mode) {
        if (mode_ != mode) {
            mode_ = mode;
            coeffsDirty_ = true;
        }
    }
    
    /**
     * @brief Set cutoff frequency in Hz
     * @param frequencyHz Cutoff frequency (clamped to 20Hz - Nyquist)
     */
    inline void setCutoff(float frequencyHz) {
        // Clamp to valid range
        float nyquist = sampleRate_ * 0.5f;
        if (frequencyHz < 20.0f) frequencyHz = 20.0f;
        if (frequencyHz > nyquist * 0.99f) frequencyHz = nyquist * 0.99f;
        
        if (cutoffHz_ != frequencyHz) {
            cutoffHz_ = frequencyHz;
            coeffsDirty_ = true;
        }
    }
    
    /**
     * @brief Set Q factor (resonance/bandwidth)
     * @param q Q factor (typically 0.5 - 10.0)
     *          0.707 = Butterworth (maximally flat)
     *          Higher values = more resonance/narrower bandwidth
     */
    inline void setQ(float q) {
        // Clamp to reasonable range
        if (q < 0.1f) q = 0.1f;
        if (q > 20.0f) q = 20.0f;
        
        if (q_ != q) {
            q_ = q;
            coeffsDirty_ = true;
        }
    }
    
    /**
     * @brief Process a single sample through the filter
     * @param input Input sample
     * @return Filtered output sample
     */
    inline float processSample(float input) {
        // Update coefficients if parameters changed
        if (coeffsDirty_) {
            updateCoefficients();
        }
        
        // Direct Form II Transposed implementation
        float output = b0_ * input + z1_;
        z1_ = b1_ * input - a1_ * output + z2_;
        z2_ = b2_ * input - a2_ * output;
        
        return output;
    }
    
    /**
     * @brief Reset filter state (clear delay elements)
     * Call this when starting a new note to avoid clicks.
     */
    inline void reset() {
        z1_ = 0.0f;
        z2_ = 0.0f;
    }
    
    /**
     * @brief Get current cutoff frequency
     */
    inline float getCutoff() const { return cutoffHz_; }
    
    /**
     * @brief Get current Q factor
     */
    inline float getQ() const { return q_; }
    
    /**
     * @brief Get current mode
     */
    inline Mode getMode() const { return mode_; }

private:
    /**
     * @brief Calculate biquad coefficients using RBJ formulas
     * Only called when parameters change (lazy evaluation).
     */
    inline void updateCoefficients() {
        const float PI = 3.14159265358979323846f;
        
        // Calculate normalized frequency (omega)
        float w0 = 2.0f * PI * cutoffHz_ / sampleRate_;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q_);
        
        // Coefficient calculation based on mode
        float a0, a1, a2, b0, b1, b2;
        
        switch (mode_) {
            case Mode::LOWPASS:
                b0 = (1.0f - cosw0) / 2.0f;
                b1 = 1.0f - cosw0;
                b2 = (1.0f - cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;
                
            case Mode::HIGHPASS:
                b0 = (1.0f + cosw0) / 2.0f;
                b1 = -(1.0f + cosw0);
                b2 = (1.0f + cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;
                
            case Mode::BANDPASS:
                b0 = alpha;
                b1 = 0.0f;
                b2 = -alpha;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;
                
            case Mode::NOTCH:
                b0 = 1.0f;
                b1 = -2.0f * cosw0;
                b2 = 1.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;
                
            case Mode::ALLPASS:
                b0 = 1.0f - alpha;
                b1 = -2.0f * cosw0;
                b2 = 1.0f + alpha;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;
        }
        
        // Normalize by a0
        b0_ = b0 / a0;
        b1_ = b1 / a0;
        b2_ = b2 / a0;
        a1_ = a1 / a0;
        a2_ = a2 / a0;
        
        coeffsDirty_ = false;
    }
    
    // Sample rate
    float sampleRate_;
    
    // Filter parameters
    Mode mode_ = Mode::LOWPASS;
    float cutoffHz_ = 1000.0f;
    float q_ = 0.707f;  // Butterworth response
    
    // Biquad coefficients (normalized)
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    
    // Filter state (Direct Form II Transposed)
    float z1_ = 0.0f;
    float z2_ = 0.0f;
    
    // Dirty flag for lazy coefficient update
    bool coeffsDirty_ = true;
};

} // namespace synth

#endif // BIQUAD_FILTER_HPP
