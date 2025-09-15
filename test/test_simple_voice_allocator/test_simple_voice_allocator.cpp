#include <unity.h>
#include <simple_voice_allocator.hpp>
#include <synth.hpp>
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

// Test implementation of Synth for voice allocator testing
class TestSynth : public midi::Synth {
public:
    static int instanceCounter; // Track number of instances created
    
    TestSynth() {
        instanceId = ++instanceCounter;
    }
    
    void trigger(float frequencyHz, float volume) override {
        lastTriggerFrequency = frequencyHz;
        lastTriggerVolume = volume;
        triggerCallCount++;
        isActiveState = true;
    }
    
    void release() override {
        releaseCallCount++;
        isActiveState = false;
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
        return isActiveState;
    }
    
    void setPitchBend(float bendAmount) override {
        lastPitchBend = bendAmount;
        setPitchBendCallCount++;
    }
    
    float getPitchBendRange() const override {
        getPitchBendRangeCallCount++;
        return pitchBendRange;
    }
    
    void setPitchBendRange(float semitones) override {
        pitchBendRange = semitones;
        setPitchBendRangeCallCount++;
    }
    
    // Test verification helpers
    int instanceId = 0;
    float lastTriggerFrequency = 0.0f;
    float lastTriggerVolume = 0.0f;
    float lastSetFrequency = 0.0f;
    float lastSetTimbre = 0.0f;
    float lastSetVolume = 0.0f;
    float lastPitchBend = 0.0f;
    float pitchBendRange = 2.0f;
    bool isActiveState = false;
    
    // Call counters
    int triggerCallCount = 0;
    int releaseCallCount = 0;
    int setFrequencyCallCount = 0;
    int setTimbreCallCount = 0;
    int setVolumeCallCount = 0;
    int setPitchBendCallCount = 0;
    mutable int getPitchBendRangeCallCount = 0;
    int setPitchBendRangeCallCount = 0;
    mutable int isActiveCallCount = 0;
};

// Static member definition
int TestSynth::instanceCounter = 0;

void setUp(void) {
    // Reset the instance counter before each test
    TestSynth::instanceCounter = 0;
}

void tearDown(void) {
    // Unity tearDown - currently not used, keeping for potential future use
}

void test_voiceFor_sameNoteTwice_shouldReturnSameInstance(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(8, voiceFactory);
    
    // Act - Request voice for the same MIDI note twice
    midi::Synth& voice1 = allocator.voiceFor(60); // Middle C
    midi::Synth& voice2 = allocator.voiceFor(60); // Middle C again
    
    // Assert - Should return the same instance
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice1, &voice2, "voiceFor() should return the same instance for the same MIDI note");
    
    // Verify all voices are pre-cached (optimization for real-time use)
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, TestSynth::instanceCounter, "All 8 TestSynth instances should be pre-cached");
    
    // Cast to TestSynth to verify the instance ID
    TestSynth* testVoice1 = static_cast<TestSynth*>(&voice1);
    TestSynth* testVoice2 = static_cast<TestSynth*>(&voice2);
    TEST_ASSERT_EQUAL_INT_MESSAGE(testVoice1->instanceId, testVoice2->instanceId, "Both references should point to the same TestSynth instance");
}

void test_voiceFor_differentNotes_shouldReturnDifferentInstances(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(8, voiceFactory);
    
    // Act - Request voices for different MIDI notes
    midi::Synth& voice1 = allocator.voiceFor(60); // Middle C
    midi::Synth& voice2 = allocator.voiceFor(64); // E4
    midi::Synth& voice3 = allocator.voiceFor(67); // G4
    
    // Assert - Should return different instances
    TEST_ASSERT_TRUE_MESSAGE(&voice1 != &voice2, "Different MIDI notes should return different voice instances");
    TEST_ASSERT_TRUE_MESSAGE(&voice1 != &voice3, "Different MIDI notes should return different voice instances");
    TEST_ASSERT_TRUE_MESSAGE(&voice2 != &voice3, "Different MIDI notes should return different voice instances");
    
    // Cast to TestSynth to verify different instance IDs
    TestSynth* testVoice1 = static_cast<TestSynth*>(&voice1);
    TestSynth* testVoice2 = static_cast<TestSynth*>(&voice2);
    TestSynth* testVoice3 = static_cast<TestSynth*>(&voice3);
    
    TEST_ASSERT_TRUE_MESSAGE(testVoice1->instanceId != testVoice2->instanceId, "Different notes should have different instance IDs");
    TEST_ASSERT_TRUE_MESSAGE(testVoice1->instanceId != testVoice3->instanceId, "Different notes should have different instance IDs");
    TEST_ASSERT_TRUE_MESSAGE(testVoice2->instanceId != testVoice3->instanceId, "Different notes should have different instance IDs");
}

