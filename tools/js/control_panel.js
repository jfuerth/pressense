/**
 * Pressence Control Panel
 * 
 * Knob components and parameter UI for the synthesizer control panel.
 */

window.controlPanel = (function() {
    // Parameter definitions with ranges and display settings
    const PARAMS = {
        // Aftertouch (keyboard touch response): pressure ratio that maps to 0..127
        aftertouchMinRatio: { min: 1, max: 9, label: 'AT Min', unit: '', hasAtMod: false },
        aftertouchMaxRatio: { min: 2, max: 15, label: 'AT Max', unit: '', hasAtMod: false },

        // Oscillator
        waveformShape: { min: 0, max: 1, label: 'Waveform', unit: '', hasAtMod: true },

        // Filter
        baseCutoff: { min: 20, max: 20000, label: 'Cutoff', unit: 'Hz', log: true, hasAtMod: true },
        filterQ: { min: 0.1, max: 20, label: 'Resonance', unit: '', hasAtMod: true },
        
        // Filter Envelope
        filterEnvAmount: { min: 0, max: 1, label: 'Env Amount', unit: '', hasAtMod: true },
        filterEnvAttack: { min: 0.001, max: 2, label: 'Attack', unit: 's', log: true, hasAtMod: false },
        filterEnvDecay: { min: 0.01, max: 5, label: 'Decay', unit: 's', log: true, hasAtMod: false },
        filterEnvSustain: { min: 0, max: 1, label: 'Sustain', unit: '', hasAtMod: true },
        filterEnvRelease: { min: 0.01, max: 5, label: 'Release', unit: 's', log: true, hasAtMod: false },
        
        // Amplifier. Level is the voice's gain into the shared clipper, so it is
        // also the headroom control: turning it down is what makes room for
        // polyphony. 1.0 is unity.
        level: { min: 0, max: 1, label: 'Level', unit: '', hasAtMod: true },
        ampEnvAttack: { min: 0.001, max: 2, label: 'Attack', unit: 's', log: true, hasAtMod: false },
        ampEnvDecay: { min: 0.01, max: 5, label: 'Decay', unit: 's', log: true, hasAtMod: false },
        ampEnvSustain: { min: 0, max: 1, label: 'Sustain', unit: '', hasAtMod: false },
        ampEnvRelease: { min: 0.01, max: 5, label: 'Release', unit: 's', log: true, hasAtMod: false },
        
        // Vibrato
        vibratoRate: { min: 0.1, max: 20, label: 'Rate', unit: 'Hz', hasAtMod: true },
        vibratoDepth: { min: 0, max: 1, label: 'Depth', unit: 'st', hasAtMod: true },

        // Tremolo
        tremoloRate: { min: 0.1, max: 20, label: 'Rate', unit: 'Hz', hasAtMod: true },
        tremoloDepth: { min: 0, max: 1, label: 'Depth', unit: '', hasAtMod: true },

        // Pitch bend: aftertouch-only, and in a section of its own. No primary
        // knob (pitch bend itself comes from the MIDI wheel); the AT-Mod knob
        // sets how many semitones full pressure bends the pitch. Range/unit
        // differ from the default [-1,1].
        pitchBend: { label: 'Pitch Bend', hasAtMod: true, atModOnly: true,
                     atModMin: -12, atModMax: 12, atModUnit: 'st', atModStep: 1 }
    };

    // Aftertouch mod range defaults to [-1, 1] (a dimensionless depth). A base
    // param may override via atModMin/atModMax (and label the unit via atModUnit).
    function atModRange(baseConfig) {
        const c = baseConfig || {};
        return {
            min: (c.atModMin !== undefined) ? c.atModMin : -1,
            max: (c.atModMax !== undefined) ? c.atModMax : 1
        };
    }

    // Aftertouch mod params (informational; the real wiring is PARAMS[*].hasAtMod).
    // This is the full set the firmware supports - see WavetableSynth's
    // "Aftertouch Modulation Amounts" section.
    const AT_MOD_PARAMS = [
        'waveformShape_atMod', 'baseCutoff_atMod', 'filterQ_atMod',
        'filterEnvAmount_atMod', 'filterEnvSustain_atMod',
        'vibratoRate_atMod', 'vibratoDepth_atMod',
        'tremoloRate_atMod', 'tremoloDepth_atMod',
        'level_atMod', 'pitchBend_atMod'
    ];
    
    // Current parameter values
    let currentParams = {};
    
    // Debounce timer for parameter changes
    let sendTimer = null;
    const SEND_DELAY = 50; // ms

    // Drag/tap tuning. Knobs are driven by vertical drags: DRAG_RANGE_PX of
    // travel sweeps the parameter's whole range.
    const DRAG_RANGE_PX = 200;
    // A press that stays within TAP_SLOP_PX is a tap, not a drag. Two taps inside
    // DOUBLE_TAP_MS reset an AT Mod knob to zero.
    const TAP_SLOP_PX = 10;
    const DOUBLE_TAP_MS = 350;

    /**
     * Create a rotary knob element
     * @param {string} paramName Parameter name
     * @param {Object} config Parameter configuration
     * @param {boolean} isAtMod Is this an aftertouch mod knob
     * @returns {HTMLElement} Knob container element
     */
    function createKnob(paramName, config, isAtMod = false) {
        const container = document.createElement('div');
        container.className = 'knob-container' + (isAtMod ? ' at-mod-knob' : '');
        container.dataset.param = paramName;
        
        const label = document.createElement('div');
        label.className = 'knob-label';
        // Every AT knob is labelled "AT Mod", including one with no primary knob
        // (atModOnly) - its section header carries the parameter name.
        label.textContent = isAtMod ? 'AT Mod' : config.label;
        
        const knobWrapper = document.createElement('div');
        knobWrapper.className = 'knob-wrapper';
        
        const knob = document.createElement('div');
        knob.className = 'knob';
        knob.dataset.param = paramName;
        knob.dataset.isAtMod = isAtMod;
        
        const indicator = document.createElement('div');
        indicator.className = 'knob-indicator';
        knob.appendChild(indicator);
        
        const valueDisplay = document.createElement('div');
        valueDisplay.className = 'knob-value';

        knobWrapper.appendChild(knob);
        container.appendChild(label);
        container.appendChild(knobWrapper);
        container.appendChild(valueDisplay);

        // No value yet. The device is the source of truth for values, so the
        // knob stays in a "pending" state until updateFromDevice() reports one.
        // If the device never reports it (param removed from firmware), it is
        // flagged "missing" instead.
        markKnobUnknown(container, 'pending');

        // Add drag interaction. AT mod knobs additionally reset to 0 on a
        // double-tap/double-click, which setupKnobDrag wires up.
        setupKnobDrag(knob, paramName, config, isAtMod, valueDisplay,
                      isAtMod ? () => {
                          markKnobValue(container, paramName, 0, true);
                          scheduleParamSend(paramName, 0);
                      } : null);

        return container;
    }
    
    /**
     * Create a dual-knob parameter control (primary + AT mod)
     * @param {string} paramName Parameter name
     * @param {Object} config Parameter configuration
     * @returns {HTMLElement} Container with both knobs
     */
    function createDualKnob(paramName, config) {
        const wrapper = document.createElement('div');
        wrapper.className = 'dual-knob-wrapper';

        // Primary knob (skipped for atMod-only params like pitch bend)
        if (!config.atModOnly) {
            wrapper.appendChild(createKnob(paramName, config, false));
        }

        // AT mod knob (if applicable). Pass the base config so the knob can read
        // its atMod range/unit; createKnob picks the right label.
        if (config.hasAtMod) {
            const atModParam = paramName + '_atMod';
            wrapper.appendChild(createKnob(atModParam, config, true));
        }

        return wrapper;
    }
    
    /**
     * Setup drag interaction for a knob
     *
     * Built on Pointer Events, which deliver mouse, touch and pen through one
     * code path. Two things make this work on a touch screen where a
     * mouse/touch event pair would not:
     *
     * - Pointer capture. Once a drag starts, every move for that pointer is
     *   routed to this knob even after the finger leaves the small dial, so the
     *   drag doesn't die the moment it strays sideways. It also means the
     *   listeners live on the knob rather than the document, so two fingers on
     *   two knobs each drive their own knob (the old code always read
     *   touches[0]).
     * - `touch-action: none` on .knob (see the stylesheet). Without it the
     *   browser claims the vertical drag for page scrolling and stops sending
     *   move events at all.
     *
     * @param {Function|null} onDoubleTap Called on a double-tap/double-click
     *   (used to zero AT mod knobs). `dblclick` alone never fires for touch.
     */
    function setupKnobDrag(knob, paramName, config, isAtMod, valueDisplay, onDoubleTap) {
        let activePointerId = null;
        let startY = 0;         // where the press landed, for tap-vs-drag
        let maxTravel = 0;      // furthest the pointer got from startY, in px
        let dragEngaged = false;
        let anchorY = 0;        // where the drag proper began
        let anchorValue = 0;
        let lastTapTime = 0;

        const handleStart = (e) => {
            // One drag at a time per knob; ignore a second finger landing on it.
            if (activePointerId !== null) return;
            // Only the primary mouse button drags (touch/pen report button 0).
            if (e.button > 0) return;
            // Can't drag a control with no known value (pending, or removed from
            // firmware). It becomes draggable once the device reports a value.
            if (!(paramName in currentParams)) return;

            activePointerId = e.pointerId;
            startY = e.clientY;
            maxTravel = 0;
            dragEngaged = false;
            knob.classList.add('active');
            if (knob.setPointerCapture) {
                knob.setPointerCapture(e.pointerId);
            }
            e.preventDefault();
        };

        const handleMove = (e) => {
            if (e.pointerId !== activePointerId) return;

            maxTravel = Math.max(maxTravel, Math.abs(startY - e.clientY));

            if (!dragEngaged) {
                // Still within the slop, so this could yet be a tap. Moving the
                // value now would make every tap nudge the knob, which on a
                // touch screen happens constantly.
                if (maxTravel <= TAP_SLOP_PX) return;
                // The drag starts here. Anchor it at the edge of the slop zone,
                // not at the press origin (which would jump the value by the slop
                // distance) and not at the current position (which would discard
                // the travel so far, stalling a drag whose move events are
                // coarse). From here the knob tracks the finger 1:1.
                dragEngaged = true;
                anchorY = startY + (e.clientY < startY ? -TAP_SLOP_PX : TAP_SLOP_PX);
                anchorValue = currentParams[paramName];
            }

            const deltaY = anchorY - e.clientY; // Inverted: up increases value
            const startValue = anchorValue;

            // Calculate sensitivity based on range
            const atRange = isAtMod ? atModRange(config) : null;
            const range = isAtMod ? (atRange.max - atRange.min) : (config.max - config.min);
            const sensitivity = range / DRAG_RANGE_PX;

            let newValue;
            if (config.log && !isAtMod) {
                // Logarithmic scaling for frequency-like parameters
                const logMin = Math.log(config.min);
                const logMax = Math.log(config.max);
                const logStart = Math.log(startValue);
                const logDelta = (logMax - logMin) * (deltaY / DRAG_RANGE_PX);
                newValue = Math.exp(logStart + logDelta);
            } else {
                newValue = startValue + deltaY * sensitivity;
            }

            // Clamp to range
            const min = isAtMod ? atRange.min : config.min;
            const max = isAtMod ? atRange.max : config.max;
            newValue = Math.max(min, Math.min(max, newValue));

            // Snap to discrete detents if requested. Pitch bend uses whole
            // semitones so every target note is reachable; without this the knob
            // is continuous and lands on values like 5.87 instead of 6.
            const step = isAtMod ? config.atModStep : undefined;
            if (step) {
                newValue = Math.round(newValue / step) * step;
            }

            // Update display and send
            currentParams[paramName] = newValue;
            setKnobRotation(knob, valueToRotation(paramName, newValue, isAtMod));
            valueDisplay.textContent = formatValue(paramName, newValue, isAtMod);

            scheduleParamSend(paramName, newValue);
            e.preventDefault();
        };

        const handleEnd = (e) => {
            if (e.pointerId !== activePointerId) return;
            activePointerId = null;
            knob.classList.remove('active');

            if (!onDoubleTap) return;

            // The press never became a drag, so treat it as a tap and see if it
            // completes a pair. Timestamps come from the event so a stalled main
            // thread can't turn two slow taps into a double.
            if (!dragEngaged) {
                const now = e.timeStamp;
                if (now - lastTapTime <= DOUBLE_TAP_MS) {
                    onDoubleTap();
                    lastTapTime = 0;   // don't let a third tap re-trigger
                } else {
                    lastTapTime = now;
                }
            } else {
                lastTapTime = 0;
            }
        };

        // An interrupted gesture is neither a drag nor a tap, so it must not
        // count toward a double-tap.
        const handleCancel = (e) => {
            if (e.pointerId !== activePointerId) return;
            activePointerId = null;
            dragEngaged = false;
            lastTapTime = 0;
            knob.classList.remove('active');
        };

        knob.addEventListener('pointerdown', handleStart);
        knob.addEventListener('pointermove', handleMove);
        knob.addEventListener('pointerup', handleEnd);
        knob.addEventListener('pointercancel', handleCancel);
        // Long-press on a touch screen otherwise pops the browser's context menu
        // mid-drag.
        knob.addEventListener('contextmenu', (e) => e.preventDefault());
    }
    
    /**
     * Convert value to knob rotation angle (degrees)
     */
    function valueToRotation(paramName, value, isAtMod) {
        const config = PARAMS[paramName.replace('_atMod', '')];

        if (isAtMod) {
            // AT mod sweeps its range across -135..135 degrees (0 centered when
            // the range is symmetric, e.g. [-1,1] or [-12,12]).
            const r = atModRange(config);
            const normalized = (value - r.min) / (r.max - r.min);
            return (normalized * 270) - 135;
        }
        
        let normalized;
        if (config && config.log) {
            const logMin = Math.log(config.min);
            const logMax = Math.log(config.max);
            normalized = (Math.log(value) - logMin) / (logMax - logMin);
        } else if (config) {
            normalized = (value - config.min) / (config.max - config.min);
        } else {
            normalized = value;
        }
        
        // Map 0-1 to -135 to 135 degrees
        return (normalized * 270) - 135;
    }
    
    /**
     * Set knob rotation via CSS transform
     */
    function setKnobRotation(knob, degrees) {
        knob.style.transform = `rotate(${degrees}deg)`;
    }
    
    /**
     * Format value for display
     */
    function formatValue(paramName, value, isAtMod) {
        if (isAtMod) {
            const base = PARAMS[paramName.replace('_atMod', '')] || {};
            const unit = base.atModUnit || '';
            // Whole-number detents (e.g. semitones) read cleaner without decimals.
            const digits = (base.atModStep && Number.isInteger(base.atModStep)) ? 0 : 2;
            if (value === 0) return '0' + unit;
            return (value > 0 ? '+' : '') + value.toFixed(digits) + unit;
        }
        
        const config = PARAMS[paramName];
        if (!config) return value.toFixed(2);
        
        if (config.unit === 'Hz' && value >= 1000) {
            return (value / 1000).toFixed(1) + 'k';
        } else if (config.unit === 's') {
            if (value < 0.1) return (value * 1000).toFixed(0) + 'ms';
            return value.toFixed(2) + 's';
        } else if (config.max === 1 && config.min === 0) {
            return (value * 100).toFixed(0) + '%';
        }
        
        return value.toFixed(2) + (config.unit || '');
    }
    
    /**
     * Schedule parameter send with debouncing
     */
    function scheduleParamSend(paramName, value) {
        if (sendTimer) {
            clearTimeout(sendTimer);
        }
        
        sendTimer = setTimeout(() => {
            if (window.serialManager && window.serialManager.isConnected()) {
                window.serialManager.setParam(paramName, value);
            }
        }, SEND_DELAY);
    }
    
    /**
     * Apply a known value to a knob, clearing any pending/missing state.
     */
    function markKnobValue(container, paramName, value, isAtMod) {
        container.classList.remove('no-value', 'missing');
        currentParams[paramName] = value;
        const knob = container.querySelector('.knob');
        const valueDisplay = container.querySelector('.knob-value');
        if (knob) setKnobRotation(knob, valueToRotation(paramName, value, isAtMod));
        if (valueDisplay) valueDisplay.textContent = formatValue(paramName, value, isAtMod);
    }

    /**
     * Put a knob into a no-value state.
     * @param {HTMLElement} container Knob container
     * @param {string} state 'pending' (awaiting first report) or 'missing'
     *   (device reported params but not this one - likely removed from firmware)
     */
    function markKnobUnknown(container, state) {
        container.classList.add('no-value');
        container.classList.toggle('missing', state === 'missing');
        const knob = container.querySelector('.knob');
        const valueDisplay = container.querySelector('.knob-value');
        if (knob) setKnobRotation(knob, -135); // neutral resting position
        if (valueDisplay) valueDisplay.textContent = (state === 'missing') ? 'n/a' : '—';
        // No value -> not draggable (handleStart checks currentParams membership)
        delete currentParams[container.dataset.param];
    }

    /**
     * Update UI from received parameters
     * @param {Object} params Parameter object from device
     */
    function updateFromDevice(params) {
        // Names the device actually reported (the message is the full param set)
        const received = new Set(
            Object.keys(params).filter(k => k !== 'type')
        );

        for (const key of received) {
            const container = document.querySelector(`.knob-container[data-param="${key}"]`);
            if (!container) continue; // device sent a param we have no knob for
            markKnobValue(container, key, params[key], key.endsWith('_atMod'));
        }

        // Any control the device did NOT report is missing from the firmware.
        // Flag it so a removed/renamed param is visible rather than silently stale.
        document.querySelectorAll('.knob-container').forEach(container => {
            const name = container.dataset.param;
            if (name && !received.has(name)) {
                markKnobUnknown(container, 'missing');
            }
        });
    }
    
    /**
     * Create a parameter group section
     * @param {string} title Section title
     * @param {string[]} paramNames Parameter names in this group
     * @returns {HTMLElement} Section element
     */
    function createParamGroup(title, paramNames) {
        const section = document.createElement('div');
        section.className = 'param-group';
        
        const header = document.createElement('h3');
        header.textContent = title;
        section.appendChild(header);
        
        const knobsRow = document.createElement('div');
        knobsRow.className = 'knobs-row';
        
        for (const paramName of paramNames) {
            const config = PARAMS[paramName];
            if (config) {
                knobsRow.appendChild(createDualKnob(paramName, config));
            }
        }
        
        section.appendChild(knobsRow);
        return section;
    }
    
    /**
     * Initialize the control panel
     * @param {HTMLElement} container Container element
     */
    function init(container) {
        container.innerHTML = '';
        
        // Create parameter groups
        const groups = [
            { title: 'Aftertouch', params: ['aftertouchMinRatio', 'aftertouchMaxRatio'] },
            { title: 'Oscillator', params: ['waveformShape'] },
            // Pitch bend sits with the sound source rather than with Vibrato:
            // it shifts the oscillator's pitch directly under pressure, with no
            // LFO involved.
            { title: 'Pitch Bend', params: ['pitchBend'] },
            { title: 'Filter', params: ['baseCutoff', 'filterQ'] },
            { title: 'Filter Envelope', params: ['filterEnvAmount', 'filterEnvAttack', 'filterEnvDecay', 'filterEnvSustain', 'filterEnvRelease'] },
            // Level joins the amp envelope as the VCA section: both shape the
            // same gain stage.
            { title: 'Amplifier', params: ['level', 'ampEnvAttack', 'ampEnvDecay', 'ampEnvSustain', 'ampEnvRelease'] },
            { title: 'Vibrato', params: ['vibratoRate', 'vibratoDepth'] },
            { title: 'Tremolo', params: ['tremoloRate', 'tremoloDepth'] },
        ];
        
        for (const group of groups) {
            container.appendChild(createParamGroup(group.title, group.params));
        }
        
        // Request current params from device
        if (window.serialManager && window.serialManager.isConnected()) {
            window.serialManager.getParams();
        }
    }
    
    // Public API
    return {
        PARAMS: PARAMS,
        init: init,
        updateFromDevice: updateFromDevice,
        createKnob: createKnob,
        createDualKnob: createDualKnob
    };
})();
