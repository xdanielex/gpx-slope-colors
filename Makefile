# gpx-slope-colors - build file
#
#   make              native command line tool  -> gpx-slope-colors
#   make test         build and run the test suite
#   make windows      Windows .exe (GUI + CLI)
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
WINDRES  ?= x86_64-w64-mingw32-windres

CORE   := slope_core.cpp
HDR    := slope_core.hpp
WINLIB := -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -luser32
WINFLG := -static -static-libgcc -static-libstdc++

.PHONY: all test windows windows-gui windows-cli clean

all: gpx-slope-colors

gpx-slope-colors: main_cli.cpp $(CORE) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ main_cli.cpp $(CORE)

tests: test_slope.cpp $(CORE) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_slope.cpp $(CORE)

test: tests
	./tests

windows: windows-gui windows-cli

# Version resource: gives the .exe a name, version and author.
# Without it the binary is anonymous, which antivirus heuristics dislike.
version.o: version.rc
	$(WINDRES) version.rc -O coff -o version.o

windows-gui: main_gui.cpp $(CORE) $(HDR) version.o
	$(MINGW) $(CXXFLAGS) -municode -mwindows \
	    -o gpx-slope-colors.exe main_gui.cpp $(CORE) version.o \
	    $(WINFLG) $(WINLIB)
	-$(STRIPW) gpx-slope-colors.exe

windows-cli: main_cli.cpp $(CORE) $(HDR) version.o
	$(MINGW) $(CXXFLAGS) \
	    -o gpx-slope-colors-cli.exe main_cli.cpp $(CORE) version.o \
	    $(WINFLG)
	-$(STRIPW) gpx-slope-colors-cli.exe

clean:
	rm -f gpx-slope-colors tests version.o gpx-slope-colors.exe gpx-slope-colors-cli.exe
