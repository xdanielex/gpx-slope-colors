// slope_core.hpp - GPX slope colouring engine.
//
// Colours a GPX track by slope following the direction of travel:
// uphill one colour, downhill another. No external dependencies.

#ifndef SLOPE_CORE_HPP
#define SLOPE_CORE_HPP

#include <string>
#include <vector>

namespace slope {

const char *const VERSION = "2.1.1";

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

    std::string output;           // explicit output path, empty = auto
    std::string outputDir;        // when set, results go here
};

struct Stats {
    size_t points   = 0;
    size_t sections = 0;
    double totalM   = 0.0;
    double uphillM  = 0.0;
    double downhillM = 0.0;
    double flatM    = 0.0;
    size_t waypoints = 0;
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
bool process(const std::string &inputPath, const Options &opt,
             Stats &stats, std::string &error);

// Helpers shared with the front ends.
std::string defaultOutputPath(const std::string &inputPath,
                              const std::string &suffix);
std::string baseName(const std::string &path);
std::string dirName(const std::string &path);
std::string stripExtension(const std::string &path);
std::string joinPath(const std::string &dir, const std::string &name);

// Validate a --width value. Returns false with an explanation if bogus.
bool validWidth(const std::string &w, std::string &error);

}  // namespace slope

#endif  // SLOPE_CORE_HPP
