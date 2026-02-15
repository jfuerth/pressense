#pragma once

#include <sawtooth_synth.hpp>
#include <stream_processor.hpp>
#include <simple_voice_allocator.hpp>
#include <output_processor.hpp>
#include <log.hpp>
#include <memory>
#include <functional>
#include <cmath>

// Feature interfaces
#include <program_storage.hpp>

#ifdef FEATURE_CLIPBOARD
#include <clipboard.hpp>
#endif

namespace platform {

/**
 * @brief Platform-agnostic synthesizer application
 * 
 * Manages synth voices, MIDI processing, and audio rendering.
 * Platform-specific code provides MIDI input and audio output.
 */
class SynthApplication {
public:
    SynthApplication(unsigned int sampleRate = 44100,
                     unsigned int channels = 2,
                     uint8_t maxVoices = 8,
                     std::unique_ptr<features::ProgramStorage> programStorage = nullptr)
        : sampleRate_(sampleRate)
        , channels_(channels)
        , maxVoices_(maxVoices)
        , outputProcessor_(0.5f, static_cast<float>(sampleRate))
        , currentProgram_(1)
        , programStorage_(std::move(programStorage))
        {
        
        logInfo("Initializing synthesizer: %d Hz, %d voices", sampleRate_, maxVoices_);
        
        // Create voice allocator with wavetable synth factory
        auto voiceFactory = [sampleRate]() -> std::unique_ptr<midi::Synth> {
            return std::make_unique<synth::WavetableSynth>(static_cast<float>(sampleRate));
        };
        
        auto voiceAllocator = std::make_unique<midi::SimpleVoiceAllocator>(maxVoices_, voiceFactory);
        
        // Load program using provided storage implementation
        if (programStorage_) {
            programStorage_->loadProgram(currentProgram_, *voiceAllocator);
        } else {
            logWarn("No program storage provided; using synthesizer defaults");
        }
        
        // Create MIDI processor with callbacks
        midiProcessor_ = std::make_unique<midi::StreamProcessor>(
            std::move(voiceAllocator),
            0,  // Default channel
            [this](uint8_t ch, uint8_t cc, uint8_t val, midi::SynthVoiceAllocator& alloc) {
                handleCC(ch, cc, val, alloc);
            },
            [this](uint8_t ch, uint8_t note, uint8_t pressure, midi::Synth& voice) {
                handlePolyAftertouch(ch, note, pressure, voice);
            },
            [this](uint8_t ch, uint8_t prog, midi::SynthVoiceAllocator& alloc) {
                handleProgramChange(ch, prog, alloc);
            }
        );
        
        // Allocate temporary buffer for mono processing
        monoBuffer_.resize(256);  // Will resize as needed
        
        logInfo("MIDI processor ready with %d voices", maxVoices_);
    }
    
    /**
     * @brief Process incoming MIDI byte
     */
    void processMidiByte(uint8_t byte) {
        midiProcessor_->process(byte);
    }
    
    /**
     * @brief Render audio buffer
     * @param buffer Output buffer (interleaved stereo)
     * @param numFrames Number of frames to render
     */
    void renderAudio(float* buffer, unsigned int numFrames) {
        // Get all voices
        std::vector<synth::WavetableSynth*> activeSynths;
        midiProcessor_->forEachVoice([&](midi::Synth& synth) {
            activeSynths.push_back(static_cast<synth::WavetableSynth*>(&synth));
        });
        
        // Resize mono buffer if needed
        if (monoBuffer_.size() < numFrames) {
            monoBuffer_.resize(numFrames);
        }
        
        // Pass 1: Mix all voices into mono buffer
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            float sample = 0.0f;
            for (auto* synth : activeSynths) {
                sample += synth->nextSample();
            }
            monoBuffer_[frame] = sample;
        }
        
        // Pass 2: Process with output processor
        outputProcessor_.processBuffer(monoBuffer_.data(), numFrames);
        
        // Pass 3: Duplicate processed mono to stereo
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            float processed = monoBuffer_[frame];
            buffer[frame * channels_ + 0] = processed; // Left
            buffer[frame * channels_ + 1] = processed; // Right
        }
    }
    
    midi::StreamProcessor& getMidiProcessor() { return *midiProcessor_; }
    
#ifdef FEATURE_CLIPBOARD
    void setClipboard(std::unique_ptr<features::Clipboard> clipboard) {
        clipboard_ = std::move(clipboard);
    }
#endif

