# Pressence Embedded MIDI Synthesizer

Pressence is a multi-platform software synthesizer with an emphasis on continuous
pressure sensing (aka MIDI polyphonic aftertouch).

High-level architecture, from input side to output side:

* Optional hardware key scanning
  * → Outputs MIDI messages (note on, continuous note pressure, note off)
* MIDI out (from key scanner) and in (to MIDI state machine) is possible at this layer
  * For example, the Linux build doesn't include a key scanner, but it feeds ALSA MIDI to the synth
  * Similarly, a low-power microcontroller could scan keys and transmit MIDI, omitting synthesis entirely
* MIDI parser/state machine with pluggable note-to-voice allocation algorithms
  * → Interfaces with the synth voice allocator; triggers and releases notes
* Synthesizer abstraction & toolkit
  * Currently one implementation: subtractive synthesizer with morphable Sawtooth/Triangle/Square waveforms
  * Reusable biquad filter, composable with envelope generator
  * Reusable ADSR envelope generator
  * Voice mixing with various clipping/distortion algorithms
  * There's room to implement other types of synth engines without worrying about note-to-voice allocation, mixdown, etc.
* Audio output
  * The ESP32 implementation drives an external DAC via I2S
  * The Linux implementation outputs to ALSA (probably should update to Pipewire or JACK)

## Code organization

(See also: [CODING_STANDARDS.md] for more detailed rules about use of macros, error handling, and so on.)

Each file fits into one of three broad categories:

1. Platform-Independent code - compiles and runs on any system (desktop, microcontroller, Linux, Mac, etc.)
2. Abstractions/Interface Definitions - defines behaviour that must be implemented using hardware- or OS-specific code
3. Non-portable Code - implements the abstractions so the platform-neutral code can make noise!

**Interface Definitions**
* *lib/features*: Type definitions  ???
* *lib/platform*: ??

**Platform-Independent Implementaion code**
* *lib/synth*: Platform-independent polyphonic subtractive synthesizer.
* *lib/midi*: Platform-independent MIDI processor. Drives the synth voices.
* *lib/nlohmann*: Third-party JSON input and output library (for storing synth presets).

**Platform-Specific Implementaion code**
* *lib/linux*: Linux-specific implementations of the generic interfaces (ALSA-based MIDI input and audio sink).
* *lib/esp*: ESP32-specific implementations of the generic interfaces (audio sink, preset storage, capacitive key scanning).
* *src/*: Main entry points. One per target OS/platform.

Example hookup on a Linux build (arrows indicate direction of API calls):

```mermaid
flowchart LR
linux::AlsaMidiIn-->midi::StreamProcessor
midi::StreamProcessor-->midi::SimpleVoiceAllocator
midi::StreamProcessor-->synth::WavetableSynth
synth::WavetableSynth-->linux::AlsaPcmOut
```

Notes:
* `synth::WavetableSynth` implements `midi::Synth`
* `midi::SimpleVoiceAllocator` implements `midi::SynthVoiceAllocator`.
  * The voice allocator owns the instances of `synth::WavetableSynth` and manages their lifecycles based on which note triggered them
  * `midi::SimpleVoiceAllocator` uses a factory function (supplied to its constructor) to create the Synth instances during construction
* Dynamic memory allocation is confined to setup and tear-down time; no heap allocations happen while the synths are running

## Build and Run

### Native Linux executable

```bash
source ~/.platformio/penv/bin/activate
pio run -e native
.pio/build/native/program
```

### ESP32 DEVKITV1

```bash
source ~/.platformio/penv/bin/activate
pio run -e esp32dev
```

You might get a Python stack trace mentioning IntelHex. The library didn't auto-install for me. You can install it with:

```bash
source ~/.platformio/penv/bin/activate
pip install intelhex
```
