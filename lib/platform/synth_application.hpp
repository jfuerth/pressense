#pragma once

#include <sawtooth_synth.hpp>
#include <stream_processor.hpp>
#include <cc_map.hpp>
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

        // Let the host play the synth over the control link, through the same
        // MIDI stream the keyboard uses.
        webController_->setMidiByteCallback([this](uint8_t byte) {
            midiProcessor_->process(byte);
        });

        registerOutputStageParams();
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
     * @brief Register an external control-panel param (e.g. a keyboard setting)
     *
     * Forwards to the web controller's param registry. Use this for params that
     * live outside the synth voices (such as the keyboard aftertouch range),
     * which SynthApplication doesn't own.
     */
    void registerExternalParam(const std::string& name,
                               std::function<void(float)> setter,
                               std::function<float()> getter) {
        webController_->registerParam(name, std::move(setter), std::move(getter));
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
        
        // Pass 1: Mix all voices into the mono buffer.
        //
        // Voice-major rather than frame-major, over indexed access rather than
        // forEachVoice(). Two reasons, both measured (AUDIO_PIPELINE_PERFORMANCE.md):
        // the old frame-major loop built a std::function every frame and reached
        // each voice through it, and it ran silent voices as well as sounding
        // ones. Skipping idle voices is what matters most - a keyboard is rarely
        // holding all of them - and it makes a one-voice buffer 37% cheaper.
        //
        // A silent voice's nextSample() returns 0, so skipping it is exactly
        // equivalent to summing it.
        timer.nextSpan("app:voice_synthesis");
        float* mono = monoBuffer_.data();
        for (unsigned int frame = 0; frame < numFrames; ++frame) {
            mono[frame] = 0.0f;
        }
        const size_t voiceCount = voicePool_->getVoiceCount();
        for (size_t v = 0; v < voiceCount; ++v) {
            synth::WavetableSynth& voice = voicePool_->voiceAt(v);
            if (!voice.isActive()) continue;
            for (unsigned int frame = 0; frame < numFrames; ++frame) {
                mono[frame] += voice.nextSample(timer);
            }
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

    /**
     * @brief Get the shared output stage (drive, clipping algorithm, post-filter)
     *
     * Global, unlike everything on a voice: one stage for the whole mix, and not
     * part of a program.
     */
    synth::OutputProcessor& getOutputProcessor() { return outputProcessor_; }
    
#ifdef FEATURE_CLIPBOARD
    void setClipboard(std::unique_ptr<features::Clipboard> clipboard) {
        clipboard_ = std::move(clipboard);
    }
#endif

private:
    /**
     * @brief Expose the output stage to the control panel
     *
     * The output processor is one shared stage rather than a per-voice setting,
     * so WebController - which only knows how to reach voices - cannot see it.
     * Registering it here gives the panel knobs for drive, the post-filter and
     * the clipping algorithm, and lets the CC table address them by name. These
     * are deliberately *not* part of ProgramData: they are global, so a program
     * change must not be able to jump the master drive.
     */
    void registerOutputStageParams() {
        webController_->registerParam("drive",
            [this](float v) { outputProcessor_.setDrive(v); },
            [this]() { return outputProcessor_.getDrive(); });

        webController_->registerParam("postFilterCutoff",
            [this](float v) { outputProcessor_.getPostFilter().setCutoff(v); },
            [this]() { return outputProcessor_.getPostFilter().getCutoff(); });

        webController_->registerParam("postFilterQ",
            [this](float v) { outputProcessor_.getPostFilter().setQ(v); },
            [this]() { return outputProcessor_.getPostFilter().getQ(); });

        // Rounded, not truncated, for the same reason as filterMode: a CC sweep
        // or a knob arrives fractional.
        webController_->registerParam("outputMode",
            [this](float v) {
                int index = static_cast<int>(v + 0.5f);
                if (index < 0) index = 0;
                outputProcessor_.setModeIndex(static_cast<size_t>(index));
            },
            [this]() { return static_cast<float>(outputProcessor_.getModeIndex()); });
    }

    void handleCC(uint8_t channel, uint8_t cc, uint8_t value) {
        float normalized = static_cast<float>(value) / 127.0f;

        // Continuous parameters are table-driven (midi/cc_map.hpp) and applied
        // through the same entry point the control panel uses, so a CC and its
        // knob cannot disagree about what the parameter means.
        if (const midi::CcMapping* mapping = midi::findCcMapping(cc)) {
            webController_->applyNamedParam(mapping->param,
                                            midi::ccValueToParam(*mapping, value));
            return;
        }

        // What is left are the presses: cycling a mode, or copying a patch.
        switch(cc) {
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
