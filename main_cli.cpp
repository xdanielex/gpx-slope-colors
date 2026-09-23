// main_cli.cpp - command line front end.

#include "slope_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace slope;

namespace {

void printUsage(const char *prog) {
    std::cout <<
"usage: " << prog << " [options] FILE.gpx [FILE.gpx ...]\n"
"\n"
"Colour a GPX track by slope: uphill in one colour, downhill in another\n"
"(for OsmAnd). Colours follow the direction of travel, i.e. the order of\n"
"the points in the file.\n"
"\n"
"options:\n"
"  -h, --help             show this help and exit\n"
"  -o, --output PATH      output file (single input only; default: next to\n"
"                         the input file, with the suffix added)\n"
"      --output-dir DIR   write results into DIR instead\n"
"      --suffix TEXT      suffix added to the name (default _slope)\n"
"      --threshold PCT    gradient % below which a stretch is flat (1.5)\n"
"      --window M         metres for smoothing and gradient (50)\n"
"      --minlen M         shortest coloured section in metres (80)\n"
"      --uphill COLOR     uphill colour: name or #rrggbb (red)\n"
"      --downhill COLOR   downhill colour (green)\n"
"      --flat COLOR       flat colour (purple)\n"
"      --no-flat          no flat class: everything is uphill or downhill\n"
"      --split-files      one file per class instead of a single one\n"

"      --width W          thin | medium | bold | 1-24 (24 = maximum)\n"
"      --no-arrows        do not show direction arrows\n"
"      --keep-waypoints   copy <wpt> waypoints into the output\n"
"      --quiet            print errors only\n"
"      --colors           list the colour names and exit\n"
"      --version          print the version and exit\n"
"\n"
"examples\n"
"--------\n"
"  Basic run (all defaults):\n"
"      " << prog << " ride.gpx\n"
"      -> writes  ride_slope.gpx\n"
"\n"
"  Choosing the output name:\n"
"      " << prog << " ride.gpx -o coloured.gpx\n"
"\n"
"  A different suffix:\n"
"      " << prog << " ride.gpx --suffix _osmand\n"
"      -> writes  ride_osmand.gpx\n"
"\n"
"  Colours by name or hex:\n"
"      " << prog << " ride.gpx --uphill orange --downhill lightblue\n"
"      " << prog << " ride.gpx --uphill \"#ff8800\" --downhill \"#0044cc\"\n"
"\n"
"  Long track, less fragmentation, thinner line:\n"
"      " << prog << " ride.gpx --window 100 --minlen 250 --width 16\n"
"\n"
"  Whole folder at once, into one place:\n"
"      " << prog << " --output-dir coloured *.gpx\n"
"\n"
"where the output goes\n"
"---------------------\n"
"  Without -o the file is written NEXT TO THE INPUT FILE (not in the folder\n"
"  you run the command from), with the suffix added:\n"
"\n"
"      ride.gpx            ->  ride_slope.gpx\n"
"      tracks/ride.gpx     ->  tracks/ride_slope.gpx\n"
"\n"
"  An existing output file is overwritten without asking.\n"
"\n"
"tuning\n"
"------\n"
"  --threshold  raise it if flat ground is broken into many tiny sections.\n"
"               Road cycling 1, MTB 1.5, hiking 3.\n"
"  --window     metres over which elevations are smoothed. Higher is calmer.\n"
"  --minlen     shortest section that keeps its own colour. Raise this first\n"
"               when the result looks choppy. 250 suits routes over 50 km.\n"
"\n"
"The input GPX must contain <ele> elevation data.\n";
}

void printColors() {
    std::cout << "Colour names (use with --uphill / --downhill / --flat):\n\n";
    for (const NamedColor &c : palette()) {
        std::cout << "  " << c.hex << "   " << c.name;
        if (c.aliases && c.aliases[0]) std::cout << "   (aliases: " << c.aliases << ")";
        std::cout << "\n";
    }
    std::cout << "\nYou can also pass a hex code directly, e.g. --uphill '#ff8800'\n";
}

bool needValue(int i, int argc, const char *flag) {
    if (i + 1 >= argc) {
        std::cerr << "Error: " << flag << " needs a value.\n";
        return false;
    }
    return true;
}

bool parseDouble(const char *s, double &out, const char *flag) {
    char *end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s || (end && *end != '\0')) {
        std::cerr << "Error: " << flag << " must be a number (got '" << s << "').\n";
        return false;
    }
    if (v < 0) {
        std::cerr << "Error: " << flag << " cannot be negative.\n";
        return false;
    }
    out = v;
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Options opt;
    std::vector<std::string> inputs;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")    { printUsage("gpx-slope-colors"); return 0; }
        if (a == "--version")              { std::cout << "gpx-slope-colors " << VERSION << "\n"; return 0; }
        if (a == "--colors" || a == "--colours") { printColors(); return 0; }

