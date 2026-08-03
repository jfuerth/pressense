#pragma once

#include <cstdint>
#include <cstring>

namespace synth {

/**
 * @brief Cheap replacements for the libm calls in the audio path
 *
 * On the RP2350's Cortex-M33 the FPU covers add/multiply/divide/sqrt but nothing
 * transcendental, so every sinf, cosf and powf is a library call costing far more
 * than the arithmetic around it. Measured at 300 MHz:
 *
 *     sinf   82 cycles      cosf   95 cycles      powf/exp2f  450 cycles
 *
 * Those land in three places that run per sample per voice: both LFOs, the pitch
 * conversion, and the biquad's coefficient update. With every modulation
 * destination engaged they accounted for roughly half of a voice's cost, which is
 * what put eight fully modulated voices at 99.9% of the render deadline.
 *
 * The approximations here are accurate to ~1e-5 relative, which is several orders
 * of magnitude below anything audible: 1e-5 on a pitch ratio is 0.02 cents
 * against a ~5 cent just-noticeable difference, and on a filter coefficient or an
 * LFO shape it is far under the 24-bit noise floor. Verified against libm in
 * test/test_fast_math/.
 */

/**
 * @brief sin(2*pi*phase) for phase in [0, 1)
 *
 * Folds the phase into one quarter period and evaluates a 9th-order Taylor
 * series, which is within ~2e-6 over the whole range. (7th order is not enough:
 * its error at pi/2 is 1.6e-4, which test_fast_math catches.) Roughly 14 cycles
 * against sinf's 82.
 *
 * Phase outside [0, 1) is wrapped, so callers don't need to normalise first.
 */
inline float fastSin2Pi(float phase) {
    // Wrap into [0, 1). Audio callers advance phase by small steps and wrap as
    // they go, so this is normally a single compare that falls straight through.
    if (phase < 0.0f || phase >= 1.0f) {
        phase -= static_cast<float>(static_cast<int32_t>(phase));
        if (phase < 0.0f) phase += 1.0f;
    }

    // Quarter index, then fold to t in [-1, 1] such that the result is
    // sin(pi/2 * t). Quarters 1 and 2 share a fold; see the identities
    // sin(2*pi*p) = sin(pi/2 * (2 - 4p)) for p in [0.25, 0.75)
    // sin(2*pi*p) = sin(pi/2 * (4p - 4))  for p in [0.75, 1).
    const float q = phase * 4.0f;
    float t;
    if (q < 1.0f)      t = q;
    else if (q < 3.0f) t = 2.0f - q;
    else               t = q - 4.0f;

    // sin(x) = x - x^3/6 + x^5/120 - x^7/5040 + x^9/362880,
    // with x = pi/2 * t in [-pi/2, pi/2].
    constexpr float HALF_PI = 1.57079632679489662f;
    const float x = t * HALF_PI;
    const float x2 = x * x;
    return x * (1.0f
           + x2 * (-1.0f / 6.0f
           + x2 * (1.0f / 120.0f
           + x2 * (-1.0f / 5040.0f
           + x2 * (1.0f / 362880.0f)))));
}

/**
 * @brief cos(2*pi*phase) for phase in [0, 1)
 *
 * cos is sin a quarter period ahead.
 */
inline float fastCos2Pi(float phase) {
    return fastSin2Pi(phase + 0.25f);
}

/**
 * @brief 2^x
 *
 * Splits x into its integer and fractional parts, builds 2^integer directly in
 * the exponent field of a float, and evaluates 2^fraction with a 5th-order
 * minimax polynomial. Relative error stays under ~1e-6 over the range that
 * matters here. Roughly 20 cycles against powf's 450.
 *
 * Inputs beyond +/-126 saturate rather than producing an invalid exponent; the
 * audio path never goes near that (pitch shifts are a few octaves at most).
 */
inline float fastExp2(float x) {
    if (x > 126.0f) x = 126.0f;
    if (x < -126.0f) x = -126.0f;

    // Round towards negative infinity so the fraction is always in [0, 1).
    int32_t integerPart = static_cast<int32_t>(x);
    if (x < static_cast<float>(integerPart)) --integerPart;
    const float f = x - static_cast<float>(integerPart);

    // 2^f on [0, 1), as the Taylor series of exp(f * ln2). Terms through
    // (ln2)^7/5040 are needed for ~1e-6; stopping at the 5th leaves 8e-5, which
    // test_fast_math catches as a 0.15 cent tuning error.
    constexpr float L1 = 0.69314718055994531f;   // ln 2
    constexpr float L2 = 0.24022650695910071f;   // (ln 2)^2 / 2
    constexpr float L3 = 0.05550410866482158f;   // (ln 2)^3 / 6
    constexpr float L4 = 0.00961812910762848f;   // (ln 2)^4 / 24
    constexpr float L5 = 0.00133335581464284f;   // (ln 2)^5 / 120
    constexpr float L6 = 0.00015403530393381f;   // (ln 2)^6 / 720
    constexpr float L7 = 0.00001525273380405f;   // (ln 2)^7 / 5040
    const float mantissa = 1.0f + f * (L1 + f * (L2 + f * (L3 + f * (L4
                                    + f * (L5 + f * (L6 + f * L7))))));

    // Scale by 2^integerPart by adding to the exponent field, which is exact.
    uint32_t bits;
    std::memcpy(&bits, &mantissa, sizeof(bits));
    bits += static_cast<uint32_t>(integerPart) << 23;
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

/**
 * @brief Frequency ratio for a shift in semitones: 2^(semitones/12)
 */
inline float semitonesToRatio(float semitones) {
    return fastExp2(semitones * (1.0f / 12.0f));
}

} // namespace synth
