#ifndef SAWTOOTH_SYNTH_HPP
#define SAWTOOTH_SYNTH_HPP

#include <synth.hpp>
#include <wavetable_oscillator.hpp>
#include <adsr_envelope.hpp>
#include <cmath>

namespace synth {

/**
 * @brief Modular wavetable synthesizer with ADSR envelope
 * 
 * Composed of:
 * - WavetableOscillator: Morphable waveform generation
 * - AdsrEnvelope: Amplitude envelope
 * - Inline pitch bend processing
 */
class WavetableSynth : public midi::Synth {
public:
    WavetableSynth(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate),
          oscillator_(sampleRate),
          envelope_(sampleRate) {}
    
    void trigger(float frequencyHz, float volume) override {
        baseFrequency_ = frequencyHz;
        volume_ = volume;
        oscillator_.reset();
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
        
        // Apply envelope and volume
        float envelopeLevel = envelope_.nextSample();
        sample *= envelopeLevel * volume_;
        
        return sample;
    }

private:
    float sampleRate_;
    
    // Components (composed by value for cache locality and inlining)
    WavetableOscillator oscillator_;
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
