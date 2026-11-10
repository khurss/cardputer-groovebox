CXX := g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Icomponents/gb_core/include -Itests
SOURCES := $(wildcard components/gb_core/src/*.cpp) $(wildcard tests/*.cpp)
HEADERS := $(wildcard components/gb_core/include/gb/*.h) tests/test.h

.PHONY: test clean
test: build/tests
	./build/tests

build/tests: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

clean:
	rm -rf build
