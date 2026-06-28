#pragma once

#include "command_protocol.hpp"
#include <json.hpp>
#include <sawtooth_synth.hpp>
#include <program_storage.hpp>
#include <log.hpp>
#include <functional>
#include <string>
#include <cstdint>
#include <cstdio>

namespace webcontrol {

/**
 * @brief Callback type for base note changes
 * 
 * This allows external code (e.g. main) to hook into base note commands
 * since the keyboard controller is not part of WebController.
 */
using SetBaseNoteCallback = std::function<void(uint8_t note)>;

/**
 * @brief Callbacks for the keyboard aftertouch input range
 *
 * These live in the keyboard controller, which (like the base note) is not part
 * of WebController, so they are reached via callbacks supplied by main.
 */
using SetAftertouchRatioCallback = std::function<void(float ratio)>;
using GetAftertouchRangeCallback = std::function<void(float& minRatio, float& maxRatio)>;

/**
 * @brief Type aliases for voice iteration (same pattern as ProgramStorage)
 */
using VoiceVisitor = std::function<void(synth::Voice&)>;
using VoiceIterator = std::function<void(VoiceVisitor)>;

/**
 * @brief Web control panel controller
 * 
 * Parses JSON command lines from the control panel and directly manipulates
 * synth voices. This class knows about synth internals and provides a web
 * interface to control the synthesizer.
 * 
 * Usage:
 *   WebController controller(
 *       [&voicePool](auto visitor) { voicePool.forEachVoice(visitor); },
 *       &programStorage,
 *       [](uint8_t note) { keyboard.setBaseNote(note); }
 *   );
 *   
 *   // In main loop:
 *   if (controller.accumulate(ch)) {
 *       controller.process(controller.getLineBuffer());
 *   }
 */
class WebController {
public:
    /**
     * @brief Construct with voice iterator and optional storage
     * @param voiceIterator Function to iterate all voices
     * @param programStorage Optional program storage for save/load
     * @param onSetBaseNote Optional callback for base note changes
     */
    WebController(
        VoiceIterator voiceIterator,
        features::ProgramStorage* programStorage = nullptr,
        SetBaseNoteCallback onSetBaseNote = nullptr
    ) : voiceIterator_(std::move(voiceIterator)),
        programStorage_(programStorage),
        onSetBaseNote_(std::move(onSetBaseNote)) {}
    
    /**
     * @brief Process a JSON command line
     * @param jsonLine Null-terminated JSON string
     * @return true if command was parsed and dispatched successfully
     */
    bool process(const char* jsonLine) {
        if (!jsonLine || jsonLine[0] == '\0') {
            return false;
        }
        
        // Use non-throwing JSON parse (for embedded targets without exceptions)
        nlohmann::json j = nlohmann::json::parse(jsonLine, nullptr, false);
        
        // Check if parse failed
        if (j.is_discarded()) {
            sendError("JSON parse error");
            return false;
        }
        
        // Get command type
        if (!j.contains("cmd") || !j["cmd"].is_string()) {
            sendError("missing or invalid 'cmd' field");
            return false;
        }
        
        std::string cmdStr = j["cmd"].get<std::string>();
        auto cmdType = parseCommandType(cmdStr);
        
        switch (cmdType) {
            case CommandType::SET_PARAM:
                return handleSetParam(j);
                
            case CommandType::GET_PARAMS:
                return handleGetParams();
                
            case CommandType::SAVE_PROGRAM:
                return handleSaveProgram(j);
                
            case CommandType::LOAD_PROGRAM:
                return handleLoadProgram(j);
                
            case CommandType::SET_BASE_NOTE:
                return handleSetBaseNote(j);
                
            case CommandType::UNKNOWN:
            default:
                sendError("unknown command: " + cmdStr);
                return false;
        }
    }
    
    /**
     * @brief Accumulate a character into the line buffer
     * @param ch Character received
     * @return true if a complete line is ready (newline received)
     */
    bool accumulate(char ch) {
        if (ch == '\n' || ch == '\r') {
            if (bufferLen_ > 0) {
                lineBuffer_[bufferLen_] = '\0';
                bufferLen_ = 0;
                return true;
            }
            return false;
        }
        
        if (bufferLen_ < sizeof(lineBuffer_) - 1) {
            lineBuffer_[bufferLen_++] = ch;
        }
        // If buffer overflows, we'll just truncate
        return false;
    }
    
    /**
     * @brief Get the accumulated line buffer
     * @return Pointer to null-terminated line
     */
    const char* getLineBuffer() const {
        return lineBuffer_;
    }
    
    /**
     * @brief Reset the line buffer
     */
    void resetBuffer() {
        bufferLen_ = 0;
        lineBuffer_[0] = '\0';
    }
    
