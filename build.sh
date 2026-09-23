#!/bin/sh
# Build gpx-slope-colors on macOS or Linux.
#
#   sh build.sh
#
# Produces an executable called "gpx-slope-colors" in this folder.
# The only requirement is a C++ compiler, which you almost certainly have:
#
#   macOS    xcode-select --install
#   Debian   sudo apt install g++
#   Fedora   sudo dnf install gcc-c++

set -e
cd "$(dirname "$0")"

CXX="${CXX:-c++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "No C++ compiler found."
    echo
    echo "  macOS   run:  xcode-select --install"
    echo "  Debian  run:  sudo apt install g++"
    echo "  Fedora  run:  sudo dnf install gcc-c++"
    exit 1
fi

echo "Compiling with $CXX ..."
"$CXX" -std=c++17 -O2 -o gpx-slope-colors main_cli.cpp slope_core.cpp
chmod +x gpx-slope-colors

echo
echo "Done. Built: $(pwd)/gpx-slope-colors"
echo
echo "Try it:"
echo "  ./gpx-slope-colors ../example/demo.gpx"
echo
echo "See every option:"
echo "  ./gpx-slope-colors --help"
echo
echo "To use it from anywhere, copy it somewhere on your PATH:"
echo "  sudo cp gpx-slope-colors /usr/local/bin/"
