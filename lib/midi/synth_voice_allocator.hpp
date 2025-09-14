#ifndef SYNTH_VOICE_ALLOCATOR_H
#define SYNTH_VOICE_ALLOCATOR_H

#include "synth.hpp"
#include <cstdint>
#include <functional>

namespace midi {

  /**
   * @brief Abstract base class for managing allocation of synthesizer voices for MIDI notes
   * 
   * This class provides the interface for mapping between MIDI notes and available
   * synthesizer voices, ensuring efficient voice allocation for polyphonic synthesis.
   * This class owns the synthesizer instances it manages.
   */
  class SynthVoiceAllocator {
  public:
    /**
     * @brief Default constructor
     */
    SynthVoiceAllocator(uint8_t maxVoices) : maxVoices(maxVoices) {
        // Initialize voice allocation data structures
    }

    virtual ~SynthVoiceAllocator() = default;
    SynthVoiceAllocator(const SynthVoiceAllocator&) = delete;
    SynthVoiceAllocator& operator=(const SynthVoiceAllocator&) = delete;
    SynthVoiceAllocator(SynthVoiceAllocator&&) = default;
    SynthVoiceAllocator& operator=(SynthVoiceAllocator&&) = default;
    
    /**
     * @brief Retrieve the current voice assigned to the note, or allocate a new one
     * @param midiNote The MIDI note number (0-127)
     * @return The voice for the note.
     * 
     * If there are not enough voices available, a previously allocated voice will be reassigned.
     * This method should never fail and always return a valid voice.
     */
    virtual Synth& voiceFor(uint8_t midiNote) = 0;
    
    // TODO: Add method like existingVoiceFor(uint8_t midiNote) -> Synth* 
    // that returns a voice if and only if it's currently allocated to that note.
    // This would prevent note-off events from stealing back voices that were 
    // already reassigned to different notes.
    
    /**
     * @brief Apply a function to each voice managed by this allocator
     * @param func Function to apply to each voice
     * 
     * This allows operations like pitch bend to be applied to all voices
     * without exposing the internal voice management structure.
     */
    virtual void forEachVoice(std::function<void(Synth&)> func) = 0;
    
  private:
    uint8_t maxVoices;
  };

} // namespace midi

#endif // SYNTH_VOICE_ALLOCATOR_H
