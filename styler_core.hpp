// styler_core.hpp - batch OsmAnd styling for existing GPX files.
//
// Reads GPX files and adds the extensions OsmAnd reads as appearance
// instructions. Track points are never touched.
//
// Placement follows what was measured on OsmAnd Android in September 2026:
//
//   <trk><extensions>   colour and width  - honoured per track
//   <gpx><extensions>   arrows, start/finish, split, point groups
//                       - honoured only here, ignored inside <trk>
//
// Both blocks are written to the same file; they do not conflict.

#ifndef STYLER_CORE_HPP
#define STYLER_CORE_HPP

#include <map>
#include <string>
#include <vector>

namespace styler {

// ------------------------------------------------------------- palettes

enum class Palette { Distinct, Warm, Cool, Colorblind };

bool parsePalette(const std::string &name, Palette &out, std::string &error);

// Colours of the given palette, in order, as "#rrggbb".
const std::vector<std::string> &paletteColors(Palette p);

// How a file's colour is picked when --auto-color is on.
//   Stable      hash of the file name; adding files never reshuffles the rest
//   Sequential  alphabetical order
enum class AutoMode { Stable, Sequential };

// Colour for `name`, index `seq` within a run of `total` files.
std::string autoColorFor(const std::string &name, size_t seq, size_t total,
                         Palette p, AutoMode mode);

// Colours for a whole run at once. Resolves collisions so that every palette
// entry is used before any colour repeats; see the implementation for why
// per-file hashing alone is not enough.
std::vector<std::string> assignColors(const std::vector<std::string> &names,
                                      Palette p, AutoMode mode);

// ------------------------------------------------------------- options

// Tri-state: the user may leave a switch alone rather than force it on/off.
enum class Tri { Unset, On, Off };

struct Options {
    // colour
    std::string color;              // explicit, already resolved to #rrggbb
    bool        autoColor = false;
    Palette     palette   = Palette::Distinct;
    AutoMode    autoMode  = AutoMode::Stable;
    bool        perTrackColor = false;   // vary colour per track, not per file

    // appearance
    std::string width;              // thin|medium|bold|1-24, empty = leave
    Tri  arrows      = Tri::Unset;
    Tri  startFinish = Tri::Unset;
    std::string splitType;          // no_split|distance|time
    std::string splitInterval;
    std::string coloring;           // solid|speed|altitude  (slope is Pro)
    std::string colorPalette;       // user palette name

    // 3D wall under the track. Paid OsmAnd feature, so it is only written
    // when explicitly asked for.
    std::string viz3d;              // none|altitude|fixed_height|...
    std::string wall3d;             // none|solid|upward_gradient|...
    std::string wallPos3d;          // top|bottom|top_bottom
    std::string scale3d;            // vertical exaggeration
    std::string height3d;           // metres, only for fixed_height

    // metadata
    std::string activity;
    std::string desc;
    std::string link;

    // waypoints
    std::string wptIcon;
    std::string wptColor;
    std::string wptBackground;      // circle|square|octagon
    bool groupByType = false;

    // existing osmand extensions
    bool keepExisting  = false;
    bool stripExisting = false;

    // merging: one file to import instead of many
    bool merge = true;              // on by default: the whole point is to
                                    // import once and configure nothing
    std::string mergeName = "all-tracks";

    // output
    std::string outDir;
    std::string suffix = "_osmand";
    bool inPlace   = false;
    bool backup    = false;
    bool dryRun    = false;
    bool recursive = false;
};

// ------------------------------------------------------------- results

// Result of merging every styled file into a single multi-track GPX.
struct MergeResult {
    std::string output;         // path written
    size_t tracks = 0;
    size_t waypoints = 0;
    size_t sources = 0;         // how many input files went in
    bool written = false;
};

struct Group {
    std::string type;       // value of <type> in the source waypoints
    std::string color;
    std::string icon;
    std::string background;
    size_t count = 0;
    bool  iconGuessed = false;   // no entry in the table, generic icon used
};

struct FileResult {
    std::string input;
    std::string output;
    std::string color;          // colour assigned to the file, if any
    size_t tracks    = 0;
    size_t waypoints = 0;
    bool   written   = false;
    bool   skipped   = false;
    std::string skipReason;
    std::vector<std::string> warnings;
    std::vector<Group> groupsFound;
};

struct RunStats {
    std::vector<FileResult> files;
    std::vector<Group> groups;
    size_t written = 0;
    size_t skipped = 0;
    size_t failed  = 0;
    MergeResult merged;         // only filled in when Options::merge is on
};

// ------------------------------------------------------------- work

// Every *.gpx below `path` (a file or a directory), sorted, case-insensitive
// on the extension. Returns false only when the path does not exist.
// When `skipSuffix` is not empty, files whose name already ends with it are
// left out: running twice over the same folder must not restyle the output of
// the first run (and renumber every colour in the process).
bool collectInputs(const std::string &path, bool recursive,
                   std::vector<std::string> &out, std::string &error,
                   const std::string &skipSuffix = std::string(),
                   size_t *skippedCount = nullptr);

// Validate an option set before any file is touched, so a typo fails fast
// instead of halfway through a folder.
bool validate(const Options &opt, std::string &error);

// Style one file. `color` overrides opt.color/auto for this file.
// `groupColors` carries the waypoint-group palette across files. Each file is
// styled on its own, so without it every file restarts at palette slot 0 and
// all groups in a run end up the same colour.
bool styleFile(const std::string &inPath, const std::string &outPath,
               const Options &opt, const std::string &color,
               FileResult &res, std::string &error,
               std::string *capture = nullptr,
               std::map<std::string, std::string> *groupColors = nullptr);

// The whole run: collect, assign colours, style, collect groups.
// Called once per file as the run proceeds, so a caller with a progress bar
// can move it while the work happens rather than after it. `done` counts from
// 1 to `total`. Optional: pass nullptr when there is nothing to report to.
using ProgressFn = void (*)(void *user, size_t done, size_t total);

bool run(const std::vector<std::string> &inputs, const Options &opt,
         RunStats &stats, std::string &error,
         ProgressFn progress = nullptr, void *user = nullptr);

// Icon suggested for a waypoint <type>; empty when unknown.
std::string iconForType(const std::string &type);

// Valid values, for the front ends and for --list-* output.
const std::vector<std::string> &coloringTypes();     // free ones only
const std::vector<std::string> &proColoringTypes();  // need OsmAnd Pro
const std::vector<std::string> &backgrounds();
const std::vector<std::string> &splitTypes();

}  // namespace styler

#endif  // STYLER_CORE_HPP
