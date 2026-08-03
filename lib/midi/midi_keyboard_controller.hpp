#pragma once

#include <key_scanner.hpp>
#include <telemetry_sink.hpp>
#include <functional>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <log.hpp>
#include <json.hpp>

namespace midi {

/**
 * @brief Telemetry data for debugging key scanner behavior
 * 
 * Contains per-key readings, baselines, ratios, and state information
 * for visualization and analysis.
 * 
 * Template parameter allows compile-time optimization with stack arrays.
 * 
 * @tparam NumKeys Number of keys to hold stats for
 */
template<uint8_t NumKeys>
struct KeyScanStats {
    static constexpr uint8_t keyCount = NumKeys;
    uint16_t readings[NumKeys];
    float baselines[NumKeys];
    float ratios[NumKeys];
    bool noteStates[NumKeys];
    uint8_t aftertouchValues[NumKeys];
    
    // Threshold ratios (multiple of each key's baseline) for note on/off detection
    float noteOnThreshold;
    float noteOffThreshold;
    
    // Calibration state
    bool isCalibrated;
    uint16_t calibrationCount;
    
    /**
     * @brief Default constructor - initializes all arrays to zero
     */
    KeyScanStats()
        : readings{}
        , baselines{}
        , ratios{}
        , noteStates{}
        , aftertouchValues{}
        , noteOnThreshold(0.0f)
        , noteOffThreshold(0.0f)
        , isCalibrated(false)
        , calibrationCount(0)
    {}

    /**
     * @brief Upper bound on the length of writeJson()'s output, including the NUL
     *
     * Five arrays of per-key values; 40 characters per key is comfortably more
     * than the longest any of them can produce.
     */
    static constexpr size_t JSON_CAPACITY = NumKeys * 40u + 256u;

    /**
     * @brief Serialise to JSON without allocating
     *
     * The nlohmann path (to_json below) builds five std::vectors and a
     * std::string for every report. On the RP2350 that memory traffic runs on the
     * same bus as audio rendering on the other core and measurably slows it down
     * - about 18% of the render deadline at full polyphony. This writes straight
     * into a caller-owned buffer instead. Output is identical in shape, so the
     * control panel does not care which path produced it.
     *
     * @return Number of characters written, excluding the NUL
     */
    size_t writeJson(char* out, size_t capacity) const {
        JsonWriter w{out, capacity};

        w.raw("{\"type\":\"keyScan\"");
        w.fmt(",\"keyCount\":%u", (unsigned)keyCount);
        w.raw(isCalibrated ? ",\"isCalibrated\":true" : ",\"isCalibrated\":false");
        w.fmt(",\"calibrationCount\":%u", (unsigned)calibrationCount);
        w.raw(",\"noteOnThreshold\":");   w.number(noteOnThreshold);
        w.raw(",\"noteOffThreshold\":");  w.number(noteOffThreshold);

        w.raw(",\"readings\":[");
        for (uint8_t i = 0; i < NumKeys; ++i) {
            if (i) w.raw(",");
            w.fmt("%u", (unsigned)readings[i]);
        }
        w.raw("],\"baselines\":[");
        for (uint8_t i = 0; i < NumKeys; ++i) {
            if (i) w.raw(",");
            w.number(baselines[i]);
        }
        w.raw("],\"ratios\":[");
        for (uint8_t i = 0; i < NumKeys; ++i) {
            if (i) w.raw(",");
            w.number(ratios[i]);
        }
        w.raw("],\"noteStates\":[");
        for (uint8_t i = 0; i < NumKeys; ++i) {
            if (i) w.raw(",");
            w.raw(noteStates[i] ? "true" : "false");
        }
        w.raw("],\"aftertouchValues\":[");
        for (uint8_t i = 0; i < NumKeys; ++i) {
            if (i) w.raw(",");
            w.fmt("%u", (unsigned)aftertouchValues[i]);
        }
        w.raw("]}");

        return w.finish();
    }

private:
    /**
     * @brief Bounds-checked append-only writer over a fixed buffer
     *
     * Every append silently stops at the end of the buffer, so a capacity
     * miscalculation truncates the report rather than corrupting memory.
     */
    struct JsonWriter {
        char* buf;
        size_t capacity;
        size_t len = 0;

        size_t room() const { return (len + 1 < capacity) ? capacity - len - 1 : 0; }

