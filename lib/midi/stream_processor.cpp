#include "stream_processor.hpp"
#include <utility>
#include <cmath>

namespace midi {

StreamProcessor::StreamProcessor(
        std::unique_ptr<SynthVoiceAllocator> voiceAllocator,
        uint8_t listenChannel,
        ControlChangeCallback ccCallback,
        PolyAftertouchCallback polyAftertouchCallback,
        ProgramChangeCallback programChangeCallback)
    : synthVoiceAllocator_(std::move(voiceAllocator))
    , controlChangeCallback_(ccCallback)
    , polyAftertouchCallback_(polyAftertouchCallback)
    , programChangeCallback_(programChangeCallback)
    , listenChannel_(listenChannel)
{
    // Constructor implementation - dependencies are moved and stored
}

StreamProcessor::ProcessorState StreamProcessor::stateFromCommandByte(uint8_t command) {
    if (command <= 0xBF || (command & COMMAND_MASK) == 0xE0) {
        return Need2Bytes;
    } else if (command <= 0xDF) {
        return Need1Byte;
    } else { // System messages or unsupported commands
        return Initial; // For now, treat as Initial (no data bytes expected)
    }
}

void StreamProcessor::process(const uint8_t data)
{
    // Hex Binary   Data Bytes DESCRIPTION
    //
    // -- Channel Voice Messages --
    // 8nH 1000nnnn         2  Note Off
    // 9nH 1001nnnn         2  Note On (a velocity of 0 = Note Off)
    // AnH 1010nnnn         2  Polyphonic key pressure/Aftertouch
    // BnH 1011nnnn         2  Control change (first byte <= 120; otherwise see Channel Mode Messages)
    // CnH 1100nnnn         1  Program change
    // DnH 1101nnnn         1  Channel pressure/Aftertouch
    // EnH 1110nnnn         2  Pitch bend change
    //
    // -- Channel Mode Messages --
    // BnH 1011nnnn         2 Selects Channel Mode (first byte >= 121)
    //
    // -- System Messages --
    // F0H 11110000     ***** System Exclusive, terminated by F7H
    // FxH 11110sss    0 to 2 System Common
    // FxH 11111ttt         0 System Real Time

    // Decision tree for state transitions
    // The first few checks are for data bytes that are independent of current MIDI parser state
    if (isSystemRealTime(data)) {
        // These messages can appear anywhere in the stream and do not affect running status
        // For now, we just ignore them and preserve the current state
        return;

    } else if (isStatusByte(data)) {
        // Non-realtime status bytes reset the running status and discard any partial message
        uint8_t channel = extractChannel(data);
        uint8_t command = extractCommand(data);
        
        // Only process messages on our listen channel
        if (channel != listenChannel_) {
            currentCommand_ = 0;
            processorState_ = Initial;
            return;
        }
        
        currentCommand_ = command;
        processorState_ = stateFromCommandByte(command);

    } else if (processorState_ == Need2Bytes) {
        messageByte1_ = data;
        processorState_ = Need1Byte;

    } else if (processorState_ == Need1Byte) {
        if (currentCommand_ == (NOTE_ON_COMMAND)) {
            uint8_t note = messageByte1_;
            uint8_t velocity = data;

            Synth& voice = synthVoiceAllocator_->allocate(note);

            if (velocity == 0) {
                // Note On with velocity 0 is treated as Note Off
                voice.release();
            } else {
                float frequencyHz = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
                float volume = static_cast<float>(velocity) / 127.0f;
                voice.trigger(frequencyHz, volume);
            }

        } else if (currentCommand_ == (NOTE_OFF_COMMAND)) {
            uint8_t note = messageByte1_;
            // data is release velocity (ignored)
            Synth& voice = synthVoiceAllocator_->allocate(note);
            voice.release();

        } else if (currentCommand_ == (POLY_AFTERTOUCH_COMMAND)) {
            uint8_t note = messageByte1_;
            uint8_t pressure = data;
            
            // Poly aftertouch affects only the specific note's voice
            Synth* voice = synthVoiceAllocator_->findAllocated(note);
            if (voice && polyAftertouchCallback_) {
                polyAftertouchCallback_(listenChannel_, note, pressure, *voice);
            }

        } else if (currentCommand_ == (CONTROL_CHANGE_COMMAND) && messageByte1_ < 120) {
            uint8_t controllerNumber = messageByte1_;
            uint8_t controllerValue = data;

            // Delegate to application callback if provided
            if (controlChangeCallback_) {
                controlChangeCallback_(listenChannel_, controllerNumber, controllerValue, *synthVoiceAllocator_);
            }
            // Otherwise, no default behavior (application must provide mapping)

        } else if (currentCommand_ == (PROGRAM_CHANGE_COMMAND)) {
            uint8_t programNumber = data;  // Program Change is 1-byte message, data comes directly
            
            // Delegate to application callback if provided
            if (programChangeCallback_) {
                programChangeCallback_(listenChannel_, programNumber, *synthVoiceAllocator_);
            }

        } else if (currentCommand_ == (PITCH_BEND_COMMAND)) {
            uint8_t lsb = messageByte1_;
            uint8_t msb = data;
            
            // Combine LSB and MSB into 14-bit value
            uint16_t pitchBendValue = (msb << 7) | lsb;
            
            // Convert 14-bit value (0-16383) to normalized float (-1.0 to +1.0)
            // Center value is 8192, so subtract center and divide by range
            float normalizedBend = (static_cast<float>(pitchBendValue) - 8192.0f) / 8192.0f;
            
            // Apply pitch bend to all voices via forEachVoice
            // This ensures even voices not currently assigned to notes get updated
            synthVoiceAllocator_->forEachVoice([normalizedBend](Synth& voice) {
                voice.setPitchBend(normalizedBend);
            });
        }

        processorState_ = stateFromCommandByte(currentCommand_);

    } else {
        processorState_ = stateFromCommandByte(currentCommand_);
    }
}

bool StreamProcessor::isStatusByte(uint8_t data)
{
    return (data & STATUS_BYTE_MASK) != 0;
}

bool StreamProcessor::isSystemRealTime(uint8_t data)
{
    return data >= SYSTEM_REALTIME_MIN && data <= SYSTEM_REALTIME_MAX;
}

uint8_t StreamProcessor::extractChannel(uint8_t statusByte)
{
    return statusByte & CHANNEL_MASK;
}

uint8_t StreamProcessor::extractCommand(uint8_t statusByte)
{
    return statusByte & COMMAND_MASK;
}

} // namespace midi
