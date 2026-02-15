# ESP-IDF component registration for Pressence synthesizer
# Platform-specific: ESP32 build only (not used by PlatformIO native)

# Only compile main_esp32.cpp for ESP32 build
set(app_sources "main_esp32.cpp")

# Include paths for platform-specific libraries
set(include_dirs 
    "${CMAKE_SOURCE_DIR}/lib/platform"
    "${CMAKE_SOURCE_DIR}/lib/esp32"
    "${CMAKE_SOURCE_DIR}/lib/features"
    "${CMAKE_SOURCE_DIR}/lib/midi"
    "${CMAKE_SOURCE_DIR}/lib/synth"
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES driver
)

