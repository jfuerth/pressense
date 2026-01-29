#pragma once

#ifdef PLATFORM_ESP32

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <cstdint>
#include <log.hpp>

namespace esp32 {

/**
 * @brief FreeRTOS task that generates MIDI arpeggio pattern
 * 
 * Plays a simple C major arpeggio (C4-E4-G4-C5) repeatedly
 * to test the synthesizer without requiring external MIDI input.
 * 
 * Sends standard MIDI bytes via callback:
 * - Note On: 0x90 note velocity
 * - Note Off: 0x80 note 0x00
 */
class ArpeggioTask {
public:
    /**
     * @brief Create and start arpeggio task
     * @param midiCallback Function to call with each MIDI byte
     * @param noteDurationMs Duration of each note in milliseconds
     */
    ArpeggioTask(std::function<void(uint8_t)> midiCallback, 
                 uint32_t noteDurationMs = 300)
        : midiCallback_(midiCallback)
        , noteDurationMs_(noteDurationMs)
        , running_(true) {
        
        // Create FreeRTOS task
        xTaskCreate(
            taskFunction,
            "ArpeggioTask",
            4096,              // Stack size
            this,              // Task parameter (this pointer)
            1,                 // Priority (low)
            &taskHandle_
        );
        
        logInfo("Arpeggio task started (note duration: %lu ms)", noteDurationMs_);
    }
    
    ~ArpeggioTask() {
        stop();
    }
    
    void stop() {
        if (running_) {
            running_ = false;
            if (taskHandle_) {
                vTaskDelete(taskHandle_);
                taskHandle_ = nullptr;
            }
        }
    }
    
private:
    static void taskFunction(void* parameter) {
        ArpeggioTask* self = static_cast<ArpeggioTask*>(parameter);
        self->run();
    }
    
    void run() {
        // C major arpeggio notes (C4, E4, G4, C5)
        const uint8_t notes[] = {60, 64, 67, 72};
        const uint8_t numNotes = sizeof(notes) / sizeof(notes[0]);
        const uint8_t channel = 0;
        const uint8_t velocity = 100;
        
        uint8_t currentNote = 0;
        
        logInfo("Arpeggio pattern: C4-E4-G4-C5 (MIDI notes %d-%d-%d-%d)",
                notes[0], notes[1], notes[2], notes[3]);
        
        while (running_) {
            uint8_t note = notes[currentNote];
            
            // Send Note On (0x90 = note on channel 0)
            sendMidiByte(0x90 | channel);
            sendMidiByte(note);
            sendMidiByte(velocity);
            
            // Hold note
            vTaskDelay(pdMS_TO_TICKS(noteDurationMs_));
            
            // Send Note Off (0x80 = note off channel 0)
            sendMidiByte(0x80 | channel);
            sendMidiByte(note);
            sendMidiByte(0x00);
            
            // Small gap between notes
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // Next note in arpeggio
            currentNote = (currentNote + 1) % numNotes;
            
            // Pause at end of pattern
            if (currentNote == 0) {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
    }
    
    void sendMidiByte(uint8_t byte) {
        if (midiCallback_) {
            midiCallback_(byte);
        }
    }
    
    std::function<void(uint8_t)> midiCallback_;
    uint32_t noteDurationMs_;
    bool running_;
    TaskHandle_t taskHandle_ = nullptr;
};

} // namespace esp32

#endif // PLATFORM_ESP32
