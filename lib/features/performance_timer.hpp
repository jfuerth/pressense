#pragma once

#include <cstdint>
#include <limits>
#include <json.hpp>

namespace features {

/**
 * @brief Statistics for a single named span
 * 
 * Accumulates min/max/total/count for computing performance metrics.
 * The name pointer must remain valid for the lifetime of this struct
 * (typically a string literal).
 */
struct SpanStats {
    const char* name = nullptr;
    uint64_t min = std::numeric_limits<uint64_t>::max();
    uint64_t max = 0;
    uint64_t total = 0;
    uint32_t count = 0;

    /**
     * @brief Record a measurement for this span
     * @param duration Duration in platform-specific units (cycles or microseconds)
     */
    void record(uint64_t duration) {
        if (duration < min) min = duration;
        if (duration > max) max = duration;
        total += duration;
        ++count;
    }

    /**
     * @brief Reset statistics for next accumulation period
     */
    void reset() {
        min = std::numeric_limits<uint64_t>::max();
        max = 0;
        total = 0;
        count = 0;
        // Note: name is preserved
    }
};

/**
 * @brief JSON serialization for SpanStats
 */
inline void to_json(nlohmann::json& j, const SpanStats& s) {
    j = nlohmann::json{
        {"name", s.name ? s.name : ""},
        {"min", s.min == std::numeric_limits<uint64_t>::max() ? 0 : s.min},
        {"max", s.max},
        {"total", s.total},
        {"count", s.count}
    };
}

/**
 * @brief Aggregated timing statistics for telemetry output
 * 
 * Contains all span statistics plus metadata about the timing session.
 * 
 * @tparam MaxSpans Maximum number of distinct span names
 */
template<size_t MaxSpans>
struct TimingStats {
    SpanStats spans[MaxSpans];
    size_t spanCount = 0;
    uint32_t droppedSpans = 0;
    uint32_t lapCount = 0;
    const char* unit = "cycles";

    TimingStats() : spans{}, spanCount(0), droppedSpans(0), lapCount(0), unit("cycles") {}
};

/**
 * @brief JSON serialization for TimingStats
 */
template<size_t MaxSpans>
inline void to_json(nlohmann::json& j, const TimingStats<MaxSpans>& t) {
    nlohmann::json spansArray = nlohmann::json::array();
    for (size_t i = 0; i < t.spanCount; ++i) {
        spansArray.push_back(t.spans[i]);
    }
    j = nlohmann::json{
        {"type", "timing"},
        {"unit", t.unit},
        {"lapCount", t.lapCount},
        {"droppedSpans", t.droppedSpans},
        {"spans", spansArray}
    };
}

/**
 * @brief No-op timing policy for release builds
 * 
 * All methods are constexpr and inline, allowing the compiler to
 * completely eliminate timing code when this policy is used.
 */
struct NoOpTimingPolicy {
    static constexpr uint64_t now() noexcept { return 0; }
    static constexpr const char* unitName() noexcept { return "none"; }
    static constexpr uint64_t toMicroseconds(uint64_t) noexcept { return 0; }
};

/**
 * @brief Lap timer for measuring performance of code spans
 * 
 * Measures time between successive calls to nextSpan() and accumulates
 * statistics (min/max/total/count) for each named span. Uses compile-time
 * polymorphism via the TimingPolicy template parameter to allow zero-overhead
 * no-op implementation.
 * 
 * Usage:
 * @code
 * LapTimer<Esp32TimingPolicy, 8> timer;
 * timer.nextSpan("phase1");
 * // ... code for phase1 ...
 * timer.nextSpan("phase2");
 * // ... code for phase2 ...
 * timer.end();
 * 
 * // After accumulating multiple laps:
 * telemetrySink->sendTelemetry(timer.getStats());
 * timer.reset();
 * @endcode
 * 
 * IMPORTANT: Span names must be string literals (or have static storage duration).
 * The timer uses pointer equality for fast span lookup, so the same literal must
 * be passed each time for a given span.
 * 
 * @tparam TimingPolicy Policy class providing static now() and unitName() methods
 * @tparam MaxSpans Maximum number of distinct span names (pre-allocated)
 */
template<typename TimingPolicy, size_t MaxSpans>
class LapTimer {
public:
    LapTimer() = default;

