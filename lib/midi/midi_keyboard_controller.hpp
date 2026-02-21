#pragma once

#include <key_scanner.hpp>
#include <telemetry_sink.hpp>
#include <functional>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <log.hpp>

namespace midi {

/**
 * @brief Telemetry data for debugging key scanner behavior
 * 
 * Contains per-key readings, baselines, ratios, and state information
 * for visualization and analysis.
 */
struct KeyScanStats {
    static constexpr uint8_t MAX_KEYS = 16;
    
    uint8_t keyCount;
    uint16_t readings[MAX_KEYS];
    float baselines[MAX_KEYS];
    float ratios[MAX_KEYS];
    bool noteStates[MAX_KEYS];
    uint8_t aftertouchValues[MAX_KEYS];
    
    // Thresholds (same for all keys)
    float noteOnThreshold;
    float noteOffThreshold;
    
    // Calibration state
    bool isCalibrated;
    uint16_t calibrationCount;
};

/**
 * @brief Converts capacitive key scanner readings into MIDI messages
 * 
 * Features:
 * - Startup calibration to establish baseline per key
 * - Note On/Off with hysteresis for stable triggering
 * - Polyphonic Aftertouch based on continuous pressure sensing
 * - Baseline tracking that freezes during touch for maximum aftertouch expression
 * - Configurable transposition and velocity
 */
class MidiKeyboardController {
public:
    static constexpr uint8_t CALIBRATION_SCANS = 100;
    static constexpr float NOTE_ON_THRESHOLD = 1.20f;   // 20% above baseline
    static constexpr float NOTE_OFF_THRESHOLD = 1.10f;  // 10% above baseline (hysteresis)
    static constexpr float BASELINE_ALPHA = 0.001f;     // Exponential moving average factor
    static constexpr uint8_t AFTERTOUCH_DEADBAND = 2;   // Suppress small changes
    
    /**
     * @brief Construct MIDI keyboard controller
     * @param scanner Reference to key scanner (must outlive this controller)
     * @param midiCallback Function to send MIDI bytes
     * @param telemetrySink Platform-specific telemetry output (use NoTelemetrySink if not needed)
     * @param baseNote MIDI note number for first key (default 60 = C4)
     * @param fixedVelocity Note-on velocity 0-127 (default 64)
     */
    MidiKeyboardController(
        KeyScanner& scanner,
        std::function<void(uint8_t)> midiCallback,
        std::unique_ptr<features::TelemetrySink<KeyScanStats>> telemetrySink,
        uint8_t baseNote = 60,
        uint8_t fixedVelocity = 64
    )
        : scanner_(scanner)
        , midiCallback_(std::move(midiCallback))
        , telemetrySink_(std::move(telemetrySink))
        , baseNote_(baseNote)
        , fixedVelocity_(fixedVelocity)
        , calibrationCount_(0)
        , isCalibrated_(false)
        , telemetryEnabled_(false)
    {
        uint8_t keyCount = scanner_.getKeyCount();
        baselines_.resize(keyCount, 0.0f);
        calibrationSums_.resize(keyCount, 0);
        keyStates_.resize(keyCount, false);
        lastAftertouch_.resize(keyCount, 0);
        
        logInfo("MIDI keyboard controller initialized: %d keys, base note %d, velocity %d",
                keyCount, baseNote_, fixedVelocity_);
    }
    
    /**
     * @brief Process current scanner readings and generate MIDI events
     * 
     * Call this periodically (e.g., at scan rate) to convert sensor readings
     * into MIDI messages sent via the callback.
     */
    void processScan() {
        const uint16_t* readings = scanner_.getScanReadings();
        uint8_t keyCount = scanner_.getKeyCount();
        
        // Calibration phase: accumulate baseline values
        if (!isCalibrated_) {
            for (uint8_t i = 0; i < keyCount; i++) {
                calibrationSums_[i] += readings[i];
            }
            
            calibrationCount_++;
            
            if (calibrationCount_ >= CALIBRATION_SCANS) {
                // Finalize calibration
                for (uint8_t i = 0; i < keyCount; i++) {
                    baselines_[i] = static_cast<float>(calibrationSums_[i]) / CALIBRATION_SCANS;
                }
                isCalibrated_ = true;
                logInfo("Keyboard calibration complete");
            }
            return;
        }
        
        // Normal operation: process each key
        for (uint8_t i = 0; i < keyCount; i++) {
            processKey(i, readings[i]);
        }
        
        // Send telemetry if enabled
        if (telemetryEnabled_) {
            KeyScanStats telemetry;
            telemetry.keyCount = keyCount;
            telemetry.isCalibrated = true;
            telemetry.calibrationCount = CALIBRATION_SCANS;
            telemetry.noteOnThreshold = NOTE_ON_THRESHOLD;
            telemetry.noteOffThreshold = NOTE_OFF_THRESHOLD;
            
            for (uint8_t i = 0; i < keyCount && i < KeyScanStats::MAX_KEYS; i++) {
                telemetry.readings[i] = readings[i];
                telemetry.baselines[i] = baselines_[i];
                telemetry.ratios[i] = readings[i] / baselines_[i];
                telemetry.noteStates[i] = keyStates_[i];
                telemetry.aftertouchValues[i] = lastAftertouch_[i];
            }
            
            telemetrySink_->sendTelemetry(telemetry);
        }
    }
    
