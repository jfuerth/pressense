#pragma once

#include <telemetry_sink.hpp>
#include <json.hpp>
#include <cstdio>

namespace rp2350 {

/**
 * @brief RP2350 telemetry sink using USB serial output
 * 
 * Outputs telemetry data as JSON Lines to USB serial (stdout).
 * Synchronous output - no buffering or background tasks.
 * 
 * @tparam TelemetryDataT Type of telemetry data (must have to_json function)
 */
template<typename TelemetryDataT>
class Rp2350TelemetrySink : public features::TelemetrySink<TelemetryDataT> {
public:
    /**
     * @brief Construct RP2350 telemetry sink
     */
    Rp2350TelemetrySink() = default;
    
    ~Rp2350TelemetrySink() override = default;
    
    /**
     * @brief Send telemetry data (synchronous output to USB serial)
     * @param data Telemetry data to serialize and output
     */
    void sendTelemetry(const TelemetryDataT& data) override {
        nlohmann::json j = data;
        printf("%s\n", j.dump().c_str());
    }
};

} // namespace rp2350
