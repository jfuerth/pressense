#include <esp32_audio_sink.hpp>
#include <esp32_capacitive_scanner.hpp>
#include <esp32_telemetry_sink.hpp>
#include <midi_keyboard_controller.hpp>
#include <synth_application.hpp>
#include <performance_timer.hpp>
#include <log.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <memory>

// Platform-specific implementations
#include <embedded_program_storage.hpp>

// Keyboard configuration: GPIO pins for 14 keys (avoiding I2S pins 22, 25, 26 and boot/flash pins)
static constexpr gpio_num_t KEY_GPIOS[] = {
    GPIO_NUM_4,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_14,
    GPIO_NUM_15,
    GPIO_NUM_16,  // Labelled RX2 on DEVKIT V1
    GPIO_NUM_17,  // Labelled TX2 on DEVKIT V1
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_21,
    GPIO_NUM_23,
    GPIO_NUM_27,
    GPIO_NUM_32,
    GPIO_NUM_33
};
static constexpr size_t NUM_KEYS = sizeof(KEY_GPIOS) / sizeof(KEY_GPIOS[0]);

// Scanner is templated so compiler can optimize code for actual key count
using ScannerType = esp32::ESP32CapacitiveScanner<KEY_GPIOS, NUM_KEYS>;

// MIDI controller type with compile-time configuration
using MidiControllerType = midi::MidiKeyboardController<NUM_KEYS>;

// Global instances
static std::unique_ptr<platform::SynthApplication> synthApp;
static std::unique_ptr<ScannerType> scanner;
static std::unique_ptr<MidiControllerType> keyboard;
static std::unique_ptr<esp32::I2sAudioSink> audioSink;

/**
 * @brief Audio rendering task - pinned to core 1 for dedicated audio processing
 */
void audioTask(void* parameter) {
    logInfo("Audio task started on core %d", xPortGetCoreID());
    
    // Timer for performance measurement (NoOp for now - enable with Esp32TimingPolicy)
    features::LapTimer<features::NoOpTimingPolicy, 4> timer;
    
    // Main audio loop
    while (true) {
        // Process keyboard scan (called from audio loop, scanner runs in separate task)
        keyboard->processScan();

        // Fill and write audio buffer
        audioSink->write([&](float* buffer, unsigned int numFrames) {
            // Render audio
            synthApp->renderAudio(buffer, numFrames, timer);
            timer.end();
        });
    }
}

extern "C" void app_main(void) {
    logInfo("Pressence Synthesizer - ESP32");
    logInfo("==============================");
    
    // Audio configuration
    const unsigned int REQUESTED_SAMPLE_RATE = 44100;
    const unsigned int CHANNELS = 2;
    const unsigned int BUFFER_FRAMES = 128;
    const uint8_t MAX_VOICES = 8;
    
    // Create I2S audio output first to determine actual sample rate
    logInfo("Initializing I2S audio output...");
    audioSink = std::make_unique<esp32::I2sAudioSink>(REQUESTED_SAMPLE_RATE, CHANNELS, BUFFER_FRAMES);
    
    // Get the actual achieved sample rate from the I2S hardware
    unsigned int actualSampleRate = audioSink->getSampleRate();
    logInfo("Audio: %d Hz, %d channels, %d frames/buffer",
           actualSampleRate, 
           audioSink->getChannels(), 
           audioSink->getBufferFrames());
    
    logInfo("Initializing synthesizer...");
    synthApp = std::make_unique<platform::SynthApplication>(
        actualSampleRate,
        CHANNELS,
        MAX_VOICES,
        std::make_unique<esp32::EmbeddedProgramStorage>());
        
    // Start capacitive touch keyboard
    logInfo("Starting capacitive keyboard scanner...");
    scanner = std::make_unique<ScannerType>();
    
    logInfo("Initializing MIDI keyboard controller...");
    keyboard = std::make_unique<MidiControllerType>(
        *scanner,
        [](uint8_t byte) { synthApp->processMidiByte(byte); },
        std::make_unique<esp32::Esp32TelemetrySink<midi::KeyScanStats<NUM_KEYS>>>("keyscan_telem", 0),
        60-24,  // Base note: C4
        20   // Fixed velocity
    );
    
    // Enable telemetry output
    keyboard->setTelemetryEnabled(true);
    
    logInfo("\nCreating audio task on core 1...");
    
    // Create audio task pinned to core 1 with high priority
    TaskHandle_t audioTaskHandle = nullptr;
    xTaskCreatePinnedToCore(
        audioTask,
        "audio",
        8192,  // Larger stack for audio processing
        nullptr,
        2,     // Priority 2 (higher than scanner/telemetry)
        &audioTaskHandle,
        1      // Core 1 (APP_CPU) - dedicated to audio
    );
    
    if (audioTaskHandle == nullptr) {
        logError("Failed to create audio task!");
        return;
    }
    
    logInfo("Audio/MIDI processing started!");
    logInfo("App_main running on core %d", xPortGetCoreID());
    
    // All work is now in other tasks. Just idle.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Sleep, let other tasks run
    }
}
