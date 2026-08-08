#pragma once

#include <voice.hpp>
#include <wavetable_oscillator.hpp>
#include <adsr_envelope.hpp>
#include <biquad_filter.hpp>
#include <lfo.hpp>
#include <aftertouch_modulator.hpp>
#include <performance_timer.hpp>
#include <fast_math.hpp>
#include <cmath>

namespace synth {

/**
 * @brief Modular wavetable synthesizer with filter and dual ADSR envelopes
 * 
 * Composed of:
 * - WavetableOscillator: Morphable waveform generation
 * - BiquadFilter: Resonant lowpass filter
 * - AdsrEnvelope (amplitude): Volume envelope
 * - AdsrEnvelope (filter): Filter cutoff modulation envelope
 * - Lfo (vibrato): Pitch modulation
 * - Lfo (tremolo): Amplitude modulation
 * - Inline pitch bend processing
 * - Per-voice aftertouch modulation
 * 
 * Filter envelope modulates cutoff frequency relative to base cutoff (from timbre).
 * Filter envelope amount controls modulation depth.
 * Aftertouch can modulate waveform shape, filter cutoff, resonance, filter env
 * amount, filter env sustain, vibrato rate/depth, tremolo rate/depth, pitch bend,
 * and voice level.
 *
 * Every aftertouch-modulatable parameter is owned by this class rather than by
 * the component it drives, because the component holds the *modulated* value
 * while a key is held. The getters below always report the unmodulated baseline,
 * which is what program save and the control panel need. As a consequence,
 * shape and resonance must be set through setWaveformShape()/setFilterQ(), not
 * through getOscillator()/getFilter() -- a direct write there is overwritten by
 * the next rendered sample.
 */
class WavetableSynth : public Voice {
public:
    // Ranges over which the additively-modulated parameters sweep. An atMod of
    // 1.0 at full pressure moves the parameter by the whole span, so every
    // setting is reachable from any baseline. These match the CC mappings in
    // platform/synth_application.hpp and the knob ranges in the control panel.
    static constexpr float FILTER_Q_MIN = 0.1f;
    static constexpr float FILTER_Q_MAX = 20.0f;
    static constexpr float LFO_RATE_MIN = 0.1f;
    static constexpr float LFO_RATE_MAX = 20.0f;
    // Voice level, applied before the shared clipper. Voices sum raw into it and
    // there is no master volume afterwards, so this is the voice-side headroom
    // control: eight voices at 1.0 already peak well into saturation.
    static constexpr float LEVEL_MIN = 0.0f;
    static constexpr float LEVEL_MAX = 1.0f;

    WavetableSynth(float sampleRate = 44100.0f)
        : sampleRate_(sampleRate),
          oscillator_(sampleRate),
          filter_(sampleRate),
          ampEnvelope_(sampleRate),
          filterEnvelope_(sampleRate),
          vibratoLfo_(sampleRate),
          tremoloLfo_(sampleRate) {
        // Initialize filter with default settings
        filter_.setMode(BiquadFilter::Mode::LOWPASS);
        setFilterQ(0.707f);  // Butterworth response

        // Initialize filter envelope with defaults
        filterEnvelope_.setAttackTime(0.005f);   // 5ms
        filterEnvelope_.setDecayTime(0.2f);      // 200ms
        setFilterEnvSustain(0.3f);               // via the baseline setter, so
                                                 // the applied cache is seeded too
        filterEnvelope_.setReleaseTime(0.1f);    // 100ms

        // Seed the baseline caches from the components' own defaults, so the
        // getters are correct before anything is explicitly set and the first
        // rendered sample has no spurious change to apply.
        setWaveformShape(oscillator_.getShape());
        setVibratoRate(vibratoLfo_.getRate());
        setTremoloRate(tremoloLfo_.getRate());
    }
    
