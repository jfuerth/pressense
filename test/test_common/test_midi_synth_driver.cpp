#include <unity.h>
#include <stream_processor.hpp>
#include <synth.hpp>
#include <memory>
#include <vector>

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
    MockSynthVoiceAllocator() : midi::SynthVoiceAllocator(8) {
        // We're not testing the allocator here; just have a voice per note
        for (int i = 0; i < 128; i++) {
            voices.push_back(std::make_unique<MockSynth>());
        }
    }

    midi::Synth& voiceFor(uint8_t midiNote) override {
        lastQueriedMidiNote = midiNote;
        voiceForCallCount++;
        
        // Simple voice allocation: each note has a dedicated voice for testing
        lastAllocatedVoiceIndex = midiNote;
        return *voices[midiNote];
    }
    
    // Test access helpers
    MockSynth* getVoice(size_t index) {
        if (index < voices.size()) {
            return voices[index].get();
        }
        return nullptr;
    }
    
    MockSynth* getLastAllocatedVoice() {
        return getVoice(lastAllocatedVoiceIndex);
    }
        
    // Test verification helpers
    mutable uint8_t lastQueriedMidiNote = 0;
    mutable size_t lastAllocatedVoiceIndex = 0;
    
    // Call counters
    mutable int voiceForCallCount = 0;

private:
    std::vector<std::unique_ptr<MockSynth>> voices;
};

void setUp(void) {
    // Unity setUp - currently not used, keeping for potential future use
}

void tearDown(void) {
    // Unity tearDown - currently not used, keeping for potential future use
}

// Helper struct to hold test fixtures
struct TestFixture {
    MockSynthVoiceAllocator* allocator; // non-owning pointer; tied to processor lifetime
    std::unique_ptr<midi::StreamProcessor> processor;
    
    explicit TestFixture(uint8_t channel = 0) {
        auto allocatorPtr = std::make_unique<MockSynthVoiceAllocator>();
        allocator = allocatorPtr.get(); // Keep raw pointer for test access
        processor = std::make_unique<midi::StreamProcessor>(std::move(allocatorPtr), channel);
    }
    
    midi::StreamProcessor& getProcessor() {
        return *processor;
    }
};

// Helper function to send a complete Note On message
void sendNoteOnMessage(midi::StreamProcessor& processor, uint8_t channel, uint8_t note, uint8_t velocity) {
    uint8_t statusByte = 0x90 | channel; // Note On command + channel
    processor.process(statusByte);
    processor.process(note);
    processor.process(velocity);
}

void test_noteOn_should_allocateASynthVoice(void) {
    // Arrange
    TestFixture fixture(0); // Channel 0
    
    // Act - Send a MIDI Note On message: 0x90 (Note On, Channel 0), 0x40 (E4), 0x7F (Max velocity)
    sendNoteOnMessage(fixture.getProcessor(), 0, 0x40, 0x7F);
    
    // Assert - Verify voice allocator was called correctly
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, fixture.allocator->lastQueriedMidiNote, "Should query voice for MIDI note 0x40 (E4)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, fixture.allocator->voiceForCallCount, "voiceFor should be called exactly once");
    
    // Assert - Verify the voice that was allocated had trigger called
    MockSynth* voice = fixture.allocator->getLastAllocatedVoice();
    TEST_ASSERT_NOT_NULL_MESSAGE(voice, "Should have allocated a voice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, voice->triggerCallCount, "trigger should be called exactly once");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 329.628f, voice->lastTriggerFrequency, "Should trigger with E4 frequency (MIDI note 0x40)");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 1.0f, voice->lastTriggerVolume, "Should trigger with max volume");
    
    // Assert - Verify synth methods that should not be called
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, voice->releaseCallCount, "release should not be called for Note On");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, voice->setTimbreCallCount, "setTimbre should not be called for basic Note On");
}

void test_noteOn_shouldIgnoreWrongChannel(void) {
    // Arrange
    TestFixture fixture(1); // Listen to channel 1
    
    // Act - Send a MIDI Note On message on channel 0
    sendNoteOnMessage(fixture.getProcessor(), 0, 0x40, 0x7F);
    
    // Assert - Should ignore the message since it's on the wrong channel
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocator->voiceForCallCount, "voiceFor should not be called for wrong channel");
}

void test_noteOn_shouldRespondToCorrectChannel(void) {
    // Arrange
    TestFixture fixture(1); // Listen to channel 1
    
    // Act - Send a MIDI Note On message on channel 1
    sendNoteOnMessage(fixture.getProcessor(), 1, 0x40, 0x7F);
    
    // Assert - Should respond to the message since it's on the correct channel
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, fixture.allocator->lastQueriedMidiNote, "Should query voice for MIDI note 0x40 (E4) on correct channel");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, fixture.allocator->voiceForCallCount, "voiceFor should be called exactly once");
}

void test_runningStatus_shouldSendMultipleNotesWithoutRepeatingStatusByte(void) {
    // Arrange
    TestFixture fixture(0); // Channel 0
    
    // Act - Send a complete Note On message, then two more notes using running status
    // First message: 0x90 (Note On, Channel 0), 0x40 (E4), 0x7F (Max velocity)
    fixture.getProcessor().process(0x90);
    fixture.getProcessor().process(0x40);
    fixture.getProcessor().process(0x7F);
    
    // Running status: just data bytes for the next two notes
    // Second note: 0x41 (C#), 0x7F (Max velocity) - no status byte
    fixture.getProcessor().process(0x41);
    fixture.getProcessor().process(0x7F);
    
    // Third note: 0x42 (D), 0x7F (Max velocity) - no status byte  
    fixture.getProcessor().process(0x42);
    fixture.getProcessor().process(0x7F);
    
    // Assert - All three notes should have called voiceFor
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, fixture.allocator->voiceForCallCount, "Should call voiceFor for all three notes");
}

