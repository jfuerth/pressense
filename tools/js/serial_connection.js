/**
 * Pressence Serial Connection Manager
 * 
 * Provides Web Serial API wrapper for bidirectional communication
 * with the Pressence synthesizer.
 */

window.serialManager = (function() {
    let port = null;
    let reader = null;
    let writer = null;
    let keepReading = false;
    let lineBuffer = '';
    
    // Callbacks for received data
    let onLineReceived = null;
    let onStatusChange = null;
    
    /**
     * Check if Web Serial API is available
     */
    function isSupported() {
        return 'serial' in navigator;
    }
    
    /**
     * Check if currently connected
     */
    function isConnected() {
        return port !== null && keepReading;
    }
    
    /**
     * Connect to serial port
     * @returns {Promise<boolean>} true if connected successfully
     */
    async function connect() {
        if (!isSupported()) {
            console.error('Web Serial API not supported');
            return false;
        }
        
        try {
            port = await navigator.serial.requestPort();
            await port.open({ baudRate: 115200 });
            
            writer = port.writable.getWriter();
            
            keepReading = true;
            readLoop();
            
            if (onStatusChange) {
                onStatusChange('connected');
            }
            
            return true;
        } catch (error) {
            console.error('Failed to connect:', error);
            port = null;
            if (onStatusChange) {
                onStatusChange('error', error.message);
            }
            return false;
        }
    }
    
    /**
     * Disconnect from serial port
     */
    async function disconnect() {
        keepReading = false;
        
        if (reader) {
            await reader.cancel();
            reader = null;
        }
        
        if (writer) {
            await writer.close();
            writer = null;
        }
        
        if (port) {
            await port.close();
            port = null;
        }
        
        if (onStatusChange) {
            onStatusChange('disconnected');
        }
    }
    
    /**
     * Send a command to the device
     * @param {Object} cmd Command object to send as JSON
     */
    async function send(cmd) {
        if (!writer) {
            console.error('Not connected');
            return false;
        }
        
        try {
            const json = JSON.stringify(cmd) + '\n';
            const encoder = new TextEncoder();
            await writer.write(encoder.encode(json));
            return true;
        } catch (error) {
            console.error('Failed to send:', error);
            return false;
        }
    }
    
    /**
     * Send setParam command
     * @param {string} param Parameter name
     * @param {number} value Parameter value
     */
    async function setParam(param, value) {
        return send({ cmd: 'setParam', param: param, value: value });
    }
    
    /**
     * Request current parameters
     */
    async function getParams() {
        return send({ cmd: 'getParams' });
    }
    
    /**
     * Save current settings to program slot
     * @param {number} bank Bank number (0-7)
     * @param {number} program Program number (0-7)
     */
    async function saveProgram(bank, program) {
        return send({ cmd: 'saveProgram', bank: bank, program: program });
    }
    
    /**
     * Load settings from program slot
     * @param {number} bank Bank number (0-7)
     * @param {number} program Program number (0-7)
     */
    async function loadProgram(bank, program) {
        return send({ cmd: 'loadProgram', bank: bank, program: program });
    }
    
    /**
     * Set callback for received lines
     * @param {Function} callback Function(line, parsedJson)
     */
    function setLineCallback(callback) {
        onLineReceived = callback;
    }
    
    /**
     * Set callback for status changes
     * @param {Function} callback Function(status, message)
     */
    function setStatusCallback(callback) {
        onStatusChange = callback;
    }
    
    /**
     * Internal read loop
     */
    async function readLoop() {
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
        reader = textDecoder.readable.getReader();
        
        try {
            while (keepReading) {
                const { value, done } = await reader.read();
                
                if (done) {
                    break;
                }
                
                lineBuffer += value;
                
                // Process complete lines
                let newlineIndex;
                while ((newlineIndex = lineBuffer.indexOf('\n')) !== -1) {
                    const line = lineBuffer.substring(0, newlineIndex).trim();
                    lineBuffer = lineBuffer.substring(newlineIndex + 1);
                    
                    if (line.length > 0) {
                        processLine(line);
                    }
                }
            }
        } catch (error) {
            console.error('Read error:', error);
        }
    }
    
    /**
     * Process a received line
     */
    function processLine(line) {
        let parsed = null;
        
        if (line.startsWith('{')) {
            try {
                parsed = JSON.parse(line);
            } catch (e) {
                console.warn('JSON parse error:', e.message);
            }
        }
        
        if (onLineReceived) {
            onLineReceived(line, parsed);
        }
    }
    
    // Public API
    return {
        isSupported: isSupported,
        isConnected: isConnected,
        connect: connect,
        disconnect: disconnect,
        send: send,
        setParam: setParam,
        getParams: getParams,
        saveProgram: saveProgram,
        loadProgram: loadProgram,
        setLineCallback: setLineCallback,
        setStatusCallback: setStatusCallback
    };
})();
