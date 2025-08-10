#include <unity.h>
#include <stream_processor.hpp>
#include <memory>

// Mock implementations for testing
class MockSynth : public midi::Synth {
public:
    void trigger(float frequencyHz, float volume) override {
        lastTriggerFrequency = frequencyHz;
        lastTriggerVolume = volume;
        triggerCallCount++;
    }
    
    void release() override {
        releaseCallCount++;
    }
    
    void setFrequency(float frequencyHz) override {
        lastSetFrequency = frequencyHz;
        setFrequencyCallCount++;
    }
    
    void setTimbre(float timbre) override {
        lastSetTimbre = timbre;
        setTimbreCallCount++;
    }
    
    void setVolume(float volume) override {
        lastSetVolume = volume;
        setVolumeCallCount++;
    }
    
    bool isActive() const override {
        isActiveCallCount++;
        return activeState;
    }
    
    // Test verification helpers
    float lastTriggerFrequency = 0.0f;
    float lastTriggerVolume = 0.0f;
    float lastSetFrequency = 0.0f;
    float lastSetTimbre = 0.0f;
    float lastSetVolume = 0.0f;
    bool activeState = false;
    
    // Call counters
    int triggerCallCount = 0;
    int releaseCallCount = 0;
    int setFrequencyCallCount = 0;
    int setTimbreCallCount = 0;
    int setVolumeCallCount = 0;
    mutable int isActiveCallCount = 0;
};

class MockSynthChannelAllocator : public midi::SynthChannelAllocator {
public:
    uint8_t allocateChannel(uint8_t midiNote) override {
        lastAllocatedMidiNote = midiNote;
        allocateChannelCallCount++;
        return allocatedChannelToReturn;
    }
    
    void releaseChannel(uint8_t synthChannel) override {
        lastReleasedChannel = synthChannel;
        releaseChannelCallCount++;
    }
    
    uint8_t getSynthChannel(uint8_t midiNote) const override {
        lastQueriedMidiNote = midiNote;
        getSynthChannelCallCount++;
        return synthChannelToReturn;
    }
    
    // Test configuration
    uint8_t allocatedChannelToReturn = 1;
    uint8_t synthChannelToReturn = 1;
    
    // Test verification helpers
    uint8_t lastAllocatedMidiNote = 0;
    uint8_t lastReleasedChannel = 0;
    mutable uint8_t lastQueriedMidiNote = 0;
    
    // Call counters
    int allocateChannelCallCount = 0;
    int releaseChannelCallCount = 0;
    mutable int getSynthChannelCallCount = 0;
};

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_noteOn_should_allocateASynthChannel(void) {
    // Arrange
    auto mockSynth = std::make_unique<MockSynth>();
    auto mockAllocator = std::make_unique<MockSynthChannelAllocator>();
    
    // Keep raw pointers for verification before moving ownership
    MockSynth* synthPtr = mockSynth.get();
    MockSynthChannelAllocator* allocatorPtr = mockAllocator.get();
    
    // Configure the mock to return channel 2 when allocating
    allocatorPtr->allocatedChannelToReturn = 2;
    
    midi::StreamProcessor processor(std::move(mockSynth), std::move(mockAllocator));
    
    // Act - Send a MIDI Note On message: 0x90 (Note On, Channel 0), 0x40 (Middle C), 0x7F (Max velocity)
    processor.process(0x90); // Note On status byte
    processor.process(0x40); // Note number (Middle C = 64 decimal = 0x40 hex)
    processor.process(0x7F); // Velocity (127 decimal = 0x7F hex)
    
    // Assert - Verify channel allocator was called correctly
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, allocatorPtr->lastAllocatedMidiNote, "Should allocate channel for Middle C");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, allocatorPtr->allocateChannelCallCount, "allocateChannel should be called exactly once");
    
    // Assert - Verify channel allocator other methods were not called
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, allocatorPtr->releaseChannelCallCount, "releaseChannel should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, allocatorPtr->getSynthChannelCallCount, "getSynthChannel should not be called");
    
    // Assert - Verify synth methods call counts (exact behavior depends on implementation)
    // Note: These assertions may need adjustment based on actual StreamProcessor implementation
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, synthPtr->releaseCallCount, "release should not be called for Note On");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, synthPtr->setTimbreCallCount, "setTimbre should not be called for basic Note On");
}

// TODO test running status (should not reset the status byte when more non-status bytes are received)
// TODO test status byte interrupting a partial message (should throw away the partial message)
// TODO test system real-time messages interrupting a partial message (should resume the partial message)
// TODO test system common bytes
// TODO test system real-time bytes
// TODO test system exclusive messages
// TODO test channel voice messages
// TODO test channel mode messages

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_noteOn_should_allocateASynthChannel);
    UNITY_END();
}

extern "C" {
#ifdef PLATFORM_ESP32
void app_main() {
    RUN_UNITY_TESTS();
}
#endif

#ifdef PLATFORM_NATIVE
int main(int argc, char **argv) {
    RUN_UNITY_TESTS();
    return 0;
}
#endif
}