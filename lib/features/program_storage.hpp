#pragma once

#include <cstdint>
#include <functional>

namespace synth {
    class Voice;  // Forward declaration
}

namespace features {

/**
 * @brief Interface for program storage and retrieval
 * 
 * This interface abstracts the ability to save and load synth presets.
 * Platform-specific implementations handle the actual storage mechanism
 * (filesystem, SPIFFS, hardcoded defaults, etc.)
 * 
 * Uses a callback-based approach to iterate voices, avoiding tight coupling
 * to any specific voice pool implementation.
 * 
 * TODO: there is still an API problem with this approach: the storage
 * interface needs to know how to capture settings from voices and apply
 * settings to voices, which means it needs to know about the specific voice
 * implementation (e.g., WavetableSynth). This creates coupling between the
 * storage interface and the synth implementation. A more decoupled design
 * would separate the concerns of storage format and voice manipulation,
 * perhaps by defining a separate "Program" abstraction that can be converted
 * to/from voice settings without the storage layer needing to know about the
 * voice implementation. Or we just double down on JSON as a storage format,
 * and require that Voice implementations can serialize/deserialize themselves
 * to/from JSON. Then the storage interface only needs to deal with JSON storage
 * and retrieval.
 */
class ProgramStorage {
public:
    using VoiceVisitor = std::function<void(synth::Voice&)>;
    using VoiceIterator = std::function<void(VoiceVisitor)>;
    
    virtual ~ProgramStorage() = default;
    
    /**
     * @brief Load a program and apply it to voices
     * @param program Program number (0-127)
     * @param forEachVoice Function to iterate all voices
     * @return true if program was loaded successfully
     */
    virtual bool loadProgram(uint8_t program, VoiceIterator forEachVoice) = 0;
    
    /**
     * @brief Save current voice settings as a program
     * @param program Program number (0-127)
     * @param forEachVoice Function to iterate all voices for reading settings
     * @return true if program was saved successfully
     */
    virtual bool saveProgram(uint8_t program, VoiceIterator forEachVoice) = 0;
};

} // namespace features
