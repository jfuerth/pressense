# Pressence Coding Standards

## Platform Abstraction

This project is designed to run on multiple platforms (Linux, ESP32, etc.) with minimal code duplication. Follow these guidelines to maintain clean platform abstraction:

### 1. Avoid Platform-Specific Preprocessor Directives

Don't use platform names in preprocessor conditionals:
```cpp
#ifndef PLATFORM_ESP32
    // Linux-only code
#endif
```

Instead, use feature-based conditionals:
```cpp
#ifdef FEATURE_PROGRAM_STORAGE
    // Code that requires program storage
#endif
```

### 2. Prefer Platform-Specific Modules (Best Practice)

When functionality differs significantly between platforms, create platform-specific implementations:

**Directory Structure:**
```
lib/
├── features/           # Feature interfaces and implementations
│   ├── program_storage.hpp           # Interface
│   ├── filesystem_program_storage.hpp # Linux implementation
│   └── embedded_program_storage.hpp   # ESP32 implementation
├── linux/             # Linux-only implementations
│   ├── alsa_midi_in.hpp
│   └── linux_audio_sink.hpp
├── esp/               # ESP32-only implementations
│   ├── esp32_midi_in.hpp
│   └── esp32_audio_sink.hpp
└── platform/          # Platform-agnostic code
    └── synth_application.hpp
```

**Benefits:**
- No preprocessor conditionals in most code
- Easier to test and maintain
- Clear separation of concerns
- Compiler only sees relevant code

### 3. Use Feature Flags for Optional Functionality

When creating optional features, define feature flags in `platformio.ini`:

```ini
[env:native]
build_flags = 
    -DFEATURE_CLIPBOARD         # Enable preset copy/paste
    -DFEATURE_CUSTOM_FEATURE    # Your new feature here

[env:esp32dev]
build_flags = 
    # No clipboard (limited memory)
```

**Feature Flag Naming Convention:**
- Prefix: `FEATURE_`
- Name describes the capability, not the platform
- Examples: `FEATURE_CLIPBOARD`, `FEATURE_USB_MIDI`, `FEATURE_WIFI_SYNC`
- **Do not use feature flags for core functionality** - use dependency injection instead

### 4. Implement Features with Dependency Injection

**Core Features:** Some features (like program storage) are assumed to always be available. Each platform must provide an implementation or inject `nullptr` to disable.

**Optional Features:** Use feature flags for truly optional functionality (e.g., clipboard, USB MIDI).

#### Core Feature Example: Program Storage

Every platform provides a program storage implementation or `nullptr`:

**Interface (in `lib/features/`):**
```cpp
class ProgramStorage {
public:
    virtual ~ProgramStorage() = default;
    virtual bool loadProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) = 0;
    virtual bool saveProgram(uint8_t program, midi::SynthVoiceAllocator& allocator) = 0;
};
```

**Implementations (platform-specific):**
```cpp
// lib/features/filesystem_program_storage.hpp - for Linux
class FilesystemProgramStorage : public ProgramStorage { ... };

// lib/features/embedded_program_storage.hpp - for ESP32
class EmbeddedProgramStorage : public ProgramStorage { ... };
```

**Usage in main files (no feature flags needed):**
```cpp
// src/main_linux.cpp
#include <filesystem_program_storage.hpp>

auto storage = std::make_unique<features::FilesystemProgramStorage>();
SynthApplication synth(sampleRate, channels, voices, std::move(storage));

// src/main_esp32.cpp
#include <embedded_program_storage.hpp>

auto storage = std::make_unique<features::EmbeddedProgramStorage>();
SynthApplication synth(sampleRate, channels, voices, std::move(storage));
```

**Application code checks for null:**
```cpp
if (programStorage_) {
    programStorage_->loadProgram(program, allocator);
} else {
    logWarn("No program storage available");
}
```

#### Optional Feature Example: Clipboard

Use feature flags for optional functionality:

```cpp
// src/main_linux.cpp
#ifdef FEATURE_CLIPBOARD
#include <preset_clipboard.hpp>
synth.setClipboard(std::make_unique<features::PresetClipboard>());
#endif

// Application code
#ifdef FEATURE_CLIPBOARD
if (clipboard_) {
    clipboard_->copy(allocator);
}
#endif
```

### 5. Build Configuration

Configure PlatformIO to compile only relevant files:

```ini
[env:esp32dev]
lib_ldf_mode = deep+          # Deep dependency scanning
build_src_filter = 
    +<*>
    -<main_linux.cpp>         # Exclude Linux entry point
    +<main_esp32.cpp>         # Include ESP32 entry point
```

**Library Discovery:**
- PlatformIO's LDF (Library Dependency Finder) automatically includes only the libraries referenced by included source files
- No need to manually filter platform-specific libraries if they're only included by platform-specific source files

### 6. Logging Abstraction

Use the platform-agnostic logging macros defined in `lib/platform/log.hpp`:

```cpp
#include <log.hpp>

logInfo("Message: %s", str);
logWarn("Warning: %d", value);
logError("Error: %s", error);
logDebug("Debug: %f", floatValue);
```

The implementation automatically selects the appropriate logging mechanism:
- ESP32: Uses ESP-IDF's `ESP_LOGx` macros
- Linux: Uses `printf` with formatting

### 7. Example: Adding a New Optional Feature

To add a new optional feature (e.g., USB MIDI):

1. **Create feature interface:**
   ```cpp
   // lib/features/usb_midi.hpp
   class UsbMidiInterface {
   public:
       virtual ~UsbMidiInterface() = default;
       virtual bool isConnected() = 0;
       virtual void send(const uint8_t* data, size_t len) = 0;
   };
   ```

2. **Create platform implementations:**
   ```cpp
   // lib/features/linux_usb_midi.hpp
   class LinuxUsbMidi : public UsbMidiInterface { ... };
   
   // lib/features/esp32_usb_midi.hpp
   class Esp32UsbMidi : public UsbMidiInterface { ... };
   ```

3. **Define feature flag in platformio.ini:**
   ```ini
   [env:native]
   build_flags = -DFEATURE_USB_MIDI
   
   [env:esp32dev]
   build_flags = -DFEATURE_USB_MIDI
   ```

4. **Use in application code:**
   ```cpp
   #ifdef FEATURE_USB_MIDI
   #include <usb_midi.hpp>
   
   // Inject implementation
   std::unique_ptr<UsbMidiInterface> usbMidi;
   #ifdef PLATFORM_ESP32
   usbMidi = std::make_unique<Esp32UsbMidi>();
   #else
   usbMidi = std::make_unique<LinuxUsbMidi>();
   #endif
   #endif
   ```

## Summary

**Order of Preference:**

1. **Platform-specific modules** (separate files/directories) ← Best
2. **Feature flags** with dependency injection
3. **Preprocessor conditionals** (only when absolutely necessary) ← Last resort

**Key Principles:**

- Features, not platforms
- Interfaces, not `#ifdef`
- Dependency injection over conditional compilation
- Let the build system exclude platform-specific code, not preprocessor
