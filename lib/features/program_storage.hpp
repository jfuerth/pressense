#pragma once

#include <synth_voice_allocator.hpp>
#include <cstdint>

namespace features {

/**
 * @brief Interface for program storage and retrieval
 * 
 * This interface abstracts the ability to save and load synth presets.
 * Platform-specific implementations handle the actual storage mechanism
 * (filesystem, SPIFFS, hardcoded defaults, etc.)
 */
class ProgramStorage {
public:
    virtual ~ProgramStorage() = default;
    
    /**
     * @brief Load a program and apply it to the voice allocator
     * @param program Program number (0-127)
     * @param allocator Voice allocator to configure
     * @return true if program was loaded successfully
     */
    virtual bool loadProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) = 0;
    
    /**
     * @brief Save current voice settings as a program
     * @param program Program number (0-127)
     * @param allocator Voice allocator to capture settings from
     * @return true if program was saved successfully
     */
    virtual bool saveProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) = 0;
};

} // namespace features
