#include "test.h"

int main() {
    for (const TestCase& test : testCases()) test.run();
    std::printf("%zu tests, %d failures\n", testCases().size(), testFailures());
    return testFailures() == 0 ? 0 : 1;
}
