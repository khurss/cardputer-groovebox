#include "gb/project_library.h"
#include "gb/commands.h"
#include "test.h"
#include <filesystem>
#include <cstring>
#include <unistd.h>
using namespace gb;
TEST(projectLibraryProtectsNamesAndRestoresNamedProjects) {
    char directory[] = "/tmp/gb-library-XXXXXX"; CHECK_EQ(mkdtemp(directory) != nullptr, true);
    uint8_t buffer[kProjectBufferSize]; ProjectLibrary library(directory, buffer, sizeof(buffer));
    static Project p, out; p = Project{}; initProject(p); p.bpm = 137;
    p.keyboardLayout[2] = 1; p.keyboardRoot[2] = 7; p.keyboardScale[2] = 2;
    CHECK_EQ(library.save("session-one", p, true), true);
    p.bpm = 99; CHECK_EQ(library.save("session-one", p, true), false);
    CHECK_EQ(library.save("../escape", p), false);
    CHECK_EQ(library.save("", p), false);
    CHECK_EQ(library.load("session-one", out), LoadResult::Ok);
    CHECK_EQ(out.bpm, 137); CHECK_EQ(out.keyboardScale[2], 2); CHECK_EQ(out.keyboardRoot[2], 7); CHECK_EQ(out.keyboardLayout[2], 1);
    CHECK_EQ(library.select("session-one"), true);
    char selected[kProjectName]; CHECK_EQ(library.selected(selected, sizeof(selected)), true);
    CHECK_EQ(strcmp(selected, "session-one"), 0);
    ProjectLibraryState state; library.scan(state); CHECK_EQ(state.count, 1); CHECK_EQ(state.available, true);
    CHECK_EQ(library.load("missing", out), LoadResult::NotFound); CHECK_EQ(out.bpm, 137);
    CHECK_EQ(library.save("session-one", p), true);
    CHECK_EQ(library.load("session-one", out), LoadResult::Ok); CHECK_EQ(out.bpm, 99);
    CHECK_EQ(library.select("second"), true);
    CHECK_EQ(library.selected(selected, sizeof(selected)), true); CHECK_EQ(strcmp(selected,"second"),0);
    std::filesystem::remove(std::string(directory) + "/active.txt");
    CHECK_EQ(library.selected(selected, sizeof(selected)), true); CHECK_EQ(strcmp(selected,"session-one"),0);
    std::filesystem::remove_all(directory);
}
