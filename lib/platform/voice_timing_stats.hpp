#pragma once

#include <cstdint>
#include <limits>
#include <json.hpp>

namespace platform {

/**
 * @brief Detailed timing statistics for voice rendering components
 * 
 * Breaks down per-voice sample generation into individual operations
 * to identify specific bottlenecks within the synthesis pipeline.
 */
struct VoiceTimingStats {
    // Overall sample count
    uint32_t sampleCount = 0;
    
    // Pitch bend calculation
    uint32_t totalPitchBend = 0;
    uint32_t minPitchBend = std::numeric_limits<uint32_t>::max();
    uint32_t maxPitchBend = 0;
    
    // Oscillator sample generation
    uint32_t totalOscillator = 0;
    uint32_t minOscillator = std::numeric_limits<uint32_t>::max();
    uint32_t maxOscillator = 0;
    
    // Filter envelope calculation
    uint32_t totalFilterEnv = 0;
    uint32_t minFilterEnv = std::numeric_limits<uint32_t>::max();
    uint32_t maxFilterEnv = 0;
    
    // Filter setCutoff (coefficient recalculation)
    uint32_t totalFilterSetCutoff = 0;
    uint32_t minFilterSetCutoff = std::numeric_limits<uint32_t>::max();
    uint32_t maxFilterSetCutoff = 0;
    
    // Filter processSample (actual filtering)
    uint32_t totalFilterProcess = 0;
    uint32_t minFilterProcess = std::numeric_limits<uint32_t>::max();
    uint32_t maxFilterProcess = 0;
    
    // Amplitude envelope
    uint32_t totalAmpEnv = 0;
    uint32_t minAmpEnv = std::numeric_limits<uint32_t>::max();
    uint32_t maxAmpEnv = 0;
    
    /**
     * @brief Record timing for a component
     */
    inline void recordPitchBend(uint32_t time) {
        sampleCount++;
        totalPitchBend += time;
        if (time < minPitchBend) minPitchBend = time;
        if (time > maxPitchBend) maxPitchBend = time;
    }
    
    inline void recordOscillator(uint32_t time) {
        totalOscillator += time;
        if (time < minOscillator) minOscillator = time;
        if (time > maxOscillator) maxOscillator = time;
    }
    
    inline void recordFilterEnv(uint32_t time) {
        totalFilterEnv += time;
        if (time < minFilterEnv) minFilterEnv = time;
        if (time > maxFilterEnv) maxFilterEnv = time;
    }
    
    inline void recordFilterSetCutoff(uint32_t time) {
        totalFilterSetCutoff += time;
        if (time < minFilterSetCutoff) minFilterSetCutoff = time;
        if (time > maxFilterSetCutoff) maxFilterSetCutoff = time;
    }
    
    inline void recordFilterProcess(uint32_t time) {
        totalFilterProcess += time;
        if (time < minFilterProcess) minFilterProcess = time;
        if (time > maxFilterProcess) maxFilterProcess = time;
    }
    
    inline void recordAmpEnv(uint32_t time) {
        totalAmpEnv += time;
        if (time < minAmpEnv) minAmpEnv = time;
        if (time > maxAmpEnv) maxAmpEnv = time;
    }
    
    /**
     * @brief Get average time for each component
     */
    uint32_t getAvgPitchBend() const { return sampleCount ? totalPitchBend / sampleCount : 0; }
    uint32_t getAvgOscillator() const { return sampleCount ? totalOscillator / sampleCount : 0; }
    uint32_t getAvgFilterEnv() const { return sampleCount ? totalFilterEnv / sampleCount : 0; }
    uint32_t getAvgFilterSetCutoff() const { return sampleCount ? totalFilterSetCutoff / sampleCount : 0; }
    uint32_t getAvgFilterProcess() const { return sampleCount ? totalFilterProcess / sampleCount : 0; }
    uint32_t getAvgAmpEnv() const { return sampleCount ? totalAmpEnv / sampleCount : 0; }
    
    /**
     * @brief Merge another stat set into this one
     */
    void merge(const VoiceTimingStats& other) {
        sampleCount += other.sampleCount;
        
        totalPitchBend += other.totalPitchBend;
        if (other.minPitchBend < minPitchBend) minPitchBend = other.minPitchBend;
        if (other.maxPitchBend > maxPitchBend) maxPitchBend = other.maxPitchBend;
        
        totalOscillator += other.totalOscillator;
        if (other.minOscillator < minOscillator) minOscillator = other.minOscillator;
        if (other.maxOscillator > maxOscillator) maxOscillator = other.maxOscillator;
        
        totalFilterEnv += other.totalFilterEnv;
        if (other.minFilterEnv < minFilterEnv) minFilterEnv = other.minFilterEnv;
        if (other.maxFilterEnv > maxFilterEnv) maxFilterEnv = other.maxFilterEnv;
        
        totalFilterSetCutoff += other.totalFilterSetCutoff;
        if (other.minFilterSetCutoff < minFilterSetCutoff) minFilterSetCutoff = other.minFilterSetCutoff;
        if (other.maxFilterSetCutoff > maxFilterSetCutoff) maxFilterSetCutoff = other.maxFilterSetCutoff;
        
        totalFilterProcess += other.totalFilterProcess;
        if (other.minFilterProcess < minFilterProcess) minFilterProcess = other.minFilterProcess;
        if (other.maxFilterProcess > maxFilterProcess) maxFilterProcess = other.maxFilterProcess;
        
        totalAmpEnv += other.totalAmpEnv;
        if (other.minAmpEnv < minAmpEnv) minAmpEnv = other.minAmpEnv;
        if (other.maxAmpEnv > maxAmpEnv) maxAmpEnv = other.maxAmpEnv;
    }
    