void test_voiceFor_exceedMaxVoices_shouldReuseVoices(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(2, voiceFactory); // Only 2 voices available
    
    // Act - Request voices for 3 different MIDI notes (exceeding maxVoices)
    midi::Synth& voice1 = allocator.voiceFor(60); // Middle C
    midi::Synth& voice2 = allocator.voiceFor(64); // E4
    midi::Synth& voice3 = allocator.voiceFor(67); // G4 - should reuse one of the existing voices
    
    // Assert - Should have created exactly 2 voice instances (not 3)
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, TestSynth::instanceCounter, "Should only create 2 TestSynth instances when maxVoices is 2");
    
    // Assert - voice3 should be the same as either voice1 or voice2 (voice reuse)
    bool voice3IsReused = (&voice3 == &voice1) || (&voice3 == &voice2);
    TEST_ASSERT_TRUE_MESSAGE(voice3IsReused, "Third voice should reuse one of the existing voice instances");
    
    // Assert - voice1 and voice2 should still be different
    TEST_ASSERT_TRUE_MESSAGE(&voice1 != &voice2, "First two voices should still be different instances");
    
    // Act - Request the reused note again, should return the same instance
    midi::Synth& voice3Again = allocator.voiceFor(67); // G4 again
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice3, &voice3Again, "Requesting the same note should return the same reused voice");
}

void test_voiceFor_stolenVoice_shouldBeInactiveState(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(2, voiceFactory); // Only 2 voices available
    
    // Act - Trigger two voices and activate them
    midi::Synth& voice1 = allocator.voiceFor(60); // Middle C
    midi::Synth& voice2 = allocator.voiceFor(64); // E4
    voice1.trigger(261.63f, 0.8f); // Make voice1 active
    voice2.trigger(329.63f, 0.7f); // Make voice2 active
    
    // Verify both voices are active
    TEST_ASSERT_TRUE_MESSAGE(voice1.isActive(), "Voice1 should be active after trigger");
    TEST_ASSERT_TRUE_MESSAGE(voice2.isActive(), "Voice2 should be active after trigger");
    
    // Act - Request a third voice, which should steal one of the existing voices
    midi::Synth& voice3 = allocator.voiceFor(67); // G4 - will steal a voice
    
    // Assert - The stolen voice should be inactive (not in triggered state)
    TEST_ASSERT_FALSE_MESSAGE(voice3.isActive(), "Stolen voice should be inactive when reassigned to new note");
    
    // Assert - The stolen voice should have release() called to clean up previous state
    TestSynth* stolenVoice = static_cast<TestSynth*>(&voice3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stolenVoice->releaseCallCount, "Stolen voice should have release() called to clean up state");
    
    // Assert - Voice should be ready for new use
    voice3.trigger(392.0f, 0.6f); // Should work without issues
    TEST_ASSERT_TRUE_MESSAGE(voice3.isActive(), "Stolen voice should work normally after being reassigned");
}

// TODO implementation should prefer to reuse inactive voices before reallocating active ones

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_voiceFor_sameNoteTwice_shouldReturnSameInstance);
    RUN_TEST(test_voiceFor_differentNotes_shouldReturnDifferentInstances);
    RUN_TEST(test_voiceFor_exceedMaxVoices_shouldReuseVoices);
    RUN_TEST(test_voiceFor_stolenVoice_shouldBeInactiveState);
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
