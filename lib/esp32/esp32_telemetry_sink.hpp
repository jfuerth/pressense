#pragma once

#include <telemetry_sink.hpp>
#include <midi_keyboard_controller.hpp>
#include <log.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <json.hpp>
#include <cstdio>

namespace esp32 {

/**
 * @brief ESP32 telemetry sink using FreeRTOS queue and background task
 * 
 * Encapsulates queue creation, task spawning, and JSON serialization.
 * Outputs telemetry data as JSON Lines to console.
 * Properly cleans up resources in destructor.
 */
class Esp32TelemetrySink : public features::TelemetrySink<midi::KeyScanStats> {
public:
    /**
     * @brief Construct ESP32 telemetry sink
     * 
     * Creates queue and spawns background task for JSON serialization.
     */
    Esp32TelemetrySink()
        : queue_(nullptr)
        , taskHandle_(nullptr)
        , shouldStop_(false)
    {
        // Create single-slot overwrite queue
        queue_ = xQueueCreate(1, sizeof(midi::KeyScanStats));
        if (queue_ == nullptr) {
            logError("Failed to create telemetry queue");
            return;
        }
        
        // Spawn telemetry output task
        BaseType_t taskCreated = xTaskCreate(
            telemetryTaskWrapper,
            "telemetry",
            4096,  // 4KB stack for JSON serialization
            this,  // Pass 'this' pointer as parameter
            1,     // Low priority (below audio/scanner)
            &taskHandle_
        );
        
        if (taskCreated != pdPASS) {
            logError("Failed to create telemetry task");
            vQueueDelete(queue_);
            queue_ = nullptr;
            taskHandle_ = nullptr;
        } else {
            logInfo("Telemetry task started");
        }
    }
    
    /**
     * @brief Destructor - stops task and cleans up resources
     */
    ~Esp32TelemetrySink() {
        if (taskHandle_ != nullptr) {
            // Signal task to stop
            shouldStop_ = true;
            
            // Wait for task to finish (with timeout)
            // Send dummy data to unblock task if it's waiting on queue
            if (queue_ != nullptr) {
                midi::KeyScanStats dummy{};
                xQueueOverwrite(queue_, &dummy);
            }
            
            // Give task time to exit gracefully
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // Delete the task
            vTaskDelete(taskHandle_);
            taskHandle_ = nullptr;
            
            logInfo("Telemetry task stopped");
        }
        
        if (queue_ != nullptr) {
            vQueueDelete(queue_);
            queue_ = nullptr;
        }
    }
    
    // Prevent copying
    Esp32TelemetrySink(const Esp32TelemetrySink&) = delete;
    Esp32TelemetrySink& operator=(const Esp32TelemetrySink&) = delete;
    
    void sendTelemetry(const midi::KeyScanStats& data) override {
        if (queue_ != nullptr && !shouldStop_) {
            // Overwrite queue (non-blocking, replaces old data)
            xQueueOverwrite(queue_, &data);
        }
    }
    
private:
    QueueHandle_t queue_;
    TaskHandle_t taskHandle_;
    volatile bool shouldStop_;
    
    /**
     * @brief Static wrapper for FreeRTOS task creation
     */
    static void telemetryTaskWrapper(void* parameter) {
        auto* sink = static_cast<Esp32TelemetrySink*>(parameter);
        sink->telemetryTask();
    }
    
    /**
     * @brief Background task that reads queue and outputs JSON Lines
     */
    void telemetryTask() {
        midi::KeyScanStats telemetry;
        
        while (!shouldStop_) {
            // Block waiting for telemetry data with timeout to check stop flag
            if (xQueueReceive(queue_, &telemetry, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (shouldStop_) {
                    break;
                }
                
                // Serialize to JSON using nlohmann/json
                nlohmann::json j;
                j["keyCount"] = telemetry.keyCount;
                j["isCalibrated"] = telemetry.isCalibrated;
                j["calibrationCount"] = telemetry.calibrationCount;
                j["noteOnThreshold"] = telemetry.noteOnThreshold;
                j["noteOffThreshold"] = telemetry.noteOffThreshold;
                
                // Per-key arrays
                j["readings"] = nlohmann::json::array();
                j["baselines"] = nlohmann::json::array();
                j["ratios"] = nlohmann::json::array();
                j["noteStates"] = nlohmann::json::array();
                j["aftertouchValues"] = nlohmann::json::array();
                
                for (uint8_t i = 0; i < telemetry.keyCount; i++) {
                    j["readings"].push_back(telemetry.readings[i]);
                    j["baselines"].push_back(telemetry.baselines[i]);
                    j["ratios"].push_back(telemetry.ratios[i]);
                    j["noteStates"].push_back(telemetry.noteStates[i]);
                    j["aftertouchValues"].push_back(telemetry.aftertouchValues[i]);
                }
                
                // Output as JSON Lines (one object per line, starts with '{')
                printf("%s\n", j.dump().c_str());
            }
        }
    }
};

} // namespace esp32
