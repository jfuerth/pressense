#pragma once

#include "command_protocol.hpp"
#include <json.hpp>
#include <sawtooth_synth.hpp>
#include <program_storage.hpp>
#include <log.hpp>
#include <functional>
#include <string>
#include <vector>
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
 * @brief Feeds a raw MIDI byte into the synth's MIDI stream
 *
 * Lets a host drive the synth without the physical keyboard - to audition a
 * patch from the control panel, or to hold a known number of voices while
 * profiling the render loop.
 */
using MidiByteCallback = std::function<void(uint8_t byte)>;

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

            case CommandType::MIDI:
                return handleMidi(j);


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
    /**
     * @brief Set the sink for raw MIDI bytes injected via the "midi" command
     */
    void setMidiByteCallback(MidiByteCallback callback) {
        onMidiByte_ = std::move(callback);
    }

    void setBaseNoteCallback(SetBaseNoteCallback callback) {
        onSetBaseNote_ = std::move(callback);
    }

    /**
     * @brief Register an external float parameter (e.g. a keyboard setting)
     *
     * External params live outside the synth voices - for example in the
     * keyboard controller, which WebController doesn't own. Once registered, the
     * parameter participates in setParam/getParams exactly like a synth param,
     * with no per-parameter plumbing. Adding one is a single call from main.
     *
     * @param name    Parameter name as used by the control panel
     * @param setter  Applies a new value
     * @param getter  Reports the current value (for getParams readback)
     */
    void registerParam(const std::string& name,
                       std::function<void(float)> setter,
                       std::function<float()> getter) {
        externalParams_.push_back({name, std::move(setter), std::move(getter)});
    }

    /**
     * @brief Apply a parameter by name, exactly as a setParam command would
     *
     * Shared with the MIDI CC path (see midi/cc_map.hpp) so that a CC and the
     * knob for the same parameter cannot drift apart: both end up here.
     *
     * @return false if the name matches no known parameter
     */
    bool applyNamedParam(const std::string& name, float value) {
        return setParameter(name, value);
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
     * @brief Inject raw MIDI bytes into the synth's MIDI stream
     *
     * The bytes are fed through the normal StreamProcessor, so running status,
     * note on/off, aftertouch and CC all behave exactly as they do coming from
     * the physical keyboard.
     */
    bool handleMidi(const nlohmann::json& j) {
        if (!j.contains("bytes") || !j["bytes"].is_array()) {
            sendError("midi: expected a 'bytes' array");
            return false;
        }
        if (!onMidiByte_) {
            sendError("midi: no MIDI sink wired up on this platform");
            return false;
        }
        for (const auto& b : j["bytes"]) {
            if (!b.is_number_unsigned()) {
                sendError("midi: 'bytes' must contain unsigned integers");
                return false;
            }
            unsigned value = b.get<unsigned>();
            if (value > 255) {
                sendError("midi: byte out of range");
                return false;
            }
            onMidiByte_(static_cast<uint8_t>(value));
        }
        // Deliberately not acked. MIDI is a continuous data stream, not a control
        // operation - aftertouch alone arrives tens of times a second - and
        // printing a JSON acknowledgement for each one puts exactly the kind of
        // periodic serialise-and-write burst on this core that
        // AUDIO_PIPELINE_PERFORMANCE.md documents as starving the audio core.
        return true;
    }

    /**
     * @brief Set a synth parameter by name
     * @return false if the name matches no known parameter
     */
    bool setParameter(const std::string& param, float value) {
        // Oscillator
        if (param == "waveformShape") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setWaveformShape(value); });
        }
        // Filter
        else if (param == "baseCutoff") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setBaseCutoff(value); });
        }
        else if (param == "filterQ") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setFilterQ(value); });
        }
        else if (param == "filterMode") {
            // Rounded and clamped rather than truncated: a CC sweep or a knob
            // lands on fractional values, and truncation would make the top mode
            // reachable only at the exact maximum.
            int mode = static_cast<int>(value + 0.5f);
            const int lastMode = static_cast<int>(synth::BiquadFilter::Mode::__COUNT) - 1;
            if (mode < 0) mode = 0;
            if (mode > lastMode) mode = lastMode;
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
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setFilterEnvSustain(value); });
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
        // Voice level
        else if (param == "level") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setLevel(value); });
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
        else if (param == "waveformShape_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setWaveformShapeAtMod(value); });
        }
        else if (param == "filterQ_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setFilterQAtMod(value); });
        }
        else if (param == "vibratoRate_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setVibratoRateAtMod(value); });
        }
        else if (param == "tremoloRate_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setTremoloRateAtMod(value); });
        }
        else if (param == "filterEnvSustain_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setFilterEnvSustainAtMod(value); });
        }
        else if (param == "level_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setLevelAtMod(value); });
        }
        else if (param == "pitchBend_atMod") {
            forEachWavetableSynth([value](synth::WavetableSynth& v) { v.setPitchBendAtMod(value); });
        }
        // Externally-registered params (e.g. keyboard aftertouch range, and the
        // output stage, which SynthApplication registers)
        else if (!applyExternalParam(param, value)) {
            logWarn("Unknown parameter: %s", param.c_str());
            return false;
        }
        return true;
    }

    /**
     * @brief Apply a value to an externally-registered param
     * @return true if the param was registered (and applied), false otherwise
     */
    bool applyExternalParam(const std::string& param, float value) {
        for (auto& p : externalParams_) {
            if (p.name == param) {
                if (p.set) p.set(value);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Send current parameters as telemetry
     */
    void sendCurrentParams() {
        ParamsTelemetry params;
        bool captured = false;
        forEachWavetableSynth([&params, &captured](synth::WavetableSynth& v) {
            if (!captured) {
                params.waveformShape = v.getWaveformShape();
                params.baseCutoff = v.getBaseCutoff();
                params.filterQ = v.getFilterQ();
                params.filterMode = static_cast<int>(v.getFilter().getMode());
                params.filterEnvAmount = v.getFilterEnvelopeAmount();
                params.filterEnvAttack = v.getFilterEnvelope().getAttackTime();
                params.filterEnvDecay = v.getFilterEnvelope().getDecayTime();
                params.filterEnvSustain = v.getFilterEnvSustain();
                params.filterEnvRelease = v.getFilterEnvelope().getReleaseTime();
                params.ampEnvAttack = v.getAmpEnvelope().getAttackTime();
                params.ampEnvDecay = v.getAmpEnvelope().getDecayTime();
                params.ampEnvSustain = v.getAmpEnvelope().getSustainLevel();
                params.ampEnvRelease = v.getAmpEnvelope().getReleaseTime();
                params.vibratoRate = v.getVibratoRate();
                params.vibratoDepth = v.getVibratoDepth();
                params.tremoloRate = v.getTremoloRate();
                params.tremoloDepth = v.getTremoloDepth();
                params.level = v.getLevel();
                params.baseCutoff_atMod = v.getBaseCutoffAtMod();
                params.filterEnvAmount_atMod = v.getFilterEnvAmountAtMod();
                params.vibratoDepth_atMod = v.getVibratoDepthAtMod();
                params.tremoloDepth_atMod = v.getTremoloDepthAtMod();
                params.waveformShape_atMod = v.getWaveformShapeAtMod();
                params.filterQ_atMod = v.getFilterQAtMod();
                params.vibratoRate_atMod = v.getVibratoRateAtMod();
                params.tremoloRate_atMod = v.getTremoloRateAtMod();
                params.filterEnvSustain_atMod = v.getFilterEnvSustainAtMod();
                params.level_atMod = v.getLevelAtMod();
                params.pitchBend_atMod = v.getPitchBendAtMod();
                captured = true;
            }
        });
        nlohmann::json j = params;
        // Append externally-registered params (e.g. keyboard aftertouch range)
        for (const auto& p : externalParams_) {
            j[p.name] = p.get ? p.get() : 0.0f;
        }
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
    MidiByteCallback onMidiByte_;

    // Registry of params that live outside the synth voices
    struct ExternalParam {
        std::string name;
        std::function<void(float)> set;
        std::function<float()> get;
    };
    std::vector<ExternalParam> externalParams_;
    
    // Line accumulation buffer
    char lineBuffer_[512] = {0};
    size_t bufferLen_ = 0;
};

} // namespace webcontrol
