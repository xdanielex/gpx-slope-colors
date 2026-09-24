# gpx-slope-colors - build file
#
#   make              native command line tool  -> gpx-slope-colors
#   make test         build and run both test suites
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

CORE   := slope_core.cpp styler_core.cpp styler_palette.cpp
HDR    := slope_core.hpp styler_core.hpp
CLI    := main_cli.cpp styler_cli.cpp
WINLIB := -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -luser32
WINFLG := -static -static-libgcc -static-libstdc++

.PHONY: check-mingw all test windows windows-gui windows-cli clean

all: gpx-slope-colors

gpx-slope-colors: $(CLI) $(CORE) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(CLI) $(CORE)

tests: test_slope.cpp $(CORE) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_slope.cpp $(CORE)

tests-styler: test_styler.cpp $(CORE) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ test_styler.cpp $(CORE)

test: tests tests-styler
	./tests
	./tests-styler

windows: windows-gui windows-cli

# Version resource: gives the .exe a name, version and author.
# Without it the binary is anonymous, which antivirus heuristics dislike.
# Fail loudly when the cross-compiler is missing. A silent failure here once
# shipped a stale .exe alongside fresh sources, which is worse than no build.
check-mingw:
	@command -v $(WINDRES) >/dev/null 2>&1 || { \
	  echo "ERROR: $(WINDRES) not found - install mingw-w64"; exit 1; }
	@command -v $(MINGW) >/dev/null 2>&1 || { \
	  echo "ERROR: $(MINGW) not found - install mingw-w64"; exit 1; }

version.o: version.rc check-mingw
	$(WINDRES) version.rc -O coff -o version.o

windows-gui: check-mingw main_gui.cpp $(CORE) $(HDR) version.o
	$(MINGW) $(CXXFLAGS) -municode -mwindows \
	    -o gpx-slope-colors.exe main_gui.cpp $(CORE) version.o \
	    $(WINFLG) $(WINLIB)
	-$(STRIPW) gpx-slope-colors.exe

windows-cli: check-mingw $(CLI) $(CORE) $(HDR) version.o
	$(MINGW) $(CXXFLAGS) \
	    -o gpx-slope-colors-cli.exe $(CLI) $(CORE) version.o \
	    $(WINFLG)
	-$(STRIPW) gpx-slope-colors-cli.exe

clean:
	rm -f gpx-slope-colors tests tests-styler version.o \
	      gpx-slope-colors.exe gpx-slope-colors-cli.exe
