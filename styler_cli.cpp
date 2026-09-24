// styler_cli.cpp - the --style mode of the command line front end.
//
// Kept apart from main_cli.cpp so the slope options and the styling options
// cannot be confused with one another: they take different inputs (one file
// versus a whole folder) and produce different output.

#include "styler_core.hpp"
#include "slope_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace styler {
namespace {

bool needValue(int i, int argc, const char *flag) {
    if (i + 1 >= argc) {
        std::cerr << "Error: " << flag << " needs a value.\n";
        return false;
    }
    return true;
}

bool parseOnOff(const std::string &v, Tri &out, const char *flag) {
    if (v == "on"  || v == "true"  || v == "yes" || v == "1") { out = Tri::On;  return true; }
    if (v == "off" || v == "false" || v == "no"  || v == "0") { out = Tri::Off; return true; }
    std::cerr << "Error: " << flag << " takes on or off (got '" << v << "').\n";
    return false;
}

void printStyleHelp(const char *prog) {
    std::cout <<
"usage: " << prog << " --style [options] FOLDER-OR-FILE [MORE...]\n"
"\n"
"Apply OsmAnd appearance settings to GPX files in bulk. Track points are\n"
"never modified: the tool only adds the extensions OsmAnd reads as styling.\n"
"\n"
"Use it when you have a walk split into stages, or a folder of tracks\n"
"downloaded from different sites, and you do not want to configure each one\n"
"by hand on the phone.\n"
"\n"
"colour\n"
"------\n"
"      --color COLOR      one colour for everything: name or #rrggbb\n"
"      --auto-color[=M]   a different colour per file. M is stable (default)\n"
"                         or sequential\n"
"      --palette NAME     distinct | warm | cool | colorblind\n"
"      --per-track-color  vary the colour between tracks inside each file,\n"
"                         for a file holding several stages\n"
"\n"
"appearance\n"
"----------\n"
"      --width W          thin | medium | bold | 1-24\n"
"      --arrows on|off    direction arrows\n"
"      --start-finish on|off\n"
"      --split TYPE       no_split | distance | time\n"
"      --split-interval N metres for distance, seconds for time\n"
"      --coloring TYPE    solid | speed | altitude\n"
"      --3d TYPE          3D wall under the track: altitude | fixed_height.\n"
"                         \"none\" switches off a wall the file already has.\n"
"                         Left out entirely, nothing is written at all.\n"
"                         Seen best with the map tilted into 3D, though\n"
"                         it also shows in 2D at close zoom. Works on\n"
"                         the free OsmAnd - no subscription needed\n"
"      --3d-scale N       wall height multiplier (default 1.0)\n"
"      --3d-wall C        wall colour: solid (default, follows each track's\n"
"                         own colour) | upward_gradient | downward_gradient\n"
"                         | altitude | speed | slope\n"
"      --3d-position P    top | bottom | top_bottom   (default bottom)\n"
"      --3d-height M      metres, only with --3d fixed_height\n"
"                         (slope and the route types need OsmAnd Pro)\n"
"\n"
"metadata and waypoints\n"
"----------------------\n"
"      --activity ID      OsmAnd activity type, e.g. hiking\n"
"      --group-by-type    read each <wpt>'s <type> and give every group its\n"
"                         own icon and colour, automatically\n"
"      --wpt-icon NAME    one icon for every waypoint\n"
"      --wpt-color COLOR\n"
"      --wpt-background S circle | square | octagon\n"
"      --icons            list the icon names and exit\n"
"\n"
"existing settings\n"
"-----------------"
"\n"
"      --keep-existing    leave any osmand tags already in the file alone\n"
"      --strip-existing   remove them all before writing\n"
"                         (default: replace only what you asked for, and\n"
"                         always keep Garmin, Wikiloc and Strava tags)\n"
"\n"
"one file or many\n"
"----------------\n"
"      (default)          all tracks are merged into ONE file, so OsmAnd\n"
"                         needs a single import and no configuration.\n"
"                         Each track keeps its own colour\n"
"      --merge-name NAME  name of that file (default all-tracks.gpx)\n"
"      --separate-files   write one styled file per input instead\n"
"\n"
"input and output\n"
"----------------\n"
"  -r, --recursive        include subfolders\n"
"      --out-dir DIR      write results into DIR\n"
"      --suffix TEXT      suffix for the new names (default _osmand)\n"
"                         Files already ending with it are skipped when a\n"
"                         folder is scanned, so running twice is harmless\n"
"      --in-place         overwrite the originals\n"
"      --backup           with --in-place, keep a .bak copy\n"
"  -n, --dry-run          show what would happen, write nothing\n"
"      --quiet            errors only\n"
"\n"
"examples\n"
"--------\n"
"  Six stages of a walk, each a different colour, thick lines with arrows:\n"
"      " << prog << " --style stages --auto-color --width 12 --arrows on\n"
"      -> writes stages/all-tracks.gpx: ONE file holding all six stages,\n"
"         each in its own colour. Import that single file into OsmAnd\n"
"\n"
"  The same, but one styled file per stage instead of a merged one:\n"
"      " << prog << " --style stages --auto-color --width 12 --separate-files\n"
"      -> writes stages/tappa-01_osmand.gpx ... tappa-06_osmand.gpx\n"
"\n"
"  A whole archive merged into one file, marked as hiking:\n"
"      " << prog << " --style tracks -r --auto-color --activity hiking \\\n"
"          --out-dir tracks-osmand --merge-name my-archive\n"
"      -> writes tracks-osmand/my-archive.gpx\n"
"\n"
"  One file, several stages inside it, a colour each:\n"
"      " << prog << " --style camino.gpx --auto-color --separate-files \\\n"
"          --per-track-color\n"
"      -> writes camino_osmand.gpx\n"
"\n"
"  Waypoints too, icons picked from their <type>:\n"
"      " << prog << " --style trails --auto-color --group-by-type\n"
"\n"
"  See what would change, without changing it:\n"
"      " << prog << " --style trails --auto-color --dry-run\n"
"\n"
"notes\n"
"-----\n"
"  In OsmAnd, import with \"import as one track\", otherwise per-track colour\n"
"  and width are not applied. This matters most for the merged file, which\n"
"  is one file holding many tracks.\n"
"\n"
"  Colour and width work per track. Arrows, start/finish, split and the\n"
"  waypoint groups are file-wide: that is OsmAnd's behaviour, not a limit of\n"
"  this tool.\n"
"\n"
"  --coloring slope needs an OsmAnd Pro subscription. To colour by slope\n"
"  without one, use this program's slope mode instead, which works out the\n"
"  colours itself and writes them as plain solid tracks.\n";
}

void printIcons() {
    std::cout <<
"Waypoint <type> values recognised by --group-by-type.\n"
"Anything else gets a generic marker and is listed in the report.\n\n";
    struct Row { const char *group; const char *types; };
    const Row rows[] = {
        {"water",    "fountain fontana water acqua tap spring sorgente well pozzo "
                     "waterfall cascata"},
        {"mountain", "peak summit cima vetta monte pass valico hut refuge rifugio "
                     "bivacco shelter rescue soccorso cave grotta climb arrampicata"},
        {"the walk", "trek hiking sentiero bike bici ski sci start partenza finish "
                     "arrivo junction bivio crossing guidepost segnavia cartello"},
        {"services", "parking parcheggio fuel benzina toilet bagno food restaurant "
                     "ristorante bar cafe shop negozio hotel albergo camp campeggio "
                     "picnic rest sosta"},
        {"sights",   "viewpoint panorama vista church chiesa chapel cappella castle "
                     "castello monument monumento museum museo ruins rovine photo "
                     "foto info"},
        {"terrain",  "bridge ponte tunnel galleria beach spiaggia wood bosco park "
                     "parco danger pericolo warning hospital ospedale home casa"},
    };
    for (const Row &r : rows) {
        std::cout << "  " << r.group << "\n      " << r.types << "\n\n";
    }
    std::cout <<
"Matching ignores case and accepts a longer name containing the word, so\n"
"\"fontana vecchia\" still finds the drinking water icon.\n";
}

void report(const RunStats &st, const Options &opt, bool quiet) {
    if (quiet) return;

    for (const FileResult &f : st.files) {
        std::cout << "  " << slope::baseName(f.input);
        if (f.skipped) {
            std::cout << "   skipped: " << f.skipReason << "\n";
            continue;
        }
        if (!f.color.empty()) std::cout << "   " << f.color;
        std::cout << "   " << f.tracks << " trk";
        if (f.waypoints) std::cout << "  " << f.waypoints << " wpt";
        std::cout << "\n";
        for (const std::string &w : f.warnings)
            std::cout << "      note: " << w << "\n";
    }

    if (!st.groups.empty()) {
        std::cout << "\n  waypoint groups: " << st.groups.size() << "\n";
        for (const Group &g : st.groups) {
            std::cout << "      " << g.type << " (" << g.count << ")  ->  "
                      << g.icon;
            if (g.iconGuessed) std::cout << "   [no match, generic marker]";
            std::cout << "\n";
        }
    }

    std::cout << "\n  ";
    if (opt.merge) {
        const MergeResult &m = st.merged;
        if (opt.dryRun) {
            std::cout << "would merge " << m.sources << " file(s) into "
                      << slope::baseName(m.output) << ": " << m.tracks
                      << " track(s)";
            if (m.waypoints) std::cout << ", " << m.waypoints << " waypoint(s)";
        } else {
            std::cout << "merged " << m.sources << " file(s) into "
                      << m.output << "\n  " << m.tracks << " track(s)";
            if (m.waypoints) std::cout << ", " << m.waypoints << " waypoint(s)";
            std::cout << " - import this one file into OsmAnd";
        }
    } else if (opt.dryRun) {
        std::cout << st.files.size() - st.skipped - st.failed
                  << " file(s) would be written";
    } else {
        std::cout << st.written << " written";
    }
    if (st.skipped) std::cout << ", " << st.skipped << " skipped";
    if (st.failed)  std::cout << ", " << st.failed  << " failed";
    std::cout << ".\n";
}

}  // namespace