        void raw(const char* text) {
            size_t space = room();
            while (*text && space--) buf[len++] = *text++;
        }

        template<typename... Args>
        void fmt(const char* format, Args... args) {
            size_t space = room();
            if (!space) return;
            int written = snprintf(buf + len, space + 1, format, args...);
            if (written > 0) {
                len += (static_cast<size_t>(written) < space) ? written : space;
            }
        }

        /**
         * @brief Append a float with three decimal places
         *
         * Hand-rolled because this SDK build links newlib-nano, whose printf
         * silently emits nothing for %f. Values here are ADC counts and ratios,
         * so scaling by 1000 cannot overflow a uint32.
         */
        void number(float v) {
            bool negative = v < 0.0f;
            if (negative) v = -v;
            // Also rejects NaN and infinity, which would make the cast undefined.
            if (!(v < 4000000.0f)) { v = 0.0f; negative = false; }
            uint32_t scaled = static_cast<uint32_t>(v * 1000.0f + 0.5f);
            fmt("%s%lu.%03lu", negative ? "-" : "",
                (unsigned long)(scaled / 1000u), (unsigned long)(scaled % 1000u));
        }

        size_t finish() {
            if (capacity) buf[len] = '\0';
            return len;
        }
    };
};

/**
 * @brief JSON serialization for KeyScanStats
 */
template<uint8_t NumKeys>
inline void to_json(nlohmann::json& j, const KeyScanStats<NumKeys>& s) {
    j = nlohmann::json{
        {"type", "keyScan"},
        {"keyCount", s.keyCount},
        {"isCalibrated", s.isCalibrated},
        {"calibrationCount", s.calibrationCount},
        {"noteOnThreshold", s.noteOnThreshold},
        {"noteOffThreshold", s.noteOffThreshold},
        {"readings", std::vector<uint16_t>(s.readings, s.readings + NumKeys)},
        {"baselines", std::vector<float>(s.baselines, s.baselines + NumKeys)},
        {"ratios", std::vector<float>(s.ratios, s.ratios + NumKeys)},
        {"noteStates", std::vector<bool>(s.noteStates, s.noteStates + NumKeys)},
        {"aftertouchValues", std::vector<uint8_t>(s.aftertouchValues, s.aftertouchValues + NumKeys)}
    };
}

/**
 * @brief Converts capacitive key scanner readings into MIDI messages
 * 
 * Features:
 * - Startup calibration to establish baseline per key
 * - Note On/Off with hysteresis for stable triggering
 * - Polyphonic Aftertouch based on continuous pressure sensing
 * - Baseline tracking that freezes during touch for maximum aftertouch expression
 * - Configurable transposition and velocity
 * 
 * Template parameter allows compile-time optimization with stack arrays.
 * 
 * @tparam NumKeys Number of keys (must match scanner configuration)
 */
template<uint8_t NumKeys>
class MidiKeyboardController {
public:
    static constexpr uint16_t CALIBRATION_SCANS = 10;
    static constexpr float NOTE_ON_THRESHOLD = 2.0f;    // Ratio above baseline for note on
    static constexpr float NOTE_OFF_THRESHOLD = 1.5f;   // Ratio above baseline for note off (hysteresis)
    // Runtime-tunable aftertouch input range (defaults; adjustable via the control panel)
    static constexpr float DEFAULT_AFTERTOUCH_MIN_RATIO = 5.0f;  // Ratio where aftertouch begins; below this it stays 0
    static constexpr float DEFAULT_AFTERTOUCH_MAX_RATIO = 10.0f; // Ratio for maximum aftertouch (hard press measures ~11-12)
    static constexpr float AFTERTOUCH_GAMMA = 2.5f;     // Response curve: >1 = slow ramp at first, then steep toward max
    static constexpr float BASELINE_ALPHA = 0.001f;     // Exponential moving average factor
    static constexpr float MIN_BASELINE = 1.0f;         // Minimum baseline to prevent ratio issues

    // Scans between telemetry reports. The scan loop runs at the mains frequency
    // (~60 Hz), so 6 gives a ~10 Hz display update: fast enough to watch a key
    // being pressed, slow enough that serialising and shipping it does not
    // interfere with audio rendering on the other core.
    static constexpr uint16_t DEFAULT_TELEMETRY_INTERVAL = 6;
    
