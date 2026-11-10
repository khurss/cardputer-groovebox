#include "gb/commands.h"
#include "gb/project_io.h"
#include "gb/validate.h"
#include "test.h"
using namespace gb;
namespace {
Project& freshInstrumentProject() {
    static Project p;
    p = Project{};
    initProject(p);
    return p;
}
Command noteCommand(CommandType type, uint8_t index = 0) {
    Command c;
    c.type = type;
    c.c = index;
    c.note = {96, 48, 60, 100};
    return c;
}
}  // namespace
TEST(instrumentNoteCommandsRespectCapacityAndIndices) {
    auto& p = freshInstrumentProject();
    auto c = noteCommand(CommandType::AddNote);
    for (int i = 0; i < 65; ++i)
        applyEdit(p, c);
    CHECK_EQ(p.instruments[0][0].noteCount, 64);
    c = noteCommand(CommandType::SetNote, 63);
    c.note.pitch = 72;
    applyEdit(p, c);
    CHECK_EQ(p.instruments[0][0].notes[63].pitch, 72);
    c = noteCommand(CommandType::DeleteNote, 63);
    applyEdit(p, c);
    applyEdit(p, c);
    CHECK_EQ(p.instruments[0][0].noteCount, 63);
    c = noteCommand(CommandType::SetNote, 63);
    c.note.pitch = 30;
    applyEdit(p, c);
    CHECK_EQ(p.instruments[0][0].notes[63].pitch, 0);
}
TEST(instrumentValidationRejectsInvalidNotesAndSounds) {
    auto& p = freshInstrumentProject();
    auto c = noteCommand(CommandType::AddNote);
    c.note.lengthTicks = 0;
    CHECK_EQ(isValidCommand(c), false);
    applyEdit(p, c);
    CHECK_EQ(p.instruments[0][0].noteCount, 0);
    c.note.lengthTicks = 1;
    c.note.startTick = 1536;
    CHECK_EQ(isValidCommand(c), false);
    c.note.startTick = 1535;
    c.note.velocity = 0;
    CHECK_EQ(isValidCommand(c), false);
    p.sounds[7].tone = 128;
    CHECK_EQ(isValidProject(p), false);
    p.sounds[7].tone = 127;
    p.drums[3].velocity[2][1] = 255;
    CHECK_EQ(isValidProject(p), false);
    p.drums[3].velocity[2][1] = 0;
    p.synths[3].wave = static_cast<Waveform>(4);
    CHECK_EQ(isValidProject(p), false);
}
TEST(instrumentGridChangePreservesHiddenNotes) {
    auto& p = freshInstrumentProject();
    applyEdit(p, noteCommand(CommandType::AddNote));
    Command c;
    c.type = CommandType::SetInstrumentLength;
    c.value = 4;
    applyEdit(p, c);
    c.type = CommandType::SetInstrumentGrid;
    c.value = static_cast<int>(Grid::ThirtySecond);
    applyEdit(p, c);
    CHECK_EQ(p.instruments[0][0].notes[0].startTick, 96);
    CHECK_EQ(isValidProject(p), true);
}
TEST(stageTwoFixtureMigratesAndSavesAsVersionFour) {
    auto& p = freshInstrumentProject();
    static uint8_t buffer[kProjectBufferSize];
    CHECK_EQ(loadProjectFile("tests/fixtures/stage2-v1.gbp", p, buffer, sizeof(buffer)), LoadResult::Ok);
    CHECK_EQ(p.bpm, 133);
    CHECK_EQ(p.drums[2].velocity[1][3], 99);
    CHECK_EQ(p.instruments[1][4].notes[0].lengthTicks, 48);
    CHECK_EQ(p.recordQuantize[3], true);
    p.synths[2].wave = Waveform::Square;
    p.synths[2].cutoff = 21;
    p.recordQuantize[1] = false;
    auto size = serializeProject(p, buffer, sizeof(buffer));
    CHECK_EQ(size > 0 && size < sizeof(buffer), true);
    CHECK_EQ(buffer[4], 5);
    p = Project{};
    CHECK_EQ(deserializeProject(buffer, size, p), LoadResult::Ok);
    CHECK_EQ(p.synths[2].wave, Waveform::Square);
    CHECK_EQ(p.synths[2].cutoff, 21);
    CHECK_EQ(p.recordQuantize[1], false);
    CHECK_EQ(p.instruments[1][4].notes[0].pitch, 60);
}