    void trigger(float frequencyHz, float volume) override {
        baseFrequency_ = frequencyHz;
        volume_ = volume;
        aftertouch_ = 0.0f;  // Reset aftertouch on new note
        oscillator_.reset();
        filter_.reset();  // Clear filter state for clean attack
        // Aftertouch is back to zero, so return the modulated sub-components to
        // their baselines. Without this a new note would inherit the previous
        // note's modulated shape/resonance/rates up to the moved() thresholds.
        setWaveformShape(waveformShape_);
        setFilterQ(filterQ_);
        setFilterEnvSustain(filterEnvSustain_);
        setVibratoRate(vibratoRate_);
        setTremoloRate(tremoloRate_);
        vibratoLfo_.reset();
        tremoloLfo_.reset();
        ampEnvelope_.trigger();
        filterEnvelope_.trigger();
    }
    
    void release() override {
        ampEnvelope_.release();
        filterEnvelope_.release();
    }
    
    void setFrequency(float frequencyHz) override {
        baseFrequency_ = frequencyHz;
    }
    
    void setVolume(float volume) override {
        volume_ = volume;
    }
    
    void setPitchBend(float bendAmount) override {
        pitchBend_ = bendAmount;
    }
    
    float getPitchBendRange() const override {
        return pitchBendRange_;
    }
    
    void setPitchBendRange(float semitones) override {
        pitchBendRange_ = semitones;
    }
    
    bool isActive() const override {
        return ampEnvelope_.isActive();
    }

    /**
     * @brief Get oscillator for direct parameter control from CC callbacks
     *
     * Do not set the waveform shape here; use setWaveformShape() so the
     * baseline survives aftertouch modulation.
     */
    WavetableOscillator& getOscillator() {
        return oscillator_;
    }

    /**
     * @brief Get filter for direct parameter control from CC callbacks
     *
     * Do not set Q here; use setFilterQ() so the baseline survives aftertouch
     * modulation. Cutoff and mode are fine to set directly.
     */
    BiquadFilter& getFilter() {
        return filter_;
    }

    /**
     * @brief Set the base waveform morph (0=sawtooth, 0.5=triangle, 1=square)
     */
    void setWaveformShape(float shape) {
        if (shape < 0.0f) shape = 0.0f;
        if (shape > 1.0f) shape = 1.0f;
        waveformShape_ = shape;
        appliedWaveformShape_ = shape;
        oscillator_.updateWavetable(shape);
    }

    /**
     * @brief Get the base waveform morph (before aftertouch modulation)
     */
    float getWaveformShape() const { return waveformShape_; }

    /**
     * @brief Set the base filter resonance (before aftertouch modulation)
     */
    void setFilterQ(float q) {
        if (q < FILTER_Q_MIN) q = FILTER_Q_MIN;
        if (q > FILTER_Q_MAX) q = FILTER_Q_MAX;
        filterQ_ = q;
        appliedFilterQ_ = q;
        filter_.setQ(q);
    }

    /**
     * @brief Get the base filter resonance (before aftertouch modulation)
     */
    float getFilterQ() const { return filterQ_; }

    /**
     * @brief Set base filter cutoff (controlled by CC, before envelope modulation)
     */
    void setBaseCutoff(float cutoff) {
        baseCutoff_ = cutoff;
    }
    
    /**
     * @brief Get base filter cutoff (before envelope modulation)
     */
    float getBaseCutoff() const {
        return baseCutoff_;
    }
    
    /**
     * @brief Get filter envelope for parameter control from CC callbacks
     *
     * Do not set the sustain level here; use setFilterEnvSustain() so the
     * baseline survives aftertouch modulation. The times are fine to set
     * directly.
     */
    AdsrEnvelope& getFilterEnvelope() {
        return filterEnvelope_;
    }

    /**
     * @brief Set the base filter envelope sustain level (before aftertouch mod)
     * @param level Sustain level [0.0, 1.0]
     */
    void setFilterEnvSustain(float level) {
        if (level < 0.0f) level = 0.0f;
        if (level > 1.0f) level = 1.0f;
        filterEnvSustain_ = level;
        appliedFilterEnvSustain_ = level;
        filterEnvelope_.setSustainLevel(level);
    }

    /**
     * @brief Get the base filter envelope sustain level (before aftertouch mod)
     */
    float getFilterEnvSustain() const { return filterEnvSustain_; }

    /**
     * @brief Set the voice level (VCA gain applied before the shared clipper)
     * @param level Gain [LEVEL_MIN, LEVEL_MAX]
     *
     * Multiplies with note velocity and the amplitude envelope, and stacks with
     * the output stage's drive: raising it pushes this voice further into
     * saturation, lowering it buys headroom for polyphony.
     */
    void setLevel(float level) {
        if (level < LEVEL_MIN) level = LEVEL_MIN;
        if (level > LEVEL_MAX) level = LEVEL_MAX;
        level_ = level;
    }

