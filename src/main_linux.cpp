#include <linux_audio_sink.hpp>
#include <alsa_midi_in.hpp>
#include <synth_application.hpp>
#include <log.hpp>
#include <csignal>
#include <atomic>

// Platform-specific implementations
#include <filesystem_program_storage.hpp>

#ifdef FEATURE_CLIPBOARD
#include <preset_clipboard.hpp>
#endif

std::atomic<bool> running(true);

void signalHandler(int signum) {
    logInfo("\nShutting down...");
    running = false;
}

extern "C" {
  int app_main(const char* midiDevice = nullptr);
  int main(int argc, char** argv);
}

int app_main(const char* midiDevice) {
    try {
        logInfo("Pressence Synthesizer - Linux");
        logInfo("=============================");
        
        // List available MIDI devices
        logInfo("\nAvailable MIDI input devices:");
        auto devices = linux::AlsaMidiIn::listDevices();
        
        if (devices.empty()) {
            logInfo("  (none found)");
        } else {
            for (size_t i = 0; i < devices.size(); ++i) {
                logInfo("  [%zu] %s - %s", i, 
                       devices[i].name.c_str(), 
                       devices[i].description.c_str());
            }
        }
        
        // Check if device was specified
        if (midiDevice == nullptr) {
            logInfo("\nNo MIDI device specified. Exiting.");
            logInfo("Usage: program <midi-device-name>");
            logInfo("Example: program hw:1,0,0");
            return 1;
        }
        
        // Setup signal handler for graceful shutdown
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // Audio configuration
        const unsigned int SAMPLE_RATE = 44100;
        const unsigned int CHANNELS = 2;
        const unsigned int BUFFER_FRAMES = 128;
        const uint8_t MAX_VOICES = 8;
        
        // Create MIDI input
        logInfo("\nOpening MIDI device: %s", midiDevice);
        linux::AlsaMidiIn midiIn(midiDevice);
        logInfo("MIDI input ready: %s", midiIn.getDeviceName().c_str());
        
        // Create audio sink
        logInfo("\nInitializing audio output...");
        linux::AlsaPcmOut audioSink("default", SAMPLE_RATE, CHANNELS, BUFFER_FRAMES);
        logInfo("Audio: %d Hz, %d channels, %d frames/buffer",
               audioSink.getSampleRate(), 
               audioSink.getChannels(), 
               audioSink.getBufferFrames());
        
        // Create synthesizer application with platform implementations
        auto programStorage = std::make_unique<linux::FilesystemProgramStorage>();
        platform::SynthApplication synth(SAMPLE_RATE, CHANNELS, MAX_VOICES, std::move(programStorage));

#ifdef FEATURE_CLIPBOARD
        synth.setClipboard(std::make_unique<linux::PresetClipboard>());
#endif
        
        // Main audio loop
        logInfo("\nStarting audio/MIDI processing (Ctrl+C to stop)...");
        logInfo("Play notes on your MIDI device!");
        
        while (running) {
            // Fill and write audio buffer
            audioSink.write([&](float* buffer, unsigned int numFrames) {
                // First, drain MIDI input and process all pending messages
                midiIn.pollAndRead([&](uint8_t byte) {
                    synth.processMidiByte(byte);
                });
                
                // Render audio
                synth.renderAudio(buffer, numFrames);
            });
        }
        
        logInfo("\nPlayback stopped.");
        return 0;
        
    } catch (const std::exception& e) {
        logError("Error: %s", e.what());
        return 1;
    }
}

int main(int argc, char** argv) {
  const char* midiDevice = (argc > 1) ? argv[1] : nullptr;
  return app_main(midiDevice);
}
