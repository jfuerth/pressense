#pragma once

#include <cstdint>
#include <json.hpp>
#include <voice_timing_stats.hpp>

namespace platform {

/**
 * @brief Detailed timing breakdown for audio rendering components
 */
struct TimingBreakdown {
    // Voice mixing timings
    uint32_t avgVoiceMixing = 0;
    uint32_t minVoiceMixing = 0;
    uint32_t maxVoiceMixing = 0;
    
    // Output processing timings
    uint32_t avgOutputProcessing = 0;
    uint32_t minOutputProcessing = 0;
    uint32_t maxOutputProcessing = 0;
    
    // Stereo duplication timings
    uint32_t avgStereoDup = 0;
    uint32_t minStereoDup = 0;
    uint32_t maxStereoDup = 0;
    
    // Float-to-int conversion timings
    uint32_t avgFloatToInt = 0;
    uint32_t minFloatToInt = 0;
    uint32_t maxFloatToInt = 0;
    
    // I2S write timings
    uint32_t avgI2sWrite = 0;
    uint32_t minI2sWrite = 0;
    uint32_t maxI2sWrite = 0;
    
    // Detailed per-voice component timing
    VoiceTimingStats voiceComponents;
};

/**
 * @brief Telemetry data for audio rendering performance
 * 
 * Contains timing information about audio loop execution,
 * scan processing, and buffer underruns.
 */
struct AudioStats {
    uint32_t frameCount;           // Total number of buffers rendered
    uint32_t avgLoopTime;          // Average loop time in microseconds
    uint32_t maxLoopTime;          // Maximum loop time in microseconds
    uint32_t bufferDuration;       // Target buffer duration in microseconds
    uint32_t avgScanTime;          // Average scan processing time in microseconds
    uint32_t avgRenderTime;        // Average audio rendering time in microseconds
    uint32_t underrunCount;        // Total buffer underruns
    uint32_t partialWriteCount;    // Total partial writes
    uint8_t coreId;                // CPU core running audio task
    
    // Detailed timing breakdown
    TimingBreakdown timing;
};

/**
 * @brief JSON serialization for TimingBreakdown
 */
inline void to_json(nlohmann::json& j, const TimingBreakdown& t) {
    j = nlohmann::json{
        {"voiceMixing", {
            {"avg", t.avgVoiceMixing},
            {"min", t.minVoiceMixing},
            {"max", t.maxVoiceMixing},
            {"components", t.voiceComponents}
        }},
        {"outputProcessing", {
            {"avg", t.avgOutputProcessing},
            {"min", t.minOutputProcessing},
            {"max", t.maxOutputProcessing}
        }},
        {"stereoDup", {
            {"avg", t.avgStereoDup},
            {"min", t.minStereoDup},
            {"max", t.maxStereoDup}
        }},
        {"floatToInt", {
            {"avg", t.avgFloatToInt},
            {"min", t.minFloatToInt},
            {"max", t.maxFloatToInt}
        }},
        {"i2sWrite", {
            {"avg", t.avgI2sWrite},
            {"min", t.minI2sWrite},
            {"max", t.maxI2sWrite}
        }}
    };
}

/**
 * @brief JSON serialization for AudioStats
 */
inline void to_json(nlohmann::json& j, const AudioStats& s) {
    j = nlohmann::json{
        {"type", "audio"},
        {"frameCount", s.frameCount},
        {"avgLoopTime", s.avgLoopTime},
        {"maxLoopTime", s.maxLoopTime},
        {"bufferDuration", s.bufferDuration},
        {"avgScanTime", s.avgScanTime},
        {"avgRenderTime", s.avgRenderTime},
        {"underrunCount", s.underrunCount},
        {"partialWriteCount", s.partialWriteCount},
        {"coreId", s.coreId},
        {"timingBreakdown", s.timing}
    };
}

} // namespace platform
