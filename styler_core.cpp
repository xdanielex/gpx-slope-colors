// styler_core.cpp - batch OsmAnd styling engine.

#include "styler_core.hpp"
#include "slope_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

namespace styler {
namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool endsWithGpx(const std::string &p) {
    if (p.size() < 4) return false;
    return lowerCopy(p.substr(p.size() - 4)) == ".gpx";
}

#ifdef _WIN32
std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string narrow(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n,
                        nullptr, nullptr);
    return s;
}
#endif

bool isDirectory(const std::string &path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

bool pathExists(const std::string &path) {
#ifdef _WIN32
    return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

// One directory level. Recursion is handled by the caller so that symlink
// loops cannot run away: we simply never follow a link to a directory.
void listDir(const std::string &dir, std::vector<std::string> &files,
             std::vector<std::string> &subdirs) {
#ifdef _WIN32
    std::wstring pattern = widen(dir) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring wn = fd.cFileName;
        if (wn == L"." || wn == L"..") continue;
        std::string name = narrow(wn);
        std::string full = dir + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) subdirs.push_back(full);
        else if (endsWithGpx(name)) files.push_back(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (lstat(full.c_str(), &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) subdirs.push_back(full);
        else if (endsWithGpx(name)) files.push_back(full);
    }
    closedir(d);
#endif
}

std::string readWhole(const std::string &path, bool &ok) {
#ifdef _WIN32
    std::ifstream f(widen(path).c_str(), std::ios::binary);
#else
    std::ifstream f(path.c_str(), std::ios::binary);
#endif
    if (!f) { ok = false; return std::string(); }
    std::ostringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

bool writeWhole(const std::string &path, const std::string &data) {
#ifdef _WIN32
    std::ofstream f(widen(path).c_str(), std::ios::binary);
#else
    std::ofstream f(path.c_str(), std::ios::binary);
#endif
    if (!f) return false;
    f << data;
    return f.good();
}

// Input files come from everywhere: a BOM from Windows editors, CRLF from
// almost anything, the odd Latin-1 file from an old device. Strip the BOM and
// normalise line endings so the rest of the code sees one shape of text.
std::string normaliseText(std::string s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') continue;
            out += '\n';
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string xmlEscape(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;";  break;
            case '<': o += "&lt;";   break;
            case '>': o += "&gt;";   break;
            case '"': o += "&quot;"; break;
            default:  o += c;
        }
    }
    return o;
}

// Position of the matching close tag for `name` opened at `from`, counting
// nesting so an <extensions> inside an <extensions> does not end it early.
size_t matchingClose(const std::string &s, const std::string &name,
                     size_t openPos) {
    const std::string open  = "<" + name;
    const std::string close = "</" + name + ">";
    int depth = 0;
    size_t i = openPos;
    while (i < s.size()) {
        size_t o = s.find(open, i);
        size_t c = s.find(close, i);
        if (c == std::string::npos) return std::string::npos;
        if (o != std::string::npos && o < c) {
            // Ignore <extensionsfoo>; require a delimiter after the name.
            char after = (o + open.size() < s.size()) ? s[o + open.size()] : ' ';
            if (after == '>' || after == ' ' || after == '\n' ||
                after == '\t' || after == '/') {
                ++depth;
            }
            i = o + open.size();
        } else {
            --depth;
            if (depth <= 0) return c;
            i = c + close.size();
        }
    }
    return std::string::npos;
}

// Very small sanity check. A .gpx that is actually an HTML error page is a
// real thing that happens when a download goes wrong, and it must be skipped
// with a clear message rather than corrupt the run.
bool looksLikeGpx(const std::string &s, std::string &why) {
    size_t p = s.find("<gpx");
    if (p == std::string::npos) {
        if (s.find("<html") != std::string::npos ||
            s.find("<!DOCTYPE html") != std::string::npos) {
            why = "looks like an HTML page, not a GPX file";
        } else {
            why = "no <gpx> element found";
        }
        return false;
    }
    if (s.find("<trk") == std::string::npos &&
        s.find("<wpt") == std::string::npos &&
        s.find("<rte") == std::string::npos) {
        why = "no tracks, routes or waypoints";
        return false;
    }
    return true;
}

const char *const kOsmandNs =
    "https://osmand.net/docs/technical/osmand-file-formats/osmand-gpx";

// Waypoint <type> to OsmAnd icon.
//
// Every name on the right was checked against OsmAnd-resources/poi/
// poi_categories.json, the list the app itself draws from. That check matters:
// the first version of this table was written from intuition and six of its
// nineteen names did not exist - "natural_peak", "tourism_alpine_hut",
// "amenity_restaurant" and friends all look plausible and all silently produce
// the default marker, which is exactly what a device test showed.
//
// Biased towards what turns up on hiking and cycling tracks, in English and
// Italian. Anything missing falls back to special_marker and is listed in the
// report, so the user can supply their own mapping with --icon-map.
struct IconRow { const char *type; const char *icon; };

const IconRow kIcons[] = {
    // water
    {"fountain",    "amenity_drinking_water"},
    {"fontana",     "amenity_drinking_water"},
    {"water",       "amenity_drinking_water"},
    {"acqua",       "amenity_drinking_water"},
    {"tap",         "water_tap"},
    {"spring",      "natural_spring"},
    {"sorgente",    "natural_spring"},
    {"well",        "man_made_water_well"},
    {"pozzo",       "man_made_water_well"},
    {"waterfall",   "waterfall"},
    {"cascata",     "waterfall"},
    // mountain
    {"peak",        "mountain"},
    {"summit",      "mountain"},
    {"cima",        "mountain"},
    {"vetta",       "mountain"},
    {"monte",       "mountain"},
    {"pass",        "mountain"},
    {"valico",      "mountain"},
    {"hut",         "tourism_hostel"},
    {"refuge",      "tourism_hostel"},
    {"rifugio",     "tourism_hostel"},
    {"bivacco",     "tourism_hostel"},
    {"shelter",     "tourism_hostel"},
    {"rescue",      "mountain_rescue"},
    {"soccorso",    "mountain_rescue"},
    {"cave",        "natural_cave_entrance"},
    {"grotta",      "natural_cave_entrance"},
    {"climb",       "sport_climbing"},
    {"arrampicata", "sport_climbing"},
    // the walk itself
    {"trek",        "special_trekking"},
    {"hiking",      "special_trekking"},
    {"sentiero",    "special_trekking"},
    {"bike",        "special_bicycle"},
    {"bici",        "special_bicycle"},
    {"ski",         "special_skiing"},
    {"sci",         "special_skiing"},
    {"start",       "special_point_start"},
    {"partenza",    "special_point_start"},
    {"finish",      "special_point_finish"},
    {"arrivo",      "special_point_finish"},
    {"junction",    "special_symbol_plus"},
    {"bivio",       "special_symbol_plus"},
    {"crossing",    "special_symbol_plus"},
    {"guidepost",   "information_guidepost"},
    {"segnavia",    "information_guidepost"},
    {"cartello",    "information_guidepost"},
    // services
    {"parking",     "amenity_parking"},
    {"parcheggio",  "amenity_parking"},
    {"fuel",        "fuel"},
    {"benzina",     "fuel"},
    {"toilet",      "amenity_toilets"},
    {"bagno",       "amenity_toilets"},
    {"food",        "restaurants"},
    {"restaurant",  "restaurants"},
    {"ristorante",  "restaurants"},
    {"bar",         "amenity_bar"},
    {"cafe",        "amenity_cafe"},
    {"shop",        "shop_supermarket"},
    {"negozio",     "shop_supermarket"},
    {"hotel",       "tourism_hotel"},
    {"albergo",     "tourism_hotel"},
    {"camp",        "tourism_camp_site"},
    {"campeggio",   "tourism_camp_site"},
    {"picnic",      "tourism_picnic_site"},
    {"rest",        "rest_area"},
    {"sosta",       "rest_area"},
    // what you look at
    {"viewpoint",   "tourism_viewpoint"},
    {"panorama",    "tourism_viewpoint"},
    {"vista",       "tourism_viewpoint"},
    {"church",      "building_type_church"},
    {"chiesa",      "building_type_church"},
    {"chapel",      "building_type_chapel"},
    {"cappella",    "building_type_chapel"},
    {"castle",      "historic_castle"},
    {"castello",    "historic_castle"},
    {"monument",    "monument"},
    {"monumento",   "monument"},
    {"museum",      "tourism_museum"},
    {"museo",       "tourism_museum"},
    {"ruins",       "historic_archaeological_site"},
    {"rovine",      "historic_archaeological_site"},
    {"photo",       "special_photo_camera"},
    {"foto",        "special_photo_camera"},
    {"info",        "tourism_information"},
    // terrain and warnings
    {"bridge",      "bridge_structure_arch"},
    {"ponte",       "bridge_structure_arch"},
    {"tunnel",      "tunnel"},
    {"galleria",    "tunnel"},
    {"beach",       "beach"},
    {"spiaggia",    "beach"},
    {"wood",        "wood"},
    {"bosco",       "wood"},
    {"park",        "park"},
    {"parco",       "park"},
    {"danger",      "special_symbol_exclamation_mark"},
    {"pericolo",    "special_symbol_exclamation_mark"},
    {"warning",     "special_symbol_exclamation_mark"},
    {"hospital",    "amenity_hospital"},
    {"ospedale",    "amenity_hospital"},
    {"home",        "special_house"},
    {"casa",        "special_house"},
};

}  // namespace

// ---------------------------------------------------------------- tables

std::string iconForType(const std::string &type) {
    const std::string t = lowerCopy(type);
    for (const IconRow &r : kIcons) {
        if (t == r.type) return r.icon;
    }
    // Loose match: "fontana vecchia" should still find the fountain icon.
    for (const IconRow &r : kIcons) {
        if (t.find(r.type) != std::string::npos) return r.icon;
    }
    return std::string();
}

const std::vector<std::string> &coloringTypes() {
    static const std::vector<std::string> v = {"solid", "speed", "altitude"};
    return v;
}

const std::vector<std::string> &proColoringTypes() {
    static const std::vector<std::string> v = {
        "slope", "routeInfo_roadClass", "routeInfo_surface",
        "routeInfo_smoothness"};
    return v;
}

const std::vector<std::string> &backgrounds() {
    static const std::vector<std::string> v = {"circle", "square", "octagon"};
    return v;
}

const std::vector<std::string> &splitTypes() {
    static const std::vector<std::string> v = {"no_split", "distance", "time"};
    return v;
}

// ---------------------------------------------------------------- inputs

bool collectInputs(const std::string &path, bool recursive,
                   std::vector<std::string> &out, std::string &error,
                   const std::string &skipSuffix, size_t *skippedCount) {
    if (!pathExists(path)) {
        error = "No such file or folder: " + path;
        return false;
    }
    if (!isDirectory(path)) {
        // An explicitly named file is always honoured, suffix or not.
        out.push_back(path);
        return true;
    }
    std::vector<std::string> pending;
    pending.push_back(path);
    std::set<std::string> seen;
    while (!pending.empty()) {
        std::string dir = pending.back();
        pending.pop_back();
        if (!seen.insert(dir).second) continue;
        std::vector<std::string> files, subs;
        listDir(dir, files, subs);
        for (const std::string &f : files) {
            if (!skipSuffix.empty()) {
                const std::string stem = slope::stripExtension(slope::baseName(f));
                if (stem.size() > skipSuffix.size() &&
                    stem.compare(stem.size() - skipSuffix.size(),
                                 skipSuffix.size(), skipSuffix) == 0) {
                    if (skippedCount) ++*skippedCount;
                    continue;
                }
            }
            out.push_back(f);
        }
        if (recursive) {
            for (const std::string &s : subs) pending.push_back(s);
        }
    }
    std::sort(out.begin(), out.end());
    return true;
}

// -------------------------------------------------------------- validate

bool validate(const Options &opt, std::string &error) {
    if (!opt.width.empty() && !slope::validWidth(opt.width, error)) return false;

    // The 3D tags take the same values in both modes, so the checks live in
    // slope_core and are reused here rather than written twice.
    {
        slope::Options s;
        s.viz3d = opt.viz3d; s.wall3d = opt.wall3d;
        s.wallPos3d = opt.wallPos3d; s.scale3d = opt.scale3d;
        s.height3d = opt.height3d;
        if (!slope::valid3d(s, error)) return false;
    }

    if (!opt.coloring.empty()) {
        const std::string c = opt.coloring;
        bool free_ok = false;
        for (const std::string &v : coloringTypes()) if (v == c) free_ok = true;
        if (!free_ok) {
            for (const std::string &v : proColoringTypes()) {
                if (lowerCopy(v) == lowerCopy(c)) {
                    error = "'" + c + "' needs an OsmAnd Pro subscription and "
                            "is ignored without one. Free values: solid, "
                            "speed, altitude.";
                    return false;
                }
            }
            error = "Unknown --coloring '" + c +
                    "'. Use solid, speed or altitude.";
            return false;
        }
    }

    if (!opt.splitType.empty()) {
        bool ok = false;
        for (const std::string &v : splitTypes()) if (v == opt.splitType) ok = true;
        if (!ok) {
            error = "Unknown --split '" + opt.splitType +
                    "'. Use no_split, distance or time.";
            return false;
        }
        if (opt.splitType != "no_split" && opt.splitInterval.empty()) {
            error = "--split " + opt.splitType + " also needs --split-interval.";
            return false;
        }
    }

    if (!opt.wptBackground.empty()) {
        bool ok = false;
        for (const std::string &v : backgrounds())
            if (v == opt.wptBackground) ok = true;
        if (!ok) {
            error = "Unknown --wpt-background '" + opt.wptBackground +
                    "'. Use circle, square or octagon.";
            return false;
        }
    }

    if (opt.keepExisting && opt.stripExisting) {
        error = "--keep-existing and --strip-existing contradict each other.";
        return false;
    }
    if (opt.inPlace && !opt.outDir.empty()) {
        error = "--in-place and --out-dir contradict each other.";
        return false;
    }
    if (opt.backup && !opt.inPlace) {
        error = "--backup only makes sense together with --in-place.";
        return false;
    }
    if (opt.autoColor && !opt.color.empty()) {
        error = "--color and --auto-color contradict each other.";
        return false;
    }
    return true;
}


// ------------------------------------------------------------ write bits

namespace {

// Per-track block: colour and width only. Both are honoured by OsmAnd inside
// <trk>, which is what makes a multi-track file useful - ten stages of a walk
// can each keep their own colour.
std::string trackExtensions(const std::string &color,
                            const std::string &width,
                            const std::string &indent) {
    if (color.empty() && width.empty()) return std::string();
    std::string o;
    o += indent + "<extensions>\n";
    if (!color.empty())
        o += indent + "  <osmand:color>" + color + "</osmand:color>\n";
    if (!width.empty())
        o += indent + "  <osmand:width>" + width + "</osmand:width>\n";
    o += indent + "</extensions>\n";
    return o;
}

// File-level block. Everything here is ignored by OsmAnd if written inside
// <trk> instead - measured on the device, not guessed.
std::string fileExtensions(const Options &opt,
                           const std::vector<Group> &groups) {
    std::string o;
    std::string inner;

    if (opt.arrows != Tri::Unset)
        inner += std::string("    <osmand:show_arrows>") +
                 (opt.arrows == Tri::On ? "true" : "false") +
                 "</osmand:show_arrows>\n";
    if (opt.startFinish != Tri::Unset)
        inner += std::string("    <osmand:show_start_finish>") +
                 (opt.startFinish == Tri::On ? "true" : "false") +
                 "</osmand:show_start_finish>\n";
    if (!opt.splitType.empty())
        inner += "    <osmand:split_type>" + opt.splitType +
                 "</osmand:split_type>\n";
    if (!opt.splitInterval.empty())
        inner += "    <osmand:split_interval>" +
                 slope::asDouble(opt.splitInterval) +
                 "</osmand:split_interval>\n";
    if (!opt.coloring.empty())
        inner += "    <osmand:coloring_type>" + opt.coloring +
                 "</osmand:coloring_type>\n";
    if (!opt.colorPalette.empty())
        inner += "    <osmand:color_palette>" + xmlEscape(opt.colorPalette) +
                 "</osmand:color_palette>\n";
    // See slope_core: empty = leave alone, "none" = actively switch off.
    if (opt.viz3d == "none") {
        inner += "    <osmand:line_3d_visualization_by_type>none"
                 "</osmand:line_3d_visualization_by_type>\n";
    } else if (!opt.viz3d.empty()) {
        inner += "    <osmand:line_3d_visualization_by_type>" + opt.viz3d +
                 "</osmand:line_3d_visualization_by_type>\n";
        // Default "solid": the wall takes each track's own colour, so a file
        // where every stage has its own colour keeps them in 3D too.
        inner += "    <osmand:line_3d_visualization_wall_color_type>" +
                 (opt.wall3d.empty() ? std::string("solid") : opt.wall3d) +
                 "</osmand:line_3d_visualization_wall_color_type>\n";
        inner += "    <osmand:line_3d_visualization_position_type>" +
                 (opt.wallPos3d.empty() ? std::string("bottom") : opt.wallPos3d) +
                 "</osmand:line_3d_visualization_position_type>\n";
        if (!opt.scale3d.empty())
            inner += "    <osmand:vertical_exaggeration_scale>" + opt.scale3d +
                     "</osmand:vertical_exaggeration_scale>\n";
        if (opt.viz3d == "fixed_height" && !opt.height3d.empty())
            inner += "    <osmand:elevation_meters>" + opt.height3d +
                     "</osmand:elevation_meters>\n";
    }

    if (!groups.empty()) {
        inner += "    <osmand:points_groups>\n";
        for (const Group &g : groups) {
            inner += "      <group name=\"" + xmlEscape(g.type) +
                     "\" color=\"" + g.color + "\"";
            if (!g.icon.empty()) inner += " icon=\"" + g.icon + "\"";
            if (!g.background.empty())
                inner += " background=\"" + g.background + "\"";
            inner += "/>\n";
        }
        inner += "    </osmand:points_groups>\n";
    }

    if (inner.empty()) return std::string();
    o += "  <extensions>\n" + inner + "  </extensions>\n";
    return o;
}

// Make sure xmlns:osmand is declared on the root, adding it when the source
// file never had it. Without the declaration the prefixed tags are not
// well-formed and the whole file fails to parse.
bool ensureNamespace(std::string &xml, std::string &error) {
    size_t g = xml.find("<gpx");
    if (g == std::string::npos) { error = "no <gpx> element"; return false; }
    size_t close = xml.find('>', g);
    if (close == std::string::npos) { error = "malformed <gpx> tag"; return false; }
    std::string tag = xml.substr(g, close - g);
    if (tag.find("xmlns:osmand") != std::string::npos) return true;
    std::string decl = std::string("\n     xmlns:osmand=\"") + kOsmandNs + "\"";
    bool selfClosing = close > g && xml[close - 1] == '/';
    size_t at = selfClosing ? close - 1 : close;
    xml.insert(at, decl);
    return true;
}

// Remove osmand-prefixed children from an <extensions> block, leaving Garmin,
// Wikiloc and Strava tags alone. Those belong to other tools and throwing
// them away would quietly damage the file.
std::string dropOsmandChildren(const std::string &block) {
    std::string out;
    size_t i = 0;
    while (i < block.size()) {
        size_t lt = block.find("<osmand:", i);
        if (lt == std::string::npos) { out += block.substr(i); break; }
        size_t nameEnd = block.find_first_of(" \t\n/>", lt + 8);
        if (nameEnd == std::string::npos) { out += block.substr(i); break; }
        std::string name = block.substr(lt + 1, nameEnd - lt - 1);
        size_t endTag = block.find("</" + name + ">", lt);
        size_t selfEnd = block.find("/>", lt);
        size_t gt = block.find('>', lt);
        size_t stop;
        if (selfEnd != std::string::npos && gt != std::string::npos &&
            selfEnd + 1 == gt) {
            stop = gt + 1;
        } else if (endTag != std::string::npos) {
            stop = endTag + name.size() + 3;
        } else {
            out += block.substr(i);
            break;
        }
        // Drop the whitespace on the line that held the tag.
        size_t lineStart = block.rfind('\n', lt);
        size_t copyTo = (lineStart != std::string::npos && lineStart >= i)
                            ? lineStart : lt;
        out += block.substr(i, copyTo - i);
        i = stop;
        while (i < block.size() && (block[i] == ' ' || block[i] == '\t')) ++i;
        if (i < block.size() && block[i] == '\n' && copyTo == lineStart) ++i;
    }
    return out;
}

bool blockIsEmpty(const std::string &block) {
    for (char c : block) {
        if (!std::isspace((unsigned char)c)) return false;
    }
    return true;
}

}  // namespace

// ------------------------------------------------------------- one file

// When `capture` is non-null the styled XML is handed back instead of being
// written: merging needs the result in memory, not on disk.
bool styleFile(const std::string &inPath, const std::string &outPath,
               const Options &opt, const std::string &color,
               FileResult &res, std::string &error, std::string *capture,
               std::map<std::string, std::string> *groupColors) {
    res.input  = inPath;
    res.output = outPath;
    res.color  = color;

    bool ok = false;
    std::string raw = readWhole(inPath, ok);
    if (!ok) { error = "Cannot read: " + inPath; return false; }

    std::string xml = normaliseText(raw);

    std::string why;
    if (!looksLikeGpx(xml, why)) {
        res.skipped = true;
        res.skipReason = why;
        return true;
    }

    if (!ensureNamespace(xml, error)) {
        res.skipped = true;
        res.skipReason = error;
        error.clear();
        return true;
    }

    // ---- collect waypoint types for --group-by-type -----------------
    std::vector<Group> groups;
    std::map<std::string, size_t> typeCount;
    std::vector<std::string> typeOrder;
    size_t wptCount = 0;
    {
        size_t i = 0;
        while ((i = xml.find("<wpt", i)) != std::string::npos) {
            size_t end = matchingClose(xml, "wpt", i);
            if (end == std::string::npos) {
                size_t sc = xml.find("/>", i);
                if (sc == std::string::npos) break;
                ++wptCount;
                i = sc + 2;
                continue;
            }
            ++wptCount;
            std::string body = xml.substr(i, end - i);
            size_t t = body.find("<type>");
            if (t != std::string::npos) {
                size_t te = body.find("</type>", t);
                if (te != std::string::npos) {
                    std::string ty = body.substr(t + 6, te - t - 6);
                    if (!ty.empty()) {
                        if (typeCount.find(ty) == typeCount.end())
                            typeOrder.push_back(ty);
                        typeCount[ty]++;
                    }
                }
            }
            i = end + 6;
        }
    }
    res.waypoints = wptCount;

    if (opt.groupByType && !typeOrder.empty()) {
        const std::vector<std::string> &cols = paletteColors(opt.palette);
        size_t n = 0;
        for (const std::string &ty : typeOrder) {
            Group g;
            g.type  = ty;
            g.count = typeCount[ty];
            // A type keeps the same colour everywhere in the run: the index
            // comes from the shared table, not from this file's position.
            size_t slot = n;
            if (groupColors) {
                auto it = groupColors->find(ty);
                if (it != groupColors->end()) {
                    g.color = it->second;
                    slot = std::string::npos;
                } else {
                    slot = groupColors->size();
                }
            }
            if (slot != std::string::npos)
                g.color = cols.empty() ? "#e6194b" : cols[slot % cols.size()];
            if (groupColors) (*groupColors)[ty] = g.color;
            g.icon  = iconForType(ty);
            if (g.icon.empty()) {
                g.icon = "special_marker";
                g.iconGuessed = true;
            }
            g.background = opt.wptBackground.empty() ? "circle"
                                                     : opt.wptBackground;
            groups.push_back(g);
            ++n;
        }
    }

    // ---- per-waypoint icons -----------------------------------------
    //
    // <osmand:points_groups> declares how a GROUP should look, but OsmAnd
    // still wants the icon on the waypoint itself: without it the point falls
    // back to the default marker, which is what a device test showed - names
    // appeared, icons did not. So the icon, colour and background go into each
    // <wpt><extensions> as well, and the groups block stays for the list UI.
    //
    // GPX 1.1 puts <extensions> last inside <wpt>, after <type>, so the block
    // is inserted immediately before </wpt>.
    if (opt.groupByType || !opt.wptIcon.empty() || !opt.wptColor.empty() ||
        !opt.wptBackground.empty()) {
        std::vector<size_t> wptPos;
        size_t i = 0;
        while ((i = xml.find("<wpt", i)) != std::string::npos) {
            char after = (i + 4 < xml.size()) ? xml[i + 4] : ' ';
            if (after == '>' || after == ' ' || after == '\n' || after == '\t')
                wptPos.push_back(i);
            i += 4;
        }
        // Backwards, so earlier offsets stay valid as text is inserted.
        for (size_t k = wptPos.size(); k-- > 0;) {
            size_t pos = wptPos[k];
            size_t end = matchingClose(xml, "wpt", pos);
            if (end == std::string::npos) continue;   // self-closing <wpt/>
            std::string body = xml.substr(pos, end - pos);

            std::string ty;
            size_t t = body.find("<type>");
            if (t != std::string::npos) {
                size_t te = body.find("</type>", t);
                if (te != std::string::npos) ty = body.substr(t + 6, te - t - 6);
            }

            std::string icon = opt.wptIcon;
            std::string col  = opt.wptColor;
            if (opt.groupByType && !ty.empty()) {
                for (const Group &g : groups) {
                    if (g.type == ty) {
                        if (icon.empty()) icon = g.icon;
                        if (col.empty())  col  = g.color;
                        break;
                    }
                }
            }
            std::string bg = opt.wptBackground;
            if (icon.empty() && col.empty() && bg.empty()) continue;
            if (bg.empty()) bg = "circle";

            std::string inner;
            if (!col.empty())
                inner += "      <osmand:color>" + col + "</osmand:color>\n";
            if (!icon.empty())
                inner += "      <osmand:icon>" + icon + "</osmand:icon>\n";
            inner += "      <osmand:background>" + bg +
                     "</osmand:background>\n";

            size_t exi = body.find("<extensions");
            if (exi != std::string::npos) {
                size_t exEnd = matchingClose(xml, "extensions", pos + exi);
                if (exEnd == std::string::npos || exEnd > end) continue;
                size_t bodyStart = xml.find('>', pos + exi) + 1;
                std::string keep = opt.stripExisting
                        ? std::string()
                        : dropOsmandChildren(
                              xml.substr(bodyStart, exEnd - bodyStart));
                if (opt.keepExisting &&
                    xml.substr(bodyStart, exEnd - bodyStart)
                            .find("<osmand:") != std::string::npos) {
                    continue;
                }
                std::string rebuilt = "\n" + inner;
                if (!blockIsEmpty(keep)) rebuilt += keep;
                rebuilt += "    ";
                xml.replace(bodyStart, exEnd - bodyStart, rebuilt);
            } else {
                std::string block = "    <extensions>\n" + inner +
                                    "    </extensions>\n  ";
                xml.insert(end, "\n" + block);
            }
        }
    }

    // ---- per-track extensions ---------------------------------------
    // Walk the tracks backwards so that inserting text never invalidates the
    // offsets of the tracks still to be handled.
    std::vector<size_t> trkPos;
    {
        size_t i = 0;
        while ((i = xml.find("<trk", i)) != std::string::npos) {
            char after = (i + 4 < xml.size()) ? xml[i + 4] : ' ';
            if (after == '>' || after == ' ' || after == '\n' || after == '\t') {
                trkPos.push_back(i);
            }
            i += 4;
        }
    }
    res.tracks = trkPos.size();

    const std::vector<std::string> &cols = paletteColors(opt.palette);

    for (size_t k = trkPos.size(); k-- > 0;) {
        size_t pos = trkPos[k];
        size_t end = matchingClose(xml, "trk", pos);
        if (end == std::string::npos) continue;

        // --per-track-color walks the palette inside the file, so a single
        // file holding ten stages comes out as ten distinguishable colours.
        // The file's own colour is used as the starting point, so two files
        // processed together still differ from one another.
        std::string trackColor = color;
        if (opt.perTrackColor && !cols.empty()) {
            size_t start = 0;
            for (size_t c = 0; c < cols.size(); ++c) {
                if (cols[c] == color) { start = c; break; }
            }
            trackColor = cols[(start + k) % cols.size()];
        }
        if (trackColor.empty() && opt.width.empty()) continue;

        size_t open = xml.find('>', pos);
        if (open == std::string::npos) continue;

        // GPX 1.1 fixes the order inside <trk>: name, cmt, desc, src, link,
        // number, type, extensions, then the segments. So the block goes
        // immediately before the first <trkseg>, not straight after <trk>,
        // which would put it ahead of <name> and fail validation.
        size_t firstSeg = xml.find("<trkseg", open);
        if (firstSeg == std::string::npos || firstSeg > end) firstSeg = end;

        // The track's own <extensions> can only live before the first
        // <trkseg>. Searching the whole track would find the <extensions> of
        // the first <trkpt> instead - every track recorded with a heart rate
        // or cadence sensor has those - and the width would be written inside
        // a track point, where OsmAnd ignores it. The symptom was "most
        // tracks are the right width but some come out thinner".
        size_t exi = xml.find("<extensions", open);
        bool haveOwn = (exi != std::string::npos && exi < firstSeg);

        if (haveOwn) {
            size_t exEnd = matchingClose(xml, "extensions", exi);
            if (exEnd == std::string::npos || exEnd > end) continue;
            size_t bodyStart = xml.find('>', exi) + 1;
            std::string inner = xml.substr(bodyStart, exEnd - bodyStart);

            if (opt.keepExisting && inner.find("<osmand:") != std::string::npos) {
                res.warnings.push_back("track " + std::to_string(k + 1) +
                                       ": kept existing osmand tags");
                continue;
            }
            std::string kept = opt.stripExisting ? std::string()
                                                 : dropOsmandChildren(inner);
            if (!opt.stripExisting) kept = dropOsmandChildren(inner);

            std::string rebuilt;
            if (!trackColor.empty())
                rebuilt += "      <osmand:color>" + trackColor +
                           "</osmand:color>\n";
            if (!opt.width.empty())
                rebuilt += "      <osmand:width>" + opt.width +
                           "</osmand:width>\n";
            if (!blockIsEmpty(kept)) rebuilt += kept;
            else if (rebuilt.empty()) rebuilt = kept;

            xml.replace(bodyStart, exEnd - bodyStart, "\n" + rebuilt + "    ");
        } else {
            std::string block = trackExtensions(trackColor, opt.width, "    ");
            if (!block.empty()) {
                // Back up to the start of the line holding <trkseg> so the
                // inserted block keeps the file's indentation. When <trkseg>
                // shares its line with earlier tags - plenty of files put the
                // whole track on one line - break the line first, otherwise
                // the block would be spliced into the middle of it.
                size_t at = firstSeg;
                size_t ls = xml.rfind('\n', firstSeg);
                bool ownLine = false;
                if (ls != std::string::npos && ls > open) {
                    ownLine = true;
                    for (size_t j = ls + 1; j < firstSeg; ++j) {
                        if (!std::isspace((unsigned char)xml[j])) {
                            ownLine = false;
                            break;
                        }
                    }
                    if (ownLine) at = ls + 1;
                }
                xml.insert(at, ownLine ? block : "\n" + block + "    ");
            }
        }
    }

    // ---- file level extensions --------------------------------------
    //
    // A file exported by OsmAnd carries <osmand:width> and <osmand:color> at
    // the <gpx> level, and those win over the per-track values we write. Left
    // in place they quietly undo the width the user asked for - the symptom
    // is "most tracks are right but some come out thinner". So the stale
    // file-level copies are stripped whenever we set those per track.
    std::string fileBlock = fileExtensions(opt, groups);
    const bool takeOverWidth = !opt.width.empty();
    const bool takeOverColor = !color.empty();
    if (!fileBlock.empty() || takeOverWidth || takeOverColor) {
        size_t gpxClose = xml.rfind("</gpx>");
        if (gpxClose == std::string::npos) {
            res.skipped = true;
            res.skipReason = "no closing </gpx>";
            return true;
        }
        // GPX 1.1 wants <extensions> last inside <gpx>. If the file already
        // has one there, merge into it rather than adding a second.
        size_t lastTrk = xml.rfind("</trk>");
        size_t lastWpt = xml.rfind("</wpt>");
        size_t after = std::string::npos;
        if (lastTrk != std::string::npos) after = lastTrk;
        if (lastWpt != std::string::npos &&
            (after == std::string::npos || lastWpt > after)) after = lastWpt;

        size_t existing = std::string::npos;
        if (after != std::string::npos) {
            size_t cand = xml.find("<extensions", after);
            if (cand != std::string::npos && cand < gpxClose) existing = cand;
        }
        if (existing != std::string::npos) {
            size_t exEnd = matchingClose(xml, "extensions", existing);
            if (exEnd != std::string::npos) {
                size_t bodyStart = xml.find('>', existing) + 1;
                const std::string inner = xml.substr(bodyStart, exEnd - bodyStart);

                // Keep whatever is not ours - Garmin, Wikiloc and the rest -
                // and drop only the osmand keys we are replacing.
                std::string kept = opt.stripExisting
                        ? std::string()
                        : dropOsmandChildren(inner);

                std::string merged = fileBlock;
                if (merged.empty() && !blockIsEmpty(kept)) {
                    merged = "  <extensions>\n" + kept + "  </extensions>\n";
                } else if (!merged.empty() && !blockIsEmpty(kept)) {
                    const std::string close = "  </extensions>\n";
                    size_t at = merged.rfind(close);
                    if (at != std::string::npos) merged.insert(at, kept);
                }

                size_t lineStart = xml.rfind('\n', existing);
                if (lineStart == std::string::npos) lineStart = existing;
                xml.replace(lineStart, exEnd + 13 - lineStart,
                            merged.empty() ? std::string() : "\n" + merged);
            }
        } else if (!fileBlock.empty()) {
            xml.insert(gpxClose, fileBlock);
        }
    }

    res.groupsFound = groups;

    // A waypoint sitting on the first or last track point disappears under the
    // start/finish marker OsmAnd draws there. It looks exactly like the icon
    // failed to apply - it cost four rounds of device testing to work out that
    // the icons had been there all along. Warn instead of letting the user
    // hunt for it.
    if (opt.startFinish == Tri::On && wptCount > 0) {
        std::vector<std::string> ends;
        size_t p = 0;
        while ((p = xml.find("<trkseg", p)) != std::string::npos) {
            size_t segEnd = matchingClose(xml, "trkseg", p);
            if (segEnd == std::string::npos) break;
            std::string seg = xml.substr(p, segEnd - p);
            size_t f = seg.find("<trkpt");
            size_t l = seg.rfind("<trkpt");
            for (size_t q : {f, l}) {
                if (q == std::string::npos) continue;
                size_t gt = seg.find('>', q);
                if (gt != std::string::npos) ends.push_back(seg.substr(q, gt - q));
            }
            p = segEnd + 8;
        }
        auto coord = [](const std::string &tag, const char *what) {
            size_t a = tag.find(std::string(what) + "=\"");
            if (a == std::string::npos) return std::string();
            a += std::strlen(what) + 2;
            size_t b = tag.find('"', a);
            if (b == std::string::npos) return std::string();
            std::string v = tag.substr(a, b - a);
            // Compare at ~1 m: trailing zeros differ between writers, the
            // position does not.
            size_t dot = v.find('.');
            if (dot != std::string::npos && v.size() > dot + 5)
                v = v.substr(0, dot + 6);
            while (v.size() > 1 && v.back() == '0') v.pop_back();
            if (!v.empty() && v.back() == '.') v.pop_back();
            return v;
        };
        size_t clash = 0;
        size_t w = 0;
        while ((w = xml.find("<wpt", w)) != std::string::npos) {
            size_t gt = xml.find('>', w);
            if (gt == std::string::npos) break;
            std::string tag = xml.substr(w, gt - w);
            std::string wl = coord(tag, "lat"), wo = coord(tag, "lon");
            for (const std::string &e : ends) {
                if (!wl.empty() && wl == coord(e, "lat") &&
                    !wo.empty() && wo == coord(e, "lon")) { ++clash; break; }
            }
            w = gt;
        }
        if (clash > 0) {
            res.warnings.push_back(
                std::to_string(clash) +
                " waypoint(s) sit on a track start or end: the start/finish "
                "marker will cover the icon. Use --start-finish off to see them.");
        }
    }

    if (capture) { *capture = xml; res.written = false; return true; }

    if (opt.dryRun) { res.written = false; return true; }

    if (opt.inPlace && opt.backup) {
        bool bok = false;
        std::string orig = readWhole(inPath, bok);
        if (bok) writeWhole(inPath + ".bak", orig);
    }
    if (!writeWhole(outPath, xml)) {
        error = "Cannot write: " + outPath;
        return false;
    }
    res.written = true;
    return true;
}


// ------------------------------------------------------------ merging
//
// Importing ten files into OsmAnd means ten imports and ten trips through the
// appearance menu. Merging them into one multi-track GPX means a single
// import and no configuration at all, while each track keeps its own colour
// because the colour lives inside <trk><extensions>, not on the file.
//
// The merge is textual on purpose: the styled XML is already correct, so
// lifting whole <wpt> and <trk> elements out of it cannot corrupt what the
// styling just produced. GPX 1.1 fixes the order of children of <gpx>:
// metadata, then every wpt, then rte, then trk, then extensions. Waypoints
// are therefore buffered separately from tracks and written first.

// Collect the xmlns:PREFIX="URI" declarations of a document root. Harvested
// tracks may carry Garmin (gpxx:), Wikiloc or Strava tags; if the merged root
// does not declare their prefixes the result is not well-formed XML and
// nothing will open it. Conflicts (same prefix, different URI) keep the first
// definition: silently rewriting a foreign namespace would be worse.
void collectNamespaces(const std::string &xml,
                       std::map<std::string, std::string> &ns) {
    size_t g = xml.find("<gpx");
    if (g == std::string::npos) return;
    size_t close = xml.find('>', g);
    if (close == std::string::npos) return;
    std::string tag = xml.substr(g, close - g);
    size_t i = 0;
    while ((i = tag.find("xmlns:", i)) != std::string::npos) {
        size_t nameEnd = tag.find('=', i);
        if (nameEnd == std::string::npos) break;
        std::string prefix = tag.substr(i + 6, nameEnd - i - 6);
        size_t q1 = tag.find('"', nameEnd);
        if (q1 == std::string::npos) break;
        size_t q2 = tag.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string uri = tag.substr(q1 + 1, q2 - q1 - 1);
        while (!prefix.empty() && std::isspace((unsigned char)prefix.back()))
            prefix.pop_back();
        if (!prefix.empty() && ns.find(prefix) == ns.end()) ns[prefix] = uri;
        i = q2 + 1;
    }
}

// Pull out every top-level <name>...</name> element of a <trk> block.
std::string trackNameOf(const std::string &trk) {
    size_t n = trk.find("<name>");
    if (n == std::string::npos) return std::string();
    size_t e = trk.find("</name>", n);
    if (e == std::string::npos) return std::string();
    return trk.substr(n + 6, e - n - 6);
}

// Give a track a name when it has none, or prefix the file it came from, so
// the OsmAnd track list stays readable after the merge.
std::string renameTrack(const std::string &trk, const std::string &label) {
    size_t open = trk.find('>');
    if (open == std::string::npos) return trk;
    size_t n = trk.find("<name>");
    size_t firstSeg = trk.find("<trkseg");
    if (n != std::string::npos && (firstSeg == std::string::npos || n < firstSeg)) {
        size_t e = trk.find("</name>", n);
        if (e == std::string::npos) return trk;
        std::string cur = trk.substr(n + 6, e - n - 6);
        if (cur == label || cur.find(label) != std::string::npos) return trk;
        std::string out = trk;
        out.replace(n + 6, e - n - 6, xmlEscape(label) + " - " + cur);
        return out;
    }
    std::string out = trk;
    out.insert(open + 1, "\n    <name>" + xmlEscape(label) + "</name>");
    return out;
}

// Collect the top-level <wpt> and <trk> elements of one styled document.
void harvest(const std::string &xml, const std::string &label,
             std::string &wpts, std::string &trks,
             size_t &nWpt, size_t &nTrk) {
    size_t i = 0;
    while ((i = xml.find("<wpt", i)) != std::string::npos) {
        char after = (i + 4 < xml.size()) ? xml[i + 4] : ' ';
        if (after != '>' && after != ' ' && after != '\n' && after != '\t' &&
            after != '\r') { i += 4; continue; }
        size_t end = matchingClose(xml, "wpt", i);
        if (end == std::string::npos) {
            size_t sc = xml.find("/>", i);
            if (sc == std::string::npos) break;
            wpts += "  " + xml.substr(i, sc + 2 - i) + "\n";
            ++nWpt;
            i = sc + 2;
            continue;
        }
        wpts += "  " + xml.substr(i, end + 6 - i) + "\n";
        ++nWpt;
        i = end + 6;
    }
    i = 0;
    while ((i = xml.find("<trk", i)) != std::string::npos) {
        char after = (i + 4 < xml.size()) ? xml[i + 4] : ' ';
        if (after != '>' && after != ' ' && after != '\n' && after != '\t' &&
            after != '\r') { i += 4; continue; }
        size_t end = matchingClose(xml, "trk", i);
        if (end == std::string::npos) break;
        std::string one = xml.substr(i, end + 6 - i);
        if (!label.empty()) one = renameTrack(one, label);
        trks += "  " + one + "\n";
        ++nTrk;
        i = end + 6;
    }
}

// ------------------------------------------------------------- the run

bool run(const std::vector<std::string> &inputs, const Options &opt,
         RunStats &stats, std::string &error,
         ProgressFn progress, void *user) {
    if (!validate(opt, error)) return false;

    std::set<std::string> usedOutputs;
    std::map<std::string, Group> allGroups;
    // Waypoint-group colours are shared by the whole run, see styleFile.
    std::map<std::string, std::string> groupColors;

    // --- merge bookkeeping
    std::string mergedWpts, mergedTrks, mergePath;
    std::map<std::string, std::string> mergedNs;
    size_t nWpt = 0, nTrk = 0, nSources = 0;
    if (opt.merge) {
        std::string name = opt.mergeName.empty() ? "all-tracks" : opt.mergeName;
        if (name.size() < 4 ||
            lowerCopy(name.substr(name.size() - 4)) != ".gpx") name += ".gpx";
        std::string dir = opt.outDir;
        if (dir.empty() && !inputs.empty()) dir = slope::dirName(inputs[0]);
        mergePath = dir.empty() ? name : slope::joinPath(dir, name);
    }

    // The merged file lands in the same folder it reads from, so a second run
    // would swallow its own output and double every track. Drop it up front.
    std::vector<std::string> work;
    size_t reMerged = 0;
    for (const std::string &p : inputs) {
        if (opt.merge && !mergePath.empty() &&
            lowerCopy(slope::baseName(p)) == lowerCopy(slope::baseName(mergePath))) {
            ++reMerged;
            continue;
        }
        work.push_back(p);
    }
    if (opt.merge && work.empty() && reMerged > 0) {
        error = "the only .gpx here is " + slope::baseName(mergePath) +
                ", which this run would create. Point it at the original "
                "files, or pass --merge-name to use another name.";
        return false;
    }

    std::vector<std::string> autoColorsW;
    if (opt.autoColor) autoColorsW = assignColors(work, opt.palette, opt.autoMode);

    // Merging puts every track in one list, so two tracks that came from the
    // same source file would be indistinguishable: --auto-color assigns one
    // colour per FILE. When merging, spread the palette across tracks instead.
    Options eff = opt;
    if (opt.merge && opt.autoColor) eff.perTrackColor = true;

    for (size_t i = 0; i < work.size(); ++i) {
        const std::string &in = work[i];

        std::string color = opt.color;
        if (opt.autoColor && i < autoColorsW.size()) color = autoColorsW[i];

        std::string out;
        if (opt.inPlace) {
            out = in;
        } else {
            std::string base = slope::stripExtension(slope::baseName(in));
            std::string name = base + opt.suffix + ".gpx";
            out = opt.outDir.empty() ? slope::joinPath(slope::dirName(in), name)
                                     : slope::joinPath(opt.outDir, name);
        }

        FileResult res;
        if (!opt.merge && !opt.inPlace &&
            !usedOutputs.insert(lowerCopy(out)).second) {
            res.input = in;
            res.output = out;
            res.skipped = true;
            res.skipReason = "another file already writes to this name";
            stats.files.push_back(res);
            stats.skipped++;
            if (progress) progress(user, i + 1, work.size());
            continue;
        }

        std::string err;
        std::string styled;
        if (!styleFile(in, out, eff, color, res, err,
                       opt.merge ? &styled : nullptr, &groupColors)) {
            res.skipped = true;
            res.skipReason = err;
            stats.files.push_back(res);
            stats.failed++;
            if (progress) progress(user, i + 1, work.size());
            continue;
        }
        if (opt.merge && !res.skipped) {
            // The merged file is the deliverable, so report the destination
            // every track actually ends up in, not a per-file name that is
            // never written.
            res.output = mergePath;
            std::string label = slope::stripExtension(slope::baseName(in));
            collectNamespaces(styled, mergedNs);
            harvest(styled, label, mergedWpts, mergedTrks, nWpt, nTrk);
            ++nSources;
        }
        if (res.skipped) stats.skipped++;
        else if (res.written) stats.written++;
        else if (opt.merge && !res.skipped) stats.written++;

        for (const Group &g : res.groupsFound) {
            auto it = allGroups.find(g.type);
            if (it == allGroups.end()) allGroups[g.type] = g;
            else it->second.count += g.count;
        }
        stats.files.push_back(res);
        if (progress) progress(user, i + 1, work.size());
    }

    for (const auto &kv : allGroups) stats.groups.push_back(kv.second);

    if (opt.merge) {
        stats.merged.output    = mergePath;
        stats.merged.tracks    = nTrk;
        stats.merged.waypoints = nWpt;
        stats.merged.sources   = nSources;

        if (nTrk == 0 && nWpt == 0) {
            error = "nothing to merge: no tracks or waypoints were found.";
            return false;
        }
        if (!opt.dryRun) {
            // The file-level block carries arrows, split and the waypoint
            // groups. It is built once for the merged document: written per
            // track it would be ignored, which is why it lives here.
            std::vector<Group> groups;
            for (const auto &kv : allGroups) groups.push_back(kv.second);

            std::string doc;
            doc += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            doc += "<gpx version=\"1.1\" creator=\"gpx-slope-colors\"\n";
            doc += "     xmlns=\"http://www.topografix.com/GPX/1/1\"\n";
            doc += std::string("     xmlns:osmand=\"") + kOsmandNs + "\"";
            // Prefixes used by the harvested tracks, e.g. Garmin's gpxx.
            for (const auto &kv : mergedNs) {
                if (kv.first == "osmand") continue;
                doc += "\n     xmlns:" + kv.first + "=\"" + kv.second + "\"";
            }
            doc += ">\n";
            doc += "  <metadata>\n    <name>" +
                   xmlEscape(slope::stripExtension(slope::baseName(mergePath))) +
                   "</name>\n  </metadata>\n";
            doc += mergedWpts;      // GPX 1.1: every wpt before any trk
            doc += mergedTrks;
            doc += fileExtensions(opt, groups);
            doc += "</gpx>\n";

            if (!writeWhole(mergePath, doc)) {
                error = "Cannot write: " + mergePath;
                return false;
            }
            stats.merged.written = true;
        }
    }
    return true;
}

}  // namespace styler
