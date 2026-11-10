#include "sampler_storage.h"
#include "gb/validate.h"
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace {
constexpr uint32_t kMagic = 0x35534247, kHeaderBytes = 4096;
struct Entry { uint32_t offset = 0, frames = 0; };
struct CacheHeader {
    uint32_t magic = kMagic, identity = 0, used = kHeaderBytes, pcmHash = 0, headerHash = 0;
    Entry entry[gb::kSampleSlots];
};
uint32_t hash(const void* bytes, size_t size, uint32_t value = 2166136261u) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    while (size--) value = (value ^ *p++) * 16777619u;
    return value;
}
bool wavName(const char* name) {
    const size_t n = strlen(name);
    return n > 4 && n < gb::kSampleNameLength && !strcasecmp(name + n - 4, ".wav");
}
}
void SamplerStorage::start(gb::Project& p, gb::Engine& e, gb::CommandSink sink, void* context, bool testMode) {
    project_ = &p; engine_ = &e; sink_ = sink; context_ = context;
    snprintf(root_, sizeof(root_), "/sd/groovebox/%s", testMode ? "test-samples" : "samples");
    mkdir(root_, 0777);
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "samples");
    if (partition_ && partition_->size >= kHeaderBytes)
        if (esp_partition_mmap(partition_, 0, partition_->size, ESP_PARTITION_MMAP_DATA, &mapped_, &mapHandle_) != ESP_OK) mapped_ = nullptr;
    running_ = xTaskCreatePinnedToCore(task, "sampler_io", 8192, this, 3, nullptr, 0) == pdPASS;
}
void SamplerStorage::task(void* context) { static_cast<SamplerStorage*>(context)->run(); }
void SamplerStorage::error(const char* message) { result_.ok = false; snprintf(result_.message, sizeof(result_.message), "%.31s", message); }
void SamplerStorage::publish() { while (!results_.push(result_)) vTaskDelay(pdMS_TO_TICKS(5)); }
void SamplerStorage::run() {
    for (;;) {
        if (recordFile_) {
            drainRecording(false);
            const auto state = recordState_.load();
            if (state == Overrun || writeFailed_) {
                result_.job = {}; result_.job.type = gb::SampleJobType::RecordStop;
                stopRecording(); publish();
            }
        }
        gb::SampleJob job;
        if (jobs_.pop(job)) { execute(job); publish(); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
void SamplerStorage::execute(const gb::SampleJob& job) {
    result_.job = job; result_.ok = false; result_.message[0] = 0;
    using T = gb::SampleJobType;
    if (job.type == T::RecordStop) { stopRecording(); return; }
    if (recordFile_) { error("Stop microphone recording first"); return; }
    switch (job.type) {
        case T::Scan: scan(); break;
        case T::Waveform: waveform(); break;
        case T::RecordStart: startRecording(); break;
        case T::Prepare:
        case T::Store:
        case T::Remove:
            if (engine_->playing()) error("Stop playback to prepare samples");
            else if (cache(job.type != T::Prepare ? &job : nullptr)) {
                result_.ok = true;
                snprintf(result_.message, sizeof(result_.message), job.type == T::Store ? "Saved %u slice(s)" : job.type == T::Remove ? "Sample removed; WAV kept" : "Samples ready", job.count);
            }
            break;
        case T::RecordStop: break;
    }
}
void SamplerStorage::scan() {
    DIR* directory = opendir(root_);
    if (!directory) { error("SD sample folder unavailable"); return; }
    unsigned count = 0;
    while (auto* f = readdir(directory)) if (wavName(f->d_name)) ++count;
    rewinddir(directory);
    result_.files = std::min<unsigned>(count, UINT16_MAX);
    result_.job.page = std::min<unsigned>(result_.job.page, count ? (count - 1) / 16 : 0);
    result_.visibleFiles = 0; count = 0;
    while (auto* f = readdir(directory)) if (wavName(f->d_name)) {
        if (count / 16 == result_.job.page && result_.visibleFiles < 16)
            snprintf(result_.names[result_.visibleFiles++], gb::kSampleNameLength, "%s", f->d_name);
        ++count;
    }
    closedir(directory); result_.ok = true;
    snprintf(result_.message, sizeof(result_.message), "%u WAV file(s) | Alt record", result_.files);
}
void SamplerStorage::waveform() {
    char path[112];
    gb::SampleClip check; memcpy(check.source, result_.job.source, sizeof(check.source)); check.end = 1;
    if (!gb::isValidSampleClip(check)) { error("Invalid WAV filename"); return; }
    snprintf(path, sizeof(path), "%s/%s", root_, check.source);
    if (!reader_.open(path)) { error("Unsupported or damaged WAV"); return; }
    result_.frames = reader_.info().outputFrames;
    if (!result_.job.end) result_.job.end = result_.frames;
    if (!gb::buildWaveform(reader_, result_.peaks, result_.job.start, result_.job.end)) error("WAV read failed");
    else { result_.ok = true; snprintf(result_.message, sizeof(result_.message), "A/D bounds | Enter save slice"); }
    reader_.close();
}
bool SamplerStorage::cache(const gb::SampleJob* store) {
    if (!partition_ || !mapped_) { error("Sample flash partition missing"); return false; }
    memcpy(clips_, project_->samples, sizeof(clips_));
    if (store && store->type == gb::SampleJobType::Remove) {
        if (store->slots[0] >= gb::kSampleSlots) { error("Invalid sample slot"); return false; }
        clips_[store->slots[0]] = {};
    } else if (store) {
        if (!store->count || store->count > gb::kSampleSlots || store->end <= store->start || store->end - store->start < store->count) { error("Invalid slice range"); return false; }
        for (unsigned i = 0; i < store->count; ++i) {
            if (store->slots[i] >= gb::kSampleSlots || clips_[store->slots[i]].source[0]) { error("Sample slot is occupied"); return false; }
            auto& c = clips_[store->slots[i]];
            memcpy(c.source, store->source, sizeof(c.source));
            c.start = store->start + uint64_t(store->end - store->start) * i / store->count;
            c.end = store->start + uint64_t(store->end - store->start) * (i + 1) / store->count;
        }
    }
    CacheHeader header;
    uint32_t identity = hash(root_, strlen(root_));
    char path[112];
    for (unsigned i = 0; i < gb::kSampleSlots; ++i) {
        const auto& c = clips_[i];
        if (!gb::isValidSampleClip(c)) { error("Invalid sample metadata"); return false; }
        identity = hash(c.source, sizeof(c.source), identity); identity = hash(&c.start, sizeof(c.start), identity); identity = hash(&c.end, sizeof(c.end), identity);
        if (!c.source[0]) continue;
        snprintf(path, sizeof(path), "%.47s/%.47s", root_, c.source);
        struct stat info;
        if (stat(path, &info) || !reader_.open(path)) { error("Missing or damaged source WAV"); return false; }
        const auto frames = reader_.info().outputFrames; reader_.close();
        if (c.end > frames) { error("Slice exceeds source WAV"); return false; }
        identity = hash(&info.st_size, sizeof(info.st_size), identity); identity = hash(&info.st_mtime, sizeof(info.st_mtime), identity);
        const uint32_t length = c.end - c.start;
        if (uint64_t(header.used) + uint64_t(length) * 2 + 3 > partition_->size) { error("Sample cache full (about 65s)"); return false; }
        header.entry[i] = {header.used, length};
        header.used = (header.used + length * 2 + 3) & ~3u;
    }
    header.identity = identity;
    CacheHeader old; memcpy(&old, mapped_, sizeof(old));
    const uint32_t oldChecksum = old.headerHash; old.headerHash = 0;
    const bool cached = old.magic == kMagic && old.identity == identity && old.used == header.used &&
        oldChecksum == hash(&old, sizeof(old)) && old.pcmHash == hash(static_cast<const uint8_t*>(mapped_) + kHeaderBytes, header.used - kHeaderBytes);
    // Stop and detach every reader before any cache page can be erased.
    gb::Command cmd; cmd.type = gb::CommandType::SetSampleData;
    for (unsigned i = 0; i < gb::kSampleSlots; ++i) { cmd.a = i; cmd.sampleData = {nullptr, 0}; sink_(context_, cmd); }
    const auto epoch = engine_->sampleEpoch(); cmd.type = gb::CommandType::SilenceSamples; sink_(context_, cmd);
    while (engine_->sampleEpoch() == epoch) vTaskDelay(pdMS_TO_TICKS(1));
    ready_.store(false);
    if (!cached) {
        const size_t erase = (header.used + 4095) & ~4095u;
        if (esp_partition_erase_range(partition_, 0, erase) != ESP_OK) { error("Flash erase failed"); return false; }
        for (unsigned i = 0; i < gb::kSampleSlots; ++i) {
            if (!header.entry[i].frames) continue;
            const auto& c = clips_[i]; snprintf(path, sizeof(path), "%.47s/%.47s", root_, c.source);
            if (!reader_.open(path) || !reader_.seek(c.start)) { error("WAV disappeared during prepare"); return false; }
            uint32_t remaining = header.entry[i].frames, offset = header.entry[i].offset;
            while (remaining) {
                const auto frames = std::min<uint32_t>(remaining, sizeof(io_) / 2);
                memset(io_, 0, sizeof(io_));
                if (reader_.read(reinterpret_cast<int16_t*>(io_), frames) != frames ||
                    esp_partition_write(partition_, offset, io_, (frames * 2 + 3) & ~3u) != ESP_OK) {
                    reader_.close(); error("Sample cache write failed"); return false;
                }
                remaining -= frames; offset += frames * 2;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            reader_.close();
        }
        header.pcmHash = hash(static_cast<const uint8_t*>(mapped_) + kHeaderBytes, header.used - kHeaderBytes);
        header.headerHash = hash(&header, sizeof(header));
        if (esp_partition_write(partition_, 0, &header, sizeof(header)) != ESP_OK) { error("Cache header write failed"); return false; }
    }
    cmd.type = gb::CommandType::SetSampleData;
    uint32_t count = 0;
    for (unsigned i = 0; i < gb::kSampleSlots; ++i) {
        const auto& e = header.entry[i]; cmd.a = i;
        cmd.sampleData = {e.frames ? reinterpret_cast<const int16_t*>(static_cast<const uint8_t*>(mapped_) + e.offset) : nullptr, e.frames};
        sink_(context_, cmd); count += e.frames;
    }
    cmd.type = gb::CommandType::SilenceSamples; const auto installed = engine_->sampleEpoch(); sink_(context_, cmd);
    while (engine_->sampleEpoch() == installed) vTaskDelay(pdMS_TO_TICKS(1));
    cacheFrames_.store(count); ready_.store(true); return true;
}
void SamplerStorage::capture(const int16_t* stereo) {
    const auto state = recordState_.load();
    if (state == StopRequested) { recordState_.store(Stopped); return; }
    if (state != Recording) return;
    RecordBlock block;
    uint16_t peak = 0;
    for (unsigned i = 0; i < 256; ++i) { block.pcm[i] = stereo[i * 2]; peak = std::max<uint16_t>(peak, std::abs(int(block.pcm[i]))); }
    if (!recordBlocks_.push(block)) { dropped_.fetch_add(1); recordState_.store(Overrun); return; }
    recordedFrames_.fetch_add(256);
    auto before = peak_.load(); while (peak > before && !peak_.compare_exchange_weak(before, peak)) {}
}
void SamplerStorage::startRecording() {
    struct stat info;
    for (unsigned i = 1; i <= 99999; ++i) {
        snprintf(recordName_, sizeof(recordName_), "REC%05u.WAV", i);
        snprintf(recordPath_, sizeof(recordPath_), "%s/%s", root_, recordName_);
        snprintf(recordTemporary_, sizeof(recordTemporary_), "%s.part", recordPath_);
        if (stat(recordPath_, &info) && stat(recordTemporary_, &info)) break;
        if (i == 99999) { error("Recording names exhausted"); return; }
    }
    recordFile_ = fopen(recordTemporary_, "wb");
    if (!recordFile_ || !gb::writeWavHeader(recordFile_, 0)) {
        if (recordFile_) fclose(recordFile_);
        recordFile_ = nullptr; error("Cannot create recording on SD"); return;
    }
    buffered_ = writtenFrames_ = 0; writeFailed_ = false;
    recordedFrames_.store(0); peak_.store(0); dropped_.store(0); recordState_.store(Recording);
    result_.ok = true; snprintf(result_.message, sizeof(result_.message), "MIC recording | Alt stop");
}
void SamplerStorage::drainRecording(bool final) {
    RecordBlock block;
    while (recordBlocks_.pop(block)) {
        memcpy(io_ + buffered_, block.pcm, sizeof(block.pcm)); buffered_ += sizeof(block.pcm);
        if (buffered_ == sizeof(io_)) {
            if (!writeFailed_ && fwrite(io_, 1, buffered_, recordFile_) == buffered_) writtenFrames_ += buffered_ / 2;
            else writeFailed_ = true;
            buffered_ = 0;
        }
    }
    if (final && buffered_) {
        if (!writeFailed_ && fwrite(io_, 1, buffered_, recordFile_) == buffered_) writtenFrames_ += buffered_ / 2;
        else writeFailed_ = true;
        buffered_ = 0;
    }
}
void SamplerStorage::stopRecording() {
    if (!recordFile_) { error("Microphone is not recording"); return; }
    if (recordState_.load() == Recording) {
        recordState_.store(StopRequested);
        while (recordState_.load() == StopRequested) vTaskDelay(pdMS_TO_TICKS(1));
    }
    drainRecording(true); recordState_.store(Idle);
    const bool header = gb::writeWavHeader(recordFile_, writtenFrames_);
    const bool closed = fclose(recordFile_) == 0; recordFile_ = nullptr;
    if (!header || !closed || !writtenFrames_) { error("Recording failed; .part kept"); return; }
    if (rename(recordTemporary_, recordPath_)) { error("Recording rename failed"); return; }
    memcpy(result_.job.source, recordName_, sizeof(recordName_)); result_.job.start = result_.job.end = 0;
    waveform();
    if (dropped_.load() || writeFailed_) snprintf(result_.message, sizeof(result_.message), "SD slow: partial WAV saved");
    else snprintf(result_.message, sizeof(result_.message), "Recorded WAV | A/D bounds");
}
