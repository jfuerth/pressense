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
 */
class ESP32CapacitiveScanner : public midi::KeyScanner {
public:
    static constexpr uint8_t NUM_KEYS = 8;
    static constexpr uint32_t SCAN_INTERVAL_MS = 10;  // 100Hz
    static constexpr uint32_t DISCHARGE_TIME_US = 100;  // Discharge capacitor
    static constexpr uint32_t TIMEOUT_US = 500;  // Max measurement time
    static constexpr uint8_t MOVING_AVG_SAMPLES = 5;
    
    /**
     * @brief Construct and start the capacitive scanner task
     */
    ESP32CapacitiveScanner() {
        // GPIO pins for 8 keys (all touch-capable, avoiding I2S pins 22, 25, 26)
        keyGpios_[0] = GPIO_NUM_4;
        keyGpios_[1] = GPIO_NUM_12;
        keyGpios_[2] = GPIO_NUM_13;
        keyGpios_[3] = GPIO_NUM_14;
        keyGpios_[4] = GPIO_NUM_15;
        keyGpios_[5] = GPIO_NUM_27;
        keyGpios_[6] = GPIO_NUM_32;
        keyGpios_[7] = GPIO_NUM_33;
        
        // Initialize GPIO pins
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            gpio_reset_pin(keyGpios_[i]);
            gpio_set_direction(keyGpios_[i], GPIO_MODE_OUTPUT);
            gpio_set_level(keyGpios_[i], 0);  // Start low
        }
        
        // Initialize moving average buffers
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            for (uint8_t j = 0; j < MOVING_AVG_SAMPLES; j++) {
                movingAvgBuffer_[i][j] = 0;
            }
            movingAvgIndex_[i] = 0;
            currentReadings_[i] = 0;
        }
        
        // Create scanning task
        xTaskCreate(
            scanTaskWrapper,
            "cap_scan",
            4096,  // Stack size
            this,  // Pass this pointer to static wrapper
            1,     // Priority (same as arpeggio task)
            &taskHandle_
        );
        
        logInfo("ESP32 capacitive scanner started with %d keys", NUM_KEYS);
    }
    
    ~ESP32CapacitiveScanner() {
        if (taskHandle_ != nullptr) {
            vTaskDelete(taskHandle_);
            taskHandle_ = nullptr;
        }
        
        // Return pins to safe state
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            gpio_set_direction(keyGpios_[i], GPIO_MODE_OUTPUT);
            gpio_set_level(keyGpios_[i], 0);
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
     * @param gpio GPIO pin to measure
     * @return Time in microseconds until pin reads high (clamped to TIMEOUT_US)
     */
    uint16_t measureKey(gpio_num_t gpio) {
        // 1. Discharge: Set to output, drive low
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
        
        // 3. Return to low output state
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio, 0);
        
        return static_cast<uint16_t>(elapsed);
    }
    
    /**
     * @brief Scan all keys and update moving averages
     */
    void scanAllKeys() {
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            // Measure raw value
            uint16_t rawValue = measureKey(keyGpios_[i]);
            
            // Update moving average buffer
            movingAvgBuffer_[i][movingAvgIndex_[i]] = rawValue;
            movingAvgIndex_[i] = (movingAvgIndex_[i] + 1) % MOVING_AVG_SAMPLES;
            
            // Calculate moving average
            uint32_t sum = 0;
            for (uint8_t j = 0; j < MOVING_AVG_SAMPLES; j++) {
                sum += movingAvgBuffer_[i][j];
            }
            currentReadings_[i] = sum / MOVING_AVG_SAMPLES;
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
