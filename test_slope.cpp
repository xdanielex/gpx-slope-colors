// test_slope.cpp - self contained test suite for the slope engine.
//
// Build:  g++ -std=c++17 -O2 -o tests test_slope.cpp slope_core.cpp
// Run:    ./tests          (or simply: make test)

#include "slope_core.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace slope;

namespace {

int g_pass = 0, g_fail = 0;
std::string g_group;

void group(const std::string &name) {
    g_group = name;
    std::cout << "\n" << name << "\n";
}

void check(bool ok, const std::string &what) {
    if (ok) {
        ++g_pass;
        std::cout << "  ok    " << what << "\n";
    } else {
        ++g_fail;
        std::cout << "  FAIL  " << what << "\n";
    }
}

void checkNear(double got, double want, double tol, const std::string &what) {
    bool ok = std::fabs(got - want) <= tol;
    std::ostringstream m;
    m << what << "  (got " << got << ", want " << want << ")";
    check(ok, ok ? what : m.str());
}

// ------------------------------------------------------------- fixtures

std::string tmpDir() {
    const char *t = std::getenv("TMPDIR");
    return t ? std::string(t) : std::string("/tmp");
}

std::string writeTemp(const std::string &name, const std::string &body) {
    std::string path = tmpDir() + "/gsc_test_" + name;
    std::ofstream f(path.c_str(), std::ios::binary);
    f << body;
    return path;
}

std::string readAll(const std::string &path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A synthetic track: `n` points along a meridian, `step` metres apart,
// with elevation given by a callback.
template <typename F>
std::string makeGpx(const std::string &name, int n, double stepM, F ele,
                    bool withEle = true, const std::string &extra = "") {
    const double degPerM = 1.0 / 111320.0;
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<gpx version=\"1.1\" creator=\"test\" "
      << "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
      << extra << "  <trk><name>t</name><trkseg>\n";
    for (int i = 0; i < n; ++i) {
        x << "    <trkpt lat=\"" << (45.0 + i * stepM * degPerM)
          << "\" lon=\"9.0\">";
        if (withEle) x << "<ele>" << ele(i) << "</ele>";
        x << "</trkpt>\n";
    }
    x << "  </trkseg></trk>\n</gpx>\n";
    return writeTemp(name, x.str());
}

int countOccurrences(const std::string &hay, const std::string &needle) {
    int n = 0;
    size_t p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
    return n;
}

// ---------------------------------------------------------------- tests

void testColors() {
    group("colours");
    std::string out, err;

    check(resolveColor("red", out, err) && out == "#e01b1b", "name: red");
    check(resolveColor("rosso", out, err) && out == "#e01b1b", "italian alias: rosso");
    check(resolveColor("VERDE", out, err) && out == "#00a03c", "case insensitive: VERDE");
    check(resolveColor("light blue", out, err) && out == "#00bfff", "space tolerated: light blue");
    check(resolveColor("light-green", out, err) && out == "#5ad45f", "dash tolerated: light-green");
    check(resolveColor("#ff8800", out, err) && out == "#ff8800", "hex with hash");
    check(resolveColor("ff8800", out, err) && out == "#ff8800", "hex without hash");
    check(resolveColor("#FF8800", out, err) && out == "#ff8800", "hex is lowercased");
    check(resolveColor("grey", out, err) && resolveColor("gray", out, err),
          "both grey and gray accepted");

    check(!resolveColor("nosuchcolour", out, err), "unknown name rejected");
    check(!resolveColor("#12345", out, err), "5 hex digits rejected");
    check(!resolveColor("#gggggg", out, err), "non hex digits rejected");
    check(!resolveColor("", out, err), "empty value rejected");
    check(!err.empty(), "an error message is produced");
    check(palette().size() == 19, "palette has 19 colours");
}

void testWidth() {
    group("line width");
    std::string err;
    check(validWidth("thin", err), "thin");
    check(validWidth("medium", err), "medium");
    check(validWidth("bold", err), "bold");
    check(validWidth("1", err), "1");
    check(validWidth("24", err), "24 (the default, maximum)");
    check(!validWidth("0", err), "0 rejected");
    check(!validWidth("25", err), "25 rejected");
    check(!validWidth("fat", err), "unknown word rejected");
    check(!validWidth("", err), "empty rejected");
}

void testHaversine() {
    group("distance");
    // One degree of latitude is about 111.2 km.
    checkNear(haversine(45.0, 9.0, 46.0, 9.0), 111195.0, 200.0, "1 degree of latitude");
    checkNear(haversine(45.0, 9.0, 45.0, 9.0), 0.0, 1e-9, "same point is zero");
    // Symmetry.
    checkNear(haversine(45.0, 9.0, 45.5, 9.5),
              haversine(45.5, 9.5, 45.0, 9.0), 1e-6, "symmetric");
}

void testPaths() {
    group("paths");
    check(baseName("/a/b/c.gpx") == "c.gpx", "baseName");
    check(dirName("/a/b/c.gpx") == "/a/b", "dirName");
    check(stripExtension("/a/b/c.gpx") == "/a/b/c", "stripExtension");
    check(stripExtension("noext") == "noext", "stripExtension without a dot");
    check(defaultOutputPath("/a/ride.gpx", "_slope") == "/a/ride_slope.gpx",
          "default output path");
    check(defaultOutputPath("/a/ride.gpx", "-coloured") == "/a/ride-coloured.gpx",
          "custom suffix is honoured");
    check(defaultOutputPath("/a/ride.gpx", "") == "/a/ride.gpx",
          "empty suffix overwrites the name");
}

void testReadAndInterpolate() {
    group("reading and elevation");
    std::string err;

    std::string p = makeGpx("read.gpx", 10, 100.0, [](int i) { return 100 + i * 10; });
    std::vector<Point> pts;
    check(readPoints(p, pts, err), "a simple file is read");
    check(pts.size() == 10, "10 points found");
    checkNear(pts[0].lat, 45.0, 1e-9, "first latitude");
    checkNear(pts[3].ele, 130.0, 1e-9, "elevation is parsed");

    std::vector<Point> missing;
    check(readPoints(p, missing, err), "reread for the gap test");
    missing[2].hasEle = false;
    missing[3].hasEle = false;
    check(fillMissingElevations(missing), "gaps can be filled");
    checkNear(missing[2].ele, 120.0, 1e-6, "interpolated middle value");
    checkNear(missing[3].ele, 130.0, 1e-6, "second interpolated value");

    // Leading and trailing gaps copy the nearest known value.
    std::vector<Point> edges;
    readPoints(p, edges, err);
    edges[0].hasEle = false;
    edges[9].hasEle = false;
    check(fillMissingElevations(edges), "edge gaps can be filled");
    checkNear(edges[0].ele, 110.0, 1e-6, "leading gap copies the first known value");
    checkNear(edges[9].ele, 180.0, 1e-6, "trailing gap copies the last known value");

    // No elevation at all is a hard failure.
    std::string flat = makeGpx("noele.gpx", 5, 100.0,
                               [](int) { return 0; }, false);
    std::vector<Point> none;
    readPoints(flat, none, err);
    check(!fillMissingElevations(none), "a track with no elevation is rejected");

    // Missing file.
    std::vector<Point> nope;
    check(!readPoints(tmpDir() + "/does_not_exist_xyz.gpx", nope, err),
          "a missing file is an error");
    check(!err.empty(), "the error explains why");
}

void testClassification() {
    group("classification");
    std::string err;

    // A steady 10% climb: 100 points, 50 m apart, +5 m each.
    std::string up = makeGpx("up.gpx", 100, 50.0, [](int i) { return 100 + i * 5; });
    std::vector<Point> pts;
    readPoints(up, pts, err);
    fillMissingElevations(pts);
    std::vector<double> d = cumulativeDistance(pts);
    checkNear(d.back(), 4950.0, 60.0, "cumulative distance of the climb");

    std::vector<Klass> cls = classify(pts, d, 50.0, 1.5, true);
    int uphill = 0;
    for (Klass k : cls) if (k == Klass::Uphill) ++uphill;
    check(uphill > 90, "a 10% climb is almost entirely uphill");

    // The same track reversed must be downhill: direction of travel matters.
    std::vector<Point> rev(pts.rbegin(), pts.rend());
    std::vector<double> dr = cumulativeDistance(rev);
    std::vector<Klass> clsRev = classify(rev, dr, 50.0, 1.5, true);
    int downhill = 0;
    for (Klass k : clsRev) if (k == Klass::Downhill) ++downhill;
    check(downhill > 90, "the same track reversed is downhill");

    // A level track is flat.
    std::string lvl = makeGpx("level.gpx", 60, 50.0, [](int) { return 200; });
    std::vector<Point> lp;
    readPoints(lvl, lp, err);
    fillMissingElevations(lp);
    std::vector<double> ld = cumulativeDistance(lp);
    std::vector<Klass> lc = classify(lp, ld, 50.0, 1.5, true);
    int flat = 0;
    for (Klass k : lc) if (k == Klass::Flat) ++flat;
    check(flat == (int)lc.size(), "a level track is entirely flat");

    // With --no-flat nothing is ever flat.
    std::vector<Klass> nf = classify(lp, ld, 50.0, 1.5, false);
    int anyFlat = 0;
    for (Klass k : nf) if (k == Klass::Flat) ++anyFlat;
    check(anyFlat == 0, "no-flat removes the flat class");

    // A higher threshold turns a gentle slope into flat.
    std::string gentle = makeGpx("gentle.gpx", 80, 50.0,
                                 [](int i) { return 100 + i * 1.0; });  // 2%
    std::vector<Point> gp;
    readPoints(gentle, gp, err);
    fillMissingElevations(gp);
    std::vector<double> gd = cumulativeDistance(gp);
    int steep = 0, calm = 0;
    for (Klass k : classify(gp, gd, 50.0, 1.5, true)) if (k == Klass::Uphill) ++steep;
    for (Klass k : classify(gp, gd, 50.0, 5.0, true)) if (k == Klass::Uphill) ++calm;
    check(steep > calm, "a higher threshold classifies less as uphill");
}

void testSections() {
    group("sections");
    std::vector<double> d;
    for (int i = 0; i < 100; ++i) d.push_back(i * 50.0);

    // Uphill, a short downhill blip, then uphill again.
    std::vector<Klass> cls(100, Klass::Uphill);
    cls[50] = Klass::Downhill;

    std::vector<Section> keep = groupSections(cls, d, 0.0);
    check(keep.size() == 3, "with minlen 0 the blip survives");

    std::vector<Section> absorb = groupSections(cls, d, 500.0);
    check(absorb.size() == 1, "with minlen 500 m the blip is absorbed");

    // Sections must cover every point with no gaps.
    std::vector<Klass> mixed(60, Klass::Uphill);
    for (int i = 20; i < 40; ++i) mixed[i] = Klass::Downhill;
    std::vector<Section> secs = groupSections(mixed, d, 0.0);
    check(secs.size() == 3, "three sections");
    check(secs.front().first == 0, "first section starts at 0");
    check(secs.back().last == 59, "last section ends at the last point");
    bool contiguous = true;
    for (size_t i = 1; i < secs.size(); ++i)
        if (secs[i].first != secs[i - 1].last + 1) contiguous = false;
    check(contiguous, "sections are contiguous");
}

void testProcess() {
    group("end to end");
    std::string err;

    // Up then down: two sections, two colours.
    std::string path = makeGpx("hill.gpx", 200, 50.0, [](int i) {
        return i < 100 ? 100 + i * 5 : 100 + (200 - i) * 5;
    });

    Options opt;
    opt.output = tmpDir() + "/gsc_out.gpx";
    Stats st;
    check(process(path, opt, st, err), "a hill is processed");
    check(st.points == 200, "200 points");
    check(st.sections >= 2, "at least two sections");
    checkNear(st.totalM, 9950.0, 150.0, "total length");
    check(st.uphillM > 4000 && st.downhillM > 4000, "up and down are balanced");
    check(st.written.size() == 1, "one file written");

    std::string xml = readAll(opt.output);
    check(xml.find("<?xml") == 0, "output starts with the xml declaration");
    check(xml.find("xmlns:osmand=\"https://osmand.net\"") != std::string::npos,
          "the osmand namespace is declared");
    check(xml.find("<osmand:color>#e01b1b</osmand:color>") != std::string::npos,
          "uphill colour present");
    check(xml.find("<osmand:color>#00a03c</osmand:color>") != std::string::npos,
          "downhill colour present");
    check(xml.find("<osmand:width>24</osmand:width>") != std::string::npos,
          "default width is 24");
    check(xml.find("<osmand:show_arrows>true</osmand:show_arrows>") != std::string::npos,
          "arrows on by default");
    check(countOccurrences(xml, "<trk>") == (int)st.sections,
          "one trk block per section");
    check(countOccurrences(xml, "<trk>") == countOccurrences(xml, "</trk>"),
          "every trk is closed");

    // Custom colours, width and no arrows.
    Options c;
    c.uphill = "blue";
    c.downhill = "orange";
    c.width = "thin";
    c.arrows = false;
    c.output = tmpDir() + "/gsc_custom.gpx";
    Stats cs;
    check(process(path, c, cs, err), "custom options are accepted");
    std::string cx = readAll(c.output);
    check(cx.find("<osmand:color>#0a7ef0</osmand:color>") != std::string::npos,
          "named colour blue resolved");
    check(cx.find("<osmand:color>#ff7a00</osmand:color>") != std::string::npos,
          "named colour orange resolved");
    check(cx.find("<osmand:width>thin</osmand:width>") != std::string::npos,
          "width thin written through");
    check(cx.find("<osmand:show_arrows>false</osmand:show_arrows>") != std::string::npos,
          "arrows switched off when asked");
    check(cx.find("<osmand:show_arrows>true</osmand:show_arrows>") == std::string::npos,
          "no track still asks for arrows");

    // The suffix drives the automatic name.
    Options s;
    s.suffix = "-coloured";
    Stats ss;
    check(process(path, s, ss, err), "suffix option runs");
    check(ss.written.size() == 1 &&
          ss.written[0].find("-coloured.gpx") != std::string::npos,
          "the suffix appears in the file name");

    // --split-files makes one file per class and ignores -o.
    Options sp;
    sp.splitFiles = true;
    sp.output = tmpDir() + "/ignored_on_purpose.gpx";
    Stats sps;
    check(process(path, sp, sps, err), "split mode runs");
    check(sps.written.size() >= 2, "split mode writes several files");
    bool ignored = true;
    for (const std::string &w : sps.written)
        if (w.find("ignored_on_purpose") != std::string::npos) ignored = false;
    check(ignored, "split mode ignores --output");

    // Waypoints are only copied when asked.
    std::string wp = makeGpx("wpt.gpx", 120, 50.0,
                             [](int i) { return 100 + i * 3; }, true,
                             "  <wpt lat=\"45.0\" lon=\"9.0\"><name>Start</name></wpt>\n");
    Options nw;
    nw.output = tmpDir() + "/gsc_nowpt.gpx";
    Stats nws;
    process(wp, nw, nws, err);
    check(nws.waypoints == 0, "waypoints dropped by default");
    check(readAll(nw.output).find("<wpt") == std::string::npos,
          "no wpt in the output");

    Options kw;
    kw.keepWaypoints = true;
    kw.output = tmpDir() + "/gsc_wpt.gpx";
    Stats kws;
    process(wp, kw, kws, err);
    check(kws.waypoints == 1, "one waypoint kept");
    std::string kx = readAll(kw.output);
    check(kx.find("<name>Start</name>") != std::string::npos,
          "the waypoint name survives");
    check(kx.find("<wpt") < kx.find("<trk>"), "waypoints come before the tracks");

    // Bad input is reported, not crashed on.
    Options bad;
    Stats bs;
    check(!process(tmpDir() + "/no_such_file_abc.gpx", bad, bs, err),
          "a missing input fails cleanly");

    std::string empty = writeTemp("empty.gpx",
        "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>"
        "</trkseg></trk></gpx>");
    Stats es;
    check(!process(empty, bad, es, err), "an empty track fails cleanly");
    check(!err.empty(), "with an explanation");

    std::string one = makeGpx("one.gpx", 1, 50.0, [](int) { return 100; });
    Stats os;
    check(!process(one, bad, os, err), "a single point fails cleanly");
}

void testGarminColours() {
    group("garmin colour extension");
    std::string err;
    std::string path = makeGpx("garmin.gpx", 200, 50.0, [](int i) {
        return i < 100 ? 100 + i * 5 : 100 + (200 - i) * 5;
    });

    Options opt;
    opt.output = tmpDir() + "/gsc_garmin.gpx";
    Stats st;
    check(process(path, opt, st, err), "file with garmin tags is written");
    std::string xml = readAll(opt.output);

    check(xml.find("xmlns:gpxx=\"http://www.garmin.com/xmlschemas/"
                   "GpxExtensions/v3\"") != std::string::npos,
          "gpxx namespace declared");
    check(xml.find("xmlns:gpxtrx=") != std::string::npos,
          "gpxtrx namespace declared");
    check(countOccurrences(xml, "<gpxx:TrackExtension>") == (int)st.sections,
          "one gpxx block per section");
    check(countOccurrences(xml, "<gpxtrx:TrackExtension>") == (int)st.sections,
          "one gpxtrx block per section (handhelds read this one)");

    // The defaults must land on sensible Garmin names.
    check(xml.find("<gpxx:DisplayColor>Red</gpxx:DisplayColor>") != std::string::npos,
          "uphill red -> Red");
    check(xml.find("<gpxx:DisplayColor>DarkGreen</gpxx:DisplayColor>") != std::string::npos,
          "downhill green -> DarkGreen");

    // Both prefixes must always agree.
    std::vector<std::string> a, b;
    size_t p = 0;
    while ((p = xml.find("<gpxx:DisplayColor>", p)) != std::string::npos) {
        size_t s = p + 19, e = xml.find('<', s);
        a.push_back(xml.substr(s, e - s));
        p = e;
    }
    p = 0;
    while ((p = xml.find("<gpxtrx:DisplayColor>", p)) != std::string::npos) {
        size_t s = p + 21, e = xml.find('<', s);
        b.push_back(xml.substr(s, e - s));
        p = e;
    }
    check(!a.empty() && a == b, "gpxx and gpxtrx always carry the same colour");

    // Every custom colour must map to a name Garmin's schema actually allows.
    const char *allowed[] = {"Black", "DarkRed", "DarkGreen", "DarkYellow",
                             "DarkBlue", "DarkMagenta", "DarkCyan", "LightGray",
                             "DarkGray", "Red", "Green", "Yellow", "Blue",
                             "Magenta", "Cyan", "White"};
    bool allValid = true;
    for (const NamedColor &nc : palette()) {
        Options c;
        c.uphill = nc.hex;
        c.output = tmpDir() + "/gsc_gmap.gpx";
        Stats cs;
        if (!process(path, c, cs, err)) { allValid = false; break; }
        std::string cx = readAll(c.output);
        size_t s = cx.find("<gpxx:DisplayColor>") + 19;
        std::string got = cx.substr(s, cx.find('<', s) - s);
        bool ok = false;
        for (const char *w : allowed) if (got == w) ok = true;
        if (!ok) { allValid = false; break; }
    }
    check(allValid, "all 19 palette colours map to a legal Garmin name");
}

void testSensorDataPreserved() {
    group("sensor data survives");
    std::string err;

    // A track with heart rate and cadence, the way a bike computer writes it.
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<gpx version=\"1.1\" creator=\"test\" "
      << "xmlns=\"http://www.topografix.com/GPX/1/1\" "
      << "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/"
         "TrackPointExtension/v1\">\n"
      << "  <trk><name>t</name><trkseg>\n";
    for (int i = 0; i < 200; ++i) {
        double lat = 45.0 + i * 50.0 / 111320.0;
        double ele = i < 100 ? 100 + i * 5 : 100 + (200 - i) * 5;
        x << "    <trkpt lat=\"" << lat << "\" lon=\"9.0\">"
          << "<ele>" << ele << "</ele>"
          << "<extensions><gpxtpx:TrackPointExtension>"
          << "<gpxtpx:hr>" << (140 + i % 20) << "</gpxtpx:hr>"
          << "<gpxtpx:cad>" << (85 + i % 10) << "</gpxtpx:cad>"
          << "</gpxtpx:TrackPointExtension></extensions></trkpt>\n";
    }
    x << "  </trkseg></trk>\n</gpx>\n";
    std::string path = writeTemp("sensors.gpx", x.str());

    // Coloured (multi-track) output.
    Options opt;
    opt.output = tmpDir() + "/gsc_sensors.gpx";
    Stats st;
    check(process(path, opt, st, err), "track with sensor data is processed");
    std::string xml = readAll(opt.output);
    int pts = countOccurrences(xml, "<trkpt");
    check(countOccurrences(xml, "<gpxtpx:hr>") == pts,
          "every point keeps its heart rate");
    check(countOccurrences(xml, "<gpxtpx:cad>") == pts,
          "every point keeps its cadence");
    check(xml.find("xmlns:gpxtpx=") != std::string::npos,
          "the gpxtpx namespace is declared");

    // Split-files output must keep the sensor data too.
    Options sp;
    sp.splitFiles = true;
    sp.outputDir = tmpDir();
    Stats ss;
    check(process(path, sp, ss, err), "split mode runs on it");
    int totalHr = 0;
    for (const std::string &w : ss.written)
        totalHr += countOccurrences(readAll(w), "<gpxtpx:hr>");
    check(totalHr > 0, "split files keep heart rate too");

    // A point with no extensions must not gain an empty block.
    std::string plain = makeGpx("plain.gpx", 120, 50.0,
                                [](int i) { return 100 + i * 3; });
    Options p2;
    p2.output = tmpDir() + "/gsc_plain.gpx";
    Stats ps;
    process(plain, p2, ps, err);
    std::string px = readAll(p2.output);
    check(px.find("<extensions></extensions>") == std::string::npos &&
          px.find("<trkpt><extensions>") == std::string::npos,
          "points without extensions stay clean");

    // Nested and self-closing extensions must not break the scanner.
    std::string odd =
        "<?xml version=\"1.0\"?><gpx version=\"1.1\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\"><trk><trkseg>"
        "<trkpt lat=\"45.0\" lon=\"9.0\"><ele>100</ele>"
        "<extensions/></trkpt>"
        "<trkpt lat=\"45.001\" lon=\"9.0\"><ele>110</ele>"
        "<extensions><a><extensions>deep</extensions></a></extensions></trkpt>"
        "<trkpt lat=\"45.002\" lon=\"9.0\"><ele>120</ele></trkpt>"
        "</trkseg></trk></gpx>";
    std::string oddPath = writeTemp("odd.gpx", odd);
    std::vector<Point> op;
    check(readPoints(oddPath, op, err), "odd extensions parse");
    check(op.size() == 3, "all three points found");
    check(op[1].extra.find("deep") != std::string::npos,
          "nested extensions captured whole");
}

void testGapsBetweenSections() {
    group("no gaps between sections");
    std::string err;
    std::string path = makeGpx("gap.gpx", 200, 50.0, [](int i) {
        return i < 100 ? 100 + i * 5 : 100 + (200 - i) * 5;
    });
    Options opt;
    opt.output = tmpDir() + "/gsc_gap.gpx";
    Stats st;
    process(path, opt, st, err);

    // Every consecutive pair of sections must share a point, otherwise the
    // map shows a hole between two colours.
    std::string xml = readAll(opt.output);
    std::vector<std::string> lastOf, firstOf;
    size_t pos = 0;
    while ((pos = xml.find("<trkseg>", pos)) != std::string::npos) {
        size_t end = xml.find("</trkseg>", pos);
        std::string seg = xml.substr(pos, end - pos);
        size_t a = seg.find("<trkpt");
        size_t b = seg.rfind("<trkpt");
        firstOf.push_back(seg.substr(a, seg.find('>', a) - a));
        lastOf.push_back(seg.substr(b, seg.find('>', b) - b));
        pos = end;
    }
    check(firstOf.size() == st.sections, "one segment per section");
    bool shared = true;
    for (size_t i = 1; i < firstOf.size(); ++i)
        if (firstOf[i] != lastOf[i - 1]) shared = false;
    check(shared, "each section starts where the previous one ended");
}

}  // namespace

int main() {
    std::cout << "gpx-slope-colors " << VERSION << " - test suite\n";

    testColors();
    testWidth();
    testHaversine();
    testPaths();
    testReadAndInterpolate();
    testClassification();
    testSections();
    testProcess();
    testGarminColours();
    testSensorDataPreserved();
    testGapsBetweenSections();

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
