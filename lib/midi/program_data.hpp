#ifndef PROGRAM_DATA_HPP
#define PROGRAM_DATA_HPP

#include <biquad_filter.hpp>
#include <sawtooth_synth.hpp>
#include <synth_voice_allocator.hpp>
#include <cstdint>
#include <json.hpp>  // nlohmann/json single-header

namespace midi {

/**
 * @brief Program data structure for synth presets
 * 
 * Serialization to/from JSON is defined via free functions.
 * Storage implementations (elsewhere) handle actual file/memory operations.
 * 
 * When adding new parameters:
 * 1. Add member variable with default value
 * 2. Add to to_json() function
 * 3. Add to from_json() function with .value() for backward compatibility
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
     * @brief Capture current synth settings from voice allocator
     */
    void captureFromVoices(SynthVoiceAllocator& allocator) {
        bool captured = false;
        allocator.forEachVoice([&](Synth& voice) {
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
};

/**
 * @brief Apply program data to all voices in allocator
 */
inline void applyProgramToVoices(const ProgramData& program, SynthVoiceAllocator& allocator) {
    allocator.forEachVoice([&program](Synth& voice) {
        auto& ws = static_cast<synth::WavetableSynth&>(voice);
        
        // Apply oscillator settings
        ws.getOscillator().updateWavetable(program.waveformShape);
        
        // Apply filter settings
        ws.setBaseCutoff(program.baseCutoff);
        ws.getFilter().setQ(program.filterQ);
        ws.getFilter().setMode(static_cast<synth::BiquadFilter::Mode>(program.filterMode));
        
        // Apply filter envelope settings
        ws.setFilterEnvelopeAmount(program.filterEnvAmount);
        ws.getFilterEnvelope().setAttackTime(program.filterEnvAttack);
        ws.getFilterEnvelope().setDecayTime(program.filterEnvDecay);
        ws.getFilterEnvelope().setSustainLevel(program.filterEnvSustain);
        ws.getFilterEnvelope().setReleaseTime(program.filterEnvRelease);
    });
}

// JSON serialization functions (must be in same namespace as ProgramData)
inline void to_json(nlohmann::json& j, const ProgramData& p) {
    j = nlohmann::json{
        {"waveformShape", p.waveformShape},
        {"baseCutoff", p.baseCutoff},
        {"filterQ", p.filterQ},
        {"filterMode", p.filterMode},
        {"filterEnvAmount", p.filterEnvAmount},
        {"filterEnvAttack", p.filterEnvAttack},
        {"filterEnvDecay", p.filterEnvDecay},
        {"filterEnvSustain", p.filterEnvSustain},
        {"filterEnvRelease", p.filterEnvRelease}
    };
}

inline void from_json(const nlohmann::json& j, ProgramData& p) {
    // Use value() with defaults for backward compatibility
    // If a field is missing in the JSON, the default value is used
    p.waveformShape = j.value("waveformShape", 0.0f);
    p.baseCutoff = j.value("baseCutoff", 1000.0f);
    p.filterQ = j.value("filterQ", 0.707f);
    p.filterMode = j.value("filterMode", 0);
    p.filterEnvAmount = j.value("filterEnvAmount", 0.5f);
    p.filterEnvAttack = j.value("filterEnvAttack", 0.005f);
    p.filterEnvDecay = j.value("filterEnvDecay", 0.2f);
    p.filterEnvSustain = j.value("filterEnvSustain", 0.3f);
    p.filterEnvRelease = j.value("filterEnvRelease", 0.1f);
}

} // namespace midi

#endif // PROGRAM_DATA_HPP
