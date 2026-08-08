#include <unity.h>
#include <cc_map.hpp>
#include <web_controller.hpp>
#include <sawtooth_synth.hpp>
#include <synth_application.hpp>
#include <vector>
#include <string>
#include <cmath>

// The CC assignment table. Two things here cannot be checked by reading the
// table: that no two parameters claim the same CC number, and that every
// parameter name in it is one the synth actually recognises. Both fail silently
// in use - a duplicate means one knob shadows another, a typo'd name means the
// controller does nothing at all - so they are asserted instead.
//
// The rest pins down the value curves, in particular that a bipolar CC can reach
// exactly zero, since zero is what "no aftertouch modulation" means.

static constexpr float SAMPLE_RATE = 48000.0f;

void setUp(void) {}
void tearDown(void) {}

// ===== Assignments =====

void test_noDuplicateCcNumbers(void) {
    for (size_t i = 0; i < midi::CC_MAP_SIZE; ++i) {
        for (size_t j = i + 1; j < midi::CC_MAP_SIZE; ++j) {
            if (midi::CC_MAP[i].cc == midi::CC_MAP[j].cc) {
                char msg[128];
                snprintf(msg, sizeof(msg), "CC %d is assigned to both %s and %s",
                         midi::CC_MAP[i].cc, midi::CC_MAP[i].param, midi::CC_MAP[j].param);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

void test_noDuplicateParams(void) {
    for (size_t i = 0; i < midi::CC_MAP_SIZE; ++i) {
        for (size_t j = i + 1; j < midi::CC_MAP_SIZE; ++j) {
            if (std::string(midi::CC_MAP[i].param) == midi::CC_MAP[j].param) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%s is assigned to both CC %d and CC %d",
                         midi::CC_MAP[i].param, midi::CC_MAP[i].cc, midi::CC_MAP[j].cc);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

// Channel mode messages (120+) never reach the CC handler - StreamProcessor
// filters them out - so an assignment up there would be dead on arrival.
void test_ccNumbersAreDeliverable(void) {
    for (size_t i = 0; i < midi::CC_MAP_SIZE; ++i) {
        char msg[96];
        snprintf(msg, sizeof(msg), "CC %d (%s) is at or above the channel-mode cutoff",
                 midi::CC_MAP[i].cc, midi::CC_MAP[i].param);
        TEST_ASSERT_TRUE_MESSAGE(midi::CC_MAP[i].cc < 120, msg);
    }
}

void test_rangesAreOrdered(void) {
    for (size_t i = 0; i < midi::CC_MAP_SIZE; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(midi::CC_MAP[i].min < midi::CC_MAP[i].max,
                                 midi::CC_MAP[i].param);
    }
}

// The headline check: every name in the table reaches a real parameter. A typo
// here would leave a controller knob doing nothing, with only a log line to say
// so.
void test_everyMappedParamIsRecognized(void) {
    std::vector<synth::WavetableSynth> voices;
    voices.emplace_back(SAMPLE_RATE);
    auto forEachVoice = [&voices](webcontrol::VoiceVisitor visitor) {
        for (auto& v : voices) visitor(v);
    };
    webcontrol::WebController controller(forEachVoice, nullptr);

    // The output stage is registered by SynthApplication, which is not built
    // here; stand in for it so those four names resolve.
    float outputStageValue = 0.0f;
    for (const char* name : {"drive", "postFilterCutoff", "postFilterQ", "outputMode"}) {
        controller.registerParam(name,
            [&outputStageValue](float v) { outputStageValue = v; },
            [&outputStageValue]() { return outputStageValue; });
    }

    for (size_t i = 0; i < midi::CC_MAP_SIZE; ++i) {
        const midi::CcMapping& mapping = midi::CC_MAP[i];
        float value = midi::ccValueToParam(mapping, 64);
        char msg[128];
        snprintf(msg, sizeof(msg), "CC %d names \"%s\", which the synth does not recognise",
                 mapping.cc, mapping.param);
        TEST_ASSERT_TRUE_MESSAGE(controller.applyNamedParam(mapping.param, value), msg);
    }
}

// ===== Value curves =====

void test_linearCurveSpansTheRange(void) {
    const midi::CcMapping m{99, "test", 0.0f, 1.0f, midi::CcCurve::LINEAR};
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, midi::ccValueToParam(m, 0), "CC 0 should be the minimum");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, midi::ccValueToParam(m, 127), "CC 127 should be the maximum");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.5f, midi::ccValueToParam(m, 64),
        "Mid travel should be mid range");
}

// Equal CC steps are equal ratios, so the bottom of a cutoff sweep gets as many
// usable steps as the top.
void test_exponentialCurveIsGeometric(void) {
    const midi::CcMapping m{99, "test", 100.0f, 10000.0f, midi::CcCurve::EXPONENTIAL};
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 100.0f, midi::ccValueToParam(m, 0),
        "CC 0 should be the minimum");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 10000.0f, midi::ccValueToParam(m, 127),
        "CC 127 should be the maximum");
    // Two decades over 127 steps puts the geometric midpoint at 1 kHz.
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(20.0f, 1000.0f, midi::ccValueToParam(m, 64),
        "Mid travel should be the geometric mean, not the arithmetic one");
}

// Centre has to be exact: for an aftertouch mod amount, centre means "off", and
// a plain value/127 sweep would leave 0.008 of modulation dialled in at CC 64.
void test_bipolarCurveHitsExactZeroAtCentre(void) {
    const midi::CcMapping m{99, "test", -1.0f, 1.0f, midi::CcCurve::BIPOLAR};
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, midi::ccValueToParam(m, 64),
        "CC 64 should be exactly zero modulation");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-1.0f, midi::ccValueToParam(m, 0),
        "CC 0 should be full downward modulation");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, midi::ccValueToParam(m, 127),
        "CC 127 should be full upward modulation");
}

