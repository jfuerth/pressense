#include <sawtooth_synth.hpp>
#include <linux_audio_sink.hpp>
#include <alsa_midi_in.hpp>
#include <stream_processor.hpp>
#include <simple_voice_allocator.hpp>
#include <program_data.hpp>
#include <output_processor.hpp>
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
        
        // Create master output processor with switchable modes
        synth::MasterOutputProcessor outputProcessor(0.5f);  // Start with normalized drive 0.5
        
        // Create voice allocator with wavetable synth factory
        auto voiceFactory = [SAMPLE_RATE]() -> std::unique_ptr<midi::Synth> {
            return std::make_unique<synth::WavetableSynth>(static_cast<float>(SAMPLE_RATE));
        };

        synth::BiquadFilter::Mode filterMode = synth::BiquadFilter::Mode::LOWPASS;
        
        auto voiceAllocator = std::make_unique<midi::SimpleVoiceAllocator>(MAX_VOICES, voiceFactory);
        
        // Program management
        midi::ProgramData clipboard;  // Clipboard for copy/paste
        uint8_t currentProgram = 1;   // Start on program 1
        
        // Load program 1 or use defaults
        midi::ProgramData currentProgramData;
        if (currentProgramData.loadFromFile(currentProgram)) {
            currentProgramData.applyToVoices(*voiceAllocator);
        } else {
            std::cout << "Program " << static_cast<int>(currentProgram) 
                      << " not found, using defaults" << std::endl;
        }
        
        // Print header for CC parameter table
        std::cout << std::endl;
        std::cout << "CC# | Val | WavShp | Cutoff(Hz) | Q     | Mode | FEnvAmt | FEnvA(s) | FEnvD(s) | FEnvS | FEnvR(s)" << std::endl;
        std::cout << "----+-----+--------+------------+-------+------+---------+----------+----------+-------+---------" << std::endl;
        
        // Define CC mapping (glue code - maps MIDI controls to synth parameters)
        auto ccMapper = [&](uint8_t channel, uint8_t cc, uint8_t value, midi::SynthVoiceAllocator& allocator) {
            float normalized = static_cast<float>(value) / 127.0f;
            
            switch(cc) {
                case 1:  // Modulation wheel -> waveform shape
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        static_cast<synth::WavetableSynth&>(voice).getOscillator().updateWavetable(normalized);
                    });
                    break;
                case 20: {// C1 -> Filter cutoff
                        // Use exponential mapping for more musical response
                        // Range: 100Hz to 10kHz
                        const float MIN_CUTOFF = 100.0f;
                        const float MAX_CUTOFF = 10000.0f;
                        float cutoff = MIN_CUTOFF * std::pow(MAX_CUTOFF / MIN_CUTOFF, normalized);
                        allocator.forEachVoice([cutoff](midi::Synth& voice) {
                            static_cast<synth::WavetableSynth&>(voice).setBaseCutoff(cutoff);
                        });
                    }
                    break;
                case 21: // C2 -> Filter resonance
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        static_cast<synth::WavetableSynth&>(voice).getFilter().setQ(normalized * 20.0f); // Q range 0.1 to 20.0
                    });
                    break;
                case 71: // C3 -> Filter envelope attack time
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        float attackTime = 0.001f + normalized * 2.0f; // 1ms to 2s
                        static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setAttackTime(attackTime);
                    });
                    break;
                case 72: // C4 -> Filter envelope decay time
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        float decayTime = 0.01f + normalized * 5.0f; // 10ms to 5s
                        static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setDecayTime(decayTime);
                    });
                    break;
                case 25: // C5 -> Filter envelope sustain level
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setSustainLevel(normalized);
                    });
                    break;
                case 73: // C6 -> Filter envelope release time
                    allocator.forEachVoice([normalized](midi::Synth& voice) {
                        float releaseTime = 0.01f + normalized * 5.0f; // 10ms to 5s
                        static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setReleaseTime(releaseTime);
                    });
                    break;
                case 74: // C7 -> Output drive
                    outputProcessor.setDrive(normalized);
                    break;
                case 96: {// C18 button -> cycle filter mode
                        if (normalized > 0.5f) {
                            // Cycle mode once, then apply to all voices
                            synth::BiquadFilter::Mode newMode;
                            bool modeSet = false;
                            allocator.forEachVoice([&newMode, &modeSet](midi::Synth& voice) {
                                auto& ws = static_cast<synth::WavetableSynth&>(voice);
                                if (!modeSet) {
                                    newMode = synth::BiquadFilter::nextMode(ws.getFilter().getMode());
                                    modeSet = true;
                                }
                                ws.getFilter().setMode(newMode);
                            });
                        }
                    }
                    break;
                case 102: // C24 button -> cycle output mode
                    if (normalized > 0.5f) {
                        outputProcessor.nextMode();
                        std::cout << "Output mode: " << outputProcessor.getName() 
                                  << " (drive=" << outputProcessor.getDrive() << ")" << std::endl;
                    }
                    break;
                case 103: // Copy to clipboard
                    if (normalized > 0.5f) {
                        clipboard.captureFromVoices(allocator);
                        std::cout << "Copied current settings to clipboard" << std::endl;
                    }
                    break;
                case 104: // Paste from clipboard
                    if (normalized > 0.5f) {
                        if (currentProgram == 1) {
                            std::cout << "ERROR: Cannot paste into program 1 (protected)" << std::endl;
                        } else {
                            clipboard.applyToVoices(allocator);
                            clipboard.saveToFile(currentProgram);
                            std::cout << "Pasted clipboard to program " 
                                      << static_cast<int>(currentProgram) << std::endl;
                        }
                    }
                    break;
                
                default:
                    std::cout << "Unhandled CC #" << static_cast<int>(cc) 
                          << " value " << static_cast<int>(value) << std::endl;
                    break;
            }
            
            // Print current parameters (table row) after processing CC
            allocator.forEachVoice([&](midi::Synth& voice) {
                auto* v = static_cast<synth::WavetableSynth*>(&voice);
                const char* modeStr = "???";
                switch(v->getFilter().getMode()) {
                    case synth::BiquadFilter::Mode::LOWPASS:  modeStr = "LP"; break;
                    case synth::BiquadFilter::Mode::HIGHPASS: modeStr = "HP"; break;
                    case synth::BiquadFilter::Mode::BANDPASS: modeStr = "BP"; break;
                    case synth::BiquadFilter::Mode::NOTCH:    modeStr = "NT"; break;
                    case synth::BiquadFilter::Mode::ALLPASS:  modeStr = "AP"; break;
                    default: break;
                }
                
                printf("%3d | %3d | %6.3f | %10.1f | %5.2f | %4s | %7.3f | %8.3f | %8.3f | %5.3f | %8.3f\n",
                    cc, value,
                    normalized,  // WaveShape (normalized CC value as proxy)
                    v->getBaseCutoff(),
                    v->getFilter().getQ(),
                    modeStr,
                    v->getFilterEnvelopeAmount(),
                    v->getFilterEnvelope().getAttackTime(),
                    v->getFilterEnvelope().getDecayTime(),
                    v->getFilterEnvelope().getSustainLevel(),
                    v->getFilterEnvelope().getReleaseTime());
            });
        };
        
        // Program change callback
        auto programChangeMapper = [&](uint8_t channel, uint8_t program, midi::SynthVoiceAllocator& allocator) {
            currentProgram = program;
            
            midi::ProgramData programData;
            if (programData.loadFromFile(program)) {
                programData.applyToVoices(allocator);
                std::cout << "Loaded program " << static_cast<int>(program) << std::endl;
            } else {
                // Use defaults if file doesn't exist
                programData.applyToVoices(allocator);
                std::cout << "Program " << static_cast<int>(program) 
                          << " not found, using defaults" << std::endl;
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
        midi::StreamProcessor midiProcessor(std::move(voiceAllocator), 0, ccMapper, polyAftertouchMapper, programChangeMapper);
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
                
                // Pass 1: Mix all voices into buffer (mono, using left channel position)
                for (unsigned int frame = 0; frame < numFrames; ++frame) {
                    float sample = 0.0f;
                    for (auto* synth : activeSynths) {
                        sample += synth->nextSample();
                    }
                    buffer[frame * CHANNELS] = sample;  // Store at left channel position
                }
                
                // Pass 2: Process buffer in-place with output processor
                // Extract mono samples to temporary buffer for processing
                // TODO lost the in-place feature here
                float monoBuffer[numFrames];
                for (unsigned int frame = 0; frame < numFrames; ++frame) {
                    monoBuffer[frame] = buffer[frame * CHANNELS];
                }
                outputProcessor.processBuffer(monoBuffer, numFrames);
                
                // Pass 3: Duplicate processed mono to stereo
                for (unsigned int frame = 0; frame < numFrames; ++frame) {
                    float processed = monoBuffer[frame];
                    buffer[frame * CHANNELS + 0] = processed; // Left
                    buffer[frame * CHANNELS + 1] = processed; // Right
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