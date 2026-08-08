#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace midi {

/**
 * @brief MIDI Control Change assignments for every continuous parameter
 *
 * One table, consulted by SynthApplication::handleCC, mapping a CC number onto a
 * parameter name that WebController::setParameter understands. Keeping the
 * mapping declarative means the CC path and the control panel's JSON path share
 * one implementation of "apply parameter X" - a CC cannot drift from its knob -
 * and the assignments can be checked for collisions by a test rather than by
 * eye.
 *
 * How the numbers were chosen:
 *
 * - CCs 1, 20, 21, 25, 63, 70-74 predate this table. They were picked to match
 *   the knob positions on a particular M-Audio controller, so they follow no
 *   pattern; they are kept as they were rather than renumbered, because that
 *   controller is what is plugged in.
 * - Everything added since sits in ranges the MIDI spec leaves undefined: 22-24
 *   and 26-31 (continuing the 20-31 block), 85-87, and 105-116. Nothing here
 *   collides with a defined controller, so a generic controller can send them
 *   without stepping on sustain, volume, expression, bank select, NRPN/RPN data
 *   entry, or the channel-mode messages at 120+.
 * - CC 88 is deliberately skipped: later revisions of the MIDI spec assign it as
 *   the High Resolution Velocity Prefix.
 *
 * Note that CCs at or above 120 never arrive here at all - StreamProcessor
 * filters them out before the callback (see stream_processor.cpp).
 *
 * Not in this table, because they are not continuous parameters: CC 96 and 102
 * cycle the filter and output modes on a press (footswitch style), and CC
 * 103/104 copy and paste a patch. They stay as explicit cases in handleCC. The
 * modes are *also* settable absolutely, via CC 24 and 87 below.
 */
enum class CcCurve {
    /// Value spread evenly over [min, max]
    LINEAR,
    /// Equal CC steps are equal frequency ratios. For cutoffs, where the low end
    /// of the range needs as many steps as the high end.
    EXPONENTIAL,
    /// 0 -> min, 64 -> exactly the centre of the range, 127 -> max. Aftertouch
    /// modulation amounts need an exact centre, because centre means "off".
    BIPOLAR,
};

/**
 * @brief One CC -> parameter assignment
 *
 * @c param must name a parameter WebController::setParameter accepts, which is
 * asserted by test_midi_cc rather than trusted.
 */
struct CcMapping {
    uint8_t cc;
    const char* param;
    float min;
    float max;
    CcCurve curve;
};

