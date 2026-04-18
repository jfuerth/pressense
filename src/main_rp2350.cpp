/**
 * Pressence Synth - RP2350B Platform Entry Point
 * 
 * Phase 3: Full synth pipeline - capacitive keys -> MIDI -> synth -> I2S audio
 */

#include <stdio.h>
#include <cstdint>
#include <cmath>
#include <memory>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

// Platform-specific implementations
#include <pio_capacitive_scanner.hpp>
#include <rp2350_audio_sink.hpp>
#include <rp2350_telemetry_sink.hpp>

// Synth modules
#include <sawtooth_synth.hpp>
#include <simple_voice_allocator.hpp>
#include <stream_processor.hpp>

// MIDI keyboard controller
#include <midi_keyboard_controller.hpp>

// Audio configuration
static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr size_t BUFFER_SIZE = 256;

// I2S pin configuration: three consecutive pins - data, bclk, lrclk
static constexpr uint8_t I2S_FIRST_PIN = 32;

// Key scanner configuration
static constexpr uint8_t FIRST_KEY_PIN = 0;
static constexpr uint8_t NUM_KEYS = 32;
static constexpr uint8_t NUM_VOICES = 8;

// Type aliases
using Scanner = rp2350::PioCapacitiveScanner<FIRST_KEY_PIN, NUM_KEYS>;
using AudioSink = rp2350::Rp2350AudioSink<BUFFER_SIZE>;
using MidiController = midi::MidiKeyboardController<NUM_KEYS>;

// Global instances
static Scanner* scanner = nullptr;
static AudioSink* audioSink = nullptr;
static MidiController* keyboard = nullptr;
static midi::StreamProcessor* midiProcessor = nullptr;

/**
 * @brief MIDI callback - feeds bytes from keyboard controller to stream processor
 */
void midiCallback(uint8_t midiByte) {
    if (midiProcessor) {
        midiProcessor->process(midiByte);
    }
}

/**
 * @brief Generate audio samples from all synth voices into the buffer
 */
void generateAudio(int32_t* buffer, size_t length) {
    for (size_t i = 0; i < length; i++) {
        float mixedSample = 0.0f;
        
        // Sum all voice outputs
        midiProcessor->forEachVoice([&mixedSample](midi::Synth& voice) {
            mixedSample += static_cast<synth::WavetableSynth&>(voice).nextSample();
        });
        
        // Scale down by number of voices to prevent clipping, then apply master volume
        mixedSample = mixedSample / static_cast<float>(NUM_VOICES) * 0.8f;
        
        // Clamp to [-1, 1]
        if (mixedSample > 1.0f) mixedSample = 1.0f;
        if (mixedSample < -1.0f) mixedSample = -1.0f;
        
        // Convert to 31-bit integer (PIO outputs 31 bits per channel)
        buffer[i] = static_cast<int32_t>(mixedSample * 1073741823.0f);
    }
}

/**
 * @brief Audio generation loop (runs on core 1)
 *
 * Fills the inactive buffer while the active buffer is being transmitted via
 * DMA, then swaps. This keeps the PIO TX FIFO continuously fed.
 */
void core1_audio_loop() {
    printf("Core 1: audio loop started\n");

    while (true) {
        // Wait for the current DMA transfer to finish before touching the
        // inactive buffer — the previous swap may have just started it.
        while (!audioSink->isTransferComplete()) {
            tight_loop_contents();
        }

        // Fill the now-idle buffer and promote it to active.
        generateAudio(audioSink->getInactiveBuffer(), BUFFER_SIZE);
        audioSink->swapBuffers();
    }
}

int main() {
    // Initialize USB stdio
    stdio_init_all();
    
    // Wait for USB CDC host to connect (can take several seconds after board reset)
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    // Brief pause to let the terminal settle after connection
    sleep_ms(100);
    
    printf("\n");
    printf("========================================\n");
    printf("   Pressence Synth - RP2350B Platform  \n");
    printf("========================================\n");
    printf("Board: Waveshare Core 2350B\n");
    printf("CPU: RP2350 Cortex-M33 @ %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
    printf("Phase: Key Scanner + Synth + Audio\n");
    printf("========================================\n\n");
    
    // Initialize key scanner
    printf("Initializing PIO capacitive key scanner...\n");
    scanner = new Scanner();
    
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
    
    // Initialize synth voice allocator with wavetable synths
    printf("Initializing %d-voice polyphonic synthesizer...\n", NUM_VOICES);
    auto voiceAllocator = std::make_unique<midi::SimpleVoiceAllocator>(
        NUM_VOICES,
        []() -> std::unique_ptr<midi::Synth> {
            return std::make_unique<synth::WavetableSynth>(static_cast<float>(SAMPLE_RATE));
        }
    );
    
    // Create MIDI stream processor (owns the voice allocator)
    midiProcessor = new midi::StreamProcessor(std::move(voiceAllocator));
    
    if (!midiProcessor) {
        printf("ERROR: Failed to create MIDI processor!\n");
        return 1;
    }
    printf("Synth initialized\n");
    
    // Initialize MIDI keyboard controller with telemetry
    printf("Initializing MIDI keyboard controller...\n");
    auto telemetrySink = std::make_unique<rp2350::Rp2350TelemetrySink<midi::KeyScanStats<NUM_KEYS>>>();
    keyboard = new MidiController(
        *scanner,
        midiCallback,
        std::move(telemetrySink),
        60,  // Base note C4
        100  // Velocity
    );
    
    if (!keyboard) {
        printf("ERROR: Failed to create keyboard controller!\n");
        return 1;
    }
    
    // Enable telemetry output
    keyboard->setTelemetryEnabled(true);
    printf("Keyboard controller initialized\n");
    printf("Calibrating... (this takes a few seconds)\n\n");

    // Pre-fill audio buffer before starting
    generateAudio(audioSink->getInactiveBuffer(), BUFFER_SIZE);

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
        // Trigger a scan
        scanner->startScan();
        
        // Wait for scan to complete
        scanner->waitForScanComplete();
        
        // Process scan results and generate MIDI events -> synth
        keyboard->processScan();
        
        // Scan at ~100 Hz
        sleep_ms(10);
    }
    
    return 0;
}
