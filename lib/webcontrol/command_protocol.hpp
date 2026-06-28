#pragma once

#include <json.hpp>
#include <string>

namespace webcontrol {

/**
 * @brief Command types for the control panel protocol
 * 
 * Commands are sent from the HTML control panel to the device.
 * Responses are sent back from the device to confirm receipt.
 */
enum class CommandType {
    SET_PARAM,      // Set a single parameter: {"cmd": "setParam", "param": "baseCutoff", "value": 500.0}
    GET_PARAMS,     // Request all current parameters: {"cmd": "getParams"}
    SAVE_PROGRAM,   // Save current settings to program slot: {"cmd": "saveProgram", "bank": 0, "program": 1}
    LOAD_PROGRAM,   // Load settings from program slot: {"cmd": "loadProgram", "bank": 0, "program": 1}
    SET_BASE_NOTE,  // Set keyboard base note: {"cmd": "setBaseNote", "note": 48}
    UNKNOWN
};

/**
 * @brief Parse command type from string
 */
inline CommandType parseCommandType(const std::string& cmd) {
    if (cmd == "setParam") return CommandType::SET_PARAM;
    if (cmd == "getParams") return CommandType::GET_PARAMS;
    if (cmd == "saveProgram") return CommandType::SAVE_PROGRAM;
    if (cmd == "loadProgram") return CommandType::LOAD_PROGRAM;
    if (cmd == "setBaseNote") return CommandType::SET_BASE_NOTE;
    return CommandType::UNKNOWN;
}

/**
 * @brief Response sent back to control panel
 */
struct CommandResponse {
    std::string ack;      // Command that was acknowledged
    std::string status;   // "ok" or "error"
    std::string error;    // Error message if status is "error"
};

// JSON serialization for CommandResponse
inline void to_json(nlohmann::json& j, const CommandResponse& r) {
    j = nlohmann::json{
        {"type", "cmdResponse"},
        {"ack", r.ack},
        {"status", r.status}
    };
    if (!r.error.empty()) {
        j["error"] = r.error;
    }
}

/**
 * @brief Params telemetry sent to control panel
 * 
 * Sent in response to getParams command or when parameters change.
 */
struct ParamsTelemetry {
    // Oscillator
    float waveformShape;
    
    // Filter
    float baseCutoff;
    float filterQ;
    int filterMode;
    
    // Filter envelope
    float filterEnvAmount;
    float filterEnvAttack;
    float filterEnvDecay;
    float filterEnvSustain;
    float filterEnvRelease;
    
    // Amplitude envelope
    float ampEnvAttack;
    float ampEnvDecay;
    float ampEnvSustain;
    float ampEnvRelease;
    
    // Vibrato
    float vibratoRate;
    float vibratoDepth;
    
    // Tremolo
    float tremoloRate;
    float tremoloDepth;
    
    // Aftertouch modulation
    float baseCutoff_atMod;
    float filterEnvAmount_atMod;
    float vibratoDepth_atMod;
    float tremoloDepth_atMod;

    // Aftertouch input mapping (keyboard pressure ratio -> 0..127)
    float aftertouchMinRatio;
    float aftertouchMaxRatio;
};

inline void to_json(nlohmann::json& j, const ParamsTelemetry& p) {
    j = nlohmann::json{
        {"type", "params"},
        {"waveformShape", p.waveformShape},
        {"baseCutoff", p.baseCutoff},
        {"filterQ", p.filterQ},
        {"filterMode", p.filterMode},
        {"filterEnvAmount", p.filterEnvAmount},
        {"filterEnvAttack", p.filterEnvAttack},
        {"filterEnvDecay", p.filterEnvDecay},
        {"filterEnvSustain", p.filterEnvSustain},
        {"filterEnvRelease", p.filterEnvRelease},
        {"ampEnvAttack", p.ampEnvAttack},
        {"ampEnvDecay", p.ampEnvDecay},
        {"ampEnvSustain", p.ampEnvSustain},
        {"ampEnvRelease", p.ampEnvRelease},
        {"vibratoRate", p.vibratoRate},
        {"vibratoDepth", p.vibratoDepth},
        {"tremoloRate", p.tremoloRate},
        {"tremoloDepth", p.tremoloDepth},
        {"baseCutoff_atMod", p.baseCutoff_atMod},
        {"filterEnvAmount_atMod", p.filterEnvAmount_atMod},
        {"vibratoDepth_atMod", p.vibratoDepth_atMod},
        {"tremoloDepth_atMod", p.tremoloDepth_atMod},
        {"aftertouchMinRatio", p.aftertouchMinRatio},
        {"aftertouchMaxRatio", p.aftertouchMaxRatio}
    };
}

} // namespace webcontrol
