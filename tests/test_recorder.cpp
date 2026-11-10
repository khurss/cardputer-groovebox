#include "gb/engine.h"
#include "test.h"
using namespace gb;
namespace {
Project& recordProject() {
    static Project p;
    p = Project{};
    initProject(p);
    p.scenes[0].pattern[1] = 0;
    return p;
}
void advance(Sequencer& s, uint32_t frames) {
    Event e[544];
    while (frames) {
        auto n = frames > 256 ? 256 : frames;
        s.render(n, e, 544);
        frames -= n;
    }
}
}  // namespace
TEST(recordingCapturesChordAndFinishesOnStop) {
    auto& p = recordProject();
    Sequencer seq(p, 32000);
    Recorder r(p, seq);
    r.arm(0, 0);
    seq.playScene(0);
    r.play();
    r.noteOn(0, 'z', 60, 100);
    r.noteOn(0, 'c', 64, 110);
    advance(seq, 8000);
    r.noteOff(0, 'z');
    r.finish();
    CHECK_EQ(p.instruments[0][0].noteCount, 2);
    CHECK_EQ(p.instruments[0][0].notes[0].startTick, 0);
    CHECK_EQ(p.instruments[0][0].notes[0].lengthTicks, 48);
    CHECK_EQ(p.instruments[0][0].notes[1].pitch, 64);
    RecordUpdate u;
    int added = 0, ended = 0;
    while (r.popUpdate(u)) {
        if (u.type == RecordUpdateType::Added)
            ++added;
        if (u.type == RecordUpdateType::Ended)
            ++ended;
    }
    CHECK_EQ(added, 2);
    CHECK_EQ(ended, 1);
    CHECK_EQ(r.locked(0, 0), true);
    r.acknowledge();
    CHECK_EQ(r.locked(0, 0), false);
}
TEST(recordingWrapAndLongHeldNotesHavePositiveDuration) {
    auto& p = recordProject();
    p.recordQuantize[0] = false;
    Sequencer seq(p, 32000);
    Recorder r(p, seq);
    seq.playScene(0);
    advance(seq, 62000);
    r.arm(0, 0);
    r.noteOn(0, 'z', 60, 100);
    advance(seq, 4000);
    r.noteOff(0, 'z');
    auto n = p.instruments[0][0].notes[0];
    CHECK_EQ(n.startTick > 360, true);
    CHECK_EQ(n.lengthTicks >= 23 && n.lengthTicks <= 25, true);
    r.noteOn(0, 'x', 62, 100);
    advance(seq, 140000);
    r.noteOff(0, 'x');
    CHECK_EQ(p.instruments[0][0].notes[1].lengthTicks, 384);
    r.noteOn(0, 'c', 64, 100);
    r.noteOff(0, 'c');
    CHECK_EQ(p.instruments[0][0].notes[2].lengthTicks, 1);
}
TEST(recordingReservesLastSlotAndKeepsResultsForSlowUI) {
    auto& p = recordProject();
    Sequencer seq(p, 32000);
    Recorder r(p, seq);
    seq.playScene(0);
    r.arm(0, 0);
    for (int i = 0; i < 64; ++i)
        r.noteOn(0, i, 60, 100);
    r.noteOn(0, 65, 65, 100);
    r.finish();
    CHECK_EQ(p.instruments[0][0].noteCount, 64);
    r.arm(0, 1);
    CHECK_EQ(r.locked(0, 1), false);
    int added = 0, full = 0, ended = 0;
    RecordUpdate u;
    while (r.popUpdate(u)) {
        added += u.type == RecordUpdateType::Added;
        full += u.type == RecordUpdateType::Full;
        ended += u.type == RecordUpdateType::Ended;
    }
    CHECK_EQ(added, 64);
    CHECK_EQ(full, 1);
    CHECK_EQ(ended, 1);
    r.acknowledge();
    r.arm(0, 1);
    CHECK_EQ(r.locked(0, 1), true);
}
TEST(instrumentTransactionIsAtomicAndInvalidStageCannotCommit) {
    auto& p = recordProject();
    Engine e(p, 32000);
    Command c;
    c.type = CommandType::BeginInstrumentEdit;
    c.c = 8;
    c.value = 2;
    e.handle(c);
    c.type = CommandType::StageInstrumentNote;
    c.c = 0;
    c.note = {0, 24, 60, 100};
    e.handle(c);
    CHECK_EQ(p.instruments[0][0].noteCount, 0);
    c.type = CommandType::CommitInstrumentEdit;
    e.handle(c);
    CHECK_EQ(p.instruments[0][0].noteCount, 1);
    CHECK_EQ(p.instruments[0][0].length, 8);
    c.type = CommandType::BeginInstrumentEdit;
    c.c = 4;
    e.handle(c);
    c.type = CommandType::StageInstrumentNote;
    c.c = 0;
    c.note.lengthTicks = 0;
    e.handle(c);
    c.type = CommandType::CommitInstrumentEdit;
    e.handle(c);
    CHECK_EQ(p.instruments[0][0].noteCount, 1);
}
