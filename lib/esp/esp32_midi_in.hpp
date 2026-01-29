#pragma once

#ifdef PLATFORM_ESP32

#include <driver/uart.h>
#include <functional>
#include <stdexcept>
#include <log.hpp>

namespace esp32 {

/**
 * @brief ESP32 UART-based MIDI input
 * 
 * Standard MIDI serial configuration:
 * - Baud rate: 31.25 kbaud
 * - Data bits: 8
 * - Stop bits: 1
 * - Parity: None
 * 
 * Pin configuration:
 * - MIDI RX -> GPIO 16 (UART2 RX)
 * 
 * MIDI hardware interface (requires optocoupler):
 * - MIDI IN pin 5 -> 220Ω resistor -> optocoupler anode
 * - MIDI IN pin 2 -> GND
 * - Optocoupler cathode -> MIDI IN pin 4
 * - Optocoupler output -> GPIO 16 (with pullup)
 */
class UartMidiIn {
public:
    UartMidiIn(uart_port_t uartPort = UART_NUM_2, int rxPin = 16)
        : uartPort_(uartPort) {
        
        // MIDI standard baud rate
        const int MIDI_BAUD_RATE = 31250;
        
        // UART configuration for MIDI
        uart_config_t uart_config = {
            .baud_rate = MIDI_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_APB,
            .flags = {
                .backup_before_sleep = 0
            }
        };
        
        // Install UART driver with small ring buffer (MIDI messages are short)
        const int UART_BUFFER_SIZE = 256;
        esp_err_t err = uart_driver_install(uartPort_, UART_BUFFER_SIZE, 0, 0, nullptr, 0);
        if (err != ESP_OK) {
            logError("Failed to install UART driver: %d", err);
            return;
        }
        
        err = uart_param_config(uartPort_, &uart_config);
        if (err != ESP_OK) {
            uart_driver_delete(uartPort_);
            logError("Failed to configure UART: %d", err);
            return;
        }
        
        // Set UART pins (TX not used for MIDI input)
        err = uart_set_pin(uartPort_, UART_PIN_NO_CHANGE, rxPin, 
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            uart_driver_delete(uartPort_);
            logError("Failed to set UART pins: %d", err);
            return;
        }
        
        logInfo("UART MIDI initialized on port %d, RX pin %d", uartPort_, rxPin);
    }
    
    ~UartMidiIn() {
        uart_driver_delete(uartPort_);
    }
    
    // Delete copy constructor and assignment
    UartMidiIn(const UartMidiIn&) = delete;
    UartMidiIn& operator=(const UartMidiIn&) = delete;
    
    /**
     * @brief Read all available MIDI bytes and process them with callback
     * @param callback Function to call for each received byte
     * @return Number of bytes read
     * 
     * Non-blocking: returns immediately if no data is available.
     * Suitable for calling from audio processing loop.
     */
    size_t pollAndRead(std::function<void(uint8_t)> callback) {
        size_t totalBytesRead = 0;
        uint8_t buffer[64];
        
        while (true) {
            int bytesRead = uart_read_bytes(uartPort_, buffer, sizeof(buffer), 0);
            
            if (bytesRead <= 0) {
                // No more data available
                break;
            }
            
            // Process each byte through callback
            for (int i = 0; i < bytesRead; ++i) {
                callback(buffer[i]);
            }
            
            totalBytesRead += bytesRead;
        }
        
        return totalBytesRead;
    }
    
    const char* getDeviceName() const {
        return "ESP32 UART MIDI";
    }
    
private:
    uart_port_t uartPort_;
};

} // namespace esp32

#endif // PLATFORM_ESP32
