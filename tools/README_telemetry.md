# Pressence Web Control Panel & Telemetry

`tools/pressence_ui.html` is a browser-based tool for controlling the synthesizer and
visualizing capacitive key scanner telemetry in real-time. It talks to the device over
USB serial using the Web Serial API and is built from three modules in `tools/js/`:

- `serial_connection.js` — Web Serial API wrapper for bidirectional communication
- `control_panel.js` — knob components and parameter UI for the synthesizer
- `telemetry_panel.js` — key scan visualization and timing telemetry display

## Features

- **Synth parameter control**: Knobs for oscillator, filter, envelopes, vibrato/tremolo,
  and aftertouch modulation, sent to the device as JSON commands (see `lib/webcontrol`)
- **Program save/load**: Store and recall synth settings in the device's program slots
- **Base note control**: Transpose the keyboard from the browser
- **Real-time key scan visualization**: Vertical bars showing sensor readings for each key
- **Threshold indicators**: Horizontal lines showing the Note ON and Note OFF thresholds
  (reported by the device in the telemetry stream)
- **Active note highlighting**: Visual feedback when keys are pressed
- **Aftertouch display**: Shows polyphonic aftertouch values for active notes
- **Timing telemetry**: Audio processing time visualization
- **Log separation**: Telemetry data is automatically separated from regular log messages

## Usage

### 1. Build and Upload Firmware

```bash
# RP2350 (Waveshare RP2350B)
platformio run --target upload --environment waveshare_2350b

# ESP32
platformio run --target upload --environment esp32dev
```

### 2. Open the Control Panel

Open `tools/pressence_ui.html` in a Chromium-based browser (Chrome, Edge, or Opera).

**Note**: Web Serial API is required and only supported in Chromium-based browsers.

### 3. Connect to Device

1. Click "Connect"
2. Select your device from the dialog
3. The serial connection runs at 115200 baud; telemetry and command responses
   stream as JSON Lines

## How It Works

### Firmware Side

- **MidiKeyboardController** populates a `KeyScanStats` struct with:
  - Raw sensor readings (per key)
  - Baseline values (per key)
  - Reading/baseline ratios (per key)
  - Note states (on/off per key)
  - Aftertouch values (per key)
  - Note ON/OFF threshold constants and calibration state

- The struct is sent through the platform-agnostic `features::TelemetrySink`
  abstraction. Each platform implements it differently:
  - **ESP32** (`esp32::Esp32TelemetrySink`): a dedicated FreeRTOS task on core 0
    serializes to JSON and prints it. A single-slot queue with overwrite semantics
    keeps the audio path non-blocking — old frames are dropped if serial can't keep up.
  - **RP2350** (`rp2350::Rp2350TelemetrySink`): synchronous JSON serialization and
    `printf()` to USB serial.

- Key scan telemetry is off by default; each platform's `main` toggles it with
  `keyboard->setTelemetryEnabled(...)`.

- **WebController** (`lib/webcontrol`) parses incoming JSON command lines from the
  control panel (`setParam`, `getParams`, `saveProgram`, `loadProgram`, `setBaseNote`)
  and applies them to the synth voices, program storage, and keyboard.

- Audio timing telemetry (`lib/features/performance_timer.hpp`) is compile-time gated;
  on RP2350 it is controlled by `ENABLE_AUDIO_TIMING_TELEMETRY` in `main_rp2350.cpp`.

### Web Side

- **Web Serial API** reads from and writes to the serial port at 115200 baud
- **JSON Lines parser** separates telemetry from logs:
  - Lines starting with `{` → parsed as JSON and dispatched by their `type` field
  - Other lines → displayed in the log panel
- Message types: `keyScan` (key scanner telemetry), `timing` (audio processing time),
  `params` (current synth parameters), `cmdResponse` (command acknowledgements)
- **Canvas rendering** shows a real-time bar chart with threshold overlays
- **requestAnimationFrame** ensures smooth updates

## Key Scan Telemetry Format

Each telemetry message is a single JSON object on one line:

```json
{
  "type": "keyScan",
  "keyCount": 8,
  "isCalibrated": true,
  "calibrationCount": 10,
  "noteOnThreshold": 2.0,
  "noteOffThreshold": 1.5,
  "readings": [450, 480, 460, ...],
  "baselines": [400.5, 405.2, 398.7, ...],
  "ratios": [1.12, 1.18, 1.15, ...],
  "noteStates": [false, true, false, ...],
  "aftertouchValues": [0, 45, 0, ...]
}
```

The threshold values are defined as constants in `lib/midi/midi_keyboard_controller.hpp`
and reported in the stream, so the visualizer always reflects what the firmware is using.

## Troubleshooting

### No key scan telemetry appearing

- Check that the device is powered and running
- Verify serial connection in browser DevTools console
- Check that `keyboard->setTelemetryEnabled(true)` is called in the platform's `main`

### Browser doesn't support Web Serial API

- Use Chrome 89+, Edge 89+, or Opera 76+
- Safari and Firefox do not support Web Serial API

### Connection drops during upload

- This is expected when reprogramming the device
- Click "Connect" again once the device has rebooted

## Toggling Key Scan Telemetry

Telemetry output is controlled at runtime in the platform's `main`
(`src/main_rp2350.cpp` or `src/main_esp32.cpp`):

```cpp
keyboard->setTelemetryEnabled(true);   // stream key scan telemetry
keyboard->setTelemetryEnabled(false);  // logs and command responses only
```
