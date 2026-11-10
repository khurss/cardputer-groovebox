#pragma once
#include <cstdio>
#include <vector>

struct TestCase {
    const char* name;
    void (*run)();
};

inline std::vector<TestCase>& testCases() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& testFailures() {
    static int failures = 0;
    return failures;
}

struct TestRegistrar {
    TestRegistrar(const char* name, void (*run)()) { testCases().push_back({name, run}); }
};

#define TEST(name)                                  \
    static void name();                             \
    static TestRegistrar registrar_##name(#name, name); \
    static void name()

#define CHECK_EQ(actual, expected)                                                                  \
    do {                                                                                            \
        const auto a_ = (actual);                                                                   \
        const auto e_ = (expected);                                                                 \
        if (!(a_ == e_)) {                                                                          \
            std::printf("%s:%d: %s: got %lld, expected %lld\n", __FILE__, __LINE__, #actual,       \
                        static_cast<long long>(a_), static_cast<long long>(e_));                    \
            ++testFailures();                                                                       \
        }                                                                                           \
    } while (0)
