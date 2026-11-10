#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include "gb/engine.h"
#include "gb/project_io.h"
#include "gb/sample_player.h"
#include "gb/sampler_ui.h"
#include "gb/validate.h"
#include "gb/wav.h"
#include "test.h"
using namespace gb;
namespace {
struct TempWav {
    std::string path;
    TempWav(uint16_t bits, uint16_t channels, uint32_t rate, const std::vector<uint8_t>& pcm, uint16_t format = 1) {
        char file[] = "/tmp/gbsamplerXXXXXX"; int fd = mkstemp(file); close(fd); path = file;
        std::vector<uint8_t> bytes;
        auto text = [&](const char* s) { for (int i=0;i<4;++i) bytes.push_back(s[i]); };
        auto u16 = [&](uint16_t n) { bytes.push_back(n); bytes.push_back(n>>8); };
        auto u32 = [&](uint32_t n) { u16(n);u16(n>>16); };
        text("RIFF");u32(48 + pcm.size() + (pcm.size()&1));text("WAVE");
        text("JUNK");u32(3);bytes.insert(bytes.end(),{1,2,3,0});
        text("fmt ");u32(16);u16(format);u16(channels);u32(rate);u32(rate*channels*bits/8);u16(channels*bits/8);u16(bits);
        text("data");u32(pcm.size());bytes.insert(bytes.end(),pcm.begin(),pcm.end()); if (pcm.size()&1) bytes.push_back(0);
        FILE* f=fopen(path.c_str(),"wb");fwrite(bytes.data(),1,bytes.size(),f);fclose(f);
    }
    ~TempWav() { remove(path.c_str()); }
};
Project& project() { static Project p;p=Project{};initProject(p);return p; }
Command command(CommandType type,uint8_t a=0,uint16_t value=0) {Command c;c.type=type;c.a=a;c.value=value;return c;}
struct UiSession {
    Project& p=project();std::vector<Command> sent;SamplerUi ui{p,sink,this};
    static bool sink(void* ctx,const Command& c) {static_cast<UiSession*>(ctx)->sent.push_back(c);return true;}
    void load() {
        ui.activate(0);SampleJob j;CHECK_EQ(ui.takeJob(j),true);
        SampleResult r;r.job=j;r.ok=true;r.files=1;r.visibleFiles=1;std::strcpy(r.names[0],"track.wav");ui.receive(r);
        ui.onKey(key::Enter,false,false);CHECK_EQ(ui.takeJob(j),true);CHECK_EQ(j.type,SampleJobType::Waveform);
        r.job=j;r.frames=32000;r.job.end=r.frames;ui.receive(r);
    }
};
}
TEST(wavReadsOddChunksResamplesAndSeeksPrecisely) {
    TempWav f(8,1,8000,{0,255,128,64});WavReader r;
    CHECK_EQ(r.open(f.path.c_str()),true);CHECK_EQ(r.info().outputFrames,16u);
    int16_t out[16];CHECK_EQ(r.read(out,16),16u);CHECK_EQ(out[0],-32768);CHECK_EQ(out[4],32512);CHECK_EQ(out[8],0);
    CHECK_EQ(out[2],-128);CHECK_EQ(r.seek(4),true);CHECK_EQ(r.read(out,1),1u);CHECK_EQ(out[0],32512);
    CHECK_EQ(r.seek(17),false);
}
TEST(wavMixesStereoWithoutOverflowAndSupportsTwentyFourBit) {
    TempWav stereo(16,2,32000,{0,128,255,127,0,64,0,64});WavReader r;int16_t out[4];
    CHECK_EQ(r.open(stereo.path.c_str()),true);CHECK_EQ(r.read(out,4),2u);CHECK_EQ(out[0],0);CHECK_EQ(out[1],16384);
    TempWav pcm24(24,1,32000,{0,0,128,255,255,127});
    CHECK_EQ(r.open(pcm24.path.c_str()),true);CHECK_EQ(r.read(out,4),2u);CHECK_EQ(out[0],-32768);CHECK_EQ(out[1],32767);
}
TEST(wavFloatClampsAndRemovesNonFiniteValues) {
    std::vector<uint8_t> data;for(float value:{2.0f,-2.0f,NAN,0.5f}) {
        uint32_t raw;memcpy(&raw,&value,4);for(int i=0;i<4;++i)data.push_back(raw>>(8*i));
    }
    TempWav f(32,1,32000,data,3);WavReader r;int16_t out[4];
    CHECK_EQ(r.open(f.path.c_str()),true);CHECK_EQ(r.read(out,4),4u);
    CHECK_EQ(out[0],32767);CHECK_EQ(out[1],-32768);CHECK_EQ(out[2],0);CHECK_EQ(out[3],16384);
}
TEST(wavRejectsTruncatedChunksBadFormatsAndEmptyData) {
    TempWav bad(16,3,32000,{0,0,0,0,0,0});WavReader r;CHECK_EQ(r.open(bad.path.c_str()),false);
    TempWav empty(16,1,32000,{});CHECK_EQ(r.open(empty.path.c_str()),false);
    TempWav file(16,1,32000,{0,0,0,0});truncate(file.path.c_str(),20);CHECK_EQ(r.open(file.path.c_str()),false);
    TempWav rate(16,1,0,{0,0});CHECK_EQ(r.open(rate.path.c_str()),false);
}
TEST(waveformUsesFullSourceAndZoomedRange) {
    std::vector<uint8_t> pcm(960);pcm[0]=255;pcm[1]=127;pcm[958]=0;pcm[959]=128;
    TempWav file(16,1,32000,pcm);WavReader r;CHECK_EQ(r.open(file.path.c_str()),true);
    WavePeak peaks[kWaveColumns];CHECK_EQ(buildWaveform(r,peaks,0,480),true);
    CHECK_EQ(peaks[0].high,32767);CHECK_EQ(peaks[239].low,-32768);
    CHECK_EQ(buildWaveform(r,peaks,240,480),true);CHECK_EQ(peaks[0].high,0);CHECK_EQ(peaks[239].low,-32768);
    CHECK_EQ(buildWaveform(r,peaks,480,480),false);CHECK_EQ(buildWaveform(r,peaks,0,481),false);
}
TEST(samplePlayerBoundsVoicesAndFadesBothEdges) {
    SamplePlayer s;int16_t pcm[256];for(auto& x:pcm)x=32767;
    s.setData(0,pcm,256);s.trigger(0,127);float first=s.render();for(int i=0;i<63;++i)s.render();
    CHECK_EQ(first>0 && first<0.02f,true);CHECK_EQ(s.render()>0.99f,true);
    for(int i=65;i<255;++i) { s.render(); }
    CHECK_EQ(s.render()<0.02f,true);CHECK_EQ(s.activeVoices(),0u);
    for(int i=0;i<20;++i) { s.trigger(0,127); }
    CHECK_EQ(s.activeVoices(),kSampleVoices);
    s.stop();CHECK_EQ(s.render(),0.0f);s.trigger(255,127);CHECK_EQ(s.activeVoices(),0u);
    s.setData(0,nullptr,0);s.trigger(0,127);CHECK_EQ(s.activeVoices(),0u);
}
TEST(samplerSequencerUsesSeparateTrackAndSampleMappings) {
    auto& p=project();p.scenes[0].pattern[kSamplerTrack]=0;p.samplePatterns[0].steps.velocity[2][0]=90;p.samplePatterns[0].slot[2]=19;
    Sequencer seq(p,32000);seq.playScene(0);Event events[32];auto count=seq.render(256,events,32);
    CHECK_EQ(count,1u);CHECK_EQ(events[0].type,EventType::SampleHit);CHECK_EQ(events[0].track,kSamplerTrack);
    CHECK_EQ(events[0].data,19);CHECK_EQ(events[0].velocity,90);CHECK_EQ(events[0].frame,0u);
}
TEST(samplerEngineHonorsMixerSoloAndFivePercentCeiling) {
    auto& p=project();p.scenes[0].pattern[kSamplerTrack]=0;p.samplePatterns[0].steps.velocity[0][0]=127;
    Engine e(p,32000);int16_t pcm[2048];for(auto& x:pcm)x=32767;
    auto c=command(CommandType::SetSampleData,0);c.sampleData={pcm,2048};e.handle(c);
    e.handle(command(CommandType::SetOutputLimit,0,5));e.handle(command(CommandType::Play));
    int16_t out[512];e.render(out,256);CHECK_EQ(e.sampleVoices(),1u);CHECK_EQ(out[200]>0,true);
    for(auto sample:out)CHECK_EQ(sample>=-1557 && sample<=1557,true);
    e.handle(command(CommandType::SetTrackMute,kSamplerTrack,1));e.render(out,256);for(auto sample:out)CHECK_EQ(sample,0);
    e.handle(command(CommandType::SetTrackMute,kSamplerTrack,0));e.handle(command(CommandType::SetTrackSolo,1,1));
    e.render(out,256);for(auto sample:out)CHECK_EQ(sample,0);
    e.handle(command(CommandType::SetTrackSolo,1,0));e.render(out,256);CHECK_EQ(out[200]>0,true);
    e.handle(command(CommandType::Stop));e.render(out,256);CHECK_EQ(e.sampleVoices(),0u);
}
TEST(samplerStorageModelMigratesLegacyAndKeepsClipBoundariesAndSteps) {
    auto& p=project();static uint8_t buffer[kProjectBufferSize];
    CHECK_EQ(loadProjectFile("tests/fixtures/stage4-v3.gbp",p,buffer,sizeof(buffer)),LoadResult::Ok);
    CHECK_EQ(p.masterVolume,37);CHECK_EQ(p.scenes[0].pattern[kSamplerTrack],-1);CHECK_EQ(p.mixer[kSamplerTrack].volume,100);
    std::strcpy(p.samples[13].source,"take 01.wav");p.samples[13].start=123456;p.samples[13].end=198765;
    p.samplePatterns[9].slot[5]=13;p.samplePatterns[9].steps.velocity[5][8]=127;p.scenes[3].pattern[kSamplerTrack]=9;
    auto size=serializeProject(p,buffer,sizeof(buffer));CHECK_EQ(size>0,true);CHECK_EQ(buffer[4],6);
    p=Project{};CHECK_EQ(deserializeProject(buffer,size,p),LoadResult::Ok);CHECK_EQ(p.samples[13].start,123456u);
    CHECK_EQ(p.samples[13].end,198765u);CHECK_EQ(p.samplePatterns[9].slot[5],13);CHECK_EQ(p.samplePatterns[9].steps.velocity[5][8],127);
    CHECK_EQ(p.scenes[3].pattern[kSamplerTrack],9);
}
TEST(samplerValidatesFileNamesAndNeverAcceptsEscapingPaths) {
    SampleClip c;std::strcpy(c.source,"../private.wav");c.end=100;CHECK_EQ(isValidSampleClip(c),false);
    std::strcpy(c.source,"clip.wav");CHECK_EQ(isValidSampleClip(c),true);c.start=c.end;CHECK_EQ(isValidSampleClip(c),false);
    std::memset(c.source,'a',sizeof(c.source));c.start=0;CHECK_EQ(isValidSampleClip(c),false);
    SamplePattern pattern;pattern.slot[0]=kSampleSlots;CHECK_EQ(isValidSamplePattern(pattern),false);
}
TEST(samplerCuttingAllocatesUniqueSlotsAndCommitsOnlyAfterStorageSuccess) {
    UiSession s;s.load();s.ui.onKey('a',true,false); // four -> eight automatic slices
    s.ui.onKey(key::Enter,true,false);SampleJob job;CHECK_EQ(s.ui.takeJob(job),true);CHECK_EQ(job.count,8);
    CHECK_EQ(s.p.samples[0].source[0],0);CHECK_EQ(s.ui.busy(),true);
    SampleResult done;done.job=job;done.ok=true;s.ui.receive(done);
    CHECK_EQ(s.p.samples[0].start,0u);CHECK_EQ(s.p.samples[0].end,4000u);CHECK_EQ(s.p.samples[7].start,28000u);
    CHECK_EQ(s.p.samples[7].end,32000u);CHECK_EQ(s.p.samplePatterns[0].slot[7],7);CHECK_EQ(s.p.scenes[0].pattern[kSamplerTrack],0);
    s.ui.onKey(key::Enter,false,false);CHECK_EQ(s.ui.takeJob(job),true);CHECK_EQ(job.slots[0],8);
    done.job=job;done.ok=false;std::strcpy(done.message,"SD read failed");s.ui.receive(done);CHECK_EQ(s.p.samples[8].source[0],0);
}
TEST(samplerPadsWriteIndependentPatternsClearAndUndoPreserveAssignments) {
    UiSession s;s.load();s.ui.onKey('p',true,false);s.ui.onKey('3',false,false);s.ui.onKey('q',false,false);
    CHECK_EQ(s.p.samplePatterns[0].steps.velocity[2][0],100);CHECK_EQ(s.p.drums[0].velocity[2][0],0);
    s.ui.onKey('=',true,false);CHECK_EQ(s.p.samplePatterns[0].slot[2],3);
    s.ui.onKey(key::Backspace,true,false);CHECK_EQ(s.p.samplePatterns[0].steps.velocity[2][0],0);CHECK_EQ(s.p.samplePatterns[0].slot[2],3);
    s.ui.onKey('z',true,false);CHECK_EQ(s.p.samplePatterns[0].steps.velocity[2][0],100);CHECK_EQ(s.p.samplePatterns[0].slot[2],3);
}
TEST(samplerPatternSelectionAssignsSceneAndDeletionWaitsForStorage) {
    UiSession s;s.load();s.ui.onKey(']',true,false);
    CHECK_EQ(s.p.scenes[0].pattern[kSamplerTrack],1);
    std::strcpy(s.p.samples[0].source,"track.wav");s.p.samples[0].end=32000;
    s.ui.onKey('d',true,false);SampleJob job;CHECK_EQ(s.ui.takeJob(job),true);CHECK_EQ(job.type,SampleJobType::Remove);
    CHECK_EQ(s.p.samples[0].end,32000u);SampleResult r;r.job=job;r.ok=false;s.ui.receive(r);
    CHECK_EQ(s.p.samples[0].end,32000u);
    s.ui.onKey('d',true,false);CHECK_EQ(s.ui.takeJob(job),true);r.job=job;r.ok=true;s.ui.receive(r);
    CHECK_EQ(s.p.samples[0].source[0],0);CHECK_EQ(s.p.samples[0].end,0u);
}
TEST(samplerMicrophoneProgressRemainsLockedUntilFinalizedWaveformArrives) {
    UiSession s;s.load();s.ui.onKey(key::Alt,false,false);SampleJob job;CHECK_EQ(s.ui.takeJob(job),true);
    CHECK_EQ(job.type,SampleJobType::RecordStart);SampleResult r;r.job=job;r.ok=true;s.ui.receive(r);
    CHECK_EQ(s.ui.recording(),true);s.ui.recordingProgress(64000,30);CHECK_EQ(s.ui.sourceFrames(),64000u);
    s.ui.onKey(key::Enter,false,false);CHECK_EQ(s.ui.takeJob(job),false);
    s.ui.onKey(key::Alt,false,false);CHECK_EQ(s.ui.takeJob(job),true);CHECK_EQ(job.type,SampleJobType::RecordStop);
    r.job=job;std::strcpy(r.job.source,"REC00001.WAV");r.frames=64000;r.job.end=64000;s.ui.receive(r);
    CHECK_EQ(s.ui.busy(),false);CHECK_EQ(s.ui.start(),0u);CHECK_EQ(s.ui.end(),64000u);
}
TEST(samplerOnlySceneDeterminesSongLengthAndReleasesOnEnd) {
    auto& p=project();p.scenes[0].pattern[0]=-1;p.scenes[0].pattern[kSamplerTrack]=0;
    p.samplePatterns[0].steps.length=4;p.samplePatterns[0].steps.grid=Grid::Sixteenth;
    p.samplePatterns[0].steps.velocity[0][0]=127;p.songLength=1;p.song[0]={0,1};
    Sequencer seq(p,32000);seq.playSong(0);Event events[32];unsigned hits=0;bool released=false;
    for(int i=0;i<70;++i) {
        const auto count=seq.render(256,events,32);
        for(unsigned e=0;e<count;++e) {
            hits+=events[e].type==EventType::SampleHit;
            released=released || events[e].type==EventType::AllNotesOff;
        }
    }
    CHECK_EQ(hits,1u);CHECK_EQ(seq.playing(),false);CHECK_EQ(released,true);
}
TEST(samplerCanMarkAdjacentCutsAndKeepsZoomedCursorVisible) {
    UiSession s;s.load();s.ui.onKey('/',true,false);s.ui.onKey('d',false,false);
    const auto firstEnd=s.ui.end();CHECK_EQ(firstEnd>0 && firstEnd<32000,true);
    s.ui.onKey('a',false,false);CHECK_EQ(s.ui.start(),firstEnd);CHECK_EQ(s.ui.end(),32000u);
    s.ui.onKey('/',true,false);s.ui.onKey('d',false,false);
    CHECK_EQ(s.ui.start(),firstEnd);CHECK_EQ(s.ui.end()>firstEnd,true);
    s.ui.onKey(';',true,false);SampleJob zoom;CHECK_EQ(s.ui.takeJob(zoom),true);
    SampleResult r;r.job=zoom;r.ok=true;r.frames=32000;s.ui.receive(r);
    bool panned=false;
    for(int i=0;i<130;++i) {
        s.ui.onKey('/',true,false);
        SampleJob pan;
        if(s.ui.takeJob(pan)) { CHECK_EQ(pan.type,SampleJobType::Waveform);CHECK_EQ(pan.start>zoom.start,true);panned=true;break; }
    }
    CHECK_EQ(panned,true);
}
TEST(synthBudgetRetiresQuietestVoiceAndRestoresFiveVoiceAllocation) {
    Synth s(32000);SynthSound sound;sound.attack=0;sound.decay=0;sound.sustain=127;sound.release=0;s.setSound(0,sound);
    for(int i=0;i<5;++i)s.noteOn(0,60+i,i?100:1,i,NoteSource::Live);
    for(int i=0;i<256;++i)s.render();
    s.setVoiceLimit(4);CHECK_EQ(s.activeVoices(),4);CHECK_EQ(s.hasVoice(0,0,NoteSource::Live),false);
    CHECK_EQ(s.hasVoice(0,4,NoteSource::Live),true);
    s.noteOn(0,72,100,8,NoteSource::Live);CHECK_EQ(s.activeVoices(),4);
    s.setVoiceLimit(5);s.noteOn(0,74,100,9,NoteSource::Live);CHECK_EQ(s.activeVoices(),5);
    s.allNotesOff();for(int i=0;i<1024;++i)CHECK_EQ(std::isfinite(s.render()),true);
    CHECK_EQ(s.activeVoices(),0);
}
TEST(engineOnlyReservesSamplerHeadroomWhileSamplesActuallySound) {
    auto& p=project();for(auto& s:p.synths){s.attack=0;s.sustain=127;}
    Engine e(p,32000);for(int i=0;i<5;++i){auto c=command(CommandType::LiveNoteOn,0,100);c.b=60+i;c.c=i;e.handle(c);}
    int16_t out[512];e.render(out,256);CHECK_EQ(e.activeVoices(),5);CHECK_EQ(e.voiceBudget(),5);
    int16_t pcm[1024]={};auto c=command(CommandType::SetSampleData,0);c.sampleData={pcm,1024};e.handle(c);
    e.handle(command(CommandType::PreviewSample,0,100));e.render(out,256);
    CHECK_EQ(e.activeVoices(),4);CHECK_EQ(e.voiceBudget(),4);
    for(int i=0;i<4;++i) { e.render(out,256); }
    CHECK_EQ(e.voiceBudget(),5);
    c=command(CommandType::LiveNoteOn,0,100);c.b=80;c.c=80;e.handle(c);e.render(out,256);CHECK_EQ(e.activeVoices(),5);
}
