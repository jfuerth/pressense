#include <unity.h>
#include <fast_math.hpp>
#include <cmath>
#include <cstdio>

// The fast sine and exp2 replace libm inside the per-sample audio path, so they
// have to be right across their whole input range, not just at convenient points.
// A wrong sine would detune vibrato and skew every filter coefficient; a wrong
// exp2 would put every modulated note out of tune. These tests sweep densely and
// compare against libm.

static constexpr float TWO_PI = 6.28318530717958648f;

void setUp(void) {}
void tearDown(void) {}

// Absolute error, since sin's output is O(1) and passes through zero.
void test_fastSin2Pi_matchesLibmAcrossOnePeriod(void) {
    float worst = 0.0f;
    float worstAt = 0.0f;
    for (int i = 0; i <= 20000; ++i) {
        float phase = static_cast<float>(i) / 20000.0f;
        float expected = std::sin(TWO_PI * phase);
        float error = std::fabs(synth::fastSin2Pi(phase) - expected);
        if (error > worst) { worst = error; worstAt = phase; }
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "worst sin error %.2e at phase %.4f", worst, worstAt);
    TEST_ASSERT_TRUE_MESSAGE(worst < 1e-5f, msg);
}

// The quarter-period folding is where a sign or reflection mistake would hide, so
// pin the exact quarter boundaries.
void test_fastSin2Pi_quarterBoundaries(void) {
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f, 0.0f, synth::fastSin2Pi(0.0f), "sin(0)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f, 1.0f, synth::fastSin2Pi(0.25f), "sin(pi/2)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f, 0.0f, synth::fastSin2Pi(0.5f), "sin(pi)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f, -1.0f, synth::fastSin2Pi(0.75f), "sin(3pi/2)");
    // Just below a full period should be just below zero from underneath.
    TEST_ASSERT_TRUE_MESSAGE(synth::fastSin2Pi(0.999f) < 0.0f,
                             "Approaching a full period should still be negative");
}

// Callers shouldn't have to normalise, so out-of-range phase must wrap.
void test_fastSin2Pi_wrapsOutOfRangePhase(void) {
    for (float base = -3.0f; base <= 3.0f; base += 1.0f) {
        for (int i = 0; i < 16; ++i) {
            float frac = static_cast<float>(i) / 16.0f;
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2e-5f,
                synth::fastSin2Pi(frac), synth::fastSin2Pi(frac + base),
                "Phase offset by a whole period should give the same value");
        }
    }
}

void test_fastCos2Pi_matchesLibm(void) {
    float worst = 0.0f;
    for (int i = 0; i <= 20000; ++i) {
        float phase = static_cast<float>(i) / 20000.0f;
        float error = std::fabs(synth::fastCos2Pi(phase) - std::cos(TWO_PI * phase));
        if (error > worst) worst = error;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "worst cos error %.2e", worst);
    TEST_ASSERT_TRUE_MESSAGE(worst < 1e-5f, msg);
}

// Relative error for exp2, since its output spans many orders of magnitude.
void test_fastExp2_matchesLibmOverAudioRange(void) {
    float worst = 0.0f;
    float worstAt = 0.0f;
    // Pitch work stays within a few octaves; go wider than that.
    for (int i = -16000; i <= 16000; ++i) {
        float x = static_cast<float>(i) / 1000.0f;   // -16 .. +16
        float expected = std::exp2(x);
        float relative = std::fabs(synth::fastExp2(x) - expected) / expected;
        if (relative > worst) { worst = relative; worstAt = x; }
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "worst exp2 relative error %.2e at x=%.3f", worst, worstAt);
    TEST_ASSERT_TRUE_MESSAGE(worst < 1e-5f, msg);
}

void test_fastExp2_exactAtIntegers(void) {
    for (int i = -20; i <= 20; ++i) {
        float expected = std::exp2(static_cast<float>(i));
        float actual = synth::fastExp2(static_cast<float>(i));
        char msg[64];
        snprintf(msg, sizeof(msg), "2^%d", i);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(expected * 1e-5f, expected, actual, msg);
    }
}

// The whole point of this one is tuning, so check it in cents. A just-noticeable
// pitch difference is around 5 cents; anything here should be far below that.
void test_semitonesToRatio_isInTune(void) {
    float worstCents = 0.0f;
    for (int i = -2400; i <= 2400; ++i) {
        float semitones = static_cast<float>(i) / 100.0f;   // -24 .. +24, in cents
        float expected = std::pow(2.0f, semitones / 12.0f);
        float actual = synth::semitonesToRatio(semitones);
        // Convert the ratio error to cents: 1200 * log2(actual/expected)
        float cents = std::fabs(1200.0f * std::log2(actual / expected));
        if (cents > worstCents) worstCents = cents;
    }
    char msg[80];
    snprintf(msg, sizeof(msg), "worst tuning error %.4f cents", worstCents);
    TEST_ASSERT_TRUE_MESSAGE(worstCents < 0.05f, msg);
}

// An octave must be exactly an octave, or every transposed note drifts.
void test_semitonesToRatio_octavesAreExact(void) {
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-5f, 1.0f, synth::semitonesToRatio(0.0f), "unison");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, 2.0f, synth::semitonesToRatio(12.0f), "octave up");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, 0.5f, synth::semitonesToRatio(-12.0f), "octave down");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, 4.0f, synth::semitonesToRatio(24.0f), "two octaves");
}

// Extreme inputs must not produce a garbage exponent or a NaN.
void test_fastExp2_saturatesInsteadOfOverflowing(void) {
    TEST_ASSERT_TRUE_MESSAGE(std::isfinite(synth::fastExp2(1000.0f)),
                             "Huge input should saturate, not overflow to inf/NaN");
    TEST_ASSERT_TRUE_MESSAGE(synth::fastExp2(-1000.0f) >= 0.0f,
                             "Very negative input should stay non-negative");
    TEST_ASSERT_TRUE_MESSAGE(std::isfinite(synth::fastExp2(-1000.0f)),
                             "Very negative input should stay finite");
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_fastSin2Pi_matchesLibmAcrossOnePeriod);
    RUN_TEST(test_fastSin2Pi_quarterBoundaries);
    RUN_TEST(test_fastSin2Pi_wrapsOutOfRangePhase);
    RUN_TEST(test_fastCos2Pi_matchesLibm);
    RUN_TEST(test_fastExp2_matchesLibmOverAudioRange);
    RUN_TEST(test_fastExp2_exactAtIntegers);
    RUN_TEST(test_semitonesToRatio_isInTune);
    RUN_TEST(test_semitonesToRatio_octavesAreExact);
    RUN_TEST(test_fastExp2_saturatesInsteadOfOverflowing);
    UNITY_END();
}

extern "C" {
#ifdef PLATFORM_ESP32
void app_main() { RUN_UNITY_TESTS(); }
#endif

#ifdef PLATFORM_RP2350
#include <pico/stdlib.h>
int main() {
    stdio_init_all();
    while (!stdio_usb_connected()) { sleep_ms(100); }
    RUN_UNITY_TESTS();
    while (true) { tight_loop_contents(); }
    return 0;
}
#endif

#ifdef PLATFORM_NATIVE
int main(int argc, char** argv) {
    RUN_UNITY_TESTS();
    return 0;
}
#endif
}
