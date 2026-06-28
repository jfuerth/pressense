/**
 * Pressence Synth - RP2350B Platform Entry Point
 * 
 * Phase 3: Full synth pipeline - capacitive keys -> MIDI -> synth -> I2S audio
 */

#include <stdio.h>
#include <cstdint>
#include <cmath>
#include <memory>
#include <type_traits>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

// Platform-specific implementations
#include <pio_capacitive_scanner.hpp>
#include <rp2350_audio_sink.hpp>
#include <rp2350_telemetry_sink.hpp>
#include <rp2350_timing_policy.hpp>
#include <embedded_program_storage.hpp>

// Synth modules
#include <synth_application.hpp>
#include <performance_timer.hpp>
#include <sawtooth_synth.hpp>

// MIDI keyboard controller
#include <midi_keyboard_controller.hpp>

// System clock: overclock to 300 MHz (2x the RP2350's 150 MHz default spec).
// Requires a core-voltage bump to stay stable; see main().
static constexpr uint32_t TARGET_SYS_CLOCK_KHZ = 300000;

// Audio configuration
static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr size_t BUFFER_SIZE = 256;

// I2S pin configuration: three consecutive pins - data, bclk, lrclk
static constexpr uint8_t I2S_FIRST_PIN = 32;

// Key scanner configuration
static constexpr uint8_t FIRST_KEY_PIN = 0;
static constexpr uint8_t NUM_KEYS = 32;

// Mains hum rejection: oversample each key and average over an integer number
// of AC line cycles. A boxcar integral over one full mains period has a null at
// the line frequency and all its harmonics, cancelling the hum that was beating
// against the previous ~64 Hz scan rate.
static constexpr int MAINS_FREQUENCY_HZ = 60;
static constexpr int MAINS_CYCLES_PER_SCAN = 1;  // integration window, in line cycles
static constexpr int64_t MAINS_AVERAGE_WINDOW_US =
    (1000000LL * MAINS_CYCLES_PER_SCAN) / MAINS_FREQUENCY_HZ;
static constexpr uint8_t NUM_VOICES = 8;

static constexpr float MASTER_VOLUME = 0.05f;  // Master volume scaling factor (0.0 to 1.0)

static constexpr bool ENABLE_AUDIO_TIMING_TELEMETRY = false;  // Set to true to enable timing telemetry output (adds overhead)

// Type aliases
using Scanner = rp2350::PioCapacitiveScanner<FIRST_KEY_PIN, NUM_KEYS>;
using AudioSink = rp2350::Rp2350AudioSink<BUFFER_SIZE>;
using MidiController = midi::MidiKeyboardController<NUM_KEYS>;
using AudioTimer = features::LapTimer<
    std::conditional_t<ENABLE_AUDIO_TIMING_TELEMETRY,
                       rp2350::Rp2350TimingPolicy,
                       features::NoOpTimingPolicy>,
    12>;
using AudioTimingStats = features::TimingStats<12>;

// Telemetry emission interval (in audio frames)
static constexpr uint32_t TIMING_TELEMETRY_INTERVAL = 100;  // ~every 0.5 seconds at 48kHz/256 frames

// Global instances
static AudioSink* audioSink = nullptr;
static MidiController* keyboard = nullptr;
static platform::SynthApplication* synthApp = nullptr;
static rp2350::Rp2350TelemetrySink<AudioTimingStats>* timingSink = nullptr;

// Shared state for cross-core timing telemetry
// Core 1 writes stats here, core 0 reads and emits telemetry
static volatile bool timingStatsReady = false;
static AudioTimingStats sharedTimingStats;

/**
 * @brief Generate audio samples from all synth voices into the buffer
 */
void generateAudio(int32_t* buffer, size_t length, AudioTimer& timer) {
    // Use a temporary float buffer for SynthApplication
    static float floatBuffer[BUFFER_SIZE * 2];  // Stereo
    
    synthApp->renderAudio(floatBuffer, length, timer);
    
    // Convert stereo float to mono int32 (take left channel)
    timer.nextSpan("main:float_to_int");
    const float INTEGER_SCALE = 1073741824.0f; // 2^30
    for (size_t i = 0; i < length; i++) {
        float sample = floatBuffer[i * 2] * MASTER_VOLUME * INTEGER_SCALE;
        buffer[i] = static_cast<int32_t>(sample);
    }
    timer.end();
}

/**
 * @brief Audio generation loop (runs on core 1)
 *
 * Uses triple-buffering approach: while buffer A is being transmitted via DMA,
 * we fill buffer B. When DMA completes, we immediately swap to buffer B
 * (which is already filled) and start filling buffer A for the next swap.
 */
void core1_audio_loop() {
    printf("Core 1: audio loop started\n");

    // Timer for performance measurement
    AudioTimer timer;
    uint32_t frameCount = 0;
    
    // Pre-fill the inactive buffer so it's ready when the first DMA completes
    generateAudio(audioSink->getInactiveBuffer(), BUFFER_SIZE, timer);

    while (true) {
        // Start measuring wait time for the next audio frame request
        timer.nextSpan("main:wait_for_dma");
        
        // Wait for the current DMA transfer to complete
        while (!audioSink->isTransferComplete()) {
            tight_loop_contents();
        }

        // Immediately swap to the pre-filled buffer and start DMA
        audioSink->swapBuffers();
        
        // Now fill the new inactive buffer while DMA runs on the other one
        generateAudio(audioSink->getInactiveBuffer(), BUFFER_SIZE, timer);
        
        // Periodically signal core 0 to emit timing telemetry
        // (Don't do printf/JSON from core 1 - causes crashes)
        if (++frameCount >= TIMING_TELEMETRY_INTERVAL) {
            if (!timingStatsReady) {
                // Copy stats to shared buffer for core 0 to emit
                sharedTimingStats = timer.getStats();
                __sync_synchronize();  // Memory barrier to ensure write is visible
                timingStatsReady = true;
            }
            timer.reset();
            frameCount = 0;
        }
    }
}

