#include <unity.h>
#include <adsr_envelope.hpp>

// ADSR timing: each stage should take the time it was set to, including when
// the sustain level changes after the times were set, and when a note is
// released before it reaches sustain.

static constexpr float SAMPLE_RATE = 1000.0f;  // 1 sample per millisecond

using synth::AdsrEnvelope;
using Phase = AdsrEnvelope::Phase;

void setUp(void) {}
void tearDown(void) {}

// Run until the envelope leaves `phase` (or a limit); return the samples spent in it.
static int samplesIn(AdsrEnvelope& env, Phase phase, int limit = 100000) {
    int n = 0;
    while (env.getPhase() == phase && n < limit) {
        env.nextSample();
        ++n;
    }
    return n;
}

void test_stage_times(void) {
    AdsrEnvelope env(SAMPLE_RATE);
    env.setParameters(0.010f, 0.100f, 0.5f, 0.200f);
    env.trigger();
    TEST_ASSERT_INT_WITHIN(1, 10, samplesIn(env, Phase::ATTACK));
    TEST_ASSERT_INT_WITHIN(1, 100, samplesIn(env, Phase::DECAY));
    env.release();
    TEST_ASSERT_INT_WITHIN(1, 200, samplesIn(env, Phase::RELEASE));
}

// The decay rate depends on the sustain level, so changing only the sustain
// level must still give a decay that takes the decay time.
void test_sustain_change_keeps_decay_time(void) {
    AdsrEnvelope env(SAMPLE_RATE);
    env.setParameters(0.001f, 0.100f, 0.9f, 0.100f);
    env.setSustainLevel(0.1f);
    env.trigger();
    samplesIn(env, Phase::ATTACK);
    TEST_ASSERT_INT_WITHIN(1, 100, samplesIn(env, Phase::DECAY));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, env.getLevel());
}

// Released during decay with zero sustain: the release must still fall to
// silence over the release time, not hang at the level it was released from.
void test_early_release_with_zero_sustain_finishes(void) {
    AdsrEnvelope env(SAMPLE_RATE);
    env.setParameters(0.001f, 0.200f, 0.0f, 0.050f);
    env.trigger();
    samplesIn(env, Phase::ATTACK);
    for (int i = 0; i < 50; ++i) env.nextSample();  // a quarter of the way through decay
    TEST_ASSERT_TRUE(env.getLevel() > 0.5f);
    env.release();
    TEST_ASSERT_INT_WITHIN(1, 50, samplesIn(env, Phase::RELEASE));
    TEST_ASSERT_FALSE(env.isActive());
}

// Released during attack: the release time is measured from the current level.
void test_early_release_during_attack(void) {
    AdsrEnvelope env(SAMPLE_RATE);
    env.setParameters(0.100f, 0.100f, 0.8f, 0.040f);
    env.trigger();
    for (int i = 0; i < 25; ++i) env.nextSample();  // level ~0.25, below sustain
    env.release();
    TEST_ASSERT_INT_WITHIN(1, 40, samplesIn(env, Phase::RELEASE));
}

// Changing the release time during a release continues from the current level.
void test_release_time_change_mid_release(void) {
    AdsrEnvelope env(SAMPLE_RATE);
    env.setParameters(0.001f, 0.001f, 1.0f, 1.000f);
    env.trigger();
    samplesIn(env, Phase::ATTACK);
    samplesIn(env, Phase::DECAY);
    env.release();
    for (int i = 0; i < 500; ++i) env.nextSample();  // halfway down
    env.setReleaseTime(0.010f);
    TEST_ASSERT_INT_WITHIN(1, 10, samplesIn(env, Phase::RELEASE));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_stage_times);
    RUN_TEST(test_sustain_change_keeps_decay_time);
    RUN_TEST(test_early_release_with_zero_sustain_finishes);
    RUN_TEST(test_early_release_during_attack);
    RUN_TEST(test_release_time_change_mid_release);
    return UNITY_END();
}