void test_runningStatus_shouldBeInterruptedByNewStatusByte(void) {
    // Arrange
    TestFixture fixture(0); // Channel 0
    
    // Act - Send a Note On, then interrupt with a new status byte before completion
    fixture.getProcessor().process(0x90); // Note On status
    fixture.getProcessor().process(0x40); // Note number (E4)
    // Before sending velocity, interrupt with a new status byte
    fixture.getProcessor().process(0x80); // Note Off status - should clear running status
    fixture.getProcessor().process(0x41); // Note number for Note Off
    fixture.getProcessor().process(0x7F); // Velocity for Note Off
    
    // Now send data bytes that should be interpreted as running status Note Off
    fixture.getProcessor().process(0x42); // This should be interpreted as another Note Off note
    fixture.getProcessor().process(0x7F);
    
    // Assert - The Note Off and running status Note Off should both call voiceFor (incomplete Note On should not)
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, fixture.allocator->voiceForCallCount, "Note Off and running status Note Off should both call voiceFor");
}

void test_noteOff_shouldReleaseAllocatedVoice(void) {
    // Arrange
    TestFixture fixture(0); // Channel 0

    // First allocate a voice with Note On
    sendNoteOnMessage(fixture.getProcessor(), 0, 0x40, 0x7F);
    
    // Act - Send a MIDI Note Off message: 0x80 (Note Off, Channel 0), 0x40 (E4), 0x7F (velocity)
    fixture.getProcessor().process(0x80); // Note Off status
    fixture.getProcessor().process(0x40); // Note number (E4)
    fixture.getProcessor().process(0x7F); // Release velocity
    
    // Assert - Voice should have release called
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, fixture.allocator->voiceForCallCount, "voiceFor should be called twice (Note On + Note Off)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, fixture.allocator->lastQueriedMidiNote, "Should query the correct MIDI note");
    
    // Check that the allocated voice had release called
    MockSynth* voice = fixture.allocator->getLastAllocatedVoice();
    TEST_ASSERT_NOT_NULL_MESSAGE(voice, "Should have allocated a voice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, voice->releaseCallCount, "release should be called exactly once");
}

void test_noteOnZeroVelocity_shouldReleaseAllocatedVoice(void) {
    // Arrange
    TestFixture fixture(0); // Channel 0

    // First allocate a voice with Note On
    sendNoteOnMessage(fixture.getProcessor(), 0, 0x40, 0x7F);

    // Act - Send a MIDI Note On message for the same channel and note, 0 velocity
    sendNoteOnMessage(fixture.getProcessor(), 0, 0x40, 0x00);
    
    // Assert - Voice should have release called
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, fixture.allocator->voiceForCallCount, "voiceFor should be called twice (Note On + Note On with zero velocity)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x40, fixture.allocator->lastQueriedMidiNote, "Should query the correct MIDI note");
    
    // Check that the allocated voice had release called
    MockSynth* voice = fixture.allocator->getLastAllocatedVoice();
    TEST_ASSERT_NOT_NULL_MESSAGE(voice, "Should have allocated a voice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, voice->releaseCallCount, "release should be called exactly once");
}

void test_statusByteInterruption_shouldDiscardPartialMessage(void) {
    // Arrange
    TestFixture fixture(0); // Channel 0
    
    // Start a Note On but don't complete it
    fixture.getProcessor().process(0x90); // Note On status
    fixture.getProcessor().process(0x40); // Note number (E4)
    // Missing velocity - message incomplete
    
    // Send a Program Change message (which interrupts and has only 1 data byte)
    fixture.getProcessor().process(0xC0); // Program Change status 
    fixture.getProcessor().process(0x05); // Program number
    
    // Now send what would be running status data for Program Change
    fixture.getProcessor().process(0x41); // Should be interpreted as another Program Change
    
    // Assert - The incomplete Note On should never trigger a voice
    // Program Change messages should not call voiceFor
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fixture.allocator->voiceForCallCount, "Incomplete Note On and Program Change should not call voiceFor");
    
    // Verify the voice for note 0x40 was never triggered or released
    MockSynth* voice = fixture.allocator->getVoice(0x40);
    TEST_ASSERT_NOT_NULL_MESSAGE(voice, "Voice should exist");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, voice->triggerCallCount, "Voice should not be triggered by incomplete Note On");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, voice->releaseCallCount, "Voice should not be released by incomplete Note On");
}

// TODO test system real-time messages interrupting a partial message (should resume the partial message)
// TODO test system common bytes
// TODO test system real-time bytes
// TODO test system exclusive messages
// TODO test polyphonic aftertouch messages (setTimbre 0f..1f)
// TODO test control change (volume for now, log others)
// TODO test program change (log program number)
// TODO test channel aftertouch
// TODO test pitch bend messages
// TODO test channel mode messages

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_noteOn_should_allocateASynthVoice);
    RUN_TEST(test_noteOn_shouldIgnoreWrongChannel);
    RUN_TEST(test_noteOn_shouldRespondToCorrectChannel);
    RUN_TEST(test_runningStatus_shouldSendMultipleNotesWithoutRepeatingStatusByte);
    RUN_TEST(test_runningStatus_shouldBeInterruptedByNewStatusByte);
    RUN_TEST(test_noteOff_shouldReleaseAllocatedVoice);
    RUN_TEST(test_noteOnZeroVelocity_shouldReleaseAllocatedVoice);
    RUN_TEST(test_statusByteInterruption_shouldDiscardPartialMessage);
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