    /**
     * @brief Construct MIDI keyboard controller
     * @param scanner Reference to key scanner (must outlive this controller)
     * @param midiCallback Function to send MIDI bytes (required, must be valid)
     * @param telemetrySink Platform-specific telemetry output (use NoTelemetrySink if not needed)
     * @param baseNote MIDI note number for first key (default 60 = C4)
     * @param fixedVelocity Note-on velocity 0-127 (default 64)
     */
    MidiKeyboardController(
        KeyScanner& scanner,
        std::function<void(uint8_t)> midiCallback,
        std::unique_ptr<features::TelemetrySink<KeyScanStats<NumKeys>>> telemetrySink,
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
        , calibrationSums_{}
        , baselines_{}
        , keyStates_{}
        , lastAftertouch_{}
        , telemetryEnabled_(false)
    {
        if (!midiCallback_) {
            logFatal("MidiKeyboardController: midiCallback is required");
        }
        if (!telemetrySink_) {
            logFatal("MidiKeyboardController: telemetrySink is required (use NoTelemetrySink if not needed)");
        }
        logInfo("MIDI keyboard controller initialized: %d keys, base note %d, velocity %d",
                NumKeys, baseNote_, fixedVelocity_);
    }
    
    /**
     * @brief Process current scanner readings and generate MIDI events
     * 
     * Call this periodically (e.g., at scan rate) to convert sensor readings
     * into MIDI messages sent via the callback.
     */
    void processScan() {
        processScan(scanner_.getScanReadings());
    }

    /**
     * @brief Process externally-supplied readings and generate MIDI events
     *
     * Identical to processScan() but lets the caller pre-condition the raw
     * scans first - e.g. averaging over a full AC line cycle to reject 50/60 Hz
     * mains hum before note-on/aftertouch detection.
     *
     * @param readings Per-key readings (length == NumKeys)
     */
    void processScan(const uint16_t* readings) {
        // Calibration phase: accumulate baseline values
        if (!isCalibrated_) {
            for (uint8_t i = 0; i < NumKeys; i++) {
                calibrationSums_[i] += readings[i];
            }
            
            calibrationCount_++;
            
            if (calibrationCount_ >= CALIBRATION_SCANS) {
                // Finalize calibration with minimum baseline enforcement
                for (uint8_t i = 0; i < NumKeys; i++) {
                    float avgBaseline = static_cast<float>(calibrationSums_[i]) / CALIBRATION_SCANS;
                    baselines_[i] = std::max(avgBaseline, MIN_BASELINE);
                }
                isCalibrated_ = true;
                logInfo("Keyboard calibration complete");
            }
            return;
        }
        
        // Normal operation: process each key
        for (uint8_t i = 0; i < NumKeys; i++) {
            processKey(i, readings[i]);
        }
        
        // Send telemetry if enabled, at a fraction of the scan rate.
        //
        // A full scan report is five arrays of per-key values, serialised through
        // nlohmann into a heap-allocated string and written to USB. Emitting that
        // every scan (~60 Hz) is enough memory and USB traffic to starve the audio
        // core on the other side of the chip: it took a single sounding voice from
        // 33% of the render deadline to 145%, i.e. audible dropouts. It is a
        // diagnostic display, so a UI-rate update is all it ever needed.
        // Measurements in AUDIO_PIPELINE_PERFORMANCE.md.
        if (telemetryEnabled_ && ++scansSinceTelemetry_ >= telemetryInterval_) {
            scansSinceTelemetry_ = 0;
            KeyScanStats<NumKeys> telemetry;
            telemetry.isCalibrated = true;
            telemetry.calibrationCount = CALIBRATION_SCANS;
            telemetry.noteOnThreshold = NOTE_ON_THRESHOLD;
            telemetry.noteOffThreshold = NOTE_OFF_THRESHOLD;
            
            for (uint8_t i = 0; i < NumKeys; i++) {
                telemetry.readings[i] = readings[i];
                telemetry.baselines[i] = baselines_[i];
                telemetry.ratios[i] = (baselines_[i] > 0) ? (readings[i] / baselines_[i]) : 0.0f;  // Ratio of reading to baseline
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

    /**
     * @brief Emit a telemetry report only every Nth scan
     *
     * Telemetry is expensive enough to disturb audio rendering (see processScan),
     * so it is deliberately decoupled from the scan rate. 1 restores the old
     * every-scan behaviour and should only be used when nothing is playing.
     *
     * @param scans Scans between reports; 0 and 1 both mean every scan
     */
    void setTelemetryInterval(uint16_t scans) {
        telemetryInterval_ = scans ? scans : 1;
        scansSinceTelemetry_ = 0;
    }

    uint16_t getTelemetryInterval() const {
        return telemetryInterval_;
    }

    /**
     * @brief Set the pressure ratio at which aftertouch begins
     *
     * Below this ratio the key produces no aftertouch (output stays 0).
     */
    void setAftertouchMinRatio(float ratio) {
        aftertouchMinRatio_ = ratio;
    }

    /**
     * @brief Set the pressure ratio that maps to maximum aftertouch (127)
     */
    void setAftertouchMaxRatio(float ratio) {
        aftertouchMaxRatio_ = ratio;
    }

    float getAftertouchMinRatio() const { return aftertouchMinRatio_; }
    float getAftertouchMaxRatio() const { return aftertouchMaxRatio_; }

private:
    KeyScanner& scanner_;
    std::function<void(uint8_t)> midiCallback_;
    std::unique_ptr<features::TelemetrySink<KeyScanStats<NumKeys>>> telemetrySink_;
    uint8_t baseNote_;
    uint8_t fixedVelocity_;
    
    // Calibration state
    uint16_t calibrationCount_;
    bool isCalibrated_;
    uint32_t calibrationSums_[NumKeys];
    
    // Per-key state
    float baselines_[NumKeys];       // Current baseline (ambient) value
    bool keyStates_[NumKeys];        // Note on/off state
    uint8_t lastAftertouch_[NumKeys]; // Last sent aftertouch value
    
    // Telemetry
    bool telemetryEnabled_;
    // Reports are throttled to one per telemetryInterval_ scans; see processScan
    // for why this must not run at the scan rate.
    uint16_t telemetryInterval_ = DEFAULT_TELEMETRY_INTERVAL;
    uint16_t scansSinceTelemetry_ = 0;

    // Runtime-tunable aftertouch input range (set from the control panel)
    float aftertouchMinRatio_ = DEFAULT_AFTERTOUCH_MIN_RATIO;
    float aftertouchMaxRatio_ = DEFAULT_AFTERTOUCH_MAX_RATIO;

    /**
     * @brief Process a single key and generate MIDI events
     */
    void processKey(uint8_t keyIndex, uint16_t reading) {
        float baseline = baselines_[keyIndex];
        
        // Ensure baseline is never too low to prevent ratio issues
        if (baseline < MIN_BASELINE) {
            baseline = MIN_BASELINE;
            baselines_[keyIndex] = MIN_BASELINE;
        }
        
        float ratio = reading / baseline;  // Ratio of current reading to baseline
        
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
                baselines_[keyIndex] = std::max(
                    baseline * (1.0f - BASELINE_ALPHA) + reading * BASELINE_ALPHA,
                    MIN_BASELINE
                );
            }
        }
        // State machine: Note On → Note Off or Aftertouch
        else {
            if (ratio < NOTE_OFF_THRESHOLD) {
                // Note Off
                keyStates_[keyIndex] = false;
                sendNoteOff(midiNote);
                
                // Resume baseline tracking with minimum enforcement
                baselines_[keyIndex] = std::max(
                    baseline * (1.0f - BASELINE_ALPHA) + reading * BASELINE_ALPHA,
                    MIN_BASELINE
                );
            } else {
                // Polyphonic Aftertouch: map pressure ratio to 0-127.
                // Normalize aftertouchMinRatio_..aftertouchMaxRatio_ to 0..1
                // (so light touches below the onset stay silent), then apply a
                // gamma curve for a slow-then-steep response.
                float range = aftertouchMaxRatio_ - aftertouchMinRatio_;
                float pressure = (range > 0.0f) ? (ratio - aftertouchMinRatio_) / range : 0.0f;
                pressure = std::max(0.0f, std::min(1.0f, pressure));
                float shaped = std::pow(pressure, AFTERTOUCH_GAMMA);
                uint8_t aftertouch = static_cast<uint8_t>(shaped * 127.0f);
                
                // Send on any change. The mains-averaged signal is clean enough
                // that a deadband isn't needed - and a deadband strands the value
                // a couple of LSB above 0 until the key fully releases.
                if (aftertouch != lastAftertouch_[keyIndex]) {
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
