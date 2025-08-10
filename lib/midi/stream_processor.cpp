#include "stream_processor.hpp"
#include <utility>

namespace midi {

StreamProcessor::StreamProcessor(std::unique_ptr<Synth> synth, 
                               std::unique_ptr<SynthVoiceAllocator> voiceAllocator,
                               uint8_t listenChannel)
    : synth(std::move(synth))
    , synthVoiceAllocator(std::move(voiceAllocator))
    , listenChannel(listenChannel)
{
    // Constructor implementation - dependencies are moved and stored
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

    if (isStatusByte(data)) {
        handleStatusByte(data);
    } else {
        handleDataByte(data);
    }
}

bool StreamProcessor::isStatusByte(uint8_t data) const
{
    return (data & STATUS_BYTE_MASK) != 0;
}

uint8_t StreamProcessor::extractChannel(uint8_t statusByte) const
{
    return statusByte & CHANNEL_MASK;
}

uint8_t StreamProcessor::extractCommand(uint8_t statusByte) const
{
    return statusByte & COMMAND_MASK;
}

void StreamProcessor::handleStatusByte(uint8_t data)
{
    uint8_t channel = extractChannel(data);
    uint8_t command = extractCommand(data);
    
    // Only process messages on our listen channel
    if (channel != listenChannel) {
        currentCommand = 0; // Clear current command for wrong channel
        return;
    }
    
    currentCommand = command;
    if (command == (NOTE_ON_COMMAND)) {
        // Note On message - reset state for new message
        noteOnState = WaitingForNote;
    } else if (command == (NOTE_OFF_COMMAND)) {
        // Note Off message
    }
}

void StreamProcessor::handleDataByte(uint8_t data)
{
    if (currentCommand == (NOTE_ON_COMMAND)) {
        handleNoteOnDataByte(data);
    }
}

void StreamProcessor::handleNoteOnDataByte(uint8_t data)
{
    if (noteOnState == WaitingForNote) {
        noteNumber = data;
        noteOnState = WaitingForVelocity;
    } else if (noteOnState == WaitingForVelocity) {
        velocity = data;
        // Complete Note On message received - allocate voice
        synthVoiceAllocator->allocateVoice(noteNumber);
        noteOnState = WaitingForNote; // Reset for next message
    }
}

} // namespace midi