// Sorted by CC number so a duplicate or a free slot is easy to spot. The times
// mirror each other between the two envelopes, so a controller feels the same on
// either one.
inline constexpr CcMapping CC_MAP[] = {
    // Oscillator
    {  1, "waveformShape",          0.0f,     1.0f,     CcCurve::LINEAR      },

    // Filter and its envelope
    { 20, "baseCutoff",           100.0f, 10000.0f,     CcCurve::EXPONENTIAL },
    { 21, "filterQ",                0.1f,    20.0f,     CcCurve::LINEAR      },
    { 22, "filterEnvAmount",        0.0f,     1.0f,     CcCurve::LINEAR      },
    { 24, "filterMode",             0.0f,     4.0f,     CcCurve::LINEAR      },
    { 25, "filterEnvSustain",       0.0f,     1.0f,     CcCurve::LINEAR      },
    { 71, "filterEnvAttack",        0.001f,   2.001f,   CcCurve::LINEAR      },
    { 72, "filterEnvDecay",         0.01f,    5.01f,    CcCurve::LINEAR      },
    { 73, "filterEnvRelease",       0.01f,    5.01f,    CcCurve::LINEAR      },

    // Amplifier and its envelope
    { 23, "level",                  0.0f,     1.0f,     CcCurve::LINEAR      },
    { 26, "ampEnvAttack",           0.001f,   2.001f,   CcCurve::LINEAR      },
    { 27, "ampEnvDecay",            0.01f,    5.01f,    CcCurve::LINEAR      },
    { 28, "ampEnvSustain",          0.0f,     1.0f,     CcCurve::LINEAR      },
    { 29, "ampEnvRelease",          0.01f,    5.01f,    CcCurve::LINEAR      },

    // Vibrato and tremolo
    { 30, "vibratoRate",            0.1f,    20.0f,     CcCurve::LINEAR      },
    { 31, "vibratoDepth",           0.0f,     1.0f,     CcCurve::LINEAR      },
    { 85, "tremoloRate",            0.1f,    20.0f,     CcCurve::LINEAR      },
    { 86, "tremoloDepth",           0.0f,     1.0f,     CcCurve::LINEAR      },

    // Output stage (global - shared by all voices, not saved with a program)
    { 63, "postFilterQ",            0.1f,    20.0f,     CcCurve::LINEAR      },
    { 70, "postFilterCutoff",     100.0f, 20000.0f,     CcCurve::EXPONENTIAL },
    { 74, "drive",                  0.0f,     1.0f,     CcCurve::LINEAR      },
    { 87, "outputMode",             0.0f,     2.0f,     CcCurve::LINEAR      },

    // Aftertouch modulation amounts. Centre (CC 64) is no modulation; below
    // centre the parameter moves down with pressure, above it moves up.
    {105, "waveformShape_atMod",   -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {106, "baseCutoff_atMod",      -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {107, "filterQ_atMod",         -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {108, "filterEnvAmount_atMod", -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {109, "filterEnvSustain_atMod",-1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {110, "level_atMod",           -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {111, "vibratoRate_atMod",     -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {112, "vibratoDepth_atMod",    -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {113, "tremoloRate_atMod",     -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    {114, "tremoloDepth_atMod",    -1.0f,     1.0f,     CcCurve::BIPOLAR     },
    // Semitones rather than a [-1, 1] scaler, like the knob in the panel.
    {115, "pitchBend_atMod",      -12.0f,    12.0f,     CcCurve::BIPOLAR     },
};

inline constexpr size_t CC_MAP_SIZE = sizeof(CC_MAP) / sizeof(CC_MAP[0]);

/**
 * @brief Find the mapping for a CC number
 * @return The mapping, or nullptr if this CC is not a continuous parameter
 */
inline const CcMapping* findCcMapping(uint8_t cc) {
    for (size_t i = 0; i < CC_MAP_SIZE; ++i) {
        if (CC_MAP[i].cc == cc) {
            return &CC_MAP[i];
        }
    }
    return nullptr;
}

/**
 * @brief Convert a 7-bit CC value into a parameter value
 *
 * Runs at control rate - a few times per knob turn - so the exponential curve's
 * std::pow is not on any hot path.
 *
 * @param mapping Assignment describing the range and curve
 * @param value   Raw CC value [0, 127]
 */
inline float ccValueToParam(const CcMapping& mapping, uint8_t value) {
    if (value > 127) value = 127;

    switch (mapping.curve) {
        case CcCurve::EXPONENTIAL: {
            float normalized = static_cast<float>(value) / 127.0f;
            return mapping.min * std::pow(mapping.max / mapping.min, normalized);
        }
        case CcCurve::BIPOLAR: {
            // Split the sweep at CC 64 so that centre lands exactly on the
            // middle of the range instead of the 0.008 a plain value/127 gives.
            // "Off" has to be reachable from a controller, not merely nearly.
            const float centre = (mapping.min + mapping.max) * 0.5f;
            if (value >= 64) {
                return centre + (value - 64) * (mapping.max - centre) / 63.0f;
            }
            return centre - (64 - value) * (centre - mapping.min) / 64.0f;
        }
        case CcCurve::LINEAR:
        default: {
            float normalized = static_cast<float>(value) / 127.0f;
            return mapping.min + normalized * (mapping.max - mapping.min);
        }
    }
}

} // namespace midi
