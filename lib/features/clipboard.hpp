#pragma once

#include <synth_voice_allocator.hpp>
#include <cstdint>

namespace features {

class ProgramStorage;

/**
 * @brief Interface for preset clipboard functionality
 * 
 * Provides copy/paste operations for synth presets.
 * Platform-specific implementations handle the actual storage mechanism.
 */
class Clipboard {
public:
    virtual ~Clipboard() = default;
    
    /**
     * @brief Copy current voice settings to clipboard
     */
    virtual void copy(midi::SynthVoiceAllocator& allocator) = 0;
    
    /**
     * @brief Paste clipboard contents to voices
     * @return true if clipboard had data to paste
     */
    virtual bool paste(midi::SynthVoiceAllocator& allocator) = 0;
    
    /**
     * @brief Paste clipboard and save to program file
     * @param program Program number to save to
     * @param storage Program storage implementation to use for saving
     * @return true if successful
     */
    virtual bool pasteAndSave(midi::SynthVoiceAllocator& allocator, uint8_t program, ProgramStorage& storage) = 0;
    
    /**
     * @brief Check if clipboard has data
     */
    virtual bool hasData() const = 0;
};

} // namespace features
