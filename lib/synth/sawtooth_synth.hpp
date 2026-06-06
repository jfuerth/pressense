#pragma once

#include <voice.hpp>
#include <wavetable_oscillator.hpp>
#include <adsr_envelope.hpp>
#include <biquad_filter.hpp>
#include <lfo.hpp>
#include <aftertouch_modulator.hpp>
#include <performance_timer.hpp>
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
 * - Lfo (vibrato): Pitch modulation
 * - Lfo (tremolo): Amplitude modulation
 * - Inline pitch bend processing
 * - Per-voice aftertouch modulation
 * 
 * Filter envelope modulates cutoff frequency relative to base cutoff (from timbre).
 * Filter envelope amount controls modulation depth.
 * Aftertouch can modulate filter cutoff, filter env amount, vibrato, and tremolo.
 */
class WavetableSynth : public Voice {
public:
    WavetableSynth(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate),
          oscillator_(sampleRate),
          filter_(sampleRate),
          ampEnvelope_(sampleRate),
          filterEnvelope_(sampleRate),
          vibratoLfo_(sampleRate),
          tremoloLfo_(sampleRate) {
        // Initialize filter with default settings
        filter_.setMode(BiquadFilter::Mode::LOWPASS);
        filter_.setQ(0.707f);  // Butterworth response
        
        // Initialize filter envelope with defaults
        filterEnvelope_.setAttackTime(0.005f);   // 5ms
        filterEnvelope_.setDecayTime(0.2f);      // 200ms
        filterEnvelope_.setSustainLevel(0.3f);
        filterEnvelope_.setReleaseTime(0.1f);    // 100ms
    }
    
