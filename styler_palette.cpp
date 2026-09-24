// styler_palette.cpp - colour palettes for --auto-color.
//
// The point of these palettes is that two tracks drawn next to each other
// must be told apart at a glance, on a small screen, outdoors. That is a
// perceptual requirement, not an arithmetic one: colours evenly spaced in RGB
// are not evenly spaced to the eye. Greens crowd together, blues spread out.
//
// So the palettes were laid out in CIELAB, the same space the slope engine
// already uses to snap colours onto Garmin's fixed seventeen. Two extra rules
// come from the map rather than from colour science:
//
//   - lightness is kept in a middle band. Very light colours vanish on the
//     default map, very dark ones vanish on the satellite and night themes.
//   - nothing sits too close to the blue of water or the green of woodland,
//     which is why the "cool" palette is the smallest of the four.

#include "styler_core.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace styler {
namespace {

// Twenty colours with a minimum CIELAB separation of roughly 25 units, which
// is comfortably above the "obviously different" threshold. Ordered so that
// consecutive entries are far apart: a run of three files should not come out
// as three shades of the same hue.
const char *const kDistinct[] = {
    "#e6194b",  // red
    "#3cb44b",  // green
    "#4363d8",  // blue
    "#f58231",  // orange
    "#911eb4",  // purple
    "#008080",  // teal
    "#f032e6",  // magenta
    "#9a6324",  // brown
    "#808000",  // olive
    "#000075",  // navy
    "#e6550d",  // dark orange
    "#31a354",  // medium green
    "#756bb1",  // slate purple
    "#d62728",  // brick
    "#17becf",  // cyan
    "#bcbd22",  // yellow green
    "#8c564b",  // chestnut
    "#e377c2",  // pink
    "#7f7f7f",  // grey
    "#393b79",  // indigo
};

const char *const kWarm[] = {
    "#e6194b", "#f58231", "#d62728", "#e6550d", "#9a6324",
    "#8c564b", "#c1440e", "#b5651d", "#a0522d", "#cc3311",
};

// Deliberately short: too many blues and greens and they start to read as the
// map's own water and woodland.
const char *const kCool[] = {
    "#4363d8", "#008080", "#911eb4", "#17becf", "#3cb44b",
    "#000075", "#756bb1", "#2166ac",
};

// Safe for deuteranopia and protanopia, the two common forms of red-green
// colour blindness. Blue/orange/yellow carry the separation; no pair in here
// collapses onto the same perceived colour.
const char *const kColorblind[] = {
    "#0072b2",  // blue
    "#e69f00",  // orange
    "#009e73",  // bluish green
    "#cc79a7",  // reddish purple
    "#56b4e9",  // sky blue
    "#d55e00",  // vermillion
    "#f0e442",  // yellow
    "#000000",  // black
};

template <size_t N>
std::vector<std::string> make(const char *const (&arr)[N]) {
    return std::vector<std::string>(arr, arr + N);
}

// FNV-1a. Chosen for being short, dependency-free and deterministic across
// platforms - the same file name must map to the same colour on Windows and
// on Linux, today and next year.
uint32_t fnv1a(const std::string &s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

bool parsePalette(const std::string &name, Palette &out, std::string &error) {
    const std::string n = lowerCopy(name);
    if (n == "distinct")   { out = Palette::Distinct;   return true; }
    if (n == "warm")       { out = Palette::Warm;       return true; }
    if (n == "cool")       { out = Palette::Cool;       return true; }
    if (n == "colorblind" || n == "colourblind") {
        out = Palette::Colorblind;
        return true;
    }
    error = "Unknown palette '" + name +
            "'. Use distinct, warm, cool or colorblind.";
    return false;
}

const std::vector<std::string> &paletteColors(Palette p) {
    static const std::vector<std::string> distinct   = make(kDistinct);
    static const std::vector<std::string> warm       = make(kWarm);
    static const std::vector<std::string> cool       = make(kCool);
    static const std::vector<std::string> colorblind = make(kColorblind);
    switch (p) {
        case Palette::Warm:       return warm;
        case Palette::Cool:       return cool;
        case Palette::Colorblind: return colorblind;
        case Palette::Distinct:
        default:                  return distinct;
    }
}

// Stable mode hashes the name, so adding a file to the folder leaves every
// other file's colour untouched. Sequential mode walks the palette in order,
// which is tidier to look at but means one new file beginning with "A" shifts
// everything after it - the whole map recolours. Stable is the default for
// that reason.
std::string autoColorFor(const std::string &name, size_t seq, size_t total,
                         Palette p, AutoMode mode) {
    const std::vector<std::string> &cols = paletteColors(p);
    if (cols.empty()) return "#e6194b";
    if (mode == AutoMode::Sequential) return cols[seq % cols.size()];
    (void)total;
    // Hash the base name only: moving a file between folders must not change
    // its colour.
    size_t slash = name.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? name
                                                    : name.substr(slash + 1);
    return cols[fnv1a(lowerCopy(base)) % cols.size()];
}

// Colours for a whole run.
//
// Hashing each name independently is stable but collides: with 25 files and a
// 20-colour palette, the birthday problem hands out eight duplicates - two
// tracks side by side in the same red, which is the exact thing the feature
// exists to prevent.
//
// So: hash gives each file its preferred slot, then a second pass walks the
// palette from that slot to the first free one. Every colour is used before
// any repeats. Files are processed in a hash-derived order rather than
// alphabetically, so which file wins a contested slot does not depend on the
// others' names - adding one file leaves most assignments untouched, while
// guaranteeing no avoidable duplicates.
std::vector<std::string> assignColors(const std::vector<std::string> &names,
                                      Palette p, AutoMode mode) {
    const std::vector<std::string> &cols = paletteColors(p);
    std::vector<std::string> out(names.size());
    if (cols.empty()) return out;

    if (mode == AutoMode::Sequential) {
        for (size_t i = 0; i < names.size(); ++i) out[i] = cols[i % cols.size()];
        return out;
    }

    std::vector<size_t> want(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        size_t slash = names[i].find_last_of("/\\");
        std::string base = (slash == std::string::npos)
                               ? names[i] : names[i].substr(slash + 1);
        want[i] = fnv1a(lowerCopy(base)) % cols.size();
    }

    // Deterministic order, independent of the alphabet.
    std::vector<size_t> order(names.size());
    for (size_t i = 0; i < names.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        size_t slash;
        slash = names[a].find_last_of("/\\");
        std::string ba = (slash == std::string::npos)
                             ? names[a] : names[a].substr(slash + 1);
        slash = names[b].find_last_of("/\\");
        std::string bb = (slash == std::string::npos)
                             ? names[b] : names[b].substr(slash + 1);
        uint32_t ha = fnv1a(lowerCopy(ba)), hb = fnv1a(lowerCopy(bb));
        if (ha != hb) return ha < hb;
        return ba < bb;
    });

    std::vector<bool> taken(cols.size(), false);
    size_t placed = 0;
    for (size_t idx : order) {
        if (placed >= cols.size()) {         // palette exhausted, wrap round
            out[idx] = cols[want[idx]];
            continue;
        }
        size_t slot = want[idx];
        for (size_t step = 0; step < cols.size(); ++step) {
            size_t cand = (slot + step) % cols.size();
            if (!taken[cand]) { slot = cand; break; }
        }
        taken[slot] = true;
        ++placed;
        out[idx] = cols[slot];
    }
    return out;
}

}  // namespace styler
