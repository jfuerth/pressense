#include "stream_processor.hpp"
#include <utility>

namespace midi {

StreamProcessor::StreamProcessor(std::unique_ptr<Synth> synth, 
                               std::unique_ptr<SynthChannelAllocator> channelAllocator)
    : synth(std::move(synth))
    , synthChannelAllocator(std::move(channelAllocator))
{
    // Constructor implementation - dependencies are moved and stored
}

void StreamProcessor::process(const uint8_t data)
{
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

void StreamProcessor::handleStatusByte(uint8_t data)
{
    currentCommand = data;
    if (data == NOTE_ON_COMMAND) {
        // Note On message - reset state for new message
        noteOnState = WaitingForNote;
    } else if (data == NOTE_OFF_COMMAND) {
        // Note Off message
    }
}

void StreamProcessor::handleDataByte(uint8_t data)
{
    if (currentCommand == NOTE_ON_COMMAND) {
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
        // Complete Note On message received - allocate channel
        synthChannelAllocator->allocateChannel(noteNumber);
        noteOnState = WaitingForNote; // Reset for next message
    }
}

} // namespace midi
