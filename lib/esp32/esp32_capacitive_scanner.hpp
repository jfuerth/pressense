#pragma once

#include <key_scanner.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <log.hpp>
#include <array>

namespace esp32 {

/**
 * @brief ESP32 capacitive touch scanner using discharge-time measurement
 * 
 * Uses regular GPIOs with external pull-up resistors (800kΩ recommended).
 * Measures RC discharge time to detect capacitance changes from finger touches.
 * Runs as FreeRTOS task at 100Hz with 5-sample moving average per key.
 * 
 * HARDWARE REQUIREMENTS:
 * - Each key needs an 800kΩ pull-up resistor to 3.3V
 */
class ESP32CapacitiveScanner : public midi::KeyScanner {
public:
    static constexpr uint8_t NUM_KEYS = 14;
    static constexpr uint32_t SCAN_INTERVAL_MS = 10;  // 100Hz
    static constexpr uint32_t DISCHARGE_TIME_US = 100;  // Discharge capacitor
    static constexpr uint32_t TIMEOUT_US = 500;  // Max measurement time (balance between range and blocking)
    static constexpr uint32_t SETTLE_TIME_US = 5;  // Minimal settle time
    static constexpr uint8_t MOVING_AVG_SAMPLES = 5;
    static constexpr uint16_t MIN_BASELINE_VALUE = 1;  // Minimum baseline to avoid ratio issues
    
    /**
     * @brief Construct and start the capacitive scanner task
     */
    ESP32CapacitiveScanner() {
        // GPIO pins for 14 keys (avoiding I2S pins 22, 25, 26 and boot/flash pins)
        keyGpios_[0] = GPIO_NUM_4;
        keyGpios_[1] = GPIO_NUM_12;
        keyGpios_[2] = GPIO_NUM_13;
        keyGpios_[3] = GPIO_NUM_14;
        keyGpios_[4] = GPIO_NUM_15;
        keyGpios_[5] = GPIO_NUM_16; // Labelled RX2 on DEVKIT V1
        keyGpios_[6] = GPIO_NUM_17; // Labelled TX2 on DEVKIT V1
        keyGpios_[7] = GPIO_NUM_18;
        keyGpios_[8] = GPIO_NUM_19;
        keyGpios_[9] = GPIO_NUM_21;
        keyGpios_[10] = GPIO_NUM_23;
        keyGpios_[11] = GPIO_NUM_27;
        keyGpios_[12] = GPIO_NUM_32;
        keyGpios_[13] = GPIO_NUM_33;
        
        // Initialize GPIO pins - start as inputs (high-Z) for minimal crosstalk
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            gpio_reset_pin(keyGpios_[i]);
            gpio_set_direction(keyGpios_[i], GPIO_MODE_INPUT);
            gpio_set_pull_mode(keyGpios_[i], GPIO_FLOATING);  // High-Z, external pull-up
        }
        
        // Initialize moving average buffers
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            for (uint8_t j = 0; j < MOVING_AVG_SAMPLES; j++) {
                movingAvgBuffer_[i][j] = 0;
            }
            movingAvgIndex_[i] = 0;
            currentReadings_[i] = 0;
        }
        
        // Create scanning task pinned to core 0 (keep away from audio on core 1)
        xTaskCreatePinnedToCore(
            scanTaskWrapper,
            "cap_scan",
            4096,  // Stack size
            this,  // Pass this pointer to static wrapper
            1,     // Priority 1 (higher than telemetry, lower than audio)
            &taskHandle_,
            0      // Core 0 (PRO_CPU)
        );
        