// Entry point for `--style`. argv[0] is the program, argv[1] is "--style".
int styleMain(int argc, char **argv) {
    Options opt;
    std::vector<std::string> paths;
    bool quiet = false;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];

        if (a == "-h" || a == "--help") { printStyleHelp(argv[0]); return 0; }
        if (a == "--icons")             { printIcons();            return 0; }

        if (a == "--color") {
            if (!needValue(i, argc, "--color")) return 2;
            std::string out, err;
            if (!slope::resolveColor(argv[++i], out, err)) {
                std::cerr << "Error: " << err << "\n";
                return 2;
            }
            opt.color = out;
        } else if (a == "--auto-color") {
            opt.autoColor = true;
        } else if (a.rfind("--auto-color=", 0) == 0) {
            opt.autoColor = true;
            const std::string m = a.substr(13);
            if (m == "stable")          opt.autoMode = AutoMode::Stable;
            else if (m == "sequential") opt.autoMode = AutoMode::Sequential;
            else {
                std::cerr << "Error: --auto-color takes stable or sequential "
                             "(got '" << m << "').\n";
                return 2;
            }
        } else if (a == "--palette") {
            if (!needValue(i, argc, "--palette")) return 2;
            std::string err;
            if (!parsePalette(argv[++i], opt.palette, err)) {
                std::cerr << "Error: " << err << "\n";
                return 2;
            }
        } else if (a == "--per-track-color") {
            opt.perTrackColor = true;
        } else if (a == "--width") {
            if (!needValue(i, argc, "--width")) return 2;
            opt.width = argv[++i];
        } else if (a == "--arrows") {
            if (!needValue(i, argc, "--arrows")) return 2;
            if (!parseOnOff(argv[++i], opt.arrows, "--arrows")) return 2;
        } else if (a == "--start-finish") {
            if (!needValue(i, argc, "--start-finish")) return 2;
            if (!parseOnOff(argv[++i], opt.startFinish, "--start-finish")) return 2;
        } else if (a == "--split") {
            if (!needValue(i, argc, "--split")) return 2;
            opt.splitType = argv[++i];
        } else if (a == "--split-interval") {
            if (!needValue(i, argc, "--split-interval")) return 2;
            opt.splitInterval = argv[++i];
        } else if (a == "--coloring" || a == "--colouring") {
            if (!needValue(i, argc, a.c_str())) return 2;
            opt.coloring = argv[++i];
        } else if (a == "--activity") {
            if (!needValue(i, argc, "--activity")) return 2;
            opt.activity = argv[++i];
        } else if (a == "--desc") {
            if (!needValue(i, argc, "--desc")) return 2;
            opt.desc = argv[++i];
        } else if (a == "--link") {
            if (!needValue(i, argc, "--link")) return 2;
            opt.link = argv[++i];
        } else if (a == "--group-by-type") {
            opt.groupByType = true;
        } else if (a == "--wpt-icon") {
            if (!needValue(i, argc, "--wpt-icon")) return 2;
            opt.wptIcon = argv[++i];
        } else if (a == "--wpt-color") {
            if (!needValue(i, argc, "--wpt-color")) return 2;
            std::string out, err;
            if (!slope::resolveColor(argv[++i], out, err)) {
                std::cerr << "Error: " << err << "\n";
                return 2;
            }
            opt.wptColor = out;
        } else if (a == "--wpt-background") {
            if (!needValue(i, argc, "--wpt-background")) return 2;
            opt.wptBackground = argv[++i];
        } else if (a == "--keep-existing") {
            opt.keepExisting = true;
        } else if (a == "--strip-existing") {
            opt.stripExisting = true;
        } else if (a == "-r" || a == "--recursive") {
            opt.recursive = true;
        } else if (a == "--out-dir") {
            if (!needValue(i, argc, "--out-dir")) return 2;
            opt.outDir = argv[++i];
        } else if (a == "--suffix") {
            if (!needValue(i, argc, "--suffix")) return 2;
            opt.suffix = argv[++i];
        } else if (a == "--3d") {
            if (!needValue(i, argc, "--3d")) return 2;
            opt.viz3d = argv[++i];
        } else if (a == "--3d-wall") {
            if (!needValue(i, argc, "--3d-wall")) return 2;
            opt.wall3d = argv[++i];
        } else if (a == "--3d-position") {
            if (!needValue(i, argc, "--3d-position")) return 2;
            opt.wallPos3d = argv[++i];
        } else if (a == "--3d-scale") {
            if (!needValue(i, argc, "--3d-scale")) return 2;
            opt.scale3d = argv[++i];
        } else if (a == "--3d-height") {
            if (!needValue(i, argc, "--3d-height")) return 2;
            opt.height3d = argv[++i];
        } else if (a == "--separate-files" || a == "--no-merge") {
            opt.merge = false;
        } else if (a == "--merge") {
            opt.merge = true;          // already the default; accepted so a
                                       // script can be explicit about it
        } else if (a == "--merge-name") {
            if (!needValue(i, argc, "--merge-name")) return 2;
            opt.mergeName = argv[++i];
        } else if (a == "--in-place") {
            opt.inPlace = true;
        } else if (a == "--backup") {
            opt.backup = true;
        } else if (a == "-n" || a == "--dry-run") {
            opt.dryRun = true;
        } else if (a == "--quiet") {
            quiet = true;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Error: unknown option '" << a << "'.\n"
                      << "Try " << argv[0] << " --style --help\n";
            return 2;
        } else {
            paths.push_back(a);
        }
    }

    if (paths.empty()) {
        printStyleHelp(argv[0]);
        return 2;
    }

    // Nothing to do is a mistake worth catching: it would copy every file
    // unchanged and look like it had worked.
    if (!opt.viz3d.empty()) { /* the 3D wall alone is a real change */ }
    else if (opt.color.empty() && !opt.autoColor && opt.width.empty() &&
        opt.arrows == Tri::Unset && opt.startFinish == Tri::Unset &&
        opt.splitType.empty() && opt.coloring.empty() && opt.activity.empty() &&
        !opt.groupByType && opt.wptIcon.empty() && opt.wptColor.empty() &&
        opt.wptBackground.empty() && !opt.stripExisting) {
        std::cerr << "Error: nothing to apply. Give at least one setting, "
                     "for example --auto-color or --width 12.\n"
                  << "Try " << argv[0] << " --style --help\n";
        return 2;
    }

    // --in-place rewrites the originals; merging writes one new file. Asking
    // for both is a contradiction, so say so rather than silently picking one.
    if (opt.merge && opt.inPlace) opt.merge = false;

    std::string err;
    if (!validate(opt, err)) {
        std::cerr << "Error: " << err << "\n";
        return 2;
    }

    std::vector<std::string> inputs;
    size_t alreadyStyled = 0;
    for (const std::string &p : paths) {
        // Skip what a previous run already produced, unless the output goes
        // somewhere else (then there is nothing to re-read) or we overwrite.
        const std::string skip =
            (opt.inPlace || !opt.outDir.empty()) ? std::string() : opt.suffix;
        if (!collectInputs(p, opt.recursive, inputs, err, skip, &alreadyStyled)) {
            std::cerr << "Error: " << err << "\n";
            return 1;
        }
    }
    if (inputs.empty()) {
        if (alreadyStyled > 0) {
            std::cerr << "Error: no .gpx files left to style: all "
                      << alreadyStyled << " file(s) already end with \""
                      << opt.suffix << "\", so they look like the output of an "
                      << "earlier run.\n"
                      << "Use --out-dir to write elsewhere, --in-place to "
                      << "restyle them, or --suffix to pick another name.\n";
        } else {
            std::cerr << "Error: no .gpx files found.\n";
        }
        return 1;
    }

    if (!quiet) {
        std::cout << "\n  " << inputs.size() << " file(s)";
        if (alreadyStyled > 0)
            std::cout << "   [" << alreadyStyled << " skipped: already styled]";
        if (opt.dryRun) std::cout << "   [dry run, nothing will be written]";
        std::cout << "\n\n";
    }

    RunStats st;
    if (!run(inputs, opt, st, err)) {
        std::cerr << "Error: " << err << "\n";
        return 1;
    }
    report(st, opt, quiet);
    return st.failed ? 1 : 0;
}

}  // namespace styler
