#pragma once

#include <sawtooth_synth.hpp>
#include <stream_processor.hpp>
#include <polyphonic_synth_target.hpp>
#include <output_processor.hpp>
#include <timing_stats.hpp>
#include <log.hpp>
#include <memory>
#include <functional>
#include <cmath>

// Feature interfaces
#include <program_storage.hpp>

#ifdef FEATURE_CLIPBOARD
#include <clipboard.hpp>
#endif

#ifdef FEATURE_PERFORMANCE_TIMING
#include <timing.hpp>
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
#ifdef FEATURE_PERFORMANCE_TIMING
        , platformTimer_()
#endif
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
#ifdef FEATURE_PERFORMANCE_TIMING
        platform::IntervalTimer<> timer;
#endif
        
        // Resize mono buffer if needed
        if (monoBuffer_.size() < numFrames) {
            monoBuffer_.resize(numFrames);
        }
        
        // Pass 1: Mix all voices into mono buffer
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            float sample = 0.0f;
            voicePool_->forEachVoice([&sample](synth::WavetableSynth& synth) {
                sample += synth.nextSample();
            });
            monoBuffer_[frame] = sample;
        }
        
#ifdef FEATURE_PERFORMANCE_TIMING
        timingVoiceMixing_.record(timer.elapsed());
#endif
        
        // Pass 2: Process with output processor
        outputProcessor_.processBuffer(monoBuffer_.data(), numFrames);
        
#ifdef FEATURE_PERFORMANCE_TIMING
        timingOutputProcessing_.record(timer.elapsed());
#endif
        
        // Pass 3: Duplicate mono to stereo
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            float processed = monoBuffer_[frame];
            buffer[frame * channels_ + 0] = processed;
            buffer[frame * channels_ + 1] = processed;
        }
        
#ifdef FEATURE_PERFORMANCE_TIMING
        timingStereoDup_.record(timer.elapsed());
#endif
    }

    /**
     * @brief Get timing statistics and reset counters
     */
    void getAndResetTimingStats(TimingStats& outVoiceMixing,
                                 TimingStats& outOutputProcessing,
                                 TimingStats& outStereoDup) {
        outVoiceMixing = timingVoiceMixing_;
        outOutputProcessing = timingOutputProcessing_;
        outStereoDup = timingStereoDup_;
        
        timingVoiceMixing_.reset();
        timingOutputProcessing_.reset();
        timingStereoDup_.reset();
    }
    
    /**
     * @brief Access voice timing for aggregation
     */
    VoiceTimingStats getAndResetVoiceTimingStats() {
        VoiceTimingStats combined;
        
        voicePool_->forEachVoice([&](synth::WavetableSynth& voice) {
            auto voiceStats = voice.getAndResetVoiceTimingStats();
            combined.merge(voiceStats);
        });
        
        return combined;
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
    
    synth::OutputProcessor outputProcessor_;
    uint8_t currentProgram_;
    
    std::vector<float> monoBuffer_;
    
    std::unique_ptr<features::ProgramStorage> programStorage_;
    
#ifdef FEATURE_CLIPBOARD
    std::unique_ptr<features::Clipboard> clipboard_;
#endif
    
    TimingStats timingVoiceMixing_;
    TimingStats timingOutputProcessing_;
    TimingStats timingStereoDup_;

#ifdef FEATURE_PERFORMANCE_TIMING
    platform::PlatformTimer platformTimer_;
#endif
};

} // namespace platform
