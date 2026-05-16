#include "stream_processor.hpp"
#include <utility>

namespace midi {

StreamProcessor::StreamProcessor(
        NoteTarget& target,
        uint8_t listenChannel,
        ControlChangeCallback ccCallback,
        ProgramChangeCallback programChangeCallback)
    : target_(target)
    , controlChangeCallback_(std::move(ccCallback))
    , programChangeCallback_(std::move(programChangeCallback))
    , listenChannel_(listenChannel)
{
}

StreamProcessor::ProcessorState StreamProcessor::stateFromCommandByte(uint8_t command) {
    if (command <= 0xBF || (command & COMMAND_MASK) == 0xE0) {
        return Need2Bytes;
    } else if (command <= 0xDF) {
        return Need1Byte;
    } else {
        return Initial;
    }
}

void StreamProcessor::process(uint8_t data)
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
        // These messages can appear anywhere in the stream and do not affect running status.
        // For now, we just ignore them and preserve the current state
        return;

    } else if (isStatusByte(data)) {
        // Non-realtime status bytes reset the running status and discard any partial message
        uint8_t channel = extractChannel(data);
        uint8_t command = extractCommand(data);
        
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
        if (currentCommand_ == NOTE_ON_COMMAND) {
            uint8_t note = messageByte1_;
            uint8_t velocity = data;
            if (velocity == 0) {
                // MIDI spec: Note On with velocity 0 is equivalent to Note Off
                target_.noteOff(note, 0);
            } else {
                target_.noteOn(note, velocity);
            }

        } else if (currentCommand_ == NOTE_OFF_COMMAND) {
            uint8_t note = messageByte1_;
            uint8_t velocity = data;
            target_.noteOff(note, velocity);

        } else if (currentCommand_ == POLY_AFTERTOUCH_COMMAND) {
            uint8_t note = messageByte1_;
            uint8_t pressure = data;
            target_.polyAftertouch(note, pressure);

        } else if (currentCommand_ == CONTROL_CHANGE_COMMAND && messageByte1_ < 120) {
            uint8_t cc = messageByte1_;
            uint8_t value = data;
            if (controlChangeCallback_) {
                controlChangeCallback_(listenChannel_, cc, value);
            }

        } else if (currentCommand_ == PITCH_BEND_COMMAND) {
            uint8_t lsb = messageByte1_;
            uint8_t msb = data;
            uint16_t rawBend = (msb << 7) | lsb;
            int16_t signedBend = static_cast<int16_t>(rawBend) - 8192;
            target_.pitchBend(signedBend);
        }

        processorState_ = stateFromCommandByte(currentCommand_);

    } else if (processorState_ == Initial && currentCommand_ == PROGRAM_CHANGE_COMMAND) {
        // Program change is a 1-byte message handled here
        if (programChangeCallback_) {
            programChangeCallback_(listenChannel_, data);
        }
        processorState_ = Initial;

    } else if (processorState_ == Initial && currentCommand_ == CHANNEL_AFTERTOUCH_COMMAND) {
        // Channel aftertouch is a 1-byte message
        target_.channelAftertouch(data);
        processorState_ = Initial;

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
