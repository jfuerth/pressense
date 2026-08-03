#include <unity.h>
#include <midi_keyboard_controller.hpp>
#include <json.hpp>
#include <string>
#include <vector>

// KeyScanStats can serialise itself into a fixed buffer, bypassing nlohmann, so
// that emitting telemetry doesn't allocate. That path is only worth having if it
// produces the same report the nlohmann path does, so these tests parse its
// output and compare it field by field against to_json. They also pin down what
// happens when the buffer is too small, since the writer runs on an embedded
// target where an overrun would be silent corruption.

using Stats = midi::KeyScanStats<8>;

void setUp(void) {}
void tearDown(void) {}

static Stats makeStats() {
    Stats s;
    s.isCalibrated = true;
    s.calibrationCount = 10;
    s.noteOnThreshold = 2.0f;
    s.noteOffThreshold = 1.5f;
    for (uint8_t i = 0; i < 8; ++i) {
        s.readings[i] = static_cast<uint16_t>(1000 + i * 137);
        s.baselines[i] = 900.5f + i * 11.25f;
        s.ratios[i] = 1.0f + i * 0.375f;
        s.noteStates[i] = (i % 3) == 0;
        s.aftertouchValues[i] = static_cast<uint8_t>(i * 17);
    }
    return s;
}

static nlohmann::json parseDirect(const Stats& s) {
    std::vector<char> buf(Stats::JSON_CAPACITY);
    size_t len = s.writeJson(buf.data(), buf.size());
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "writeJson wrote nothing");
    TEST_ASSERT_EQUAL_MESSAGE(len, std::string(buf.data()).size(),
        "Returned length should match the NUL-terminated string");
    return nlohmann::json::parse(std::string(buf.data()));
}

// The direct output has to be valid JSON before anything else matters.
void test_directOutput_isValidJson(void) {
    Stats s = makeStats();
    nlohmann::json j = parseDirect(s);
    TEST_ASSERT_TRUE_MESSAGE(j.is_object(), "Output should be a JSON object");
    TEST_ASSERT_EQUAL_STRING("keyScan", j["type"].get<std::string>().c_str());
}

// Scalars must match the nlohmann rendering exactly.
void test_scalarFields_matchNlohmann(void) {
    Stats s = makeStats();
    nlohmann::json direct = parseDirect(s);
    nlohmann::json viaNlohmann = s;

    TEST_ASSERT_EQUAL_STRING(viaNlohmann["type"].get<std::string>().c_str(),
                             direct["type"].get<std::string>().c_str());
    TEST_ASSERT_EQUAL_MESSAGE(viaNlohmann["keyCount"].get<int>(),
                              direct["keyCount"].get<int>(), "keyCount");
    TEST_ASSERT_EQUAL_MESSAGE(viaNlohmann["isCalibrated"].get<bool>(),
                              direct["isCalibrated"].get<bool>(), "isCalibrated");
    TEST_ASSERT_EQUAL_MESSAGE(viaNlohmann["calibrationCount"].get<int>(),
                              direct["calibrationCount"].get<int>(), "calibrationCount");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, viaNlohmann["noteOnThreshold"].get<float>(),
                                     direct["noteOnThreshold"].get<float>(), "noteOnThreshold");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, viaNlohmann["noteOffThreshold"].get<float>(),
                                     direct["noteOffThreshold"].get<float>(), "noteOffThreshold");
}