    /**
     * @brief Get the voice level (before aftertouch modulation)
     */
    float getLevel() const { return level_; }


    /**
     * @brief Set filter envelope modulation amount
     * @param amount Modulation depth [0.0, 1.0]
     *               0.0 = no modulation, 1.0 = full range modulation
     */
    void setFilterEnvelopeAmount(float amount) {
        filterEnvAmount_ = amount;
        if (filterEnvAmount_ < 0.0f) filterEnvAmount_ = 0.0f;
        if (filterEnvAmount_ > 1.0f) filterEnvAmount_ = 1.0f;
    }
    
    /**
     * @brief Get current filter envelope amount
     */
    float getFilterEnvelopeAmount() const {
        return filterEnvAmount_;
    }
    
    /**
     * @brief Get amplitude envelope for parameter control
     */
    AdsrEnvelope& getAmpEnvelope() {
        return ampEnvelope_;
    }

    /**
     * @brief Get the vibrato LFO
     *
     * Its rate and depth carry the aftertouch-modulated values while a key is
     * held; set the baselines via setVibratoRate()/setVibratoDepth().
     */
    Lfo& getVibratoLfo() {
        return vibratoLfo_;
    }

    /**
     * @brief Get the tremolo LFO
     *
     * Its rate and depth carry the aftertouch-modulated values while a key is
     * held; set the baselines via setTremoloRate()/setTremoloDepth().
     */
    Lfo& getTremoloLfo() {
        return tremoloLfo_;
    }

    // ===== Vibrato (pitch LFO) =====
    
    // Clamped to the same span the aftertouch modulation sweeps, so the baseline
    // reported here is always the one actually being rendered.
    void setVibratoRate(float rateHz) {
        if (rateHz < LFO_RATE_MIN) rateHz = LFO_RATE_MIN;
        if (rateHz > LFO_RATE_MAX) rateHz = LFO_RATE_MAX;
        vibratoRate_ = rateHz;
        appliedVibratoRate_ = rateHz;
        vibratoLfo_.setRate(rateHz);
    }

    // The baseline rate, not the LFO's current (possibly aftertouch-modulated) one.
    float getVibratoRate() const {
        return vibratoRate_;
    }

    void setVibratoDepth(float semitones) {
        vibratoDepth_ = semitones;
    }
    
    float getVibratoDepth() const {
        return vibratoDepth_;
    }
    
    // ===== Tremolo (amplitude LFO) =====
    
    // Clamped to the same span the aftertouch modulation sweeps, so the baseline
    // reported here is always the one actually being rendered.
    void setTremoloRate(float rateHz) {
        if (rateHz < LFO_RATE_MIN) rateHz = LFO_RATE_MIN;
        if (rateHz > LFO_RATE_MAX) rateHz = LFO_RATE_MAX;
        tremoloRate_ = rateHz;
        appliedTremoloRate_ = rateHz;
        tremoloLfo_.setRate(rateHz);
    }

    // The baseline rate, not the LFO's current (possibly aftertouch-modulated) one.
    float getTremoloRate() const {
        return tremoloRate_;
    }

    void setTremoloDepth(float depth) {
        tremoloDepth_ = depth;
        if (tremoloDepth_ < 0.0f) tremoloDepth_ = 0.0f;
        if (tremoloDepth_ > 1.0f) tremoloDepth_ = 1.0f;
    }
    
    float getTremoloDepth() const {
        return tremoloDepth_;
    }
    
    // ===== Aftertouch =====
    
    /**
     * @brief Set per-voice aftertouch level
     * @param aftertouch Normalized aftertouch [0.0, 1.0]
     */
    void setAftertouch(float aftertouch) {
        aftertouch_ = aftertouch;
        if (aftertouch_ < 0.0f) aftertouch_ = 0.0f;
        if (aftertouch_ > 1.0f) aftertouch_ = 1.0f;
    }
    
    float getAftertouch() const {
        return aftertouch_;
    }
    
