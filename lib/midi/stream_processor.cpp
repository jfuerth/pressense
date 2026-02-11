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
    : synthVoiceAllocator(std::move(voiceAllocator))
    , controlChangeCallback(ccCallback)
    , polyAftertouchCallback(polyAftertouchCallback)
    , programChangeCallback(programChangeCallback)
    , listenChannel(listenChannel)
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
        if (channel != listenChannel) {
            currentCommand = 0;
            processorState = Initial;
            return;
        }
        
        currentCommand = command;
        processorState = stateFromCommandByte(command);

    } else if (processorState == Need2Bytes) {
        messageByte1 = data;
        processorState = Need1Byte;

    } else if (processorState == Need1Byte) {
        if (currentCommand == (NOTE_ON_COMMAND)) {
            uint8_t note = messageByte1;
            uint8_t velocity = data;

            Synth& voice = synthVoiceAllocator->allocate(note);

            if (velocity == 0) {
                // Note On with velocity 0 is treated as Note Off
                voice.release();
            } else {
                float frequencyHz = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
                float volume = static_cast<float>(velocity) / 127.0f;
                
                #ifdef PLATFORM_ESP32
                // Debug: log first few note triggers to verify frequency calculation
                static int noteCount = 0;
                if (noteCount < 8) {
                    printf("Note %d -> %.2f Hz\n", note, frequencyHz);
                    noteCount++;
                }
                #endif
                
                voice.trigger(frequencyHz, volume);
            }

        } else if (currentCommand == (NOTE_OFF_COMMAND)) {
            uint8_t note = messageByte1;
            // data is release velocity (ignored)
            Synth& voice = synthVoiceAllocator->allocate(note);
            voice.release();

        } else if (currentCommand == (POLY_AFTERTOUCH_COMMAND)) {
            uint8_t note = messageByte1;
            uint8_t pressure = data;
            
            // Poly aftertouch affects only the specific note's voice
            Synth* voice = synthVoiceAllocator->findAllocated(note);
            if (voice && polyAftertouchCallback) {
                polyAftertouchCallback(listenChannel, note, pressure, *voice);
            }

        } else if (currentCommand == (CONTROL_CHANGE_COMMAND) && messageByte1 < 120) {
            uint8_t controllerNumber = messageByte1;
            uint8_t controllerValue = data;

            // Delegate to application callback if provided
            if (controlChangeCallback) {
                controlChangeCallback(listenChannel, controllerNumber, controllerValue, *synthVoiceAllocator);
            }
            // Otherwise, no default behavior (application must provide mapping)

        } else if (currentCommand == (PROGRAM_CHANGE_COMMAND)) {
            uint8_t programNumber = data;  // Program Change is 1-byte message, data comes directly
            
            // Delegate to application callback if provided
            if (programChangeCallback) {
                programChangeCallback(listenChannel, programNumber, *synthVoiceAllocator);
            }

        } else if (currentCommand == (PITCH_BEND_COMMAND)) {
            uint8_t lsb = messageByte1;
            uint8_t msb = data;
            
            // Combine LSB and MSB into 14-bit value
            uint16_t pitchBendValue = (msb << 7) | lsb;
            
            // Convert 14-bit value (0-16383) to normalized float (-1.0 to +1.0)
            // Center value is 8192, so subtract center and divide by range
            float normalizedBend = (static_cast<float>(pitchBendValue) - 8192.0f) / 8192.0f;
            
            // Apply pitch bend to all voices via forEachVoice
            // This ensures even voices not currently assigned to notes get updated
            synthVoiceAllocator->forEachVoice([normalizedBend](Synth& voice) {
                voice.setPitchBend(normalizedBend);
            });
        }

        processorState = stateFromCommandByte(currentCommand);

    } else {
        processorState = stateFromCommandByte(currentCommand);
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
