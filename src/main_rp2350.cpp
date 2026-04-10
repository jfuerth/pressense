/**
 * Hello World for RP2350B (Waveshare Core 2350B)
 * 
 * Uses Pico SDK with USB stdio for serial output.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"

int main() {
    // Initialize USB stdio (no separate UART pins required)
    stdio_init_all();
    
    // Wait a moment for USB connection to establish
    sleep_ms(2000);
    
    printf("Hello from RP2350B!\n");
    printf("Waveshare Core 2350B - Pressence Synth Platform\n");
    printf("CPU: RP2350 Cortex-M33 @ %d MHz\n", clock_get_hz(clk_sys) / 1000000);
    
    int counter = 0;
    while (true) {
        printf("Counter: %d\n", counter++);
        sleep_ms(1000);
        
        // Blink the LED if available
        // (LED GPIO typically on pin 25, but can vary by board)
        // For now, just keep it simple
    }
    
    return 0;
}