    // ===== Aftertouch Modulation Amounts =====
    // These control how much aftertouch affects each parameter
    // Range: [-1.0, 1.0], 0 = no effect
    //
    // Two modulation laws are in play (see aftertouch_modulator.hpp):
    // multiplicative, baseline * (1 + aftertouch * atMod), for parameters whose
    // baseline is never zero; and additive, baseline + aftertouch * atMod * span,
    // for the rest, so that a baseline of zero can still be modulated.

    void setBaseCutoffAtMod(float amount) { baseCutoff_atMod_ = amount; }
    float getBaseCutoffAtMod() const { return baseCutoff_atMod_; }

    void setWaveformShapeAtMod(float amount) { waveformShape_atMod_ = amount; }
    float getWaveformShapeAtMod() const { return waveformShape_atMod_; }

    void setFilterQAtMod(float amount) { filterQ_atMod_ = amount; }
    float getFilterQAtMod() const { return filterQ_atMod_; }

    void setVibratoRateAtMod(float amount) { vibratoRate_atMod_ = amount; }
    float getVibratoRateAtMod() const { return vibratoRate_atMod_; }

    void setTremoloRateAtMod(float amount) { tremoloRate_atMod_ = amount; }
    float getTremoloRateAtMod() const { return tremoloRate_atMod_; }

    void setFilterEnvAmountAtMod(float amount) { filterEnvAmount_atMod_ = amount; }
    float getFilterEnvAmountAtMod() const { return filterEnvAmount_atMod_; }

    // Additive over the sustain level's own [0, 1] span. Distinct from
    // filterEnvAmount_atMod_, which scales the whole envelope including the
    // attack peak: this moves only the level the envelope holds at, so pressure
    // sustains the brightness without changing the initial bite.
    void setFilterEnvSustainAtMod(float amount) { filterEnvSustain_atMod_ = amount; }
    float getFilterEnvSustainAtMod() const { return filterEnvSustain_atMod_; }

    // Additive over the level's own [0, 1] span, so a voice sitting at level 0
    // can still be brought up by pressure (a multiplicative law could not).
    void setLevelAtMod(float amount) { level_atMod_ = amount; }
    float getLevelAtMod() const { return level_atMod_; }


    void setVibratoDepthAtMod(float amount) { vibratoDepth_atMod_ = amount; }
    float getVibratoDepthAtMod() const { return vibratoDepth_atMod_; }
    
    void setTremoloDepthAtMod(float amount) { tremoloDepth_atMod_ = amount; }
    float getTremoloDepthAtMod() const { return tremoloDepth_atMod_; }

    // Pitch bend aftertouch modulation is expressed directly in semitones
    // (unlike the [-1, 1] scalers above): full aftertouch bends the pitch by
    // this many semitones. Clamped to [-12, +12]. Negative bends downward.
    void setPitchBendAtMod(float semitones) {
        if (semitones < -12.0f) semitones = -12.0f;
        if (semitones > 12.0f) semitones = 12.0f;
        pitchBend_atMod_ = semitones;
    }
    float getPitchBendAtMod() const { return pitchBend_atMod_; }
    
