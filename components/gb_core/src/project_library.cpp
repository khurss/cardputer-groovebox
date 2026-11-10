#include "gb/project_library.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
namespace gb {
ProjectLibrary::ProjectLibrary(const char* dir, uint8_t* buffer, size_t size) : directory_(dir), buffer_(buffer), capacity_(size) {}
bool ProjectLibrary::validName(const char* name) {
    const size_t n = name ? strlen(name) : 0;
    if (!n || n >= kProjectName) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
bool ProjectLibrary::path(const char* name, char* out, size_t size) const {
    return validName(name) && size_t(snprintf(out, size, "%s/%s.gbp", directory_, name)) < size;
}
void ProjectLibrary::scan(ProjectLibraryState& state) const {
    state.count = 0; state.more = false;
    DIR* dir = opendir(directory_); state.available = dir != nullptr;
    if (!dir) return;
    while (auto* e = readdir(dir)) {
        const size_t n = strlen(e->d_name);
        if (n < 5 || n - 4 >= kProjectName || strcmp(e->d_name + n - 4, ".gbp")) continue;
        char name[kProjectName] = {}; memcpy(name, e->d_name, n - 4);
        if (!validName(name) || !strncmp(name, "stage3-", 7)) continue;
        if (state.count == kLibraryEntries) { state.more = true; continue; }
        strcpy(state.names[state.count++], name);
    }
    closedir(dir);
    for (unsigned i = 0; i < state.count; ++i) for (unsigned j = i + 1; j < state.count; ++j)
        if (strcmp(state.names[i], state.names[j]) > 0) for (unsigned k = 0; k < kProjectName; ++k) std::swap(state.names[i][k], state.names[j][k]);
}
bool ProjectLibrary::save(const char* name, const Project& project, bool exclusive) const {
    char file[96]; if (!path(name, file, sizeof(file))) return false;
    struct stat st;
    if (exclusive && stat(file, &st) == 0) return false;
    return saveProjectFile(file, project, buffer_, capacity_);
}
LoadResult ProjectLibrary::load(const char* name, Project& project) const {
    char file[96]; if (!path(name, file, sizeof(file))) return LoadResult::Invalid;
    return loadProjectFile(file, project, buffer_, capacity_);
}
bool ProjectLibrary::select(const char* name) const {
    if (!validName(name)) return false;
    char temp[96], dest[96], backup[96];
    snprintf(temp, sizeof(temp), "%s/active.tmp", directory_);
    snprintf(dest, sizeof(dest), "%s/active.txt", directory_);
    snprintf(backup, sizeof(backup), "%s/active.bak", directory_);
    FILE* f = fopen(temp, "wb"); if (!f) return false;
    bool ok = fwrite(name, 1, strlen(name), f) == strlen(name);
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) return false;
    // FatFs does not replace an existing destination with rename(). Rotate the
    // marker, and keep a bootable selection if power fails between renames.
    struct stat st;
    const bool hadPrevious = stat(dest, &st) == 0;
    if (hadPrevious) {
        if (stat(backup, &st) == 0 && remove(backup) != 0) return false;
        if (rename(dest, backup) != 0) return false;
    }
    if (rename(temp, dest) == 0) return true;
    if (hadPrevious) rename(backup, dest);
    return false;
}
bool ProjectLibrary::selected(char* name, size_t capacity) const {
    for (const char* suffix : {"active.txt", "active.bak"}) {
        char file[96], value[kProjectName] = {};
        snprintf(file, sizeof(file), "%s/%s", directory_, suffix);
        FILE* f = fopen(file, "rb"); if (!f) continue;
        const auto n = fread(value, 1, sizeof(value), f); fclose(f);
        if (n >= sizeof(value) || !validName(value) || n >= capacity) continue;
        strcpy(name, value); return true;
    }
    return false;
}
}
