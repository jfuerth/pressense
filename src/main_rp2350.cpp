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

// Platform-specific implementation
#include <pio_capacitive_scanner.hpp>

// Global instances
static rp2350::PioCapacitiveScanner* scanner = nullptr;

/**
 * @brief Print key telemetry to USB serial
 */
void printKeyTelemetry() {
    const uint32_t* rawReadings = scanner->getRawReadings();
    const uint8_t keyCount = scanner->getKeyCount();
    
    printf("Keys: ");
    for (uint8_t i = 0; i < keyCount; i++) {
        // Convert from countdown timer to actual count
        uint32_t count = 0xFFFFFFFF - rawReadings[i];
        printf("K%02d:%6lu  ", i, count);
        
        // New line every 8 keys for readability
        if ((i + 1) % 8 == 0 && i < keyCount - 1) {
            printf("\n      ");
        }
    }
    printf("\n");
}

/**
 * @brief Main telemetry loop (runs on core 1)
 */
void core1_telemetry_loop() {
    printf("Core 1: Telemetry loop started\n");
    
    while (true) {
        // Trigger a scan
        scanner->startScan();
        
        // Wait for scan to complete
        scanner->waitForScanComplete();
        
        // Print telemetry
        printKeyTelemetry();
        
        // Scan at ~10 Hz
        sleep_ms(100);
    }
}

int main() {
    // Initialize USB stdio (will wait for connection)
    stdio_init_all();
    
    // Wait for USB connection to establish
    sleep_ms(2000);
    
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
    scanner = new rp2350::PioCapacitiveScanner();
    
    if (!scanner) {
        printf("ERROR: Failed to create scanner!\n");
        return 1;
    }
    
    printf("Scanner initialized with %d keys\n", scanner->getKeyCount());
    printf("\n");
    
    // Launch telemetry loop on core 1
    printf("Launching telemetry loop on core 1...\n");
    multicore_launch_core1(core1_telemetry_loop);
    
    printf("Core 0: Idle loop started\n");
    printf("========================================\n\n");
    
    // Core 0 idle loop
    while (true) {
        tight_loop_contents();
    }
    
    return 0;
}