    /**
     * @brief Generate the next audio sample
     * 
     * @tparam TimingPolicy Policy class providing now() and unitName()
     * @tparam MaxSpans Maximum number of span names the timer can track
     * @param timer Lap timer for performance measurement. Span names use "synth:" prefix.
     * @return Audio sample in range [-1.0, 1.0]
     */
    template<typename TimingPolicy, size_t MaxSpans>
    float nextSample(features::LapTimer<TimingPolicy, MaxSpans>& timer) {
        if (!ampEnvelope_.isActive()) {
            timer.nextSpan("synth:inactive");
            return 0.0f;
        }
        
        // Calculate aftertouch-modulated parameters
        timer.nextSpan("synth:aftertouch");
        float effectiveCutoff = baseCutoff_ * (1.0f + aftertouch_ * baseCutoff_atMod_);
        float effectiveFilterEnvAmount = filterEnvAmount_ * (1.0f + aftertouch_ * filterEnvAmount_atMod_);
        // Vibrato and tremolo use additive modulation (can start from 0)
        float effectiveVibratoDepth = vibratoDepth_ + aftertouch_ * vibratoDepth_atMod_;
        float effectiveTremoloDepth = tremoloDepth_ + aftertouch_ * tremoloDepth_atMod_;
        // Shape, resonance and the LFO rates are additive over their full span,
        // so full pressure at atMod=1 sweeps the parameter end to end. Shape's
        // span is 1.0, so it needs no explicit factor.
        float effectiveShape = waveformShape_ + aftertouch_ * waveformShape_atMod_;
        float effectiveFilterQ = filterQ_ +
            aftertouch_ * filterQ_atMod_ * (FILTER_Q_MAX - FILTER_Q_MIN);
        float effectiveVibratoRate = vibratoRate_ +
            aftertouch_ * vibratoRate_atMod_ * (LFO_RATE_MAX - LFO_RATE_MIN);
        float effectiveTremoloRate = tremoloRate_ +
            aftertouch_ * tremoloRate_atMod_ * (LFO_RATE_MAX - LFO_RATE_MIN);
        // Sustain level and voice level both span 1.0, so they need no factor.
        float effectiveFilterEnvSustain = filterEnvSustain_ +
            aftertouch_ * filterEnvSustain_atMod_;
        float effectiveLevel = level_ + aftertouch_ * level_atMod_;

        // Clamp values to valid ranges
        if (effectiveCutoff < 20.0f) effectiveCutoff = 20.0f;
        if (effectiveCutoff > 20000.0f) effectiveCutoff = 20000.0f;
        if (effectiveFilterEnvAmount < 0.0f) effectiveFilterEnvAmount = 0.0f;
        if (effectiveFilterEnvAmount > 1.0f) effectiveFilterEnvAmount = 1.0f;
        if (effectiveVibratoDepth < 0.0f) effectiveVibratoDepth = 0.0f;
        if (effectiveTremoloDepth < 0.0f) effectiveTremoloDepth = 0.0f;
        if (effectiveTremoloDepth > 1.0f) effectiveTremoloDepth = 1.0f;
        if (effectiveShape < 0.0f) effectiveShape = 0.0f;
        if (effectiveShape > 1.0f) effectiveShape = 1.0f;
        if (effectiveFilterQ < FILTER_Q_MIN) effectiveFilterQ = FILTER_Q_MIN;
        if (effectiveFilterQ > FILTER_Q_MAX) effectiveFilterQ = FILTER_Q_MAX;
        if (effectiveVibratoRate < LFO_RATE_MIN) effectiveVibratoRate = LFO_RATE_MIN;
        if (effectiveVibratoRate > LFO_RATE_MAX) effectiveVibratoRate = LFO_RATE_MAX;
        if (effectiveTremoloRate < LFO_RATE_MIN) effectiveTremoloRate = LFO_RATE_MIN;
        if (effectiveTremoloRate > LFO_RATE_MAX) effectiveTremoloRate = LFO_RATE_MAX;
        if (effectiveFilterEnvSustain < 0.0f) effectiveFilterEnvSustain = 0.0f;
        if (effectiveFilterEnvSustain > 1.0f) effectiveFilterEnvSustain = 1.0f;
        if (effectiveLevel < LEVEL_MIN) effectiveLevel = LEVEL_MIN;
        if (effectiveLevel > LEVEL_MAX) effectiveLevel = LEVEL_MAX;

        // Calculate vibrato (pitch modulation). setRate() costs a divide, so it
        // is only re-applied when the rate has actually moved (see moved()).
        timer.nextSpan("synth:vibrato");
        if (moved(effectiveVibratoRate, appliedVibratoRate_, 0.01f)) {
            appliedVibratoRate_ = effectiveVibratoRate;
            vibratoLfo_.setRate(effectiveVibratoRate);
        }
        vibratoLfo_.setDepth(effectiveVibratoDepth);
        float vibratoMod = vibratoLfo_.nextSample();  // Returns semitone offset

        // Calculate current frequency with pitch bend and vibrato.
        // Aftertouch adds a further linear bend (semitones) on top of the MIDI
        // pitch wheel and vibrato; all three sources sum in the semitone domain.
        timer.nextSpan("synth:pitch_bend");
        float aftertouchBend = aftertouch_ * pitchBend_atMod_;
        float semitoneShift = pitchBend_ * pitchBendRange_ + vibratoMod + aftertouchBend;

        // The exponential semitone->frequency mapping is guarded two ways, because
        // vibrato and aftertouch bend make it run constantly.
        //
        // First, semitonesToRatio uses a polynomial rather than powf, which costs
        // 450 cycles here - 7% of the entire eight-voice budget for one call. It
        // is accurate to well under a hundredth of a cent.
        //
        // Second, it is only recomputed when the combined shift (or the base note)
        // actually moves. 0.01 semitone is well below the ~5-cent pitch JND, so
        // this is inaudible, and it keeps a held, unmodulated note from paying
        // anything at all. This mirrors the change-threshold guard in
        // BiquadFilter::setCutoff.
        float shiftDelta = semitoneShift - lastSemitoneShift_;
        if (shiftDelta < 0.0f) shiftDelta = -shiftDelta;
        if (baseFrequency_ != cachedBaseFrequency_ || shiftDelta > 0.01f) {
            cachedBaseFrequency_ = baseFrequency_;
            lastSemitoneShift_ = semitoneShift;
            cachedFrequency_ = baseFrequency_ * semitonesToRatio(semitoneShift);
        }
        float frequency = cachedFrequency_;
        
        // Generate oscillator sample. updateWavetable() only recomputes three
        // blend weights, but skip it when the morph hasn't moved audibly.
        timer.nextSpan("synth:oscillator");
        if (moved(effectiveShape, appliedWaveformShape_, 0.002f)) {
            appliedWaveformShape_ = effectiveShape;
            oscillator_.updateWavetable(effectiveShape);
        }
        float sample = oscillator_.nextSample(frequency);

        // Calculate filter cutoff with envelope modulation.
        //
        // setSustainLevel() is a bare store - unlike the ADSR times, it does not
        // recompute the per-sample rates - so this is the cheapest modulation
        // destination in the voice. Two consequences of that, both deliberate:
        // the decay slope and release rate stay as the last time setting derived
        // them (AdsrEnvelope::updateRates divides by sustain), and an upward move
        // while the envelope is still decaying snaps the level to the new sustain
        // in one sample. Both are inaudible on a cutoff; neither would be on a
        // VCA, which is why voice level below is a plain gain instead.
        timer.nextSpan("synth:filter_env");
        if (moved(effectiveFilterEnvSustain, appliedFilterEnvSustain_, 0.002f)) {
            appliedFilterEnvSustain_ = effectiveFilterEnvSustain;
            filterEnvelope_.setSustainLevel(effectiveFilterEnvSustain);
        }
        float filterEnvLevel = filterEnvelope_.nextSample();
        float envModulation = filterEnvLevel * effectiveFilterEnvAmount;
        
        // Apply modulation (exponential, upward only)
        float modulatedCutoff = effectiveCutoff * (1.0f + envModulation * 9.0f);  // Up to 10x base cutoff
        
        // Resonance is applied before the cutoff so that a single coefficient
        // recompute can pick up both. Like BiquadFilter::setCutoff, small changes
        // are skipped: recomputing coefficients costs a sinf/cosf pair, and 3% of
        // the Q span is well below the audible threshold for resonance.
        timer.nextSpan("synth:filter");
        if (moved(effectiveFilterQ, appliedFilterQ_, 0.03f * (FILTER_Q_MAX - FILTER_Q_MIN))) {
            appliedFilterQ_ = effectiveFilterQ;
            filter_.setQ(effectiveFilterQ);
        }
        filter_.setCutoff(modulatedCutoff);

        // Apply filter
        sample = filter_.processSample(sample);
        
        // Calculate tremolo (amplitude modulation)
        timer.nextSpan("synth:tremolo");
        if (moved(effectiveTremoloRate, appliedTremoloRate_, 0.01f)) {
            appliedTremoloRate_ = effectiveTremoloRate;
            tremoloLfo_.setRate(effectiveTremoloRate);
        }
        tremoloLfo_.setDepth(effectiveTremoloDepth);
        float tremoloMod = tremoloLfo_.nextSample();  // Returns [-depth, +depth]
        float tremoloMultiplier = 1.0f + tremoloMod;  // Range: [1-depth, 1+depth]
        
        // Apply amplitude envelope, note velocity, voice level, and tremolo.
        // Voice level is a plain gain rather than a push into the amp envelope's
        // sustain, so pressure moves the amplitude in every phase - attack and
        // release included - with no discontinuity to click on.
        timer.nextSpan("synth:amp_env");
        float ampEnvLevel = ampEnvelope_.nextSample();
        sample *= ampEnvLevel * volume_ * effectiveLevel * tremoloMultiplier;
        
        return sample;
    }

private:
    /**
     * @brief Has a control-rate value moved enough to be worth re-applying?
     *
     * The effective values in nextSample() are recomputed every sample but only
     * actually change when aftertouch or a base parameter does. Pushing an
     * unchanged (or inaudibly changed) value into a component would repeat its
     * setup cost - biquad coefficients, an LFO phase increment - for nothing.
     */
    static inline bool moved(float value, float applied, float epsilon) {
        float delta = value - applied;
        if (delta < 0.0f) delta = -delta;
        return delta > epsilon;
    }

