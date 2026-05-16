#pragma once

#include <cstdint>

namespace midi {

/**
 * @brief Abstract interface for receiving MIDI note events
 *
 * This interface defines what a MIDI target can receive, expressed in
 * MIDI-native terms (note numbers 0-127, velocities 0-127, etc.).
 *
 * Implementations handle the translation from MIDI concepts to whatever
 * the underlying sound engine needs (e.g., Hz, normalized floats).
 *
 * This interface focuses on note-related events. Channel voice messages
 * like note on/off, aftertouch, and pitch bend are included. Control
 * changes and program changes are handled separately via callbacks
 * since their interpretation is application-specific.
 */
class NoteTarget {
public:
    virtual ~NoteTarget() = default;

    /**
     * @brief Handle a Note On event
     * @param note MIDI note number (0-127)
     * @param velocity Note velocity (1-127; velocity 0 is treated as Note Off)
     */
    virtual void noteOn(uint8_t note, uint8_t velocity) = 0;

    /**
     * @brief Handle a Note Off event
     * @param note MIDI note number (0-127)
     * @param velocity Release velocity (0-127, often ignored)
     */
    virtual void noteOff(uint8_t note, uint8_t velocity) = 0;

    /**
     * @brief Handle Polyphonic Aftertouch (per-note pressure)
     * @param note MIDI note number (0-127)
     * @param pressure Pressure amount (0-127)
     */
    virtual void polyAftertouch(uint8_t note, uint8_t pressure) = 0;

    /**
     * @brief Handle Pitch Bend
     * @param bend 14-bit pitch bend value (-8192 to +8191, where 0 is center)
     *
     * Raw MIDI pitch bend is 0-16383 with 8192 as center. This interface
     * uses signed representation for clarity.
     */
    virtual void pitchBend(int16_t bend) = 0;

    /**
     * @brief Handle Channel Aftertouch (channel-wide pressure)
     * @param pressure Pressure amount (0-127)
     */
    virtual void channelAftertouch(uint8_t pressure) = 0;
};

} // namespace midi
