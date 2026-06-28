#pragma once

namespace synth {

/**
 * @brief Band-limited oscillator with runtime-morphable waveforms
 *
 * Morphs between sawtooth, triangle, and square waves. The waveform is
 * generated analytically from the phase accumulator and band-limited with
 * PolyBLEP, which suppresses the aliasing that a raw saw/square (or a
 * wavetable lookup of one) produces at its discontinuities.
 *
 * Note: the class name is retained for API compatibility; it no longer uses
 * a literal wavetable.
 */
class WavetableOscillator {
public:
    WavetableOscillator(float sampleRate = 44100.0f)
        : sampleRate_(sampleRate) {
        updateWavetable(0.0f);  // Start with sawtooth
    }

    /**
     * @brief Update the waveform morph weights from the shape parameter
     * @param shape Waveform morph parameter: 0.0=sawtooth, 0.5=triangle, 1.0=square
     *
     * Cheap to call; recomputes only the three blend weights. Call it when the
     * timbre changes rather than every sample.
     */
    inline void updateWavetable(float shape) {
        // Clamp shape to [0, 1]
        if (shape < 0.0f) shape = 0.0f;
        if (shape > 1.0f) shape = 1.0f;

        shape_ = shape;  // Store for later retrieval

        // Precompute the saw/triangle/square blend weights.
        if (shape < 0.5f) {
            // Blend sawtooth → triangle
            float blend = shape * 2.0f;
            wSaw_ = 1.0f - blend;
            wTriangle_ = blend;
            wSquare_ = 0.0f;
        } else {
            // Blend triangle → square
            float blend = (shape - 0.5f) * 2.0f;
            wSaw_ = 0.0f;
            wTriangle_ = 1.0f - blend;
            wSquare_ = blend;
        }
    }

    /**
     * @brief Generate the next audio sample
     * @param frequency Frequency in Hz
     * @return Audio sample in range [-1.0, 1.0]
     */
    inline float nextSample(float frequency) {
        const float dt = frequency / sampleRate_;  // phase increment per sample
        const float t = phase_;

        // Naive (pre-band-limiting) morphed waveform value at the current phase.
        const float saw = 2.0f * t - 1.0f;
        const float triangle = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        const float square = (t < 0.5f) ? 1.0f : -1.0f;
        float sample = wSaw_ * saw + wTriangle_ * triangle + wSquare_ * square;

        // PolyBLEP corrections for the value discontinuities (steps).
        // At the phase wrap (t=0): the saw resets by -2, the square rises by +2.
        sample += (wSquare_ - wSaw_) * polyBlep(t, dt);
        // At the square's falling edge (t=0.5): the square drops by -2.
        float tHalf = t + 0.5f;
        if (tHalf >= 1.0f) tHalf -= 1.0f;
        sample -= wSquare_ * polyBlep(tHalf, dt);
        // The triangle's slope discontinuities are left uncorrected: they would
        // need PolyBLAMP and alias far less than a hard step in value.

        // Update phase
        phase_ += dt;
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
    
    /**
     * @brief Get current waveform shape
     */
    inline float getShape() const {
        return shape_;
    }

private:
    /**
     * @brief PolyBLEP residual for a unit-amplitude step discontinuity
     * @param t  Phase in [0, 1) measured from the discontinuity's location
     * @param dt Phase increment per sample (frequency / sampleRate)
     *
     * Returns a correction that, added to a naive waveform with a step of
     * height 2 at t=0, smooths the one-sample neighbourhood of the step and
     * removes most of its aliasing. Zero outside the [0, dt) and (1-dt, 1)
     * windows around the discontinuity.
     */
    static inline float polyBlep(float t, float dt) {
        if (dt <= 0.0f) return 0.0f;
        if (t < dt) {
            // Just after the discontinuity.
            t /= dt;
            return t + t - t * t - 1.0f;
        } else if (t > 1.0f - dt) {
            // Just before the next discontinuity (wraps to the same edge).
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    float phase_ = 0.0f;
    float sampleRate_;
    float shape_ = 0.0f;     // Current waveform shape
    float wSaw_ = 1.0f;      // Morph weight: sawtooth component
    float wTriangle_ = 0.0f; // Morph weight: triangle component
    float wSquare_ = 0.0f;   // Morph weight: square component
};

} // namespace synth

