#ifndef ADSR_ENVELOPE_HPP
#define ADSR_ENVELOPE_HPP

namespace synth {

/**
 * @brief ADSR (Attack, Decay, Sustain, Release) envelope generator
 * 
 * Generates an envelope curve from 0.0 to 1.0 based on trigger/release events.
 * All methods are inline for optimal performance in the audio generation loop.
 */
class AdsrEnvelope {
public:
    enum class Phase {
        IDLE,
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE
    };
    
    AdsrEnvelope(float sampleRate = 44100.0f) 
        : sampleRate_(sampleRate) {
        updateRates();
    }
    
    /**
     * @brief Set ADSR parameters
     * @param attack Attack time in seconds
     * @param decay Decay time in seconds
     * @param sustain Sustain level [0.0, 1.0]
     * @param release Release time in seconds
     */
    inline void setParameters(float attack, float decay, float sustain, float release) {
        attackTime_ = attack;
        decayTime_ = decay;
        sustainLevel_ = sustain;
        releaseTime_ = release;
        updateRates();
    }
    
    /**
     * @brief Set individual ADSR parameters
     */
    inline void setAttackTime(float time) { 
        attackTime_ = time; 
        updateRates();
    }
    
    inline void setDecayTime(float time) { 
        decayTime_ = time; 
        updateRates();
    }
    
    inline void setSustainLevel(float level) { 
        sustainLevel_ = level; 
    }
    
    inline void setReleaseTime(float time) { 
        releaseTime_ = time; 
        updateRates();
    }
    
    /**
     * @brief Trigger the envelope (start attack phase)
     */
    inline void trigger() {
        phase_ = Phase::ATTACK;
        level_ = 0.0f;
    }
    
    /**
     * @brief Release the envelope (start release phase)
     */
    inline void release() {
        if (phase_ != Phase::IDLE) {
            phase_ = Phase::RELEASE;
        }
    }
    
    /**
     * @brief Generate the next envelope sample
     * @return Envelope level [0.0, 1.0]
     */
    inline float nextSample() {
        switch (phase_) {
            case Phase::ATTACK:
                level_ += attackRate_;
                if (level_ >= 1.0f) {
                    level_ = 1.0f;
                    phase_ = Phase::DECAY;
                }
                break;
                
            case Phase::DECAY:
                level_ -= decayRate_;
                if (level_ <= sustainLevel_) {
                    level_ = sustainLevel_;
                    phase_ = Phase::SUSTAIN;
                }
                break;
                
            case Phase::SUSTAIN:
                level_ = sustainLevel_;
                break;
                
            case Phase::RELEASE:
                level_ -= releaseRate_;
                if (level_ <= 0.0f) {
                    level_ = 0.0f;
                    phase_ = Phase::IDLE;
                }
                break;
                
            case Phase::IDLE:
                level_ = 0.0f;
                break;
        }
        
        return level_;
    }
    
    /**
     * @brief Check if envelope is active (not in IDLE phase)
     */
    inline bool isActive() const {
        return phase_ != Phase::IDLE;
    }
    
    /**
     * @brief Get current envelope level
     */
    inline float getLevel() const {
        return level_;
    }
    
    /**
     * @brief Get current phase
     */
    inline Phase getPhase() const {
        return phase_;
    }
    
    /**
     * @brief Get ADSR parameter values
     */
    inline float getAttackTime() const { return attackTime_; }
    inline float getDecayTime() const { return decayTime_; }
    inline float getSustainLevel() const { return sustainLevel_; }
    inline float getReleaseTime() const { return releaseTime_; }
    
    /**
     * @brief Reset envelope to idle state
     */
    inline void reset() {
        phase_ = Phase::IDLE;
        level_ = 0.0f;
    }

private:
    inline void updateRates() {
        // Calculate per-sample rates
        attackRate_ = (attackTime_ > 0.0f) ? (1.0f / (attackTime_ * sampleRate_)) : 1.0f;
        decayRate_ = (decayTime_ > 0.0f) ? ((1.0f - sustainLevel_) / (decayTime_ * sampleRate_)) : 1.0f;
        releaseRate_ = (releaseTime_ > 0.0f) ? (sustainLevel_ / (releaseTime_ * sampleRate_)) : 1.0f;
    }
    
    float sampleRate_;
    
    // ADSR parameters (in seconds and level)
    float attackTime_ = 0.01f;    // 10ms
    float decayTime_ = 0.05f;     // 50ms
    float sustainLevel_ = 0.7f;
    float releaseTime_ = 0.1f;    // 100ms
    
    // Computed rates (per sample)
    float attackRate_ = 0.0f;
    float decayRate_ = 0.0f;
    float releaseRate_ = 0.0f;
    
    // Runtime state
    Phase phase_ = Phase::IDLE;
    float level_ = 0.0f;
};

} // namespace synth

#endif // ADSR_ENVELOPE_HPP
