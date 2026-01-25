#ifndef OUTPUT_PROCESSOR_HPP
#define OUTPUT_PROCESSOR_HPP

#include <cmath>
#include <vector>
#include <memory>

namespace synth {

/**
 * @brief Base class for output processing effects (compression, limiting, distortion)
 * 
 * Output processors operate on mixed audio buffers to prevent clipping and add character.
 * All methods are inline for optimal performance in the audio generation loop.
 * Processing is done in-place to maximize cache locality and minimize memory usage.
 */
class OutputProcessor {
public:
    OutputProcessor(float initialDrive = 0.5f) : drive_(initialDrive) {}
    virtual ~OutputProcessor() = default;
    
    /**
     * @brief Process audio buffer in-place
     * @param buffer Mono audio buffer to process
     * @param numFrames Number of frames in buffer
     */
    virtual void processBuffer(float* buffer, unsigned int numFrames) = 0;
    
    /**
     * @brief Set drive amount (normalized 0.0 to 1.0)
     * @param drive Normalized drive parameter [0.0, 1.0]
     *              Processors internally scale this to their useful range
     */
    virtual void setDrive(float drive) {
        if (drive < 0.0f) drive = 0.0f;
        if (drive > 1.0f) drive = 1.0f;
        drive_ = drive;
    }
    
    /**
     * @brief Get current normalized drive amount [0.0, 1.0]
     */
    virtual float getDrive() const { return drive_; }
    
    /**
     * @brief Get processor name for UI display
     */
    virtual const char* getName() const = 0;
    
    /**
     * @brief Reset processor state (if stateful)
     */
    virtual void reset() {}
    
protected:
    float drive_;  // Normalized drive [0.0, 1.0]
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
class TanhClipper : public OutputProcessor {
public:
    TanhClipper(float normalizedDrive = 0.5f) : OutputProcessor(normalizedDrive) {}
    
    const char* getName() const override { return "TanhClipper"; }
    
    void processBuffer(float* buffer, unsigned int numFrames) override {
        // Map normalized drive [0, 1] to exponential range [0.1, 10.0]
        // 0.0 → 0.1x, 0.5 → 1.0x (unity), 1.0 → 10.0x
        float actualDrive = 0.1f * std::pow(100.0f, drive_);
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = std::tanh(buffer[i] * actualDrive);
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
class WaveFolderClipper : public OutputProcessor {
public:
    WaveFolderClipper(float normalizedDrive = 0.5f) : OutputProcessor(normalizedDrive) {}
    
    const char* getName() const override { return "WaveFolder"; }
    
    void processBuffer(float* buffer, unsigned int numFrames) override {
        // Map normalized drive [0, 1] to exponential range [0.1, 10.0]
        // 0.0 → 0.1x, 0.5 → 1.0x (unity), 1.0 → 10.0x
        float actualDrive = 0.1f * std::pow(100.0f, drive_);
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = wavefold(buffer[i] * actualDrive);
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
 * Similar to WaveFolderClipper but uses tanh to smooth the fold points,
 * creating a warmer, less aggressive character with reduced high harmonics.
 * Combines the folding concept with soft saturation at the peaks.
 */
class SoftWaveFolderClipper : public OutputProcessor {
public:
    SoftWaveFolderClipper(float normalizedDrive = 0.5f) : OutputProcessor(normalizedDrive) {}
    
    const char* getName() const override { return "SoftWaveFolder"; }
    
    void processBuffer(float* buffer, unsigned int numFrames) override {
        // Map normalized drive [0, 1] to exponential range [0.1, 10.0]
        // 0.0 → 0.1x, 0.5 → 1.0x (unity), 1.0 → 10.0x
        float actualDrive = 0.1f * std::pow(100.0f, drive_);
        for (unsigned int i = 0; i < numFrames; ++i) {
            buffer[i] = wavefoldSoft(buffer[i] * actualDrive);
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
 * @brief Master output processor with switchable modes
 * 
 * Manages multiple output processor types and allows runtime switching between them.
 * Available modes are allocated up front; switching is done by changing active index.
 */
class MasterOutputProcessor : public OutputProcessor {
public:
    MasterOutputProcessor(float normalizedDrive = 0.5f) 
        : OutputProcessor(normalizedDrive),
          activeIndex_(0) {
        // Initialize all available processors
        processors_.push_back(std::make_unique<TanhClipper>(normalizedDrive));
        processors_.push_back(std::make_unique<WaveFolderClipper>(normalizedDrive));
        processors_.push_back(std::make_unique<SoftWaveFolderClipper>(normalizedDrive));
    }
    
    /**
     * @brief Cycle to next processor mode
     */
    inline void nextMode() {
        activeIndex_ = (activeIndex_ + 1) % processors_.size();
    }
    
    /**
     * @brief Get current processor index
     */
    inline size_t getModeIndex() const {
        return activeIndex_;
    }
    
    /**
     * @brief Set processor by index
     */
    inline void setModeIndex(size_t index) {
        if (index < processors_.size()) {
            activeIndex_ = index;
        }
    }
    
    void setDrive(float drive) override {
        OutputProcessor::setDrive(drive);
        for (auto& processor : processors_) {
            processor->setDrive(drive);
        }
    }
    
    const char* getName() const override {
        return processors_[activeIndex_]->getName();
    }
    
    void processBuffer(float* buffer, unsigned int numFrames) override {
        processors_[activeIndex_]->processBuffer(buffer, numFrames);
    }
    
    void reset() override {
        for (auto& processor : processors_) {
            processor->reset();
        }
    }
    
private:
    std::vector<std::unique_ptr<OutputProcessor>> processors_;
    size_t activeIndex_;
};

} // namespace synth

#endif // OUTPUT_PROCESSOR_HPP
