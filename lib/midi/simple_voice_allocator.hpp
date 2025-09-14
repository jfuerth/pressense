#ifndef SIMPLE_VOICE_ALLOCATOR_H
#define SIMPLE_VOICE_ALLOCATOR_H

#include "synth_voice_allocator.hpp"
#include "synth.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

namespace midi {

  /**
   * @brief A simple concrete SynthVoiceAllocator that uses a factory function to create voices
   * 
   * This allocator creates voices on-demand using a provided factory function,
   * allowing for flexible configuration of voice types and parameters.
   * Uses a simple allocation strategy suitable for testing and basic use cases.
   */
  class SimpleVoiceAllocator : public SynthVoiceAllocator {
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
    }
    
    // Rule of 5: Follow base class design (move-only)
    virtual ~SimpleVoiceAllocator() = default;
    SimpleVoiceAllocator(const SimpleVoiceAllocator&) = delete;
    SimpleVoiceAllocator& operator=(const SimpleVoiceAllocator&) = delete;
    SimpleVoiceAllocator(SimpleVoiceAllocator&&) = default;
    SimpleVoiceAllocator& operator=(SimpleVoiceAllocator&&) = default;
    
    /**
     * @brief Get a new or existing synthesizer voice for the specified MIDI note
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
        // Check if we already have a voice for this MIDI note
        auto existingVoice = noteToVoiceMap.find(midiNote);
        if (existingVoice != noteToVoiceMap.end()) {
            return *voices[existingVoice->second];
        }
        
        // Need to allocate a new voice
        size_t voiceIndex;
        if (voices.size() < voices.capacity()) {
            // Create a new voice
            voices.push_back(voiceFactory());
            voiceIndex = voices.size() - 1;
        } else {
            // Reuse an existing voice (round-robin)
            voiceIndex = (lastAllocatedIndex + 1) % voices.capacity();
            lastAllocatedIndex = voiceIndex;
            
            // Remove the old mapping for this voice and release it
            for (auto it = noteToVoiceMap.begin(); it != noteToVoiceMap.end(); ++it) {
                if (it->second == voiceIndex) {
                    noteToVoiceMap.erase(it);
                    break;
                }
            }
            
            // Release the voice to clean up its state before reassignment
            voices[voiceIndex]->release();
        }
        
        // Map this MIDI note to the voice
        noteToVoiceMap[midiNote] = voiceIndex;
        return *voices[voiceIndex];
    }
    
    void forEachVoice(std::function<void(Synth&)> func) override {
        for (auto& voice : voices) {
            func(*voice);
        }
    }
    
  private:
    VoiceFactory voiceFactory;
    std::vector<std::unique_ptr<Synth>> voices;
    std::unordered_map<uint8_t, size_t> noteToVoiceMap; // MIDI note -> voice index
    size_t lastAllocatedIndex = 0;
  };

} // namespace midi

#endif // SIMPLE_VOICE_ALLOCATOR_H
