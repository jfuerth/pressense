# Pressence Coding Standards

## Platform Abstraction

This project is designed to run on multiple platforms (Linux, ESP32, RP2350, etc.) with minimal code duplication. Follow these guidelines to maintain clean platform abstraction:

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
├── features/           # Abstract interfaces only
│   ├── program_storage.hpp           # Interface
│   └── clipboard.hpp                 # Interface
├── linux/             # All Linux implementations
│   ├── alsa_midi_in.hpp
│   ├── alsa_audio_sink.hpp
│   ├── filesystem_program_storage.hpp  # Implements features::ProgramStorage
│   └── preset_clipboard.hpp            # Implements features::Clipboard
├── esp32/             # All ESP32 implementations
│   ├── i2s_audio_sink.hpp
│   ├── capacitive_scanner.hpp
│   └── embedded_program_storage.hpp    # Implements features::ProgramStorage
└── platform/          # Platform-agnostic code
    └── synth_application.hpp
```

**Benefits:**
- No preprocessor conditionals in most code
- Easier to test and maintain
- Clear separation of concerns
- Compiler only sees relevant code
- Platform variants are naturally grouped together
- Adding new platforms is straightforward

**Key principle:** Interfaces live in `lib/features/`, implementations live in `lib/<platform>/`. This makes it clear what each platform provides and easy to add new platforms without scattering code across the codebase.

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

**Implementations (in platform directories):**
```cpp
// lib/linux/filesystem_program_storage.hpp
class FilesystemProgramStorage : public features::ProgramStorage { ... };

// lib/esp32/embedded_program_storage.hpp
class EmbeddedProgramStorage : public features::ProgramStorage { ... };
```

**Usage in main files (no feature flags needed):**
```cpp
// src/main_linux.cpp
#include <filesystem_program_storage.hpp>

auto storage = std::make_unique<linux::FilesystemProgramStorage>();
SynthApplication synth(sampleRate, channels, voices, std::move(storage));

// src/main_esp32.cpp
#include <embedded_program_storage.hpp>

auto storage = std::make_unique<esp32::EmbeddedProgramStorage>();
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
synth.setClipboard(std::make_unique<linux::PresetClipboard>());
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
   // lib/linux/usb_midi.hpp
   class LinuxUsbMidi : public features::UsbMidiInterface { ... };
   
   // lib/esp32/usb_midi.hpp
   class Esp32UsbMidi : public features::UsbMidiInterface { ... };
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
   
   // Inject platform-specific implementation
   auto usbMidi = std::make_unique<linux::UsbMidi>();  // or esp32::UsbMidi
   #endif
   ```

## Naming Conventions

Follow these naming conventions throughout the codebase:

### Header Guards

Use `#pragma once` for all header files:

```cpp
#pragma once

// Header content
```

**Do not use** traditional `#ifndef`/`#define`/`#endif` include guards.

### Identifiers

- **Classes, Structs, Enums**: PascalCase
  ```cpp
  class SynthVoiceAllocator { };
  struct TimingStats { };
  enum class FilterMode { };
  ```

- **Functions and Methods**: camelCase
  ```cpp
  void processSample(float sample);
  float nextSample();
  bool isActive() const;
  ```

- **Member Variables**: camelCase with trailing underscore
  ```cpp
  class MyClass {
  private:
      float sampleRate_;
      int maxVoices_;
      std::vector<Voice> voices_;
  };
  ```

- **Local Variables and Parameters**: camelCase (no underscore)
  ```cpp
  void process(float inputSample) {
      float outputLevel = 0.5f;
      int voiceCount = getVoiceCount();
  }
  ```

- **Constants**: SCREAMING_SNAKE_CASE
  ```cpp
  static constexpr int MAX_VOICES = 8;
  static constexpr float DEFAULT_SAMPLE_RATE = 48000.0f;
  const int BUFFER_SIZE = 256;
  ```

- **Namespaces**: lowercase with underscores (if needed)
  ```cpp
  namespace midi { }
  namespace platform { }
  ```

### No Hungarian Notation

Do not use prefixes to indicate type or scope:

**Avoid:**
```cpp
static std::unique_ptr<SynthApplication> gSynth;  // 'g' prefix
int iCount;                                        // 'i' prefix
float fLevel;                                      // 'f' prefix
```

**Prefer:**
```cpp
static std::unique_ptr<SynthApplication> synth;
int count;
float level;
```

The type system and modern IDEs provide sufficient type information. Let the code speak for itself.

## Platform Isolation

### Keep Platform Code Separate

**Golden Rule:** Adding or removing platform support should involve adding or removing entire directories or files, not editing individual shared files.

When implementing platform-specific functionality:

1. **Put implementation in platform directories:**
   ```
   lib/esp32/esp32_timing.hpp      # ESP32-specific implementation
   lib/linux/linux_timing.hpp      # Linux-specific implementation
   lib/rp2350/rp2350_timing.hpp    # Future platform - just add file
   ```

2. **Use minimal forwarding headers in lib/platform/:**
   ```cpp
   // lib/platform/timing.hpp - ONLY includes + type alias
   #pragma once
   
   #if defined(ESP_PLATFORM)
   #include <esp32_timing.hpp>
   namespace platform {
       using PlatformTimer = esp32::Esp32CycleTimer;
   }
   #else
   #include <linux_timing.hpp>
   namespace platform {
       using PlatformTimer = linux::LinuxTscTimer;
   }
   #endif
   ```

3. **Benefits of this approach:**
   - Adding RP2350 support = add `lib/rp2350/` directory
   - Removing ESP32 support = delete `lib/esp32/` directory
   - No surgery to individual files
   - Clear separation of platform concerns
   - Build system automatically includes only relevant files

**Anti-pattern - Avoid This:**
```cpp
// DON'T mix multiple platform implementations in one file
#if defined(ESP_PLATFORM)
    // 50 lines of ESP32 code
#elif defined(__x86_64__)
    // 50 lines of Linux code
#elif defined(RP2350)
    // 50 lines of RP2350 code
#endif
```

This anti-pattern makes the file grow with each platform, creates merge conflicts, and makes it hard to cleanly add/remove platforms.

## Summary

**Order of Preference:**

1. **Platform-specific modules** (separate files/directories) ← Best
2. **Feature flags** with dependency injection
3. **Preprocessor conditionals** (only when absolutely necessary) ← Last resort

**Key Principles:**

- Features, not platforms
- Interfaces, not `#ifdef`
- Dependency injection over conditional compilation
- Group by platform, not by feature

**Where to Put New Code:**

- **Platform-independent DSP/synthesis algorithms** → `lib/synth/`
- **Platform-independent MIDI handling** → `lib/midi/`
- **Platform-agnostic application logic** → `lib/platform/`
- **Abstract feature interfaces** → `lib/features/`
- **Linux-specific implementations** → `lib/linux/`
- **ESP32-specific implementations** → `lib/esp32/`
- **New platform implementations** → Create `lib/<platform_name>/`
- **Entry points** → `src/main_<platform>.cpp`
- Let the build system exclude platform-specific code, not preprocessor
