#include <unity.h>
#include <sawtooth_synth.hpp>
#include <performance_timer.hpp>

// Aftertouch modulation of waveform shape, resonance, and the vibrato/tremolo
// rates. These four parameters live inside sub-components (oscillator, filter,
// LFOs) rather than in the voice, so the voice caches their baselines: while a
// key is held the component holds the *modulated* value, and the getters must
// still report the unmodulated one. Program save and the control panel depend on
// that split, so it is what these tests pin down, alongside the modulation
// itself reaching the component.

static constexpr float SAMPLE_RATE = 48000.0f;
static constexpr float BASE_FREQ = 440.0f;

using Synth = synth::WavetableSynth;
using SynthTimer = features::LapTimer<features::NoOpTimingPolicy, 16>;

// Modulation is applied in nextSample(), so nothing reaches a component until
// audio is rendered. A handful of samples is enough; none of the applies wait on
// an envelope.
static constexpr int SETTLE_SAMPLES = 8;

// Tolerances absorb the change-thresholds in nextSample(), which skip applying a
// value that has barely moved. Each is the corresponding moved() epsilon plus
// headroom, so a test failure means the modulation is wrong, not merely lagging.
static constexpr float SHAPE_TOLERANCE = 0.005f;                                  // eps 0.002
static constexpr float Q_TOLERANCE = 0.03f * (Synth::FILTER_Q_MAX - Synth::FILTER_Q_MIN) + 0.05f;
static constexpr float RATE_TOLERANCE = 0.02f;                                    // eps 0.01

void setUp(void) {}
void tearDown(void) {}

static void render(Synth& voice, int samples = SETTLE_SAMPLES) {
    SynthTimer timer;
    for (int i = 0; i < samples; ++i) {
        (void)voice.nextSample(timer);
    }
}

// A voice sounding a note at the given pressure, with baselines already set.
static void play(Synth& voice, float aftertouch) {
    voice.trigger(BASE_FREQ, 1.0f);
    voice.setAftertouch(aftertouch);
    render(voice);
}

// ===== Baselines survive modulation =====

// The headline invariant: pressure moves the components but never the baselines
// the getters report. If this regressed, saving a program while a key was held
// would bake the modulated values in as the new patch.
void test_baselines_unaffectedByModulation(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.25f);
    voice.setFilterQ(2.0f);
    voice.setVibratoRate(4.0f);
    voice.setTremoloRate(6.0f);

    voice.setWaveformShapeAtMod(0.5f);
    voice.setFilterQAtMod(0.5f);
    voice.setVibratoRateAtMod(0.5f);
    voice.setTremoloRateAtMod(0.5f);

    play(voice, 1.0f);

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.25f, voice.getWaveformShape(),
        "Shape baseline should be unchanged by aftertouch");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(2.0f, voice.getFilterQ(),
        "Resonance baseline should be unchanged by aftertouch");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(4.0f, voice.getVibratoRate(),
        "Vibrato rate baseline should be unchanged by aftertouch");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(6.0f, voice.getTremoloRate(),
        "Tremolo rate baseline should be unchanged by aftertouch");
}

// With no modulation configured, pressure leaves the components alone.
void test_zeroAtMod_leavesComponentsAtBaseline(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.25f);
    voice.setFilterQ(2.0f);
    voice.setVibratoRate(4.0f);
    voice.setTremoloRate(6.0f);

    play(voice, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SHAPE_TOLERANCE, 0.25f, voice.getOscillator().getShape(),
        "Zero atMod should leave the oscillator shape at baseline");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(Q_TOLERANCE, 2.0f, voice.getFilter().getQ(),
        "Zero atMod should leave the filter Q at baseline");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(RATE_TOLERANCE, 4.0f, voice.getVibratoLfo().getRate(),
        "Zero atMod should leave the vibrato rate at baseline");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(RATE_TOLERANCE, 6.0f, voice.getTremoloLfo().getRate(),
        "Zero atMod should leave the tremolo rate at baseline");
}

// ===== Waveform shape =====

// Shape is additive over its own [0,1] span, so atMod 1.0 at full pressure
// sweeps a sawtooth baseline all the way to square.
void test_waveformShape_fullAtModSweepsFullSpan(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.0f);
    voice.setWaveformShapeAtMod(1.0f);
    play(voice, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SHAPE_TOLERANCE, 1.0f, voice.getOscillator().getShape(),
        "Full pressure at atMod 1.0 should morph a saw baseline to square");
}

