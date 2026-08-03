#include <unity.h>
#include <sawtooth_synth.hpp>
#include <performance_timer.hpp>
#include <cmath>

// Aftertouch-driven pitch bend: full aftertouch should bend a voice's pitch by
// its configured number of semitones, interpolating linearly with pressure and
// summing with the MIDI pitch wheel. These tests render real audio through the
// voice and recover the fundamental frequency, so they guard the whole DSP path
// (setter -> semitone sum -> 2^(n/12) -> oscillator increment), not just the math.

static constexpr float SAMPLE_RATE = 48000.0f;
static constexpr float BASE_FREQ = 440.0f;   // A4, comfortably below the default cutoff

using SynthTimer = features::LapTimer<features::NoOpTimingPolicy, 16>;

void setUp(void) {}
void tearDown(void) {}

// Recover the fundamental frequency from the rendered output by counting rising
// zero crossings (one per waveform period, regardless of morph shape). A warmup
// skips the amplitude envelope's attack/decay so we sample steady-state pitch.
static float measureFrequency(synth::WavetableSynth& voice) {
    SynthTimer timer;
    for (int i = 0; i < 4800; ++i) {   // ~100 ms warmup -> envelope sustain
        (void)voice.nextSample(timer);
    }

    const int numSamples = 48000;      // 1 s window -> ~0.1% counting resolution
    int risingCrossings = 0;
    float prev = voice.nextSample(timer);
    for (int i = 1; i < numSamples; ++i) {
        float s = voice.nextSample(timer);
        if (prev <= 0.0f && s > 0.0f) {
            ++risingCrossings;
        }
        prev = s;
    }
    return risingCrossings * SAMPLE_RATE / static_cast<float>(numSamples - 1);
}

// Assert a measured frequency matches 440 * 2^(semitones/12) within 2.5%
// (a semitone is ~5.9%, so this comfortably distinguishes adjacent shifts).
static void assertPitchShift(synth::WavetableSynth& voice, float expectedSemitones,
                             const char* msg) {
    float expected = BASE_FREQ * std::pow(2.0f, expectedSemitones / 12.0f);
    float measured = measureFrequency(voice);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(expected * 0.025f, expected, measured, msg);
}

// Zero aftertouch leaves pitch untouched, even with a large bend range set.
void test_zeroAftertouch_noPitchChange(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendAtMod(12.0f);
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(0.0f);
    assertPitchShift(voice, 0.0f, "No aftertouch should not bend pitch");
}

// Full aftertouch with +12 range bends up exactly one octave.
void test_fullAftertouch_bendsUpFullRange(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendAtMod(12.0f);
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(1.0f);
    assertPitchShift(voice, 12.0f, "Full aftertouch at +12 should bend up an octave");
}

// A negative range bends downward with pressure.
void test_fullAftertouch_bendsDownFullRange(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendAtMod(-12.0f);
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(1.0f);
    assertPitchShift(voice, -12.0f, "Full aftertouch at -12 should bend down an octave");
}

// Half pressure interpolates to half the range (in semitones).
void test_halfAftertouch_interpolatesLinearly(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendAtMod(12.0f);
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(0.5f);
    assertPitchShift(voice, 6.0f, "Half aftertouch should bend half the range");
}

// An arbitrary in-between range (2 semitones) works, not just the extremes.
void test_smallRange_twoSemitones(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendAtMod(2.0f);
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(1.0f);
    assertPitchShift(voice, 2.0f, "Full aftertouch at +2 should bend up two semitones");
}

// Aftertouch bend and the MIDI pitch wheel sum in the semitone domain.
void test_combinesWithPitchWheel(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendRange(2.0f);
    voice.setPitchBend(1.0f);        // +2 semitones from the wheel
    voice.setPitchBendAtMod(12.0f);  // +12 semitones from full aftertouch
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(1.0f);
    assertPitchShift(voice, 14.0f, "Wheel (+2) and aftertouch (+12) should sum to +14");
}

// The range is clamped to [-12, +12] semitones by the setter.
void test_rangeClampedToOctave(void) {
    synth::WavetableSynth voice(SAMPLE_RATE);
    voice.setPitchBendAtMod(24.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(12.0f, voice.getPitchBendAtMod(),
        "Range above +12 should clamp to +12");
    voice.setPitchBendAtMod(-24.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-12.0f, voice.getPitchBendAtMod(),
        "Range below -12 should clamp to -12");
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_zeroAftertouch_noPitchChange);
    RUN_TEST(test_fullAftertouch_bendsUpFullRange);
    RUN_TEST(test_fullAftertouch_bendsDownFullRange);
    RUN_TEST(test_halfAftertouch_interpolatesLinearly);
    RUN_TEST(test_smallRange_twoSemitones);
    RUN_TEST(test_combinesWithPitchWheel);
    RUN_TEST(test_rangeClampedToOctave);
    UNITY_END();
}

extern "C" {
#ifdef PLATFORM_ESP32
void app_main() {
    RUN_UNITY_TESTS();
}
#endif

#ifdef PLATFORM_RP2350
#include <pico/stdlib.h>
int main() {
    stdio_init_all();

    // Wait until the USB serial port is connected by a host terminal
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    RUN_UNITY_TESTS();

    // Don't return from main, because anything buffered on stdout will be lost.
    while (true) {
        tight_loop_contents();
    }

    return 0;
}
#endif

#ifdef PLATFORM_NATIVE
int main(int argc, char **argv) {
    RUN_UNITY_TESTS();
    return 0;
}
#endif
}
