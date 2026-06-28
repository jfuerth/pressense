#pragma once

#include <sawtooth_synth.hpp>
#include <stream_processor.hpp>
#include <web_controller.hpp>
#include <polyphonic_synth_target.hpp>
#include <output_processor.hpp>
#include <performance_timer.hpp>
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
 * 
 * Architecture:
 * - PolyphonicSynthTarget: Bridges MIDI to synth voices (owns voices)
 * - StreamProcessor: Parses MIDI bytes, routes to target
 * - OutputProcessor: Post-processing (drive, filtering)
 */
class SynthApplication {
public:
    using VoicePool = PolyphonicSynthTarget<synth::WavetableSynth>;

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
        
        // Create voice pool with wavetable synth factory
        voicePool_ = std::make_unique<VoicePool>(
            maxVoices_,
            [sampleRate]() {
                return std::make_unique<synth::WavetableSynth>(static_cast<float>(sampleRate));
            }
        );
        
        // Load program using provided storage implementation
        if (programStorage_) {
            loadCurrentProgram();
        } else {
            logWarn("No program storage provided; using synthesizer defaults");
        }
        
        // Create MIDI processor with callbacks that capture our voice pool
        midiProcessor_ = std::make_unique<midi::StreamProcessor>(
            *voicePool_,
            0,  // Default channel
            [this](uint8_t ch, uint8_t cc, uint8_t val) {
                handleCC(ch, cc, val);
            },
            [this](uint8_t ch, uint8_t prog) {
                handleProgramChange(ch, prog);
            }
        );
        
        // Allocate temporary buffer for mono processing
        monoBuffer_.resize(256);
        
        logInfo("MIDI processor ready with %d voices", maxVoices_);
        
        // Create web controller for control panel communication
        webController_ = std::make_unique<webcontrol::WebController>(
            [this](auto visitor) { voicePool_->forEachVoice(visitor); },
            programStorage_.get()
        );
    }
    
    /**
     * @brief Set callback for base note changes
     * 
     * This allows external code (e.g. main) to hook into base note commands
     * since the keyboard controller is not part of SynthApplication.
     */
    void setBaseNoteCallback(webcontrol::SetBaseNoteCallback callback) {
        webController_->setBaseNoteCallback(std::move(callback));
    }

    /**
     * @brief Set callbacks for the keyboard aftertouch input range
     *
     * Like the base note, the aftertouch range lives in the keyboard controller,
     * which is not owned by SynthApplication, so these forward to the web
     * controller's callback hooks.
     */
    void setAftertouchMinRatioCallback(webcontrol::SetAftertouchRatioCallback callback) {
        webController_->setAftertouchMinRatioCallback(std::move(callback));
    }
    void setAftertouchMaxRatioCallback(webcontrol::SetAftertouchRatioCallback callback) {
        webController_->setAftertouchMaxRatioCallback(std::move(callback));
    }
    void setAftertouchRangeProvider(webcontrol::GetAftertouchRangeCallback callback) {
        webController_->setAftertouchRangeProvider(std::move(callback));
    }
    
    /**
     * @brief Process incoming MIDI byte
     */
    void processMidiByte(uint8_t byte) {
        midiProcessor_->process(byte);
    }
    
    /**
     * @brief Process incoming command character (for serial input)
     * @return true if a complete command was processed
     */
    bool processCommandChar(char ch) {
        if (webController_->accumulate(ch)) {
            webController_->process(webController_->getLineBuffer());
            return true;
        }
        return false;
    }
    
    /**
     * @brief Process a complete command line
     */
    void processCommand(const char* jsonLine) {
        webController_->process(jsonLine);
    }
    
    /**
     * @brief Render audio buffer
     * 
     * @tparam TimingPolicy Policy class providing now() and unitName()
     * @tparam MaxSpans Maximum number of span names the timer can track
     * @param buffer Output buffer (interleaved stereo)
     * @param numFrames Number of frames to render
     * @param timer Lap timer for performance measurement. Span names are
     *  implementation details but should be consistent for telemetry output.
     *  Pass a NoOpTimingPolicy timer if timing is not needed.
     */
    template<typename TimingPolicy, size_t MaxSpans>
    void renderAudio(float* buffer, unsigned int numFrames,
                     features::LapTimer<TimingPolicy, MaxSpans>& timer) {
        // Resize mono buffer if needed
        if (monoBuffer_.size() < numFrames) {
            monoBuffer_.resize(numFrames);
        }
        
        // Pass 1: Mix all voices into mono buffer
        timer.nextSpan("app:voice_synthesis");
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            float sample = 0.0f;
            voicePool_->forEachVoice([&sample, &timer](synth::WavetableSynth& synth) {
                sample += synth.nextSample(timer);
            });
            monoBuffer_[frame] = sample;
        }
        
        // Pass 2: Process with output processor
        timer.nextSpan("app:output_processing");
        outputProcessor_.processBuffer(monoBuffer_.data(), numFrames);
        
        // Pass 3: Duplicate mono to stereo
        timer.nextSpan("app:stereo_dup");
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            float processed = monoBuffer_[frame];
            buffer[frame * channels_ + 0] = processed;
            buffer[frame * channels_ + 1] = processed;
        }
    }

    /**
     * @brief Get the voice pool for direct access (e.g., for program loading)
     */
    VoicePool& getVoicePool() { return *voicePool_; }
    
