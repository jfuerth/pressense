/**
 * Pressence Synth - RP2350B audio pipeline benchmark
 *
 * A standalone firmware that measures the real cycle cost of every piece of the
 * audio path on hardware, at the production clock speed. Built as its own
 * PlatformIO environment (bench_2350b) and intended to be loaded straight into
 * SRAM with `picotool load -x`, so repeated iterations don't wear out flash.
 *
 * Why this exists: the LapTimer instrumentation that used to be the only source
 * of timing data has 1 us resolution and inserts two timer reads plus a linear
 * span lookup *inside* the per-sample loop, which costs more than most of the
 * work it is trying to measure. Every number here is taken with the cycle
 * counter, with interrupts masked, reported as the minimum of several runs so
 * that USB and DMA activity can't inflate it.
 *
 * See AUDIO_PIPELINE_PERFORMANCE.md for the recorded results and conclusions.
 */

#include <stdio.h>
#include <cstdint>
#include <cmath>
#include <memory>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/sync.h"

#include <rp2350_cycle_counter.hpp>
#include <rp2350_timing_policy.hpp>
#include <sawtooth_synth.hpp>
#include <biquad_filter.hpp>
#include <lfo.hpp>
#include <adsr_envelope.hpp>
#include <wavetable_oscillator.hpp>
#include <output_processor.hpp>
#include <polyphonic_synth_target.hpp>
#include <performance_timer.hpp>

// Mirror the production configuration exactly - a benchmark at a different clock
// or buffer size measures a different machine.
static constexpr uint32_t TARGET_SYS_CLOCK_KHZ = 300000;
static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr size_t BUFFER_SIZE = 256;
static constexpr uint8_t NUM_VOICES = 8;

// The deadline: one buffer must be rendered in the time it takes to play one.
static constexpr uint32_t BUFFER_PERIOD_US = 1000000u * BUFFER_SIZE / SAMPLE_RATE;

// Filled once the clock is known: the cycle budget for one buffer.
static uint32_t g_deadlineCycles = 1;

using NoOpTimer = features::LapTimer<features::NoOpTimingPolicy, 16>;

// ---------------------------------------------------------------------------
// Measurement harness
// ---------------------------------------------------------------------------

// Inputs live in RAM and are filled at runtime, so the compiler cannot constant
// fold a libm call it would otherwise evaluate at compile time.
static float g_in[64];
static volatile float g_sink = 0.0f;

static uint32_t g_clockHz = 0;

/**
 * @brief Cycles for `iters` iterations of body, minimum of `reps` runs
 *
 * Interrupts are masked for the duration of each run so a USB or DMA interrupt
 * cannot land inside the window, and the minimum across runs discards anything
 * that still slipped through (cache/bus contention, regulator noise).
 */
template<typename F>
static uint32_t measure(int iters, int reps, F body) {
    // Warm up: settle branch predictors and any lazily-initialised libm state.
    for (int i = 0; i < 16; ++i) body(i);

    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < reps; ++r) {
        uint32_t save = save_and_disable_interrupts();
        uint32_t start = rp2350::CycleCounter::now();
        for (int i = 0; i < iters; ++i) body(i);
        uint32_t elapsed = rp2350::CycleCounter::now() - start;
        restore_interrupts(save);
        if (elapsed < best) best = elapsed;
    }
    return best;
}

// This SDK build links newlib-nano's printf, which silently prints nothing for
// %f. Everything below is therefore formatted from integers, with fixed-point
// values carried as tenths and split by hand.

