#pragma once

#include <program_storage.hpp>
#include <cstdint>

namespace features {

/**
 * @brief Interface for preset clipboard functionality
 * 
 * Provides copy/paste operations for synth presets.
 * Platform-specific implementations handle the actual storage mechanism.
 */
class Clipboard {
public:
    using VoiceVisitor = ProgramStorage::VoiceVisitor;
    using VoiceIterator = ProgramStorage::VoiceIterator;
    
    virtual ~Clipboard() = default;
    
    /**
     * @brief Copy current voice settings to clipboard
     * @param forEachVoice Function to iterate all voices
     */
    virtual void copy(VoiceIterator forEachVoice) = 0;
    
    /**
     * @brief Paste clipboard contents to voices
     * @param forEachVoice Function to iterate all voices
     * @return true if clipboard had data to paste
     */
    virtual bool paste(VoiceIterator forEachVoice) = 0;
    
    /**
     * @brief Paste clipboard and save to program file
     * @param forEachVoice Function to iterate all voices
     * @param program Program number to save to
     * @param storage Program storage implementation to use for saving
     * @return true if successful
     */
    virtual bool pasteAndSave(VoiceIterator forEachVoice, uint8_t program, ProgramStorage& storage) = 0;
    
    /**
     * @brief Check if clipboard has data
     */
    virtual bool hasData() const = 0;
};

} // namespace features
