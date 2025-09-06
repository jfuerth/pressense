#ifndef SYNTH_VOICE_ALLOCATOR_H
#define SYNTH_VOICE_ALLOCATOR_H

#include "synth.hpp"
#include <cstdint>

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
    
  private:
    uint8_t maxVoices;
  };

} // namespace midi

#endif // SYNTH_VOICE_ALLOCATOR_H
