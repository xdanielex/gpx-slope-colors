#include "slope_core.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace slope {
namespace {

const double PI = 3.14159265358979323846;

// On Windows the narrow fstream constructors go through the ANSI code page,
// so a path with accents or non-Latin characters would fail to open. Paths
// travel through this code as UTF-8, so widen them and use the wide overload
// libstdc++ provides on MinGW.
#ifdef _WIN32
std::wstring widenPath(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
void openIn(std::ifstream &f, const std::string &path) {
    f.open(widenPath(path).c_str(), std::ios::binary);
}
void openOut(std::ofstream &f, const std::string &path) {
    f.open(widenPath(path).c_str(), std::ios::binary);
}
#else
void openIn(std::ifstream &f, const std::string &path) {
    f.open(path.c_str(), std::ios::binary);
}
void openOut(std::ofstream &f, const std::string &path) {
    f.open(path.c_str(), std::ios::binary);
}
#endif

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Normalise a colour token: lowercase, drop spaces / dashes / underscores.
std::string normaliseName(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool isHexDigits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Read an XML attribute value: name="..."
bool attr(const std::string &tag, const std::string &name, std::string &out) {
    size_t p = tag.find(name + "=");
    if (p == std::string::npos) return false;
    p += name.size() + 1;
    if (p >= tag.size()) return false;
    char q = tag[p];
    if (q != '"' && q != '\'') return false;
    size_t e = tag.find(q, p + 1);
    if (e == std::string::npos) return false;
    out = tag.substr(p + 1, e - p - 1);
    return true;
}

// Text inside the first <name>...</name> child, namespace tolerant.
bool childText(const std::string &body, const std::string &name,
               std::string &out) {
    // matches <name> or <ns:name>
    size_t pos = 0;
    while (true) {
        size_t lt = body.find('<', pos);
        if (lt == std::string::npos) return false;
        size_t gt = body.find('>', lt);
        if (gt == std::string::npos) return false;
        std::string tag = body.substr(lt + 1, gt - lt - 1);
        if (!tag.empty() && tag[0] != '/' && tag[0] != '?' && tag[0] != '!') {
            std::string bare = tag;
            size_t sp = bare.find_first_of(" \t\r\n/");
            if (sp != std::string::npos) bare = bare.substr(0, sp);
            size_t colon = bare.find(':');
            std::string local = (colon == std::string::npos)
                                    ? bare : bare.substr(colon + 1);
            if (lower(local) == name) {
                if (!tag.empty() && tag.back() == '/') { out.clear(); return true; }
                size_t close = body.find("</", gt);
                if (close == std::string::npos) return false;
                out = body.substr(gt + 1, close - gt - 1);
                return true;
            }
        }
        pos = gt + 1;
    }
}

// Local name of the element starting at `lt` ("<gpxtpx:hr ...>" -> "hr").
// Returns an empty string for closing tags, comments and declarations.
std::string tagLocalName(const std::string &s, size_t lt, bool &closing) {
    closing = false;
    if (lt + 1 >= s.size()) return "";
    size_t i = lt + 1;
    if (s[i] == '/') { closing = true; ++i; }
    if (i < s.size() && (s[i] == '?' || s[i] == '!')) return "";
    size_t end = s.find_first_of(" \t\r\n/>", i);
    if (end == std::string::npos) return "";
    std::string bare = s.substr(i, end - i);
    size_t colon = bare.find(':');
    return lower(colon == std::string::npos ? bare : bare.substr(colon + 1));
}

// Offset of the opening tag of the first `name` child, namespace tolerant.
size_t findChildTag(const std::string &body, const std::string &name) {
    size_t pos = 0;
    while (true) {
        size_t lt = body.find('<', pos);
        if (lt == std::string::npos) return std::string::npos;
        bool closing = false;
        if (tagLocalName(body, lt, closing) == name && !closing) return lt;
        pos = lt + 1;
    }
}

// Offset just past the matching close tag for the element opening at `start`.
// Counts nesting, so an <extensions> containing another <extensions> is safe,
// and handles the self-closing "<extensions/>" case.
size_t findChildClose(const std::string &body, const std::string &name,
                      size_t start) {
    size_t gt = body.find('>', start);
    if (gt == std::string::npos) return std::string::npos;
    if (gt > start && body[gt - 1] == '/') return gt + 1;   // self-closing

    int depth = 1;
    size_t pos = gt + 1;
    while (depth > 0) {
        size_t lt = body.find('<', pos);
        if (lt == std::string::npos) return std::string::npos;
        bool closing = false;
        if (tagLocalName(body, lt, closing) == name) {
            if (closing) {
                --depth;
                if (depth == 0) {
                    size_t e = body.find('>', lt);
                    return e == std::string::npos ? std::string::npos : e + 1;
                }
            } else {
                size_t e = body.find('>', lt);
                if (e == std::string::npos) return std::string::npos;
                if (!(e > lt && body[e - 1] == '/')) ++depth;
            }
        }
        pos = lt + 1;
    }
    return std::string::npos;
}

// Drop trailing spaces and newlines, and any stray carriage returns, so the
// block can be re-indented cleanly on output.
std::string trimTrailingBlank(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) if (c != '\r') out += c;
    size_t end = out.find_last_not_of(" \t\n");
    return end == std::string::npos ? "" : out.substr(0, end + 1);
}

std::string readWholeFile(const std::string &path, bool &ok) {
    std::ifstream f;
    openIn(f, path);
    if (!f) { ok = false; return ""; }
    std::ostringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string xmlHeader(const std::string &name) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<gpx version=\"1.1\" creator=\"gpx-slope-colors\"\n"
           "     xmlns=\"http://www.topografix.com/GPX/1/1\"\n"
           // The URI documented by OsmAnd. The app does not validate it, but
           // matching the published form keeps the file correct for anything
           // that does.
           "     xmlns:osmand=\"https://osmand.net/docs/technical/"
           "osmand-file-formats/osmand-gpx\"\n"
           "     xmlns:gpxx=\"http://www.garmin.com/xmlschemas/GpxExtensions/v3\"\n"
           "     xmlns:gpxtrx=\"http://www.garmin.com/xmlschemas/GpxExtensions/v3\"\n"
           // Declared because preserved per-point <extensions> blocks carry
           // heart rate, cadence and power in these namespaces. Without the
           // declarations the copied prefixes would be undefined and the file
           // would not parse.
           "     xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/"
           "TrackPointExtension/v1\"\n"
           "     xmlns:gpxpx=\"http://www.garmin.com/xmlschemas/"
           "PowerExtension/v1\">\n"
           "  <metadata><name>" + name + "</name></metadata>\n";
}

// ---------------------------------------------------------- Garmin colours
//
// Garmin's GpxExtensions v3 schema allows a track colour, but only as one of
// 17 fixed names - no hex. So an arbitrary colour has to be snapped to the
// nearest one. Matching is done in RGB space, which is crude but perfectly
// adequate for picking between seventeen widely spaced colours.
//
// Note the schema is published under one namespace but Garmin's own devices
// and desktop software disagree on the prefix: BaseCamp writes "gpxx", the
// handhelds expect "gpxtrx", and each ignores the other. We therefore emit
// both, which costs two lines and makes the colour show up in both places.

struct GarminColor { const char *name; int r, g, b; };

const GarminColor kGarminColors[] = {
    {"Black",       0,   0,   0},
    {"DarkRed",     139, 0,   0},
    {"DarkGreen",   0,   100, 0},
    {"DarkYellow",  139, 128, 0},
    {"DarkBlue",    0,   0,   139},
    {"DarkMagenta", 139, 0,   139},
    {"DarkCyan",    0,   139, 139},
    {"LightGray",   211, 211, 211},
    {"DarkGray",    169, 169, 169},
    {"Red",         255, 0,   0},
    {"Green",       0,   255, 0},
    {"Yellow",      255, 255, 0},
    {"Blue",        0,   0,   255},
    {"Magenta",     255, 0,   255},
    {"Cyan",        0,   255, 255},
    {"White",       255, 255, 255},
};

// Convert one sRGB channel (0-255) to its linear-light value.
double srgbToLinear(int v) {
    double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

struct Lab { double l, a, b; };

// sRGB -> CIELAB, D65 white point.
Lab toLab(int r8, int g8, int b8) {
    const double r = srgbToLinear(r8);
    const double g = srgbToLinear(g8);
    const double b = srgbToLinear(b8);

    const double X = r * 0.4124 + g * 0.3576 + b * 0.1805;
    const double Y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    const double Z = r * 0.0193 + g * 0.1192 + b * 0.9505;

    auto f = [](double t) {
        return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0;
    };
    const double fx = f(X / 0.95047);
    const double fy = f(Y / 1.00000);
    const double fz = f(Z / 1.08883);

    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

// Map "#rrggbb" onto the closest name Garmin will accept.
//
// The comparison happens in CIELAB rather than RGB. Plain RGB distance gives
// visibly wrong answers here: our green #00a03c comes out closer to DarkCyan
// than to DarkGreen, and our blue #0a7ef0 also lands on DarkCyan, because
// RGB distance has little to do with how colours actually look. CIELAB is
// built for perceptual comparison and puts every colour in the right family.
std::string garminColorName(const std::string &hex) {
    if (hex.size() != 7 || hex[0] != '#') return "Red";
    unsigned r = 0, g = 0, b = 0;
    if (std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3)
        return "Red";

    const Lab want = toLab(static_cast<int>(r), static_cast<int>(g),
                           static_cast<int>(b));
    const char *best = "Red";
    double bestDist = -1.0;
    for (const GarminColor &c : kGarminColors) {
        const Lab have = toLab(c.r, c.g, c.b);
        const double dl = want.l - have.l;
        const double da = want.a - have.a;
        const double db = want.b - have.b;
        const double d = dl * dl + da * da + db * db;
        if (bestDist < 0.0 || d < bestDist) { bestDist = d; best = c.name; }
    }
    return best;
}

// Re-indent a preserved <extensions> block to sit under a <trkpt>.
// Inner lines get one extra level so nesting stays readable; the closing
// </extensions> is pulled back to line up with its opening tag.
std::string indentBlock(const std::string &block, const std::string &pad) {
    std::string out;
    size_t i = 0;
    bool first = true;
    while (i < block.size()) {
        size_t nl = block.find('\n', i);
        std::string line = block.substr(i, nl == std::string::npos
                                               ? std::string::npos : nl - i);
        size_t a = line.find_first_not_of(" \t");
        if (a != std::string::npos) {
            std::string body = line.substr(a);
            bool closer = body.compare(0, 2, "</") == 0;
            out += (first || closer) ? pad : pad + "  ";
            out += body;
            out += "\n";
            first = false;
        }
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return out;
}

std::string trkBlock(const std::string &name, const std::string &color,
                     const std::vector<Point> &pts, size_t from, size_t to,
                     const std::string &width, bool arrows) {
    std::string o;
    o += "  <trk>\n    <name>" + name + "</name>\n";
    o += "    <extensions>\n";
    o += "      <osmand:color>" + color + "</osmand:color>\n";
    o += "      <osmand:width>" + width + "</osmand:width>\n";
    o += std::string("      <osmand:show_arrows>") + (arrows ? "true" : "false") +
         "</osmand:show_arrows>\n";
    o += "      <osmand:coloring_type>solid</osmand:coloring_type>\n";
    // Garmin-compatible colour, snapped to their fixed palette. Emitted under
    // both prefixes because BaseCamp and the handhelds each read only one.
    const std::string gname = garminColorName(color);
    o += "      <gpxx:TrackExtension><gpxx:DisplayColor>" + gname +
         "</gpxx:DisplayColor></gpxx:TrackExtension>\n";
    o += "      <gpxtrx:TrackExtension><gpxtrx:DisplayColor>" + gname +
         "</gpxtrx:DisplayColor></gpxtrx:TrackExtension>\n";
    o += "    </extensions>\n";
    o += "    <trkseg>\n";
    for (size_t i = from; i <= to && i < pts.size(); ++i) {
        const Point &p = pts[i];
        o += "      <trkpt lat=\"" + fmt(p.lat, 7) + "\" lon=\"" +
             fmt(p.lon, 7) + "\">\n";
        if (p.hasEle) o += "        <ele>" + fmt(p.ele, 1) + "</ele>\n";
        if (!p.time.empty()) o += "        <time>" + p.time + "</time>\n";
        if (!p.extra.empty()) o += indentBlock(p.extra, "        ");
        o += "      </trkpt>\n";
    }
    o += "    </trkseg>\n  </trk>\n";
    return o;
}

// File-level <extensions>, written just before </gpx>.
//
// OsmAnd only honours show_arrows and show_start_finish when they sit at the
// <gpx> level; the same tags inside <trk><extensions> are silently ignored.
// Verified on OsmAnd Android, September 2026: two files identical except for
// the position of the block, only the <gpx>-level one drew the arrows.
// Per-track colour and width, by contrast, DO work inside <trk>, which is why
// they stay there and only the file-wide switches are repeated here.
//
// GPX 1.1 requires <extensions> to be the last child of <gpx>, after every
// <wpt>, <rte> and <trk>, so this must be emitted immediately before the
// closing tag.
std::string gpxLevelExtensions(bool arrows, const Options &opt) {
    std::string o;
    o += "  <extensions>\n";
    o += std::string("    <osmand:show_arrows>") + (arrows ? "true" : "false") +
         "</osmand:show_arrows>\n";
    if (!opt.splitType.empty())
        o += "    <osmand:split_type>" + opt.splitType +
             "</osmand:split_type>\n";
    if (!opt.splitInterval.empty())
        o += "    <osmand:split_interval>" + asDouble(opt.splitInterval) +
             "</osmand:split_interval>\n";
    // The 3D wall. Written only when asked for: it is a paid OsmAnd feature,
    // and the tags are harmless but pointless for everyone else.
    // Empty means "not asked for", so nothing is written and the file keeps
    // whatever it had. An explicit "none" is a request to turn the wall off,
    // which needs the tag to actually be there.
    if (opt.viz3d == "none") {
        o += "    <osmand:line_3d_visualization_by_type>none"
             "</osmand:line_3d_visualization_by_type>\n";
    } else if (!opt.viz3d.empty()) {
        o += "    <osmand:line_3d_visualization_by_type>" + opt.viz3d +
             "</osmand:line_3d_visualization_by_type>\n";
        // "solid" makes the wall take each track's own colour, so under an
        // uphill section the wall is the uphill colour. Any gradient here
        // would paint over the slope colours this program just worked out.
        o += "    <osmand:line_3d_visualization_wall_color_type>" +
             (opt.wall3d.empty() ? std::string("solid") : opt.wall3d) +
             "</osmand:line_3d_visualization_wall_color_type>\n";
        o += "    <osmand:line_3d_visualization_position_type>" +
             (opt.wallPos3d.empty() ? std::string("bottom") : opt.wallPos3d) +
             "</osmand:line_3d_visualization_position_type>\n";
        if (!opt.scale3d.empty())
            o += "    <osmand:vertical_exaggeration_scale>" + opt.scale3d +
                 "</osmand:vertical_exaggeration_scale>\n";
        if (opt.viz3d == "fixed_height" && !opt.height3d.empty())
            o += "    <osmand:elevation_meters>" + opt.height3d +
                 "</osmand:elevation_meters>\n";
    }
    o += "  </extensions>\n";
    return o;
}

const char *klassLabel(Klass k) {
    switch (k) {
        case Klass::Uphill:   return "Uphill";
        case Klass::Downhill: return "Downhill";
        default:              return "Flat";
    }
}

const char *klassSlug(Klass k) {
    switch (k) {
        case Klass::Uphill:   return "uphill";
        case Klass::Downhill: return "downhill";
        default:              return "flat";
    }
}

}  // namespace

// ---------------------------------------------------------------- colours

const std::vector<NamedColor> &palette() {
    static const std::vector<NamedColor> p = {
        {"red",        "rosso",              "#e01b1b"},
        {"darkred",    "rossoscuro",         "#8b0000"},
        {"orange",     "arancione",          "#ff7a00"},
        {"yellow",     "giallo",             "#f2c200"},
        {"green",      "verde",              "#00a03c"},
        {"lightgreen", "verdechiaro",        "#5ad45f"},
        {"darkgreen",  "verdescuro",         "#00602a"},
        {"lightblue",  "azzurro",            "#00bfff"},
        {"blue",       "blu",                "#0a7ef0"},
        {"darkblue",   "bluscuro",           "#00337f"},
        {"cyan",       "ciano",              "#00e5e5"},
        {"purple",     "viola",              "#9b30d9"},
        {"magenta",    "",                   "#c724b1"},
        {"pink",       "fucsia",             "#ff2d95"},
        {"brown",      "marrone",            "#8b5a2b"},
        {"gray",       "grey,grigio",        "#8c8c8c"},
        {"lightgray",  "lightgrey,grigiochiaro", "#c8c8c8"},
        {"black",      "nero",               "#1a1a1a"},
        {"white",      "bianco",             "#ffffff"},
    };
    return p;
}

bool resolveColor(const std::string &in, std::string &out, std::string &error) {
    std::string t = normaliseName(in);
    if (t.empty()) { error = "empty colour"; return false; }

    for (const NamedColor &c : palette()) {
        if (t == c.name) { out = c.hex; return true; }
        std::string al = c.aliases;
        size_t start = 0;
        while (!al.empty() && start <= al.size()) {
            size_t comma = al.find(',', start);
            std::string one = al.substr(start, comma == std::string::npos
                                                   ? std::string::npos
                                                   : comma - start);
            if (!one.empty() && t == one) { out = c.hex; return true; }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    std::string h = t;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if ((h.size() == 6 || h.size() == 8) && isHexDigits(h)) {
        out = "#" + h;
        return true;
    }

    std::string names;
    for (const NamedColor &c : palette()) {
        if (!names.empty()) names += ", ";
        names += c.name;
    }
    error = "Unknown colour: '" + in + "'\n"
            "Use a hex code (#rrggbb) or one of these names:\n  " + names;
    return false;
}

bool validWidth(const std::string &w, std::string &error) {
    std::string t = lower(trim(w));
    if (t == "thin" || t == "medium" || t == "bold") return true;
    if (t.empty()) { error = "width cannot be empty"; return false; }
    for (char c : t)
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            error = "Width must be thin, medium, bold, or a number from 1 to 24.";
            return false;
        }
    int v = std::atoi(t.c_str());
    if (v < 1 || v > 24) {
        error = "Width must be between 1 and 24.";
        return false;
    }
    return true;
}

// ----------------------------------------------------------------- paths

std::string baseName(const std::string &path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

std::string dirName(const std::string &path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string() : path.substr(0, p);
}

std::string stripExtension(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return path;
    if (slash != std::string::npos && dot < slash) return path;
    return path.substr(0, dot);
}

std::string joinPath(const std::string &dir, const std::string &name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

std::string defaultOutputPath(const std::string &inputPath,
                              const std::string &suffix) {
    return stripExtension(inputPath) + suffix + ".gpx";
}

// -------------------------------------------------------------- geometry

double haversine(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;
    double p1 = lat1 * PI / 180.0;
    double p2 = lat2 * PI / 180.0;
    double dp = p2 - p1;
    double dl = (lon2 - lon1) * PI / 180.0;
    double a = std::sin(dp / 2) * std::sin(dp / 2) +
               std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return 2.0 * R * std::asin(std::sqrt(a));
}

// --------------------------------------------------------------- parsing

bool readPoints(const std::string &path, std::vector<Point> &out,
                std::string &error) {
    bool ok = false;
    std::string text = readWholeFile(path, ok);
    if (!ok) { error = "Cannot open file: " + path; return false; }

    out.clear();
    size_t pos = 0;
    while (true) {
        size_t p = text.find("<trkpt", pos);
        if (p == std::string::npos) break;
        // guard against matching e.g. <trkptx
        char after = (p + 6 < text.size()) ? text[p + 6] : ' ';
        if (!(after == ' ' || after == '\t' || after == '\n' || after == '\r')) {
            pos = p + 6;
            continue;
        }
        size_t gt = text.find('>', p);
        if (gt == std::string::npos) break;
        std::string openTag = text.substr(p, gt - p + 1);
        bool selfClosing = (gt > 0 && text[gt - 1] == '/');

        Point pt;
        std::string v;
        if (!attr(openTag, "lat", v)) { pos = gt + 1; continue; }
        pt.lat = std::atof(v.c_str());
        if (!attr(openTag, "lon", v)) { pos = gt + 1; continue; }
        pt.lon = std::atof(v.c_str());

        if (selfClosing) {
            out.push_back(pt);
            pos = gt + 1;
            continue;
        }

        size_t close = text.find("</trkpt>", gt);
        if (close == std::string::npos) break;
        std::string body = text.substr(gt + 1, close - gt - 1);

        std::string t;
        if (childText(body, "ele", t)) {
            std::string s = trim(t);
            if (!s.empty()) { pt.ele = std::atof(s.c_str()); pt.hasEle = true; }
        }
        if (childText(body, "time", t)) pt.time = trim(t);

        // Keep the point's <extensions> block exactly as it came in. This is
        // where heart rate, cadence, power and temperature live, and throwing
        // it away would quietly degrade the file for anything that reads
        // sensor data. We do not parse it, we just carry it across.
        size_t ex = findChildTag(body, "extensions");
        if (ex != std::string::npos) {
            size_t exEnd = findChildClose(body, "extensions", ex);
            if (exEnd != std::string::npos)
                pt.extra = trimTrailingBlank(body.substr(ex, exEnd - ex));
        }

        out.push_back(pt);
        pos = close + 8;
    }

    if (out.empty()) {
        error = "No track points found. Is this a GPX file with a <trk>?";
        return false;
    }
    return true;
}

std::vector<std::string> readWaypoints(const std::string &path) {
    std::vector<std::string> out;
    bool ok = false;
    std::string text = readWholeFile(path, ok);
    if (!ok) return out;

    size_t pos = 0;
    while (true) {
        size_t p = text.find("<wpt", pos);
        if (p == std::string::npos) break;
        char after = (p + 4 < text.size()) ? text[p + 4] : ' ';
        if (!(after == ' ' || after == '\t' || after == '\n' || after == '\r')) {
            pos = p + 4;
            continue;
        }
        size_t gt = text.find('>', p);
        if (gt == std::string::npos) break;
        if (text[gt - 1] == '/') {                 // <wpt ... />
            out.push_back(text.substr(p, gt - p + 1));
            pos = gt + 1;
            continue;
        }
        size_t close = text.find("</wpt>", gt);
        if (close == std::string::npos) break;
        out.push_back(text.substr(p, close - p + 6));
        pos = close + 6;
    }
    // Normalise line endings so a CRLF source file does not produce a mixed
    // output, and trim trailing blanks left by the original indentation.
    for (std::string &w : out) {
        std::string clean;
        clean.reserve(w.size());
        for (char c : w)
            if (c != '\r') clean += c;
        while (!clean.empty() &&
               (clean.back() == '\n' || clean.back() == ' ' || clean.back() == '\t'))
            clean.erase(clean.size() - 1);
        w.swap(clean);
    }
    return out;
}

bool fillMissingElevations(std::vector<Point> &pts) {
    std::vector<size_t> known;
    for (size_t i = 0; i < pts.size(); ++i)
        if (pts[i].hasEle) known.push_back(i);
    if (known.empty()) return false;

    for (size_t i = 0; i < pts.size(); ++i) {
        if (pts[i].hasEle) continue;
        // nearest known before / after
        const size_t *prev = nullptr;
        const size_t *next = nullptr;
        for (size_t k = 0; k < known.size(); ++k) {
            if (known[k] < i) prev = &known[k];
            if (known[k] > i) { next = &known[k]; break; }
        }
        if (!prev)      pts[i].ele = pts[*next].ele;
        else if (!next) pts[i].ele = pts[*prev].ele;
        else {
            double f = static_cast<double>(i - *prev) /
                       static_cast<double>(*next - *prev);
            pts[i].ele = pts[*prev].ele + f * (pts[*next].ele - pts[*prev].ele);
        }
        pts[i].hasEle = true;
    }
    return true;
}

std::vector<double> cumulativeDistance(const std::vector<Point> &pts) {
    std::vector<double> d(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i)
        d[i] = d[i - 1] + haversine(pts[i - 1].lat, pts[i - 1].lon,
                                    pts[i].lat, pts[i].lon);
    return d;
}

std::vector<double> smoothElevations(const std::vector<Point> &pts,
                                     const std::vector<double> &dist,
                                     double windowM) {
    size_t n = pts.size();
    std::vector<double> out(n, 0.0);
    double half = windowM / 2.0;
    size_t j0 = 0, j1 = 0;

    // The window is summed afresh at every point rather than kept as a
    // running total. A running total is faster but drifts: subtracting the
    // values that leave the window leaves a tiny rounding residue behind,
    // and on stretches that are almost level that residue is enough to flip
    // the sign of the gradient. With --no-flat, where every point has to be
    // either uphill or downhill, that shows up as whole sections changing
    // colour. Exactness matters more here than speed: the window holds only
    // a handful of points.
    for (size_t i = 0; i < n; ++i) {
        while (dist[j0] < dist[i] - half) ++j0;
        while (j1 + 1 < n && dist[j1 + 1] <= dist[i] + half) ++j1;
        if (j1 < i) j1 = i;

        double sum = 0.0;
        for (size_t k = j0; k <= j1; ++k) sum += pts[k].ele;
        out[i] = sum / static_cast<double>(j1 - j0 + 1);
    }
    return out;
}

std::vector<Klass> classify(const std::vector<Point> &pts,
                            const std::vector<double> &dist,
                            double windowM, double thresholdPct, bool useFlat) {
    std::vector<double> ele = smoothElevations(pts, dist, windowM);
    size_t n = pts.size();
    std::vector<Klass> out(n, Klass::Flat);
    double half = windowM / 2.0;

    for (size_t i = 0; i < n; ++i) {
        size_t a = i;
        while (a > 0 && dist[i] - dist[a] < half) --a;
        size_t b = i;
        while (b + 1 < n && dist[b] - dist[i] < half) ++b;

        double dd = dist[b] - dist[a];
        double grade = (dd < 1e-6) ? 0.0 : (ele[b] - ele[a]) / dd * 100.0;

        if (useFlat && std::fabs(grade) < thresholdPct) out[i] = Klass::Flat;
        else if (grade >= 0)                            out[i] = Klass::Uphill;
        else                                            out[i] = Klass::Downhill;
    }
    return out;
}

std::vector<Section> groupSections(const std::vector<Klass> &classes,
                                   const std::vector<double> &dist,
                                   double minlenM) {
    std::vector<Section> secs;
    if (classes.empty()) return secs;

    size_t start = 0;
    for (size_t i = 1; i <= classes.size(); ++i) {
        if (i == classes.size() || classes[i] != classes[start]) {
            secs.push_back({classes[start], start, i - 1});
            start = i;
        }
    }

    // absorb sections shorter than the minimum length
    bool changed = true;
    while (changed && secs.size() > 1) {
        changed = false;
        for (size_t k = 0; k < secs.size(); ++k) {
            double len = dist[secs[k].last] - dist[secs[k].first];
            if (len >= minlenM) continue;
            if (k == 0) {
                secs[1].first = secs[0].first;
                secs.erase(secs.begin());
            } else if (k == secs.size() - 1) {
                secs[k - 1].last = secs[k].last;
                secs.erase(secs.begin() + static_cast<long>(k));
            } else {
                double prevLen = dist[secs[k - 1].last] - dist[secs[k - 1].first];
                double nextLen = dist[secs[k + 1].last] - dist[secs[k + 1].first];
                if (prevLen >= nextLen) secs[k - 1].last = secs[k].last;
                else                    secs[k + 1].first = secs[k].first;
                secs.erase(secs.begin() + static_cast<long>(k));
            }
            changed = true;
            break;
        }
    }

    // join neighbours of the same class
    std::vector<Section> merged;
    for (const Section &s : secs) {
        if (!merged.empty() && merged.back().klass == s.klass)
            merged.back().last = s.last;
        else
            merged.push_back(s);
    }
    return merged;
}

// ------------------------------------------------------------------ main

bool valid3d(const Options &opt, std::string &error) {
    if (opt.viz3d.empty()) return true;
    static const char *kBy[] = {"none", "altitude", "fixed_height",
                                "shared_string_speed",
                                "map_widget_ant_heart_rate",
                                "map_widget_ant_bicycle_cadence",
                                "map_widget_ant_bicycle_power",
                                "shared_string_temperature"};
    bool ok = false;
    for (const char *v : kBy) if (opt.viz3d == v) ok = true;
    if (!ok) {
        error = "unknown --3d value \"" + opt.viz3d +
                "\". Use: altitude, fixed_height or none.";
        return false;
    }
    if (!opt.wall3d.empty()) {
        static const char *kWall[] = {"none", "solid", "downward_gradient",
                                      "upward_gradient", "altitude", "slope",
                                      "speed"};
        ok = false;
        for (const char *v : kWall) if (opt.wall3d == v) ok = true;
        if (!ok) {
            error = "unknown --3d-wall value \"" + opt.wall3d +
                    "\". Use: solid, upward_gradient, downward_gradient, "
                    "altitude, slope or speed.";
            return false;
        }
    }
    if (!opt.wallPos3d.empty() && opt.wallPos3d != "top" &&
        opt.wallPos3d != "bottom" && opt.wallPos3d != "top_bottom") {
        error = "unknown --3d-position value \"" + opt.wallPos3d +
                "\". Use: top, bottom or top_bottom.";
        return false;
    }
    for (const auto &p : {std::make_pair(opt.scale3d, "--3d-scale"),
                          std::make_pair(opt.height3d, "--3d-height")}) {
        if (p.first.empty()) continue;
        char *end = nullptr;
        double v = std::strtod(p.first.c_str(), &end);
        if (end == p.first.c_str() || *end != '\0' || v <= 0) {
            error = std::string(p.second) + " wants a positive number, got \"" +
                    p.first + "\".";
            return false;
        }
    }
    return true;
}

std::string asDouble(const std::string &n) {
    if (n.empty()) return n;
    if (n.find('.') != std::string::npos) return n;   // already has one
    char *end = nullptr;
    double v = std::strtod(n.c_str(), &end);
    if (end == n.c_str() || *end != '\0') return n;   // not a number, leave it
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

bool validMarkers(const Options &opt, std::string &error) {
    if (opt.splitType.empty()) return true;
    if (opt.splitType != "no_split" && opt.splitType != "distance" &&
        opt.splitType != "time") {
        error = "split type must be no_split, distance or time.";
        return false;
    }
    if (opt.splitType == "no_split") return true;
    if (opt.splitInterval.empty()) {
        error = "distance markers need an interval.";
        return false;
    }
    char *end = nullptr;
    double v = std::strtod(opt.splitInterval.c_str(), &end);
    if (end == opt.splitInterval.c_str() || *end != '\0' || v <= 0) {
        error = "marker interval wants a positive number, got \"" +
                opt.splitInterval + "\".";
        return false;
    }
    return true;
}

const char *const kNoDataColor = "#f2c200";

bool process(const std::string &inputPath, const Options &opt,
             Stats &stats, std::string &error, MergeSink *sink) {
    std::vector<Point> pts;
    if (!readPoints(inputPath, pts, error)) return false;
    if (pts.size() < 2) {
        error = "The track contains fewer than 2 points.";
        return false;
    }
    // No elevation anywhere in the file. There is nothing to classify, but
    // refusing the file outright used to drop it from a merged output, which
    // silently lost a track. Keep it, in a neutral colour, and let the caller
    // report why it is not coloured.
    const bool noEle = !fillMissingElevations(pts);
    stats.noElevation = noEle;

    std::string upHex, downHex, flatHex;
    if (!resolveColor(opt.uphill, upHex, error)) return false;
    if (!resolveColor(opt.downhill, downHex, error)) return false;
    if (!resolveColor(opt.flat, flatHex, error)) return false;
    if (!validWidth(opt.width, error)) return false;
    if (!valid3d(opt, error)) return false;
    if (!validMarkers(opt, error)) return false;

    std::vector<double> dist = cumulativeDistance(pts);
    std::vector<Section> secs;
    if (noEle) {
        Section whole;
        whole.first = 0;
        whole.last  = pts.size() - 1;
        whole.klass = Klass::Flat;   // a placeholder; colourFor overrides it
        secs.push_back(whole);
    } else {
        std::vector<Klass> classes =
            classify(pts, dist, opt.window, opt.threshold, !opt.noFlat);
        secs = groupSections(classes, dist, opt.minlen);
    }

    stats.points = pts.size();
    stats.sections = secs.size();
    stats.totalM = dist.back();
    stats.uphillM = stats.downhillM = stats.flatM = 0.0;
    if (noEle) stats.sections = 0;   // nothing was classified
    for (const Section &s : secs) {
        if (noEle) break;
        double len = dist[s.last] - dist[s.first];
        if (s.klass == Klass::Uphill)        stats.uphillM += len;
        else if (s.klass == Klass::Downhill) stats.downhillM += len;
        else                                 stats.flatM += len;
    }

    std::vector<std::string> wpts;
    if (opt.keepWaypoints) wpts = readWaypoints(inputPath);
    stats.waypoints = wpts.size();

    std::string base = stripExtension(inputPath);
    std::string niceName = baseName(base);
    if (!opt.outputDir.empty())
        base = joinPath(opt.outputDir, niceName);

    auto colorFor = [&](Klass k) {
        if (noEle) return std::string(kNoDataColor);
        if (k == Klass::Uphill) return upHex;
        if (k == Klass::Downhill) return downHex;
        return flatHex;
    };

    // Each section is extended by one point so it shares the junction with
    // the next one; without that overlap the connecting segment is drawn by
    // neither and a gap shows up at high zoom.
    auto lastIndex = [&](size_t sectionLast) {
        return std::min(sectionLast + 1, pts.size() - 1);
    };

    // ---- merging: hand the blocks to the caller instead of writing ----
    //
    // The track name is prefixed with the source file so six stages stay
    // tellable apart in OsmAnd's track list, where they now share one file.
    if (sink) {
        for (const std::string &w : wpts) {
            sink->waypoints += "  " + w + "\n";
            ++sink->waypointCount;
        }
        int i = 1;
        for (const Section &s : secs) {
            char num[16];
            std::snprintf(num, sizeof(num), "%03d ", i++);
            sink->tracks += trkBlock(niceName + (noEle
                                         ? std::string(" - no elevation data")
                                         : " - " + std::string(num) +
                                               klassLabel(s.klass)),
                                     colorFor(s.klass), pts, s.first,
                                     lastIndex(s.last), opt.width, opt.arrows);
            ++sink->trackCount;
        }
        ++sink->files;
        return true;
    }

    if (opt.splitFiles && noEle) {
        error = "No elevation data (<ele>) in this file, so there are no "
                "uphill/downhill classes to split into.";
        return false;
    }

    if (opt.splitFiles) {
        const Klass kinds[3] = {Klass::Uphill, Klass::Downhill, Klass::Flat};
        for (Klass k : kinds) {
            std::vector<const Section *> mine;
            for (const Section &s : secs)
                if (s.klass == k) mine.push_back(&s);
            if (mine.empty()) continue;

            std::string path = base + "_" + klassSlug(k) + ".gpx";
            std::ofstream f;
            openOut(f, path);
            if (!f) { error = "Cannot write: " + path; return false; }
            f << xmlHeader(niceName + " - " + klassLabel(k));
            for (const std::string &w : wpts) f << "  " << w << "\n";
            int i = 1;
            for (const Section *s : mine) {
                std::ostringstream nm;
                nm << klassLabel(k) << " " << i++;
                f << trkBlock(nm.str(), colorFor(k), pts, s->first,
                              lastIndex(s->last), opt.width, opt.arrows);
            }
            f << gpxLevelExtensions(opt.arrows, opt);
            f << "</gpx>\n";
            if (!f) { error = "Failed while writing: " + path; return false; }
            stats.written.push_back(path);
        }
        if (stats.written.empty()) {
            error = "Nothing to write.";
            return false;
        }
        return true;
    }

    std::string outPath = opt.output.empty()
                              ? base + opt.suffix + ".gpx"
                              : opt.output;
    std::ofstream f;
    openOut(f, outPath);
    if (!f) { error = "Cannot write: " + outPath; return false; }
    f << xmlHeader(niceName);
    for (const std::string &w : wpts) f << "  " << w << "\n";

    // Single-track mode: everything inside one <trk>, each coloured stretch a
    // separate <trkseg>. Sites that treat a GPX as exactly one activity
    // (Strava is the usual one) count the <trk> elements, so the multi-track
    // layout we use for OsmAnd can come in as several activities, or only the
    // first one. This layout imports as a single ride everywhere.
    //
    int i = 1;
    for (const Section &s : secs) {
        char num[16];
        std::snprintf(num, sizeof(num), "%03d ", i++);
        f << trkBlock(noEle ? niceName + " - no elevation data"
                            : std::string(num) + klassLabel(s.klass),
                      colorFor(s.klass),
                      pts, s.first, lastIndex(s.last), opt.width, opt.arrows);
    }
    f << gpxLevelExtensions(opt.arrows, opt);
    f << "</gpx>\n";
    if (!f) { error = "Failed while writing: " + outPath; return false; }
    stats.written.push_back(outPath);
    return true;
}

bool writeMerged(const MergeSink &sink, const std::string &path,
                 const Options &opt, std::string &error) {
    if (sink.trackCount == 0) {
        error = "Nothing to write.";
        return false;
    }
    std::ofstream f;
    openOut(f, path);
    if (!f) { error = "Cannot write: " + path; return false; }
    f << xmlHeader(stripExtension(baseName(path)));
    // GPX 1.1 fixes the order: every <wpt> first, then the tracks, then the
    // file-level <extensions>.
    f << sink.waypoints;
    f << sink.tracks;
    f << gpxLevelExtensions(opt.arrows, opt);
    f << "</gpx>\n";
    if (!f) { error = "Failed while writing: " + path; return false; }
    return true;
}

}  // namespace slope