private:
    void handleCC(uint8_t channel, uint8_t cc, uint8_t value, midi::SynthVoiceAllocator& allocator) {
        float normalized = static_cast<float>(value) / 127.0f;
        
        switch(cc) {
            case 1:  // Modulation wheel -> waveform shape
                allocator.forEachVoice([normalized](midi::Synth& voice) {
                    static_cast<synth::WavetableSynth&>(voice).getOscillator().updateWavetable(normalized);
                });
                break;
            case 20: {// Filter cutoff (exponential 100Hz - 10kHz)
                    const float MIN_CUTOFF = 100.0f;
                    const float MAX_CUTOFF = 10000.0f;
                    float cutoff = MIN_CUTOFF * std::pow(MAX_CUTOFF / MIN_CUTOFF, normalized);
                    allocator.forEachVoice([cutoff](midi::Synth& voice) {
                        static_cast<synth::WavetableSynth&>(voice).setBaseCutoff(cutoff);
                    });
                }
                break;
            case 21: // Filter resonance (Q 0.1 - 20.0)
                allocator.forEachVoice([normalized](midi::Synth& voice) {
                    static_cast<synth::WavetableSynth&>(voice).getFilter().setQ(0.1f + normalized * 19.9f);
                });
                break;
            case 71: // Filter envelope attack (1ms - 2s)
                allocator.forEachVoice([normalized](midi::Synth& voice) {
                    float attackTime = 0.001f + normalized * 2.0f;
                    static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setAttackTime(attackTime);
                });
                break;
            case 72: // Filter envelope decay (10ms - 5s)
                allocator.forEachVoice([normalized](midi::Synth& voice) {
                    float decayTime = 0.01f + normalized * 5.0f;
                    static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setDecayTime(decayTime);
                });
                break;
            case 25: // Filter envelope sustain level
                allocator.forEachVoice([normalized](midi::Synth& voice) {
                    static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setSustainLevel(normalized);
                });
                break;
            case 73: // Filter envelope release (10ms - 5s)
                allocator.forEachVoice([normalized](midi::Synth& voice) {
                    float releaseTime = 0.01f + normalized * 5.0f;
                    static_cast<synth::WavetableSynth&>(voice).getFilterEnvelope().setReleaseTime(releaseTime);
                });
                break;
            case 74: // Output drive
                outputProcessor_.setDrive(normalized);
                break;
            case 70: {// Post-filter cutoff (100Hz - 20kHz)
                    const float MIN_CUTOFF = 100.0f;
                    const float MAX_CUTOFF = 20000.0f;
                    float cutoff = MIN_CUTOFF * std::pow(MAX_CUTOFF / MIN_CUTOFF, normalized);
                    outputProcessor_.getPostFilter().setCutoff(cutoff);
                }
                break;
            case 63: // Post-filter resonance
                outputProcessor_.getPostFilter().setQ(0.1f + normalized * 19.9f);
                break;
            case 96: {// Cycle filter mode
                    if (normalized > 0.5f) {
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
            case 102: // Cycle output mode
                if (normalized > 0.5f) {
                    outputProcessor_.nextMode();
                    logInfo("Output mode: %s (drive=%.2f)", 
                            outputProcessor_.getName(), outputProcessor_.getDrive());
                }
                break;
#ifdef FEATURE_CLIPBOARD
            case 103: // Copy to clipboard
                if (normalized > 0.5f && clipboard_) {
                    clipboard_->copy(allocator);
                }
                break;
            case 104: // Paste from clipboard
                if (normalized > 0.5f && clipboard_) {
                    if (currentProgram_ == 1) {
                        logError("Cannot paste into program 1 (protected)");
                    } else if (programStorage_) {
                        clipboard_->pasteAndSave(allocator, currentProgram_, *programStorage_);
                    } else {
                        clipboard_->paste(allocator);
                    }
                }
                break;
#endif
            default:
                // Silently ignore unknown CCs (ESP32 has limited logging)
                break;
        }
    }
    
    void handlePolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure, midi::Synth& voice) {
        // TODO: Map pressure to per-voice parameter
        // Potential mappings: filter cutoff, amplitude, LFO depth, vibrato, etc.
    }
    
    void handleProgramChange(uint8_t channel, uint8_t program, midi::SynthVoiceAllocator& allocator) {
        currentProgram_ = program;
        if (programStorage_) {
            programStorage_->loadProgram(program, allocator);
        } else {
            logWarn("Program change requested but no storage available (program %d)", program);
        }
    }

    unsigned int sampleRate_;
    unsigned int channels_;
    uint8_t maxVoices_;
    
    synth::OutputProcessor outputProcessor_;
    uint8_t currentProgram_;
    
    std::unique_ptr<midi::StreamProcessor> midiProcessor_;
    std::vector<float> monoBuffer_;
    
    std::unique_ptr<features::ProgramStorage> programStorage_;

#ifdef FEATURE_CLIPBOARD
    std::unique_ptr<features::Clipboard> clipboard_;
#endif
};

} // namespace platform
