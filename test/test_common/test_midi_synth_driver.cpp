#include <unity.h>
#include <stream_processor.hpp>
#include <memory>

// Provide std::make_unique for C++11 compatibility
#if __cplusplus <= 201103L
namespace std {
    template <typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}
#endif

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

class MockSynthVoiceAllocator : public midi::SynthVoiceAllocator {
public:
    uint8_t allocateVoice(uint8_t midiNote) override {
        lastAllocatedMidiNote = midiNote;
        allocateVoiceCallCount++;
        return allocatedVoiceToReturn;
    }
    
    void releaseVoice(uint8_t synthVoice) override {
        lastReleasedVoice = synthVoice;
        releaseVoiceCallCount++;
    }
    
    uint8_t getSynthVoice(uint8_t midiNote) const override {
        lastQueriedMidiNote = midiNote;
        getSynthVoiceCallCount++;
        return synthVoiceToReturn;
    }
    
    // Test configuration
    uint8_t allocatedVoiceToReturn = 1;
    uint8_t synthVoiceToReturn = 1;
    
    // Test verification helpers
    uint8_t lastAllocatedMidiNote = 0;
    uint8_t lastReleasedVoice = 0;
    mutable uint8_t lastQueriedMidiNote = 0;
    
    // Call counters
    int allocateVoiceCallCount = 0;
    int releaseVoiceCallCount = 0;
    mutable int getSynthVoiceCallCount = 0;
};

void setUp(void) {
    // Unity setUp - currently not used, keeping for potential future use
}

void tearDown(void) {
    // Unity tearDown - currently not used, keeping for potential future use
}

// Helper struct to hold test fixtures
struct TestFixture {
    std::unique_ptr<MockSynth> mockSynth;
    std::unique_ptr<MockSynthVoiceAllocator> mockAllocator;
    MockSynth* synthPtr;
    MockSynthVoiceAllocator* allocatorPtr;
    
    TestFixture() {
        mockSynth = std::make_unique<MockSynth>();
        mockAllocator = std::make_unique<MockSynthVoiceAllocator>();
        synthPtr = mockSynth.get();
        allocatorPtr = mockAllocator.get();
    }
};

// Helper function to create a StreamProcessor with default channel (0)
midi::StreamProcessor createProcessor(TestFixture& fixture) {
    return midi::StreamProcessor(std::move(fixture.mockSynth), std::move(fixture.mockAllocator));
}

// Helper function to create a StreamProcessor with specific channel
midi::StreamProcessor createProcessorWithChannel(TestFixture& fixture, uint8_t channel) {
    return midi::StreamProcessor(std::move(fixture.mockSynth), std::move(fixture.mockAllocator), channel);
}

// Helper function to send a complete Note On message
void sendNoteOnMessage(midi::StreamProcessor& processor, uint8_t channel, uint8_t note, uint8_t velocity) {
    uint8_t statusByte = 0x90 | channel; // Note On command + channel
    processor.process(statusByte);
    processor.process(note);
    processor.process(velocity);
}

void test_noteOn_should_allocateASynthVoice(void) {
    // Arrange
    TestFixture fixture;
    fixture.allocatorPtr->allocatedVoiceToReturn = 2;
    
    midi::StreamProcessor processor = createProcessor(fixture);
    
    // Act - Send a MIDI Note On message: 0x90 (Note On, Channel 0), 0x40 (Middle C), 0x7F (Max velocity)
    sendNoteOnMessage(processor, 0, 0x40, 0x7F);
    
    // Assert - Verify voice allocator was called correctly
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, fixture.allocatorPtr->lastAllocatedMidiNote, "Should allocate voice for Middle C");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, fixture.allocatorPtr->allocateVoiceCallCount, "allocateVoice should be called exactly once");
    
    // Assert - Verify voice allocator other methods were not called
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocatorPtr->releaseVoiceCallCount, "releaseVoice should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocatorPtr->getSynthVoiceCallCount, "getSynthVoice should not be called");
    
    // Assert - Verify synth methods call counts (exact behavior depends on implementation)
    // Note: These assertions may need adjustment based on actual StreamProcessor implementation
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.synthPtr->releaseCallCount, "release should not be called for Note On");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.synthPtr->setTimbreCallCount, "setTimbre should not be called for basic Note On");
}

void test_noteOn_shouldIgnoreWrongChannel(void) {
    // Arrange
    TestFixture fixture;
    midi::StreamProcessor processor = createProcessorWithChannel(fixture, 1); // Listen to channel 1
    
    // Act - Send a MIDI Note On message on channel 0
    sendNoteOnMessage(processor, 0, 0x40, 0x7F);
    
    // Assert - Should ignore the message since it's on the wrong channel
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocatorPtr->allocateVoiceCallCount, "allocateVoice should not be called for wrong channel");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocatorPtr->releaseVoiceCallCount, "releaseVoice should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocatorPtr->getSynthVoiceCallCount, "getSynthVoice should not be called");
}

void test_noteOn_shouldRespondToCorrectChannel(void) {
    // Arrange
    TestFixture fixture;
    midi::StreamProcessor processor = createProcessorWithChannel(fixture, 1); // Listen to channel 1
    
    // Act - Send a MIDI Note On message on channel 1
    sendNoteOnMessage(processor, 1, 0x40, 0x7F);
    
    // Assert - Should respond to the message since it's on the correct channel
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, fixture.allocatorPtr->lastAllocatedMidiNote, "Should allocate voice for Middle C on correct channel");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, fixture.allocatorPtr->allocateVoiceCallCount, "allocateVoice should be called exactly once");
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
    RUN_TEST(test_noteOn_should_allocateASynthVoice);
    RUN_TEST(test_noteOn_shouldIgnoreWrongChannel);
    RUN_TEST(test_noteOn_shouldRespondToCorrectChannel);
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