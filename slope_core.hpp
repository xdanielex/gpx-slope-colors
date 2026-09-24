// slope_core.hpp - GPX slope colouring engine.
//
// Colours a GPX track by slope following the direction of travel:
// uphill one colour, downhill another. No external dependencies.

#ifndef SLOPE_CORE_HPP
#define SLOPE_CORE_HPP

#include <string>
#include <vector>

namespace slope {

const char *const VERSION = "3.0.0";

// ---------------------------------------------------------------- colours

struct NamedColor {
    const char *name;
    const char *aliases;   // comma separated, may be empty
    const char *hex;
};

// The canonical palette, in display order.
extern const std::vector<NamedColor> &palette();

// Resolve a colour name ("red", "rosso") or hex ("#ff8800", "ff8800").
// Returns false and fills `error` when the value is not understood.
bool resolveColor(const std::string &in, std::string &out, std::string &error);

// ------------------------------------------------------------------ model

struct Point {
    double lat = 0.0;
    double lon = 0.0;
    double ele = 0.0;
    bool hasEle = false;
    std::string time;      // raw ISO string, empty when absent
    std::string extra;     // verbatim child XML we do not understand
};

enum class Klass { Uphill, Downhill, Flat };

struct Section {
    Klass klass;
    size_t first;          // index of first point
    size_t last;           // index of last point (inclusive)
};

// Everything the caller can tune. Mirrors the command line one to one.
// Colour used for a track we could not classify, because the file has no
// elevation. Yellow, and the same yellow the palette calls "yellow": grey was
// tried first and disappeared against OsmAnd's light map background.
extern const char *const kNoDataColor;

struct Options {
    double threshold = 1.5;       // percent; below this a stretch is "flat"
    double window    = 50.0;      // metres, smoothing / gradient window
    double minlen    = 80.0;      // metres, shortest coloured section

    std::string uphill   = "#e01b1b";
    std::string downhill = "#00a03c";
    std::string flat     = "#9b30d9";

    std::string width  = "24";    // thin | medium | bold | 1..24
    std::string suffix = "_slope";

    bool noFlat        = false;
    bool splitFiles    = false;
    bool arrows        = true;
    bool keepWaypoints = false;

    // Distance/time markers along the track, and the 3D wall. Both are
    // file-wide tags in OsmAnd, so they sit next to show_arrows.
    std::string splitType;        // no_split | distance | time  (empty = leave)
    std::string splitInterval;    // metres for distance, seconds for time

    std::string viz3d;            // none | altitude | fixed_height | ...
    std::string wall3d;           // none | solid | upward_gradient | ...
    std::string wallPos3d;        // top | bottom | top_bottom
    std::string scale3d;          // vertical exaggeration, e.g. 2.0
    std::string height3d;         // metres, only for fixed_height

    std::string output;           // explicit output path, empty = auto
    std::string outputDir;        // when set, results go here

    // Several inputs into one file to import. On by default: six stages then
    // cost one import in OsmAnd instead of six, and the slope colours are
    // already per-track so nothing is lost by putting them together.
    bool merge = true;
    std::string mergeName = "all-tracks";
};

// Buffer used when several inputs are merged into a single document: each
// file contributes its <trk> blocks and waypoints instead of writing its own.
struct MergeSink {
    std::string tracks;
    std::string waypoints;
    size_t files     = 0;
    size_t trackCount = 0;
    size_t waypointCount = 0;
};

struct Stats {
    size_t points   = 0;
    size_t sections = 0;
    double totalM   = 0.0;
    double uphillM  = 0.0;
    double downhillM = 0.0;
    double flatM    = 0.0;
    size_t waypoints = 0;
    // True when the file carried no <ele> at all. The track is still written,
    // in a neutral colour, because dropping it would silently lose a track
    // from a merged file - but nothing about it has been classified.
    bool noElevation = false;
    std::vector<std::string> written;   // paths actually created
};

// ------------------------------------------------------------------- work

double haversine(double lat1, double lon1, double lat2, double lon2);

// Parse the <trkpt> list, in file order. Returns false on read/parse failure.
bool readPoints(const std::string &path, std::vector<Point> &out,
                std::string &error);

// Raw <wpt>...</wpt> blocks, copied verbatim when --keep-waypoints is on.
std::vector<std::string> readWaypoints(const std::string &path);

// Fill gaps in elevation by linear interpolation.
// Returns false when the track has no elevation at all.
bool fillMissingElevations(std::vector<Point> &pts);

std::vector<double> cumulativeDistance(const std::vector<Point> &pts);

std::vector<double> smoothElevations(const std::vector<Point> &pts,
                                     const std::vector<double> &dist,
                                     double windowM);

std::vector<Klass> classify(const std::vector<Point> &pts,
                            const std::vector<double> &dist,
                            double windowM, double thresholdPct, bool useFlat);

std::vector<Section> groupSections(const std::vector<Klass> &classes,
                                   const std::vector<double> &dist,
                                   double minlenM);

// Full pipeline: read, classify, write. Returns false and fills `error`
// on any failure. `stats` is filled on success.
// When `sink` is non-null the result is appended to it and nothing is
// written; the caller finishes the document with writeMerged().
bool process(const std::string &inputPath, const Options &opt,
             Stats &stats, std::string &error, MergeSink *sink = nullptr);

// Write the document collected in `sink`. `name` is the file to create.
bool writeMerged(const MergeSink &sink, const std::string &path,
                 const Options &opt, std::string &error);

// Helpers shared with the front ends.
std::string defaultOutputPath(const std::string &inputPath,
                              const std::string &suffix);
std::string baseName(const std::string &path);
std::string dirName(const std::string &path);
std::string stripExtension(const std::string &path);
std::string joinPath(const std::string &dir, const std::string &name);

// Validate a --width value. Returns false with an explanation if bogus.
bool validWidth(const std::string &w, std::string &error);

// The 3D wall and the marker tags accept only the values OsmAnd knows. A typo
// would otherwise be written into the file and silently ignored by the app,
// which looks exactly like the feature not working.
bool valid3d(const Options &opt, std::string &error);
bool validMarkers(const Options &opt, std::string &error);

// OsmAnd types split_interval as a Double and its own exports write it with a
// decimal point ("2000.0"). Writing a bare "1000" is what a human would type,
// but it is not what the app produces, so the value is normalised here.
std::string asDouble(const std::string &n);

}  // namespace slope

#endif  // SLOPE_CORE_HPP
