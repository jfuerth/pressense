#pragma once

#include <note_target.hpp>
#include <voice.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <cmath>

namespace platform {

/**
 * @brief Bridges MIDI note events to a pool of synth voices
 *
 * This class implements midi::NoteTarget by managing a pool of synth::Voice
 * instances. It handles:
 * - MIDI note number to Hz conversion
 * - MIDI velocity to normalized volume conversion
 * - Polyphonic voice allocation with voice stealing
 * - Pitch bend with 14-bit to normalized float conversion
 *
 * The voice pool is pre-allocated at construction to ensure no dynamic
 * memory allocation during real-time audio processing.
 *
 * @tparam VoiceT Concrete voice type (must implement synth::Voice)
 */
template<typename VoiceT>
class PolyphonicSynthTarget : public midi::NoteTarget {
public:
    using VoiceFactory = std::function<std::unique_ptr<VoiceT>()>;

    /**
     * @brief Construct with a voice factory
     * @param maxVoices Maximum number of simultaneous voices
     * @param factory Function that creates new voice instances
     */
    PolyphonicSynthTarget(uint8_t maxVoices, VoiceFactory factory)
        : maxVoices_(maxVoices)
    {
        voices_.reserve(maxVoices);
        for (uint8_t i = 0; i < maxVoices; ++i) {
            voices_.emplace_back(factory());
        }
    }

    // Non-copyable, movable
    PolyphonicSynthTarget(const PolyphonicSynthTarget&) = delete;
    PolyphonicSynthTarget& operator=(const PolyphonicSynthTarget&) = delete;
    PolyphonicSynthTarget(PolyphonicSynthTarget&&) = default;
    PolyphonicSynthTarget& operator=(PolyphonicSynthTarget&&) = default;

    //--------------------------------------------------------------------------
    // midi::NoteTarget implementation
    //--------------------------------------------------------------------------

    void noteOn(uint8_t note, uint8_t velocity) override {
        if (velocity == 0) {
            // Velocity 0 is equivalent to Note Off
            noteOff(note, 0);
            return;
        }

        VoiceT* voice = allocateVoice(note);
        float hz = midiNoteToHz(note);
        float volume = velocity / 127.0f;
        voice->trigger(hz, volume);
    }

    void noteOff(uint8_t note, uint8_t /*velocity*/) override {
        VoiceT* voice = findVoiceForNote(note);
        if (voice) {
            voice->release();
            // Mark the slot as released but keep the note association
            // until the voice becomes inactive (for aftertouch during release)
            for (auto& slot : voices_) {
                if (slot.voice.get() == voice && slot.isAllocated) {
                    slot.isAllocated = false;  // Available for stealing
                    break;
                }
            }
        }
    }

    void polyAftertouch(uint8_t note, uint8_t pressure) override {
        VoiceT* voice = findVoiceForNote(note);
        if (voice) {
            // Pass normalized aftertouch to voice for modulation
            // The voice uses this to modulate filter, vibrato, tremolo, etc.
            // based on its aftertouch modulation settings
            float aftertouch = pressure / 127.0f;
            voice->setAftertouch(aftertouch);
        }
    }

    void pitchBend(int16_t bend) override {
        // Convert 14-bit signed (-8192 to +8191) to normalized (-1.0 to +1.0)
        float normalized = bend / 8192.0f;
        for (auto& slot : voices_) {
            slot.voice->setPitchBend(normalized);
        }
    }

    void channelAftertouch(uint8_t pressure) override {
        // Apply aftertouch to all active voices
        float aftertouch = pressure / 127.0f;
        for (auto& slot : voices_) {
            if (slot.isAllocated || slot.voice->isActive()) {
                slot.voice->setAftertouch(aftertouch);
            }
        }
    }

    //--------------------------------------------------------------------------
    // Voice access for rendering and control
    //--------------------------------------------------------------------------

    /**
     * @brief Apply a function to each voice (for audio rendering, global effects)
     * @param func Function to call with each voice
     */
    void forEachVoice(std::function<void(VoiceT&)> func) {
        for (auto& slot : voices_) {
            func(*slot.voice);
        }
    }

    /**
     * @brief Apply a function to each voice via base interface
     * @param func Function to call with each voice as synth::Voice&
     */
    void forEachVoice(std::function<void(synth::Voice&)> func) {
        for (auto& slot : voices_) {
            func(*slot.voice);
        }
    }

    /**
     * @brief Get the number of voices
     */
    uint8_t getVoiceCount() const {
        return maxVoices_;
    }

private:
    struct VoiceSlot {
        std::unique_ptr<VoiceT> voice;
        uint8_t assignedNote = 0;
        bool isAllocated = false;

        explicit VoiceSlot(std::unique_ptr<VoiceT> v) : voice(std::move(v)) {}

        // Move-only
        VoiceSlot(const VoiceSlot&) = delete;
        VoiceSlot& operator=(const VoiceSlot&) = delete;
        VoiceSlot(VoiceSlot&&) = default;
        VoiceSlot& operator=(VoiceSlot&&) = default;
    };

    std::vector<VoiceSlot> voices_;
    uint8_t maxVoices_;
    size_t lastAllocatedIndex_ = 0;

    /**
     * @brief Convert MIDI note number to frequency in Hz
     * @param note MIDI note number (0-127, where 69 = A4 = 440Hz)
     * @return Frequency in Hz
     */
    static float midiNoteToHz(uint8_t note) {
        // A4 (note 69) = 440 Hz
        return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
    }

    /**
     * @brief Allocate a voice for a note, stealing if necessary
     * @param note MIDI note number
     * @return Pointer to the allocated voice
     */
    VoiceT* allocateVoice(uint8_t note) {
        // Check if we already have this note playing
        for (auto& slot : voices_) {
            if (slot.isAllocated && slot.assignedNote == note) {
                return slot.voice.get();
            }
        }

        // Find an unallocated slot
        for (auto& slot : voices_) {
            if (!slot.isAllocated) {
                slot.assignedNote = note;
                slot.isAllocated = true;
                return slot.voice.get();
            }
        }

        // Find an inactive voice (finished release phase)
        for (auto& slot : voices_) {
            if (!slot.voice->isActive()) {
                slot.assignedNote = note;
                slot.isAllocated = true;
                return slot.voice.get();
            }
        }

        // All voices active - steal round-robin
        size_t stealIndex = (lastAllocatedIndex_ + 1) % voices_.size();
        lastAllocatedIndex_ = stealIndex;

        auto& slot = voices_[stealIndex];
        slot.voice->release();
        slot.assignedNote = note;
        slot.isAllocated = true;
        return slot.voice.get();
    }

    /**
     * @brief Find the voice currently assigned to a note
     * @param note MIDI note number
     * @return Pointer to voice if found, nullptr otherwise
     */
    VoiceT* findVoiceForNote(uint8_t note) {
        for (auto& slot : voices_) {
            if (slot.assignedNote == note && 
                (slot.isAllocated || slot.voice->isActive())) {
                return slot.voice.get();
            }
        }
        return nullptr;
    }
};

} // namespace platform
