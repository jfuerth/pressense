#pragma once

#include <driver/i2s.h>
#include <vector>
#include <stdexcept>
#include <log.hpp>

namespace esp32 {

/**
 * @brief ESP32 I2S audio output for PCM5102 DAC
 * 
 * Pin configuration for PCM5102:
 * - I2S_BCK (bit clock) -> GPIO 26
 * - I2S_WS (word select/LRCK) -> GPIO 25
 * - I2S_DATA_OUT -> GPIO 22
 * 
 * PCM5102 connections:
 * Digital side:
 *  - SCK -> GND (High frequency clock generated internally in the DAC)
 *  - BCK -> GPIO 26 (Bit clock from I2S)
 *  - DIN -> GPIO 22 (Data input from I2S)
 *  - LRCK -> GPIO 25 (Word select from I2S)
 * 
 * Analog side:
 *  - FLT -> GND (FIR normal latency filter)
 *  - DEMP -> GND (De-emphasis off)
 *  - XSMT -> AVDD (3v3, soft mute off)
 *  - FMT -> GND (I2S format)
 */
class I2sAudioSink {
public:
    I2sAudioSink(unsigned int sampleRate = 44100,
                 unsigned int channels = 2,
                 unsigned int bufferFrames = 128,
                 i2s_port_t i2sPort = I2S_NUM_0)
        : sampleRate_(sampleRate)
        , channels_(channels)
        , bufferFrames_(bufferFrames)
        , i2sPort_(i2sPort)
        , underrunCount_(0)
        , partialWriteCount_(0) {
        
        // I2S configuration
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate = sampleRate_,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // 32-bit for better quality
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 4,
            .dma_buf_len = static_cast<int>(bufferFrames_),
            .use_apll = true,  // Use APLL for better clock accuracy
            .tx_desc_auto_clear = true,
            .fixed_mclk = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT  // match bits_per_sample
        };
        
        // I2S pin configuration for PCM5102
        i2s_pin_config_t pin_config = {
            .mck_io_num = I2S_PIN_NO_CHANGE,
            .bck_io_num = 26,        // Bit clock
            .ws_io_num = 25,         // Word select (LRCK)
            .data_out_num = 22,      // Data out
            .data_in_num = I2S_PIN_NO_CHANGE
        };
        
        // Install and start I2S driver
        esp_err_t err = i2s_driver_install(i2sPort_, &i2s_config, 0, nullptr);
        if (err != ESP_OK) {
            logError("Failed to install I2S driver: %d", err);
            // Fatal error - system will halt
            return;
        }
        
        err = i2s_set_pin(i2sPort_, &pin_config);
        if (err != ESP_OK) {
            i2s_driver_uninstall(i2sPort_);
            logError("Failed to set I2S pins: %d", err);
            // Fatal error - system will halt
            return;
        }
        
        // Get the actual sample rate achieved by the I2S hardware
        // ESP32 I2S has limited clock divider options, so actual rate may differ
        float actualSampleRateFloat = i2s_get_clk(i2sPort_);
        uint32_t actualSampleRate = static_cast<uint32_t>(actualSampleRateFloat);
        
        if (actualSampleRate != sampleRate_) {
            logWarn("I2S actual sample rate %lu Hz differs from requested %u Hz", 
                    actualSampleRate, sampleRate_);
            logWarn("This will cause pitch errors! Adjust synthesizer sample rate.");
            // Update our stored sample rate to match reality
            sampleRate_ = actualSampleRate;
        }
        
        // Allocate buffer (float samples)
        buffer_.resize(bufferFrames_ * channels_);
        
        // Allocate I2S buffer (32-bit integers)
        i2sBuffer_.resize(bufferFrames_ * channels_);
        
        logInfo("I2S audio initialized: %lu Hz actual, %d channels, %d frames/buffer",
                actualSampleRate, channels_, bufferFrames_);
    }
    
    ~I2sAudioSink() {
        i2s_driver_uninstall(i2sPort_);
    }
    
    // Delete copy constructor and assignment
    I2sAudioSink(const I2sAudioSink&) = delete;
    I2sAudioSink& operator=(const I2sAudioSink&) = delete;
    
    /**
     * @brief Fill buffer and write to I2S device
     * @param fillCallback Function that generates samples: callback(buffer, numFrames)
     */
    template<typename Callback>
    void write(Callback fillCallback) {
        // Fill buffer with audio data (float format)
        fillCallback(buffer_.data(), bufferFrames_);
        
        // Convert float to 32-bit integer for I2S
        // PCM5102 expects 32-bit data, MSB first
        // Use 24-bit range to avoid overflow and provide headroom
        const float scale = 8388608.0f;  // 2^23 for 24-bit range
        
        for (size_t i = 0; i < bufferFrames_ * channels_; ++i) {
            // Clamp and convert float [-1.0, 1.0] to int32
            float sample = buffer_[i];
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            
            // Convert to 24-bit signed integer, then shift left 8 bits
            // This left-justifies the 24-bit data in the 32-bit word
            int32_t sample24 = static_cast<int32_t>(sample * scale);
            i2sBuffer_[i] = sample24 << 8;
        }
        
        // Write to I2S
        size_t bytesWritten = 0;
        size_t bytesToWrite = bufferFrames_ * channels_ * sizeof(int32_t);
        esp_err_t err = i2s_write(i2sPort_, i2sBuffer_.data(), 
                                  bytesToWrite,
                                  &bytesWritten, portMAX_DELAY);
        
        if (err != ESP_OK) {
            logError("I2S write failed: %d", err);
        }
        
        // Detect buffer underruns
        if (bytesWritten < bytesToWrite) {
            partialWriteCount_++;
            if (partialWriteCount_ % 100 == 1) {
                logWarn("I2S partial write: %d/%d bytes (underrun #%lu)", 
                        bytesWritten, bytesToWrite, partialWriteCount_);
            }
        }
        
        // Check DMA buffer status
        if (bytesWritten == 0) {
            underrunCount_++;
            if (underrunCount_ % 10 == 1) {
                logError("I2S complete underrun (zero bytes written) #%lu", underrunCount_);
            }
        }
    }
    
    // Get underrun statistics
    uint32_t getUnderrunCount() const { return underrunCount_; }
    uint32_t getPartialWriteCount() const { return partialWriteCount_; }
    
    unsigned int getSampleRate() const { return sampleRate_; }
    unsigned int getChannels() const { return channels_; }
    unsigned int getBufferFrames() const { return bufferFrames_; }
    
private:
    unsigned int sampleRate_;
    unsigned int channels_;
    unsigned int bufferFrames_;
    i2s_port_t i2sPort_;
    
    std::vector<float> buffer_;      // Float buffer for synthesis
    std::vector<int32_t> i2sBuffer_; // I2S output buffer
    
    // Underrun detection counters
    uint32_t underrunCount_;
    uint32_t partialWriteCount_;
};

} // namespace esp32