        if (a == "-o" || a == "--output") {
            if (!needValue(i, argc, "--output")) return 2;
            opt.output = argv[++i];
        } else if (a == "--output-dir") {
            if (!needValue(i, argc, "--output-dir")) return 2;
            opt.outputDir = argv[++i];
        } else if (a == "--suffix") {
            if (!needValue(i, argc, "--suffix")) return 2;
            opt.suffix = argv[++i];
        } else if (a == "--threshold") {
            if (!needValue(i, argc, "--threshold")) return 2;
            if (!parseDouble(argv[++i], opt.threshold, "--threshold")) return 2;
        } else if (a == "--window") {
            if (!needValue(i, argc, "--window")) return 2;
            if (!parseDouble(argv[++i], opt.window, "--window")) return 2;
        } else if (a == "--minlen") {
            if (!needValue(i, argc, "--minlen")) return 2;
            if (!parseDouble(argv[++i], opt.minlen, "--minlen")) return 2;
        } else if (a == "--uphill") {
            if (!needValue(i, argc, "--uphill")) return 2;
            opt.uphill = argv[++i];
        } else if (a == "--downhill") {
            if (!needValue(i, argc, "--downhill")) return 2;
            opt.downhill = argv[++i];
        } else if (a == "--flat") {
            if (!needValue(i, argc, "--flat")) return 2;
            opt.flat = argv[++i];
        } else if (a == "--width") {
            if (!needValue(i, argc, "--width")) return 2;
            opt.width = argv[++i];
        } else if (a == "--no-flat") {
            opt.noFlat = true;
        } else if (a == "--split-files") {
            opt.splitFiles = true;
        } else if (a == "--no-arrows") {
            opt.arrows = false;
        } else if (a == "--keep-waypoints") {
            opt.keepWaypoints = true;
        } else if (a == "--quiet") {
            quiet = true;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "Error: unknown option '" << a << "'\n"
                      << "Try --help.\n";
            return 2;
        } else {
            inputs.push_back(a);
        }
    }

    if (inputs.empty()) {
        std::cerr << "Error: no input file.\n"
                  << "Try --help, or --colors to list the colour names.\n";
        return 2;
    }
    if (inputs.size() > 1 && !opt.output.empty()) {
        std::cerr << "Error: -o works with a single input file. "
                     "Use --output-dir for several.\n";
        return 2;
    }

    // fail fast on bad settings, before touching any file
    std::string err, dummy;
    if (!resolveColor(opt.uphill, dummy, err))   { std::cerr << err << "\n"; return 1; }
    if (!resolveColor(opt.downhill, dummy, err)) { std::cerr << err << "\n"; return 1; }
    if (!resolveColor(opt.flat, dummy, err))     { std::cerr << err << "\n"; return 1; }
    if (!validWidth(opt.width, err))             { std::cerr << err << "\n"; return 1; }

    int failures = 0;
    for (const std::string &in : inputs) {
        Stats st;
        std::string e;
        if (!process(in, opt, st, e)) {
            std::cerr << baseName(in) << ": " << e << "\n";
            ++failures;
            continue;
        }
        if (quiet) continue;

        for (const std::string &w : st.written)
            std::cout << "written: " << w << "  (" << st.sections << " sections)\n";

        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "\nPoints: %zu   Length: %.2f km   Sections: %zu\n",
                      st.points, st.totalM / 1000.0, st.sections);
        std::cout << buf;

        struct Row { const char *label; double m; const std::string &col; };
        std::string upHex, downHex, flatHex;
        resolveColor(opt.uphill, upHex, e);
        resolveColor(opt.downhill, downHex, e);
        resolveColor(opt.flat, flatHex, e);
        const Row rows[3] = {{"Uphill", st.uphillM, upHex},
                             {"Downhill", st.downhillM, downHex},
                             {"Flat", st.flatM, flatHex}};
        for (const Row &r : rows) {
            if (r.m <= 0) continue;
            std::snprintf(buf, sizeof(buf), "  %-9s %7.2f km  (%4.1f%%)  %s\n",
                          r.label, r.m / 1000.0,
                          st.totalM > 0 ? r.m / st.totalM * 100.0 : 0.0,
                          r.col.c_str());
            std::cout << buf;
        }
        if (st.waypoints)
            std::cout << "  " << st.waypoints << " waypoint(s) copied\n";
        std::cout << "\n";
    }

    if (failures && !quiet)
        std::cerr << failures << " file(s) failed.\n";
    return failures ? 1 : 0;
}
