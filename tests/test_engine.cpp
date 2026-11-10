#include <cstdlib>
#include <vector>

#include "gb/commands.h"
#include "gb/engine.h"
#include "test.h"

using namespace gb;

namespace {

constexpr uint32_t kRate = 32000;
constexpr uint32_t kBlock = 256;

Project& fresh() {
    static Project project;
    project = Project{};
    initProject(project);
    return project;
}

Command make(CommandType type, uint8_t a = 0, uint8_t b = 0) {
    Command command;
    command.type = type;
    command.a = a;
    command.b = b;
    return command;
}

// Mono copy of the left channel; also checks that both channels match.
std::vector<int16_t> run(Engine& engine, uint32_t frames, int* channelMismatches = nullptr) {
    std::vector<int16_t> left;
    int16_t block[kBlock * 2];
    for (uint32_t done = 0; done < frames; done += kBlock) {
        engine.render(block, kBlock);
        for (uint32_t i = 0; i < kBlock; ++i) {
            left.push_back(block[i * 2]);
            if (channelMismatches && block[i * 2] != block[i * 2 + 1]) ++*channelMismatches;
        }
    }
    return left;
}

int peak(const std::vector<int16_t>& samples, size_t from = 0, size_t to = SIZE_MAX) {
    int highest = 0;
    for (size_t i = from; i < samples.size() && i < to; ++i) highest = std::max(highest, std::abs(samples[i]));
    return highest;
}

size_t firstSound(const std::vector<int16_t>& samples) {
    for (size_t i = 0; i < samples.size(); ++i)
        if (samples[i] != 0) return i;
    return SIZE_MAX;
}

}  // namespace

TEST(silentWhenStopped) {
    Engine engine(fresh(), kRate);
    CHECK_EQ(peak(run(engine, 4096)), 0);
}

TEST(stepPlaysAtItsSampleInsideBlock) {
    Project& project = fresh();
    project.drums[0].grid = Grid::ThirtySecond;
    project.drums[0].velocity[3][1] = 127;  // Tick 12 is frame 2000 at 120 BPM.
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    const auto samples = run(engine, 4096);
    CHECK_EQ(firstSound(samples), 2000u);
}

TEST(previewPlaysWhileStopped) {
    Engine engine(fresh(), kRate);
    engine.handle(make(CommandType::Preview, 1, 127));
    CHECK_EQ(peak(run(engine, 2048)) > 1000, true);
}

TEST(invalidPreviewIsIgnored) {
    Engine engine(fresh(), kRate);
    engine.handle(make(CommandType::Preview, 8, 127));  // Lane 8 does not exist.
    CHECK_EQ(peak(run(engine, 2048)), 0);
}

TEST(stopSilencesTheSequence) {
    Project& project = fresh();
    project.drums[0].length = 4;
    project.drums[0].velocity[0][0] = 127;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    run(engine, 512);
    CHECK_EQ(engine.playing(), true);
    engine.handle(make(CommandType::Stop));
    run(engine, 128000);  // Let the kick fade out.
    CHECK_EQ(engine.playing(), false);
    CHECK_EQ(peak(run(engine, 4096)), 0);
}

TEST(editsApplyToThePlayingProject) {
    Project& project = fresh();
    project.drums[0].length = 4;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    run(engine, 512);
    Command command;
    command.type = CommandType::SetStep;
    command.b = 1;
    command.c = 1;
    command.value = 127;
    engine.handle(command);
    CHECK_EQ(project.drums[0].velocity[1][1], 127);
    CHECK_EQ(peak(run(engine, 16000)) > 1000, true);
}

TEST(outputNeverClipsWithAllLanes) {
    Project& project = fresh();
    for (int lane = 0; lane < kDrumLanes; ++lane) project.drums[0].velocity[lane][0] = 127;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    const auto samples = run(engine, 8192);
    CHECK_EQ(peak(samples) > 3000, true);
    CHECK_EQ(peak(samples) <= 32767, true);
}

TEST(channelsMatch) {
    Project& project = fresh();
    project.drums[0].velocity[1][0] = 127;
    Engine engine(project, kRate);
    engine.handle(make(CommandType::Play));
    int mismatches = 0;
    run(engine, 4096, &mismatches);
    CHECK_EQ(mismatches, 0);
}

TEST(sceneTickAdvancesWhilePlaying) {
    Engine engine(fresh(), kRate);
    engine.handle(make(CommandType::Play));
    run(engine, 16128);  // 63 blocks is about 96 ticks at 120 BPM.
    CHECK_EQ(engine.sceneTick() >= 96u, true);
    CHECK_EQ(engine.sceneTick() < 100u, true);
}
