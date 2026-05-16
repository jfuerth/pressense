#pragma once

#include <clipboard.hpp>
#include <program_storage.hpp>
#include <program_data.hpp>
#include <log.hpp>

namespace linux {

/**
 * @brief Linux clipboard implementation for copying/pasting synth presets
 * 
 * Provides in-memory storage for a single preset that can be copied
 * from and pasted to voice allocators.
 */
class PresetClipboard : public features::Clipboard {
public:
    PresetClipboard() = default;
    
    /**
     * @brief Copy current voice settings to clipboard
     */
    void copy(VoiceIterator forEachVoice) override {
        clipboard_.captureFromVoices(forEachVoice);
        hasData_ = true;
        logInfo("Copied current settings to clipboard");
    }
    
    /**
     * @brief Paste clipboard contents to voices
     * @return true if clipboard had data to paste
     */
    bool paste(VoiceIterator forEachVoice) override {
        if (!hasData_) {
            logWarn("Clipboard is empty");
            return false;
        }
        
        midi::applyProgramToVoices(clipboard_, forEachVoice);
        logInfo("Pasted clipboard to voices");
        return true;
    }
    
    /**
     * @brief Paste clipboard and save to program file
     * @param forEachVoice Function to iterate all voices
     * @param program Program number to save to
     * @param storage Program storage implementation to use for saving
     * @return true if successful
     */
    bool pasteAndSave(VoiceIterator forEachVoice, uint8_t program, features::ProgramStorage& storage) override {
        if (!paste(forEachVoice)) {
            return false;
        }
        
        return storage.saveProgram(program, forEachVoice);
    }
    
    bool hasData() const override { return hasData_; }
    
private:
    midi::ProgramData clipboard_;
    bool hasData_ = false;
};

} // namespace linux
