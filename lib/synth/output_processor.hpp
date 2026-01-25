#ifndef OUTPUT_PROCESSOR_HPP
#define OUTPUT_PROCESSOR_HPP

#include <cmath>
#include <vector>
#include <memory>
#include "biquad_filter.hpp"

namespace synth {

/**
 * @brief Base class for clipping/waveshaping algorithms
 * 
 * ClippingAlgorithms are pure strategy objects that implement the waveshaping
 * transfer function. They are stateless and lightweight.
 */
class ClippingAlgorithm {
public:
    virtual ~ClippingAlgorithm() = default;
    
    /**
     * @brief Apply clipping/waveshaping to an entire buffer in-place
     * @param buffer Mono audio buffer to process in-place
     * @param numFrames Number of frames in buffer
     * @param drive Amount to multiply each input sample by before clipping/waveshaping (controls intensity)
     */
    virtual void processBuffer(float* buffer, unsigned int numFrames, float drive) const = 0;
    
    /**
     * @brief Get algorithm name for UI display
     */
    virtual const char* getName() const = 0;
};

/**
 * @brief Soft clipper using hyperbolic tangent waveshaping
 * 
 * Provides smooth, musical saturation when driven hard.
 * tanh() naturally compresses to [-1, 1] range with smooth rolloff.
 * 
 * Transfer function: output = tanh(input * drive)
 * - Drive < 1.0: Reduces signal, increases headroom
 * - Drive = 1.0: Unity gain for small signals, soft limiting for large
 * - Drive > 1.0: Adds harmonic saturation/distortion
 */
class TanhClipping : public ClippingAlgorithm {
public:
    const char* getName() const override { return "TanhClipper"; }
    
    void processBuffer(float* buffer, unsigned int numFrames, float drive) const override {
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = std::tanh(buffer[i] * drive);
        }
    }
};

/**
 * @brief Wave folder - folds peaks back down for complex harmonic distortion
 * 
 * Instead of clipping, signals exceeding the threshold are "folded" back.
 * Creates rich, metallic harmonics - classic buchla/serge-style waveshaping.
 * 
 * Transfer function creates a triangle wave pattern from input signal.
 * Multiple folds can occur for large input signals, creating complex spectra.
 * Folds at fixed ±1.0 threshold; drive controls input gain.
 */
class WaveFoldClipping : public ClippingAlgorithm {
public:
    const char* getName() const override { return "WaveFolder"; }
    
    void processBuffer(float* buffer, unsigned int numFrames, float drive) const override {
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = wavefold(buffer[i] * drive);
        }
    }
    
private:
    static constexpr float FOLD_THRESHOLD = 1.0f;
    
    /**
     * @brief Fold waveform at ±1.0 boundaries
     * Creates triangle wave pattern for signals exceeding ±1.0
     */
    inline float wavefold(float x) const {
        // Normalize to [0, 1] range around threshold
        x = (x / FOLD_THRESHOLD) * 0.5f + 0.5f;
        
        // Apply modulo to create repeating pattern
        x = std::fmod(x, 2.0f);
        if (x < 0.0f) x += 2.0f;
        
        // Create triangle wave (fold back when > 1.0)
        if (x > 1.0f) x = 2.0f - x;
        
        // Denormalize back to [-1.0, 1.0]
        return (x * 2.0f - 1.0f) * FOLD_THRESHOLD;
    }
};

/**
 * @brief Soft wave folder - folds with smooth rollover for warmer distortion
 * 
 * Similar to WaveFoldClipping but uses tanh to smooth the fold points,
 * creating a warmer, less aggressive character with reduced high harmonics.
 * Combines the folding concept with soft saturation at the peaks.
 */
class SoftWaveFoldClipping : public ClippingAlgorithm {
public:
    const char* getName() const override { return "SoftWaveFolder"; }
    
    void processBuffer(float* buffer, unsigned int numFrames, float drive) const override {
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = wavefoldSoft(buffer[i] * drive);
        }
    }
    
private:
    static constexpr float FOLD_THRESHOLD = 1.0f;
    static constexpr float SOFTNESS = 3.0f;  // Controls smoothness at fold points
    
    /**
     * @brief Soft fold waveform at ±1.0 boundaries
     * Uses tanh to smooth the triangle wave peaks for warmer distortion
     */
    inline float wavefoldSoft(float x) const {
        // Normalize to [0, 1] range around threshold
        x = (x / FOLD_THRESHOLD) * 0.5f + 0.5f;
        
        // Apply modulo to create repeating pattern
        x = std::fmod(x, 2.0f);
        if (x < 0.0f) x += 2.0f;
        
        // Create triangle wave (fold back when > 1.0)
        if (x > 1.0f) x = 2.0f - x;
        
        // Apply soft saturation to smooth the peaks
        // Map [0, 1] to [-1, 1] for tanh, then back to [0, 1]
        x = (x * 2.0f - 1.0f);
        x = std::tanh(x * SOFTNESS) / std::tanh(SOFTNESS);
        x = x * 0.5f + 0.5f;
        
        // Denormalize back to [-1.0, 1.0]
        return (x * 2.0f - 1.0f) * FOLD_THRESHOLD;
    }
};

