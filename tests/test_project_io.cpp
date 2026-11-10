#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "gb/commands.h"
#include "gb/project_io.h"
#include "test.h"

using namespace gb;

namespace {

uint8_t buffer[kProjectBufferSize];

Project& sample() {
    static Project project;
    project = Project{};
    initProject(project);
    project.bpm = 133;
    project.swing = 62;
    project.drums[2].grid = Grid::Eighth;
    project.drums[2].length = 8;
    project.drums[2].velocity[1][3] = 99;
    project.sounds[5].decay = 17;
    project.instruments[1][4].noteCount = 1;
    project.instruments[1][4].notes[0] = {96, 48, 60, 90};
    project.scenes[3].pattern[2] = 5;
    project.song[0] = {3, 2};
    project.songLength = 1;
    return project;
}

Project& loaded() {
    static Project project;
    project = Project{};
    return project;
}

std::string tempDir() {
    char name[] = "/tmp/gbtestXXXXXX";
    return mkdtemp(name);
}

void corruptFile(const std::string& path) {
    FILE* file = fopen(path.c_str(), "r+b");
    fseek(file, 100, SEEK_SET);
    fputc(0x5A, file);
    fclose(file);
}

}  // namespace

TEST(projectSurvivesRoundTrip) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    CHECK_EQ(size > 0, true);
    Project& out = loaded();
    CHECK_EQ(deserializeProject(buffer, size, out) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
    CHECK_EQ(out.swing, 62);
    CHECK_EQ(out.drums[2].grid == Grid::Eighth, true);
    CHECK_EQ(out.drums[2].length, 8);
    CHECK_EQ(out.drums[2].velocity[1][3], 99);
    CHECK_EQ(out.sounds[5].decay, 17);
    CHECK_EQ(out.instruments[1][4].notes[0].startTick, 96);
    CHECK_EQ(out.instruments[1][4].notes[0].lengthTicks, 48);
    CHECK_EQ(out.scenes[3].pattern[2], 5);
    CHECK_EQ(out.song[0].repeats, 2);
    CHECK_EQ(out.songLength, 1);
}

TEST(serializeRefusesSmallBuffer) { CHECK_EQ(serializeProject(sample(), buffer, 10), 0u); }

TEST(detectsBadMagic) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[0] = 'X';
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::BadMagic, true);
}

TEST(detectsUnknownVersion) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[4] = 99;
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::BadVersion, true);
}

TEST(detectsTruncation) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    CHECK_EQ(deserializeProject(buffer, size - 1, loaded()) == LoadResult::BadSize, true);
    CHECK_EQ(deserializeProject(buffer, 3, loaded()) == LoadResult::BadSize, true);
}

TEST(detectsCorruption) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[size / 2] ^= 0x40;
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::BadChecksum, true);
}

TEST(refusesProjectThatFailsValidation) {
    Project bad = sample();
    bad.bpm = 0;
    const size_t size = serializeProject(bad, buffer, sizeof(buffer));
    CHECK_EQ(deserializeProject(buffer, size, loaded()) == LoadResult::Invalid, true);
}

TEST(failedLoadLeavesOutputUntouched) {
    const size_t size = serializeProject(sample(), buffer, sizeof(buffer));
    buffer[size / 2] ^= 0x40;
    Project& out = loaded();
    out.bpm = 77;
    deserializeProject(buffer, size, out);
    CHECK_EQ(out.bpm, 77);
}

TEST(fileRoundTrip) {
    const std::string path = tempDir() + "/p.gbp";
    CHECK_EQ(saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer)), true);
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(saveKeepsPreviousVersionAsBackup) {
    const std::string path = tempDir() + "/p.gbp";
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    Project& changed = sample();
    changed.bpm = 90;
    saveProjectFile(path.c_str(), changed, buffer, sizeof(buffer));
    corruptFile(path);
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::OkFromBackup, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(loadsBackupWhenInterruptedBeforeFinalRename) {
    const std::string path = tempDir() + "/p.gbp";
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    remove(path.c_str());
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::Ok, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(missingFileIsReported) {
    const std::string path = tempDir() + "/none.gbp";
    CHECK_EQ(loadProjectFile(path.c_str(), loaded(), buffer, sizeof(buffer)) == LoadResult::NotFound, true);
}

TEST(damagedPrimaryDoesNotReplaceGoodBackup) {
    const std::string path = tempDir() + "/p.gbp";
    Project& first = sample();
    first.bpm = 133;
    saveProjectFile(path.c_str(), first, buffer, sizeof(buffer));
    Project& second = sample();
    second.bpm = 90;
    saveProjectFile(path.c_str(), second, buffer, sizeof(buffer));
    corruptFile(path);
    Project& third = sample();
    third.bpm = 70;
    saveProjectFile(path.c_str(), third, buffer, sizeof(buffer));
    corruptFile(path);
    Project& out = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), out, buffer, sizeof(buffer)) == LoadResult::OkFromBackup, true);
    CHECK_EQ(out.bpm, 133);
}

TEST(damagedBackupWithoutPrimaryIsReported) {
    const std::string path = tempDir() + "/p.gbp";
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    saveProjectFile(path.c_str(), sample(), buffer, sizeof(buffer));
    remove(path.c_str());
    corruptFile(path + ".bak");
    CHECK_EQ(loadProjectFile(path.c_str(), loaded(), buffer, sizeof(buffer)) == LoadResult::BadChecksum, true);
}

TEST(refusesToSaveInvalidProject) {
    const std::string path = tempDir() + "/p.gbp";
    Project& bad = sample();
    bad.bpm = 0;
    CHECK_EQ(saveProjectFile(path.c_str(), bad, buffer, sizeof(buffer)), false);
    CHECK_EQ(loadProjectFile(path.c_str(), loaded(), buffer, sizeof(buffer)) == LoadResult::NotFound, true);
}

TEST(versionOnePrimaryBecomesLoadableBackupAfterMigration) {
    const std::string path = tempDir() + "/p.gbp";
    FILE* fixture = fopen("tests/fixtures/stage2-v1.gbp", "rb");
    CHECK_EQ(fixture != nullptr, true);
    if (!fixture) return;
    const size_t size = fread(buffer, 1, sizeof(buffer), fixture);
    fclose(fixture);
    FILE* primary = fopen(path.c_str(), "wb");
    CHECK_EQ(primary != nullptr, true);
    if (!primary) return;
    CHECK_EQ(fwrite(buffer, 1, size, primary), size);
    fclose(primary);
    auto& p = loaded();
    CHECK_EQ(loadProjectFile(path.c_str(), p, buffer, sizeof(buffer)), LoadResult::Ok);
    p.bpm = 90;
    CHECK_EQ(saveProjectFile(path.c_str(), p, buffer, sizeof(buffer)), true);
    corruptFile(path);
    CHECK_EQ(loadProjectFile(path.c_str(), p, buffer, sizeof(buffer)), LoadResult::OkFromBackup);
    CHECK_EQ(p.bpm, 133);
    CHECK_EQ(p.instruments[1][4].notes[0].pitch, 60);
}
