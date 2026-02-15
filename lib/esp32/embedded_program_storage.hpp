#pragma once

#include "program_storage.hpp"
#include <sawtooth_synth.hpp>
#include <biquad_filter.hpp>
#include <program_data.hpp>
#include <log.hpp>

namespace esp32 {

/**
 * @brief Embedded program storage for platforms without filesystem
 * 
 * Provides a single hardcoded default program.
 * Used on embedded platforms like ESP32.
 */
class EmbeddedProgramStorage : public features::ProgramStorage {
public:
    EmbeddedProgramStorage() = default;
    
    bool loadProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) override {
        // All program numbers load the same embedded default
        midi::ProgramData programData = getDefaultProgram();
        midi::applyProgramToVoices(programData, allocator);
        logInfo("Loaded embedded default program (requested program %d)", program);
        return true;
    }
    
    bool saveProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) override {
        // Saving not supported on embedded platforms
        logWarn("Program save not supported on this platform (program %d)", program);
        return false;
    }
    
private:
    midi::ProgramData getDefaultProgram() {
        // Default program: Matches program_2.json
        midi::ProgramData p;
        p.waveformShape = 0.0f;  // Pure sawtooth
        p.baseCutoff = 222.0530242919922f;
        p.filterQ = 3.9370079040527344f;
        p.filterMode = 0;  // LOWPASS
        p.filterEnvAmount = 0.5f;
        p.filterEnvAttack = 0.06399212777614594f;
        p.filterEnvDecay = 0.24622048437595367f;
        p.filterEnvSustain = 0.023622047156095505f;
        p.filterEnvRelease = 0.3249606192111969f;
        return p;
    }
};

} // namespace esp32
