#pragma once

namespace synth {

/**
 * @brief Abstract interface for a monophonic synthesizer voice
 *
 * This interface defines what a synth voice can do in pure audio/DSP terms,
 * with no knowledge of MIDI or other control protocols.
 *
 * All parameters use audio-native units:
 * - Frequency in Hz
 * - Volume/amplitude as normalized floats (0.0 to 1.0)
 * - Pitch bend as normalized float (-1.0 to +1.0)
 *
 * Implementations handle oscillators, envelopes, filters, etc.
 */
class Voice {
public:
    virtual ~Voice() = default;

    /**
     * @brief Trigger the voice with the specified frequency and volume
     *
     * Starts the voice's envelope (attack phase). The voice will remain
     * in the sustain phase until release() is called.
     *
     * @param frequencyHz The fundamental frequency in Hz
     * @param volume The volume level (0.0 to 1.0)
     */
    virtual void trigger(float frequencyHz, float volume) = 0;

    /**
     * @brief Release the voice
     *
     * Transitions the voice's envelope to the release phase. The voice
     * may continue producing sound until the envelope completes.
     * Check isActive() to determine if the voice is still sounding.
     */
    virtual void release() = 0;

    /**
     * @brief Update the voice's frequency
     * @param frequencyHz The new frequency in Hz
     */
    virtual void setFrequency(float frequencyHz) = 0;

    /**
     * @brief Update the voice's volume level
     * @param volume The volume level (0.0 to 1.0)
     */
    virtual void setVolume(float volume) = 0;

    /**
     * @brief Apply pitch bend to the voice
     * @param bendAmount Normalized pitch bend (-1.0 to +1.0, where 0.0 is center)
     *
     * The actual frequency change depends on the pitch bend range.
     * For example, with a 2 semitone range:
     * -1.0 = -2 semitones, 0.0 = no change, +1.0 = +2 semitones
     */
    virtual void setPitchBend(float bendAmount) = 0;

    /**
     * @brief Get the current pitch bend range in semitones
     * @return The pitch bend range (typically 2.0 semitones)
     */
    virtual float getPitchBendRange() const = 0;

    /**
     * @brief Set the pitch bend range in semitones
     * @param semitones The range for full pitch bend (e.g., 2.0, 12.0)
     */
    virtual void setPitchBendRange(float semitones) = 0;

    /**
     * @brief Check if the voice is currently producing sound
     *
     * A voice is active from trigger() until its envelope completes
     * after release(). Voice allocators use this to find available voices.
     *
     * @return true if the voice is still sounding, false if silent
     */
    virtual bool isActive() const = 0;
};

} // namespace synth
