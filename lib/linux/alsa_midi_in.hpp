#ifndef ALSA_MIDI_IN_HPP
#define ALSA_MIDI_IN_HPP

#include <alsa/asoundlib.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>
#include <cstring>

namespace linux {

/**
 * @brief Information about an available MIDI input device
 */
struct MidiDeviceInfo {
    std::string name;        // e.g., "hw:2,0,0"
    std::string description; // Human-readable description
    int card;
    int device;
    int subdevice;
};

/**
 * @brief ALSA raw MIDI input for Linux
 * 
 * Uses the raw MIDI API for direct byte-by-byte access, suitable for
 * feeding into a MIDI stream processor. Non-blocking reads allow polling
 * from the audio thread without blocking.
 */
class AlsaMidiIn {
public:
    /**
     * @brief List available MIDI input devices
     * @return Vector of device information structs
     */
    static std::vector<MidiDeviceInfo> listDevices() {
        std::vector<MidiDeviceInfo> devices;
        int card = -1;
        
        // Iterate through all sound cards
        while (snd_card_next(&card) >= 0 && card >= 0) {
            snd_ctl_t* ctl;
            char name[32];
            snprintf(name, sizeof(name), "hw:%d", card);
            
            if (snd_ctl_open(&ctl, name, 0) < 0) {
                continue;
            }
            
            int device = -1;
            // Iterate through all devices on this card
            while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0) {
                snd_rawmidi_info_t* info;
                snd_rawmidi_info_alloca(&info);
                snd_rawmidi_info_set_device(info, device);
                
                // Check each subdevice for input capability
                int subdevice = -1;
                snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
                
                while (true) {
                    subdevice++;
                    snd_rawmidi_info_set_subdevice(info, subdevice);
                    
                    int err = snd_ctl_rawmidi_info(ctl, info);
                    if (err < 0) {
                        break; // No more subdevices
                    }
                    
                    MidiDeviceInfo devInfo;
                    devInfo.card = card;
                    devInfo.device = device;
                    devInfo.subdevice = subdevice;
                    
                    // Build device name
                    char devName[64];
                    snprintf(devName, sizeof(devName), "hw:%d,%d,%d", card, device, subdevice);
                    devInfo.name = devName;
                    
                    // Get description
                    const char* devNameStr = snd_rawmidi_info_get_name(info);
                    const char* subdevNameStr = snd_rawmidi_info_get_subdevice_name(info);
                    
                    if (subdevNameStr && strlen(subdevNameStr) > 0) {
                        devInfo.description = std::string(devNameStr) + " - " + subdevNameStr;
                    } else {
                        devInfo.description = devNameStr;
                    }
                    
                    devices.push_back(devInfo);
                }
            }
            
            snd_ctl_close(ctl);
        }
        
        return devices;
    }
    
    /**
     * @brief Open a MIDI input device
     * @param deviceName ALSA device name (e.g., "hw:2,0,0"). If nullptr, opens first available device.
     * @throws std::runtime_error if device cannot be opened
     */
    AlsaMidiIn(const char* deviceName = nullptr) {
        std::string actualDevice;
        
        if (deviceName == nullptr) {
            // Find first available device
            auto devices = listDevices();
            if (devices.empty()) {
                throw std::runtime_error("No MIDI input devices found");
            }
            actualDevice = devices[0].name;
        } else {
            actualDevice = deviceName;
        }
        
        // Open raw MIDI device in non-blocking mode
        int err = snd_rawmidi_open(&midiHandle_, nullptr, actualDevice.c_str(), SND_RAWMIDI_NONBLOCK);
        if (err < 0) {
            throw std::runtime_error(std::string("Cannot open MIDI device ") + actualDevice + ": " + snd_strerror(err));
        }
        
        deviceName_ = actualDevice;
    }
    
    ~AlsaMidiIn() {
        if (midiHandle_) {
            snd_rawmidi_close(midiHandle_);
        }
    }
    
    // Delete copy constructor and assignment
    AlsaMidiIn(const AlsaMidiIn&) = delete;
    AlsaMidiIn& operator=(const AlsaMidiIn&) = delete;
    
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
        uint8_t buffer[256];
        
        while (true) {
            ssize_t bytesRead = snd_rawmidi_read(midiHandle_, buffer, sizeof(buffer));
            
            if (bytesRead < 0) {
                if (bytesRead == -EAGAIN || bytesRead == -EWOULDBLOCK) {
                    // No more data available (normal in non-blocking mode)
                    break;
                } else {
                    // Actual error
                    throw std::runtime_error(std::string("MIDI read error: ") + snd_strerror(bytesRead));
                }
            }
            
            if (bytesRead == 0) {
                break;
            }
            
            // Process each byte through callback
            for (ssize_t i = 0; i < bytesRead; ++i) {
                callback(buffer[i]);
            }
            
            totalBytesRead += bytesRead;
        }
        
        return totalBytesRead;
    }
    
    /**
     * @brief Get the name of the opened device
     */
    const std::string& getDeviceName() const {
        return deviceName_;
    }

private:
    snd_rawmidi_t* midiHandle_ = nullptr;
    std::string deviceName_;
};

} // namespace linux

#endif // ALSA_MIDI_IN_HPP
