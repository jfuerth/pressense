#include <sawtooth_synth.hpp>
#include <linux_audio_sink.hpp>
#include <alsa_midi_in.hpp>
#include <stream_processor.hpp>
#include <simple_voice_allocator.hpp>
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <cstring>

std::atomic<bool> running(true);

void signalHandler(int signum) {
    std::cout << "\nShutting down..." << std::endl;
    running = false;
}

extern "C" {
  int app_main(const char* midiDevice = nullptr);
  int main(int argc, char** argv);
}

int app_main(const char* midiDevice) {
    try {
        std::cout << "Pressence Synthesizer" << std::endl;
        std::cout << "=====================" << std::endl;
        
        // List available MIDI devices
        std::cout << "\nAvailable MIDI input devices:" << std::endl;
        auto devices = linux::AlsaMidiIn::listDevices();
        
        if (devices.empty()) {
            std::cout << "  (none found)" << std::endl;
        } else {
            for (size_t i = 0; i < devices.size(); ++i) {
                std::cout << "  [" << i << "] " << devices[i].name 
                          << " - " << devices[i].description << std::endl;
            }
        }
        
        // Check if device was specified
        if (midiDevice == nullptr) {
            std::cout << "\nNo MIDI device specified. Exiting." << std::endl;
            std::cout << "Usage: program <midi-device-name>" << std::endl;
            std::cout << "Example: program hw:1,0,0" << std::endl;
            return 1;
        }
        
        // Setup signal handler for graceful shutdown
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // Audio configuration
        const unsigned int SAMPLE_RATE = 44100;
        const unsigned int CHANNELS = 2;
        const unsigned int BUFFER_FRAMES = 128;  // 128 frames / 44100 frames/s = ~2.9ms latency
        const uint8_t MAX_VOICES = 8;
        
        // Create MIDI input
        std::cout << "\nOpening MIDI device: " << midiDevice << std::endl;
        linux::AlsaMidiIn midiIn(midiDevice);
        std::cout << "MIDI input ready: " << midiIn.getDeviceName() << std::endl;
        
        // Create audio sink
        std::cout << "\nInitializing audio output..." << std::endl;
        linux::AlsaPcmOut audioSink("default", SAMPLE_RATE, CHANNELS, BUFFER_FRAMES);
        std::cout << "Audio: " << audioSink.getSampleRate() << " Hz, " 
                  << audioSink.getChannels() << " channels, "
                  << audioSink.getBufferFrames() << " frames/buffer" << std::endl;
        
        // Create voice allocator with wavetable synth factory
        auto voiceFactory = [SAMPLE_RATE]() -> std::unique_ptr<midi::Synth> {
            return std::make_unique<synth::WavetableSynth>(static_cast<float>(SAMPLE_RATE));
        };

        synth::BiquadFilter::Mode filterMode = synth::BiquadFilter::Mode::LOWPASS;
        
        auto voiceAllocator = std::make_unique<midi::SimpleVoiceAllocator>(MAX_VOICES, voiceFactory);
        
        // Define CC mapping (glue code - maps MIDI controls to synth parameters)
        auto ccMapper = [&filterMode](uint8_t channel, uint8_t cc, uint8_t value, midi::SynthVoiceAllocator& allocator) {
            float normalized = static_cast<float>(value) / 127.0f;
            
            switch(cc) {
                case 1:  // Modulation wheel -> timbre
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        voice.setTimbre(normalized);
                    });
                    break;
                case 20: {// C1 -> Filter cutoff
                        // Use exponential mapping for more musical response
                        // Range: 100Hz to 10kHz
                        const float MIN_CUTOFF = 100.0f;
                        const float MAX_CUTOFF = 10000.0f;
                        float cutoff = MIN_CUTOFF * std::pow(MAX_CUTOFF / MIN_CUTOFF, normalized);
                        allocator.forEachVoice([cutoff](midi::Synth& voice) {
                            static_cast<synth::WavetableSynth&>(voice).getFilter().setCutoff(cutoff);
                        });
                    }
                    break;
                case 21: // C2 -> Filter resonance
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        static_cast<synth::WavetableSynth&>(voice).getFilter().setQ(normalized * 20.0f); // Q range 0.1 to 20.0
                    });
                    break;
                case 96: // C18 button
                    allocator.forEachVoice([normalized, &filterMode](midi::Synth& voice) {
                        // cycle through filter modes on each press
                        if (normalized > 0.5f) {
                            filterMode = synth::BiquadFilter::nextMode(filterMode);
                            static_cast<synth::WavetableSynth&>(voice).getFilter().setMode(filterMode);
                        }
                    });
                    break;
                
                default:
                    std::cout << "Unhandled CC #" << static_cast<int>(cc) 
                          << " value " << static_cast<int>(value) << std::endl;
                    break;
            }
        };
        
        // Poly aftertouch mapping (continuous per-note pressure -> per-voice control)
        auto polyAftertouchMapper = [](uint8_t channel, uint8_t note, uint8_t pressure, midi::Synth& voice) {
            // TODO: Map pressure to per-voice parameter
            // The way pressure maps to synth parameters will be highly customizable at run time based
            // on knobs on the keyboard. For example, we might affect the following:
            // - Filter cutoff
            // - Amplitude (volume)
            // - LFO depth/rate
            //   - filter mix/depth
            //   - vibrato/tremolo speed (also for filters... Assign to LFO?)
            //   - vibrato/tremolo depth
            // - fine tune
            // - second oscillator detune 
        };
        
        // Create MIDI stream processor with callbacks
        midi::StreamProcessor midiProcessor(std::move(voiceAllocator), 0, ccMapper, polyAftertouchMapper);
        std::cout << "MIDI processor ready with " << static_cast<int>(MAX_VOICES) << " voices" << std::endl;
        
        // Main audio loop
        std::cout << "\nStarting audio/MIDI processing (Ctrl+C to stop)..." << std::endl;
        std::cout << "Play notes on your MIDI device!" << std::endl;
        
        while (running) {
            // Fill and write audio buffer
            audioSink.write([&](float* buffer, unsigned int numFrames) {
                // First, drain MIDI input and process all pending messages
                midiIn.pollAndRead([&](uint8_t byte) {
                    midiProcessor.process(byte);
                });
                
                // Get all voices and mix them
                std::vector<synth::WavetableSynth*> activeSynths;
                midiProcessor.forEachVoice([&](midi::Synth& synth) {
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
                }
            });
        }
        
        std::cout << "\nPlayback stopped." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int main(int argc, char** argv) {
  const char* midiDevice = (argc > 1) ? argv[1] : nullptr;
  return app_main(midiDevice);
}