#pragma once

#include "program_storage.hpp"
#include <program_data.hpp>
#include <log.hpp>
#include <fstream>
#include <sys/stat.h>
#include <errno.h>
#include <cstdio>

namespace features {

/**
 * @brief Filesystem-based program storage implementation
 * 
 * Stores programs as JSON files in the patches/ directory.
 * Only available on platforms with filesystem support.
 */
class FilesystemProgramStorage : public ProgramStorage {
public:
    FilesystemProgramStorage(const char* basePath = "patches")
        : basePath_(basePath) {}
    
    bool loadProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) override {
        midi::ProgramData programData;
        
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "%s/bank_0/program_%d.json", basePath_, program);
        
        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                // File doesn't exist - use defaults
                logInfo("Program %d not found, using defaults", program);
                midi::applyProgramToVoices(programData, allocator);
                return false;
            }
            
            nlohmann::json j;
            file >> j;
            programData = j.get<midi::ProgramData>();
            
            midi::applyProgramToVoices(programData, allocator);
            logInfo("Loaded program %d from %s", program, filePath);
            return true;
        } catch (const std::exception& e) {
            logError("Error loading program %d: %s", program, e.what());
            // Apply defaults on error
            midi::applyProgramToVoices(programData, allocator);
            return false;
        }
    }
    
    bool saveProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) override {
        midi::ProgramData programData;
        programData.captureFromVoices(allocator);
        
        // Create directories if needed
        if (!ensureDirectoryExists(basePath_)) {
            return false;
        }
        
        char bankPath[256];
        snprintf(bankPath, sizeof(bankPath), "%s/bank_0", basePath_);
        if (!ensureDirectoryExists(bankPath)) {
            return false;
        }
        
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "%s/program_%d.json", bankPath, program);
        
        try {
            nlohmann::json j = programData;
            std::ofstream file(filePath);
            if (!file.is_open()) {
                logError("Failed to open %s for writing", filePath);
                return false;
            }
            file << j.dump(2);  // Pretty print with 2-space indent
            file.close();
            logInfo("Saved program %d to %s", program, filePath);
            return true;
        } catch (const std::exception& e) {
            logError("Error saving program %d: %s", program, e.what());
            return false;
        }
    }
    
private:
    const char* basePath_;
    
    bool ensureDirectoryExists(const char* path) {
        struct stat st;
        if (stat(path, &st) == 0) {
            return true;  // Already exists
        }
        
        if (mkdir(path, 0755) == 0 || errno == EEXIST) {
            return true;
        }
        
        logError("Failed to create directory %s: %s", path, strerror(errno));
        return false;
    }
};

} // namespace features
