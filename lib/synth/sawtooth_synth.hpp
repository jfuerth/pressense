#ifndef SAWTOOTH_SYNTH_HPP
#define SAWTOOTH_SYNTH_HPP

#include <synth.hpp>
#include <wavetable_oscillator.hpp>
#include <adsr_envelope.hpp>
#include <biquad_filter.hpp>
#include <cmath>

namespace synth {

/**
 * @brief Modular wavetable synthesizer with filter and ADSR envelope
 * 
 * Composed of:
 * - WavetableOscillator: Morphable waveform generation
 * - BiquadFilter: Resonant lowpass filter
 * - AdsrEnvelope: Amplitude envelope
 * - Inline pitch bend processing
 * 
 * TODO: Add more filter controls via CC messages (sliders/knobs)
 *       - Filter Q/resonance
 *       - Filter mode selection
 *       - Filter envelope amount
 *       - Second filter stage for steeper rolloff
 */
class WavetableSynth : public midi::Synth {
public:
    WavetableSynth(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate),
          oscillator_(sampleRate),
          filter_(sampleRate),
          envelope_(sampleRate) {
        // Initialize filter with default settings
        filter_.setMode(BiquadFilter::Mode::LOWPASS);
        filter_.setQ(0.707f);  // Butterworth response
    }
    
    void trigger(float frequencyHz, float volume) override {
        baseFrequency_ = frequencyHz;
        volume_ = volume;
        oscillator_.reset();
        filter_.reset();  // Clear filter state for clean attack
        envelope_.trigger();
    }
    
    void release() override {
        envelope_.release();
    }
    
    void setFrequency(float frequencyHz) override {
        baseFrequency_ = frequencyHz;
    }
    
    void setTimbre(float timbre) override {
        timbre_ = timbre;
        oscillator_.updateWavetable(timbre);
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
        return envelope_.isActive();
    }

    BiquadFilter& getFilter() {
        return filter_;
    }
    
    /**
     * @brief Generate the next audio sample
     * @return Audio sample in range [-1.0, 1.0]
     */
    float nextSample() {
        if (!envelope_.isActive()) {
            return 0.0f;
        }
        
        // Calculate current frequency with pitch bend (inline)
        float semitoneShift = pitchBend_ * pitchBendRange_;
        float frequency = baseFrequency_ * std::pow(2.0f, semitoneShift / 12.0f);
        
        // Generate oscillator sample
        float sample = oscillator_.nextSample(frequency);
        
        // Apply filter (classic subtractive synthesis: osc -> filter -> envelope)
        sample = filter_.processSample(sample);
        
        // Apply envelope and volume
        float envelopeLevel = envelope_.nextSample();
        sample *= envelopeLevel * volume_;
        
        return sample;
    }

private:
    float sampleRate_;
    
    // Components (composed by value for cache locality and inlining)
    WavetableOscillator oscillator_;
    BiquadFilter filter_;
    AdsrEnvelope envelope_;
    
    // Voice parameters
    float baseFrequency_ = 440.0f;
    float volume_ = 1.0f;
    float timbre_ = 0.5f;
    float pitchBend_ = 0.0f;
    float pitchBendRange_ = 2.0f;
};

} // namespace synth

#endif // SAWTOOTH_SYNTH_HPP
