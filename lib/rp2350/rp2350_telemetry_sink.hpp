#pragma once

#include <telemetry_sink.hpp>
#include <json.hpp>
#include <cstdio>
#include <type_traits>

namespace rp2350 {

/**
 * @brief Detects a telemetry type that can serialise itself without allocating
 *
 * Such a type provides `size_t writeJson(char*, size_t) const` and a
 * `JSON_CAPACITY` bound on its output.
 */
template<typename T, typename = void>
struct HasDirectJson : std::false_type {};

template<typename T>
struct HasDirectJson<T, std::void_t<
    decltype(std::declval<const T&>().writeJson(static_cast<char*>(nullptr), size_t{0})),
    decltype(T::JSON_CAPACITY)>> : std::true_type {};

/**
 * @brief Buffer size needed for a type's direct JSON output
 *
 * One byte for types that don't support it - an empty array is ill-formed.
 */
template<typename T, bool = HasDirectJson<T>::value>
struct DirectJsonCapacity { static constexpr size_t value = 1; };

template<typename T>
struct DirectJsonCapacity<T, true> { static constexpr size_t value = T::JSON_CAPACITY; };

/**
 * @brief RP2350 telemetry sink using USB serial output
 *
 * Outputs telemetry data as JSON Lines to USB serial (stdout).
 * Synchronous output - no buffering or background tasks.
 *
 * If the payload type can serialise itself into a fixed buffer (see
 * HasDirectJson) that path is used in preference to nlohmann. This matters on
 * this platform specifically: building the JSON through nlohmann allocates a
 * vector per array plus a string for the result, and that memory traffic
 * contends with audio rendering on the other core - measured at ~18% of the
 * render deadline at full polyphony. See AUDIO_PIPELINE_PERFORMANCE.md.
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
        if constexpr (HasDirectJson<TelemetryDataT>::value) {
            data.writeJson(buffer_, sizeof(buffer_));
            puts(buffer_);   // puts appends the newline
        } else {
            nlohmann::json j = data;
            printf("%s\n", j.dump().c_str());
        }
    }

private:
    // Only meaningfully sized for the types that can use it; the nlohmann
    // fallback leaves a single unused byte.
    char buffer_[DirectJsonCapacity<TelemetryDataT>::value];
};

} // namespace rp2350
