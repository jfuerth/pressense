#ifndef SAWTOOTH_SYNTH_HPP
#define SAWTOOTH_SYNTH_HPP

#include "../lib/midi/synth.hpp"
#include <cmath>

/**
 * @brief Simple sawtooth wave synthesizer with ADSR envelope
 */
class SawtoothSynth : public midi::Synth {
public:
    SawtoothSynth(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate) {}
    
    void trigger(float frequencyHz, float volume) override {
        baseFrequency_ = frequencyHz;
        volume_ = volume;
        phase_ = 0.0f;
        isActive_ = true;
        envelopePhase_ = EnvelopePhase::ATTACK;
        envelopeLevel_ = 0.0f;
    }
    
    void release() override {
        if (isActive_) {
            envelopePhase_ = EnvelopePhase::RELEASE;
        }
    }
    
    void setFrequency(float frequencyHz) override {
        baseFrequency_ = frequencyHz;
    }
    
    void setTimbre(float timbre) override {
        timbre_ = timbre;
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
        return isActive_;
    }
    
    /**
     * @brief Generate the next audio sample
     * @return Audio sample in range [-1.0, 1.0]
     */
    float nextSample() {
        if (!isActive_) {
            return 0.0f;
        }
        
        // Update envelope
        updateEnvelope();
        
        // Calculate current frequency with pitch bend
        float semitoneShift = pitchBend_ * pitchBendRange_;
        float frequency = baseFrequency_ * std::pow(2.0f, semitoneShift / 12.0f);
        
        // Generate sawtooth wave (ramps from -1 to +1)
        float sample = 2.0f * phase_ - 1.0f;
        
        // Apply envelope and volume
        sample *= envelopeLevel_ * volume_;
        
        // Update phase
        phase_ += frequency / sampleRate_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        }
        
        return sample;
    }

private:
    enum class EnvelopePhase {
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE,
        IDLE
    };
    
    void updateEnvelope() {
        const float attackTime = 0.01f;   // 10ms
        const float decayTime = 0.05f;    // 50ms
        const float sustainLevel = 0.7f;
        const float releaseTime = 0.1f;   // 100ms
        
        const float attackRate = 1.0f / (attackTime * sampleRate_);
        const float decayRate = (1.0f - sustainLevel) / (decayTime * sampleRate_);
        const float releaseRate = sustainLevel / (releaseTime * sampleRate_);
        
        switch (envelopePhase_) {
            case EnvelopePhase::ATTACK:
                envelopeLevel_ += attackRate;
                if (envelopeLevel_ >= 1.0f) {
                    envelopeLevel_ = 1.0f;
                    envelopePhase_ = EnvelopePhase::DECAY;
                }
                break;
                
            case EnvelopePhase::DECAY:
                envelopeLevel_ -= decayRate;
                if (envelopeLevel_ <= sustainLevel) {
                    envelopeLevel_ = sustainLevel;
                    envelopePhase_ = EnvelopePhase::SUSTAIN;
                }
                break;
                
            case EnvelopePhase::SUSTAIN:
                envelopeLevel_ = sustainLevel;
                break;
                
            case EnvelopePhase::RELEASE:
                envelopeLevel_ -= releaseRate;
                if (envelopeLevel_ <= 0.0f) {
                    envelopeLevel_ = 0.0f;
                    envelopePhase_ = EnvelopePhase::IDLE;
                    isActive_ = false;
                }
                break;
                
            case EnvelopePhase::IDLE:
                envelopeLevel_ = 0.0f;
                isActive_ = false;
                break;
        }
    }
    
    float sampleRate_;
    float baseFrequency_ = 440.0f;
    float volume_ = 1.0f;
    float timbre_ = 0.5f;
    float pitchBend_ = 0.0f;
    float pitchBendRange_ = 2.0f;
    float phase_ = 0.0f;
    float envelopeLevel_ = 0.0f;
    EnvelopePhase envelopePhase_ = EnvelopePhase::IDLE;
    bool isActive_ = false;
};

#endif // SAWTOOTH_SYNTH_HPP
