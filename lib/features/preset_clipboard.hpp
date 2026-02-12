#pragma once

#include <program_data.hpp>
#include <synth_voice_allocator.hpp>
#include <log.hpp>

namespace features {

/**
 * @brief Clipboard for copying/pasting synth presets
 * 
 * Provides in-memory storage for a single preset that can be copied
 * from and pasted to voice allocators.
 */
class PresetClipboard {
public:
    PresetClipboard() = default;
    
    /**
     * @brief Copy current voice settings to clipboard
     */
    void copy(midi::SynthVoiceAllocator& allocator) {
        clipboard_.captureFromVoices(allocator);
        hasData_ = true;
        logInfo("Copied current settings to clipboard");
    }
    
    /**
     * @brief Paste clipboard contents to voices
     * @return true if clipboard had data to paste
     */
    bool paste(midi::SynthVoiceAllocator& allocator) {
        if (!hasData_) {
            logWarn("Clipboard is empty");
            return false;
        }
        
        midi::applyProgramToVoices(clipboard_, allocator);
        logInfo("Pasted clipboard to voices");
        return true;
    }
    
    /**
     * @brief Paste clipboard and save to program file
     * @param program Program number to save to
     * @param storage Program storage implementation to use for saving
     * @return true if successful
     */
    bool pasteAndSave(midi::SynthVoiceAllocator& allocator, uint8_t program, ProgramStorage& storage) {
        if (!paste(allocator)) {
            return false;
        }
        
        return storage.saveProgram(program, allocator);
    }
    
    bool hasData() const { return hasData_; }
    
private:
    midi::ProgramData clipboard_;
    bool hasData_ = false;
};

} // namespace features
