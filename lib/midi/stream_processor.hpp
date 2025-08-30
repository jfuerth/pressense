#ifndef STREAM_PROCESSOR_H
#define STREAM_PROCESSOR_H

#include "synth_voice_allocator.hpp"
#include "synth.hpp"
#include <memory>

namespace midi {

  // MIDI Constants
  static constexpr uint8_t STATUS_BYTE_MASK = 0x80;
  static constexpr uint8_t CHANNEL_MASK = 0x0F;
  static constexpr uint8_t COMMAND_MASK = 0xF0;
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
     * @brief Construct a StreamProcessor with custom synth and voice allocator
     * @param synth Unique pointer to the synthesizer implementation
     * @param voiceAllocator Unique pointer to the voice allocator implementation
     * @param listenChannel MIDI channel to listen to (0-15)
     */
    StreamProcessor(std::unique_ptr<Synth> synth, 
                   std::unique_ptr<SynthVoiceAllocator> voiceAllocator,
                   uint8_t listenChannel = 0);
    
    StreamProcessor(StreamProcessor&& other) = default;
    StreamProcessor& operator=(StreamProcessor&& other) = default;
    StreamProcessor(const StreamProcessor&) = delete;
    StreamProcessor& operator=(const StreamProcessor&) = delete;

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
     * @brief Voice allocator for managing synthesizer voices
     */
    std::unique_ptr<SynthVoiceAllocator> synthVoiceAllocator;

    uint8_t currentCommand = 0; // Current MIDI command being processed
    uint8_t listenChannel = 0;  // MIDI channel to listen to (0-15)
    
    // State for Note On message parsing
    enum NoteOnState {
        WaitingForNote,
        WaitingForVelocity
    };
    NoteOnState noteOnState = WaitingForNote;
    uint8_t noteNumber = 0;
    uint8_t velocity = 0;
    
    // Private helper methods for MIDI processing
    bool isStatusByte(uint8_t data) const;
    uint8_t extractChannel(uint8_t statusByte) const;
    uint8_t extractCommand(uint8_t statusByte) const;
    void handleStatusByte(uint8_t data);
    void handleDataByte(uint8_t data);
    void handleNoteOnDataByte(uint8_t data);
  };

} // namespace midi

#endif // STREAM_PROCESSOR_H