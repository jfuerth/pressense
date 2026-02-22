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
 * Generic template-based implementation that works with any telemetry data type.
 * Requires the data type to have a to_json() function defined for serialization.
 * Outputs telemetry data as JSON Lines to console.
 * Properly cleans up resources in destructor.
 * 
 * @tparam TelemetryDataT Type of telemetry data (must have to_json function)
 */
template<typename TelemetryDataT>
class Esp32TelemetrySink : public features::TelemetrySink<TelemetryDataT> {
public:
    /**
     * @brief Construct ESP32 telemetry sink
     * 
     * Creates queue and spawns background task for JSON serialization.
     * @param taskName Name of the telemetry task (for debugging)
     * @param priority FreeRTOS task priority
     */
    Esp32TelemetrySink(const std::string taskName = "telemetry", UBaseType_t priority = 0)
        : queue_(nullptr)
        , taskHandle_(nullptr)
        , shouldStop_(false)
    {
        // Create single-slot overwrite queue
        queue_ = xQueueCreate(1, sizeof(TelemetryDataT));
        if (queue_ == nullptr) {
            logError("Failed to create telemetry queue");
            return;
        }
        
        // Spawn telemetry output task pinned to core 0 (keep away from audio on core 1)
        BaseType_t taskCreated = xTaskCreatePinnedToCore(
            telemetryTaskWrapper,
            taskName.c_str(),
            4096,  // 4KB stack for JSON serialization
            this,  // Pass 'this' pointer as parameter
            priority,
            &taskHandle_,
            0      // Core 0 (PRO_CPU)
        );
        
        if (taskCreated != pdPASS) {
            logError("Failed to create telemetry task: %s", taskName.c_str());
            vQueueDelete(queue_);
            queue_ = nullptr;
            taskHandle_ = nullptr;
        } else {
            logInfo("Telemetry task started: %s", taskName.c_str());
        }
    };
    
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
    
    void sendTelemetry(const TelemetryDataT& data) override {
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
        auto* sink = static_cast<Esp32TelemetrySink<TelemetryDataT>*>(parameter);
        sink->telemetryTask();
    }
    
    /**
     * @brief Background task that reads queue and outputs JSON Lines
     */
    void telemetryTask() {
        TelemetryDataT telemetry;
        
        while (!shouldStop_) {
            // Block waiting for telemetry data with timeout to check stop flag
            if (xQueueReceive(queue_, &telemetry, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (shouldStop_) {
                    break;
                }
                
                // Serialize to JSON using automatic conversion via to_json()
                nlohmann::json j = telemetry;
                
                // Output as JSON Lines (one object per line, starts with '{')
                printf("%s\n", j.dump().c_str());
            }
        }
    }
};

} // namespace esp32