    float sampleRate_;

    // Components (composed by value for cache locality and inlining)
    WavetableOscillator oscillator_;
    BiquadFilter filter_;
    AdsrEnvelope ampEnvelope_;
    AdsrEnvelope filterEnvelope_;
    Lfo vibratoLfo_;
    Lfo tremoloLfo_;
    
    // Voice parameters
    float baseFrequency_ = 440.0f;
    float volume_ = 1.0f;
    float pitchBend_ = 0.0f;
    float pitchBendRange_ = 2.0f;
    float baseCutoff_ = 1000.0f;    // Base filter cutoff (controlled by setter)
    float filterEnvAmount_ = 0.5f;  // 50% modulation by default

    // LFO parameters (depth stored separately for aftertouch modulation)
    float vibratoDepth_ = 0.0f;     // Semitones
    float tremoloDepth_ = 0.0f;     // 0-1 amplitude modulation

    // Baselines for parameters that live in a sub-component but are aftertouch
    // modulated, so the component holds the modulated value while a key is held.
    // Seeded from the components' defaults by the constructor.
    float waveformShape_ = 0.0f;
    float filterQ_ = 0.707f;
    float filterEnvSustain_ = 0.3f;
    float vibratoRate_ = 5.0f;      // Hz
    float tremoloRate_ = 5.0f;      // Hz