    /**
     * @brief Set callback for base note changes
     */
    void setBaseNoteCallback(SetBaseNoteCallback callback) {
        onSetBaseNote_ = std::move(callback);
    }

    /**
     * @brief Set callbacks for the keyboard aftertouch input range
     */
    void setAftertouchMinRatioCallback(SetAftertouchRatioCallback callback) {
        onSetAftertouchMinRatio_ = std::move(callback);
    }
    void setAftertouchMaxRatioCallback(SetAftertouchRatioCallback callback) {
        onSetAftertouchMaxRatio_ = std::move(callback);
    }
    void setAftertouchRangeProvider(GetAftertouchRangeCallback callback) {
        onGetAftertouchRange_ = std::move(callback);
    }

private:
    bool handleSetParam(const nlohmann::json& j) {
        if (!j.contains("param") || !j.contains("value")) {
            sendError("setParam requires 'param' and 'value'");
            return false;
        }
        
        std::string param = j["param"].get<std::string>();
        float value = j["value"].get<float>();
        
        setParameter(param, value);
        sendAck("setParam");
        return true;
    }
    
    bool handleGetParams() {
        sendCurrentParams();
        return true;
    }
    
    bool handleSaveProgram(const nlohmann::json& j) {
        uint8_t bank = j.value("bank", 0);
        uint8_t program = j.value("program", 0);
        
        saveProgram(bank, program);
        sendAck("saveProgram");
        return true;
    }
    
    bool handleLoadProgram(const nlohmann::json& j) {
        uint8_t bank = j.value("bank", 0);
        uint8_t program = j.value("program", 0);
        
        loadProgram(bank, program);
        sendAck("loadProgram");
        return true;
    }
    
    bool handleSetBaseNote(const nlohmann::json& j) {
        uint8_t note = j.value("note", 48);  // Default to C3
        
        if (onSetBaseNote_) {
            onSetBaseNote_(note);
        }
        
        sendAck("setBaseNote");
        return true;
    }
    