#ifdef FEATURE_CLIPBOARD
    void setClipboard(std::unique_ptr<features::Clipboard> clipboard) {
        clipboard_ = std::move(clipboard);
    }
#endif

private:
    void handleCC(uint8_t channel, uint8_t cc, uint8_t value) {
        float normalized = static_cast<float>(value) / 127.0f;
        
        switch(cc) {
            case 1: // Modulation wheel -> waveform shape
                voicePool_->forEachVoice([normalized](synth::WavetableSynth& voice) {
                    voice.getOscillator().updateWavetable(normalized);
                });
                break;
            case 20: { // Filter cutoff (exponential 100Hz - 10kHz)
                    const float MIN_CUTOFF = 100.0f;
                    const float MAX_CUTOFF = 10000.0f;
                    float cutoff = MIN_CUTOFF * std::pow(MAX_CUTOFF / MIN_CUTOFF, normalized);
                    voicePool_->forEachVoice([cutoff](synth::WavetableSynth& voice) {
                        voice.setBaseCutoff(cutoff);
                    });
                }
                break;
            case 21: // Filter resonance (Q 0.1 - 20.0)
                voicePool_->forEachVoice([normalized](synth::WavetableSynth& voice) {
                    voice.getFilter().setQ(0.1f + normalized * 19.9f);
                });
                break;
            case 71: // Filter envelope attack (1ms - 2s)
                voicePool_->forEachVoice([normalized](synth::WavetableSynth& voice) {
                    float attackTime = 0.001f + normalized * 2.0f;
                    voice.getFilterEnvelope().setAttackTime(attackTime);
                });
                break;
            case 72: // Filter envelope decay (10ms - 5s)
                voicePool_->forEachVoice([normalized](synth::WavetableSynth& voice) {
                    float decayTime = 0.01f + normalized * 5.0f;
                    voice.getFilterEnvelope().setDecayTime(decayTime);
                });
                break;
            case 25: // Filter envelope sustain level
                voicePool_->forEachVoice([normalized](synth::WavetableSynth& voice) {
                    voice.getFilterEnvelope().setSustainLevel(normalized);
                });
                break;
            case 73: // Filter envelope release (10ms - 5s)
                voicePool_->forEachVoice([normalized](synth::WavetableSynth& voice) {
                    float releaseTime = 0.01f + normalized * 5.0f;
                    voice.getFilterEnvelope().setReleaseTime(releaseTime);
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
                        voicePool_->forEachVoice([&newMode, &modeSet](synth::WavetableSynth& voice) {
                            if (!modeSet) {
                                newMode = synth::BiquadFilter::nextMode(voice.getFilter().getMode());
                                modeSet = true;
                            }
                            voice.getFilter().setMode(newMode);
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
                    clipboard_->copy([this](auto visitor) { voicePool_->forEachVoice(visitor); });
                }
                break;
            case 104: // Paste from clipboard
                if (normalized > 0.5f && clipboard_) {
                    auto voiceIterator = [this](auto visitor) { voicePool_->forEachVoice(visitor); };
                    if (currentProgram_ == 1) {
                        logError("Cannot paste into program 1 (protected)");
                    } else if (programStorage_) {
                        clipboard_->pasteAndSave(voiceIterator, currentProgram_, *programStorage_);
                    } else {
                        clipboard_->paste(voiceIterator);
                    }
                }
                break;
#endif
            default:
                break;
        }
    }
    
    void handleProgramChange(uint8_t channel, uint8_t program) {
        currentProgram_ = program;
        loadCurrentProgram();
    }
    
    void loadCurrentProgram() {
        if (programStorage_) {
            programStorage_->loadProgram(currentProgram_, [this](auto visitor) {
                voicePool_->forEachVoice(visitor);
            });
        } else {
            logWarn("Program change requested but no storage available (program %d)", currentProgram_);
        }
    }

    unsigned int sampleRate_;
    unsigned int channels_;
    uint8_t maxVoices_;
    
    std::unique_ptr<VoicePool> voicePool_;
    std::unique_ptr<midi::StreamProcessor> midiProcessor_;
    std::unique_ptr<webcontrol::WebController> webController_;
    
    synth::OutputProcessor outputProcessor_;
    uint8_t currentProgram_;
    
    std::vector<float> monoBuffer_;
    
    std::unique_ptr<features::ProgramStorage> programStorage_;
    
#ifdef FEATURE_CLIPBOARD
    std::unique_ptr<features::Clipboard> clipboard_;
#endif
};

} // namespace platform
