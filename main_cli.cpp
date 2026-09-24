// main_cli.cpp - command line front end.

#include "slope_core.hpp"

namespace styler { int styleMain(int argc, char **argv); }

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
"      --style ...        batch-apply OsmAnd appearance to whole folders,\n"
"                         without recolouring by slope. See --style --help\n"
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
"      --split-files      separate files for uphill, downhill and flat,\n"
"                         so each can be shown on its own in OsmAnd\n"
"      --separate-files   with several inputs, write one output file each\n"
"                         (default: merge them into one file to import)\n"
"      --merge-name NAME  name of the merged file (default all-tracks.gpx)\n"

"      --width W          thin | medium | bold | 1-24 (24 = maximum)\n"
"      --no-arrows        do not show direction arrows\n"
"      --keep-waypoints   copy <wpt> waypoints into the output\n"
"      --markers M        distance markers along the track, every M metres.\n"
"                         \"off\" removes markers the file already has. Left\n"
"                         out, nothing is written. --markers-time S for time\n"
"      --3d TYPE          3D wall under the track: altitude | fixed_height.\n"
"                         \"none\" switches off a wall the file already has.\n"
"                         Left out entirely, nothing is written at all.\n"
"                         Seen best with the map tilted into 3D, though\n"
"                         it also shows in 2D at close zoom. Works on\n"
"                         the free OsmAnd - no subscription needed\n"
"      --3d-scale N       wall height multiplier (default 1.0)\n"
"      --3d-wall C        wall colour: solid (default, follows the slope\n"
"                         colours) | upward_gradient | downward_gradient\n"
"                         | altitude | speed | slope\n"
"      --3d-position P    top | bottom | top_bottom   (default bottom)\n"
"      --3d-height M      metres, only with --3d fixed_height\n"
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
"  Several stages, merged into ONE file to import:\n"
"      " << prog << " stages/*.gpx\n"
"      -> writes  stages/all-tracks.gpx, holding every stage with its\n"
"         uphill/downhill colours. One import in OsmAnd, no configuring\n"
"\n"
"  The same, but one coloured file per stage:\n"
"      " << prog << " stages/*.gpx --separate-files\n"
"      -> writes  stages/tappa-01_slope.gpx, tappa-02_slope.gpx, ...\n"
"\n"
"  Whole folder at once, into one place:\n"
"      " << prog << " --output-dir coloured *.gpx\n"
"      -> writes  coloured/all-tracks.gpx\n"
"\n"
"where the output goes\n"
"---------------------\n"
"  With SEVERAL input files the results are merged into one all-tracks.gpx\n"
"  next to the first input, so OsmAnd needs a single import. Use\n"
"  --separate-files for the old one-output-per-input behaviour.\n"
"\n"
"  With ONE input file, or with --separate-files, the file is written NEXT\n"
"  TO THE INPUT FILE (not in the folder you run the command from), with the\n"
"  suffix added:\n"
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
    // Two modes in one binary. --style must be the first argument: it takes a
    // folder rather than a file, and its options have nothing in common with
    // the slope ones, so mixing them would only create confusion.
    if (argc > 1 && std::strcmp(argv[1], "--style") == 0) {
        return styler::styleMain(argc, argv);
    }

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
        } else if (a == "--markers") {
            // distance markers along the track
            if (i + 1 >= argc) {
                std::cerr << "Error: --markers needs a value (metres, or 'off').\n";
                return 2;
            }
            std::string v = argv[++i];
            if (v == "off" || v == "no" || v == "none") {
                opt.splitType = "no_split";
                opt.splitInterval.clear();
            } else {
                opt.splitType = "distance";
                opt.splitInterval = v;
            }
        } else if (a == "--markers-time") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --markers-time needs a value in seconds.\n";
                return 2;
            }
            opt.splitType = "time";
            opt.splitInterval = argv[++i];
        } else if (a == "--3d") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --3d needs a value. Try --3d altitude\n";
                return 2;
            }
            opt.viz3d = argv[++i];
        } else if (a == "--3d-wall") {
            if (i + 1 >= argc) { std::cerr << "Error: --3d-wall needs a value.\n"; return 2; }
            opt.wall3d = argv[++i];
        } else if (a == "--3d-position") {
            if (i + 1 >= argc) { std::cerr << "Error: --3d-position needs a value.\n"; return 2; }
            opt.wallPos3d = argv[++i];
        } else if (a == "--3d-scale") {
            if (i + 1 >= argc) { std::cerr << "Error: --3d-scale needs a number.\n"; return 2; }
            opt.scale3d = argv[++i];
        } else if (a == "--3d-height") {
            if (i + 1 >= argc) { std::cerr << "Error: --3d-height needs metres.\n"; return 2; }
            opt.height3d = argv[++i];
        } else if (a == "--separate-files" || a == "--no-merge") {
            opt.merge = false;
        } else if (a == "--merge") {
            opt.merge = true;
        } else if (a == "--merge-name") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --merge-name needs a value.\n";
                return 2;
            }
            opt.mergeName = argv[++i];
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

    // Merging is for several inputs. With one file, or with --split-files
    // (which already writes one file per class), it would only get in the way.
    const bool merging =
        opt.merge && inputs.size() > 1 && !opt.splitFiles && opt.output.empty();

    // A shell glob run twice hands back what the previous run wrote. With one
    // file named explicitly that is the user's choice and is honoured; with
    // several, output of an earlier run is dropped. Same rule as --style.
    if (inputs.size() > 1 && !opt.suffix.empty()) {
        std::vector<std::string> keep;
        size_t dropped = 0;
        for (const std::string &p : inputs) {
            std::string stem = stripExtension(baseName(p));
            if (stem.size() > opt.suffix.size() &&
                stem.compare(stem.size() - opt.suffix.size(),
                             opt.suffix.size(), opt.suffix) == 0) {
                ++dropped;
                continue;
            }
            keep.push_back(p);
        }
        if (!keep.empty() && dropped) {
            if (!quiet)
                std::cout << "skipping " << dropped << " file(s) already ending in \""
                          << opt.suffix << "\": output of an earlier run\n";
            inputs = keep;
        }
    }

    // The merged file lands beside its own sources, so a shell glob run twice
    // hands it back as an input and every track doubles. Drop it up front.
    std::string mergePath;
    if (merging) {
        std::string dir = opt.outputDir;
        if (dir.empty()) dir = dirName(inputs[0]);
        std::string name = opt.mergeName.empty() ? "all-tracks" : opt.mergeName;
        if (name.size() < 4 || name.substr(name.size() - 4) != ".gpx")
            name += ".gpx";
        mergePath = dir.empty() ? name : joinPath(dir, name);

        std::vector<std::string> keep;
        size_t dropped = 0;
        for (const std::string &p : inputs) {
            if (baseName(p) == baseName(mergePath)) { ++dropped; continue; }
            keep.push_back(p);
        }
        if (keep.empty()) {
            std::cerr << "Error: the only input is " << baseName(mergePath)
                      << ", which this run would create.\n"
                         "Point it at the original tracks, or use "
                         "--merge-name for another name.\n";
            return 1;
        }
        if (dropped && !quiet)
            std::cout << "skipping " << baseName(mergePath)
                      << ": that is the merged file itself\n";
        inputs = keep;
    }

    MergeSink sink;
    int failures = 0;
    for (const std::string &in : inputs) {
        Stats st;
        std::string e;
        if (!process(in, opt, st, e, merging ? &sink : nullptr)) {
            std::cerr << baseName(in) << ": " << e << "\n";
            ++failures;
            continue;
        }
        if (quiet) continue;

        if (st.noElevation)
            std::cout << "  " << baseName(in)
                      << ": no elevation data - kept, but not coloured by "
                         "slope. Add elevations in OsmAnd (Analyse on map -> "
                         "Correct altitude) and run it again.\n";
        else if (merging)
            std::cout << baseName(in) << ": " << st.sections << " sections\n";
        for (const std::string &w : st.written)
            std::cout << "written: " << w << "  (" << st.sections << " sections)\n";

        if (st.noElevation) continue;   // nothing measured, nothing to report

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

    if (merging && sink.files > 0) {
        const std::string &path = mergePath;
        std::string e;
        if (!writeMerged(sink, path, opt, e)) {
            std::cerr << e << "\n";
            return 1;
        }
        if (!quiet) {
            std::cout << "written: " << path << "\n"
                      << sink.files << " file(s) merged, "
                      << sink.trackCount << " coloured sections";
            if (sink.waypointCount)
                std::cout << ", " << sink.waypointCount << " waypoint(s)";
            std::cout << "\nImport this one file into OsmAnd, "
                         "with \"import as one track\".\n";
        }
    }

    if (failures && !quiet)
        std::cerr << failures << " file(s) failed.\n";
    return failures ? 1 : 0;
}