/** @brief Print a value held in tenths as d.d */
static void printTenths(uint64_t tenths) {
    printf("%lu.%lu", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}

// Cycles attributable to the harness itself, in tenths of a cycle: one array
// load and one volatile store per iteration. Removed from every per-op figure.
static uint64_t g_loopOverheadTenths = 0;

static void reportOp(const char* label, int iters, uint32_t cycles) {
    uint64_t tenths = (uint64_t)cycles * 10 / iters;
    tenths = (tenths > g_loopOverheadTenths) ? tenths - g_loopOverheadTenths : 0;
    // Nanoseconds rather than microseconds, so per-op costs stay integral.
    uint64_t ns = tenths * 100000000ull / g_clockHz;
    printf("  %-34s ", label);
    printTenths(tenths);
    printf(" cycles  ");
    printTenths(ns);
    printf(" ns\n");
}

template<typename F>
static void benchOp(const char* label, int iters, F body) {
    reportOp(label, iters, measure(iters, 5, body));
}

/**
 * @brief Report a whole-buffer figure against the audio deadline
 */
static void reportBuffer(const char* label, uint32_t cycles) {
    uint64_t us10 = (uint64_t)cycles * 10000000ull / g_clockHz;
    uint64_t pct10 = (uint64_t)cycles * 1000ull / g_deadlineCycles;
    printf("  %-36s %8lu cycles  ", label, (unsigned long)cycles);
    printTenths(us10);
    printf(" us  ");
    printTenths(pct10);
    printf("%% of deadline\n");
}

template<typename F>
static void benchBuffer(const char* label, F body) {
    // One iteration is already a whole buffer, so no division; still take a min.
    reportBuffer(label, measure(1, 5, [&](int) { body(); }));
}

// ---------------------------------------------------------------------------
// Sanity checks
// ---------------------------------------------------------------------------

static void checkEnvironment() {
    printf("== Environment ==\n");
    printf("  clk_sys                            %lu Hz\n", (unsigned long)g_clockHz);
    printf("  buffer                             %u frames @ %lu Hz\n",
           (unsigned)BUFFER_SIZE, (unsigned long)SAMPLE_RATE);
    printf("  deadline                           %lu us = %lu cycles\n",
           (unsigned long)BUFFER_PERIOD_US, (unsigned long)g_deadlineCycles);
#if defined(__ARM_FP) && __ARM_FP
    printf("  hardware float (__ARM_FP)          yes (0x%x)\n", __ARM_FP);
#else
    printf("  hardware float (__ARM_FP)          NO - software float!\n");
#endif
#ifdef __ARM_FEATURE_FMA
    printf("  fused multiply-add                 yes\n");
#else
    printf("  fused multiply-add                 no\n");
#endif
#ifdef __OPTIMIZE_SIZE__
    printf("  optimisation                       -Os (size)\n");
#elif defined(__OPTIMIZE__)
    printf("  optimisation                       -O2/-O3 (speed)\n");
#else
    printf("  optimisation                       -O0 !!\n");
#endif
    printf("  sizeof(WavetableSynth)             %u bytes\n",
           (unsigned)sizeof(synth::WavetableSynth));
    printf("\n");
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

static void benchPrimitives() {
    printf("== Float primitives (per call, harness overhead removed) ==\n");
    // Establish the harness cost first, with the same shape as every other body.
    g_loopOverheadTenths = 0;
    uint32_t base = measure(2000, 5, [](int i) { g_sink = g_in[i & 63]; });
    g_loopOverheadTenths = (uint64_t)base * 10 / 2000;
    printf("  (harness: load + volatile store)   ");
    printTenths(g_loopOverheadTenths);
    printf(" cycles\n");

    benchOp("float add", 2000, [](int i) { g_sink = g_in[i & 63] + 1.5f; });
    benchOp("float multiply", 2000, [](int i) { g_sink = g_in[i & 63] * 1.5f; });
    benchOp("float divide", 2000, [](int i) { g_sink = 1.5f / g_in[i & 63]; });
    benchOp("float sqrt", 2000, [](int i) { g_sink = sqrtf(g_in[i & 63]); });
    // A double add should be conspicuously slower - the M33 FPU is single
    // precision only, so doubles are emulated in software.
    benchOp("double add (software)", 500,
            [](int i) { g_sink = (double)g_in[i & 63] + 1.5; });
    printf("\n");

    printf("== libm calls ==\n");
    benchOp("sinf", 500, [](int i) { g_sink = sinf(g_in[i & 63]); });
    benchOp("cosf", 500, [](int i) { g_sink = cosf(g_in[i & 63]); });
    benchOp("tanhf", 500, [](int i) { g_sink = tanhf(g_in[i & 63]); });
    benchOp("expf", 500, [](int i) { g_sink = expf(g_in[i & 63]); });
    benchOp("exp2f", 500, [](int i) { g_sink = exp2f(g_in[i & 63]); });
    benchOp("powf(2, x)", 500, [](int i) { g_sink = powf(2.0f, g_in[i & 63]); });
    // std::pow on float arguments should resolve to powf; if it instead promotes
    // to the double version this will be dramatically slower than powf above.
    benchOp("std::pow(2.0f, x)", 500,
            [](int i) { g_sink = std::pow(2.0f, g_in[i & 63]); });
    benchOp("std::sin(float)", 500, [](int i) { g_sink = std::sin(g_in[i & 63]); });
    benchOp("fmodf", 500, [](int i) { g_sink = fmodf(g_in[i & 63], 2.0f); });
    printf("\n");
}

static void benchComponents() {
    printf("== DSP components (per sample) ==\n");

    static synth::BiquadFilter filter(SAMPLE_RATE);
    filter.setMode(synth::BiquadFilter::Mode::LOWPASS);
    filter.setCutoff(1000.0f);
    filter.setQ(3.94f);
    benchOp("BiquadFilter::processSample", 2000,
            [](int i) { g_sink = filter.processSample(g_in[i & 63]); });

    // Alternating far-apart cutoffs defeat the internal change threshold, so
    // this is the cost of an actual coefficient recompute.
    benchOp("BiquadFilter::setCutoff (recalc)", 500, [](int i) {
        filter.setCutoff((i & 1) ? 400.0f : 4000.0f);
        g_sink = 0.0f;
    });
    // Repeating the same value takes the early-out path instead.
    benchOp("BiquadFilter::setCutoff (skipped)", 2000, [](int) {
        filter.setCutoff(1000.0f);
        g_sink = 0.0f;
    });
    benchOp("BiquadFilter::setQ (recalc)", 500, [](int i) {
        filter.setQ((i & 1) ? 2.0f : 8.0f);
        g_sink = 0.0f;
    });

    static synth::Lfo lfo(SAMPLE_RATE);
    lfo.setRate(5.0f);
    lfo.setDepth(0.5f);
    benchOp("Lfo::nextSample (depth 0.5)", 2000,
            [](int) { g_sink = lfo.nextSample(); });
    static synth::Lfo lfoOff(SAMPLE_RATE);
    lfoOff.setDepth(0.0f);
    benchOp("Lfo::nextSample (depth 0 = off)", 2000,
            [](int) { g_sink = lfoOff.nextSample(); });

    static synth::AdsrEnvelope env(SAMPLE_RATE);
    env.setAttackTime(0.06f);
    env.setDecayTime(0.25f);
    env.setSustainLevel(0.5f);
    env.setReleaseTime(0.3f);
    env.trigger();
    benchOp("AdsrEnvelope::nextSample", 2000,
            [](int) { g_sink = env.nextSample(); });

    static synth::WavetableOscillator osc(SAMPLE_RATE);
    osc.updateWavetable(0.0f);
    benchOp("Oscillator::nextSample (saw)", 2000,
            [](int) { g_sink = osc.nextSample(220.0f); });
    static synth::WavetableOscillator oscSq(SAMPLE_RATE);
    oscSq.updateWavetable(1.0f);
    benchOp("Oscillator::nextSample (square)", 2000,
            [](int) { g_sink = oscSq.nextSample(220.0f); });
    printf("\n");
}

// A voice configured like the shipped default program, which is what the synth
// actually sounds like: a resonant lowpass with the filter envelope sweeping it.
static void configureLikeDefaultProgram(synth::WavetableSynth& v) {
    v.setWaveformShape(0.0f);
    v.setBaseCutoff(222.05f);
    v.setFilterQ(3.937f);
    v.setFilterEnvelopeAmount(0.5f);
    v.getFilterEnvelope().setAttackTime(0.064f);
    v.getFilterEnvelope().setDecayTime(0.246f);
    v.getFilterEnvelope().setSustainLevel(0.0236f);
    v.getFilterEnvelope().setReleaseTime(0.325f);
}

static void benchVoice() {
    printf("== Full voice, WavetableSynth::nextSample (per sample) ==\n");

    static synth::WavetableSynth voice(SAMPLE_RATE);
    static NoOpTimer timer;

    configureLikeDefaultProgram(voice);
    voice.trigger(220.0f, 1.0f);
    benchOp("active, default program", 2000,
            [](int) { g_sink = voice.nextSample(timer); });

    // Held past the envelope's decay, cutoff stops moving and the filter's own
    // change threshold should start skipping the coefficient recompute.
    for (int i = 0; i < 48000; ++i) (void)voice.nextSample(timer);
    benchOp("active, sustained (cutoff settled)", 2000,
            [](int) { g_sink = voice.nextSample(timer); });

    static synth::WavetableSynth modulated(SAMPLE_RATE);
    configureLikeDefaultProgram(modulated);
    modulated.setVibratoDepth(0.5f);
    modulated.setTremoloDepth(0.5f);
    modulated.trigger(220.0f, 1.0f);
    modulated.setAftertouch(0.7f);
    modulated.setBaseCutoffAtMod(0.5f);
    modulated.setFilterQAtMod(0.5f);
    modulated.setVibratoRateAtMod(0.5f);
    modulated.setTremoloRateAtMod(0.5f);
    modulated.setWaveformShapeAtMod(0.5f);
    modulated.setPitchBendAtMod(2.0f);
    benchOp("active, all modulation on", 2000,
            [](int) { g_sink = modulated.nextSample(timer); });

    static synth::WavetableSynth idle(SAMPLE_RATE);
    benchOp("inactive (early return)", 2000,
            [](int) { g_sink = idle.nextSample(timer); });

    // Isolate the filter envelope's contribution: with envAmount 0 the cutoff
    // handed to the filter never moves, so the change threshold should suppress
    // every coefficient recompute.
    static synth::WavetableSynth flat(SAMPLE_RATE);
    configureLikeDefaultProgram(flat);
    flat.setFilterEnvelopeAmount(0.0f);
    flat.trigger(220.0f, 1.0f);
    for (int i = 0; i < 4800; ++i) (void)flat.nextSample(timer);
    benchOp("active, filterEnvAmount = 0", 2000,
            [](int) { g_sink = flat.nextSample(timer); });
    printf("\n");

#ifdef BIQUAD_PROFILE_UPDATES
    // How often does the coefficient recompute actually fire? The change
    // thresholds are supposed to make it rare; this checks whether they do.
    printf("== Filter coefficient recomputes per 256 samples ==\n");
    struct { const char* label; int warmup; } phases[] = {
        { "attack/decay (just triggered)", 0 },
        { "sustain (held 1 s)", 48000 },
    };
    for (auto& p : phases) {
        static synth::WavetableSynth probe(SAMPLE_RATE);
        configureLikeDefaultProgram(probe);
        probe.trigger(220.0f, 1.0f);
        for (int i = 0; i < p.warmup; ++i) (void)probe.nextSample(timer);
        synth::BiquadFilter::updateCount = 0;
        for (int i = 0; i < 256; ++i) (void)probe.nextSample(timer);
        printf("  %-34s %lu of 256 samples\n", p.label,
               (unsigned long)synth::BiquadFilter::updateCount);
    }
    printf("\n");
#endif
}

// ---------------------------------------------------------------------------
// Whole-buffer pipeline
// ---------------------------------------------------------------------------

static synth::WavetableSynth* g_voices[NUM_VOICES];
static float g_mono[BUFFER_SIZE];
static float g_stereo[BUFFER_SIZE * 2];
static int32_t g_i2s[BUFFER_SIZE];

static void setupVoices(int activeCount) {
    for (int v = 0; v < NUM_VOICES; ++v) {
        if (!g_voices[v]) g_voices[v] = new synth::WavetableSynth(SAMPLE_RATE);
        configureLikeDefaultProgram(*g_voices[v]);
        if (v < activeCount) {
            g_voices[v]->trigger(110.0f * (1 + v), 1.0f);
        } else {
            g_voices[v]->release();
            // Run the release out so the voice reports inactive.
            static NoOpTimer t;
            for (int i = 0; i < 48000; ++i) (void)g_voices[v]->nextSample(t);
        }
    }
}

// The mixing loop as production runs it: a std::function built per frame, with
// every voice reached through that indirection.
static platform::PolyphonicSynthTarget<synth::WavetableSynth>* g_pool = nullptr;

static void renderViaVoicePool() {
    static NoOpTimer timer;
    for (size_t frame = 0; frame < BUFFER_SIZE; ++frame) {
        float sample = 0.0f;
        g_pool->forEachVoice([&sample, &timer](synth::WavetableSynth& s) {
            sample += s.nextSample(timer);
        });
        g_mono[frame] = sample;
    }
}

// The same work with the indirection removed: voices reached through a plain
// array, so nextSample can be inlined into the loop.
static void renderDirect(int activeCount) {
    static NoOpTimer timer;
    (void)activeCount;
    for (size_t frame = 0; frame < BUFFER_SIZE; ++frame) {
        float sample = 0.0f;
        for (int v = 0; v < NUM_VOICES; ++v) {
            sample += g_voices[v]->nextSample(timer);
        }
        g_mono[frame] = sample;
    }
}

// Voice-major instead of frame-major: each voice renders the whole buffer before
// the next one is touched, which keeps one voice's state resident.
static void renderVoiceMajor() {
    static NoOpTimer timer;
    for (size_t frame = 0; frame < BUFFER_SIZE; ++frame) g_mono[frame] = 0.0f;
    for (int v = 0; v < NUM_VOICES; ++v) {
        synth::WavetableSynth& s = *g_voices[v];
        if (!s.isActive()) continue;   // skip silent voices entirely
        for (size_t frame = 0; frame < BUFFER_SIZE; ++frame) {
            g_mono[frame] += s.nextSample(timer);
        }
    }
}

// Every modulation destination engaged, as a patch that actually uses what the
// instrument is for would have it. This is the case that matters for headroom:
// optimisations that only pay off in silence or with static parameters are worth
// nothing if turning the knobs up drops polyphony.
static void configureFullyModulated(synth::WavetableSynth& v) {
    configureLikeDefaultProgram(v);
    v.setVibratoDepth(0.5f);        // audible vibrato -> LFO sine runs
    v.setTremoloDepth(0.5f);        // audible tremolo -> second LFO sine runs
    v.setBaseCutoffAtMod(0.6f);
    v.setFilterQAtMod(0.4f);
    v.setFilterEnvAmountAtMod(0.5f);
    v.setVibratoDepthAtMod(0.5f);
    v.setVibratoRateAtMod(0.4f);
    v.setTremoloDepthAtMod(0.5f);
    v.setTremoloRateAtMod(0.4f);
    v.setWaveformShapeAtMod(0.5f);
    v.setPitchBendAtMod(2.0f);
}

// A pressure value that keeps moving, so none of the change-detection caches in
// the voice can settle. Deterministic, and never repeats the same value twice in
// a row.
static inline float pressureAt(uint32_t tick) {
    return 0.05f + 0.9f * (float)((tick * 37u) % 64u) / 64.0f;
}

/**
 * @brief Render a buffer while aftertouch keeps arriving
 *
 * @param aftertouchPeriod Samples between pressure updates for each voice.
 *        800 is the real keyboard's rate (it scans at the ~60 Hz mains
 *        frequency); 1 is the pathological case where pressure changes on every
 *        single sample.
 */
static void renderModulated(uint32_t aftertouchPeriod) {
    static NoOpTimer timer;
    static uint32_t tick = 0;
    for (size_t f = 0; f < BUFFER_SIZE; ++f) g_mono[f] = 0.0f;
    for (int v = 0; v < NUM_VOICES; ++v) {
        synth::WavetableSynth& s = *g_voices[v];
        if (!s.isActive()) continue;
        for (size_t f = 0; f < BUFFER_SIZE; ++f) {
            if ((f % aftertouchPeriod) == 0) {
                s.setAftertouch(pressureAt(tick++));
            }
            g_mono[f] += s.nextSample(timer);
        }
    }
}

static void benchWorstCase() {
    printf("== Fully modulated voices (per sample) ==\n");

    static synth::WavetableSynth loud(SAMPLE_RATE);
    static NoOpTimer timer;
    configureFullyModulated(loud);
    loud.trigger(220.0f, 1.0f);
    loud.setAftertouch(0.7f);
    for (int i = 0; i < 4800; ++i) (void)loud.nextSample(timer);
    benchOp("all modulation, pressure held", 2000,
            [](int) { g_sink = loud.nextSample(timer); });

    static uint32_t tick = 0;
    benchOp("all modulation, pressure at 60 Hz", 2000, [](int i) {
        if ((i % 800) == 0) loud.setAftertouch(pressureAt(tick++));
        g_sink = loud.nextSample(timer);
    });
    benchOp("all modulation, pressure every sample", 2000, [](int) {
        loud.setAftertouch(pressureAt(tick++));
        g_sink = loud.nextSample(timer);
    });
    printf("\n");

    printf("== Whole buffer, all voices fully modulated ==\n");
    for (int active : {1, 4, 8}) {
        setupVoices(active);
        for (int v = 0; v < NUM_VOICES; ++v) {
            configureFullyModulated(*g_voices[v]);
        }
        // Re-trigger so the new modulation settings apply from a clean start.
        for (int v = 0; v < active; ++v) g_voices[v]->trigger(110.0f * (1 + v), 1.0f);
        for (int v = 0; v < active; ++v) g_voices[v]->setAftertouch(0.7f);

        char label[72];
        snprintf(label, sizeof(label), "%d voice(s), pressure at 60 Hz", active);
        reportBuffer(label, measure(1, 5, [](int) { renderModulated(800); }));
        snprintf(label, sizeof(label), "%d voice(s), pressure every sample", active);
        reportBuffer(label, measure(1, 5, [](int) { renderModulated(1); }));
    }
    printf("\n");
}

static void benchPipeline() {
    printf("== Mixing loop structure (whole %u-frame buffer, 8 voices allocated) ==\n",
           (unsigned)BUFFER_SIZE);

    if (!g_pool) {
        g_pool = new platform::PolyphonicSynthTarget<synth::WavetableSynth>(
            NUM_VOICES,
            []() { return std::make_unique<synth::WavetableSynth>(SAMPLE_RATE); });
    }

    for (int active : {1, 2, 4, 8}) {
        setupVoices(active);
        char label[64];

        // Point the pool's voices at the same configuration for a fair compare.
        int idx = 0;
        g_pool->forEachVoice([&](synth::WavetableSynth& s) {
            configureLikeDefaultProgram(s);
            if (idx < active) s.trigger(110.0f * (1 + idx), 1.0f);
            else s.release();
            ++idx;
        });

        snprintf(label, sizeof(label), "%d voice(s): via forEachVoice", active);
        reportBuffer(label, measure(1, 5, [](int) { renderViaVoicePool(); }));

        setupVoices(active);
        snprintf(label, sizeof(label), "%d voice(s): direct array", active);
        reportBuffer(label, measure(1, 5, [&](int) { renderDirect(active); }));

        setupVoices(active);
        snprintf(label, sizeof(label), "%d voice(s): voice-major + skip idle", active);
        reportBuffer(label, measure(1, 5, [](int) { renderVoiceMajor(); }));
    }
    printf("\n");

    printf("== Post-processing and output (whole buffer) ==\n");
    static synth::OutputProcessor output(0.5f, SAMPLE_RATE);
    for (size_t i = 0; i < BUFFER_SIZE; ++i) g_mono[i] = g_in[i & 63] * 0.5f;
    benchBuffer("OutputProcessor (tanh + biquad)",
                [] { output.processBuffer(g_mono, BUFFER_SIZE); });

    // For reference: what the waveshaper used to cost when it called libm.
    // OutputProcessor now uses synth::fastTanh, so this line is no longer part
    // of the measurement above - it is the baseline that replaced.
    benchBuffer("  (libm tanhf over buffer, for ref)", [] {
        for (size_t i = 0; i < BUFFER_SIZE; ++i) g_mono[i] = tanhf(g_mono[i]);
    });
    benchBuffer("  fastTanh over buffer", [] {
        for (size_t i = 0; i < BUFFER_SIZE; ++i) g_mono[i] = synth::fastTanh(g_mono[i]);
    });

    benchBuffer("mono -> stereo duplication", [] {
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            g_stereo[i * 2] = g_mono[i];
            g_stereo[i * 2 + 1] = g_mono[i];
        }
    });

    benchBuffer("stereo float -> int32 (mono out)", [] {
        const float scale = 0.05f * 1073741824.0f;
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            g_i2s[i] = static_cast<int32_t>(g_stereo[i * 2] * scale);
        }
    });
    printf("\n");
}

