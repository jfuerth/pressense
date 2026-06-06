#pragma once

#include <biquad_filter.hpp>
#include <sawtooth_synth.hpp>
#include <voice.hpp>
#include <cstdint>
#include <functional>
#include <json.hpp>  // nlohmann/json single-header

namespace midi {

// Type aliases matching features::ProgramStorage
using VoiceVisitor = std::function<void(synth::Voice&)>;
using VoiceIterator = std::function<void(VoiceVisitor)>;

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
    
    // Amplitude envelope
    float ampEnvAttack = 0.01f;
    float ampEnvDecay = 0.05f;
    float ampEnvSustain = 0.7f;
    float ampEnvRelease = 0.1f;
    
    // Vibrato (pitch modulation)
    float vibratoRate = 5.0f;    // Hz
    float vibratoDepth = 0.0f;   // Semitones (0 = off)
    
    // Tremolo (amplitude modulation)
    float tremoloRate = 5.0f;    // Hz
    float tremoloDepth = 0.0f;   // 0-1 (0 = off)
    
    // Aftertouch modulation amounts [-1.0, 1.0]
    // 0 = no effect, positive = increase with pressure, negative = decrease
    float baseCutoff_atMod = 0.0f;
    float filterEnvAmount_atMod = 0.0f;
    float vibratoDepth_atMod = 0.0f;
    float tremoloDepth_atMod = 0.0f;
    
    /**
     * @brief Capture current synth settings from voices
     * @param forEachVoice Function to iterate all voices
     */
    void captureFromVoices(VoiceIterator forEachVoice) {
        bool captured = false;
        forEachVoice([&](synth::Voice& voice) {
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
                // Amplitude envelope
                ampEnvAttack = ws.getAmpEnvelope().getAttackTime();
                ampEnvDecay = ws.getAmpEnvelope().getDecayTime();
                ampEnvSustain = ws.getAmpEnvelope().getSustainLevel();
                ampEnvRelease = ws.getAmpEnvelope().getReleaseTime();
                // Vibrato
                vibratoRate = ws.getVibratoRate();
                vibratoDepth = ws.getVibratoDepth();
                // Tremolo
                tremoloRate = ws.getTremoloRate();
                tremoloDepth = ws.getTremoloDepth();
                // Aftertouch modulation
                baseCutoff_atMod = ws.getBaseCutoffAtMod();
                filterEnvAmount_atMod = ws.getFilterEnvAmountAtMod();
                vibratoDepth_atMod = ws.getVibratoDepthAtMod();
                tremoloDepth_atMod = ws.getTremoloDepthAtMod();
                captured = true;
            }
        });
    }
};

/**
 * @brief Apply program data to all voices
 * @param program Program data to apply
 * @param forEachVoice Function to iterate all voices
 */
inline void applyProgramToVoices(const ProgramData& program, VoiceIterator forEachVoice) {
    forEachVoice([&program](synth::Voice& voice) {
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
        
        // Apply amplitude envelope settings
        ws.getAmpEnvelope().setAttackTime(program.ampEnvAttack);
        ws.getAmpEnvelope().setDecayTime(program.ampEnvDecay);
        ws.getAmpEnvelope().setSustainLevel(program.ampEnvSustain);
        ws.getAmpEnvelope().setReleaseTime(program.ampEnvRelease);
        
        // Apply vibrato settings
        ws.setVibratoRate(program.vibratoRate);
        ws.setVibratoDepth(program.vibratoDepth);
        
        // Apply tremolo settings
        ws.setTremoloRate(program.tremoloRate);
        ws.setTremoloDepth(program.tremoloDepth);
        
        // Apply aftertouch modulation settings
        ws.setBaseCutoffAtMod(program.baseCutoff_atMod);
        ws.setFilterEnvAmountAtMod(program.filterEnvAmount_atMod);
        ws.setVibratoDepthAtMod(program.vibratoDepth_atMod);
        ws.setTremoloDepthAtMod(program.tremoloDepth_atMod);
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
        {"filterEnvRelease", p.filterEnvRelease},
        // Amplitude envelope
        {"ampEnvAttack", p.ampEnvAttack},
        {"ampEnvDecay", p.ampEnvDecay},
        {"ampEnvSustain", p.ampEnvSustain},
        {"ampEnvRelease", p.ampEnvRelease},
        // Vibrato
        {"vibratoRate", p.vibratoRate},
        {"vibratoDepth", p.vibratoDepth},
        // Tremolo
        {"tremoloRate", p.tremoloRate},
        {"tremoloDepth", p.tremoloDepth},
        // Aftertouch modulation
        {"baseCutoff_atMod", p.baseCutoff_atMod},
        {"filterEnvAmount_atMod", p.filterEnvAmount_atMod},
        {"vibratoDepth_atMod", p.vibratoDepth_atMod},
        {"tremoloDepth_atMod", p.tremoloDepth_atMod}
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
    // Amplitude envelope
    p.ampEnvAttack = j.value("ampEnvAttack", 0.01f);
    p.ampEnvDecay = j.value("ampEnvDecay", 0.05f);
    p.ampEnvSustain = j.value("ampEnvSustain", 0.7f);
    p.ampEnvRelease = j.value("ampEnvRelease", 0.1f);
    // Vibrato
    p.vibratoRate = j.value("vibratoRate", 5.0f);
    p.vibratoDepth = j.value("vibratoDepth", 0.0f);
    // Tremolo
    p.tremoloRate = j.value("tremoloRate", 5.0f);
    p.tremoloDepth = j.value("tremoloDepth", 0.0f);
    // Aftertouch modulation
    p.baseCutoff_atMod = j.value("baseCutoff_atMod", 0.0f);
    p.filterEnvAmount_atMod = j.value("filterEnvAmount_atMod", 0.0f);
    p.vibratoDepth_atMod = j.value("vibratoDepth_atMod", 0.0f);
    p.tremoloDepth_atMod = j.value("tremoloDepth_atMod", 0.0f);
}

} // namespace midi

