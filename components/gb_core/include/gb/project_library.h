#pragma once
#include "gb/project_io.h"
namespace gb {
constexpr unsigned kLibraryEntries = 32;
constexpr unsigned kProjectName = 24;
enum class ProjectAction : uint8_t { None, Refresh, Save, New, Copy, Load, Demos };
struct ProjectLibraryState {
    char names[kLibraryEntries][kProjectName] = {};
    char current[kProjectName] = "default";
    char message[40] = {};
    uint8_t count = 0;
    bool available = false, more = false;
};
// All filesystem work runs on the UI task, only while transport/storage are idle.
class ProjectLibrary {
public:
    ProjectLibrary(const char* directory, uint8_t* buffer, size_t capacity);
    void scan(ProjectLibraryState& state) const;
    bool select(const char* name) const;
    bool selected(char* name, size_t capacity) const;
    bool save(const char* name, const Project& project, bool exclusive = false) const;
    LoadResult load(const char* name, Project& project) const;
    bool path(const char* name, char* out, size_t size) const;
    static bool validName(const char* name);
private:
    const char* directory_;
    uint8_t* buffer_;
    size_t capacity_;
};
}