void test_waveformShape_interpolatesWithPressure(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.0f);
    voice.setWaveformShapeAtMod(1.0f);
    play(voice, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SHAPE_TOLERANCE, 0.5f, voice.getOscillator().getShape(),
        "Half pressure should morph half the span (to triangle)");
}

void test_waveformShape_negativeAtModMorphsDownward(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.8f);
    voice.setWaveformShapeAtMod(-0.5f);
    play(voice, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SHAPE_TOLERANCE, 0.3f, voice.getOscillator().getShape(),
        "Negative atMod should morph back toward sawtooth");
}

void test_waveformShape_clampedToSpan(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.8f);
    voice.setWaveformShapeAtMod(1.0f);   // 0.8 + 1.0 overshoots
    play(voice, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SHAPE_TOLERANCE, 1.0f, voice.getOscillator().getShape(),
        "Shape overshoot should clamp at square, not wrap or exceed 1.0");
}

// ===== Resonance =====

// Q is additive over the full [0.1, 20] span, so a modest atMod is a large,
// audible resonance move rather than the barely-perceptible one a multiplicative
// law would give.
void test_filterQ_modulatedByPressure(void) {
    Synth voice(SAMPLE_RATE);
    voice.setFilterQ(2.0f);
    voice.setFilterQAtMod(0.5f);
    play(voice, 1.0f);
    float expected = 2.0f + 0.5f * (Synth::FILTER_Q_MAX - Synth::FILTER_Q_MIN);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(Q_TOLERANCE, expected, voice.getFilter().getQ(),
        "Full pressure should add half the Q span");
}

void test_filterQ_fullAtModReachesTopOfRange(void) {
    Synth voice(SAMPLE_RATE);
    voice.setFilterQ(0.707f);
    voice.setFilterQAtMod(1.0f);
    play(voice, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(Q_TOLERANCE, Synth::FILTER_Q_MAX, voice.getFilter().getQ(),
        "Full pressure at atMod 1.0 should reach maximum resonance");
}

void test_filterQ_negativeAtModClampsAtFloor(void) {
    Synth voice(SAMPLE_RATE);
    voice.setFilterQ(2.0f);
    voice.setFilterQAtMod(-1.0f);
    play(voice, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(Q_TOLERANCE, Synth::FILTER_Q_MIN, voice.getFilter().getQ(),
        "Downward Q modulation should clamp at the floor, never going negative");
}

// ===== LFO rates =====

void test_vibratoRate_modulatedByPressure(void) {
    Synth voice(SAMPLE_RATE);
    voice.setVibratoRate(5.0f);
    voice.setVibratoDepth(0.5f);
    voice.setVibratoRateAtMod(0.25f);
    play(voice, 1.0f);
    float expected = 5.0f + 0.25f * (Synth::LFO_RATE_MAX - Synth::LFO_RATE_MIN);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(RATE_TOLERANCE, expected, voice.getVibratoLfo().getRate(),
        "Pressure should speed up the vibrato LFO");
}

// Baseline is high enough that a quarter-span downward move stays inside the
// range; the clamp at the floor is covered by test_lfoRates_clampedToSpan.
void test_tremoloRate_modulatedByPressure(void) {
    Synth voice(SAMPLE_RATE);
    voice.setTremoloRate(12.0f);
    voice.setTremoloDepth(0.5f);
    voice.setTremoloRateAtMod(-0.25f);
    play(voice, 1.0f);
    float expected = 12.0f - 0.25f * (Synth::LFO_RATE_MAX - Synth::LFO_RATE_MIN);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(RATE_TOLERANCE, expected, voice.getTremoloLfo().getRate(),
        "Negative atMod should slow the tremolo LFO");
}

void test_lfoRates_clampedToSpan(void) {
    Synth voice(SAMPLE_RATE);
    voice.setVibratoRate(18.0f);
    voice.setVibratoRateAtMod(1.0f);     // overshoots the top
    voice.setTremoloRate(1.0f);
    voice.setTremoloRateAtMod(-1.0f);    // undershoots the bottom
    play(voice, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(RATE_TOLERANCE, Synth::LFO_RATE_MAX,
        voice.getVibratoLfo().getRate(), "Vibrato rate should clamp at the top of the span");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(RATE_TOLERANCE, Synth::LFO_RATE_MIN,
        voice.getTremoloLfo().getRate(), "Tremolo rate should clamp at the bottom of the span");
}

// ===== Note lifecycle =====

// A new note starts at zero pressure, so it must not inherit the previous note's
// modulated values - not even the sub-threshold remainder that the change
// guards in nextSample() would otherwise leave behind.
void test_trigger_returnsComponentsToBaseline(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.2f);
    voice.setFilterQ(3.0f);
    voice.setVibratoRate(4.0f);
    voice.setTremoloRate(4.0f);
    voice.setWaveformShapeAtMod(1.0f);
    voice.setFilterQAtMod(1.0f);
    voice.setVibratoRateAtMod(1.0f);
    voice.setTremoloRateAtMod(1.0f);

    play(voice, 1.0f);   // squeeze the first note hard
    voice.release();

    voice.trigger(BASE_FREQ, 1.0f);   // second note, untouched

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.2f, voice.getOscillator().getShape(),
        "A new note should start from the baseline shape");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(3.0f, voice.getFilter().getQ(),
        "A new note should start from the baseline resonance");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(4.0f, voice.getVibratoLfo().getRate(),
        "A new note should start from the baseline vibrato rate");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(4.0f, voice.getTremoloLfo().getRate(),
        "A new note should start from the baseline tremolo rate");
}

// Releasing pressure during a held note walks the components back down.
void test_releasingPressure_returnsComponentsToBaseline(void) {
    Synth voice(SAMPLE_RATE);
    voice.setFilterQ(2.0f);
    voice.setFilterQAtMod(1.0f);

    play(voice, 1.0f);
    TEST_ASSERT_TRUE_MESSAGE(voice.getFilter().getQ() > 10.0f,
        "Sanity: full pressure should have raised Q well above baseline");

    voice.setAftertouch(0.0f);
    render(voice);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(Q_TOLERANCE, 2.0f, voice.getFilter().getQ(),
        "Letting go should return Q to baseline");
}

// The baseline setters take effect immediately, without waiting for audio, so
// the control panel and program load work on a silent synth.
void test_baselineSetters_applyWithoutRendering(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(0.6f);
    voice.setFilterQ(7.0f);
    voice.setVibratoRate(3.0f);
    voice.setTremoloRate(9.0f);

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.6f, voice.getOscillator().getShape(),
        "Shape should reach the oscillator without rendering");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(7.0f, voice.getFilter().getQ(),
        "Resonance should reach the filter without rendering");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(3.0f, voice.getVibratoLfo().getRate(),
        "Vibrato rate should reach the LFO without rendering");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(9.0f, voice.getTremoloLfo().getRate(),
        "Tremolo rate should reach the LFO without rendering");
}