// Pitch bend's atMod is in semitones, so its bipolar range is wider than [-1, 1]
// and the centre still has to land on zero.
void test_bipolarCurveScalesToWiderRanges(void) {
    const midi::CcMapping m{99, "test", -12.0f, 12.0f, midi::CcCurve::BIPOLAR};
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, midi::ccValueToParam(m, 64), "Centre is no bend");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(12.0f, midi::ccValueToParam(m, 127), "Top is an octave up");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-12.0f, midi::ccValueToParam(m, 0), "Bottom is an octave down");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.2f, 6.0f, midi::ccValueToParam(m, 96),
        "Three quarter travel should be half an octave up");
}

// ===== Lookup =====

void test_findCcMapping_findsAssignedAndRejectsFree(void) {
    const midi::CcMapping* cutoff = midi::findCcMapping(20);
    TEST_ASSERT_NOT_NULL_MESSAGE(cutoff, "CC 20 should be assigned");
    TEST_ASSERT_EQUAL_STRING("baseCutoff", cutoff->param);

    // The mode-cycling and clipboard CCs are handled as presses, not parameters,
    // so they must not be in the table.
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(96), "CC 96 cycles the filter mode, not a param");
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(102), "CC 102 cycles the output mode, not a param");
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(103), "CC 103 is a clipboard action");
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(104), "CC 104 is a clipboard action");

    // Defined MIDI controllers we must not squat on.
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(7), "CC 7 is channel volume");
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(64), "CC 64 is the sustain pedal");
    TEST_ASSERT_NULL_MESSAGE(midi::findCcMapping(88), "CC 88 is the high resolution velocity prefix");
}

// A CC actually moves the parameter it names, end to end through the same entry
// point the control panel uses.
void test_ccValueReachesTheVoice(void) {
    std::vector<synth::WavetableSynth> voices;
    voices.emplace_back(SAMPLE_RATE);
    auto forEachVoice = [&voices](webcontrol::VoiceVisitor visitor) {
        for (auto& v : voices) visitor(v);
    };
    webcontrol::WebController controller(forEachVoice, nullptr);

    const midi::CcMapping* level = midi::findCcMapping(23);
    TEST_ASSERT_NOT_NULL_MESSAGE(level, "CC 23 should drive voice level");
    controller.applyNamedParam(level->param, midi::ccValueToParam(*level, 127));
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, voices[0].getLevel(),
        "Full CC 23 should take voice level to unity");

    const midi::CcMapping* pitchAtMod = midi::findCcMapping(115);
    TEST_ASSERT_NOT_NULL_MESSAGE(pitchAtMod, "CC 115 should drive pitch bend atMod");
    controller.applyNamedParam(pitchAtMod->param, midi::ccValueToParam(*pitchAtMod, 64));
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, voices[0].getPitchBendAtMod(),
        "Centred CC 115 should mean no aftertouch bend");
}

// filterMode arrives fractional from a 128-step sweep across five modes, so the
// setter has to round rather than truncate or the top mode is unreachable.
void test_filterModeCc_reachesEveryMode(void) {
    std::vector<synth::WavetableSynth> voices;
    voices.emplace_back(SAMPLE_RATE);
    auto forEachVoice = [&voices](webcontrol::VoiceVisitor visitor) {
        for (auto& v : voices) visitor(v);
    };
    webcontrol::WebController controller(forEachVoice, nullptr);

    const midi::CcMapping* mode = midi::findCcMapping(24);
    TEST_ASSERT_NOT_NULL_MESSAGE(mode, "CC 24 should select the filter mode");

    controller.applyNamedParam(mode->param, midi::ccValueToParam(*mode, 0));
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(synth::BiquadFilter::Mode::LOWPASS),
        static_cast<int>(voices[0].getFilter().getMode()), "CC 0 should select lowpass");

    controller.applyNamedParam(mode->param, midi::ccValueToParam(*mode, 127));
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(synth::BiquadFilter::Mode::ALLPASS),
        static_cast<int>(voices[0].getFilter().getMode()), "CC 127 should select allpass");

    // Every intermediate mode is reachable somewhere in the sweep.
    for (int wanted = 0; wanted < static_cast<int>(synth::BiquadFilter::Mode::__COUNT); ++wanted) {
        bool reached = false;
        for (int value = 0; value <= 127 && !reached; ++value) {
            controller.applyNamedParam(mode->param,
                midi::ccValueToParam(*mode, static_cast<uint8_t>(value)));
            reached = static_cast<int>(voices[0].getFilter().getMode()) == wanted;
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "Filter mode %d is not reachable by CC 24", wanted);
        TEST_ASSERT_TRUE_MESSAGE(reached, msg);
    }
}