/**
 * @brief Output processor with switchable clipping algorithms and post-filter
 * 
 * Manages multiple clipping algorithms and allows runtime switching between them.
 * Applies a shared post-filter after clipping to smooth high-frequency harmonics.
 * 
 * Processing chain: Input → Clipping Algorithm → Post-Filter → Output
 */
class OutputProcessor {
public:
    OutputProcessor(float normalizedDrive = 0.5f, float sampleRate = 44100.0f) 
        : drive_(normalizedDrive),
          postFilter_(sampleRate),
          activeIndex_(0) {
        // Initialize post-filter as a low-pass at 10kHz with Q=0.707 (Butterworth)
        postFilter_.setMode(BiquadFilter::Mode::LOWPASS);
        postFilter_.setCutoff(10000.0f);
        postFilter_.setQ(0.707f);
        
        // Initialize all available clipping algorithms
        algorithms_.push_back(std::make_unique<TanhClipping>());
        algorithms_.push_back(std::make_unique<WaveFoldClipping>());
        algorithms_.push_back(std::make_unique<SoftWaveFoldClipping>());
    }
    
    /**
     * @brief Process audio buffer in-place
     * @param buffer Mono audio buffer to process
     * @param numFrames Number of frames in buffer
     * 
     * Applies active clipping algorithm then post-filter in two passes.
     * Two-pass approach reduces virtual function call overhead.
     */
    void processBuffer(float* buffer, unsigned int numFrames) {
        // Map normalized drive [0, 1] to exponential range [0.1, 10.0]
        // 0.0 → 0.1x, 0.5 → 1.0x (unity), 1.0 → 10.0x
        float actualDrive = 0.1f * std::pow(100.0f, drive_);
        
        // Pass 1: Apply clipping/waveshaping (one virtual call per buffer)
        algorithms_[activeIndex_]->processBuffer(buffer, numFrames, actualDrive);
        
        // Pass 2: Apply post-filter
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = postFilter_.processSample(buffer[i]);
        }
    }
    
    /**
     * @brief Cycle to next clipping algorithm
     * 
     * Resets the post-filter state when switching to avoid transients
     * from incompatible delay line values.
     */
    inline void nextMode() {
        activeIndex_ = (activeIndex_ + 1) % algorithms_.size();
        postFilter_.reset();  // Clear filter state on mode switch
    }
    
    /**
     * @brief Get current algorithm index
     */
    inline size_t getModeIndex() const {
        return activeIndex_;
    }
    
    /**
     * @brief Set algorithm by index
     * 
     * Resets the post-filter state when switching to avoid transients.
     */
    inline void setModeIndex(size_t index) {
        if (index < algorithms_.size() && index != activeIndex_) {
            activeIndex_ = index;
            postFilter_.reset();  // Clear filter state on mode switch
        }
    }
    
    /**
     * @brief Set drive amount (normalized 0.0 to 1.0)
     * @param drive Normalized drive parameter [0.0, 1.0]
     *              Internally scaled to exponential range [0.1, 10.0]
     */
    void setDrive(float drive) {
        if (drive < 0.0f) drive = 0.0f;
        if (drive > 1.0f) drive = 1.0f;
        drive_ = drive;
    }
    
    /**
     * @brief Get current normalized drive amount [0.0, 1.0]
     */
    float getDrive() const { return drive_; }
    
    /**
     * @brief Get current algorithm name for UI display
     */
    const char* getName() const {
        return algorithms_[activeIndex_]->getName();
    }
    
    /**
     * @brief Reset post-filter state
     */
    void reset() {
        postFilter_.reset();
    }
    
    /**
     * @brief Get reference to the post-filter for configuration
     */
    BiquadFilter& getPostFilter() { return postFilter_; }
    
private:
    float drive_;  // Normalized drive [0.0, 1.0]
    BiquadFilter postFilter_;  // Low-pass filter applied after clipping/shaping
    std::vector<std::unique_ptr<ClippingAlgorithm>> algorithms_;
    size_t activeIndex_;
};

} // namespace synth

#endif // OUTPUT_PROCESSOR_HPP