    void trigger(float frequencyHz, float volume) override {
        baseFrequency_ = frequencyHz;
        volume_ = volume;
        aftertouch_ = 0.0f;  // Reset aftertouch on new note
        oscillator_.reset();
        filter_.reset();  // Clear filter state for clean attack
        vibratoLfo_.reset();
        tremoloLfo_.reset();
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
     * @brief Get amplitude envelope for parameter control
     */
    AdsrEnvelope& getAmpEnvelope() {
        return ampEnvelope_;
    }
    
    // ===== Vibrato (pitch LFO) =====
    
    void setVibratoRate(float rateHz) {
        vibratoLfo_.setRate(rateHz);
    }
    
    float getVibratoRate() const {
        return vibratoLfo_.getRate();
    }
    
    void setVibratoDepth(float semitones) {
        vibratoDepth_ = semitones;
    }
    
    float getVibratoDepth() const {
        return vibratoDepth_;
    }
    
    // ===== Tremolo (amplitude LFO) =====
    
    void setTremoloRate(float rateHz) {
        tremoloLfo_.setRate(rateHz);
    }
    
    float getTremoloRate() const {
        return tremoloLfo_.getRate();
    }
    
    void setTremoloDepth(float depth) {
        tremoloDepth_ = depth;
        if (tremoloDepth_ < 0.0f) tremoloDepth_ = 0.0f;
        if (tremoloDepth_ > 1.0f) tremoloDepth_ = 1.0f;
    }
    
    float getTremoloDepth() const {
        return tremoloDepth_;
    }
    
    // ===== Aftertouch =====
    
    /**
     * @brief Set per-voice aftertouch level
     * @param aftertouch Normalized aftertouch [0.0, 1.0]
     */
    void setAftertouch(float aftertouch) {
        aftertouch_ = aftertouch;
        if (aftertouch_ < 0.0f) aftertouch_ = 0.0f;
        if (aftertouch_ > 1.0f) aftertouch_ = 1.0f;
    }
    
    float getAftertouch() const {
        return aftertouch_;
    }
    
    // ===== Aftertouch Modulation Amounts =====
    // These control how much aftertouch affects each parameter
    // Range: [-1.0, 1.0], 0 = no effect
    
    void setBaseCutoffAtMod(float amount) { baseCutoff_atMod_ = amount; }
    float getBaseCutoffAtMod() const { return baseCutoff_atMod_; }
    
    void setFilterEnvAmountAtMod(float amount) { filterEnvAmount_atMod_ = amount; }
    float getFilterEnvAmountAtMod() const { return filterEnvAmount_atMod_; }
    
    void setVibratoDepthAtMod(float amount) { vibratoDepth_atMod_ = amount; }
    float getVibratoDepthAtMod() const { return vibratoDepth_atMod_; }
    
    void setTremoloDepthAtMod(float amount) { tremoloDepth_atMod_ = amount; }
    float getTremoloDepthAtMod() const { return tremoloDepth_atMod_; }
    
    /**
     * @brief Generate the next audio sample
     * 
     * @tparam TimingPolicy Policy class providing now() and unitName()
     * @tparam MaxSpans Maximum number of span names the timer can track
     * @param timer Lap timer for performance measurement. Span names use "synth:" prefix.
     * @return Audio sample in range [-1.0, 1.0]
     */
    template<typename TimingPolicy, size_t MaxSpans>
    float nextSample(features::LapTimer<TimingPolicy, MaxSpans>& timer) {
        if (!ampEnvelope_.isActive()) {
            timer.nextSpan("synth:inactive");
            return 0.0f;
        }
        
        // Calculate aftertouch-modulated parameters
        timer.nextSpan("synth:aftertouch");
        float effectiveCutoff = baseCutoff_ * (1.0f + aftertouch_ * baseCutoff_atMod_);
        float effectiveFilterEnvAmount = filterEnvAmount_ * (1.0f + aftertouch_ * filterEnvAmount_atMod_);
        // Vibrato and tremolo use additive modulation (can start from 0)
        float effectiveVibratoDepth = vibratoDepth_ + aftertouch_ * vibratoDepth_atMod_;
        float effectiveTremoloDepth = tremoloDepth_ + aftertouch_ * tremoloDepth_atMod_;
        
        // Clamp values to valid ranges
        if (effectiveCutoff < 20.0f) effectiveCutoff = 20.0f;
        if (effectiveCutoff > 20000.0f) effectiveCutoff = 20000.0f;
        if (effectiveFilterEnvAmount < 0.0f) effectiveFilterEnvAmount = 0.0f;
        if (effectiveFilterEnvAmount > 1.0f) effectiveFilterEnvAmount = 1.0f;
        if (effectiveVibratoDepth < 0.0f) effectiveVibratoDepth = 0.0f;
        if (effectiveTremoloDepth < 0.0f) effectiveTremoloDepth = 0.0f;
        if (effectiveTremoloDepth > 1.0f) effectiveTremoloDepth = 1.0f;
        
        // Calculate vibrato (pitch modulation)
        timer.nextSpan("synth:vibrato");
        vibratoLfo_.setDepth(effectiveVibratoDepth);
        float vibratoMod = vibratoLfo_.nextSample();  // Returns semitone offset
        
        // Calculate current frequency with pitch bend and vibrato
        timer.nextSpan("synth:pitch_bend");
        float semitoneShift = pitchBend_ * pitchBendRange_ + vibratoMod;
        float frequency = baseFrequency_ * std::pow(2.0f, semitoneShift / 12.0f);
        
        // Generate oscillator sample
        timer.nextSpan("synth:oscillator");
        float sample = oscillator_.nextSample(frequency);
        
        // Calculate filter cutoff with envelope modulation
        timer.nextSpan("synth:filter_env");
        float filterEnvLevel = filterEnvelope_.nextSample();
        float envModulation = filterEnvLevel * effectiveFilterEnvAmount;
        
        // Apply modulation (exponential, upward only)
        float modulatedCutoff = effectiveCutoff * (1.0f + envModulation * 9.0f);  // Up to 10x base cutoff
        
        timer.nextSpan("synth:filter");
        filter_.setCutoff(modulatedCutoff);
        
        // Apply filter
        sample = filter_.processSample(sample);
        
        // Calculate tremolo (amplitude modulation)
        timer.nextSpan("synth:tremolo");
        tremoloLfo_.setDepth(effectiveTremoloDepth);
        float tremoloMod = tremoloLfo_.nextSample();  // Returns [-depth, +depth]
        float tremoloMultiplier = 1.0f + tremoloMod;  // Range: [1-depth, 1+depth]
        
        // Apply amplitude envelope, volume, and tremolo
        timer.nextSpan("synth:amp_env");
        float ampEnvLevel = ampEnvelope_.nextSample();
        sample *= ampEnvLevel * volume_ * tremoloMultiplier;
        
        return sample;
    }

private:
    float sampleRate_;
    
    // Components (composed by value for cache locality and inlining)
    WavetableOscillator oscillator_;
    BiquadFilter filter_;
    AdsrEnvelope ampEnvelope_;
    AdsrEnvelope filterEnvelope_;
    Lfo vibratoLfo_;
    Lfo tremoloLfo_;
    
    // Voice parameters
    float baseFrequency_ = 440.0f;
    float volume_ = 1.0f;
    float pitchBend_ = 0.0f;
    float pitchBendRange_ = 2.0f;
    float baseCutoff_ = 1000.0f;    // Base filter cutoff (controlled by setter)
    float filterEnvAmount_ = 0.5f;  // 50% modulation by default
    
    // LFO parameters (depth stored separately for aftertouch modulation)
    float vibratoDepth_ = 0.0f;     // Semitones
    float tremoloDepth_ = 0.0f;     // 0-1 amplitude modulation
    
    // Per-voice aftertouch
    float aftertouch_ = 0.0f;       // Normalized [0, 1]
    
    // Aftertouch modulation amounts [-1, 1]
    float baseCutoff_atMod_ = 0.0f;
    float filterEnvAmount_atMod_ = 0.0f;
    float vibratoDepth_atMod_ = 0.0f;
    float tremoloDepth_atMod_ = 0.0f;
};

} // namespace synth

