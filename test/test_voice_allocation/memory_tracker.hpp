#pragma once

#include <cstdlib>
#include <cstddef>

/**
 * @brief Memory allocation tracker for testing real-time safety
 *
 * Verifies that no dynamic memory allocations occur during critical real-time
 * code. Define ENABLE_MEMORY_TRACKING before including this header in exactly
 * one translation unit of a test program; that replaces the global new/delete
 * operators with counting versions. (Because those operators are global, only
 * one TU per test binary may define ENABLE_MEMORY_TRACKING.)
 */
class MemoryTracker {
public:
    static void resetCounters() {
        allocationCount = 0;
        deallocationCount = 0;
        trackingEnabled = false;
    }

    static void startTracking() {
        allocationCount = 0;
        deallocationCount = 0;
        trackingEnabled = true;
    }

    static void stopTracking() {
        trackingEnabled = false;
    }

    static int getAllocationCount() { return allocationCount; }
    static int getDeallocationCount() { return deallocationCount; }
    static bool isTrackingEnabled() { return trackingEnabled; }

    // Called by the overridden new/delete operators
    static void recordAllocation() { if (trackingEnabled) allocationCount++; }
    static void recordDeallocation() { if (trackingEnabled) deallocationCount++; }

private:
    static inline int allocationCount = 0;
    static inline int deallocationCount = 0;
    static inline bool trackingEnabled = false;
};

// Override global new/delete operators for tracking
#ifdef ENABLE_MEMORY_TRACKING

void* operator new(size_t size) {
    MemoryTracker::recordAllocation();
    return malloc(size);
}

void* operator new[](size_t size) {
    MemoryTracker::recordAllocation();
    return malloc(size);
}

void operator delete(void* ptr) noexcept {
    MemoryTracker::recordDeallocation();
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    MemoryTracker::recordDeallocation();
    free(ptr);
}

// C++14 sized delete operators
void operator delete(void* ptr, size_t /*size*/) noexcept {
    MemoryTracker::recordDeallocation();
    free(ptr);
}

void operator delete[](void* ptr, size_t /*size*/) noexcept {
    MemoryTracker::recordDeallocation();
    free(ptr);
}

#endif // ENABLE_MEMORY_TRACKING

/**
 * @brief RAII helper for scoped memory allocation tracking
 *
 * Usage:
 *   {
 *     ScopedMemoryTracker tracker;
 *     // ... code that should not allocate memory ...
 *     TEST_ASSERT_EQUAL_INT(0, tracker.getAllocationCount());
 *   }
 */
class ScopedMemoryTracker {
public:
    ScopedMemoryTracker() { MemoryTracker::startTracking(); }
    ~ScopedMemoryTracker() { MemoryTracker::stopTracking(); }

    int getAllocationCount() const { return MemoryTracker::getAllocationCount(); }
    int getDeallocationCount() const { return MemoryTracker::getDeallocationCount(); }
};

/**
 * @brief Test macro to verify no heap allocations occur in a code block
 *
 * Usage:
 *   TEST_NO_HEAP_ALLOCATIONS({
 *     // ... code that should not allocate memory ...
 *   });
 */
#define TEST_NO_HEAP_ALLOCATIONS(code_block) do { \
    ScopedMemoryTracker tracker; \
    code_block \
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, tracker.getAllocationCount(), \
        "Code block should not allocate heap memory"); \
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, tracker.getDeallocationCount(), \
        "Code block should not deallocate heap memory"); \
} while(0)
