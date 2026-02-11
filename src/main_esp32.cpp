#include <esp32_audio_sink.hpp>
#include <esp32_arpeggio_task.hpp>
#include <synth_application.hpp>
#include <log.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <memory>

// Global synth application instance
static std::unique_ptr<platform::SynthApplication> gSynth;
static std::unique_ptr<esp32::ArpeggioTask> gArpeggio;

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
    esp32::I2sAudioSink audioSink(REQUESTED_SAMPLE_RATE, CHANNELS, BUFFER_FRAMES);
    
    // Get the actual achieved sample rate from the I2S hardware
    unsigned int actualSampleRate = audioSink.getSampleRate();
    logInfo("Audio: %d Hz, %d channels, %d frames/buffer",
           actualSampleRate, 
           audioSink.getChannels(), 
           audioSink.getBufferFrames());
    
    // Create synthesizer application with the ACTUAL sample rate
    logInfo("Initializing synthesizer...");
    gSynth = std::make_unique<platform::SynthApplication>(actualSampleRate, CHANNELS, MAX_VOICES);
        
    // Start arpeggio task for testing
    logInfo("Starting arpeggio task...");
    gArpeggio = std::make_unique<esp32::ArpeggioTask>(
        [](uint8_t byte) {
            if (gSynth) {
                gSynth->processMidiByte(byte);
            }
        },
        200  // milliseconds per note
    );
    
    logInfo("\nAudio/MIDI processing started!");
    
    // Timing statistics
    uint32_t frameCount = 0;
    uint32_t maxRenderTime = 0;
    uint32_t totalRenderTime = 0;
    
    // Main audio loop
    while (true) {
        uint32_t startTime = esp_timer_get_time();  // microseconds
        
        // Fill and write audio buffer
        audioSink.write([&](float* buffer, unsigned int numFrames) {
            // Render audio
            gSynth->renderAudio(buffer, numFrames);
        });
        
        uint32_t renderTime = esp_timer_get_time() - startTime;
        totalRenderTime += renderTime;
        if (renderTime > maxRenderTime) {
            maxRenderTime = renderTime;
        }
        
        frameCount++;
        
        // Stats logging
        if (frameCount % 1000 == 0) {
            uint32_t avgRenderTime = totalRenderTime / 1000;
            uint32_t bufferDuration = (BUFFER_FRAMES * 1000000) / actualSampleRate;  // microseconds
            
            logInfo("Audio stats: avg=%lu us, max=%lu us, budget=%lu us, underruns=%lu, partial=%lu",
                    avgRenderTime, maxRenderTime, bufferDuration,
                    audioSink.getUnderrunCount(), audioSink.getPartialWriteCount());
            
            maxRenderTime = 0;
            totalRenderTime = 0;
        }
    }
}
