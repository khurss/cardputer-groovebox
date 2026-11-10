#include "gb/sampler_ui.h"
#include "gb/ui_style.h"
#include "gb/validate.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
namespace gb {
namespace {
constexpr const char* grids[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T"};
Command command(CommandType type, uint8_t a = 0, uint16_t value = 0) { Command c; c.type = type; c.a = a; c.value = value; return c; }
}
void SamplerUi::send(Command c) {
    if (sink_(context_, c)) { applyEdit(p_, c); ++edits_; }
    dirty_ = true;
}
void SamplerUi::activate(uint8_t scene) {
    scene_ = scene;
    const int pattern = p_.scenes[scene].pattern[kSamplerTrack];
    if (pattern >= 0) pattern_ = pattern;
    if (!initialized_ && !pending_) { initialized_ = true; request(SampleJob{}); }
    dirty_ = true;
}
void SamplerUi::setPattern(const SamplePattern& p, bool remember) {
    if (remember) { undo_ = p_.samplePatterns[pattern_]; undoAvailable_ = true; }
    auto c = command(CommandType::SetSamplePattern, pattern_); new (&c.samplePattern) SamplePattern(p); send(c);
    if (p_.scenes[scene_].pattern[kSamplerTrack] != pattern_) {
        c = command(CommandType::SetScenePattern, scene_); c.b = kSamplerTrack; c.c = pattern_; send(c);
    }
}
void SamplerUi::clearPattern() {
    if (busy()) return;
    auto p = p_.samplePatterns[pattern_];
    for (auto& lane : p.steps.velocity) for (auto& v : lane) v = 0;
    setPattern(p);
}
void SamplerUi::setTiming(bool grid, uint16_t value) {
    if (busy()) return;
    auto p = p_.samplePatterns[pattern_];
    if (grid && value <= 5) changeDrumGrid(p.steps, static_cast<Grid>(value));
    else if (!grid && (value == 4 || value == 8 || value == 16)) p.steps.length = value;
    else return;
    setPattern(p);
}
void SamplerUi::request(SampleJob job) {
    if (pending_) return;
    job_ = job; pending_ = jobReady_ = dirty_ = true;
    std::snprintf(message_, sizeof(message_), "%s", job.type == SampleJobType::Store ? "Preparing samples..." : "Working...");
}
bool SamplerUi::takeJob(SampleJob& job) {
    if (!jobReady_) return false;
    job = job_; jobReady_ = false; return true;
}
void SamplerUi::waveform(uint32_t begin, uint32_t end) {
    SampleJob job; job.type = SampleJobType::Waveform;
    std::memcpy(job.source, source_, sizeof(source_)); job.start = begin; job.end = end; request(job);
}
void SamplerUi::store(uint8_t count) {
    if (!frames_ || end_ <= start_ || end_ - start_ < count) return;
    SampleJob job; job.type = SampleJobType::Store; job.start = start_; job.end = end_; job.count = count;
    std::memcpy(job.source, source_, sizeof(source_));
    unsigned found = 0;
    for (unsigned i = 0; i < kSampleSlots && found < count; ++i) if (!p_.samples[i].source[0]) job.slots[found++] = i;
    if (found < count) { std::snprintf(message_, sizeof(message_), "Not enough empty slots (%u)", kSampleSlots); return; }
    request(job);
}
void SamplerUi::receive(const SampleResult& r) {
    pending_ = false; dirty_ = true;
    std::snprintf(message_, sizeof(message_), "%s", r.message);
    if (!r.ok) { if (r.job.type == SampleJobType::RecordStop) recording_ = false; return; }
    if (r.job.type == SampleJobType::Prepare) {
        initialized_ = true; request(SampleJob{});
    } else if (r.job.type == SampleJobType::Scan) {
        files_ = r.files; visible_ = r.visibleFiles; page_ = r.job.page; file_ = 0;
        std::memcpy(names_, r.names, sizeof(names_)); browser_ = true; sequencer_ = false;
    } else if (r.job.type == SampleJobType::Waveform || r.job.type == SampleJobType::RecordStop) {
        const bool changed = std::strcmp(source_, r.job.source) || r.job.type == SampleJobType::RecordStop;
        std::memcpy(source_, r.job.source, sizeof(source_)); frames_ = r.frames;
        viewStart_ = r.job.start; viewEnd_ = r.job.end ? r.job.end : frames_;
        if (changed || end_ == 0) { start_ = 0; end_ = frames_; cursor_ = 0; }
        std::memcpy(peaks_, r.peaks, sizeof(peaks_));
        browser_ = sequencer_ = recording_ = false;
    } else if (r.job.type == SampleJobType::RecordStart) {
        recording_ = true; frames_ = 0;
        for (auto& p : peaks_) p = {};
        browser_ = sequencer_ = false;
    } else if (r.job.type == SampleJobType::Remove) {
        auto c = command(CommandType::SetSampleClip, r.job.slots[0]); new (&c.sampleClip) SampleClip{}; send(c);
    } else if (r.job.type == SampleJobType::Store) {
        const auto& j = r.job;
        for (unsigned i = 0; i < j.count; ++i) {
            auto c = command(CommandType::SetSampleClip, j.slots[i]);
            new (&c.sampleClip) SampleClip{}; std::memcpy(c.sampleClip.source, j.source, sizeof(j.source));
            c.sampleClip.start = j.start + uint64_t(j.end - j.start) * i / j.count;
            c.sampleClip.end = j.start + uint64_t(j.end - j.start) * (i + 1) / j.count;
            send(c);
        }
        auto p = p_.samplePatterns[pattern_];
        if (j.count == 1) p.slot[pad_] = j.slots[0];
        else for (unsigned i = 0; i < std::min<unsigned>(j.count, kSamplePads); ++i) p.slot[i] = j.slots[i];
        setPattern(p);
        if (j.count == 1) sink_(context_, command(CommandType::PreviewSample, j.slots[0], 100));
    }
}
void SamplerUi::recordingProgress(uint32_t frames, uint16_t peak) {
    if (!recording_ || frames == frames_) return;
    frames_ = frames;
    for (unsigned i = 1; i < kWaveColumns; ++i) peaks_[i - 1] = peaks_[i];
    peaks_[kWaveColumns - 1] = {static_cast<int16_t>(-std::min<unsigned>(peak, 32767)), static_cast<int16_t>(std::min<unsigned>(peak, 32767))};
    dirty_ = true;
}
void SamplerUi::onKey(char code, bool fn, bool shift) {
    dirty_ = true;
    if (pending_) return;
    if (code == key::Alt) {
        SampleJob j; j.type = recording_ ? SampleJobType::RecordStop : SampleJobType::RecordStart; request(j); return;
    }
    if (recording_) return;
    if (fn && code == 'd' && p_.samples[slot()].source[0]) {
        SampleJob j; j.type = SampleJobType::Remove; j.slots[0] = slot(); request(j); return;
    }
    if (fn && code == 'b') { SampleJob j; j.page = page_; request(j); return; }
    if (fn && code == 'p') { sequencer_ = !sequencer_; browser_ = false; return; }
    if (fn && (code == '[' || code == ']')) {
        pattern_ = std::max(0, std::min(kPatternsPerTrack - 1, pattern_ + (code == ']' ? 1 : -1)));
        auto c = command(CommandType::SetScenePattern, scene_); c.b = kSamplerTrack; c.c = pattern_; send(c);
        undoAvailable_ = false; return;
    }
    if (browser_) {
        if (fn && (code == ';' || code == '.')) file_ = std::max(0, std::min(std::max(0, int(visible_) - 1), file_ + (code == '.' ? 1 : -1)));
        else if (code == '[' || code == ']') {
            SampleJob j; j.page = std::max(0, std::min(std::max(0, (int(files_) - 1) / 16), page_ + (code == ']' ? 1 : -1))); request(j);
        } else if (code == key::Enter && visible_) {
            SampleJob j; j.type = SampleJobType::Waveform; std::memcpy(j.source, names_[file_], sizeof(j.source)); request(j);
        }
        return;
    }
    if (code >= '1' && code <= '8' && !fn) {
        pad_ = code - '1'; sink_(context_, command(CommandType::PreviewSample, slot(), shift ? 127 : 100)); return;
    }
    if (fn && (code == '-' || code == '=')) {
        auto p = p_.samplePatterns[pattern_];
        p.slot[pad_] = std::max(0, std::min(kSampleSlots - 1, int(slot()) + (code == '=' ? 1 : -1)));
        setPattern(p); return;
    }
    if (sequencer_) {
        if (fn) {
            auto p = p_.samplePatterns[pattern_];
            if (code == key::Backspace) { for (auto& lane : p.steps.velocity) for (auto& v : lane) v = 0; }
            else if (code == 'l') p.steps.length = p.steps.length == 16 ? 4 : p.steps.length == 4 ? 8 : 16;
            else if (code == 'g') changeDrumGrid(p.steps, static_cast<Grid>((static_cast<unsigned>(p.steps.grid) + 1) % 6));
            else if (code == 'c') { clipboard_ = p; clipboardFull_ = true; return; }
            else if (code == 'v' && clipboardFull_) p = clipboard_;
            else if (code == 'z' && undoAvailable_) { setPattern(undo_, false); undoAvailable_ = false; return; }
            else return;
            setPattern(p);
        } else {
            constexpr char keys[] = "qwertyuiasdfghjk";
            const char* at = code ? std::strchr(keys, code) : nullptr;
            if (at) {
                const unsigned step = at - keys;
                auto p = p_.samplePatterns[pattern_];
                if (step < p.steps.length) {
                    auto& v = p.steps.velocity[pad_][step];
                    v = shift ? (v == 127 ? 100 : 127) : (v ? 0 : 100); setPattern(p);
                }
            }
        }
        return;
    }
    if (fn && (code == ',' || code == '/')) {
        const uint32_t step = shift ? 1 : std::max<uint32_t>(1, (viewEnd_ - viewStart_) / 120);
        cursor_ = code == '/' ? cursor_ + std::min(step, frames_ - cursor_) : cursor_ - std::min(cursor_, step);
        if (cursor_ < viewStart_ || cursor_ > viewEnd_) {
            const uint32_t span = viewEnd_ - viewStart_;
            const uint32_t begin = std::min(frames_ - span, cursor_ - std::min(cursor_, span / 2));
            waveform(begin, begin + span);
        }
    } else if (fn && (code == ';' || code == '.') && frames_) {
        const uint32_t span = viewEnd_ - viewStart_;
        const uint32_t next = code == ';' ? std::max<uint32_t>(240, span / 2) : std::min<uint64_t>(frames_, uint64_t(span) * 2);
        const uint32_t length = std::min(frames_, next);
        const uint32_t begin = std::min(frames_ - length, cursor_ - std::min(cursor_, length / 2));
        waveform(begin, begin + length);
    } else if (!fn && code == 'a' && cursor_ < frames_) {
        start_ = cursor_;
        if (end_ <= start_) end_ = frames_;
    } else if (!fn && code == 'd' && cursor_ > 0) {
        end_ = cursor_;
        if (start_ >= end_) start_ = 0;
    }
    else if (!fn && code == 'r') { start_ = cursor_ = 0; end_ = frames_; if (frames_) waveform(0, frames_); }
    else if (fn && code == 'a') parts_ = parts_ == 32 ? 4 : parts_ * 2;
    else if (code == key::Enter) store(fn ? parts_ : 1);
}
void SamplerUi::draw(Canvas& c,uint32_t tick,bool playing) const {
    c.fillRect(0,0,240,135,color::Background);char text[80];
    snprintf(text,sizeof(text),"SAMPLER / P%02u",pattern_+1);style::header(c,text,recording_?"REC":playing?"PLAY":"STOP",recording_?color::Warning:color::StepOn);
    const int mode=browser_?0:sequencer_?2:1;constexpr const char* tabs[]={"FILES","CUT","PADS"};
    for(int i=0;i<3;++i){c.fillRect(3+i*79,18,76,10,i==mode?color::LaneSelected:color::Panel);c.text(24+i*79,19,tabs[i],i==mode?color::StepOn:color::Muted);}
    if(browser_){
        if(!visible_){c.text(21,52,"NO WAV FILES",color::Text);c.text(9,71,"SD: /groovebox/samples/",color::Muted);c.text(9,92,"Or press Alt to record microphone",color::StepAccent);}
        const unsigned first=(file_/6)*6;
        for(unsigned i=first;i<std::min<unsigned>(visible_,first+6);++i){int y=32+(i-first)*14;
            c.fillRect(3,y,231,13,i==file_?color::LaneSelected:color::Panel);
            snprintf(text,sizeof(text),"%02u",page_*16+i+1);c.text(5,y+3,text,color::Muted);
            snprintf(text,sizeof(text),"%.34s",names_[i]);c.text(23,y+3,text,i==file_?color::Text:color::Muted);
        }
        if(visible_>6){c.fillRect(237,32,2,82,color::StepOff);c.fillRect(237,32+file_*70/(visible_-1),2,12,color::StepOn);}
        style::footer(c,pending_?message_:"Arrows choose  Enter load  Esc back");
    }else if(sequencer_){
        const auto& p=p_.samplePatterns[pattern_];const unsigned step=(tick%patternTicks(p.steps.length,p.steps.grid))/gridTicks(p.steps.grid);
        for(int pad=0;pad<8;++pad){int y=31+pad*10;bool selected=pad==pad_;
            if(selected)c.fillRect(1,y,39,9,color::LaneSelected);
            snprintf(text,sizeof(text),"%u:%02u",pad+1,p.slot[pad]+1);c.text(3,y+1,text,selected?color::StepAccent:color::Muted);
            for(int n=0;n<16;++n){auto v=p.steps.velocity[pad][n];c.fillRect(43+n*12,y,10,8,n>=p.steps.length?color::StepOutside:v==127?color::StepAccent:v?style::tracks[pad%6]:color::StepOff);
                if(playing && n==int(step))c.fillRect(43+n*12,y,10,1,color::Playhead);
            }
        }
        snprintf(text,sizeof(text),"S%02u %.32s",slot()+1,p_.samples[slot()].source[0]?p_.samples[slot()].source:"EMPTY / load or record first");c.text(3,112,text,color::Text);
        style::footer(c,pending_?message_:"1-8 pad  q..k steps  Esc cut  Opt menu");
    }else{
        snprintf(text,sizeof(text),"%.38s",recording_?"MIC -> SD / no monitoring":source_[0]?source_:"NO SOURCE");c.text(3,31,text,color::Muted);
        const uint32_t span=viewEnd_>viewStart_?viewEnd_-viewStart_:1;
        int peak=1;for(const auto& p:peaks_)peak=std::max(peak,std::max(-int(p.low),int(p.high)));
        if(frames_ || recording_){
            for(unsigned x=0;x<kWaveColumns;++x){const uint32_t at=viewStart_+uint64_t(x)*span/kWaveColumns;
                bool selected=!recording_ && at>=start_ && at<end_;
                c.fillRect(x,42,1,52,selected?color::Panel:color::Background);
                int top=68-int(peaks_[x].high)*25/peak,bottom=68-int(peaks_[x].low)*25/peak;
                c.fillRect(x,top,1,std::max(1,bottom-top+1),recording_?color::Warning:selected?color::StepOn:color::Muted);
            }
            if(!recording_){
                auto mark=[&](uint32_t at,uint16_t ink,bool handle){if(at>=viewStart_&&at<=viewEnd_){int x=std::min<uint32_t>(239,uint64_t(at-viewStart_)*240/span);c.fillRect(x,41,1,54,ink);if(handle)c.fillRect(std::clamp(x-2,0,235),41,5,3,ink);}};
                for(unsigned i=0;i<kSampleSlots;++i){const auto& clip=p_.samples[i];if(clip.source[0]&&!strcmp(clip.source,source_)){mark(clip.start,color::StepOff,false);mark(clip.end,color::StepOff,false);}}
                mark(start_,color::StepOn,true);mark(end_,color::StepOn,true);mark(cursor_,color::Playhead,true);
                for(unsigned i=1;i<parts_;++i){const uint32_t at=start_+uint64_t(end_-start_)*i/parts_;if(at>=viewStart_&&at<=viewEnd_)c.fillRect(std::min<uint32_t>(239,uint64_t(at-viewStart_)*240/span),90,1,5,color::StepAccent);}
                snprintf(text,sizeof(text),"A%lums B%lums / %u cuts",static_cast<unsigned long>(start_/32),static_cast<unsigned long>(end_/32),parts_);
            }else snprintf(text,sizeof(text),"REC %lu.%02lus / PEAK %u%%",static_cast<unsigned long>(frames_/kSampleRate),static_cast<unsigned long>(frames_%kSampleRate/320),unsigned(peak*100/32768));
            c.text(3,100,text,color::Text);
            c.fillRect(3,113,234,5,color::StepOff);
            if(frames_){const int x=std::clamp<int>(uint64_t(viewStart_)*234/frames_,0,233),end=std::clamp<int>(uint64_t(viewEnd_)*234/frames_,x+1,234);c.fillRect(3+x,113,end-x,5,color::LaneSelected);}
        }else{c.text(36,57,"RECORD OR LOAD A WAV",color::Text);c.text(24,79,"Alt mic / Fn+B browse files",color::StepAccent);}
        style::footer(c,pending_?message_:recording_?"Alt stop recording / waveform is live":"A/B: A D  Enter save  Fn Enter chop");
    }
}
void SamplerUi::drawFnMenu(Canvas& c) const {
    c.text(3, 44, "B browse WAV  Alt mic record/stop", color::Text);
    c.text(3, 54, "P waveform / pad sequencer", color::Text);
    c.text(3, 64, "Arrows move/zoom; Shift: 1 frame", color::Text);
    c.text(3, 74, "A count Enter chop D delete slot", color::Text);
    c.text(3, 84, "+/- assign slot  [/] pattern", color::Text);
    c.text(3, 94, "SEQ: L length G grid BS clear Z undo", color::Text);
    c.text(3, 104, "No Fn: A/D bounds Enter save", color::Text);
    c.text(3, 114, "1..8 audition pads; Space transport", color::Text);
    c.text(3, 124, "OPT close   ENTER quick start", color::Text);
}
}
