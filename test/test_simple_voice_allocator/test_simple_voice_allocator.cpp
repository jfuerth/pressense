#include <unity.h>
#include <simple_voice_allocator.hpp>
#include <synth.hpp>
#include <memory>

// Enable memory tracking for this test suite
#define ENABLE_MEMORY_TRACKING
#include "memory_tracker.hpp"

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
    midi::Synth& voice1 = allocator.allocate(60); // Middle C
    midi::Synth& voice2 = allocator.allocate(60); // Middle C again

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
    midi::Synth& voice1 = allocator.allocate(60); // Middle C
    midi::Synth& voice2 = allocator.allocate(64); // E4
    midi::Synth& voice3 = allocator.allocate(67); // G4

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
    midi::Synth& voice1 = allocator.allocate(60); // Middle C
    midi::Synth& voice2 = allocator.allocate(64); // E4
    midi::Synth& voice3 = allocator.allocate(67); // G4 - should reuse one of the existing voices

    // Assert - Should have created exactly 2 voice instances (not 3)
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, TestSynth::instanceCounter, "Should only create 2 TestSynth instances when maxVoices is 2");

    // Assert - voice3 should be the same as either voice1 or voice2 (voice reuse)
    bool voice3IsReused = (&voice3 == &voice1) || (&voice3 == &voice2);
    TEST_ASSERT_TRUE_MESSAGE(voice3IsReused, "Third voice should reuse one of the existing voice instances");

    // Assert - voice1 and voice2 should still be different
    TEST_ASSERT_TRUE_MESSAGE(&voice1 != &voice2, "First two voices should still be different instances");

    // Act - Request the reused note again, should return the same instance
    midi::Synth& voice3Again = allocator.allocate(67); // G4 again
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice3, &voice3Again, "Requesting the same note should return the same reused voice");
}

void test_voiceFor_stolenVoice_shouldBeInactiveState(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(2, voiceFactory); // Only 2 voices available

    // Act - Trigger two voices and activate them
    midi::Synth& voice1 = allocator.allocate(60); // Middle C
    midi::Synth& voice2 = allocator.allocate(64); // E4
    voice1.trigger(261.63f, 0.8f); // Make voice1 active
    voice2.trigger(329.63f, 0.7f); // Make voice2 active

    // Verify both voices are active
    TEST_ASSERT_TRUE_MESSAGE(voice1.isActive(), "Voice1 should be active after trigger");
    TEST_ASSERT_TRUE_MESSAGE(voice2.isActive(), "Voice2 should be active after trigger");

    // Act - Request a third voice, which should steal one of the existing voices
    midi::Synth& voice3 = allocator.allocate(67); // G4 - will steal a voice

    // Assert - The stolen voice should be inactive (not in triggered state)
    TEST_ASSERT_FALSE_MESSAGE(voice3.isActive(), "Stolen voice should be inactive when reassigned to new note");

    // Assert - The stolen voice should have release() called to clean up previous state
    TestSynth* stolenVoice = static_cast<TestSynth*>(&voice3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, stolenVoice->releaseCallCount, "Stolen voice should have release() called to clean up state");

    // Assert - Voice should be ready for new use
    voice3.trigger(392.0f, 0.6f); // Should work without issues
    TEST_ASSERT_TRUE_MESSAGE(voice3.isActive(), "Stolen voice should work normally after being reassigned");
}

void test_voiceFor_shouldNotAllocateMemoryAfterConstruction(void) {
    // Arrange - Create allocator (this can allocate memory)
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(4, voiceFactory);

    // Act & Assert - Track memory during voice operations
    {
        ScopedMemoryTracker tracker;

        // These operations should not allocate any memory
        midi::Synth& voice1 = allocator.allocate(60); // Middle C
        midi::Synth& voice2 = allocator.allocate(64); // E4
        midi::Synth& voice3 = allocator.allocate(67); // G4
        midi::Synth& voice4 = allocator.allocate(72); // C5

        // Trigger voices (should not allocate)
        voice1.trigger(261.63f, 0.8f);
        voice2.trigger(329.63f, 0.7f);
        voice3.trigger(392.0f, 0.6f);
        voice4.trigger(523.25f, 0.9f);

        // Test voice reuse when exceeding maxVoices (should not allocate)
        midi::Synth& voice5 = allocator.allocate(76); // E5 - will steal a voice
        voice5.trigger(659.25f, 0.5f);

        // Same note requests (should not allocate)
        midi::Synth& voice1Again = allocator.allocate(60);
        midi::Synth& voice5Again = allocator.allocate(76);

        // Release voices (should not allocate)
        voice1.release();
        voice2.release();
        voice3.release();
        voice4.release();
        voice5.release();

        // Apply operations to all voices (should not allocate)
        allocator.forEachVoice([](midi::Synth& voice) {
            voice.setPitchBend(0.5f);
            voice.setVolume(0.7f);
        });

        // Assert - No memory allocations should have occurred
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, tracker.getAllocationCount(),
            "Voice operations should not allocate memory after construction");
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, tracker.getDeallocationCount(),
            "Voice operations should not deallocate memory during runtime");
    }
}

