#include "gb/audio_memory.h"
#include "gb/engine.h"

#include <algorithm>
#include <cmath>

#include "gb/validate.h"
namespace gb {
namespace {
constexpr float kMasterGain = 0.7f, kCeiling = 0.95f, kReleaseSeconds = 0.08f;
}
Engine::Engine(Project& project, uint32_t sampleRate)
    : project_(project),
      sequencer_(project, sampleRate),
      rack_(sampleRate),
      synth_(sampleRate),
      recorder_(project, sequencer_),
      arp_(sampleRate),
      releaseCoefficient_(1.0f - std::exp(-1.0f / (kReleaseSeconds * sampleRate))) {
    for (uint8_t t = 0; t < kInstrumentTracks; ++t) {
        synth_.setSound(t, project.synths[t]);
        arp_.configure(t, project.arps[t], project.bpm, synth_);
    }
    updateMixer();
    masterGain_ = masterTarget_;
}
void Engine::releaseClip(uint8_t track) {
    silence(track, NoteSource::Clip);
    if (track < kInstrumentTracks) sequencer_.resetDeferrals(track + 1);
}
void Engine::silence(uint8_t track, NoteSource source) {
    if (track == 255 && source != NoteSource::Live) { samples_.stop(); synth_.setVoiceLimit(kSynthVoices); }
    synth_.allNotesOff(track, source);
    arp_.clear(track, source, synth_);
}
void Engine::startNote(uint8_t track, uint8_t pitch, uint8_t velocity, uint16_t token, NoteSource source,
                       uint64_t frame) {
    if (project_.arps[track].mode == ArpMode::Off)
        synth_.noteOn(track, pitch, velocity, token, source);
    else
        arp_.on(track, token, source, pitch, velocity, frame);
}
void Engine::endNote(uint8_t track, uint16_t token, NoteSource source) {
    if (project_.arps[track].mode == ArpMode::Off)
        synth_.noteOff(track, token, source);
    else
        arp_.off(track, token, source, synth_);
}
void Engine::updateMixer() {
    bool solo = false;
    for (const auto& m : project_.mixer) solo = solo || m.solo;
    for (uint8_t t = 0; t < kTracks; ++t) {
        const auto& m = project_.mixer[t];
        const float gain = m.mute || (solo && !m.solo) ? 0 : m.volume * 0.01f;
        if (t == 0)
            drumGain_ = gain;
        else if (t == kSamplerTrack)
            sampleGain_ = gain;
        else
            synth_.setTrackGain(t - 1, gain);
    }
    masterTarget_ = std::min<uint8_t>(project_.masterVolume, outputLimit_) * 0.01f;
    masterGain_ = std::min(masterGain_, masterTarget_);
}
void Engine::handle(const Command& c) {
    if (!isValidCommand(c)) {
        if (staging_) stagingValid_ = false;
        return;
    }
    if (c.type == CommandType::Play || c.type == CommandType::Stop || c.type == CommandType::PlayScene ||
        c.type == CommandType::PlaySong)
        ++transportSerial_;
    switch (c.type) {
        case CommandType::SilenceSamples:
            samples_.stop();
            sampleEpoch_.fetch_add(1);
            break;
        case CommandType::SetSampleData:
            samples_.setData(c.a, c.sampleData.data, c.sampleData.frames);
            break;
        case CommandType::PreviewSample:
            samples_.trigger(c.a, c.value);
            if (samples_.activeVoices()) synth_.setVoiceLimit(kSynthVoices - 1);
            break;
        case CommandType::SetSamplePattern:
            samples_.stop();
            applyEdit(project_, c);
            break;
        case CommandType::SetOutputLimit:
            outputLimit_ = c.value;
            updateMixer();
            break;
        case CommandType::SetMasterVolume:
        case CommandType::SetTrackVolume:
        case CommandType::SetTrackMute:
        case CommandType::SetTrackSolo:
            applyEdit(project_, c);
            updateMixer();
            break;
        case CommandType::SetArpParam:
            silence(c.a);
            applyEdit(project_, c);
            arp_.configure(c.a, project_.arps[c.a], project_.bpm, synth_);
            break;
        case CommandType::PlayScene:
            if (sequencer_.playing()) {
                recorder_.finish();
                silence(255, NoteSource::Live);
            }
            sequencer_.playScene(c.a);
            recorder_.play();
            break;
        case CommandType::PlaySong:
            if (c.a < project_.songLength) {
                recorder_.finish();
                silence();
                sequencer_.playSong(c.a);
            }
            break;
        case CommandType::InsertSongEntry:
        case CommandType::SetSongEntry:
        case CommandType::DeleteSongEntry:
            if (sequencer_.songMode()) {
                recorder_.finish();
                sequencer_.stop();
                silence();
            }
            applyEdit(project_, c);
            break;
        case CommandType::SetScene:
            if (c.a == sequencer_.sceneIndex()) {
                recorder_.finish();
                silence(255, NoteSource::Clip);
            }
            applyEdit(project_, c);
            break;
        case CommandType::Play:
            sequencer_.playScene(0);
            recorder_.play();
            break;
        case CommandType::Stop:
            recorder_.finish();
            sequencer_.stop();
            silence();
            staging_ = false;
            break;
        case CommandType::Preview:
            rack_.trigger(c.a, c.b, project_.sounds[c.a]);
            break;
        case CommandType::LiveNoteOn:
            // A legato retarget ends the old recorded segment but keeps its voice.
            recorder_.noteOff(c.a, c.c);
            startNote(c.a, c.b, static_cast<uint8_t>(c.value), c.c, NoteSource::Live, frames_);
            if (c.value) recorder_.noteOn(c.a, c.c, c.b, static_cast<uint8_t>(c.value));
            break;
        case CommandType::LiveNoteOff:
            endNote(c.a, c.c, NoteSource::Live);
            recorder_.noteOff(c.a, c.c);
            break;
        case CommandType::ReleaseLive:
            recorder_.finish();
            silence(255, NoteSource::Live);
            break;
        case CommandType::SetRecording:
            if (c.value)
                recorder_.arm(c.a, c.b);
            else
                recorder_.finish();
            break;
        case CommandType::AcknowledgeRecording:
            recorder_.acknowledge();
            break;
        case CommandType::BeginInstrumentEdit:
            if (recorder_.locked(c.a, c.b)) break;
            staging_ = stagingValid_ = true;
            stagedTrack_ = c.a;
            stagedPattern_ = c.b;
            staged_ = InstrumentPattern{};
            staged_.length = c.c;
            staged_.grid = static_cast<Grid>(c.value);
            break;
        case CommandType::StageInstrumentNote:
            if (staging_ && stagingValid_ && c.a == stagedTrack_ && c.b == stagedPattern_ && c.c == staged_.noteCount &&
                staged_.noteCount < kMaxNotes)
                staged_.notes[staged_.noteCount++] = c.note;
            else
                stagingValid_ = false;
            break;
        case CommandType::CommitInstrumentEdit:
            if (staging_ && stagingValid_ && c.a == stagedTrack_ && c.b == stagedPattern_ &&
                isValidInstrument(staged_) && !recorder_.locked(c.a, c.b)) {
                releaseClip(c.a);
                project_.instruments[c.a][c.b] = staged_;
            }
            staging_ = false;
            break;
        case CommandType::CancelInstrumentEdit:
            staging_ = false;
            break;
        case CommandType::AddNote:
        case CommandType::SetNote:
        case CommandType::DeleteNote:
        case CommandType::ClearNotes:
        case CommandType::SetInstrumentGrid:
        case CommandType::SetInstrumentLength:
            if (!recorder_.locked(c.a, c.b)) {
                releaseClip(c.a);
                applyEdit(project_, c);
            }
            break;
        case CommandType::SetScenePattern:
            if (project_.scenes[c.a].pattern[c.b] == static_cast<int8_t>(c.c)) break;
            if (c.b == kSamplerTrack && c.a == sequencer_.sceneIndex()) samples_.stop();
            if (c.b > 0 && c.b <= kInstrumentTracks && c.a == sequencer_.sceneIndex()) {
                recorder_.finish();
                silence(c.b - 1);
                releaseClip(c.b - 1);
            }
            applyEdit(project_, c);
            break;
        case CommandType::SetSynthParam:
            applyEdit(project_, c);
            synth_.setSound(c.a, project_.synths[c.a]);
            break;
        case CommandType::SetRecordQuantize:
            // The target pattern and its timing settings are fixed for the whole pass.
            for (uint8_t p = 0; p < kPatternsPerTrack; ++p)
                if (recorder_.locked(c.a, p)) return;
            applyEdit(project_, c);
            break;
        default:
            applyEdit(project_, c);
            break;
    }
}
int16_t Engine::limit(float sample) {
    const float level = std::fabs(sample);
    if (level * limiterGain_ > kCeiling)
        limiterGain_ = kCeiling / level;
    else
        limiterGain_ += (1 - limiterGain_) * releaseCoefficient_;
    const float out = std::max(-kCeiling, std::min(kCeiling, sample * limiterGain_));
    masterGain_ += (masterTarget_ - masterGain_) * 0.01f;
    return static_cast<int16_t>(out * masterGain_ * 32767.0f);
}
GB_AUDIO_CODE void Engine::renderBlock(int16_t* stereo, uint32_t frames) {
    synth_.setVoiceLimit(samples_.activeVoices() ? kSynthVoices - 1 : kSynthVoices);
    arp_.tempo(project_.bpm);
    const bool wasPlaying = sequencer_.playing();
    const uint32_t beforeScene = sequencer_.status() & 0xffff;
    const uint32_t count = sequencer_.render(frames, events_, kMaxEvents);
    if (recorder_.armed() &&
        ((beforeScene & 15) != (sequencer_.status() & 15) ||
         ((beforeScene & (1u << 9)) && ((beforeScene >> 10) & 63) != ((sequencer_.status() >> 10) & 63)))) {
        recorder_.finish();
        silence(255, NoteSource::Live);
    }
    uint32_t next = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        while (next < count && events_[next].frame == frame) {
            const auto& e = events_[next++];
            switch (e.type) {
                case EventType::SampleHit:
                    samples_.trigger(e.data, e.velocity);
                    if (samples_.activeVoices()) synth_.setVoiceLimit(kSynthVoices - 1);
                    break;
                case EventType::DrumHit:
                    rack_.trigger(e.data, e.velocity, project_.sounds[e.data]);
                    break;
                case EventType::NoteOn:
                    startNote(e.track - 1, e.data, e.velocity, e.token, NoteSource::Clip, frames_ + frame);
                    break;
                case EventType::NoteOff:
                    endNote(e.track - 1, e.token, NoteSource::Clip);
                    break;
                case EventType::AllNotesOff:
                    if (e.track == kSamplerTrack) samples_.stop();
                    else silence(e.track == kAllTracks ? 255 : e.track - 1, NoteSource::Clip);
                    break;
            }
        }
        if (arp_.active()) arp_.render(frames_ + frame, synth_);
        const int16_t out = limit((rack_.render() * drumGain_ + synth_.render() + samples_.render() * sampleGain_) * kMasterGain);
        stereo[2 * frame] = stereo[2 * frame + 1] = out;
    }
    frames_ += frames;
    if (wasPlaying && !sequencer_.playing()) {
        recorder_.finish();
        silence();
    }
    transport_.store(
        sequencer_.status() | (uint32_t(sequencer_.playing()) << 16) | ((uint32_t(transportSerial_) & 32767) << 17),
        std::memory_order_relaxed);
    recorder_.flush();
    sceneTick_.store(sequencer_.sceneTick(), std::memory_order_relaxed);
    playing_.store(sequencer_.playing(), std::memory_order_relaxed);
    overflows_.store(sequencer_.overflows(), std::memory_order_relaxed);
    voices_.store(synth_.activeVoices(), std::memory_order_relaxed);
    sampleVoices_.store(samples_.activeVoices());
    const uint8_t budget = samples_.activeVoices() ? kSynthVoices - 1 : kSynthVoices;
    synth_.setVoiceLimit(budget);
    voiceBudget_.store(budget);
}
void Engine::render(int16_t* stereo, uint32_t frames) {
    for (uint32_t done = 0; done < frames;) {
        const uint32_t count = std::min<uint32_t>(256, frames - done);
        renderBlock(stereo + done * 2, count);
        done += count;
    }
}
}  // namespace gb
