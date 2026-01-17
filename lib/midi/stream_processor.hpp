#ifndef STREAM_PROCESSOR_H
#define STREAM_PROCESSOR_H

#include "synth_voice_allocator.hpp"
#include "synth.hpp"
#include <memory>
#include <functional>

namespace midi {

  // MIDI Constants
  static constexpr uint8_t STATUS_BYTE_MASK = 0x80;
  static constexpr uint8_t CHANNEL_MASK = 0x0F;
  static constexpr uint8_t COMMAND_MASK = 0xF0;
  static constexpr uint8_t NOTE_ON_COMMAND = 0x90;
  static constexpr uint8_t NOTE_OFF_COMMAND = 0x80;
  static constexpr uint8_t POLY_AFTERTOUCH_COMMAND = 0xA0;
  static constexpr uint8_t CONTROL_CHANGE_COMMAND = 0xB0;
  static constexpr uint8_t PROGRAM_CHANGE_COMMAND = 0xC0;
  static constexpr uint8_t PITCH_BEND_COMMAND = 0xE0;
  static constexpr uint8_t SYSTEM_REALTIME_MIN = 0xF8;
  static constexpr uint8_t SYSTEM_REALTIME_MAX = 0xFF;

  // Callback types for application-level MIDI control mapping
  
  /**
   * @brief Callback for control change messages (global voice control)
   * @param channel MIDI channel (0-15)
   * @param cc Controller number (0-127)
   * @param value Controller value (0-127)
   * @param allocator Reference to voice allocator for forEachVoice() access
   */
  using ControlChangeCallback = std::function<void(
    uint8_t channel, 
    uint8_t cc, 
    uint8_t value, 
    SynthVoiceAllocator& allocator)>;
  
  /**
   * @brief Callback for polyphonic aftertouch messages (per-voice control)
   * @param channel MIDI channel (0-15)
   * @param note MIDI note number (0-127)
   * @param pressure Aftertouch pressure (0-127)
   * @param voice Reference to the specific voice for this note
   */
  using PolyAftertouchCallback = std::function<void(
    uint8_t channel,
    uint8_t note,
    uint8_t pressure,
    Synth& voice)>;
  
  /**
   * @brief Callback for program change messages
   * @param channel MIDI channel (0-15)
   * @param program Program number (0-127)
   * @param allocator Reference to voice allocator for forEachVoice() access
   */
  using ProgramChangeCallback = std::function<void(
    uint8_t channel,
    uint8_t program,
    SynthVoiceAllocator& allocator)>;

  /**
   * @brief Concrete class for processing MIDI data streams
   * 
   * This class processes MIDI byte streams and routes them to a synthesizer
   * using a pluggable channel allocator for voice management. Control mapping
   * is delegated to application-level callbacks for flexibility.
   */
  class StreamProcessor {
  public:
    /**
     * @brief Construct a StreamProcessor with custom synth and voice allocator
     * @param voiceAllocator Unique pointer to the voice allocator implementation
     * @param listenChannel MIDI channel to listen to (0-15)
     * @param ccCallback Optional callback for control change messages
     * @param polyAftertouchCallback Optional callback for poly aftertouch messages
     * @param programChangeCallback Optional callback for program change messages
     */
    StreamProcessor(
      std::unique_ptr<SynthVoiceAllocator> voiceAllocator,
      uint8_t listenChannel = 0,
      ControlChangeCallback ccCallback = nullptr,
      PolyAftertouchCallback polyAftertouchCallback = nullptr,
      ProgramChangeCallback programChangeCallback = nullptr);

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
    
    /**
     * @brief Iterate over all voices, whether active or not
     * @param func Function to call for each voice
     * 
     * Provides access to voices for audio rendering without exposing allocator.
     */
    void forEachVoice(std::function<void(Synth&)> func) {
      synthVoiceAllocator->forEachVoice(func);
    }
    
  private:

    /**
     * @brief Voice allocator for managing synthesizer voices
     */
    std::unique_ptr<SynthVoiceAllocator> synthVoiceAllocator;

    // Application-level control mapping callbacks
    ControlChangeCallback controlChangeCallback;
    PolyAftertouchCallback polyAftertouchCallback;
    ProgramChangeCallback programChangeCallback;

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