void test_voiceOperations_usingMacro_shouldNotAllocateMemory(void) {
    // Arrange - Create allocator (this can allocate memory)
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(3, voiceFactory);

    // Act & Assert - Use macro to verify no allocations
    TEST_NO_HEAP_ALLOCATIONS({
        // Basic voice allocation and operations
        midi::Synth& voice1 = allocator.allocate(60);
        midi::Synth& voice2 = allocator.allocate(64);
        midi::Synth& voice3 = allocator.allocate(67);

        // Voice operations
        voice1.trigger(261.63f, 0.8f);
        voice2.trigger(329.63f, 0.7f);
        voice3.trigger(392.0f, 0.6f);

        // Voice state queries
        bool active1 = voice1.isActive();
        bool active2 = voice2.isActive();
        bool active3 = voice3.isActive();

        // Voice reuse (should steal and release)
        midi::Synth& voice4 = allocator.allocate(72); // Should reuse voice1

        // Batch operations
        allocator.forEachVoice([](midi::Synth& voice) {
            voice.setPitchBend(0.25f);
        });

        // Repeated voice access
        midi::Synth& voice1Again = allocator.allocate(60);
        midi::Synth& voice4Again = allocator.allocate(72);

        // Test existingVoiceFor operations (should not allocate)
        midi::Synth* existing1 = allocator.findAllocated(60);
        midi::Synth* existing2 = allocator.findAllocated(64);
        midi::Synth* existing3 = allocator.findAllocated(67);
        midi::Synth* existing4 = allocator.findAllocated(72);
        midi::Synth* nonExisting = allocator.findAllocated(80); // Should be nullptr

        // Release operations
        voice1Again.release();
        voice2.release();
        voice3.release();
        voice4Again.release();
    });
}

void test_existingVoiceFor_shouldReturnNullptrForUnallocatedNote(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(4, voiceFactory);

    // Act & Assert - Should return nullptr for notes that haven't been allocated
    TEST_ASSERT_NULL_MESSAGE(allocator.findAllocated(60), "Should return nullptr for unallocated note");
    TEST_ASSERT_NULL_MESSAGE(allocator.findAllocated(64), "Should return nullptr for unallocated note");
    TEST_ASSERT_NULL_MESSAGE(allocator.findAllocated(67), "Should return nullptr for unallocated note");
}

void test_existingVoiceFor_shouldReturnVoiceForAllocatedNote(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(4, voiceFactory);

    // Act - Allocate some voices
    midi::Synth& voice1 = allocator.allocate(60); // Middle C
    midi::Synth& voice2 = allocator.allocate(64); // E4

    // Assert - existingVoiceFor should return the same voices
    midi::Synth* existingVoice1 = allocator.findAllocated(60);
    midi::Synth* existingVoice2 = allocator.findAllocated(64);

    TEST_ASSERT_NOT_NULL_MESSAGE(existingVoice1, "Should return voice for allocated note");
    TEST_ASSERT_NOT_NULL_MESSAGE(existingVoice2, "Should return voice for allocated note");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice1, existingVoice1, "Should return the same voice instance");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice2, existingVoice2, "Should return the same voice instance");

    // Assert - Still return nullptr for unallocated notes
    TEST_ASSERT_NULL_MESSAGE(allocator.findAllocated(67), "Should return nullptr for unallocated note");
}

void test_existingVoiceFor_shouldReturnNullptrAfterVoiceStolen(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(2, voiceFactory); // Only 2 voices

    // Act - Allocate 2 voices
    midi::Synth& voice1 = allocator.allocate(60); // Middle C
    midi::Synth& voice2 = allocator.allocate(64); // E4

    // Verify both voices are available via existingVoiceFor
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice1, allocator.findAllocated(60), "Voice 1 should be allocated");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice2, allocator.findAllocated(64), "Voice 2 should be allocated");

    voice1.trigger(261.63f, 0.8f); // Make voice1 active
    voice2.trigger(329.63f, 0.7f); // Make voice2 active

    // Act - Request a third voice, which should steal voice2 (round-robin from index 1)
    midi::Synth& voice3 = allocator.allocate(67); // G4 - will steal voice2

    // Assert - The voice1 (note 60) should still be available
    TEST_ASSERT_NOT_NULL_MESSAGE(allocator.findAllocated(60), "Voice1 should still be available");

    // Assert - The stolen note (64) should no longer have a voice
    TEST_ASSERT_NULL_MESSAGE(allocator.findAllocated(64), "Stolen note should return nullptr");

    // Assert - The new note should have a voice
    TEST_ASSERT_NOT_NULL_MESSAGE(allocator.findAllocated(67), "New note should have voice");

    // Assert - The new voice should be the same instance as the stolen one (voice2)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice2, &voice3, "New voice should be same instance as stolen voice");
}

