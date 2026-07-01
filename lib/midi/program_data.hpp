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
 * 1. Add a member variable with its default value (the single source of truth
 *    for that parameter's default)
 * 2. Add the member name to the NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT
 *    list below - JSON (de)serialization and default-if-missing are automatic
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

// JSON serialization (must be in the same namespace as ProgramData).
//
// to_json/from_json are generated from the field list below. On read, any field
// missing from the JSON falls back to a default-constructed ProgramData's value,
// so the struct's member initializers above are the single source of truth for
// defaults and older patches stay forward-compatible as new params are added.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProgramData,
    waveformShape,
    baseCutoff, filterQ, filterMode,
    filterEnvAmount, filterEnvAttack, filterEnvDecay, filterEnvSustain, filterEnvRelease,
    ampEnvAttack, ampEnvDecay, ampEnvSustain, ampEnvRelease,
    vibratoRate, vibratoDepth,
    tremoloRate, tremoloDepth,
    baseCutoff_atMod, filterEnvAmount_atMod, vibratoDepth_atMod, tremoloDepth_atMod)

} // namespace midi

