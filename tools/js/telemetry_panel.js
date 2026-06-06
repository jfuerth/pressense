/**
 * Pressence Telemetry Panel
 * 
 * Key scan visualization and timing telemetry display.
 * Extracted from telemetry_visualizer.html for modular UI.
 */

window.telemetryPanel = (function() {
    // Visualization state
    let telemetryData = null;
    let timingData = null;
    let canvas = null;
    let ctx = null;
    let persistentMaxValue = 10;
    
    // UI elements (set during init)
    let logsEl = null;
    let infoGridEl = null;
    let timingBarsEl = null;
    let rawJsonEl = null;
    let autoScrollEnabled = true;
    
    // Raw JSON storage
    const rawJsonData = {
        keyScan: null,
        timing: null,
        params: null,
        cmdResponse: null
    };
    
    /**
     * Initialize the telemetry panel
     * @param {HTMLElement} container Container element
     */
    function init(container) {
        // Create structure
        container.innerHTML = `
            <div class="telemetry-row">
                <div class="visualization-panel">
                    <canvas id="telemetryCanvas"></canvas>
                </div>
                <div class="info-side">
                    <div class="info-panel">
                        <h3>Key Scanner</h3>
                        <div class="info-grid" id="infoGrid">
                            <span class="info-label">Status:</span>
                            <span class="info-value">Waiting for data...</span>
                        </div>
                    </div>
                    <div class="timing-panel" id="timingPanel" style="display: none;">
                        <h3>Audio Timing</h3>
                        <div id="timingBars"></div>
                    </div>
                </div>
            </div>
            <div class="log-panel">
                <div class="log-header">
                    <h3>Serial Log</h3>
                    <div class="log-controls">
                        <button id="clearLogsBtn">Clear</button>
                        <label><input type="checkbox" id="autoScrollCheck" checked> Auto-scroll</label>
                    </div>
                </div>
                <div class="logs" id="logs"></div>
            </div>
            <div class="raw-json-panel">
                <div class="raw-json-header">
                    <h3>Raw JSON</h3>
                    <select id="jsonTypeSelect">
                        <option value="keyScan">keyScan</option>
                        <option value="timing">timing</option>
                        <option value="params">params</option>
                        <option value="cmdResponse">cmdResponse</option>
                    </select>
                </div>
                <div class="raw-json" id="rawJson">Waiting for data...</div>
            </div>
        `;
        
        // Get references
        canvas = document.getElementById('telemetryCanvas');
        ctx = canvas.getContext('2d');
        logsEl = document.getElementById('logs');
        infoGridEl = document.getElementById('infoGrid');
        timingBarsEl = document.getElementById('timingBars');
        rawJsonEl = document.getElementById('rawJson');
        
        // Setup event handlers
        document.getElementById('clearLogsBtn').onclick = () => { logsEl.textContent = ''; };
        document.getElementById('autoScrollCheck').onchange = (e) => { autoScrollEnabled = e.target.checked; };
        document.getElementById('jsonTypeSelect').onchange = updateRawJsonDisplay;
        
        // Start animation loop
        requestAnimationFrame(animationLoop);
        
        // Handle resize
        window.addEventListener('resize', updateVisualization);
    }
    
    /**
     * Process a received JSON message
     * @param {Object} data Parsed JSON object
     */
    function processJson(data) {
        if (!data || !data.type) return;
        
        switch (data.type) {
            case 'keyScan':
                telemetryData = data;
                rawJsonData.keyScan = data;
                updateInfo();
                break;
                
            case 'timing':
                timingData = data;
                rawJsonData.timing = data;
                updateTimingDisplay();
                break;
                
            case 'params':
                rawJsonData.params = data;
                // Forward to control panel
                if (window.controlPanel) {
                    window.controlPanel.updateFromDevice(data);
                }
                break;
                
            case 'cmdResponse':
                rawJsonData.cmdResponse = data;
                break;
        }
        
        updateRawJsonDisplay();
    }
    
    /**
     * Add a log message
     * @param {string} message Message text
     * @param {boolean} isError Is this an error message
     */
    function addLog(message, isError = false) {
        const timestamp = new Date().toLocaleTimeString();
        const color = isError ? '#d73a49' : '#d4d4d4';
        logsEl.innerHTML += `<span style="color: #666;">[${timestamp}]</span> <span style="color: ${color};">${escapeHtml(message)}</span>\n`;
        
        if (autoScrollEnabled) {
            logsEl.scrollTop = logsEl.scrollHeight;
        }
    }
    
    /**
     * Update info panel with key scanner data
     */
    function updateInfo() {
        if (!telemetryData || !infoGridEl) return;
        
        const activeNotes = telemetryData.noteStates ? telemetryData.noteStates.filter(s => s).length : 0;
        
        infoGridEl.innerHTML = `
            <span class="info-label">Keys:</span><span class="info-value">${telemetryData.keyCount}</span>
            <span class="info-label">Calibrated:</span><span class="info-value">${telemetryData.isCalibrated ? 'Yes' : 'No'}</span>
            <span class="info-label">Active Notes:</span><span class="info-value">${activeNotes}</span>
            <span class="info-label">ON Threshold:</span><span class="info-value">${telemetryData.noteOnThreshold?.toFixed(2) || '-'}</span>
            <span class="info-label">OFF Threshold:</span><span class="info-value">${telemetryData.noteOffThreshold?.toFixed(2) || '-'}</span>
        `;
    }
    
    /**
     * Update timing display
     */
    function updateTimingDisplay() {
        if (!timingData || !timingData.spans) return;
        
        document.getElementById('timingPanel').style.display = 'block';
        
        const lapCount = timingData.lapCount || 1;
        let totalPerLap = 0;
        for (const span of timingData.spans) {
            totalPerLap += span.total / lapCount;
        }
        
        let html = '<div class="timing-bar">';
        for (const span of timingData.spans) {
            if (span.count === 0) continue;
            const avgPerLap = span.total / lapCount;
            const percent = totalPerLap > 0 ? (avgPerLap / totalPerLap) * 100 : 0;
            const hue = (span.name.charCodeAt(0) * 137) % 360;
            html += `<div class="timing-segment" style="flex: ${percent}; background: hsl(${hue}, 60%, 50%);" title="${span.name}: ${avgPerLap.toFixed(0)}"></div>`;
        }
        html += '</div>';
        
        html += '<div class="timing-legend">';
        for (const span of timingData.spans) {
            if (span.count === 0) continue;
            const avgPerLap = span.total / lapCount;
            const hue = (span.name.charCodeAt(0) * 137) % 360;
            html += `<span class="timing-item"><span class="timing-color" style="background: hsl(${hue}, 60%, 50%);"></span>${span.name}: ${avgPerLap.toFixed(0)}</span>`;
        }
        html += '</div>';
        
        timingBarsEl.innerHTML = html;
    }
    
    /**
     * Update raw JSON display
     */
    function updateRawJsonDisplay() {
        const select = document.getElementById('jsonTypeSelect');
        if (!select || !rawJsonEl) return;
        
        const data = rawJsonData[select.value];
        if (data) {
            rawJsonEl.textContent = JSON.stringify(data, null, 2);
        } else {
            rawJsonEl.textContent = `Waiting for ${select.value} data...`;
        }
    }
    
    /**
     * Update visualization canvas
     */
    function updateVisualization() {
        if (!telemetryData || !canvas || !ctx) return;
        
        const rect = canvas.parentElement.getBoundingClientRect();
        if (rect.width === 0 || rect.height === 0) return;
        
        const dpr = window.devicePixelRatio || 1;
        canvas.width = rect.width * dpr;
        canvas.height = rect.height * dpr;
        ctx.scale(dpr, dpr);
        
        const width = rect.width;
        const height = rect.height;
        const keyCount = telemetryData.keyCount;
        const padding = 30;
        const barWidth = (width - padding * 2) / keyCount;
        const maxHeight = height - padding * 2;
        
        // Clear
        ctx.fillStyle = '#1e1e1e';
        ctx.fillRect(0, 0, width, height);
        
        // Find max for scaling
        let currentMax = 10;
        for (let i = 0; i < keyCount; i++) {
            const reading = telemetryData.readings[i];
            currentMax = Math.max(currentMax, reading);
        }
        if (currentMax > persistentMaxValue) {
            persistentMaxValue = currentMax;
        }
        
        // Draw bars
        for (let i = 0; i < keyCount; i++) {
            const x = padding + i * barWidth;
            const reading = telemetryData.readings[i];
            const baseline = telemetryData.baselines[i];
            const noteActive = telemetryData.noteStates[i];
            
            const barHeight = (reading / persistentMaxValue) * maxHeight;
            const y = padding + maxHeight - barHeight;
            
            // Background highlight if active
            if (noteActive) {
                ctx.fillStyle = 'rgba(78, 201, 176, 0.2)';
                ctx.fillRect(x, padding, barWidth, maxHeight);
            }
            
            // Bar
            ctx.fillStyle = noteActive ? '#4ec9b0' : '#569cd6';
            ctx.fillRect(x + 2, y, barWidth - 4, barHeight);
            
            // Baseline line
            if (baseline > 0) {
                const baselineY = padding + maxHeight - (baseline / persistentMaxValue) * maxHeight;
                ctx.strokeStyle = '#ce9178';
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(x, baselineY);
                ctx.lineTo(x + barWidth, baselineY);
                ctx.stroke();
            }
            
            // Label
            ctx.fillStyle = '#888';
            ctx.font = '10px monospace';
            ctx.textAlign = 'center';
            ctx.fillText(i.toString(), x + barWidth / 2, height - 10);
        }
    }
    
    /**
     * Animation loop
     */
    function animationLoop() {
        updateVisualization();
        requestAnimationFrame(animationLoop);
    }
    
    /**
     * Escape HTML special characters
     */
    function escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
    
    // Public API
    return {
        init: init,
        processJson: processJson,
        addLog: addLog
    };
})();
