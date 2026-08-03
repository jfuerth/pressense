#pragma once

#include <math.h>  // Use C math.h for single-precision cosf/sinf
#include "fast_math.hpp"

namespace synth {

/**
 * @brief Digital biquad filter (2nd order IIR filter)
 * 
 * Implements lowpass, highpass, bandpass, notch, and allpass filters.
 * Uses Direct Form II Transposed structure for better numerical stability.
 * Coefficients are calculated using Robert Bristow-Johnson's formulas.
 * 
 * All methods are inline for optimal performance in the audio generation loop.
 * Coefficients are recalculated immediately when parameters change.
 */
class BiquadFilter {
public:
#ifdef BIQUAD_PROFILE_UPDATES
    // Counts actual coefficient recomputes, so a profiling build can tell how
    // often the change thresholds above really fire. Not compiled into normal
    // builds; see src/main_rp2350_bench.cpp.
    static inline uint32_t updateCount = 0;
#endif

    enum class Mode {
        LOWPASS,
        HIGHPASS,
        BANDPASS,
        NOTCH,
        ALLPASS,
        __COUNT
    };
    
    static Mode nextMode(Mode current) {
        return static_cast<Mode>((static_cast<int>(current) + 1) % static_cast<int>(Mode::__COUNT));
    }

    BiquadFilter(float sampleRate = 44100.0f)
        : sampleRate_(sampleRate) {
        reset();
        updateCoefficients();
        updateNoRecalcBand();
    }
    
    /**
     * @brief Set filter mode
     */
    inline void setMode(Mode mode) {
        if (mode_ != mode) {
            mode_ = mode;
            // Reset filter state when changing modes to avoid transients
            // from incompatible delay line values
            reset();
            updateCoefficients();
        }
    }
    
    /**
     * @brief Set cutoff frequency in Hz
     * @param frequencyHz Cutoff frequency (clamped to 20Hz - Nyquist)
     * 
     * Optimized to skip coefficient recalculation for insignificant changes.
     * Only recalculates when change exceeds 1% OR 10Hz threshold.
     * This dramatically reduces CPU usage with envelope modulation while
     * remaining perceptually transparent (threshold is well below JND).
     */
    inline void setCutoff(float frequencyHz) {
        // Fast reject. This is called once per sample per voice and almost always
        // decides to do nothing: measured on hardware, an actual recompute happens
        // for 11 of 256 samples during a filter envelope's attack/decay and 0 of
        // 256 at sustain. The band below is precomputed whenever the coefficients
        // change, so the overwhelmingly common no-op path is two compares instead
        // of a clamp, a fabsf and a divide (50 cycles -> ~4).
        if (frequencyHz >= noRecalcLo_ && frequencyHz <= noRecalcHi_) {
            return;
        }

        // Clamp to valid range
        float nyquist = sampleRate_ * 0.5f;
        if (frequencyHz < 20.0f) frequencyHz = 20.0f;
        if (frequencyHz > nyquist * 0.99f) frequencyHz = nyquist * 0.99f;

        // Only recalculate coefficients if change is significant
        // This avoids expensive trig calculations on every envelope step
        float absDelta = fabsf(frequencyHz - cutoffHz_);
        float relDelta = absDelta / cutoffHz_;

        // Recalculate if: >3% relative change OR >20Hz absolute change
        // 3% is still below the Just Noticeable Difference for filter cutoff (~5%)
        // This aggressive threshold dramatically reduces CPU load with envelope modulation
        if (relDelta > 0.03f || absDelta > 20.0f) {
            cutoffHz_ = frequencyHz;
            updateCoefficients();
            updateNoRecalcBand();
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
            updateCoefficients();
        }
    }
    
    /**
     * @brief Process a single sample through the filter
     * @param input Input sample
     * @return Filtered output sample
     */
    inline float processSample(float input) {
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
     * Called immediately when parameters change (eager evaluation).
     */
    inline void updateCoefficients() {
#ifdef BIQUAD_PROFILE_UPDATES
        ++updateCount;
#endif
        // Normalized frequency as a fraction of the sample rate, which is what the
        // fast trig functions want: sin(w0) = sin(2*pi*normalized).
        //
        // cosf and sinf cost ~95 and ~82 cycles on the Cortex-M33, and this runs
        // whenever a modulated cutoff or resonance moves far enough to matter - up
        // to every sample when aftertouch is sweeping the filter. The
        // approximations are within 2e-6, which is nowhere near enough to shift
        // the filter's response audibly.
        float normalized = cutoffHz_ / sampleRate_;
        float cosw0 = fastCos2Pi(normalized);
        float sinw0 = fastSin2Pi(normalized);
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
                
            default:  // Handles Mode::__COUNT and any other invalid values
                // Use lowpass as safe default
                b0 = (1.0f - cosw0) / 2.0f;
                b1 = 1.0f - cosw0;
                b2 = (1.0f - cosw0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cosw0;
                a2 = 1.0f - alpha;
                break;
        }
        
        // Normalize by a0. One reciprocal and five multiplies rather than five
        // divides: a float divide is ~13 cycles against ~1 for a multiply, and
        // this runs on every coefficient update, which with a modulated cutoff can
        // be most samples. a0 = 1 + alpha with alpha > 0, so it is never zero.
        const float invA0 = 1.0f / a0;
        b0_ = b0 * invA0;
        b1_ = b1 * invA0;
        b2_ = b2 * invA0;
        a1_ = a1 * invA0;
        a2_ = a2 * invA0;
    }
    
    /**
     * @brief Recompute the window of cutoffs that setCutoff() can ignore
     *
     * Mirrors the threshold test in setCutoff(): a request is insignificant when
     * it is within 3% *and* within 20 Hz of the current cutoff, so the half-width
     * is the smaller of the two.
     */
    inline void updateNoRecalcBand() {
        float halfWidth = 0.03f * cutoffHz_;
        if (halfWidth > 20.0f) halfWidth = 20.0f;
        noRecalcLo_ = cutoffHz_ - halfWidth;
        noRecalcHi_ = cutoffHz_ + halfWidth;
    }

    // Sample rate
    float sampleRate_;
    
    // Filter parameters
    Mode mode_ = Mode::LOWPASS;
    float cutoffHz_ = 1000.0f;
    float q_ = 0.707f;  // Butterworth response

    // Cutoff requests inside this band are too small to be worth recomputing
    // coefficients for; kept in sync by updateNoRecalcBand().
    float noRecalcLo_ = 1000.0f;
    float noRecalcHi_ = 1000.0f;
    
    // Biquad coefficients (normalized)
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    
    // Filter state (Direct Form II Transposed)
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};

} // namespace synth
