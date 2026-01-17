#ifndef PROGRAM_DATA_HPP
#define PROGRAM_DATA_HPP

#include <biquad_filter.hpp>
#include <sawtooth_synth.hpp>
#include <synth_voice_allocator.hpp>
#include <json.hpp>  // nlohmann/json single-header
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <errno.h>

namespace midi {

/**
 * @brief Program data structure for saving/loading synth presets
 * 
 * Uses nlohmann/json for automatic serialization/deserialization.
 * Add new parameters as struct members and to NLOHMANN_DEFINE_TYPE_INTRUSIVE macro.
 */
struct ProgramData {
    // Oscillator
    float waveformShape = 0.0f;
    
    // Filter
    float baseCutoff = 1000.0f;
    float filterQ = 0.707f;
    int filterMode = 0;  // Stored as int for JSON compatibility
    
    // Filter envelope
    float filterEnvAmount = 0.5f;
    float filterEnvAttack = 0.005f;
    float filterEnvDecay = 0.2f;
    float filterEnvSustain = 0.3f;
    float filterEnvRelease = 0.1f;
    
    /**
     * @brief Save program data to JSON file
     * @param programNumber Program number (0-127)
     * @param bankNumber Bank number (default 0)
     * @return true if successful
     */
    bool saveToFile(uint8_t programNumber, uint8_t bankNumber = 0) const {
        char dirPath[200];
        snprintf(dirPath, sizeof(dirPath), "patches/bank_%d", bankNumber);
        
        // Create directories if they don't exist
        if (mkdir("patches", 0755) == -1 && errno != EEXIST) {
            std::perror("Failed to create patches directory");
            return false;
        }
        if (mkdir(dirPath, 0755) == -1 && errno != EEXIST) {
            std::perror("Failed to create bank directory");
            return false;
        }
        
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "%s/program_%d.json", dirPath, programNumber);
        
        try {
            nlohmann::json j = *this;  // Automatic conversion
            std::ofstream file(filePath);
            if (!file.is_open()) {
                std::perror("Failed to open file for writing");
                return false;
            }
            file << j.dump(2);  // Pretty print with 2-space indent
            file.close();
            std::printf("Saved program %d to %s\n", programNumber, filePath);
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Error saving program: %s\n", e.what());
            return false;
        }
    }
    
    /**
     * @brief Load program data from JSON file
     * @param programNumber Program number (0-127)
     * @param bankNumber Bank number (default 0)
     * @return true if successful (file exists and was parsed)
     */
    bool loadFromFile(uint8_t programNumber, uint8_t bankNumber = 0) {
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "patches/bank_%d/program_%d.json", 
                 bankNumber, programNumber);
        
        try {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                return false;  // File doesn't exist - use defaults
            }
            
            nlohmann::json j;
            file >> j;
            *this = j.get<ProgramData>();  // Automatic conversion
            
            std::printf("Loaded program %d from %s\n", programNumber, filePath);
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Error loading program %d: %s\n", programNumber, e.what());
            return false;
        }
    }
    
    /**
     * @brief Apply program data to all voices
     * @param allocator Voice allocator containing synth voices
     */
    void applyToVoices(SynthVoiceAllocator& allocator) const {
        std::printf("Applying: wave=%.3f cutoff=%.1f Q=%.3f mode=%d envAmt=%.3f\n",
            waveformShape, baseCutoff, filterQ, filterMode, filterEnvAmount);
        
        allocator.forEachVoice([this](Synth& voice) {
            auto& ws = static_cast<synth::WavetableSynth&>(voice);
            ws.getOscillator().updateWavetable(waveformShape);
            ws.setBaseCutoff(baseCutoff);
            ws.getFilter().setQ(filterQ);
            ws.getFilter().setMode(static_cast<synth::BiquadFilter::Mode>(filterMode));
            ws.setFilterEnvelopeAmount(filterEnvAmount);
            ws.getFilterEnvelope().setAttackTime(filterEnvAttack);
            ws.getFilterEnvelope().setDecayTime(filterEnvDecay);
            ws.getFilterEnvelope().setSustainLevel(filterEnvSustain);
            ws.getFilterEnvelope().setReleaseTime(filterEnvRelease);
        });
    }
    
    /**
     * @brief Capture current settings from voices
     * @param allocator Voice allocator containing synth voices
     */
    void captureFromVoices(SynthVoiceAllocator& allocator) {
        // Sample parameters from first voice
        bool captured = false;
        allocator.forEachVoice([this, &captured](Synth& voice) {
            if (!captured) {
                auto& ws = static_cast<synth::WavetableSynth&>(voice);
                waveformShape = ws.getOscillator().getShape();
                baseCutoff = ws.getBaseCutoff();
                filterQ = ws.getFilter().getQ();
                filterMode = static_cast<int>(ws.getFilter().getMode());
                filterEnvAmount = ws.getFilterEnvelopeAmount();
                filterEnvAttack = ws.getFilterEnvelope().getAttackTime();
                filterEnvDecay = ws.getFilterEnvelope().getDecayTime();
                filterEnvSustain = ws.getFilterEnvelope().getSustainLevel();
                filterEnvRelease = ws.getFilterEnvelope().getReleaseTime();
                captured = true;
            }
        });
    }
    
    // Define JSON serialization using nlohmann macro (must be in same namespace)
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProgramData,
        waveformShape,
        baseCutoff,
        filterQ,
        filterMode,
        filterEnvAmount,
        filterEnvAttack,
        filterEnvDecay,
        filterEnvSustain,
        filterEnvRelease
    )
};

} // namespace midi

#endif // PROGRAM_DATA_HPP