/**
 * @brief Cost of the LapTimer instrumentation itself
 *
 * The production span names are threaded through the per-sample path, so if
 * telemetry is ever switched on this is what it adds per sample.
 */
static void benchInstrumentation() {
    printf("== Instrumentation overhead ==\n");
    static features::LapTimer<rp2350::Rp2350TimingPolicy, 16> realTimer;
    benchOp("LapTimer::nextSpan (time_us_64)", 500, [](int) {
        realTimer.nextSpan("bench:span");
        g_sink = 0.0f;
    });
    benchOp("time_us_64() alone", 500,
            [](int) { g_sink = (float)(time_us_64() & 0xFF); });
    printf("\n");
}

int main() {
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(TARGET_SYS_CLOCK_KHZ, true);

    stdio_init_all();

    // Wait for a host to actually open the port (CDC asserts DTR), so the report
    // isn't printed into the void while the device is still re-enumerating after
    // picotool restarted us. Falls through after 30 s so the board is still
    // usable standalone.
    for (int i = 0; i < 600 && !stdio_usb_connected(); ++i) sleep_ms(50);
    sleep_ms(500);

    g_clockHz = clock_get_hz(clk_sys);
    g_deadlineCycles = (uint32_t)((uint64_t)g_clockHz * BUFFER_SIZE / SAMPLE_RATE);

    // Fill the input array with values that are legal for every function under
    // test (positive, modest magnitude) but not known at compile time.
    for (int i = 0; i < 64; ++i) {
        g_in[i] = 0.1f + 0.9f * (float)((i * 37) % 64) / 64.0f;
    }

    bool haveCycles = rp2350::CycleCounter::enable();

    printf("\n\n########## Pressence audio pipeline benchmark ##########\n\n");
    if (!haveCycles) {
        printf("FATAL: DWT cycle counter unavailable; results would be meaningless.\n");
        while (true) tight_loop_contents();
    }

    checkEnvironment();
    benchPrimitives();
    benchComponents();
    benchVoice();
    benchWorstCase();
    benchPipeline();
    benchInstrumentation();

    printf("########## end of benchmark ##########\n\n");

    // Idle rather than return, so the report stays readable and the USB stack
    // keeps servicing the host.
    while (true) {
        sleep_ms(1000);
    }
}