    // Voice gain, applied in the final multiply rather than pushed into a
    // component, so it needs no applied-value cache. Mirrors ProgramData::level's
    // default, which is what a voice runs at before any program is loaded - the
    // firmware does not load one at boot.
    float level_ = 0.5f;

    // Per-voice aftertouch
    float aftertouch_ = 0.0f;       // Normalized [0, 1]

    // Aftertouch modulation amounts [-1, 1]
    float baseCutoff_atMod_ = 0.0f;
    float filterEnvAmount_atMod_ = 0.0f;
    float vibratoDepth_atMod_ = 0.0f;
    float tremoloDepth_atMod_ = 0.0f;
    float waveformShape_atMod_ = 0.0f;
    float filterQ_atMod_ = 0.0f;
    float vibratoRate_atMod_ = 0.0f;
    float tremoloRate_atMod_ = 0.0f;
    float filterEnvSustain_atMod_ = 0.0f;
    float level_atMod_ = 0.0f;
    float pitchBend_atMod_ = 0.0f;  // Semitones [-12, +12] (not a [-1,1] scaler)

    // Last value pushed into each sub-component, so nextSample() can skip
    // re-applying an unchanged one. Kept in sync by the baseline setters.
    float appliedWaveformShape_ = 0.0f;
    float appliedFilterQ_ = 0.707f;
    float appliedFilterEnvSustain_ = 0.3f;
    float appliedVibratoRate_ = 5.0f;
    float appliedTremoloRate_ = 5.0f;

    // Cache for the semitone->frequency mapping, so a held/slowly-modulated note
    // avoids calling std::pow every sample. cachedBaseFrequency_ starts at a
    // sentinel that can never match baseFrequency_, forcing the first compute.
    float lastSemitoneShift_ = 0.0f;
    float cachedFrequency_ = 0.0f;
    float cachedBaseFrequency_ = -1.0f;
};

} // namespace synth

