#include <sawtooth_synth.hpp>
#include <linux_audio_sink.hpp>
#include <stream_processor.hpp>
#include <simple_voice_allocator.hpp>
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>

std::atomic<bool> running(true);

void signalHandler(int signum) {
    std::cout << "\nShutting down..." << std::endl;
    running = false;
}

extern "C" {
  int app_main(void);
  int main(void);
}

int app_main(void) {
    try {
        std::cout << "Pressence Synthesizer" << std::endl;
        std::cout << "=====================" << std::endl;
        
        // Setup signal handler for graceful shutdown
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // Audio configuration
        const unsigned int SAMPLE_RATE = 44100;
        const unsigned int CHANNELS = 2;
        const unsigned int BUFFER_FRAMES = 512;
        const uint8_t MAX_VOICES = 8;
        
        // Create audio sink
        std::cout << "Initializing audio output..." << std::endl;
        linux::AlsaPcmOut audioSink("default", SAMPLE_RATE, CHANNELS, BUFFER_FRAMES);
        std::cout << "Audio: " << audioSink.getSampleRate() << " Hz, " 
                  << audioSink.getChannels() << " channels, "
                  << audioSink.getBufferFrames() << " frames/buffer" << std::endl;
        
        // Create voice allocator with sawtooth synth factory
        auto voiceFactory = [SAMPLE_RATE]() -> std::unique_ptr<midi::Synth> {
            return std::make_unique<synth::WavetableSynth>(static_cast<float>(SAMPLE_RATE));
        };
        
        auto voiceAllocator = std::make_unique<midi::SimpleVoiceAllocator>(MAX_VOICES, voiceFactory);
        
        // Keep a reference to the allocator before moving it
        midi::SimpleVoiceAllocator* allocatorPtr = voiceAllocator.get();
        
        // Create MIDI stream processor (listening on channel 0)
        midi::StreamProcessor midiProcessor(std::move(voiceAllocator), 0);
        std::cout << "MIDI processor ready with " << static_cast<int>(MAX_VOICES) << " voices" << std::endl;
        
        // Test: Play a chord (C major: C4, E4, G4)
        std::cout << "\nPlaying test chord (C major)..." << std::endl;
        const uint8_t channel = 0;
        const uint8_t velocity = 100;
        
        // Note On messages for C major chord
        uint8_t midiData[] = {
            0x90 | channel, 60, velocity,  // C4
            0x90 | channel, 64, velocity,  // E4
            0x90 | channel, 67, velocity,  // G4
        };
        
        for (uint8_t byte : midiData) {
            midiProcessor.process(byte);
        }
        
        // Main audio loop
        std::cout << "Starting audio loop (Ctrl+C to stop)..." << std::endl;
        
        unsigned long frameCount = 0;
        const unsigned long CHORD_DURATION = SAMPLE_RATE * 2; // 2 seconds
        
        while (running) {
            // Fill and write audio buffer
            audioSink.write([&](float* buffer, unsigned int numFrames) {
                // Get all voices and mix them
                std::vector<synth::WavetableSynth*> activeSynths;
                allocatorPtr->forEachVoice([&](midi::Synth& synth) {
                    activeSynths.push_back(static_cast<synth::WavetableSynth*>(&synth));
                });
                
                for (unsigned int frame = 0; frame < numFrames; ++frame) {
                    // Mix all active voices
                    float sample = 0.0f;
                    for (auto* synth : activeSynths) {
                        sample += synth->nextSample();
                    }
                    
                    // Apply simple limiting to prevent clipping
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    
                    // Scale down to avoid clipping when multiple voices play
                    sample *= 0.3f;
                    
                    // Write to stereo channels
                    buffer[frame * CHANNELS + 0] = sample; // Left
                    buffer[frame * CHANNELS + 1] = sample; // Right
                    
                    // Release chord after duration
                    if (frameCount == CHORD_DURATION) {
                        std::cout << "Releasing chord..." << std::endl;
                        uint8_t noteOffData[] = {
                            0x80 | channel, 60, 0,  // C4 off
                            0x80 | channel, 64, 0,  // E4 off
                            0x80 | channel, 67, 0,  // G4 off
                        };
                        for (uint8_t byte : noteOffData) {
                            midiProcessor.process(byte);
                        }
                    }
                    
                    frameCount++;
                }
            });
        }
        
        std::cout << "Playback stopped." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int main(void) {
  return app_main();
}