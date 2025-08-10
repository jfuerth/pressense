#ifndef SYNTH_VOICE_ALLOCATOR_H
#define SYNTH_VOICE_ALLOCATOR_H

#include <cstdint>

namespace midi {

  /**
   * @brief Abstract base class for managing allocation of synthesizer voices for MIDI notes
   * 
   * This class provides the interface for mapping between MIDI notes and available
   * synthesizer voices, ensuring efficient voice allocation for polyphonic synthesis.
   */
  class SynthVoiceAllocator {
  public:
    /**
     * @brief Default constructor
     */
    SynthVoiceAllocator() = default;
    
    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     */
    virtual ~SynthVoiceAllocator() = default;
    
    /**
     * @brief Deleted copy constructor to prevent copying
     */
    SynthVoiceAllocator(const SynthVoiceAllocator&) = delete;
    
    /**
     * @brief Deleted copy assignment operator to prevent copying
     */
    SynthVoiceAllocator& operator=(const SynthVoiceAllocator&) = delete;
    
    /**
     * @brief Default move constructor
     */
    SynthVoiceAllocator(SynthVoiceAllocator&&) = default;
    
    /**
     * @brief Default move assignment operator
     */
    SynthVoiceAllocator& operator=(SynthVoiceAllocator&&) = default;
    
    /**
     * @brief Allocate a synth voice for a MIDI note
     * @param midiNote The MIDI note number (0-127)
     * @return The allocated synthesizer voice number, or 0xFF if no voices available
     */
    virtual uint8_t allocateVoice(uint8_t midiNote) = 0;

    /**
     * @brief Release a previously allocated synth voice
     * @param synthVoice The synthesizer voice number to release
     */
    virtual void releaseVoice(uint8_t synthVoice) = 0;

    /**
     * @brief Get the synth voice currently assigned to a MIDI note
     * @param midiNote The MIDI note number to query (0-127)
     * @return The synthesizer voice number, or 0xFF if not allocated
     */
    virtual uint8_t getSynthVoice(uint8_t midiNote) const = 0;
    
  private:
    // Implementation details would go here
    // For example, tracking which voices are allocated
  };

} // namespace midi

#endif // SYNTH_VOICE_ALLOCATOR_H
