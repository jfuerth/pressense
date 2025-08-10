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
     * @brief Update the timbre characteristics of the synthesizer
     * @param timbre The timbre value (range 0.0 to 1.0)
     */
    virtual void setTimbre(float timbre) = 0;
    
    /**
     * @brief Update the volume level of the synthesizer
     * @param volume The volume level (0.0 to 1.0)
     */
    virtual void setVolume(float volume) = 0;

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
