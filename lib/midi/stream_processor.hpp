#pragma once

#include "note_target.hpp"
#include <functional>
#include <cstdint>

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
static constexpr uint8_t CHANNEL_AFTERTOUCH_COMMAND = 0xD0;
static constexpr uint8_t PITCH_BEND_COMMAND = 0xE0;
static constexpr uint8_t SYSTEM_REALTIME_MIN = 0xF8;
static constexpr uint8_t SYSTEM_REALTIME_MAX = 0xFF;

// Callback types for application-level MIDI control mapping

/**
 * @brief Callback for control change messages
 * @param channel MIDI channel (0-15)
 * @param cc Controller number (0-127)
 * @param value Controller value (0-127)
 * 
 * Application captures any context it needs (voice pool, etc.) in the callback.
 */
using ControlChangeCallback = std::function<void(uint8_t channel, uint8_t cc, uint8_t value)>;

/**
 * @brief Callback for program change messages
 * @param channel MIDI channel (0-15)
 * @param program Program number (0-127)
 * 
 * Application captures any context it needs in the callback.
 */
using ProgramChangeCallback = std::function<void(uint8_t channel, uint8_t program)>;

/**
 * @brief MIDI byte stream parser that routes events to a NoteTarget
 *
 * This class parses raw MIDI byte streams and routes note events to a
 * NoteTarget implementation. Control change and program change messages
 * are delegated to application-provided callbacks.
 *
 * The StreamProcessor is a pure MIDI parser - it has no knowledge of
 * synthesizers, voices, or audio. The NoteTarget handles all note events,
 * and the application handles control mapping via callbacks.
 */
class StreamProcessor {
public:
    /**
     * @brief Construct a StreamProcessor
     * @param target Reference to the note target (must outlive this processor)
     * @param listenChannel MIDI channel to listen to (0-15)
     * @param ccCallback Optional callback for control change messages
     * @param programChangeCallback Optional callback for program change messages
     */
    StreamProcessor(
        NoteTarget& target,
        uint8_t listenChannel = 0,
        ControlChangeCallback ccCallback = nullptr,
        ProgramChangeCallback programChangeCallback = nullptr);

    // Non-copyable, movable
    StreamProcessor(StreamProcessor&& other) = default;
    StreamProcessor& operator=(StreamProcessor&& other) = default;
    StreamProcessor(const StreamProcessor&) = delete;
    StreamProcessor& operator=(const StreamProcessor&) = delete;

    ~StreamProcessor() = default;

    /**
     * @brief Process a single byte of MIDI data
     * @param data The MIDI data byte to process
     */
    void process(uint8_t data);

private:
    NoteTarget& target_;
    
    ControlChangeCallback controlChangeCallback_;
    ProgramChangeCallback programChangeCallback_;

    uint8_t listenChannel_ = 0;

    enum ProcessorState {
        Initial,
        Need2Bytes,
        Need1Byte,
    };
    ProcessorState processorState_ = Initial;
    uint8_t currentCommand_ = 0;
    uint8_t messageByte1_ = 0;

    static bool isStatusByte(uint8_t data);
    static bool isSystemRealTime(uint8_t data);
    static ProcessorState stateFromCommandByte(uint8_t command);
    static uint8_t extractChannel(uint8_t statusByte);
    static uint8_t extractCommand(uint8_t statusByte);
};

} // namespace midi

