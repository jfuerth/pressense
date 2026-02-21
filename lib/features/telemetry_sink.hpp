#pragma once

namespace features {

/**
 * @brief Abstract interface for telemetry output
 * 
 * Template parameter allows use with any telemetry data structure.
 * Implementations handle platform-specific transport (FreeRTOS queue, file, network, etc.)
 * Use NoTelemetrySink for platforms without telemetry support.
 * 
 * @tparam TelemetryDataT Type of telemetry data structure
 */
template<typename TelemetryDataT>
class TelemetrySink {
public:
    virtual ~TelemetrySink() = default;
    
    /**
     * @brief Send telemetry data to platform-specific destination (non-blocking)
     * @param data Telemetry data to send
     */
    virtual void sendTelemetry(const TelemetryDataT& data) = 0;
};

/**
 * @brief Null object implementation - does nothing
 * 
 * Use this for platforms that don't support telemetry output.
 * Eliminates need for null checks in calling code.
 * 
 * @tparam TelemetryDataT Type of telemetry data structure
 */
template<typename TelemetryDataT>
class NoTelemetrySink : public TelemetrySink<TelemetryDataT> {
public:
    void sendTelemetry(const TelemetryDataT& /*data*/) override {
        // No-op
    }
};

} // namespace features