    /**
     * @brief Set a synth parameter by name
     */
    void setParameter(const std::string& param, float value) {
        // Oscillator
        if (param == "waveformShape") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getOscillator().updateWavetable(value); });
        }
        // Filter
        else if (param == "baseCutoff") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setBaseCutoff(value); });
        }
        else if (param == "filterQ") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getFilter().setQ(value); });
        }
        else if (param == "filterMode") {
            int mode = static_cast<int>(value);
            forEachWavetableSynth([mode](synth::WavetableSynth& v) { v.getFilter().setMode(static_cast<synth::BiquadFilter::Mode>(mode)); });
        }
        // Filter envelope
        else if (param == "filterEnvAmount") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setFilterEnvelopeAmount(value); });
        }
        else if (param == "filterEnvAttack") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getFilterEnvelope().setAttackTime(value); });
        }
        else if (param == "filterEnvDecay") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getFilterEnvelope().setDecayTime(value); });
        }
        else if (param == "filterEnvSustain") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getFilterEnvelope().setSustainLevel(value); });
        }
        else if (param == "filterEnvRelease") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getFilterEnvelope().setReleaseTime(value); });
        }
        // Amp envelope
        else if (param == "ampEnvAttack") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getAmpEnvelope().setAttackTime(value); });
        }
        else if (param == "ampEnvDecay") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getAmpEnvelope().setDecayTime(value); });
        }
        else if (param == "ampEnvSustain") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getAmpEnvelope().setSustainLevel(value); });
        }
        else if (param == "ampEnvRelease") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.getAmpEnvelope().setReleaseTime(value); });
        }
        // Vibrato
        else if (param == "vibratoRate") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setVibratoRate(value); });
        }
        else if (param == "vibratoDepth") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setVibratoDepth(value); });
        }
        // Tremolo
        else if (param == "tremoloRate") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setTremoloRate(value); });
        }
        else if (param == "tremoloDepth") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setTremoloDepth(value); });
        }
        // Aftertouch modulation
        else if (param == "baseCutoff_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setBaseCutoffAtMod(value); });
        }
        else if (param == "filterEnvAmount_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setFilterEnvAmountAtMod(value); });
        }
        else if (param == "vibratoDepth_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setVibratoDepthAtMod(value); });
        }
        else if (param == "tremoloDepth_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setTremoloDepthAtMod(value); });
        }
        // Keyboard aftertouch input range (lives in the keyboard controller)
        else if (param == "aftertouchMinRatio") {
            if (onSetAftertouchMinRatio_) onSetAftertouchMinRatio_(value);
        }
        else if (param == "aftertouchMaxRatio") {
            if (onSetAftertouchMaxRatio_) onSetAftertouchMaxRatio_(value);
        }
        else {
            logWarn("Unknown parameter: %s", param.c_str());
        }
    }
    
    /**
     * @brief Send current parameters as telemetry
     */
    void sendCurrentParams() {
        ParamsTelemetry params;
        bool captured = false;
        forEachWavetableSynth([&params, &captured](synth::WavetableSynth& v) {
            if (!captured) {
                params.waveformShape = v.getOscillator().getShape();
                params.baseCutoff = v.getBaseCutoff();
                params.filterQ = v.getFilter().getQ();
                params.filterMode = static_cast<int>(v.getFilter().getMode());
                params.filterEnvAmount = v.getFilterEnvelopeAmount();
                params.filterEnvAttack = v.getFilterEnvelope().getAttackTime();
                params.filterEnvDecay = v.getFilterEnvelope().getDecayTime();
                params.filterEnvSustain = v.getFilterEnvelope().getSustainLevel();
                params.filterEnvRelease = v.getFilterEnvelope().getReleaseTime();
                params.ampEnvAttack = v.getAmpEnvelope().getAttackTime();
                params.ampEnvDecay = v.getAmpEnvelope().getDecayTime();
                params.ampEnvSustain = v.getAmpEnvelope().getSustainLevel();
                params.ampEnvRelease = v.getAmpEnvelope().getReleaseTime();
                params.vibratoRate = v.getVibratoRate();
                params.vibratoDepth = v.getVibratoDepth();
                params.tremoloRate = v.getTremoloRate();
                params.tremoloDepth = v.getTremoloDepth();
                params.baseCutoff_atMod = v.getBaseCutoffAtMod();
                params.filterEnvAmount_atMod = v.getFilterEnvAmountAtMod();
                params.vibratoDepth_atMod = v.getVibratoDepthAtMod();
                params.tremoloDepth_atMod = v.getTremoloDepthAtMod();
                captured = true;
            }
        });
        // Keyboard aftertouch range comes from the keyboard controller, not the
        // voices. Fallbacks (used when no provider is wired) match the keyboard's
        // compile-time defaults.
        params.aftertouchMinRatio = 5.0f;
        params.aftertouchMaxRatio = 10.0f;
        if (onGetAftertouchRange_) {
            onGetAftertouchRange_(params.aftertouchMinRatio, params.aftertouchMaxRatio);
        }
        nlohmann::json j = params;
        printf("%s\n", j.dump().c_str());
    }
    
    /**
     * @brief Save current settings to program slot
     */
    void saveProgram(uint8_t bank, uint8_t program) {
        if (programStorage_) {
            uint8_t slot = bank * 8 + program;  // Simple slot calculation
            programStorage_->saveProgram(slot, voiceIterator_);
            logInfo("Saved program to bank %d, slot %d", bank, program);
        } else {
            logWarn("No program storage available");
        }
    }
    
    /**
     * @brief Load settings from program slot
     */
    void loadProgram(uint8_t bank, uint8_t program) {
        if (programStorage_) {
            uint8_t slot = bank * 8 + program;  // Simple slot calculation
            programStorage_->loadProgram(slot, voiceIterator_);
            logInfo("Loaded program from bank %d, slot %d", bank, program);
            // Send updated params to control panel
            sendCurrentParams();
        } else {
            logWarn("No program storage available");
        }
    }
    
    void sendAck(const std::string& cmd) {
        CommandResponse resp;
        resp.ack = cmd;
        resp.status = "ok";
        nlohmann::json j = resp;
        printf("%s\n", j.dump().c_str());
    }
    
    void sendError(const std::string& message) {
        CommandResponse resp;
        resp.ack = "error";
        resp.status = "error";
        resp.error = message;
        nlohmann::json j = resp;
        printf("%s\n", j.dump().c_str());
    }
    
    /**
     * @brief Helper to iterate voices as WavetableSynth
     */
    template<typename F>
    void forEachWavetableSynth(F&& func) {
        voiceIterator_([&func](synth::Voice& voice) {
            func(static_cast<synth::WavetableSynth&>(voice));
        });
    }
    
    // Voice access
    VoiceIterator voiceIterator_;
    features::ProgramStorage* programStorage_;
    SetBaseNoteCallback onSetBaseNote_;
    SetAftertouchRatioCallback onSetAftertouchMinRatio_;
    SetAftertouchRatioCallback onSetAftertouchMaxRatio_;
    GetAftertouchRangeCallback onGetAftertouchRange_;
    
    // Line accumulation buffer
    char lineBuffer_[512] = {0};
    size_t bufferLen_ = 0;
};

} // namespace webcontrol