// An unrecognised name is reported rather than silently swallowed, which is what
// makes test_everyMappedParamIsRecognized meaningful.
void test_unknownParamIsRejected(void) {
    std::vector<synth::WavetableSynth> voices;
    voices.emplace_back(SAMPLE_RATE);
    auto forEachVoice = [&voices](webcontrol::VoiceVisitor visitor) {
        for (auto& v : voices) visitor(v);
    };
    webcontrol::WebController controller(forEachVoice, nullptr);

    TEST_ASSERT_FALSE_MESSAGE(controller.applyNamedParam("noSuchParam", 1.0f),
        "An unknown parameter name should be reported as unrecognised");
}

// ===== End to end, through a real application =====

// Feed a Control Change on channel 0 the way a controller would.
static void sendCc(platform::SynthApplication& app, uint8_t cc, uint8_t value) {
    app.processMidiByte(0xB0);
    app.processMidiByte(cc);
    app.processMidiByte(value);
}

// The tests above stand in for the output stage with their own registrations, so
// they cannot catch a name in cc_map.hpp that disagrees with the one
// SynthApplication::registerOutputStageParams uses - the CC would resolve to
// nothing but a log line. This drives a real application to pin those four down,
// along with the voice path, over actual MIDI bytes.
void test_endToEnd_ccBytesReachVoicesAndOutputStage(void) {
    platform::SynthApplication app(48000, 2, 2);

    sendCc(app, 23, 127);   // voice level -> unity
    sendCc(app, 24, 127);   // filter mode -> allpass
    app.getVoicePool().forEachVoice([](synth::WavetableSynth& v) {
        TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, v.getLevel(),
            "CC 23 should have taken every voice to unity level");
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(synth::BiquadFilter::Mode::ALLPASS),
            static_cast<int>(v.getFilter().getMode()),
            "CC 24 should have selected allpass on every voice");
    });

    sendCc(app, 74, 127);   // drive -> maximum
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(1.0f, app.getOutputProcessor().getDrive(),
        "CC 74 should reach the output stage's drive");

    sendCc(app, 70, 0);     // post-filter cutoff -> bottom of its range
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 100.0f,
        app.getOutputProcessor().getPostFilter().getCutoff(),
        "CC 70 should reach the post-filter cutoff");

    sendCc(app, 63, 127);   // post-filter resonance -> maximum
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 20.0f,
        app.getOutputProcessor().getPostFilter().getQ(),
        "CC 63 should reach the post-filter resonance");

    sendCc(app, 87, 127);   // clipping algorithm -> the last one
    TEST_ASSERT_EQUAL_MESSAGE(2, static_cast<int>(app.getOutputProcessor().getModeIndex()),
        "CC 87 should select the clipping algorithm absolutely");
}

// CC 102 still cycles the algorithm on a press, for a footswitch or a button that
// sends only 0 and 127.
void test_endToEnd_cyclingCcsStillWork(void) {
    platform::SynthApplication app(48000, 2, 2);

    size_t before = app.getOutputProcessor().getModeIndex();
    sendCc(app, 102, 127);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(before, app.getOutputProcessor().getModeIndex(),
        "CC 102 should cycle to the next clipping algorithm");

    sendCc(app, 96, 127);
    app.getVoicePool().forEachVoice([](synth::WavetableSynth& v) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(synth::BiquadFilter::Mode::LOWPASS),
            static_cast<int>(v.getFilter().getMode()),
            "CC 96 should cycle away from the default lowpass");
    });
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_noDuplicateCcNumbers);
    RUN_TEST(test_noDuplicateParams);
    RUN_TEST(test_ccNumbersAreDeliverable);
    RUN_TEST(test_rangesAreOrdered);
    RUN_TEST(test_everyMappedParamIsRecognized);
    RUN_TEST(test_linearCurveSpansTheRange);
    RUN_TEST(test_exponentialCurveIsGeometric);
    RUN_TEST(test_bipolarCurveHitsExactZeroAtCentre);
    RUN_TEST(test_bipolarCurveScalesToWiderRanges);
    RUN_TEST(test_findCcMapping_findsAssignedAndRejectsFree);
    RUN_TEST(test_ccValueReachesTheVoice);
    RUN_TEST(test_filterModeCc_reachesEveryMode);
    RUN_TEST(test_unknownParamIsRejected);
    RUN_TEST(test_endToEnd_ccBytesReachVoicesAndOutputStage);
    RUN_TEST(test_endToEnd_cyclingCcsStillWork);
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