void test_voiceFor_shouldPreferInactiveVoicesForReallocation(void) {
    // Arrange
    auto voiceFactory = []() { return std::make_unique<TestSynth>(); };
    midi::SimpleVoiceAllocator allocator(3, voiceFactory); // Only 3 voices available

    // Act - Allocate 3 voices and make the FIRST one inactive (not the second)
    midi::Synth& voice1 = allocator.allocate(60); // Middle C - will be inactive
    midi::Synth& voice2 = allocator.allocate(64); // E4 - will be active
    midi::Synth& voice3 = allocator.allocate(67); // G4 - will be active

    // Make voice1 inactive, voices 2&3 active
    voice1.trigger(261.63f, 0.8f); // Trigger then release (inactive)
    voice1.release();
    voice2.trigger(329.63f, 0.7f); // Active
    voice3.trigger(392.0f, 0.6f);  // Active

    // Verify states
    TEST_ASSERT_FALSE_MESSAGE(voice1.isActive(), "Voice1 should be inactive after release");
    TEST_ASSERT_TRUE_MESSAGE(voice2.isActive(), "Voice2 should be active");
    TEST_ASSERT_TRUE_MESSAGE(voice3.isActive(), "Voice3 should be active");

    // Act - Request a 4th voice
    // With round-robin (current implementation), this would steal voice2 (index 1)
    // But we want it to prefer the inactive voice1 (index 0) instead
    midi::Synth& voice4 = allocator.allocate(72); // C5 - should reuse voice1 (inactive), not voice2 (active)

    // Assert - The new voice should be the inactive voice1, not the active voice2
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&voice1, &voice4, "Should reuse inactive voice1, not active voice2");

    // Assert - The inactive voice should have release() called again to clean up
    TestSynth* reusedVoice = static_cast<TestSynth*>(&voice4);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, reusedVoice->releaseCallCount, "Reused voice should have release() called to clean up state");

    // Assert - Voice should be ready for new use
    voice4.trigger(523.25f, 0.5f);
    TEST_ASSERT_TRUE_MESSAGE(voice4.isActive(), "Reused voice should work normally after being reassigned");

    // Assert - Active voices should still be available and unchanged
    TEST_ASSERT_NOT_NULL_MESSAGE(allocator.findAllocated(64), "Active voice2 should still be allocated");
    TEST_ASSERT_NOT_NULL_MESSAGE(allocator.findAllocated(67), "Active voice3 should still be allocated");
    TEST_ASSERT_TRUE_MESSAGE(voice2.isActive(), "Voice2 should still be active");
    TEST_ASSERT_TRUE_MESSAGE(voice3.isActive(), "Voice3 should still be active");

    // Assert - The inactive note should no longer be allocated
    TEST_ASSERT_NULL_MESSAGE(allocator.findAllocated(60), "Previously inactive note should no longer be allocated");

    // Assert - The new note should be allocated
    TEST_ASSERT_NOT_NULL_MESSAGE(allocator.findAllocated(72), "New note should be allocated");
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_voiceFor_sameNoteTwice_shouldReturnSameInstance);
    RUN_TEST(test_voiceFor_differentNotes_shouldReturnDifferentInstances);
    RUN_TEST(test_voiceFor_exceedMaxVoices_shouldReuseVoices);
    RUN_TEST(test_voiceFor_stolenVoice_shouldBeInactiveState);
    RUN_TEST(test_voiceFor_shouldNotAllocateMemoryAfterConstruction);
    RUN_TEST(test_voiceOperations_usingMacro_shouldNotAllocateMemory);
    RUN_TEST(test_existingVoiceFor_shouldReturnNullptrForUnallocatedNote);
    RUN_TEST(test_existingVoiceFor_shouldReturnVoiceForAllocatedNote);
    RUN_TEST(test_existingVoiceFor_shouldReturnNullptrAfterVoiceStolen);
    RUN_TEST(test_voiceFor_shouldPreferInactiveVoicesForReallocation);
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
