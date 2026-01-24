#ifndef SAWTOOTH_SYNTH_HPP
#define SAWTOOTH_SYNTH_HPP

#include <synth.hpp>
#include <wavetable_oscillator.hpp>
#include <adsr_envelope.hpp>
#include <biquad_filter.hpp>
#include <cmath>

namespace synth {

/**
 * @brief Modular wavetable synthesizer with filter and dual ADSR envelopes
 * 
 * Composed of:
 * - WavetableOscillator: Morphable waveform generation
 * - BiquadFilter: Resonant lowpass filter
 * - AdsrEnvelope (amplitude): Volume envelope
 * - AdsrEnvelope (filter): Filter cutoff modulation envelope
 * - Inline pitch bend processing
 * 
 * Filter envelope modulates cutoff frequency relative to base cutoff (from timbre).
 * Filter envelope amount controls modulation depth.
 */
class WavetableSynth : public midi::Synth {
public:
    WavetableSynth(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate),
          oscillator_(sampleRate),
          filter_(sampleRate),
          ampEnvelope_(sampleRate),
          filterEnvelope_(sampleRate) {
        // Initialize filter with default settings
        filter_.setMode(BiquadFilter::Mode::LOWPASS);
        filter_.setQ(0.707f);  // Butterworth response
        
        // Initialize filter envelope with faster attack/decay
        filterEnvelope_.setAttackTime(0.005f);   // 5ms
        filterEnvelope_.setDecayTime(0.2f);      // 200ms
        filterEnvelope_.setSustainLevel(0.3f);
        filterEnvelope_.setReleaseTime(0.1f);    // 100ms
    }
    
    void trigger(float frequencyHz, float volume) override {
        baseFrequency_ = frequencyHz;
        volume_ = volume;
        oscillator_.reset();
        filter_.reset();  // Clear filter state for clean attack
        ampEnvelope_.trigger();
        filterEnvelope_.trigger();
    }
    
    void release() override {
        ampEnvelope_.release();
        filterEnvelope_.release();
    }
    
    void setFrequency(float frequencyHz) override {
        baseFrequency_ = frequencyHz;
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
    }
    
    void setPitchBend(float bendAmount) override {
        pitchBend_ = bendAmount;
    }
    
    float getPitchBendRange() const override {
        return pitchBendRange_;
    }
    
    void setPitchBendRange(float semitones) override {
        pitchBendRange_ = semitones;
    }
    
    bool isActive() const override {
        return ampEnvelope_.isActive();
    }

    /**
     * @brief Get oscillator for direct parameter control from CC callbacks
     */
    WavetableOscillator& getOscillator() {
        return oscillator_;
    }
    
    /**
     * @brief Get filter for direct parameter control from CC callbacks
     */
    BiquadFilter& getFilter() {
        return filter_;
    }
    
    /**
     * @brief Set base filter cutoff (controlled by CC, before envelope modulation)
     */
    void setBaseCutoff(float cutoff) {
        baseCutoff_ = cutoff;
    }
    
    /**
     * @brief Get base filter cutoff (before envelope modulation)
     */
    float getBaseCutoff() const {
        return baseCutoff_;
    }
    
    /**
     * @brief Get filter envelope for parameter control from CC callbacks
     */
    AdsrEnvelope& getFilterEnvelope() {
        return filterEnvelope_;
    }
    
    /**
     * @brief Set filter envelope modulation amount
     * @param amount Modulation depth [0.0, 1.0]
     *               0.0 = no modulation, 1.0 = full range modulation
     */
    void setFilterEnvelopeAmount(float amount) {
        filterEnvAmount_ = amount;
        if (filterEnvAmount_ < 0.0f) filterEnvAmount_ = 0.0f;
        if (filterEnvAmount_ > 1.0f) filterEnvAmount_ = 1.0f;
    }
    
    /**
     * @brief Get current filter envelope amount
     */
    float getFilterEnvelopeAmount() const {
        return filterEnvAmount_;
    }
    
    /**
     * @brief Generate the next audio sample
     * @return Audio sample in range [-1.0, 1.0]
     */
    float nextSample() {
        if (!ampEnvelope_.isActive()) {
            return 0.0f;
        }
        
        // Calculate current frequency with pitch bend (inline)
        float semitoneShift = pitchBend_ * pitchBendRange_;
        float frequency = baseFrequency_ * std::pow(2.0f, semitoneShift / 12.0f);
        
        // Generate oscillator sample
        float sample = oscillator_.nextSample(frequency);
        
        // Calculate filter cutoff with envelope modulation
        // baseCutoff_ is controlled directly via setter method; envelope adds modulation on top
        
        // Modulate cutoff with filter envelope
        float filterEnvLevel = filterEnvelope_.nextSample();
        float envModulation = filterEnvLevel * filterEnvAmount_;
        
        // Apply modulation (exponential, upward only)
        float modulatedCutoff = baseCutoff_ * (1.0f + envModulation * 9.0f);  // Up to 10x base cutoff
        
        // PERF: This triggers coefficient recalculation every sample during envelope movement.
        // If polyphony is limited on embedded (ESP32/RP2350), consider quantizing cutoff changes
        // or rate-limiting updates (e.g., update filter every N samples, interpolate between).
        filter_.setCutoff(modulatedCutoff);
        
        // Apply filter
        sample = filter_.processSample(sample);
        
        // Apply amplitude envelope and volume
        float ampEnvLevel = ampEnvelope_.nextSample();
        sample *= ampEnvLevel * volume_;
        
        return sample;
    }

private:
    float sampleRate_;
    
    // Components (composed by value for cache locality and inlining)
    WavetableOscillator oscillator_;
    BiquadFilter filter_;
    AdsrEnvelope ampEnvelope_;
    AdsrEnvelope filterEnvelope_;
    
    // Voice parameters
    float baseFrequency_ = 440.0f;
    float volume_ = 1.0f;
    float pitchBend_ = 0.0f;
    float pitchBendRange_ = 2.0f;
    float baseCutoff_ = 1000.0f;    // Base filter cutoff (controlled by setter)
    float filterEnvAmount_ = 0.5f;  // 50% modulation by default
};

} // namespace synth

#endif // SAWTOOTH_SYNTH_HPP