    /**
     * @brief Set the fixed velocity for note-on events
     * @param velocity MIDI velocity 0-127
     */
    void setFixedVelocity(uint8_t velocity) {
        fixedVelocity_ = velocity & 0x7F;
    }
    
    /**
     * @brief Set the base MIDI note (transposition)
     * @param baseNote MIDI note number for first key
     */
    void setBaseNote(uint8_t baseNote) {
        baseNote_ = baseNote & 0x7F;
    }
    
    /**
     * @brief Check if calibration is complete
     */
    bool isCalibrated() const {
        return isCalibrated_;
    }
    
    /**
     * @brief Enable or disable telemetry output
     * @param enabled True to enable telemetry output
     */
    void setTelemetryEnabled(bool enabled) {
        telemetryEnabled_ = enabled;
    }
    
    /**
     * @brief Check if telemetry is enabled
     */
    bool isTelemetryEnabled() const {
        return telemetryEnabled_;
    }
    
private:
    KeyScanner& scanner_;
    std::function<void(uint8_t)> midiCallback_;
    std::unique_ptr<features::TelemetrySink<KeyScanStats>> telemetrySink_;
    uint8_t baseNote_;
    uint8_t fixedVelocity_;
    
    // Calibration state
    uint16_t calibrationCount_;
    bool isCalibrated_;
    std::vector<uint32_t> calibrationSums_;
    
    // Per-key state
    std::vector<float> baselines_;       // Current baseline (ambient) value
    std::vector<bool> keyStates_;        // Note on/off state
    std::vector<uint8_t> lastAftertouch_; // Last sent aftertouch value
    
    // Telemetry
    bool telemetryEnabled_;
    
    /**
     * @brief Process a single key and generate MIDI events
     */
    void processKey(uint8_t keyIndex, uint16_t reading) {
        float baseline = baselines_[keyIndex];
        float ratio = reading / baseline;
        
        uint8_t midiNote = baseNote_ + keyIndex;
        
        // State machine: Note Off → Note On
        if (!keyStates_[keyIndex]) {
            if (ratio >= NOTE_ON_THRESHOLD) {
                // Note On
                keyStates_[keyIndex] = true;
                sendNoteOn(midiNote, fixedVelocity_);
                lastAftertouch_[keyIndex] = 0;
                
                // Baseline tracking freezes while key is touched
            } else {
                // Update baseline (exponential moving average)
                baselines_[keyIndex] = baseline * (1.0f - BASELINE_ALPHA) + reading * BASELINE_ALPHA;
            }
        }
        // State machine: Note On → Note Off or Aftertouch
        else {
            if (ratio < NOTE_OFF_THRESHOLD) {
                // Note Off
                keyStates_[keyIndex] = false;
                sendNoteOff(midiNote);
                
                // Resume baseline tracking
                baselines_[keyIndex] = baseline * (1.0f - BASELINE_ALPHA) + reading * BASELINE_ALPHA;
            } else {
                // Polyphonic Aftertouch: map pressure to 0-127
                // More capacitance (higher ratio) = more pressure
                float pressure = (ratio - NOTE_OFF_THRESHOLD) / (2.0f - NOTE_OFF_THRESHOLD);
                pressure = std::max(0.0f, std::min(1.0f, pressure));
                uint8_t aftertouch = static_cast<uint8_t>(pressure * 127.0f);
                
                // Only send if changed by more than deadband
                if (std::abs(static_cast<int>(aftertouch) - static_cast<int>(lastAftertouch_[keyIndex])) > AFTERTOUCH_DEADBAND) {
                    sendPolyAftertouch(midiNote, aftertouch);
                    lastAftertouch_[keyIndex] = aftertouch;
                }
            }
        }
    }
    
    /**
     * @brief Send MIDI Note On message
     */
    void sendNoteOn(uint8_t note, uint8_t velocity) {
        midiCallback_(0x90);  // Note On, channel 1
        midiCallback_(note & 0x7F);
        midiCallback_(velocity & 0x7F);
    }
    
    /**
     * @brief Send MIDI Note Off message
     */
    void sendNoteOff(uint8_t note) {
        midiCallback_(0x80);  // Note Off, channel 1
        midiCallback_(note & 0x7F);
        midiCallback_(0x00);  // Velocity 0
    }
    
    /**
     * @brief Send MIDI Polyphonic Aftertouch message
     */
    void sendPolyAftertouch(uint8_t note, uint8_t pressure) {
        midiCallback_(0xA0);  // Polyphonic Aftertouch, channel 1
        midiCallback_(note & 0x7F);
        midiCallback_(pressure & 0x7F);
    }
};

} // namespace midi
