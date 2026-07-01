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
        waveformShape: { min: 0, max: 1, label: 'Waveform', unit: '', hasAtMod: false },
        
        // Filter
        baseCutoff: { min: 20, max: 20000, label: 'Cutoff', unit: 'Hz', log: true, hasAtMod: true },
        filterQ: { min: 0.1, max: 20, label: 'Resonance', unit: '', hasAtMod: false },
        
        // Filter Envelope
        filterEnvAmount: { min: 0, max: 1, label: 'Env Amount', unit: '', hasAtMod: true },
        filterEnvAttack: { min: 0.001, max: 2, label: 'Attack', unit: 's', log: true, hasAtMod: false },
        filterEnvDecay: { min: 0.01, max: 5, label: 'Decay', unit: 's', log: true, hasAtMod: false },
        filterEnvSustain: { min: 0, max: 1, label: 'Sustain', unit: '', hasAtMod: false },
        filterEnvRelease: { min: 0.01, max: 5, label: 'Release', unit: 's', log: true, hasAtMod: false },
        
        // Amp Envelope
        ampEnvAttack: { min: 0.001, max: 2, label: 'Attack', unit: 's', log: true, hasAtMod: false },
        ampEnvDecay: { min: 0.01, max: 5, label: 'Decay', unit: 's', log: true, hasAtMod: false },
        ampEnvSustain: { min: 0, max: 1, label: 'Sustain', unit: '', hasAtMod: false },
        ampEnvRelease: { min: 0.01, max: 5, label: 'Release', unit: 's', log: true, hasAtMod: false },
        
        // Vibrato
        vibratoRate: { min: 0.1, max: 20, label: 'Rate', unit: 'Hz', hasAtMod: false },
        vibratoDepth: { min: 0, max: 1, label: 'Depth', unit: 'st', hasAtMod: true },
        
        // Tremolo
        tremoloRate: { min: 0.1, max: 20, label: 'Rate', unit: 'Hz', hasAtMod: false },
        tremoloDepth: { min: 0, max: 1, label: 'Depth', unit: '', hasAtMod: true }
    };
    
    // Aftertouch mod params
    const AT_MOD_PARAMS = ['baseCutoff_atMod', 'filterEnvAmount_atMod', 'vibratoDepth_atMod', 'tremoloDepth_atMod'];
    
    // Current parameter values
    let currentParams = {};
    
    // Debounce timer for parameter changes
    let sendTimer = null;
    const SEND_DELAY = 50; // ms
    
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

        // Add drag interaction
        setupKnobDrag(knob, paramName, config, isAtMod, valueDisplay);

        // Double-click to reset AT mod knobs to 0
        if (isAtMod) {
            knob.addEventListener('dblclick', () => {
                markKnobValue(container, paramName, 0, true);
                scheduleParamSend(paramName, 0);
            });
        }
        
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
        
        // Primary knob
        wrapper.appendChild(createKnob(paramName, config, false));
        
        // AT mod knob (if applicable)
        if (config.hasAtMod) {
            const atModParam = paramName + '_atMod';
            const atModConfig = { min: -1, max: 1, label: 'AT Mod', unit: '' };
            wrapper.appendChild(createKnob(atModParam, atModConfig, true));
        }
        
        return wrapper;
    }
    
    /**
     * Setup drag interaction for a knob
     */
    function setupKnobDrag(knob, paramName, config, isAtMod, valueDisplay) {
        let isDragging = false;
        let startY = 0;
        let startValue = 0;
        
        const handleStart = (e) => {
            // Can't drag a control with no known value (pending, or removed from
            // firmware). It becomes draggable once the device reports a value.
            if (!(paramName in currentParams)) return;
            isDragging = true;
            startY = e.clientY || e.touches[0].clientY;
            startValue = currentParams[paramName];
            knob.classList.add('active');
            e.preventDefault();
        };
        
        const handleMove = (e) => {
            if (!isDragging) return;
            
            const currentY = e.clientY || e.touches[0].clientY;
            const deltaY = startY - currentY; // Inverted: up increases value
            
            // Calculate sensitivity based on range
            const range = isAtMod ? 2 : (config.max - config.min);
            const sensitivity = range / 200; // 200 pixels for full range
            
            let newValue;
            if (config.log && !isAtMod) {
                // Logarithmic scaling for frequency-like parameters
                const logMin = Math.log(config.min);
                const logMax = Math.log(config.max);
                const logStart = Math.log(startValue);
                const logDelta = (logMax - logMin) * (deltaY / 200);
                newValue = Math.exp(logStart + logDelta);
            } else {
                newValue = startValue + deltaY * sensitivity;
            }
            
            // Clamp to range
            const min = isAtMod ? -1 : config.min;
            const max = isAtMod ? 1 : config.max;
            newValue = Math.max(min, Math.min(max, newValue));
            
            // Update display and send
            currentParams[paramName] = newValue;
            setKnobRotation(knob, valueToRotation(paramName, newValue, isAtMod));
            valueDisplay.textContent = formatValue(paramName, newValue, isAtMod);
            
            scheduleParamSend(paramName, newValue);
        };
        
        const handleEnd = () => {
            if (isDragging) {
                isDragging = false;
                knob.classList.remove('active');
            }
        };
        
        knob.addEventListener('mousedown', handleStart);
        knob.addEventListener('touchstart', handleStart);
        document.addEventListener('mousemove', handleMove);
        document.addEventListener('touchmove', handleMove);
        document.addEventListener('mouseup', handleEnd);
        document.addEventListener('touchend', handleEnd);
    }
    
    /**
     * Convert value to knob rotation angle (degrees)
     */
    function valueToRotation(paramName, value, isAtMod) {
        const config = PARAMS[paramName.replace('_atMod', '')];
        
        if (isAtMod) {
            // AT mod: -1 to 1 maps to -135 to 135 degrees
            return value * 135;
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
            if (value === 0) return '0';
            return (value > 0 ? '+' : '') + value.toFixed(2);
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
            { title: 'Filter', params: ['baseCutoff', 'filterQ'] },
            { title: 'Filter Envelope', params: ['filterEnvAmount', 'filterEnvAttack', 'filterEnvDecay', 'filterEnvSustain', 'filterEnvRelease'] },
            { title: 'Amp Envelope', params: ['ampEnvAttack', 'ampEnvDecay', 'ampEnvSustain', 'ampEnvRelease'] },
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
