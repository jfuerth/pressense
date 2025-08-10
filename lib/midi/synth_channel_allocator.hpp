#ifndef SYNTH_CHANNEL_ALLOCATOR_H
#define SYNTH_CHANNEL_ALLOCATOR_H

#include <cstdint>

namespace midi {

  /**
   * @brief Abstract base class for managing allocation of synthesizer channels for MIDI notes
   * 
   * This class provides the interface for mapping between MIDI notes and available
   * synthesizer channels, ensuring efficient voice allocation for polyphonic synthesis.
   */
  class SynthChannelAllocator {
  public:
    /**
     * @brief Default constructor
     */
    SynthChannelAllocator() = default;
    
    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     */
    virtual ~SynthChannelAllocator() = default;
    
    /**
     * @brief Deleted copy constructor to prevent copying
     */
    SynthChannelAllocator(const SynthChannelAllocator&) = delete;
    
    /**
     * @brief Deleted copy assignment operator to prevent copying
     */
    SynthChannelAllocator& operator=(const SynthChannelAllocator&) = delete;
    
    /**
     * @brief Default move constructor
     */
    SynthChannelAllocator(SynthChannelAllocator&&) = default;
    
    /**
     * @brief Default move assignment operator
     */
    SynthChannelAllocator& operator=(SynthChannelAllocator&&) = default;
    
    /**
     * @brief Allocate a synth channel for a MIDI note
     * @param midiNote The MIDI note number (0-127)
     * @return The allocated synthesizer channel number, or 0xFF if no channels available
     */
    virtual uint8_t allocateChannel(uint8_t midiNote) = 0;

    /**
     * @brief Release a previously allocated synth channel
     * @param synthChannel The synthesizer channel number to release
     */
    virtual void releaseChannel(uint8_t synthChannel) = 0;

    /**
     * @brief Get the synth channel currently assigned to a MIDI note
     * @param midiNote The MIDI note number to query (0-127)
     * @return The synthesizer channel number, or 0xFF if not allocated
     */
    virtual uint8_t getSynthChannel(uint8_t midiNote) const = 0;
    
  private:
    // Implementation details would go here
    // For example, tracking which channels are allocated
  };

} // namespace midi

#endif // SYNTH_CHANNEL_ALLOCATOR_H