int main() {
    // Overclock to 300 MHz. Raise the core voltage first (the 1.10 V default is
    // not enough headroom for 2x the rated clock) and let it settle before
    // switching the system PLL. Done before stdio/peripheral init so clk_peri,
    // I2S, and PIO dividers are all derived from the final clock.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);  // allow the regulator to settle at the new voltage
    set_sys_clock_khz(TARGET_SYS_CLOCK_KHZ, true);

    // Initialize USB stdio
    stdio_init_all();
    
    // Wait for USB CDC host to connect (can take several seconds after board reset)

    // not checking with stdio_usb_connected() because we want it to work when not connected to a host
    sleep_ms(2000);
    
    printf("\n");
    printf("========================================\n");
    printf("   Pressence Synth - RP2350B Platform  \n");
    printf("========================================\n");
    printf("CPU: RP2350 Cortex-M33 @ %lu MHz\n\n", clock_get_hz(clk_sys) / 1000000);
    
    // Create synth application (handles voices, MIDI processing, audio rendering)
    printf("Initializing %d-voice polyphonic synthesizer...\n", NUM_VOICES);
    synthApp = new platform::SynthApplication(
        SAMPLE_RATE, 
        2,  // channels
        NUM_VOICES, 
        std::make_unique<rp2350::EmbeddedProgramStorage>()
    );
    printf("Synth initialized\n");
    
    // Set up a key scanner that feeds into the synth via MIDI events
    printf("Initializing PIO capacitive key scanner...\n");
    std::unique_ptr<Scanner> scanner = std::make_unique<Scanner>();
    
    if (!scanner) {
        printf("ERROR: Failed to create scanner!\n");
        return 1;
    }
    printf("Scanner initialized with %d keys\n", scanner->getKeyCount());
    
    // Initialize audio sink
    printf("Initializing I2S audio output...\n");
    audioSink = new AudioSink(SAMPLE_RATE, I2S_FIRST_PIN);
    
    if (!audioSink) {
        printf("ERROR: Failed to create audio sink!\n");
        return 1;
    }
    printf("Audio sink initialized\n");
    
    // Initialize timing telemetry sink (only if telemetry is enabled)
    if constexpr (ENABLE_AUDIO_TIMING_TELEMETRY) {
        timingSink = new rp2350::Rp2350TelemetrySink<AudioTimingStats>();
        printf("Timing telemetry sink initialized\n");
    }
    
    // Initialize MIDI keyboard controller with telemetry
    printf("Initializing MIDI keyboard controller...\n");
    auto telemetrySink = std::make_unique<rp2350::Rp2350TelemetrySink<midi::KeyScanStats<NUM_KEYS>>>();
    keyboard = new MidiController(
        *scanner,
        [](uint8_t byte) { synthApp->processMidiByte(byte); },
        std::move(telemetrySink),
        36,  // Base note (C4=60, C3=48, C2=36)
        100  // Velocity
    );
    
    if (!keyboard) {
        printf("ERROR: Failed to create keyboard controller!\n");
        return 1;
    }
    
    // Wire up base note callback so control panel can change keyboard base note
    synthApp->setBaseNoteCallback([](uint8_t note) {
        if (keyboard) {
            keyboard->setBaseNote(note);
        }
    });
    
    // Enable telemetry output
    keyboard->setTelemetryEnabled(true);
    printf("Keyboard controller initialized\n");
    printf("Calibrating... (this takes a few seconds)\n\n");

    // Pre-fill audio buffer before starting
    AudioTimer initTimer;
    generateAudio(audioSink->getInactiveBuffer(), BUFFER_SIZE, initTimer);

    // Start audio output
    printf("Starting audio output...\n");
    audioSink->start();

    // Launch audio generation on core 1
    printf("Launching audio generation loop on core 1...\n");
    multicore_launch_core1(core1_audio_loop);
    
    printf("Core 0: Key scan loop started\n");
    printf("========================================\n\n");
    
    // Core 0 runs the key scan loop
    while (true) {
        // Check if core 1 has timing stats ready to emit
        if (timingStatsReady) {
            __sync_synchronize();  // Memory barrier to ensure we see the written data
            if (timingSink) {
                timingSink->sendTelemetry(sharedTimingStats);
            }
            timingStatsReady = false;
        }
        
        // Check for incoming serial commands (non-blocking)
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            synthApp->processCommandChar(static_cast<char>(ch));
        }
        
        // Mains-synchronous averaging: oversample the keys for one full AC
        // line cycle and average, so 60 Hz hum (and its harmonics) integrate
        // to zero instead of beating down into the aftertouch band. The loop
        // now runs at ~MAINS_FREQUENCY_HZ; no extra sleep is needed.
        uint32_t readingSums[NUM_KEYS] = {0};
        uint32_t sampleCount = 0;
        absolute_time_t windowEnd = make_timeout_time_us(MAINS_AVERAGE_WINDOW_US);
        do {
            scanner->startScan();
            scanner->waitForScanComplete();
            const uint16_t* r = scanner->getScanReadings();
            for (uint8_t i = 0; i < NUM_KEYS; i++) {
                readingSums[i] += r[i];
            }
            sampleCount++;
        } while (!time_reached(windowEnd));

        uint16_t averagedReadings[NUM_KEYS];
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            averagedReadings[i] = sampleCount
                ? static_cast<uint16_t>(readingSums[i] / sampleCount)
                : 0;
        }

        // Process averaged readings and generate MIDI events -> synth
        keyboard->processScan(averagedReadings);
    }
    
    return 0;
}
