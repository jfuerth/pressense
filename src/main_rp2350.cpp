/**
 * Pressence Synth - RP2350B Platform Entry Point
 * 
 * Initial implementation: PIO-based capacitive key scanner with telemetry
 */

#include <stdio.h>
#include <cstdint>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

// Platform-specific implementations
#include <pio_capacitive_scanner.hpp>
#include <rp2350_telemetry_sink.hpp>

// Platform-agnostic MIDI logic
#include <midi_keyboard_controller.hpp>

// Configuration: Number of keys to scan
static constexpr uint8_t FIRST_KEY_PIN = 0;  // First GPIO pin for keys
static constexpr uint8_t NUM_KEYS = 32;       // Number of keys to scan

// Scanner type with compile-time configuration
using Scanner = rp2350::PioCapacitiveScanner<FIRST_KEY_PIN, NUM_KEYS>;

// MIDI controller type with compile-time configuration
using MidiController = midi::MidiKeyboardController<NUM_KEYS>;

// Global instances
static Scanner* scanner = nullptr;
static MidiController* keyboard = nullptr;

/**
 * @brief Dummy MIDI callback (not used yet - will add USB MIDI later)
 */
void midiCallback(uint8_t midiByte) {
    // TODO: Implement USB MIDI output
    (void)midiByte;
}

/**
 * @brief Main scan loop (runs on core 1)
 */
void core1_scan_loop() {
    printf("Core 1: Scan loop started\n");
    
    while (true) {
        // Trigger a scan
        scanner->startScan();
        
        // Wait for scan to complete
        scanner->waitForScanComplete();
        
        // Process scan results and generate telemetry
        keyboard->processScan();
        
        // Scan at ~100 Hz
        sleep_ms(10);
    }
}

int main() {
    // Initialize USB stdio (will wait for connection)
    stdio_init_all();
    
    // Wait for USB connection to establish
    sleep_ms(500);
    
    printf("\n");
    printf("========================================\n");
    printf("   Pressence Synth - RP2350B Platform  \n");
    printf("========================================\n");
    printf("Board: Waveshare Core 2350B\n");
    printf("CPU: RP2350 Cortex-M33 @ %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
    printf("Phase: Key Scanner + Telemetry\n");
    printf("========================================\n\n");
    
    // Initialize key scanner
    printf("Initializing PIO capacitive key scanner...\n");
    scanner = new Scanner();
    
    if (!scanner) {
        printf("ERROR: Failed to create scanner!\n");
        return 1;
    }
    
    printf("Scanner initialized with %d keys\n", scanner->getKeyCount());
    
    // Initialize MIDI keyboard controller with telemetry
    printf("Initializing MIDI keyboard controller...\n");
    auto telemetrySink = std::make_unique<rp2350::Rp2350TelemetrySink<midi::KeyScanStats<NUM_KEYS>>>();
    keyboard = new MidiController(
        *scanner,
        midiCallback,
        std::move(telemetrySink),
        60,  // Base note C4
        64   // Fixed velocity
    );
    
    if (!keyboard) {
        printf("ERROR: Failed to create keyboard controller!\n");
        return 1;
    }
    
    // Enable telemetry output
    keyboard->setTelemetryEnabled(true);
    
    printf("Keyboard controller initialized\n");
    printf("Calibrating... (this takes a few seconds)\n\n");
    
    // Launch scan loop on core 1
    printf("Launching scan loop on core 1...\n");
    multicore_launch_core1(core1_scan_loop);
    
    printf("Core 0: Idle loop started\n");
    printf("========================================\n\n");
    
    // Core 0 idle loop
    while (true) {
        tight_loop_contents();
    }
    
    return 0;
}
