#include <esp32_audio_sink.hpp>
#include <esp32_midi_in.hpp>
#include <esp32_arpeggio_task.hpp>
#include <synth_application.hpp>
#include <log.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

// Global synth application instance
static std::unique_ptr<platform::SynthApplication> gSynth;
static std::unique_ptr<esp32::ArpeggioTask> gArpeggio;

extern "C" void app_main(void) {
    logInfo("Pressence Synthesizer - ESP32");
    logInfo("==============================");
    
    // Audio configuration
    const unsigned int SAMPLE_RATE = 44100;
    const unsigned int CHANNELS = 2;
    const unsigned int BUFFER_FRAMES = 128;
    const uint8_t MAX_VOICES = 8;
    
    // Create synthesizer application
    logInfo("Initializing synthesizer...");
    gSynth = std::make_unique<platform::SynthApplication>(SAMPLE_RATE, CHANNELS, MAX_VOICES);
    
    // Create MIDI input (UART2, RX on GPIO 16)
    logInfo("Initializing UART MIDI input...");
    esp32::UartMidiIn midiIn(UART_NUM_2, 16);
    logInfo("MIDI input ready: %s", midiIn.getDeviceName());
    
    // Create I2S audio output (PCM5102 DAC)
    logInfo("Initializing I2S audio output...");
    esp32::I2sAudioSink audioSink(SAMPLE_RATE, CHANNELS, BUFFER_FRAMES);
    logInfo("Audio: %d Hz, %d channels, %d frames/buffer",
           audioSink.getSampleRate(), 
           audioSink.getChannels(), 
           audioSink.getBufferFrames());
    
    // Start arpeggio task for testing
    logInfo("Starting arpeggio task...");
    gArpeggio = std::make_unique<esp32::ArpeggioTask>(
        [](uint8_t byte) {
            if (gSynth) {
                gSynth->processMidiByte(byte);
            }
        },
        300  // 300ms per note
    );
    
    logInfo("\nAudio/MIDI processing started!");
    logInfo("You should hear a C major arpeggio: C4-E4-G4-C5");
    logInfo("Connect MIDI input to GPIO 16 (UART2 RX) for external control");
    
    // Main audio loop
    while (true) {
        // Fill and write audio buffer
        audioSink.write([&](float* buffer, unsigned int numFrames) {
            // Check for MIDI input from UART
            midiIn.pollAndRead([&](uint8_t byte) {
                gSynth->processMidiByte(byte);
            });
            
            // Render audio
            gSynth->renderAudio(buffer, numFrames);
        });
    }
}
