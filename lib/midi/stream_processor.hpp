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
  static constexpr uint8_t PITCH_BEND_COMMAND = 0xE0;
  static constexpr uint8_t SYSTEM_REALTIME_MIN = 0xF8;
  static constexpr uint8_t SYSTEM_REALTIME_MAX = 0xFF;

  /**
   * @brief Concrete class for processing MIDI data streams
   * 
   * This class processes MIDI byte streams and routes them to a synthesizer
   * using a pluggable channel allocator for voice management. Both the
   * synthesizer and channel allocator can be customized via dependency injection.
   * 
   * The voice allocator now provides existingVoiceFor() method to safely handle
   * note-off events without stealing voices that were already reassigned to different notes.
   */
  class StreamProcessor {
  public:
    /**
     * @brief Construct a StreamProcessor with custom synth and voice allocator
     * @param voiceAllocator Unique pointer to the voice allocator implementation
     * @param listenChannel MIDI channel to listen to (0-15)
     */
    StreamProcessor(
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
     * @brief Voice allocator for managing synthesizer voices
     */
    std::unique_ptr<SynthVoiceAllocator> synthVoiceAllocator;

    uint8_t listenChannel = 0;  // MIDI channel to listen to (0-15)
    
    // State for Note On message parsing
    enum ProcessorState {
        Initial,
        Need2Bytes, // Have received a valid status byte; waiting for data byte 1 of 2
        Need1Byte,  // Have received a valid status byte; waiting for data byte 1 of 1 or 2 of 2
    };
    ProcessorState processorState = Initial;
    uint8_t currentCommand = 0; // Current MIDI command being processed
    uint8_t messageByte1 = 0;

    // Private helper methods for MIDI processing
    static bool isStatusByte(uint8_t data);
    static bool isSystemRealTime(uint8_t data);
    static ProcessorState stateFromCommandByte(uint8_t command);
    static uint8_t extractChannel(uint8_t statusByte);
    static uint8_t extractCommand(uint8_t statusByte);
  };

} // namespace midi

#endif // STREAM_PROCESSOR_H