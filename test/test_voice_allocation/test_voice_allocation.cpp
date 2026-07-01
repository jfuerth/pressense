#include <unity.h>
#include <polyphonic_synth_target.hpp>
#include <sawtooth_synth.hpp>
#include <memory>

// Enable global new/delete tracking for this test program
#define ENABLE_MEMORY_TRACKING
#include "memory_tracker.hpp"

// Provide std::make_unique for C++11 compatibility (matches other suites)
#if __cplusplus <= 201103L
namespace std {
    template <typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}
#endif

using Target = platform::PolyphonicSynthTarget<synth::WavetableSynth>;

static Target::VoiceFactory makeVoiceFactory() {
    return []() { return std::make_unique<synth::WavetableSynth>(48000.0f); };
}

void setUp(void) {}
void tearDown(void) {}

void test_voiceCount_matchesMaxVoices(void) {
    Target target(6, makeVoiceFactory());
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, target.getVoiceCount(),
        "Voice pool should pre-allocate the requested number of voices");
}

// The real-time MIDI path - note on/off, aftertouch, pitch bend, voice stealing,
// and per-voice iteration - must not touch the heap. The voice pool is
// pre-allocated at construction; this test guards that guarantee against
// regressions (e.g. a future voice type that allocates on trigger).
void test_realtimeNoteOps_shouldNotAllocate(void) {
    // Construction is allowed to allocate (the voice pool + any per-voice buffers).
    Target target(4, makeVoiceFactory());

    // Warm up: force any one-time lazy initialization inside the voices to happen
    // now, so the tracked block below observes only steady-state real-time work.
    for (uint8_t n = 60; n < 64; ++n) target.noteOn(n, 100);
    for (uint8_t n = 60; n < 64; ++n) target.noteOff(n, 0);
    target.forEachVoice([](synth::WavetableSynth& v) { (void)v.isActive(); });

    TEST_NO_HEAP_ALLOCATIONS({
        // More simultaneous notes than voices -> exercises voice stealing.
        target.noteOn(60, 100);
        target.noteOn(64, 100);
        target.noteOn(67, 100);
        target.noteOn(72, 100);
        target.noteOn(76, 110);  // steals
        target.noteOn(79, 110);  // steals

        // Per-note and channel-wide modulation.
        target.polyAftertouch(64, 90);
        target.channelAftertouch(70);
        target.pitchBend(2000);

        // Release a slot and re-trigger it.
        target.noteOff(60, 0);
        target.noteOn(60, 100);

        // Per-voice iteration, as used by the audio render loop.
        target.forEachVoice([](synth::WavetableSynth& v) { (void)v.isActive(); });

        // Release everything.
        target.noteOff(64, 0);
        target.noteOff(67, 0);
        target.noteOff(72, 0);
        target.noteOff(76, 0);
        target.noteOff(79, 0);
    });
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_voiceCount_matchesMaxVoices);
    RUN_TEST(test_realtimeNoteOps_shouldNotAllocate);
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
    // If we loop here, PlatformIO will eventually see that the tests finished and move on.
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
