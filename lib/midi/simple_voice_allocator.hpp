#ifndef SIMPLE_VOICE_ALLOCATOR_H
#define SIMPLE_VOICE_ALLOCATOR_H

#include "synth_voice_allocator.hpp"
#include "synth.hpp"
#include <memory>
#include <vector>
#include <functional>

namespace midi {

  /**
   * @brief A simple concrete SynthVoiceAllocator that uses a factory function to create voices
   * 
   * This allocator creates voices on-demand using a provided factory function,
   * allowing for flexible configuration of voice types and parameters.
   * Uses a simple allocation strategy suitable for testing and basic use cases.
   * 
   * Contract: 
   * - Outside the constructor and destructor, no dynamic memory allocation happens. This is to
   *   ensure smooth real-time audio processing.
   */
  class SimpleVoiceAllocator : public SynthVoiceAllocator {
  private:
    /**
     * @brief Internal structure to track voice allocation state
     */
    struct AllocatedVoice {
        std::unique_ptr<Synth> synth;
        uint8_t assignedNote = 0;        // MIDI note this voice is assigned to
        bool isAllocated = false;        // Whether this voice is currently assigned to a note
        
        AllocatedVoice(std::unique_ptr<Synth> voice) : synth(std::move(voice)) {}
        
        // Move-only semantics
        AllocatedVoice(const AllocatedVoice&) = delete;
        AllocatedVoice& operator=(const AllocatedVoice&) = delete;
        AllocatedVoice(AllocatedVoice&&) = default;
        AllocatedVoice& operator=(AllocatedVoice&&) = default;
    };
    
  public:
    using VoiceFactory = std::function<std::unique_ptr<Synth>()>;
    
    /**
     * @brief Construct allocator with a voice factory
     * @param maxVoices Maximum number of voices to allocate
     * @param factory Function that creates new Synth instances
     */
    SimpleVoiceAllocator(uint8_t maxVoices, VoiceFactory factory)
        : SynthVoiceAllocator(maxVoices)
        , voiceFactory(factory) {
        voices.reserve(maxVoices);
        
        // Pre-cache all voices to avoid dynamic allocation during real-time operation
        for (uint8_t i = 0; i < maxVoices; ++i) {
            voices.emplace_back(voiceFactory());
        }
    }
    
    // Rule of 5: Follow base class design (move-only)
    virtual ~SimpleVoiceAllocator() = default;
    SimpleVoiceAllocator(const SimpleVoiceAllocator&) = delete;
    SimpleVoiceAllocator& operator=(const SimpleVoiceAllocator&) = delete;
    SimpleVoiceAllocator(SimpleVoiceAllocator&&) = default;
    SimpleVoiceAllocator& operator=(SimpleVoiceAllocator&&) = default;
    
    /**
     * @brief Get a synthesizer voice for the specified MIDI note
     * @param midiNote MIDI note number (0-127)
     * @return Reference to the assigned voice
     * 
     * Contract: 
     * - Same MIDI note always returns the same voice instance until released or stolen due to reallocation
     * - When maxVoices is exceeded, voices are reused in round-robin fashion
     * - Reused voices are automatically released (inactive state) before reassignment
     * - The returned voice is ready for trigger() to be called
     */
    Synth& voiceFor(uint8_t midiNote) override {
        // Check if we already have a voice allocated for this MIDI note
        for (auto& voice : voices) {
            if (voice.isAllocated && voice.assignedNote == midiNote) {
                return *voice.synth;
            }
        }
        
        // Find an unallocated voice first
        for (auto& voice : voices) {
            if (!voice.isAllocated) {
                voice.assignedNote = midiNote;
                voice.isAllocated = true;
                return *voice.synth;
            }
        }
        
        // All voices are allocated. Reuse one (round-robin).
        size_t voiceIndex = (lastAllocatedIndex + 1) % voices.size();
        lastAllocatedIndex = voiceIndex;
        
        AllocatedVoice& voice = voices[voiceIndex];
        
        // Release the voice to clean up its state before reassignment
        voice.synth->release();
        
        // Reassign to new note
        voice.assignedNote = midiNote;
        voice.isAllocated = true;
        
        return *voice.synth;
    }
    
    void forEachVoice(std::function<void(Synth&)> func) override {
        for (auto& voice : voices) {
            func(*voice.synth);
        }
    }
    
  private:
    VoiceFactory voiceFactory;
    std::vector<AllocatedVoice> voices;
    size_t lastAllocatedIndex = 0;
  };

} // namespace midi

#endif // SIMPLE_VOICE_ALLOCATOR_H
