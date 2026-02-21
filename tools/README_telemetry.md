# Pressence Telemetry Visualizer

A standalone web-based debugging tool for visualizing capacitive key scanner telemetry in real-time.

## Features

- **Real-time visualization**: 8 vertical bars showing sensor readings for each key
- **Threshold indicators**: Horizontal lines showing Note ON (1.20×) and Note OFF (1.10×) thresholds
- **Active note highlighting**: Visual feedback when keys are pressed
- **Aftertouch display**: Shows polyphonic aftertouch values for active notes
- **Log separation**: Automatically separates telemetry data from regular log messages
- **Auto-reconnect**: Handles device reprogramming gracefully

## Usage

### 1. Build and Upload Firmware

The telemetry feature is automatically enabled in the ESP32 firmware. Build and upload:

```bash
platformio run --target upload --environment esp32dev
```

### 2. Open the Visualizer

Simply open `tools/telemetry_visualizer.html` in a Chromium-based browser (Chrome, Edge, or Opera).

**Note**: Web Serial API is required and only supported in Chromium-based browsers.

### 3. Connect to Device

1. Click "Connect to Serial Port"
2. Select your ESP32 device from the dialog
3. Telemetry data will start streaming immediately

## How It Works

### Firmware Side

- **MidiKeyboardController** populates a `TelemetryData` struct with:
  - Raw sensor readings (per key)
  - Baseline values (per key)
  - Reading/baseline ratios (per key)
  - Note states (on/off per key)
  - Aftertouch values (per key)
  - Threshold constants

- A dedicated **FreeRTOS task** (`telemetry`) serializes this data to JSON and outputs it via printf()

- A **single-slot queue** with overwrite semantics ensures:
  - Non-blocking operation (audio thread never waits)
  - Low latency (always the most recent data)
  - Automatic backpressure (old frames dropped if serial can't keep up)

### Web Side

- **Web Serial API** reads from the serial port at 115200 baud
- **JSON Lines parser** separates telemetry from logs:
  - Lines starting with `{` → parsed as JSON telemetry → visualization
  - Other lines → displayed in log panel
- **Canvas rendering** shows real-time bar chart with threshold overlays
- **requestAnimationFrame** ensures smooth 60 FPS updates

## Telemetry Data Format

Each telemetry message is a single JSON object on one line:

```json
{
  "keyCount": 8,
  "isCalibrated": true,
  "calibrationCount": 100,
  "noteOnThreshold": 1.20,
  "noteOffThreshold": 1.10,
  "readings": [450, 480, 460, ...],
  "baselines": [400.5, 405.2, 398.7, ...],
  "ratios": [1.12, 1.18, 1.15, ...],
  "noteStates": [false, true, false, ...],
  "aftertouchValues": [0, 45, 0, ...]
}
```

## Troubleshooting

### No telemetry appearing

- Check that the ESP32 is powered and running
- Verify serial connection in browser DevTools console
- Ensure firmware was built with telemetry enabled

### Browser doesn't support Web Serial API

- Use Chrome 89+, Edge 89+, or Opera 76+
- Safari and Firefox do not support Web Serial API

### Connection drops during upload

- This is expected when reprogramming the device
- The visualizer will show "Waiting for device..." and auto-reconnect after ~2 seconds

## Disabling Telemetry

To disable telemetry output at runtime, modify `main_esp32.cpp`:

```cpp
// Change this line:
gKeyboard->setTelemetryEnabled(true);

// To:
gKeyboard->setTelemetryEnabled(false);
```

Or remove the telemetry task creation entirely.
