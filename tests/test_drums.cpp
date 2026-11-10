#include <cmath>
#include <vector>

#include "gb/drums.h"
#include "test.h"

using namespace gb;

namespace {

constexpr uint32_t kRate = 32000;
enum Lane { Kick, Snare, Clap, ClosedHat, OpenHat, Tom, Rim, Perc };

std::vector<float> play(uint8_t lane, const DrumSound& sound = DrumSound{}, uint32_t frames = kRate) {
    DrumRack rack(kRate);
    rack.trigger(lane, 127, sound);
    std::vector<float> output(frames);
    for (float& sample : output) sample = rack.render();
    return output;
}

float peak(const std::vector<float>& samples) {
    float highest = 0.0f;
    for (float sample : samples) highest = std::max(highest, std::fabs(sample));
    return highest;
}

int zeroCrossings(const std::vector<float>& samples, size_t count) {
    int crossings = 0;
    for (size_t i = 1; i < count && i < samples.size(); ++i)
        crossings += (samples[i - 1] < 0.0f) != (samples[i] < 0.0f);
    return crossings;
}

// Last frame still above a small level.
size_t ringLength(const std::vector<float>& samples) {
    size_t last = 0;
    for (size_t i = 0; i < samples.size(); ++i)
        if (std::fabs(samples[i]) > 0.01f) last = i;
    return last;
}

}  // namespace

TEST(everyLaneMakesSound) {
    for (uint8_t lane = 0; lane < 8; ++lane) CHECK_EQ(peak(play(lane)) > 0.05f, true);
}

TEST(silentWithoutTrigger) {
    DrumRack rack(kRate);
    CHECK_EQ(rack.render() == 0.0f, true);
}

TEST(everyLaneFadesOut) {
    for (uint8_t lane = 0; lane < 8; ++lane) {
        const auto samples = play(lane, DrumSound{}, kRate * 3);
        CHECK_EQ(std::fabs(samples.back()) < 0.01f, true);
    }
}

TEST(hatIsBrighterThanKick) {
    CHECK_EQ(zeroCrossings(play(ClosedHat), 1000) > zeroCrossings(play(Kick), 1000) * 5, true);
}

TEST(openHatRingsLongerThanClosed) { CHECK_EQ(ringLength(play(OpenHat)) > ringLength(play(ClosedHat)) * 2, true); }

TEST(tuneRaisesPitch) {
    DrumSound low, high;
    low.tune = 20;
    high.tune = 110;
    CHECK_EQ(zeroCrossings(play(Tom, high), 4000) > zeroCrossings(play(Tom, low), 4000), true);
}

TEST(decayLengthensSound) {
    DrumSound shortSound, longSound;
    shortSound.decay = 20;
    longSound.decay = 110;
    CHECK_EQ(ringLength(play(Snare, longSound)) > ringLength(play(Snare, shortSound)), true);
}

TEST(toneChangesNoiseBrightness) {
    DrumSound dark, bright;
    dark.tone = 10;
    bright.tone = 120;
    CHECK_EQ(zeroCrossings(play(Snare, bright), 2000) > zeroCrossings(play(Snare, dark), 2000), true);
}

TEST(levelZeroIsSilent) {
    DrumSound sound;
    sound.level = 0;
    CHECK_EQ(peak(play(Kick, sound)) == 0.0f, true);
}

TEST(lowerVelocityIsQuieter) {
    DrumRack soft(kRate), loud(kRate);
    soft.trigger(Kick, 40, DrumSound{});
    loud.trigger(Kick, 127, DrumSound{});
    float softPeak = 0.0f, loudPeak = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        softPeak = std::max(softPeak, std::fabs(soft.render()));
        loudPeak = std::max(loudPeak, std::fabs(loud.render()));
    }
    CHECK_EQ(loudPeak > softPeak, true);
}

TEST(retriggerRestartsTheVoice) {
    DrumRack rack(kRate);
    rack.trigger(Snare, 127, DrumSound{});
    for (int i = 0; i < 30000; ++i) rack.render();
    rack.trigger(Snare, 127, DrumSound{});
    float highest = 0.0f;
    for (int i = 0; i < 1000; ++i) highest = std::max(highest, std::fabs(rack.render()));
    CHECK_EQ(highest > 0.05f, true);
}

TEST(lanesPlayTogether) {
    DrumRack rack(kRate);
    rack.trigger(Kick, 127, DrumSound{});
    rack.trigger(ClosedHat, 127, DrumSound{});
    float highest = 0.0f;
    for (int i = 0; i < 2000; ++i) highest = std::max(highest, std::fabs(rack.render()));
    CHECK_EQ(highest > peak(play(ClosedHat, DrumSound{}, 2000)), true);
}

TEST(outOfRangeLaneIsIgnored) {
    DrumRack rack(kRate);
    rack.trigger(200, 127, DrumSound{});
    CHECK_EQ(rack.render() == 0.0f, true);
}
