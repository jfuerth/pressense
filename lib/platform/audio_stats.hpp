#pragma once

#include <cstdint>
#include <json.hpp>

namespace platform {

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
};

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
        {"coreId", s.coreId}
    };
}

} // namespace platform