// Integer arrays must be exact; float arrays match to the writer's three decimals.
void test_arrays_matchNlohmann(void) {
    Stats s = makeStats();
    nlohmann::json direct = parseDirect(s);
    nlohmann::json viaNlohmann = s;

    for (const char* key : {"readings", "baselines", "ratios",
                            "noteStates", "aftertouchValues"}) {
        TEST_ASSERT_TRUE_MESSAGE(direct[key].is_array(), key);
        TEST_ASSERT_EQUAL_MESSAGE(8, direct[key].size(), key);
    }

    for (uint8_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_MESSAGE(viaNlohmann["readings"][i].get<int>(),
                                  direct["readings"][i].get<int>(), "readings");
        TEST_ASSERT_EQUAL_MESSAGE(viaNlohmann["aftertouchValues"][i].get<int>(),
                                  direct["aftertouchValues"][i].get<int>(), "aftertouchValues");
        TEST_ASSERT_EQUAL_MESSAGE(viaNlohmann["noteStates"][i].get<bool>(),
                                  direct["noteStates"][i].get<bool>(), "noteStates");
        // Three decimal places, so half of the last digit is the worst case.
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, viaNlohmann["baselines"][i].get<float>(),
                                         direct["baselines"][i].get<float>(), "baselines");
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.0005f, viaNlohmann["ratios"][i].get<float>(),
                                         direct["ratios"][i].get<float>(), "ratios");
    }
}

// The declared capacity has to actually be enough, or the report silently
// truncates on the device. Check with values that produce the longest output.
void test_capacity_isSufficientForWorstCase(void) {
    Stats s;
    s.isCalibrated = true;
    s.calibrationCount = 65535;
    s.noteOnThreshold = -999999.999f;
    s.noteOffThreshold = -999999.999f;
    for (uint8_t i = 0; i < 8; ++i) {
        s.readings[i] = 65535;
        s.baselines[i] = -999999.999f;
        s.ratios[i] = -999999.999f;
        s.noteStates[i] = false;      // "false" is longer than "true"
        s.aftertouchValues[i] = 255;
    }

    std::vector<char> buf(Stats::JSON_CAPACITY);
    size_t len = s.writeJson(buf.data(), buf.size());
    TEST_ASSERT_TRUE_MESSAGE(len < Stats::JSON_CAPACITY - 1,
        "Worst-case output should fit inside JSON_CAPACITY with room to spare");
    // Still parseable, i.e. not truncated.
    nlohmann::json j = nlohmann::json::parse(std::string(buf.data()));
    TEST_ASSERT_EQUAL(8, j["readings"].size());
}

// A buffer that is too small must truncate within bounds rather than overrun.
void test_undersizedBuffer_truncatesSafely(void) {
    Stats s = makeStats();
    const size_t small = 64;
    std::vector<char> buf(small + 16, '\xAA');   // guard bytes past the end

    size_t len = s.writeJson(buf.data(), small);
    TEST_ASSERT_TRUE_MESSAGE(len < small, "Length must stay inside the buffer");
    TEST_ASSERT_EQUAL_MESSAGE('\0', buf[len], "Output must be NUL terminated");
    for (size_t i = small; i < buf.size(); ++i) {
        TEST_ASSERT_EQUAL_MESSAGE('\xAA', buf[i], "Wrote past the end of the buffer");
    }
}

// Non-finite values would make the float-to-integer cast undefined, so they are
// coerced rather than emitted as invalid JSON tokens like "nan".
void test_nonFiniteValues_emitValidJson(void) {
    Stats s = makeStats();
    s.baselines[0] = std::numeric_limits<float>::quiet_NaN();
    s.baselines[1] = std::numeric_limits<float>::infinity();
    s.ratios[2] = -std::numeric_limits<float>::infinity();

    nlohmann::json j = parseDirect(s);   // parse failure would throw
    TEST_ASSERT_TRUE(j["baselines"][0].is_number());
    TEST_ASSERT_TRUE(j["baselines"][1].is_number());
    TEST_ASSERT_TRUE(j["ratios"][2].is_number());
}

void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    RUN_TEST(test_directOutput_isValidJson);
    RUN_TEST(test_scalarFields_matchNlohmann);
    RUN_TEST(test_arrays_matchNlohmann);
    RUN_TEST(test_capacity_isSufficientForWorstCase);
    RUN_TEST(test_undersizedBuffer_truncatesSafely);
    RUN_TEST(test_nonFiniteValues_emitValidJson);
    UNITY_END();
}

extern "C" {
#ifdef PLATFORM_NATIVE
int main(int argc, char** argv) {
    RUN_UNITY_TESTS();
    return 0;
}
#endif
}
