#include <esp32_audio_sink.hpp>
#include <esp32_capacitive_scanner.hpp>
#include <esp32_telemetry_sink.hpp>
#include <midi_keyboard_controller.hpp>
#include <synth_application.hpp>
#include <audio_stats.hpp>
#include <log.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <memory>

#ifdef FEATURE_PERFORMANCE_TIMING
#include <timing.hpp>
#endif

// Platform-specific implementations
#include <embedded_program_storage.hpp>

// Global instances
static std::unique_ptr<platform::SynthApplication> synthApp;
static std::unique_ptr<esp32::ESP32CapacitiveScanner> scanner;
static std::unique_ptr<midi::MidiKeyboardController> keyboard;
static std::unique_ptr<esp32::I2sAudioSink> audioSink;
static std::unique_ptr<esp32::Esp32TelemetrySink<platform::AudioStats>> audioTelemetry;

/**
 * @brief Audio rendering task - pinned to core 1 for dedicated audio processing
 */
void audioTask(void* parameter) {
    unsigned int actualSampleRate = audioSink->getSampleRate();
    const unsigned int BUFFER_FRAMES = audioSink->getBufferFrames();
    
    // Timing statistics
    uint32_t frameCount = 0;
    uint32_t maxRenderTime = 0;
    uint32_t totalScanTime = 0;
    uint32_t totalRenderTime = 0;
    
    logInfo("Audio task started on core %d", xPortGetCoreID());
    
    // Main audio loop
    while (true) {
        uint32_t startTime = esp_timer_get_time();  // microseconds
        
        // Process keyboard scan (called from audio loop, scanner runs in separate task)
        keyboard->processScan();
        uint32_t now = esp_timer_get_time();
        uint32_t keyProcessingTime = now - startTime;
        startTime = now;

        // Fill and write audio buffer
        audioSink->write([&](float* buffer, unsigned int numFrames) {
            // Render audio
            synthApp->renderAudio(buffer, numFrames);
        });
        now = esp_timer_get_time();
        uint32_t renderTime = now - startTime;

        if (renderTime > maxRenderTime) {
            maxRenderTime = renderTime;
        }

        totalScanTime += keyProcessingTime;
        totalRenderTime += renderTime;

        frameCount++;
        
        // Send telemetry every 1000 frames
        if (frameCount % 1000 == 0) {
            uint32_t totalLoopTime = totalScanTime + totalRenderTime;
            uint32_t bufferDuration = (BUFFER_FRAMES * 1000000) / actualSampleRate;
            
            platform::AudioStats stats;
            stats.frameCount = frameCount;
            stats.avgLoopTime = totalLoopTime / 1000;
            stats.maxLoopTime = maxRenderTime;
            stats.bufferDuration = bufferDuration;
            stats.avgScanTime = totalScanTime / 1000;
            stats.avgRenderTime = totalRenderTime / 1000;
            stats.underrunCount = audioSink->getUnderrunCount();
            stats.partialWriteCount = audioSink->getPartialWriteCount();
            stats.coreId = xPortGetCoreID();
            
#ifdef FEATURE_PERFORMANCE_TIMING
            // Collect detailed timing breakdown
            platform::TimingStats voiceMixing, outputProcessing, stereoDup;
            synthApp->getAndResetTimingStats(voiceMixing, outputProcessing, stereoDup);
            
            platform::TimingStats floatToInt, i2sWrite;
            audioSink->getAndResetTimingStats(floatToInt, i2sWrite);
            
            platform::VoiceTimingStats voiceComponents = synthApp->getAndResetVoiceTimingStats();
            
            // Populate timing breakdown (convert cycles to microseconds)
            constexpr uint32_t cyclesPerUs = platform::PlatformTimer::cyclesPerMicrosecond;
            
            stats.timing.avgVoiceMixing = voiceMixing.getAverage() / cyclesPerUs;
            stats.timing.minVoiceMixing = voiceMixing.minTime / cyclesPerUs;
            stats.timing.maxVoiceMixing = voiceMixing.maxTime / cyclesPerUs;
            
            stats.timing.avgOutputProcessing = outputProcessing.getAverage() / cyclesPerUs;
            stats.timing.minOutputProcessing = outputProcessing.minTime / cyclesPerUs;
            stats.timing.maxOutputProcessing = outputProcessing.maxTime / cyclesPerUs;
            
            stats.timing.avgStereoDup = stereoDup.getAverage() / cyclesPerUs;
            stats.timing.minStereoDup = stereoDup.minTime / cyclesPerUs;
            stats.timing.maxStereoDup = stereoDup.maxTime / cyclesPerUs;
            
            stats.timing.avgFloatToInt = floatToInt.getAverage() / cyclesPerUs;
            stats.timing.minFloatToInt = floatToInt.minTime / cyclesPerUs;
            stats.timing.maxFloatToInt = floatToInt.maxTime / cyclesPerUs;
            
            stats.timing.avgI2sWrite = i2sWrite.getAverage() / cyclesPerUs;
            stats.timing.minI2sWrite = i2sWrite.minTime / cyclesPerUs;
            stats.timing.maxI2sWrite = i2sWrite.maxTime / cyclesPerUs;
            
            // Convert voice component timing from cycles to microseconds
            stats.timing.voiceComponents = voiceComponents.convertToMicroseconds(cyclesPerUs);
#endif
            
            audioTelemetry->sendTelemetry(stats);
            
            maxRenderTime = 0;
            totalScanTime = 0;
            totalRenderTime = 0;
        }
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
    
    // Create synthesizer application with the ACTUAL sample rate
    logInfo("Initializing synthesizer...");
    auto programStorage = std::make_unique<esp32::EmbeddedProgramStorage>();
    synthApp = std::make_unique<platform::SynthApplication>(actualSampleRate, CHANNELS, MAX_VOICES, std::move(programStorage));
        
    // Start capacitive touch keyboard
    logInfo("Starting capacitive keyboard scanner...");
    scanner = std::make_unique<esp32::ESP32CapacitiveScanner>();
    
    logInfo("Initializing MIDI keyboard controller...");
    auto keyScanTelemetry = std::make_unique<esp32::Esp32TelemetrySink<midi::KeyScanStats>>("keyscan_telem", 0);
    keyboard = std::make_unique<midi::MidiKeyboardController>(
        *scanner,
        [](uint8_t byte) {
            if (synthApp) {
                synthApp->processMidiByte(byte);
            }
        },
        std::move(keyScanTelemetry),
        60-24,  // Base note: C4
        20   // Fixed velocity
    );
    
    // Enable telemetry output
    keyboard->setTelemetryEnabled(true);
    
    // Create audio telemetry sink
    logInfo("Initializing audio telemetry...");
    audioTelemetry = std::make_unique<esp32::Esp32TelemetrySink<platform::AudioStats>>("audio_telem", 0);
    
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
