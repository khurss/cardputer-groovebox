CXX := g++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Icomponents/gb_core/include -Itests
CORE := $(wildcard components/gb_core/src/*.cpp)
SOURCES := $(CORE) $(wildcard tests/*.cpp)
HEADERS := $(wildcard components/gb_core/include/gb/*.h) tests/test.h

.PHONY: test render render-synth clean
test: build/tests
	./build/tests

render: build/render_drums
	./build/render_drums build/drums.wav

build/tests: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

build/render_drums: tools/render_drums.cpp $(CORE) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) tools/render_drums.cpp $(CORE) -o $@

clean:
	rm -rf build

render-synth: build/render_synth
	./build/render_synth build/synth.wav

build/render_synth: tools/render_synth.cpp $(CORE) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) tools/render_synth.cpp $(CORE) -o $@
