#ifndef STREAM_PROCESSOR_H
#define STREAM_PROCESSOR_H

#include "synth_channel_allocator.hpp"
#include "synth.hpp"
#include <memory>

namespace midi {

  // MIDI Constants
  static constexpr uint8_t STATUS_BYTE_MASK = 0x80;
  static constexpr uint8_t NOTE_ON_COMMAND = 0x90;
  static constexpr uint8_t NOTE_OFF_COMMAND = 0x80;

  struct MidiStatusByte {
    uint8_t command : 4; // MIDI command (e.g., Note On, Note Off)
    uint8_t channel : 4; // MIDI channel (0-15)
  };

  /**
   * @brief Concrete class for processing MIDI data streams
   * 
   * This class processes MIDI byte streams and routes them to a synthesizer
   * using a pluggable channel allocator for voice management. Both the
   * synthesizer and channel allocator can be customized via dependency injection.
   */
  class StreamProcessor {
  public:
    /**
     * @brief Construct a StreamProcessor with custom synth and channel allocator
     * @param synth Unique pointer to the synthesizer implementation
     * @param channelAllocator Unique pointer to the channel allocator implementation
     */
    StreamProcessor(std::unique_ptr<Synth> synth, 
                   std::unique_ptr<SynthChannelAllocator> channelAllocator);
    
    /**
     * @brief Destructor for proper cleanup
     */
    ~StreamProcessor() = default;
    
    /**
     * @brief Process a single byte of MIDI data
     * @param data The MIDI data byte to process
     */
    void process(const uint8_t data);
    
  private:
    /**
     * @brief Synthesizer implementation for audio generation
     */
    std::unique_ptr<Synth> synth;
    
    /**
     * @brief Channel allocator for managing synthesizer voices
     */
    std::unique_ptr<SynthChannelAllocator> synthChannelAllocator;

    uint8_t currentCommand = 0; // Current MIDI command being processed
    
    // State for Note On message parsing
    enum NoteOnState {
        WaitingForNote,
        WaitingForVelocity
    };
    NoteOnState noteOnState = WaitingForNote;
    uint8_t noteNumber = 0;
    uint8_t velocity = 0;
    
    // Helper methods for MIDI processing
    bool isStatusByte(uint8_t data) const;
    void handleStatusByte(uint8_t data);
    void handleDataByte(uint8_t data);
    void handleNoteOnDataByte(uint8_t data);
  };

} // namespace midi

#endif // STREAM_PROCESSOR_H