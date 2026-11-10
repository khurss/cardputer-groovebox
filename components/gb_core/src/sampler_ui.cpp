#include "gb/sampler_ui.h"
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
void SamplerUi::draw(Canvas& c, uint32_t tick, bool playing) const {
    c.fillRect(0, 0, 240, 135, color::Background);
    char text[80];
    std::snprintf(text, sizeof(text), "SAMPLER %s P%02u %u:S%02u %s", recording_ ? "REC" : sequencer_ ? "SEQ" : browser_ ? "FILES" : "CUT", pattern_ + 1, pad_ + 1, slot() + 1, playing ? "PLAY" : "STOP");
    c.text(0, 2, text, recording_ ? color::Warning : color::Text);
    if (browser_) {
        if (!visible_) c.text(0, 36, "Put WAV in groovebox/samples", color::Warning);
        const unsigned first = (file_ / 7) * 7;
        for (unsigned i = first; i < std::min<unsigned>(visible_, first + 7); ++i) {
            int y = 18 + (i - first) * 13;
            if (i == file_) c.fillRect(0, y, 240, 12, color::LaneSelected);
            std::snprintf(text, sizeof(text), "%02u %.35s", page_ * 16 + i + 1, names_[i]); c.text(0, y + 1, text, color::Text);
        }
        c.text(0, 112, pending_ ? message_ : "Fn+arrows select Enter open [] page", color::Text);
    } else if (sequencer_) {
        const auto& p = p_.samplePatterns[pattern_];
        const unsigned step = (tick % patternTicks(p.steps.length, p.steps.grid)) / gridTicks(p.steps.grid);
        for (int pad = 0; pad < kSamplePads; ++pad) {
            const int y = 20 + pad * 10;
            std::snprintf(text, sizeof(text), "%u", pad + 1); c.text(0, y + 1, text, pad == pad_ ? color::StepAccent : color::Text);
            for (int s = 0; s < kMaxSteps; ++s) {
                const auto v = p.steps.velocity[pad][s];
                c.fillRect(15 + s * 14, y, 12, 8, s >= p.steps.length ? color::StepOutside : v ? (v == 127 ? color::StepAccent : color::StepOn) : color::StepOff);
                if (playing && s == int(step)) c.fillRect(15 + s * 14, y, 12, 1, color::Playhead);
            }
        }
        std::snprintf(text, sizeof(text), "PAD%u -> S%02u %s x%u %s", pad_ + 1, slot() + 1, p_.samples[slot()].source[0] ? "READY" : "EMPTY", p.steps.length, grids[static_cast<unsigned>(p.steps.grid)]);
        c.text(0, 105, text, color::Text);
        c.text(0, 115, pending_ ? message_ : "1..8 pad q..k steps Fn+/- assign", color::Text);
    } else {
        const uint32_t span = viewEnd_ > viewStart_ ? viewEnd_ - viewStart_ : 1;
        int displayPeak = 1;
        for (const auto& p : peaks_) displayPeak = std::max(displayPeak, std::max(-int(p.low), int(p.high)));
        for (unsigned x = 0; x < kWaveColumns; ++x) {
            const uint32_t at = viewStart_ + uint64_t(x) * span / kWaveColumns;
            const int top = 63 - (int(peaks_[x].high) * 35 / displayPeak), bottom = 63 - (int(peaks_[x].low) * 35 / displayPeak);
            c.fillRect(x, 27, 1, 73, !recording_ && at >= start_ && at < end_ ? color::StepOutside : color::Background);
            c.fillRect(x, top, 1, std::max(1, bottom - top + 1), recording_ ? color::Warning : color::StepOn);
        }
        if (!recording_ && frames_) {
            auto mark = [&](uint32_t frame, uint16_t color) {
                if (frame >= viewStart_ && frame <= viewEnd_) c.fillRect(std::min<uint32_t>(239, uint64_t(frame - viewStart_) * 240 / span), 26, 1, 75, color);
            };
            for (unsigned i = 0; i < kSampleSlots; ++i) {
                const auto& clip = p_.samples[i];
                if (clip.source[0] && !std::strcmp(clip.source, source_)) {
                    mark(clip.start, color::LaneSelected); mark(clip.end, color::LaneSelected);
                }
            }
            mark(start_, color::LaneSelected); mark(end_, color::LaneSelected); mark(cursor_, color::StepAccent);
            if (parts_ > 1) for (unsigned i = 1; i < parts_; ++i) {
                const uint32_t frame = start_ + uint64_t(end_ - start_) * i / parts_;
                if (frame >= viewStart_ && frame <= viewEnd_) c.fillRect(std::min<uint32_t>(239, uint64_t(frame - viewStart_) * 240 / span), 27, 1, 5, color::Warning);
            }
        }
        std::snprintf(text, sizeof(text), "%.38s", recording_ ? "MIC -> SD (no monitoring)" : source_); c.text(0, 16, text, color::Text);
        if (recording_)
            std::snprintf(text, sizeof(text), "%lu.%03lus recorded | peak %u%%", static_cast<unsigned long>(frames_ / kSampleRate), static_cast<unsigned long>(frames_ % kSampleRate / 32), unsigned(displayPeak * 100 / 32768));
        else
            std::snprintf(text, sizeof(text), "%lu.%03lus A%lu B%lums N%u", static_cast<unsigned long>(cursor_ / kSampleRate), static_cast<unsigned long>(cursor_ % kSampleRate / 32), static_cast<unsigned long>(start_ / 32), static_cast<unsigned long>(end_ / 32), parts_);
        c.text(0, 104, text, color::Text);
        c.text(0, 114, pending_ ? message_ : recording_ ? "Alt stop recording" : "A start D end Enter save Fn+Enter chop", color::Text);
    }
    c.text(0, 125, pending_ ? "WORKING..." : message_, color::StepAccent);
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
