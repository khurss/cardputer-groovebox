#include "gb/project_io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>

#include "gb/validate.h"

namespace gb {

namespace {

constexpr char kMagic[4] = {'G', 'B', 'P', 'J'};
constexpr uint16_t kVersion = 6;
constexpr size_t kHeaderSize = 14;  // Magic, version, payload size, checksum.
constexpr size_t kMaxPathLength = 96;

uint32_t checksum(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void putU16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

void putU32(uint8_t* out, uint32_t value) {
    putU16(out, static_cast<uint16_t>(value));
    putU16(out + 2, static_cast<uint16_t>(value >> 16));
}

uint16_t getU16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t getU32(const uint8_t* in) { return getU16(in) | (uint32_t{getU16(in + 2)} << 16); }

// One routine both reads and writes, so the two directions cannot disagree on the layout.
class Codec {
public:
    Codec(uint8_t* data, size_t capacity, bool writing) : data_(data), capacity_(capacity), writing_(writing) {}

    void u8(uint8_t& value) {
        if (!room(1))
            return;
        if (writing_)
            data_[used_] = value;
        else
            value = data_[used_];
        used_ += 1;
    }

    void i8(int8_t& value) {
        auto raw = static_cast<uint8_t>(value);
        u8(raw);
        value = static_cast<int8_t>(raw);
    }

    void u16(uint16_t& value) {
        if (!room(2))
            return;
        if (writing_)
            putU16(data_ + used_, value);
        else
            value = getU16(data_ + used_);
        used_ += 2;
    }

    void grid(Grid& value) {
        auto raw = static_cast<uint8_t>(value);
        u8(raw);
        value = static_cast<Grid>(raw);
    }

    void boolean(bool& value) {
        uint8_t raw = value ? 1 : 0;
        u8(raw);
        value = raw != 0;
    }

    bool ok() const { return ok_; }
    size_t used() const { return used_; }

private:
    bool room(size_t count) {
        if (used_ + count > capacity_)
            ok_ = false;
        return ok_;
    }

    uint8_t* data_;
    size_t capacity_;
    size_t used_ = 0;
    bool writing_;
    bool ok_ = true;
};

void codeProject(Codec& codec, Project& project, uint16_t version) {
    codec.u16(project.bpm);
    codec.u8(project.swing);
    const int tracks = version >= 4 ? kTracks : 5;
    for (int t = 0; t < tracks; ++t) codec.boolean(project.swingEnabled[t]);
    for (DrumSound& sound : project.sounds) {
        codec.u8(sound.tune);
        codec.u8(sound.decay);
        codec.u8(sound.tone);
        codec.u8(sound.level);
    }
    for (DrumPattern& pattern : project.drums) {
        codec.u8(pattern.length);
        codec.grid(pattern.grid);
        for (auto& lane : pattern.velocity)
            for (uint8_t& velocity : lane)
                codec.u8(velocity);
    }
    for (auto& track : project.instruments) {
        for (InstrumentPattern& pattern : track) {
            codec.u8(pattern.length);
            codec.grid(pattern.grid);
            codec.u8(pattern.noteCount);
            for (Note& note : pattern.notes) {
                codec.u16(note.startTick);
                codec.u16(note.lengthTicks);
                codec.u8(note.pitch);
                codec.u8(note.velocity);
            }
        }
    }
    for (Scene& scene : project.scenes)
        for (int t = 0; t < tracks; ++t) codec.i8(scene.pattern[t]);
    for (SongEntry& entry : project.song) {
        codec.u8(entry.scene);
        codec.u8(entry.repeats);
    }
    codec.u8(project.songLength);
    if (version >= 2) {
        for (SynthSound& s : project.synths) {
            auto wave = static_cast<uint8_t>(s.wave);
            codec.u8(wave);
            s.wave = static_cast<Waveform>(wave);
            codec.u8(s.attack);
            codec.u8(s.decay);
            codec.u8(s.sustain);
            codec.u8(s.release);
            codec.u8(s.cutoff);
            codec.u8(s.resonance);
            codec.u8(s.glide);
            codec.u8(s.level);
        }
        for (bool& quantize : project.recordQuantize)
            codec.boolean(quantize);
    }
    if (version >= 3) {
        codec.u8(project.masterVolume);
        for (int t = 0; t < tracks; ++t) {
            auto& m = project.mixer[t];
            codec.u8(m.volume);
            codec.boolean(m.mute);
            codec.boolean(m.solo);
        }
        for (auto& a : project.arps) {
            auto mode = static_cast<uint8_t>(a.mode);
            codec.u8(mode);
            a.mode = static_cast<ArpMode>(mode);
            codec.grid(a.rate);
            codec.u8(a.octaves);
            codec.u8(a.gate);
        }
    }
    if (version >= 4) {
        for (auto& s : project.samples) {
            for (char& ch : s.source) { auto v = static_cast<uint8_t>(ch); codec.u8(v); ch = static_cast<char>(v); }
            uint16_t low = s.start, high = s.start >> 16;
            codec.u16(low); codec.u16(high); s.start = low | (uint32_t(high) << 16);
            low = s.end; high = s.end >> 16;
            codec.u16(low); codec.u16(high); s.end = low | (uint32_t(high) << 16);
        }
        for (auto& p : project.samplePatterns) {
            codec.u8(p.steps.length); codec.grid(p.steps.grid);
            for (auto& lane : p.steps.velocity) for (auto& v : lane) codec.u8(v);
            for (auto& slot : p.slot) codec.u8(slot);
        }
    }
    if (version >= 5) {
        for (auto& v : project.keyboardLayout) codec.u8(v);
        for (auto& v : project.keyboardRoot) codec.u8(v);
        for (auto& v : project.keyboardScale) codec.u8(v);
    }
    if(version>=6) {
        for(auto& gain:project.equalizer.gain)codec.i8(gain);
        codec.boolean(project.equalizer.enabled);
    }
}

// Keeps a large Project off the caller's stack.
Project& scratchProject() {
    static Project project;
    return project;
}

bool appendSuffix(char* out, const char* path, const char* suffix) {
    return static_cast<size_t>(snprintf(out, kMaxPathLength, "%s%s", path, suffix)) < kMaxPathLength;
}

LoadResult readImage(const char* path, Project& out, uint8_t* buffer, size_t capacity) {
    FILE* file = fopen(path, "rb");
    if (!file)
        return errno == ENOENT ? LoadResult::NotFound : LoadResult::ReadError;
    const size_t size = fread(buffer, 1, capacity, file);
    const bool error = ferror(file) != 0;
    const bool extra = fgetc(file) != EOF;
    fclose(file);
    if (error)
        return LoadResult::ReadError;
    if (extra)
        return LoadResult::BadSize;
    return deserializeProject(buffer, size, out);
}

// Validation can decode directly into scratch; no fourth full Project is needed.
bool isGoodFile(const char* path, uint8_t* buffer, size_t capacity) {
    return readImage(path, scratchProject(), buffer, capacity) == LoadResult::Ok;
}

}  // namespace

size_t serializeProject(const Project& project, uint8_t* buffer, size_t capacity) {
    if (capacity < kHeaderSize)
        return 0;
    Project& copy = scratchProject();
    copy = project;
    Codec codec(buffer + kHeaderSize, capacity - kHeaderSize, true);
    codeProject(codec, copy, kVersion);
    if (!codec.ok())
        return 0;
    memcpy(buffer, kMagic, sizeof(kMagic));
    putU16(buffer + 4, kVersion);
    putU32(buffer + 6, static_cast<uint32_t>(codec.used()));
    putU32(buffer + 10, checksum(buffer + kHeaderSize, codec.used()));
    return kHeaderSize + codec.used();
}

LoadResult deserializeProject(const uint8_t* data, size_t size, Project& out) {
    if (size < kHeaderSize)
        return LoadResult::BadSize;
    if (memcmp(data, kMagic, sizeof(kMagic)) != 0)
        return LoadResult::BadMagic;
    const uint16_t version = getU16(data + 4);
    if (version < 1 || version > kVersion)
        return LoadResult::BadVersion;
    const uint32_t payload = getU32(data + 6);
    if (payload != size - kHeaderSize)
        return LoadResult::BadSize;
    if (getU32(data + 10) != checksum(data + kHeaderSize, payload))
        return LoadResult::BadChecksum;
    Project& decoded = scratchProject();
    new (&decoded) Project{};
    Codec codec(const_cast<uint8_t*>(data + kHeaderSize), payload, false);
    codeProject(codec, decoded, version);
    if (!codec.ok() || codec.used() != payload)
        return LoadResult::BadSize;
    if (!isValidProject(decoded))
        return LoadResult::Invalid;
    out = decoded;
    return LoadResult::Ok;
}

bool saveProjectFile(const char* path, const Project& project, uint8_t* buffer, size_t capacity) {
    if (!isValidProject(project))
        return false;
    char temporary[kMaxPathLength], backup[kMaxPathLength];
    if (!appendSuffix(temporary, path, ".tmp") || !appendSuffix(backup, path, ".bak"))
        return false;
    const size_t size = serializeProject(project, buffer, capacity);
    if (size == 0)
        return false;
    FILE* file = fopen(temporary, "wb");
    if (!file)
        return false;
    const bool written = fwrite(buffer, 1, size, file) == size;
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        remove(temporary);
        return false;
    }
    if (!isGoodFile(temporary, buffer, capacity)) {
        remove(temporary);
        return false;
    }
    // FAT cannot rename over an existing file. Only a good primary may replace the backup, so a
    // damaged primary is dropped and the last good save stays loadable.
    if (isGoodFile(path, buffer, capacity)) {
        remove(backup);
        if (rename(path, backup) != 0)
            return false;
    } else {
        remove(path);
    }
    return rename(temporary, path) == 0;
}

LoadResult loadProjectFile(const char* path, Project& out, uint8_t* buffer, size_t capacity) {
    const LoadResult primary = readImage(path, out, buffer, capacity);
    if (primary == LoadResult::Ok)
        return primary;
    char backup[kMaxPathLength];
    if (!appendSuffix(backup, path, ".bak"))
        return primary;
    const LoadResult fallback = readImage(backup, out, buffer, capacity);
    // No main file means an interrupted save, so a good backup is the normal result.
    if (primary == LoadResult::NotFound)
        return fallback == LoadResult::Ok ? LoadResult::Ok : fallback;
    return fallback == LoadResult::Ok ? LoadResult::OkFromBackup : primary;
}

}  // namespace gb
