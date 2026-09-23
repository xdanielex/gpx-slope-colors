# gpx-slope-colors - build file
#
#   make              native command line tool  -> bin/gpx-slope-colors
#   make test         build and run the test suite
#   make windows      Windows .exe (GUI + CLI)  -> dist/
#   make clean        remove everything built
#
# The Windows targets need mingw-w64:
#   Debian/Ubuntu     sudo apt install mingw-w64
#   Fedora            sudo dnf install mingw64-gcc-c++
#   macOS (brew)      brew install mingw-w64

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
MINGW    ?= x86_64-w64-mingw32-g++
STRIPW   ?= x86_64-w64-mingw32-strip

CORE   := src/slope_core.cpp
HDR    := src/slope_core.hpp
WINLIB := -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -luser32
WINFLG := -static -static-libgcc -static-libstdc++

.PHONY: all test windows windows-gui windows-cli clean

all: bin/gpx-slope-colors

bin/gpx-slope-colors: src/main_cli.cpp $(CORE) $(HDR)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ src/main_cli.cpp $(CORE)

bin/tests: tests/test_slope.cpp $(CORE) $(HDR)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ tests/test_slope.cpp $(CORE)

test: bin/tests
	./bin/tests

windows: windows-gui windows-cli

windows-gui: src/main_gui.cpp $(CORE) $(HDR)
	@mkdir -p dist
	$(MINGW) $(CXXFLAGS) -municode -mwindows \
	    -o dist/gpx-slope-colors.exe src/main_gui.cpp $(CORE) \
	    $(WINFLG) $(WINLIB)
	-$(STRIPW) dist/gpx-slope-colors.exe

windows-cli: src/main_cli.cpp $(CORE) $(HDR)
	@mkdir -p dist
	$(MINGW) $(CXXFLAGS) \
	    -o dist/gpx-slope-colors-cli.exe src/main_cli.cpp $(CORE) \
	    $(WINFLG)
	-$(STRIPW) dist/gpx-slope-colors-cli.exe

clean:
	rm -rf bin dist
