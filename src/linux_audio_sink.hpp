#ifndef LINUX_AUDIO_SINK_HPP
#define LINUX_AUDIO_SINK_HPP

#include <alsa/asoundlib.h>
#include <vector>
#include <stdexcept>
#include <cstring>

/**
 * @brief ALSA-based audio output for Linux
 */
class LinuxAudioSink {
public:
    LinuxAudioSink(const char* deviceName = "default", 
                   unsigned int sampleRate = 44100,
                   unsigned int channels = 2,
                   unsigned int bufferFrames = 512)
        : sampleRate_(sampleRate)
        , channels_(channels)
        , bufferFrames_(bufferFrames) {
        
        int err;
        
        // Open PCM device
        if ((err = snd_pcm_open(&pcmHandle_, deviceName, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
            throw std::runtime_error(std::string("Cannot open audio device: ") + snd_strerror(err));
        }
        
        // Allocate hardware parameters object
        snd_pcm_hw_params_t* hwParams;
        snd_pcm_hw_params_alloca(&hwParams);
        
        // Fill with default values
        snd_pcm_hw_params_any(pcmHandle_, hwParams);
        
        // Set parameters
        snd_pcm_hw_params_set_access(pcmHandle_, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcmHandle_, hwParams, SND_PCM_FORMAT_FLOAT_LE);
        snd_pcm_hw_params_set_channels(pcmHandle_, hwParams, channels_);
        snd_pcm_hw_params_set_rate_near(pcmHandle_, hwParams, &sampleRate_, 0);
        snd_pcm_hw_params_set_period_size_near(pcmHandle_, hwParams, &bufferFrames_, 0);
        
        // Write parameters to device
        if ((err = snd_pcm_hw_params(pcmHandle_, hwParams)) < 0) {
            snd_pcm_close(pcmHandle_);
            throw std::runtime_error(std::string("Cannot set hardware parameters: ") + snd_strerror(err));
        }
        
        // Allocate buffer
        buffer_.resize(bufferFrames_ * channels_);
    }
    
    ~LinuxAudioSink() {
        if (pcmHandle_) {
            snd_pcm_drain(pcmHandle_);
            snd_pcm_close(pcmHandle_);
        }
    }
    
    // Delete copy constructor and assignment
    LinuxAudioSink(const LinuxAudioSink&) = delete;
    LinuxAudioSink& operator=(const LinuxAudioSink&) = delete;
    
    /**
     * @brief Fill buffer and write to audio device
     * @param fillCallback Function that generates samples: callback(buffer, numFrames)
     */
    template<typename Callback>
    void write(Callback fillCallback) {
        // Fill buffer with audio data
        fillCallback(buffer_.data(), bufferFrames_);
        
        // Write to ALSA
        snd_pcm_sframes_t frames = snd_pcm_writei(pcmHandle_, buffer_.data(), bufferFrames_);
        
        if (frames < 0) {
            // Try to recover from error
            frames = snd_pcm_recover(pcmHandle_, frames, 0);
        }
        
        if (frames < 0) {
            throw std::runtime_error(std::string("Audio write failed: ") + snd_strerror(frames));
        }
        
        if (frames != static_cast<snd_pcm_sframes_t>(bufferFrames_)) {
            // Short write - this is okay, just note it
        }
    }
    
    unsigned int getSampleRate() const { return sampleRate_; }
    unsigned int getChannels() const { return channels_; }
    unsigned int getBufferFrames() const { return bufferFrames_; }

private:
    snd_pcm_t* pcmHandle_ = nullptr;
    unsigned int sampleRate_;
    unsigned int channels_;
    snd_pcm_uframes_t bufferFrames_;
    std::vector<float> buffer_;
};

#endif // LINUX_AUDIO_SINK_HPP