    /**
     * @brief End the previous span (if any) and start timing a new span
     * 
     * @tparam N Array size (deduced from string literal)
     * @param literal Span name - must be a string literal or have static storage duration
     */
    template<size_t N>
    void nextSpan(const char (&literal)[N]) {
        nextSpanImpl(literal);
    }

    /**
     * @brief End the current span and complete the lap
     * 
     * Increments the lap counter. Call this at the end of each measured iteration.
     */
    void end() {
        if (currentSpanIndex_ < stats_.spanCount) {
            uint64_t now = TimingPolicy::now();
            uint64_t duration = now - spanStartTime_;
            stats_.spans[currentSpanIndex_].record(duration);
        }
        currentSpanIndex_ = MaxSpans; // Invalid index
        ++stats_.lapCount;
    }

    /**
     * @brief Get accumulated statistics for telemetry output
     * 
     * @return Reference to timing statistics
     */
    const TimingStats<MaxSpans>& getStats() const {
        return stats_;
    }

    /**
     * @brief Reset all statistics for next accumulation period
     * 
     * Clears min/max/total/count for all spans, resets lap counter and dropped count.
     * Span names are preserved to avoid re-lookup overhead.
     */
    void reset() {
        for (size_t i = 0; i < stats_.spanCount; ++i) {
            stats_.spans[i].reset();
        }
        stats_.lapCount = 0;
        stats_.droppedSpans = 0;
        currentSpanIndex_ = MaxSpans;
    }

private:
    TimingStats<MaxSpans> stats_{};
    size_t currentSpanIndex_ = MaxSpans; // Invalid index when no span active
    uint64_t spanStartTime_ = 0;

    /**
     * @brief Implementation of nextSpan - finds or creates span entry
     */
    void nextSpanImpl(const char* name) {
        uint64_t now = TimingPolicy::now();

        // End previous span if active
        if (currentSpanIndex_ < stats_.spanCount) {
            uint64_t duration = now - spanStartTime_;
            stats_.spans[currentSpanIndex_].record(duration);
        }

        // Find existing span by pointer equality (fast!)
        size_t index = MaxSpans;
        for (size_t i = 0; i < stats_.spanCount; ++i) {
            if (stats_.spans[i].name == name) {
                index = i;
                break;
            }
        }

        // Create new span if not found
        if (index == MaxSpans) {
            if (stats_.spanCount < MaxSpans) {
                index = stats_.spanCount;
                stats_.spans[index].name = name;
                stats_.spans[index].reset();
                ++stats_.spanCount;
                // Set unit name on first span creation
                if (stats_.spanCount == 1) {
                    stats_.unit = TimingPolicy::unitName();
                }
            } else {
                // No room for new span
                ++stats_.droppedSpans;
            }
        }

        // Start timing new span
        currentSpanIndex_ = index;
        spanStartTime_ = now;
    }
};

/**
 * @brief Specialization for NoOpTimingPolicy - all operations are no-ops
 * 
 * The compiler will completely eliminate calls to this class's methods.
 */
template<size_t MaxSpans>
class LapTimer<NoOpTimingPolicy, MaxSpans> {
public:
    template<size_t N>
    constexpr void nextSpan(const char (&)[N]) noexcept {}
    constexpr void end() noexcept {}
    constexpr void reset() noexcept {}
    
    // Return empty stats (constexpr where possible)
    TimingStats<MaxSpans> getStats() const { return TimingStats<MaxSpans>{}; }
};

} // namespace features