        logInfo("ESP32 capacitive scanner started with %d keys", NUM_KEYS);
    }
    
    ~ESP32CapacitiveScanner() {
        if (taskHandle_ != nullptr) {
            vTaskDelete(taskHandle_);
            taskHandle_ = nullptr;
        }
        
        // Return pins to safe high-Z state
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            gpio_set_direction(keyGpios_[i], GPIO_MODE_INPUT);
        }
        
        logInfo("ESP32 capacitive scanner stopped");
    }
    
    const uint16_t* getScanReadings() const override {
        return currentReadings_.data();
    }
    
    uint8_t getKeyCount() const override {
        return NUM_KEYS;
    }
    
private:
    std::array<gpio_num_t, NUM_KEYS> keyGpios_;
    std::array<uint16_t, NUM_KEYS> currentReadings_;
    std::array<std::array<uint16_t, MOVING_AVG_SAMPLES>, NUM_KEYS> movingAvgBuffer_;
    std::array<uint8_t, NUM_KEYS> movingAvgIndex_;
    TaskHandle_t taskHandle_ = nullptr;
    
    /**
     * @brief Measure discharge time for a single key
     * @param keyIndex Index of key to measure (0..NUM_KEYS-1)
     * @return Time in microseconds until pin reads high (clamped to TIMEOUT_US)
     * 
     * CROSSTALK MITIGATION STRATEGY:
     * - All other keys remain in high-Z (input) state during measurement
     * - This prevents charge injection through the user's hand from other electrodes
     * - The external pull-up resistors (800kΩ) provide weak pull-up to 3.3V
     * - Each key capacitance is measured independently without interference
     */
    uint16_t measureKey(uint8_t keyIndex) {
        gpio_num_t gpio = keyGpios_[keyIndex];
        
        // Note all other keys are in high-Z (INPUT) state
        
        // 1. Discharge measured key: Set to output, drive low
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio, 0);
        esp_rom_delay_us(DISCHARGE_TIME_US);
        
        // 2. Measure: Set to input (high-Z), wait for pull-up to charge capacitor
        gpio_set_direction(gpio, GPIO_MODE_INPUT);
        
        uint64_t startTime = esp_timer_get_time();
        uint64_t elapsed = 0;
        
        // Poll until high or timeout
        while (gpio_get_level(gpio) == 0) {
            elapsed = esp_timer_get_time() - startTime;
            if (elapsed >= TIMEOUT_US) {
                break;
            }
        }
        
        // Key remains in INPUT state for next scan
        // Measured time represents capacitance: more capacitance = longer charge time
        return static_cast<uint16_t>(elapsed);
    }
    
    /**
     * @brief Scan all keys and update moving averages
     */
    void scanAllKeys() {
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            // Measure raw value
            uint16_t rawValue = measureKey(i);
            
            // Allow GPIO state to settle before next measurement
            esp_rom_delay_us(SETTLE_TIME_US);
            
            // Yield CPU to allow other tasks to run if ready (currently none should be, but good practice in case of future changes)
            taskYIELD();
            
            // Update moving average buffer
            movingAvgBuffer_[i][movingAvgIndex_[i]] = rawValue;
            movingAvgIndex_[i] = (movingAvgIndex_[i] + 1) % MOVING_AVG_SAMPLES;
            
            // Calculate sum (better resolution than averaging)
            uint32_t sum = 0;
            for (uint8_t j = 0; j < MOVING_AVG_SAMPLES; j++) {
                sum += movingAvgBuffer_[i][j];
            }
            currentReadings_[i] = static_cast<uint16_t>(sum);
        }
    }
    
    /**
     * @brief Main scanning task loop
     */
    void scanTask() {
        TickType_t lastWakeTime = xTaskGetTickCount();
        
        while (true) {
            scanAllKeys();
            
            // Sleep until next scan interval (100Hz)
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(SCAN_INTERVAL_MS));
        }
    }
    
    /**
     * @brief Static wrapper for FreeRTOS task creation
     */
    static void scanTaskWrapper(void* parameter) {
        auto* scanner = static_cast<ESP32CapacitiveScanner*>(parameter);
        scanner->scanTask();
    }
};

} // namespace esp32