// The baseline setters clamp, so an out-of-range value from a patch file or a
// stray CC cannot poison the baseline the modulation is measured from.
void test_baselineSetters_clampOutOfRangeValues(void) {
    Synth voice(SAMPLE_RATE);
    voice.setWaveformShape(2.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, voice.getWaveformShape(),
        "Shape above 1.0 should clamp");
    voice.setWaveformShape(-1.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, voice.getWaveformShape(),
        "Shape below 0.0 should clamp");

    voice.setFilterQ(100.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(Synth::FILTER_Q_MAX, voice.getFilterQ(),
        "Q above the range should clamp");
    voice.setFilterQ(0.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(Synth::FILTER_Q_MIN, voice.getFilterQ(),
        "Q below the range should clamp");

    // The rates clamp to the same span the modulation sweeps, so the reported
    // baseline can never disagree with the one being rendered.
    voice.setVibratoRate(50.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(Synth::LFO_RATE_MAX, voice.getVibratoRate(),
        "Vibrato rate above the span should clamp");
    voice.setTremoloRate(0.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(Synth::LFO_RATE_MIN, voice.getTremoloRate(),
        "Tremolo rate below the span should clamp");
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_baselines_unaffectedByModulation);
    RUN_TEST(test_zeroAtMod_leavesComponentsAtBaseline);
    RUN_TEST(test_waveformShape_fullAtModSweepsFullSpan);
    RUN_TEST(test_waveformShape_interpolatesWithPressure);
    RUN_TEST(test_waveformShape_negativeAtModMorphsDownward);
    RUN_TEST(test_waveformShape_clampedToSpan);
    RUN_TEST(test_filterQ_modulatedByPressure);
    RUN_TEST(test_filterQ_fullAtModReachesTopOfRange);
    RUN_TEST(test_filterQ_negativeAtModClampsAtFloor);
    RUN_TEST(test_vibratoRate_modulatedByPressure);
    RUN_TEST(test_tremoloRate_modulatedByPressure);
    RUN_TEST(test_lfoRates_clampedToSpan);
    RUN_TEST(test_trigger_returnsComponentsToBaseline);
    RUN_TEST(test_releasingPressure_returnsComponentsToBaseline);
    RUN_TEST(test_baselineSetters_applyWithoutRendering);
    RUN_TEST(test_baselineSetters_clampOutOfRangeValues);
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
