#ifndef SYNTH_H
#define SYNTH_H

#include <cstdint>

namespace midi {

  /**
   * @brief Abstract base class for synthesizer implementations
   * 
   * This class defines the interface that all synthesizer implementations
   * must provide for generating audio from processed MIDI data (see StreamProcessor).
   * Synth implementations themselves are independent of MIDI
   * and focus solely on audio synthesis.
   *
   * Each Synth represents a monophonic synthesizer capable of generating audio
   * at a certain fundamental frequency, volume intensity, and timbre.
   */
  class Synth {
  public:
    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     */
    virtual ~Synth() = default;
    
    /**
     * @brief Trigger a note with the specified frequency and volume
     *
     * Synths with an ADSR envelope will start the attack phase, and remain in
     * the sustain phase until the note is released. See release().
     *
     * @param frequencyHz The frequency of the note in Hz
     * @param volume The volume level (0.0 to 1.0)
     */
    virtual void trigger(float frequencyHz, float volume) = 0;

    /**
     * @brief Tell the synth that a note is released
     *
     * Releases the note. Synths that have an ADSR envelope would typically
     * transition to the release phase. The synth may continue to play the note
     * for a while after this call. See isActive() to check if the synth is still playing.
     */
    virtual void release() = 0;

    /**
     * @brief Update the frequency of the currently playing note
     * @param frequencyHz The new frequency in Hz
     */
    virtual void setFrequency(float frequencyHz) = 0;
    
    /**
     * @brief Update the volume level of the synthesizer
     * @param volume The volume level (0.0 to 1.0)
     */
    virtual void setVolume(float volume) = 0;

    /**
     * @brief Apply pitch bend to the synthesizer
     * @param bendAmount The pitch bend amount (-1.0 to +1.0, where 0.0 is center)
     * 
     * The actual frequency change depends on the pitchBendRange property.
     * For example, with default 2 semitone range:
     * -1.0 = -2 semitones, 0.0 = no change, +1.0 = +2 semitones
     */
    virtual void setPitchBend(float bendAmount) = 0;

    /**
     * @brief Get the current pitch bend range in semitones
     * @return The pitch bend range (default is 2.0 semitones)
     */
    virtual float getPitchBendRange() const = 0;

    /**
     * @brief Set the pitch bend range in semitones
     * @param semitones The range for full pitch bend (typically 2.0, but can be 12.0 or more)
     */
    virtual void setPitchBendRange(float semitones) = 0;

    /**
     * @brief Check if the synth is currently active (i.e., playing a note)
     * 
     * Channel allocators can use this to determine if a synth is available
     * for new notes. If the synth is not active, it can be used to play
     * new notes without interrupting existing ones.
     *
     * @return True if the synth is active, false otherwise
     */
    virtual bool isActive() const = 0;
  };

} // namespace midi

#endif // SYNTH_H