    /**
     * @brief Reset all statistics
     */
    void reset() {
        sampleCount = 0;
        
        totalPitchBend = 0;
        minPitchBend = std::numeric_limits<uint32_t>::max();
        maxPitchBend = 0;
        
        totalOscillator = 0;
        minOscillator = std::numeric_limits<uint32_t>::max();
        maxOscillator = 0;
        
        totalFilterEnv = 0;
        minFilterEnv = std::numeric_limits<uint32_t>::max();
        maxFilterEnv = 0;
        
        totalFilterSetCutoff = 0;
        minFilterSetCutoff = std::numeric_limits<uint32_t>::max();
        maxFilterSetCutoff = 0;
        
        totalFilterProcess = 0;
        minFilterProcess = std::numeric_limits<uint32_t>::max();
        maxFilterProcess = 0;
        
        totalAmpEnv = 0;
        minAmpEnv = std::numeric_limits<uint32_t>::max();
        maxAmpEnv = 0;
    }
    
    /**
     * @brief Convert all timing values from CPU cycles to microseconds
     * @param cyclesPerMicrosecond CPU frequency in MHz (e.g., 240 for 240MHz)
     * @return A new VoiceTimingStats with converted values
     */
    VoiceTimingStats convertToMicroseconds(uint32_t cyclesPerMicrosecond) const {
        VoiceTimingStats result;
        result.sampleCount = sampleCount;
        
        result.totalPitchBend = totalPitchBend / cyclesPerMicrosecond;
        result.minPitchBend = (minPitchBend == std::numeric_limits<uint32_t>::max()) ? 
            std::numeric_limits<uint32_t>::max() : minPitchBend / cyclesPerMicrosecond;
        result.maxPitchBend = maxPitchBend / cyclesPerMicrosecond;
        
        result.totalOscillator = totalOscillator / cyclesPerMicrosecond;
        result.minOscillator = (minOscillator == std::numeric_limits<uint32_t>::max()) ? 
            std::numeric_limits<uint32_t>::max() : minOscillator / cyclesPerMicrosecond;
        result.maxOscillator = maxOscillator / cyclesPerMicrosecond;
        
        result.totalFilterEnv = totalFilterEnv / cyclesPerMicrosecond;
        result.minFilterEnv = (minFilterEnv == std::numeric_limits<uint32_t>::max()) ? 
            std::numeric_limits<uint32_t>::max() : minFilterEnv / cyclesPerMicrosecond;
        result.maxFilterEnv = maxFilterEnv / cyclesPerMicrosecond;
        
        result.totalFilterSetCutoff = totalFilterSetCutoff / cyclesPerMicrosecond;
        result.minFilterSetCutoff = (minFilterSetCutoff == std::numeric_limits<uint32_t>::max()) ? 
            std::numeric_limits<uint32_t>::max() : minFilterSetCutoff / cyclesPerMicrosecond;
        result.maxFilterSetCutoff = maxFilterSetCutoff / cyclesPerMicrosecond;
        
        result.totalFilterProcess = totalFilterProcess / cyclesPerMicrosecond;
        result.minFilterProcess = (minFilterProcess == std::numeric_limits<uint32_t>::max()) ? 
            std::numeric_limits<uint32_t>::max() : minFilterProcess / cyclesPerMicrosecond;
        result.maxFilterProcess = maxFilterProcess / cyclesPerMicrosecond;
        
        result.totalAmpEnv = totalAmpEnv / cyclesPerMicrosecond;
        result.minAmpEnv = (minAmpEnv == std::numeric_limits<uint32_t>::max()) ? 
            std::numeric_limits<uint32_t>::max() : minAmpEnv / cyclesPerMicrosecond;
        result.maxAmpEnv = maxAmpEnv / cyclesPerMicrosecond;
        
        return result;
    }
};

/**
 * @brief JSON serialization for VoiceTimingStats
 */
inline void to_json(nlohmann::json& j, const VoiceTimingStats& v) {
    j = nlohmann::json{
        {"sampleCount", v.sampleCount},
        {"pitchBend", {
            {"avg", v.getAvgPitchBend()},
            {"min", v.minPitchBend == std::numeric_limits<uint32_t>::max() ? 0 : v.minPitchBend},
            {"max", v.maxPitchBend}
        }},
        {"oscillator", {
            {"avg", v.getAvgOscillator()},
            {"min", v.minOscillator == std::numeric_limits<uint32_t>::max() ? 0 : v.minOscillator},
            {"max", v.maxOscillator}
        }},
        {"filterEnvelope", {
            {"avg", v.getAvgFilterEnv()},
            {"min", v.minFilterEnv == std::numeric_limits<uint32_t>::max() ? 0 : v.minFilterEnv},
            {"max", v.maxFilterEnv}
        }},
        {"filterSetCutoff", {
            {"avg", v.getAvgFilterSetCutoff()},
            {"min", v.minFilterSetCutoff == std::numeric_limits<uint32_t>::max() ? 0 : v.minFilterSetCutoff},
            {"max", v.maxFilterSetCutoff}
        }},
        {"filterProcess", {
            {"avg", v.getAvgFilterProcess()},
            {"min", v.minFilterProcess == std::numeric_limits<uint32_t>::max() ? 0 : v.minFilterProcess},
            {"max", v.maxFilterProcess}
        }},
        {"ampEnvelope", {
            {"avg", v.getAvgAmpEnv()},
            {"min", v.minAmpEnv == std::numeric_limits<uint32_t>::max() ? 0 : v.minAmpEnv},
            {"max", v.maxAmpEnv}
        }}
    };
}

} // namespace platform
