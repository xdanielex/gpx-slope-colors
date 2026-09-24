// main_gui.cpp - native Win32 front end. No runtime, no dependencies.
//
// Drag GPX files onto the window, set every option, press Convert.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <richedit.h>

#include <algorithm>
#include <string>
#include <vector>

#include "slope_core.hpp"
#include "styler_core.hpp"

using namespace slope;

// ----------------------------------------------------------------- ids
enum {
    ID_LIST = 1001, ID_ADD, ID_REMOVE, ID_CLEAR,
    ID_UP_SWATCH, ID_UP_EDIT, ID_DOWN_SWATCH, ID_DOWN_EDIT,
    ID_FLAT_SWATCH, ID_FLAT_EDIT, ID_NOFLAT,
    ID_PRESET, ID_THRESHOLD, ID_WINDOW, ID_MINLEN,
    ID_WIDTH, ID_ARROWS, ID_WAYPOINTS,
    ID_SUFFIX, ID_SAMEDIR, ID_OUTDIR, ID_BROWSE, ID_SPLIT,
    ID_CONVERT, ID_RESET, ID_LOG, ID_PROGRESS, ID_STATUS,
    ID_MERGE, ID_MERGENAME, ID_MARKERS, ID_3D, ID_3DSCALE,

    // Batch styler tab
    ID_TABS = 1100,
    ID_ST_AUTO, ID_ST_ONE, ID_ST_SWATCH, ID_ST_EDIT, ID_ST_PALETTE,
    ID_ST_MERGE, ID_ST_MERGENAME, ID_ST_3D, ID_ST_3DSCALE,
    ID_ST_PERTRACK, ID_ST_WIDTH, ID_ST_ARROWS, ID_ST_STARTFIN,
    ID_ST_SPLIT, ID_ST_COLORING,
    ID_ST_GROUPS, ID_ST_ACTIVITY, ID_ST_SUFFIX, ID_ST_SAMEDIR,
    ID_ST_OUTDIR, ID_ST_BROWSE, ID_ST_DRYRUN,
    ID_ST_APPLY
};

// Which tab is showing. The two modes do genuinely different things - one
// recolours by gradient, the other only labels files - so they get separate
// pages rather than a pile of options that are mutually irrelevant.
enum { TAB_SLOPE = 0, TAB_STYLE = 1 };

#define WM_WORK_LOG  (WM_APP + 1)
#define WM_WORK_DONE (WM_APP + 2)
#define WM_WORK_STEP (WM_APP + 3)

// --------------------------------------------------------------- globals
namespace {

HINSTANCE g_inst = nullptr;
HWND g_main = nullptr;
HFONT g_font = nullptr, g_fontBold = nullptr, g_fontTitle = nullptr,
      g_fontMono = nullptr;
HBRUSH g_bgBrush = nullptr, g_panelBrush = nullptr;

std::vector<std::wstring> g_files;

int g_tab = TAB_SLOPE;
COLORREF g_stColor = RGB(0xe6, 0x19, 0x4b);
std::vector<HWND> g_slopePage;  // controls belonging to each page, so
std::vector<HWND> g_stylePage;  // switching tabs is just show/hide
volatile bool g_running = false;

COLORREF g_colUp = RGB(0xe0, 0x1b, 0x1b);
COLORREF g_colDown = RGB(0x00, 0xa0, 0x3c);
COLORREF g_colFlat = RGB(0x9b, 0x30, 0xd9);

const COLORREF CLR_INK = RGB(0x14, 0x16, 0x1a);
const COLORREF CLR_MUTED = RGB(0x76, 0x7c, 0x85);
const COLORREF CLR_BG = RGB(0xff, 0xff, 0xff);
const COLORREF CLR_PANEL = RGB(0xf6, 0xf7, 0xf9);
const COLORREF CLR_OK = RGB(0x00, 0x80, 0x30);
const COLORREF CLR_ERR = RGB(0xc0, 0x10, 0x10);

// The layout is fixed: these two constants size the window exactly so there
// is no dead space under the log box.
const int kMargin  = 14;
const int kClientW = 900;
const int kClientH = 734;
const int kLogH    = 120;   // log height in the designed layout
const int kLogMinH = 64;    // below this it stops shrinking
int g_logTop = 0;           // y of the log, filled in by buildUi

struct Preset { const wchar_t *name; const wchar_t *thr, *win, *min; };
const Preset kPresets[] = {
    {L"Custom",              nullptr, nullptr, nullptr},
    {L"Road cycling",        L"1",    L"60",   L"120"},
    {L"MTB / gravel",        L"1.5",  L"50",   L"80"},
    {L"Hiking",              L"3",    L"50",   L"80"},
    {L"Long route (50 km+)", L"1.5",  L"100",  L"250"},
};

// ------------------------------------------------------------- utilities

std::wstring toW(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string toU8(const std::wstring &w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n,
                        nullptr, nullptr);
    return s;
}

std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

std::wstring hexOf(COLORREF c) {
    wchar_t buf[16];
    wsprintfW(buf, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

bool parseHex(const std::wstring &in, COLORREF &out) {
    std::string s, err;
    if (!resolveColor(toU8(in), s, err)) return false;
    unsigned r, g, b;
    if (sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3) return false;
    out = RGB(r, g, b);
    return true;
}

std::wstring baseNameW(const std::wstring &p) {
    size_t i = p.find_last_of(L"/\\");
    return (i == std::wstring::npos) ? p : p.substr(i + 1);
}

std::wstring dirNameW(const std::wstring &p) {
    size_t i = p.find_last_of(L"/\\");
    return (i == std::wstring::npos) ? L"" : p.substr(0, i);
}

bool endsWithGpx(const std::wstring &p) {
    if (p.size() < 4) return false;
    std::wstring e = p.substr(p.size() - 4);
    for (wchar_t &c : e) c = (wchar_t)towlower(c);
    return e == L".gpx";
}

// While a page is being built, every control it creates is recorded here so
// the tabs can hide it later. Labels and group boxes share id -1, so tracking
// by id is not possible - the window handle is the only reliable key.
std::vector<HWND> *g_collect = nullptr;

HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style,
        int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, style | WS_CHILD | WS_VISIBLE,
                             x, y, w, h, g_main, (HMENU)(INT_PTR)id,
                             g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_collect) g_collect->push_back(c);
    return c;
}

HWND ctl(int id) { return GetDlgItem(g_main, id); }

// Controls are created once and hidden, rather than destroyed and rebuilt:
// that way whatever the user typed on the other page survives a tab switch.
void showPage(int tab) {
    g_tab = tab;
    for (HWND h : g_slopePage) ShowWindow(h, tab == TAB_SLOPE ? SW_SHOW : SW_HIDE);
    for (HWND h : g_stylePage) ShowWindow(h, tab == TAB_STYLE ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_main, nullptr, TRUE);
}

void logLine(const std::wstring &text, COLORREF color = CLR_INK) {
    HWND h = ctl(ID_LOG);
    int len = GetWindowTextLengthW(h);
    SendMessageW(h, EM_SETSEL, len, len);
    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    std::wstring line = text + L"\r\n";
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(h, WM_VSCROLL, SB_BOTTOM, 0);
}

void refreshList() {
    HWND lb = ctl(ID_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (const std::wstring &f : g_files) {
        std::wstring row = L"  " + baseNameW(f) + L"      " + dirNameW(f);
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)row.c_str());
    }
    wchar_t buf[96];
    if (g_files.empty()) wcscpy(buf, L"No files yet.");
    else wsprintfW(buf, L"%d file%s ready.", (int)g_files.size(),
                   g_files.size() == 1 ? L"" : L"s");
    SetWindowTextW(ctl(ID_STATUS), buf);
}

void addPath(const std::wstring &p, int &added, int &skipped);

void addFolder(const std::wstring &dir, int &added, int &skipped) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> names;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (endsWithGpx(fd.cFileName)) names.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    for (const std::wstring &n : names) addPath(n, added, skipped);
}

void addPath(const std::wstring &p, int &added, int &skipped) {
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        addFolder(p, added, skipped);
        return;
    }
    if (!endsWithGpx(p)) { ++skipped; return; }
    for (const std::wstring &f : g_files)
        if (_wcsicmp(f.c_str(), p.c_str()) == 0) return;
    g_files.push_back(p);
    ++added;
}

void setPresetFromValues() {
    std::wstring t = getText(ctl(ID_THRESHOLD));
    std::wstring w = getText(ctl(ID_WINDOW));
    std::wstring m = getText(ctl(ID_MINLEN));
    int sel = 0;
    for (int i = 1; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); ++i)
        if (t == kPresets[i].thr && w == kPresets[i].win && m == kPresets[i].min)
            { sel = i; break; }
    SendMessageW(ctl(ID_PRESET), CB_SETCURSEL, sel, 0);
}

void syncOutputEnable() {
    bool same = SendMessageW(ctl(ID_SAMEDIR), BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(ctl(ID_OUTDIR), !same);
    EnableWindow(ctl(ID_BROWSE), !same);
}

// ------------------------------------------------------------ the worker

struct Job {
    std::vector<std::wstring> files;
    Options opt;
};

void postLog(const std::wstring &s, COLORREF c) {
    auto *pair = new std::pair<std::wstring, COLORREF>(s, c);
    PostMessageW(g_main, WM_WORK_LOG, 0, (LPARAM)pair);
}

DWORD WINAPI worker(LPVOID param) {
    Job *job = static_cast<Job *>(param);
    int ok = 0, fail = 0, step = 0;

    // Merging only makes sense for several inputs, and --split-files already
    // writes one file per class.
    const bool merging = job->opt.merge && job->files.size() > 1 &&
                         !job->opt.splitFiles;
    MergeSink sink;

    for (const std::wstring &f : job->files) {
        Stats st;
        std::string err;
        if (process(toU8(f), job->opt, st, err, merging ? &sink : nullptr)) {
            ++ok;
            if (st.noElevation) {
                // Kept on purpose: dropping it would quietly lose a track
                // from the merged file.
                postLog(L"  " + baseNameW(f) +
                        L": no elevation data \x2014 kept, but not coloured "
                        L"by slope.", CLR_ERR);
                PostMessageW(g_main, WM_WORK_STEP, ++step, 0);
                continue;
            }
            if (merging) {
                wchar_t buf[64];
                wsprintfW(buf, L": %d sections", (int)st.sections);
                postLog(L"  " + baseNameW(f) + buf, CLR_OK);
            }
            for (const std::string &w : st.written) {
                wchar_t buf[64];
                wsprintfW(buf, L"  (%d sections)", (int)st.sections);
                postLog(L"  written: " + toW(w) + buf, CLR_OK);
            }
            wchar_t line[256];
            swprintf(line, 256,
                     L"     %.2f km \x2014 up %.1f%%  down %.1f%%  flat %.1f%%",
                     st.totalM / 1000.0,
                     st.totalM > 0 ? st.uphillM / st.totalM * 100 : 0.0,
                     st.totalM > 0 ? st.downhillM / st.totalM * 100 : 0.0,
                     st.totalM > 0 ? st.flatM / st.totalM * 100 : 0.0);
            postLog(line, CLR_MUTED);
            if (st.waypoints) {
                wchar_t wl[64];
                wsprintfW(wl, L"     %d waypoint(s) copied", (int)st.waypoints);
                postLog(wl, CLR_MUTED);
            }
        } else {
            ++fail;
            postLog(L"  " + baseNameW(f) + L": " + toW(err), CLR_ERR);
        }
        PostMessageW(g_main, WM_WORK_STEP, ++step, 0);
    }

    if (merging && sink.files > 0) {
        std::string dir = job->opt.outputDir;
        if (dir.empty() && !job->files.empty())
            dir = dirName(toU8(job->files[0]));
        std::string name = job->opt.mergeName.empty() ? "all-tracks"
                                                      : job->opt.mergeName;
        if (name.size() < 4 || name.substr(name.size() - 4) != ".gpx")
            name += ".gpx";
        std::string path = dir.empty() ? name : joinPath(dir, name);

        std::string e;
        if (writeMerged(sink, path, job->opt, e)) {
            wchar_t line[200];
            postLog(L"", CLR_MUTED);
            postLog(L"  written: " + toW(path), CLR_OK);
            wsprintfW(line, L"  %d file(s) merged, %d coloured sections"
                            L" \x2014 import this one file into OsmAnd.",
                      (int)sink.files, (int)sink.trackCount);
            postLog(line, CLR_OK);
        } else {
            postLog(L"  " + toW(e), CLR_ERR);
            ++fail;
        }
    }

    PostMessageW(g_main, WM_WORK_DONE, (WPARAM)ok, (LPARAM)fail);
    delete job;
    return 0;
}

// ------------------------------------------------------------- painting

void paintSwatch(LPDRAWITEMSTRUCT d, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    RECT r = d->rcItem;
    FillRect(d->hDC, &r, b);
    DeleteObject(b);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x99, 0x9d, 0xa3));
    HGDIOBJ oldPen = SelectObject(d->hDC, pen);
    HGDIOBJ oldBr = SelectObject(d->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(d->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(d->hDC, oldPen);
    SelectObject(d->hDC, oldBr);
    DeleteObject(pen);
    if (d->itemState & ODS_FOCUS) DrawFocusRect(d->hDC, &r);
}

// --------------------------------------------------------------- actions

// The two tabs offer the same 3D choices, so the mapping from the combo text
// to OsmAnd's tag values lives in one place.
void read3dFrom(int comboId, int scaleId, std::string &viz, std::string &scale) {
    wchar_t buf[32] = L"";
    GetWindowTextW(ctl(comboId), buf, 32);
    const std::wstring v = buf;
    // Same three states as the markers: leave alone writes nothing at all,
    // remove it writes an explicit "none", a real choice writes the block.
    if (v == L"by altitude")       viz = "altitude";
    else if (v == L"fixed height") viz = "fixed_height";
    else if (v == L"remove it")    viz = "none";
    else                           viz.clear();
    if (!viz.empty()) {
        std::string s = toU8(getText(ctl(scaleId)));
        if (!s.empty()) scale = s;
    }
}

// "1 km" -> 1000 metres.
//
// Three states, not two. "leave alone" writes no tag at all, so a file that
// already had markers keeps them and one that never had them stays clean;
// "remove them" writes no_split, which actively turns them off. Writing
// no_split by default would have meant stamping a setting on every single
// file, which is not what an untouched default should do.
void readMarkersFrom(int comboId, std::string &type, std::string &interval) {
    wchar_t buf[32] = L"";
    GetWindowTextW(ctl(comboId), buf, 32);
    const std::wstring v = buf;
    if (v == L"leave alone") { type.clear(); interval.clear(); return; }
    if (v == L"remove them") { type = "no_split"; interval.clear(); return; }
    int metres = 0;
    if (v == L"500 m") metres = 500;
    else if (v == L"1 km") metres = 1000;
    else if (v == L"2 km") metres = 2000;
    else if (v == L"5 km") metres = 5000;
    else if (v == L"10 km") metres = 10000;
    if (metres > 0) {
        type = "distance";
        interval = std::to_string(metres);
    }
}

bool collectOptions(Options &opt, std::wstring &problem) {
    std::wstring t = getText(ctl(ID_THRESHOLD));
    std::wstring w = getText(ctl(ID_WINDOW));
    std::wstring m = getText(ctl(ID_MINLEN));

    wchar_t *end = nullptr;
    opt.threshold = wcstod(t.c_str(), &end);
    if (t.empty() || (end && *end)) { problem = L"Flat threshold must be a number."; return false; }
    if (opt.threshold < 0) { problem = L"Flat threshold cannot be negative."; return false; }

    opt.window = wcstod(w.c_str(), &end);
    if (w.empty() || (end && *end)) { problem = L"Smoothing window must be a number."; return false; }
    if (opt.window <= 0) { problem = L"Smoothing window must be greater than zero."; return false; }

    opt.minlen = wcstod(m.c_str(), &end);
    if (m.empty() || (end && *end)) { problem = L"Shortest section must be a number."; return false; }
    if (opt.minlen < 0) { problem = L"Shortest section cannot be negative."; return false; }

    opt.uphill = toU8(getText(ctl(ID_UP_EDIT)));
    opt.downhill = toU8(getText(ctl(ID_DOWN_EDIT)));
    opt.flat = toU8(getText(ctl(ID_FLAT_EDIT)));

    std::string dummy, err;
    if (!resolveColor(opt.uphill, dummy, err)) { problem = L"Uphill colour is not valid."; return false; }
    if (!resolveColor(opt.downhill, dummy, err)) { problem = L"Downhill colour is not valid."; return false; }
    if (!resolveColor(opt.flat, dummy, err)) { problem = L"Flat colour is not valid."; return false; }

    int wi = (int)SendMessageW(ctl(ID_WIDTH), CB_GETCURSEL, 0, 0);
    wchar_t wbuf[32] = L"24";
    if (wi >= 0) SendMessageW(ctl(ID_WIDTH), CB_GETLBTEXT, wi, (LPARAM)wbuf);
    opt.width = toU8(wbuf);
    if (!validWidth(opt.width, err)) { problem = toW(err); return false; }

    readMarkersFrom(ID_MARKERS, opt.splitType, opt.splitInterval);
    read3dFrom(ID_3D, ID_3DSCALE, opt.viz3d, opt.scale3d);
    {
        std::string err;
        if (!valid3d(opt, err)) { problem = toW(err); return false; }
        if (!validMarkers(opt, err)) { problem = toW(err); return false; }
    }
    opt.merge = SendMessageW(ctl(ID_MERGE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (opt.merge) {
        opt.mergeName = toU8(getText(ctl(ID_MERGENAME)));
        if (opt.mergeName.empty()) opt.mergeName = "all-tracks";
    }
    opt.suffix = toU8(getText(ctl(ID_SUFFIX)));
    if (opt.suffix.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        problem = L"The suffix cannot contain \\ / : * ? \" < > |";
        return false;
    }

    opt.noFlat = SendMessageW(ctl(ID_NOFLAT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt.splitFiles = SendMessageW(ctl(ID_SPLIT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt.arrows = SendMessageW(ctl(ID_ARROWS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt.keepWaypoints = SendMessageW(ctl(ID_WAYPOINTS), BM_GETCHECK, 0, 0) == BST_CHECKED;

    bool same = SendMessageW(ctl(ID_SAMEDIR), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!same) {
        std::wstring d = getText(ctl(ID_OUTDIR));
        DWORD a = d.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(d.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            problem = L"Choose a valid output folder.";
            return false;
        }
        opt.outputDir = toU8(d);
    }
    return true;
}

void doConvert() {
    if (g_running) return;
    if (g_files.empty()) {
        MessageBoxW(g_main, L"Add at least one GPX file first.",
                    L"gpx-slope-colors", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Options opt;
    std::wstring problem;
    if (!collectOptions(opt, problem)) {
        MessageBoxW(g_main, problem.c_str(), L"gpx-slope-colors",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    g_running = true;
    EnableWindow(ctl(ID_CONVERT), FALSE);
    SendMessageW(ctl(ID_PROGRESS), PBM_SETRANGE32, 0, (LPARAM)g_files.size());
    SendMessageW(ctl(ID_PROGRESS), PBM_SETPOS, 0, 0);

    wchar_t head[96];
    wsprintfW(head, L"Converting %d file%s\x2026", (int)g_files.size(),
              g_files.size() == 1 ? L"" : L"s");
    logLine(L"");
    logLine(head, CLR_INK);

    Job *job = new Job();
    job->files = g_files;
    job->opt = opt;
    CloseHandle(CreateThread(nullptr, 0, worker, job, 0, nullptr));
}

void doReset() {
    SetWindowTextW(ctl(ID_UP_EDIT), L"#e01b1b");
    SetWindowTextW(ctl(ID_DOWN_EDIT), L"#00a03c");
    SetWindowTextW(ctl(ID_FLAT_EDIT), L"#9b30d9");
    g_colUp = RGB(0xe0, 0x1b, 0x1b);
    g_colDown = RGB(0x00, 0xa0, 0x3c);
    g_colFlat = RGB(0x9b, 0x30, 0xd9);
    SetWindowTextW(ctl(ID_THRESHOLD), L"1.5");
    SetWindowTextW(ctl(ID_WINDOW), L"50");
    SetWindowTextW(ctl(ID_MINLEN), L"80");
    SetWindowTextW(ctl(ID_SUFFIX), L"_slope");
    SendMessageW(ctl(ID_WIDTH), CB_SETCURSEL, 7, 0);   // "24"
    SendMessageW(ctl(ID_NOFLAT), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(ctl(ID_ARROWS), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(ctl(ID_WAYPOINTS), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(ctl(ID_SPLIT), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(ctl(ID_MARKERS), CB_SETCURSEL, 0, 0);
    SendMessageW(ctl(ID_3D), CB_SETCURSEL, 0, 0);
    SetWindowTextW(ctl(ID_3DSCALE), L"1.0");
    EnableWindow(ctl(ID_3DSCALE), FALSE);
    SendMessageW(ctl(ID_MERGE), BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(ctl(ID_MERGENAME), L"all-tracks");
    EnableWindow(ctl(ID_MERGE), TRUE);
    EnableWindow(ctl(ID_MERGENAME), TRUE);
    SendMessageW(ctl(ID_SAMEDIR), BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(ctl(ID_OUTDIR), L"");
    SendMessageW(ctl(ID_PRESET), CB_SETCURSEL, 2, 0);
    syncOutputEnable();
    InvalidateRect(ctl(ID_UP_SWATCH), nullptr, TRUE);
    InvalidateRect(ctl(ID_DOWN_SWATCH), nullptr, TRUE);
    InvalidateRect(ctl(ID_FLAT_SWATCH), nullptr, TRUE);
    logLine(L"Settings reset.", CLR_MUTED);
}

void pickColor(int swatchId, int editId, COLORREF &store) {
    static COLORREF custom[16] = {0};
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g_main;
    cc.rgbResult = store;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        store = cc.rgbResult;
        SetWindowTextW(ctl(editId), hexOf(store).c_str());
        InvalidateRect(ctl(swatchId), nullptr, TRUE);
    }
}

void pickFolder() {
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = g_main;
    bi.lpszTitle = L"Choose the output folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(ctl(ID_OUTDIR), path);
    CoTaskMemFree(pidl);
}

void openFileDialog() {
    std::vector<wchar_t> buf(64 * 1024, 0);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"GPX tracks\0*.gpx\0All files\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST |
                OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    int added = 0, skipped = 0;
    std::wstring first = buf.data();
    size_t firstLen = first.size();
    if (buf[firstLen + 1] == L'\0') {
        addPath(first, added, skipped);           // single selection
    } else {
        wchar_t *p = buf.data() + firstLen + 1;   // directory + names
        while (*p) {
            addPath(first + L"\\" + p, added, skipped);
            p += wcslen(p) + 1;
        }
    }
    refreshList();
    if (added) {
        wchar_t m[64];
        wsprintfW(m, L"Added %d file%s.", added, added == 1 ? L"" : L"s");
        logLine(m, CLR_MUTED);
    }
}

// -------------------------------------------------------------- creation

// ------------------------------------------------------- batch styler page
//
// Laid out over the same rectangle as the slope controls; both sets exist at
// all times and only one is visible. Coordinates are duplicated rather than
// shared because the two pages group their options differently.

// Defined further down, next to the other option-reading helpers.
void syncPerTrackEnable();

void buildStylePage(int y0, int actionY) {
    const int M = kMargin;
    const int W = kClientW;
    const int colW = (W - 2 * M - 12) / 2;
    const int leftX = M, rightX = M + colW + 12;
    int y = y0;

    // ---- colour
    mk(L"BUTTON", L" Colour ", BS_GROUPBOX, leftX, y, colW, 158, ID_ST_AUTO + 900);
    mk(L"BUTTON", L"A different colour for each file", BS_AUTORADIOBUTTON | WS_GROUP,
           leftX + 12, y + 22, 260, 20, ID_ST_AUTO);
    mk(L"BUTTON", L"One colour for all", BS_AUTORADIOBUTTON,
           leftX + 12, y + 46, 180, 20, ID_ST_ONE);
    // Neither radio is selected by default unless we say so, and an unselected
    // pair reads as "one colour for all" further down: three files would come
    // out identical. A different colour each is the useful default here.
    SendMessageW(ctl(ID_ST_AUTO), BM_SETCHECK, BST_CHECKED, 0);
    mk(L"BUTTON", L"", BS_OWNERDRAW, leftX + 196, y + 45, 44, 22, ID_ST_SWATCH);
    mk(L"EDIT", L"#e6194b", WS_BORDER | ES_AUTOHSCROLL,
           leftX + 248, y + 45, 96, 22, ID_ST_EDIT);
    mk(L"STATIC", L"Palette", 0, leftX + 12, y + 78, 60, 18, ID_ST_PALETTE + 900);
    HWND pcb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  leftX + 80, y + 74, 150, 160, ID_ST_PALETTE);
    for (const wchar_t *p : {L"distinct", L"warm", L"cool", L"colorblind"})
        SendMessageW(pcb, CB_ADDSTRING, 0, (LPARAM)p);
    SendMessageW(pcb, CB_SETCURSEL, 0, 0);
    mk(L"BUTTON", L"Vary colour between tracks in a file", BS_AUTOCHECKBOX,
           leftX + 12, y + 102, 300, 20, ID_ST_PERTRACK);

    // ---- appearance
    mk(L"BUTTON", L" Appearance ", BS_GROUPBOX, rightX, y, colW, 158, ID_ST_WIDTH + 900);
    mk(L"STATIC", L"Line width", 0, rightX + 12, y + 26, 76, 18, ID_ST_ARROWS + 900);
    HWND wcb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  rightX + 96, y + 22, 100, 240, ID_ST_WIDTH);
    for (const wchar_t *w : {L"leave alone", L"thin", L"medium", L"bold",
                             L"4", L"8", L"12", L"16", L"24"})
        SendMessageW(wcb, CB_ADDSTRING, 0, (LPARAM)w);
    SendMessageW(wcb, CB_SETCURSEL, 6, 0);
    mk(L"BUTTON", L"Direction arrows", BS_AUTOCHECKBOX,
           rightX + 12, y + 52, 180, 20, ID_ST_ARROWS);
    mk(L"BUTTON", L"Start / finish markers", BS_AUTOCHECKBOX,
           rightX + 200, y + 52, 200, 20, ID_ST_STARTFIN);
    SendMessageW(ctl(ID_ST_ARROWS), BM_SETCHECK, BST_CHECKED, 0);

    mk(L"STATIC", L"Colour by", 0, rightX + 12, y + 80, 76, 18, ID_ST_COLORING + 900);
    HWND ccb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  rightX + 96, y + 76, 140, 160, ID_ST_COLORING);
    // Only the values OsmAnd honours without a Pro subscription. "slope" is
    // deliberately absent: the other tab does that, and for free.
    for (const wchar_t *c : {L"solid", L"speed", L"altitude"})
        SendMessageW(ccb, CB_ADDSTRING, 0, (LPARAM)c);
    SendMessageW(ccb, CB_SETCURSEL, 0, 0);

    // Same wording and same choices as the slope tab: two names for one idea
    // was just confusing. Time-based splitting stays on the command line.
    mk(L"STATIC", L"Markers every", 0, rightX + 12, y + 106, 88, 18,
       ID_ST_SPLIT + 900);
    HWND scb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  rightX + 104, y + 102, 132, 200, ID_ST_SPLIT);
    for (const wchar_t *s : {L"leave alone", L"remove them", L"500 m",
                             L"1 km", L"2 km", L"5 km", L"10 km"})
        SendMessageW(scb, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(scb, CB_SETCURSEL, 0, 0);

    mk(L"STATIC", L"3D wall", 0, rightX + 12, y + 132, 76, 18,
       ID_ST_3D + 900);
    HWND vcb2 = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                   rightX + 96, y + 128, 140, 200, ID_ST_3D);
    for (const wchar_t *s : {L"leave alone", L"remove it", L"by altitude",
                             L"fixed height"})
        SendMessageW(vcb2, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(vcb2, CB_SETCURSEL, 0, 0);
    mk(L"STATIC", L"height \x00d7", 0, rightX + 248, y + 132, 56, 18,
       ID_ST_3DSCALE + 900);
    mk(L"EDIT", L"1.0", WS_BORDER | ES_AUTOHSCROLL | ES_RIGHT | WS_DISABLED,
           rightX + 306, y + 128, 50, 21, ID_ST_3DSCALE);
    y += 170;

    // ---- waypoints and metadata
    mk(L"BUTTON", L" Waypoints and type ", BS_GROUPBOX, leftX, y, colW, 88, ID_ST_GROUPS + 900);
    mk(L"BUTTON", L"Give each waypoint group its own icon", BS_AUTOCHECKBOX,
           leftX + 12, y + 24, 320, 20, ID_ST_GROUPS);
    SendMessageW(ctl(ID_ST_GROUPS), BM_SETCHECK, BST_CHECKED, 0);
    mk(L"STATIC", L"Activity", 0, leftX + 12, y + 54, 60, 18, ID_ST_ACTIVITY + 900);
    HWND acb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  leftX + 80, y + 50, 180, 240, ID_ST_ACTIVITY);
    for (const wchar_t *a : {L"(none)", L"hiking", L"walking", L"running",
                             L"cycling", L"mountain_biking", L"road_biking",
                             L"skiing", L"ski_touring", L"motorcycling",
                             L"driving", L"sailing", L"kayaking"})
        SendMessageW(acb, CB_ADDSTRING, 0, (LPARAM)a);
    SendMessageW(acb, CB_SETCURSEL, 0, 0);

    // ---- output
    // Merging comes first and is on by default: ten stages become one import
    // with no appearance menu afterwards, which is the point of the tab.
    mk(L"BUTTON", L" Output ", BS_GROUPBOX, rightX, y, colW, 140, ID_ST_SUFFIX + 900);
    mk(L"BUTTON", L"Merge all into one file to import", BS_AUTOCHECKBOX,
           rightX + 12, y + 22, 290, 20, ID_ST_MERGE);
    SendMessageW(ctl(ID_ST_MERGE), BM_SETCHECK, BST_CHECKED, 0);
    mk(L"STATIC", L"File name", 0, rightX + 28, y + 50, 66, 18,
       ID_ST_MERGENAME + 900);
    mk(L"EDIT", L"all-tracks", WS_BORDER | ES_AUTOHSCROLL,
           rightX + 104, y + 46, 150, 21, ID_ST_MERGENAME);

    mk(L"STATIC", L"Name suffix", 0, rightX + 12, y + 78, 84, 18, ID_ST_SAMEDIR + 900);
    mk(L"EDIT", L"_osmand", WS_BORDER | ES_AUTOHSCROLL | WS_DISABLED,
           rightX + 104, y + 74, 110, 21, ID_ST_SUFFIX);
    mk(L"STATIC", L"(when not merging)", 0, rightX + 224, y + 78, 130, 18,
       ID_ST_SUFFIX + 901);
    mk(L"BUTTON", L"Next to each original", BS_AUTOCHECKBOX,
           rightX + 12, y + 104, 170, 20, ID_ST_SAMEDIR);
    SendMessageW(ctl(ID_ST_SAMEDIR), BM_SETCHECK, BST_CHECKED, 0);
    mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_DISABLED,
           rightX + 190, y + 104, colW - 300, 21, ID_ST_OUTDIR);
    mk(L"BUTTON", L"Browse\x2026", BS_PUSHBUTTON | WS_DISABLED,
           rightX + colW - 104, y + 103, 92, 23, ID_ST_BROWSE);
    // The action row is shared with the slope page: same y, so the button does
    // not jump when the tab changes. Only one of the two is ever visible.
    syncPerTrackEnable();

    mk(L"BUTTON", L"Apply style", BS_DEFPUSHBUTTON, M, actionY, 120, 34, ID_ST_APPLY);
    mk(L"BUTTON", L"Preview only (change nothing)", BS_AUTOCHECKBOX,
           M + 132, actionY + 8, 240, 20, ID_ST_DRYRUN);
}

// Reads the style page into an Options struct. Returns false with a message
// when something is inconsistent, so the user is told before any file moves.
// "Vary colour between tracks in a file" has no effect when the run already
// spreads the palette across tracks by itself, which is exactly what merging
// with a colour per file does. Leaving it clickable invites the user to tick
// it, see no change, and conclude it is broken - so it is greyed out instead.
void syncPerTrackEnable() {
    const bool merging =
        SendMessageW(ctl(ID_ST_MERGE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool autoCol =
        SendMessageW(ctl(ID_ST_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(ctl(ID_ST_PERTRACK), !(merging && autoCol));
}

bool collectStyleOptions(styler::Options &opt, std::wstring &problem) {
    using namespace styler;

    if (SendMessageW(ctl(ID_ST_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED) {
        opt.autoColor = true;
    } else {
        COLORREF c;
        if (!parseHex(getText(ctl(ID_ST_EDIT)), c)) {
            problem = L"The colour must be a hex code like #e6194b.";
            return false;
        }
        opt.color = toU8(hexOf(c));
    }
    {
        wchar_t buf[32] = L"";
        GetWindowTextW(ctl(ID_ST_PALETTE), buf, 32);
        std::string err;
        if (!parsePalette(toU8(buf), opt.palette, err)) {
            problem = toW(err);
            return false;
        }
    }
    opt.perTrackColor =
        SendMessageW(ctl(ID_ST_PERTRACK), BM_GETCHECK, 0, 0) == BST_CHECKED;

    {
        wchar_t buf[32] = L"";
        GetWindowTextW(ctl(ID_ST_WIDTH), buf, 32);
        std::wstring w = buf;
        if (w != L"leave alone") opt.width = toU8(w);
    }
    opt.arrows = SendMessageW(ctl(ID_ST_ARROWS), BM_GETCHECK, 0, 0) == BST_CHECKED
                     ? Tri::On : Tri::Off;
    opt.startFinish =
        SendMessageW(ctl(ID_ST_STARTFIN), BM_GETCHECK, 0, 0) == BST_CHECKED
            ? Tri::On : Tri::Off;
    {
        wchar_t buf[32] = L"";
        GetWindowTextW(ctl(ID_ST_COLORING), buf, 32);
        opt.coloring = toU8(buf);
    }
    readMarkersFrom(ID_ST_SPLIT, opt.splitType, opt.splitInterval);
    opt.groupByType =
        SendMessageW(ctl(ID_ST_GROUPS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    {
        wchar_t buf[64] = L"";
        GetWindowTextW(ctl(ID_ST_ACTIVITY), buf, 64);
        std::wstring a = buf;
        if (a != L"(none)") opt.activity = toU8(a);
    }
    read3dFrom(ID_ST_3D, ID_ST_3DSCALE, opt.viz3d, opt.scale3d);
    opt.merge = SendMessageW(ctl(ID_ST_MERGE), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (opt.merge) {
        opt.mergeName = toU8(getText(ctl(ID_ST_MERGENAME)));
        if (opt.mergeName.empty()) opt.mergeName = "all-tracks";
    }
    opt.suffix = toU8(getText(ctl(ID_ST_SUFFIX)));
    if (opt.suffix.empty()) opt.suffix = "_osmand";
    // No "include subfolders" box here: dropping a folder on the list already
    // expands it to files, so run() only ever sees plain paths. The CLI keeps
    // --recursive, where a folder really is passed through.
    opt.dryRun =
        SendMessageW(ctl(ID_ST_DRYRUN), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (SendMessageW(ctl(ID_ST_SAMEDIR), BM_GETCHECK, 0, 0) != BST_CHECKED) {
        opt.outDir = toU8(getText(ctl(ID_ST_OUTDIR)));
        if (opt.outDir.empty()) {
            problem = L"Choose an output folder, or tick \"Next to each original\".";
            return false;
        }
    }

    std::string err;
    if (!validate(opt, err)) { problem = toW(err); return false; }
    return true;
}

struct StyleJob {
    std::vector<std::string> files;
    styler::Options opt;
};

// styler::run() does the whole batch in one call, so without this the bar
// would sit at zero and then jump to full at the very end. Reporting per file
// from inside the run is the only way to make it move while work happens.
void styleProgress(void *, size_t done, size_t) {
    PostMessageW(g_main, WM_WORK_STEP, (WPARAM)done, 0);
}

DWORD WINAPI styleWorker(LPVOID param) {
    StyleJob *job = static_cast<StyleJob *>(param);
    styler::RunStats st;
    std::string err;

    if (!styler::run(job->files, job->opt, st, err, styleProgress, nullptr)) {
        postLog(L"  " + toW(err), CLR_ERR);
        PostMessageW(g_main, WM_WORK_DONE, 0, 1);
        delete job;
        return 0;
    }

    for (const styler::FileResult &f : st.files) {
        std::wstring name = toW(slope::baseName(f.input));
        if (f.skipped) {
            postLog(L"  " + name + L": " + toW(f.skipReason), CLR_ERR);
        } else {
            std::wstring line = L"  " + name;
            if (!f.color.empty()) line += L"   " + toW(f.color);
            postLog(line, CLR_OK);
            for (const std::string &w : f.warnings)
                postLog(L"     note: " + toW(w), CLR_MUTED);
        }
    }

    if (!st.groups.empty()) {
        postLog(L"", CLR_MUTED);
        for (const styler::Group &g : st.groups) {
            std::wstring l = L"  " + toW(g.type) + L"  \x2192  " + toW(g.icon);
            if (g.iconGuessed) l += L"   [no match, generic marker]";
            postLog(l, CLR_MUTED);
        }
    }
    if (job->opt.merge) {
        const styler::MergeResult &m = st.merged;
        wchar_t line[320];
        if (job->opt.dryRun) {
            wsprintfW(line, L"  Would merge %d file(s) into %s: %d track(s).",
                      (int)m.sources, toW(slope::baseName(m.output)).c_str(),
                      (int)m.tracks);
        } else {
            wsprintfW(line, L"  Merged %d file(s) into %s", (int)m.sources,
                      toW(m.output).c_str());
        }
        postLog(L"", CLR_MUTED);
        postLog(line, CLR_OK);
        if (!job->opt.dryRun) {
            wchar_t l2[200];
            wsprintfW(l2, L"  %d track(s), %d waypoint(s) \x2014 import this "
                          L"one file into OsmAnd.",
                      (int)m.tracks, (int)m.waypoints);
            postLog(l2, CLR_OK);
        }
    }
    if (job->opt.dryRun)
        postLog(L"  Preview only \x2014 nothing was written.", CLR_MUTED);

    PostMessageW(g_main, WM_WORK_DONE,
                 (WPARAM)(job->opt.merge ? (st.merged.written ? 1 : 0)
                                         : st.written),
                 (LPARAM)(st.skipped + st.failed));
    delete job;
    return 0;
}

void buildUi(HWND hwnd) {
    const int M = kMargin;
    const int W = kClientW;
    int y = 12;

    HWND t = mk(L"STATIC", L"Colour GPX tracks by slope", 0, M, y, 520, 26, -1);
    SendMessageW(t, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    mk(L"STATIC", L"Uphill one colour, downhill another, following the "
                  L"direction you ride.", 0, M + 330, y + 7, 420, 18, -1);
    y += 34;

    // ---- files
    mk(L"BUTTON", L" Files ", BS_GROUPBOX, M, y, W - 2 * M, 120, -1);
    mk(L"STATIC", L"Drop GPX files here, or use Add files", 0,
       M + 12, y + 20, 340, 18, -1);
    mk(L"BUTTON", L"Add files\x2026", BS_PUSHBUTTON, W - M - 270, y + 16, 84, 25, ID_ADD);
    mk(L"BUTTON", L"Remove", BS_PUSHBUTTON, W - M - 180, y + 16, 80, 25, ID_REMOVE);
    mk(L"BUTTON", L"Clear", BS_PUSHBUTTON, W - M - 94, y + 16, 80, 25, ID_CLEAR);
    mk(L"LISTBOX", L"", LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | WS_VSCROLL |
       WS_BORDER, M + 12, y + 44, W - 2 * M - 24, 64, ID_LIST);
    y += 132;

    // ---- tabs
    // The file list above is shared: both modes work on the same selection,
    // so it stays outside the tabs.
    HWND tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                M, y, W - 2 * M, 24, hwnd,
                                (HMENU)(INT_PTR)ID_TABS, g_inst, nullptr);
    SendMessageW(tabs, WM_SETFONT, (WPARAM)g_font, TRUE);
    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)L"  Slope colours  ";
    TabCtrl_InsertItem(tabs, 0, &ti);
    ti.pszText = (LPWSTR)L"  Batch styling  ";
    TabCtrl_InsertItem(tabs, 1, &ti);
    const int tabTop = y + 30;
    y = tabTop;

    g_collect = &g_slopePage;

    const int colW = (W - 2 * M - 12) / 2;
    const int leftX = M, rightX = M + colW + 12;

    // ---- colours
    mk(L"BUTTON", L" Colours ", BS_GROUPBOX, leftX, y, colW, 130, -1);
    struct CRow { const wchar_t *label; int sw, ed; COLORREF *c; const wchar_t *def; };
    const CRow rows[3] = {
        {L"Uphill",   ID_UP_SWATCH,   ID_UP_EDIT,   &g_colUp,   L"#e01b1b"},
        {L"Downhill", ID_DOWN_SWATCH, ID_DOWN_EDIT, &g_colDown, L"#00a03c"},
        {L"Flat",     ID_FLAT_SWATCH, ID_FLAT_EDIT, &g_colFlat, L"#9b30d9"},
    };
    for (int i = 0; i < 3; ++i) {
        int ry = y + 22 + i * 28;
        mk(L"STATIC", rows[i].label, 0, leftX + 12, ry + 4, 66, 18, -1);
        mk(L"BUTTON", L"", BS_OWNERDRAW, leftX + 84, ry, 44, 22, rows[i].sw);
        mk(L"EDIT", rows[i].def, WS_BORDER | ES_AUTOHSCROLL,
           leftX + 136, ry, 104, 22, rows[i].ed);
    }
    mk(L"BUTTON", L"No flat class (only up and down)", BS_AUTOCHECKBOX,
       leftX + 12, y + 106, 260, 20, ID_NOFLAT);

    // ---- detection
    mk(L"BUTTON", L" Detection ", BS_GROUPBOX, rightX, y, colW, 130, -1);
    mk(L"STATIC", L"Preset", 0, rightX + 12, y + 26, 108, 18, -1);
    HWND cb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                 rightX + 126, y + 22, 170, 200, ID_PRESET);
    for (const Preset &p : kPresets)
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)p.name);
    SendMessageW(cb, CB_SETCURSEL, 2, 0);

    const wchar_t *dl[3] = {L"Flat threshold", L"Smoothing window",
                            L"Shortest section"};
    const wchar_t *dv[3] = {L"1.5", L"50", L"80"};
    const wchar_t *du[3] = {L"%", L"m", L"m"};
    const int dids[3] = {ID_THRESHOLD, ID_WINDOW, ID_MINLEN};
    for (int i = 0; i < 3; ++i) {
        int ry = y + 52 + i * 26;
        mk(L"STATIC", dl[i], 0, rightX + 12, ry + 3, 110, 18, -1);
        mk(L"EDIT", dv[i], WS_BORDER | ES_AUTOHSCROLL | ES_RIGHT,
           rightX + 126, ry, 64, 21, dids[i]);
        mk(L"STATIC", du[i], 0, rightX + 196, ry + 3, 20, 18, -1);
    }
    y += 138;

    // ---- appearance
    // Two columns: the box is wide and the labels are short, so the markers
    // and the 3D wall fit without making the window any taller.
    mk(L"BUTTON", L" Appearance ", BS_GROUPBOX, leftX, y, colW, 106, -1);
    mk(L"STATIC", L"Line width", 0, leftX + 12, y + 26, 76, 18, -1);
    HWND wcb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  leftX + 92, y + 22, 92, 240, ID_WIDTH);
    const wchar_t *widths[] = {L"thin", L"medium", L"bold", L"4", L"8",
                               L"12", L"16", L"24"};
    for (const wchar_t *w : widths)
        SendMessageW(wcb, CB_ADDSTRING, 0, (LPARAM)w);
    SendMessageW(wcb, CB_SETCURSEL, 7, 0);
    mk(L"BUTTON", L"Show direction arrows", BS_AUTOCHECKBOX,
       leftX + 12, y + 52, 180, 20, ID_ARROWS);
    mk(L"BUTTON", L"Keep waypoints", BS_AUTOCHECKBOX,
       leftX + 12, y + 76, 180, 20, ID_WAYPOINTS);

    // right-hand column
    mk(L"STATIC", L"Markers every", 0, leftX + 198, y + 26, 92, 18, -1);
    HWND mcb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  leftX + 296, y + 22, 118, 200, ID_MARKERS);
    for (const wchar_t *s : {L"leave alone", L"remove them", L"500 m",
                             L"1 km", L"2 km", L"5 km", L"10 km"})
        SendMessageW(mcb, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(mcb, CB_SETCURSEL, 0, 0);

    mk(L"STATIC", L"3D wall", 0, leftX + 198, y + 54, 92, 18, -1);
    HWND vcb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  leftX + 296, y + 50, 118, 200, ID_3D);
    for (const wchar_t *s : {L"leave alone", L"remove it", L"by altitude",
                             L"fixed height"})
        SendMessageW(vcb, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(vcb, CB_SETCURSEL, 0, 0);

    mk(L"STATIC", L"Wall height \x00d7", 0, leftX + 198, y + 80, 92, 18, -1);
    mk(L"EDIT", L"1.0", WS_BORDER | ES_AUTOHSCROLL | ES_RIGHT | WS_DISABLED,
       leftX + 296, y + 76, 56, 21, ID_3DSCALE);
    SendMessageW(ctl(ID_ARROWS), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(ctl(ID_WAYPOINTS), BM_SETCHECK, BST_CHECKED, 0);

    // ---- output
    mk(L"BUTTON", L" Output ", BS_GROUPBOX, rightX, y, colW, 106, -1);
    mk(L"STATIC", L"Name suffix", 0, rightX + 12, y + 26, 84, 18, -1);
    mk(L"EDIT", L"_slope", WS_BORDER | ES_AUTOHSCROLL,
       rightX + 104, y + 22, 120, 21, ID_SUFFIX);
    mk(L"STATIC", L"ride.gpx \x2192 ride_slope.gpx", 0,
       rightX + 232, y + 26, 190, 18, -1);

    mk(L"BUTTON", L"Next to each original file", BS_AUTOCHECKBOX,
       rightX + 12, y + 52, 210, 20, ID_SAMEDIR);
    SendMessageW(ctl(ID_SAMEDIR), BM_SETCHECK, BST_CHECKED, 0);
    mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_DISABLED,
       rightX + 12, y + 76, colW - 130, 21, ID_OUTDIR);
    mk(L"BUTTON", L"Browse\x2026", BS_PUSHBUTTON | WS_DISABLED,
       rightX + colW - 112, y + 75, 96, 23, ID_BROWSE);

    y += 118;

    // ---- how many files come out
    // Merging and splitting both decide the number of output files, so they
    // live in the same box: keeping them apart made them look unrelated, when
    // in fact they exclude each other.
    mk(L"BUTTON", L" How many files come out ", BS_GROUPBOX,
       leftX, y, W - 2 * kMargin, 82, -1);

    // On by default: six stages become one import instead of six, and the
    // slope colours already live per track so nothing is lost.
    mk(L"BUTTON", L"Merge everything into one file to import",
       BS_AUTOCHECKBOX, leftX + 12, y + 24, 330, 20, ID_MERGE);
    SendMessageW(ctl(ID_MERGE), BM_SETCHECK, BST_CHECKED, 0);
    mk(L"STATIC", L"File name", 0, leftX + 360, y + 26, 66, 18, -1);
    mk(L"EDIT", L"all-tracks", WS_BORDER | ES_AUTOHSCROLL,
       leftX + 432, y + 22, 160, 21, ID_MERGENAME);

    mk(L"BUTTON", L"Separate files for uphill, downhill and flat",
       BS_AUTOCHECKBOX, leftX + 12, y + 52, 330, 20, ID_SPLIT);
    mk(L"STATIC", L"so each can be shown on its own in OsmAnd", 0,
       leftX + 360, y + 54, 330, 18, -1);
    y += 92;

    g_collect = nullptr;   // slope page finished

    g_collect = &g_stylePage;
    buildStylePage(tabTop, y);
    g_collect = nullptr;

    // ---- actions
    // Convert and Reset belong to the slope page so they disappear on the
    // other tab; the status line below is shared and stays put.
    g_collect = &g_slopePage;
    mk(L"BUTTON", L"Convert", BS_DEFPUSHBUTTON, M, y, 120, 34, ID_CONVERT);
    mk(L"BUTTON", L"Reset settings", BS_PUSHBUTTON, M + 132, y, 120, 34, ID_RESET);
    g_collect = nullptr;
    mk(L"STATIC", L"No files yet.", 0, M + 266, y + 10, 280, 18, ID_STATUS);
    y += 42;

    CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
                    M, y, W - 2 * M, 10, hwnd, (HMENU)(INT_PTR)ID_PROGRESS,
                    g_inst, nullptr);
    y += 18;

    g_logTop = y;
    HWND log = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                               ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                               M, y, W - 2 * M, kLogH, hwnd,
                               (HMENU)(INT_PTR)ID_LOG, g_inst, nullptr);
    SendMessageW(log, WM_SETFONT, (WPARAM)g_fontMono, TRUE);
    SendMessageW(log, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0xfb, 0xfb, 0xfc));

    logLine(L"Ready. Drop GPX files above, or use Add files.", CLR_MUTED);
}

void doApplyStyle() {
    if (g_running) return;
    if (g_files.empty()) {
        MessageBoxW(g_main, L"Add at least one GPX file first.",
                    L"gpx-slope-colors", MB_OK | MB_ICONINFORMATION);
        return;
    }
    styler::Options opt;
    std::wstring problem;
    if (!collectStyleOptions(opt, problem)) {
        MessageBoxW(g_main, problem.c_str(), L"gpx-slope-colors",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    g_running = true;
    EnableWindow(ctl(ID_ST_APPLY), FALSE);
    logLine(L"");

    StyleJob *job = new StyleJob();
    // Dropping a folder expands to every .gpx in it, output of earlier runs
    // included. Writing next to the originals would then restyle those too and
    // produce name_osmand_osmand.gpx, so they are left out here.
    int reStyled = 0;
    // When merging, the file to leave out is the merge target itself: run()
    // drops it too, but filtering here keeps the progress count honest.
    const std::wstring suffix = toW(opt.suffix);
    for (const std::wstring &f : g_files) {
        if (opt.merge) {
            std::wstring want = toW(opt.mergeName);
            size_t d = want.find_last_of(L'.');
            if (d == std::wstring::npos) want += L".gpx";
            std::wstring base = f;
            size_t s = base.find_last_of(L"\\/");
            if (s != std::wstring::npos) base.erase(0, s + 1);
            if (_wcsicmp(base.c_str(), want.c_str()) == 0) { ++reStyled; continue; }
            job->files.push_back(toU8(f));
            continue;
        }
        if (opt.outDir.empty() && !suffix.empty()) {
            std::wstring stem = f;
            size_t dot = stem.find_last_of(L'.');
            if (dot != std::wstring::npos) stem.erase(dot);
            if (stem.size() > suffix.size() &&
                _wcsicmp(stem.c_str() + stem.size() - suffix.size(),
                         suffix.c_str()) == 0) {
                ++reStyled;
                continue;
            }
        }
        job->files.push_back(toU8(f));
    }
    if (job->files.empty()) {
        delete job;
        g_running = false;
        EnableWindow(ctl(ID_ST_APPLY), TRUE);
        MessageBoxW(g_main,
                    opt.merge
                        ? L"The only file in the list is the merged file this "
                          L"run would create.\n\nAdd the original tracks, or "
                          L"change the merged file name."
                        : L"Every file in the list already ends with that "
                          L"suffix, so they look like the output of an earlier "
                          L"run.\n\nChoose an output folder, or change the "
                          L"name suffix.",
                    L"gpx-slope-colors", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (reStyled) {
        wchar_t m[128];
        wsprintfW(m, opt.merge
                      ? L"  Skipping %d file%s: that is the merged file itself."
                      : L"  Skipping %d file%s that already look styled.",
                  reStyled, reStyled == 1 ? L"" : L"s");
        logLine(m, CLR_MUTED);
    }
    job->opt = opt;
    SendMessageW(ctl(ID_PROGRESS), PBM_SETRANGE32, 0, (LPARAM)job->files.size());
    SendMessageW(ctl(ID_PROGRESS), PBM_SETPOS, 0, 0);
    wchar_t head[96];
    wsprintfW(head, L"Styling %d file%s\x2026", (int)job->files.size(),
              job->files.size() == 1 ? L"" : L"s");
    logLine(head, CLR_INK);

    CloseHandle(CreateThread(nullptr, 0, styleWorker, job, 0, nullptr));
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        buildUi(hwnd);
        showPage(TAB_SLOPE);
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        int added = 0, skipped = 0;
        for (UINT i = 0; i < n; ++i) {
            wchar_t path[MAX_PATH];
            DragQueryFileW(drop, i, path, MAX_PATH);
            addPath(path, added, skipped);
        }
        DragFinish(drop);
        refreshList();
        if (added) {
            wchar_t m[64];
            wsprintfW(m, L"Added %d file%s.", added, added == 1 ? L"" : L"s");
            logLine(m, CLR_MUTED);
        }
        if (skipped) {
            wchar_t m[80];
            wsprintfW(m, L"Ignored %d item%s (not .gpx).", skipped,
                      skipped == 1 ? L"" : L"s");
            logLine(m, CLR_MUTED);
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, CLR_BG);
        SetTextColor(dc, CLR_INK);
        return (LRESULT)g_bgBrush;
    }

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->idFrom == ID_TABS && nh->code == TCN_SELCHANGE) {
            showPage((int)TabCtrl_GetCurSel(nh->hwndFrom));
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
        if (d->CtlID == ID_ST_SWATCH) { paintSwatch(d, g_stColor); return TRUE; }
        if (d->CtlID == ID_UP_SWATCH)   { paintSwatch(d, g_colUp); return TRUE; }
        if (d->CtlID == ID_DOWN_SWATCH) { paintSwatch(d, g_colDown); return TRUE; }
        if (d->CtlID == ID_FLAT_SWATCH) { paintSwatch(d, g_colFlat); return TRUE; }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);

        if (code == EN_CHANGE) {
            if (id == ID_THRESHOLD || id == ID_WINDOW || id == ID_MINLEN)
                setPresetFromValues();
            if (id == ID_ST_EDIT) {
                COLORREF c;
                if (parseHex(getText(ctl(ID_ST_EDIT)), c)) {
                    g_stColor = c;
                    InvalidateRect(ctl(ID_ST_SWATCH), nullptr, TRUE);
                }
            }
            if (id == ID_UP_EDIT) {
                COLORREF c;
                if (parseHex(getText(ctl(ID_UP_EDIT)), c)) {
                    g_colUp = c; InvalidateRect(ctl(ID_UP_SWATCH), nullptr, TRUE);
                }
            } else if (id == ID_DOWN_EDIT) {
                COLORREF c;
                if (parseHex(getText(ctl(ID_DOWN_EDIT)), c)) {
                    g_colDown = c; InvalidateRect(ctl(ID_DOWN_SWATCH), nullptr, TRUE);
                }
            } else if (id == ID_FLAT_EDIT) {
                COLORREF c;
                if (parseHex(getText(ctl(ID_FLAT_EDIT)), c)) {
                    g_colFlat = c; InvalidateRect(ctl(ID_FLAT_SWATCH), nullptr, TRUE);
                }
            }
            return 0;
        }

        if (code == CBN_SELCHANGE && id == ID_PRESET) {
            int sel = (int)SendMessageW(ctl(ID_PRESET), CB_GETCURSEL, 0, 0);
            if (sel > 0) {
                SetWindowTextW(ctl(ID_THRESHOLD), kPresets[sel].thr);
                SetWindowTextW(ctl(ID_WINDOW), kPresets[sel].win);
                SetWindowTextW(ctl(ID_MINLEN), kPresets[sel].min);
                SendMessageW(ctl(ID_PRESET), CB_SETCURSEL, sel, 0);
            }
            return 0;
        }

        switch (id) {
        case ID_ADD:    openFileDialog(); return 0;
        case ID_CLEAR:  g_files.clear(); refreshList(); return 0;
        case ID_REMOVE: {
            HWND lb = ctl(ID_LIST);
            int n = (int)SendMessageW(lb, LB_GETSELCOUNT, 0, 0);
            if (n <= 0) return 0;
            std::vector<int> sel(n);
            SendMessageW(lb, LB_GETSELITEMS, n, (LPARAM)sel.data());
            std::sort(sel.rbegin(), sel.rend());
            for (int i : sel)
                if (i >= 0 && i < (int)g_files.size())
                    g_files.erase(g_files.begin() + i);
            refreshList();
            return 0;
        }
        case ID_UP_SWATCH:   pickColor(ID_UP_SWATCH, ID_UP_EDIT, g_colUp); return 0;
        case ID_DOWN_SWATCH: pickColor(ID_DOWN_SWATCH, ID_DOWN_EDIT, g_colDown); return 0;
        case ID_FLAT_SWATCH: pickColor(ID_FLAT_SWATCH, ID_FLAT_EDIT, g_colFlat); return 0;
        case ID_SAMEDIR:     syncOutputEnable(); return 0;
        case ID_BROWSE:      pickFolder(); return 0;
        case ID_3D:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                wchar_t b[32] = L"";
                GetWindowTextW(ctl(ID_3D), b, 32);
                const std::wstring v3 = b;
                EnableWindow(ctl(ID_3DSCALE),
                             v3 == L"by altitude" || v3 == L"fixed height");
            }
            return 0;

        case ID_MERGE: {
            BOOL on = SendMessageW(ctl(ID_MERGE), BM_GETCHECK, 0, 0)
                      == BST_CHECKED;
            EnableWindow(ctl(ID_MERGENAME), on);
            return 0;
        }

        case ID_SPLIT: {
            // Splitting by class already produces several files, so merging
            // has nothing to do; grey it out rather than quietly ignoring it.
            BOOL split = SendMessageW(ctl(ID_SPLIT), BM_GETCHECK, 0, 0)
                         == BST_CHECKED;
            EnableWindow(ctl(ID_MERGE), !split);
            EnableWindow(ctl(ID_MERGENAME), !split &&
                SendMessageW(ctl(ID_MERGE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }

        case ID_CONVERT:     doConvert(); return 0;

        case ID_ST_APPLY:    doApplyStyle(); return 0;

        case ID_ST_AUTO:
        case ID_ST_ONE:
            syncPerTrackEnable();
            return 0;

        case ID_ST_SWATCH: {
            CHOOSECOLORW cc = {};
            static COLORREF custom[16] = {0};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd;
            cc.lpCustColors = custom;
            cc.rgbResult = g_stColor;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_stColor = cc.rgbResult;
                SetWindowTextW(ctl(ID_ST_EDIT), hexOf(g_stColor).c_str());
                SendMessageW(ctl(ID_ST_ONE), BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(ctl(ID_ST_AUTO), BM_SETCHECK, BST_UNCHECKED, 0);
                syncPerTrackEnable();
                InvalidateRect(ctl(ID_ST_SWATCH), nullptr, TRUE);
            }
            return 0;
        }

        case ID_ST_3D:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                wchar_t b[32] = L"";
                GetWindowTextW(ctl(ID_ST_3D), b, 32);
                const std::wstring v3 = b;
                EnableWindow(ctl(ID_ST_3DSCALE),
                             v3 == L"by altitude" || v3 == L"fixed height");
            }
            return 0;

        case ID_ST_MERGE: {
            BOOL on = SendMessageW(ctl(ID_ST_MERGE), BM_GETCHECK, 0, 0)
                      == BST_CHECKED;
            EnableWindow(ctl(ID_ST_MERGENAME), on);
            EnableWindow(ctl(ID_ST_SUFFIX), !on);   // suffix is per-file only
            syncPerTrackEnable();
            return 0;
        }

        case ID_ST_SAMEDIR: {
            BOOL same = SendMessageW(ctl(ID_ST_SAMEDIR), BM_GETCHECK, 0, 0)
                        == BST_CHECKED;
            EnableWindow(ctl(ID_ST_OUTDIR), !same);
            EnableWindow(ctl(ID_ST_BROWSE), !same);
            return 0;
        }

        case ID_ST_BROWSE: {
            wchar_t path[MAX_PATH] = L"";
            BROWSEINFOW bi = {};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"Where should the styled files go?";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                if (SHGetPathFromIDListW(pidl, path))
                    SetWindowTextW(ctl(ID_ST_OUTDIR), path);
                CoTaskMemFree(pidl);
            }
            return 0;
        }
        case ID_RESET:       doReset(); return 0;
        }
        break;
    }

    case WM_WORK_LOG: {
        auto *pair = (std::pair<std::wstring, COLORREF> *)lp;
        logLine(pair->first, pair->second);
        delete pair;
        return 0;
    }

    case WM_WORK_STEP:
        SendMessageW(ctl(ID_PROGRESS), PBM_SETPOS, (WPARAM)wp, 0);
        return 0;

    case WM_WORK_DONE: {
        int ok = (int)wp, fail = (int)lp;
        wchar_t m[128];
        logLine(L"");
        // After a merge "1 file styled" would undersell it: the merged file
        // already got its own line, so just say it is done.
        const bool merged = g_tab == TAB_STYLE &&
            SendMessageW(ctl(ID_ST_MERGE), BM_GETCHECK, 0, 0) == BST_CHECKED;
        const wchar_t *verb = g_tab == TAB_SLOPE ? L"converted" : L"styled";
        if (fail == 0 && merged) {
            logLine(L"Finished.", CLR_OK);
        } else if (fail == 0) {
            wsprintfW(m, L"Finished: %d file%s %s.", ok,
                      ok == 1 ? L"" : L"s", verb);
            logLine(m, CLR_OK);
        } else {
            wsprintfW(m, L"Finished: %d %s, %d failed.", ok, verb, fail);
            logLine(m, CLR_ERR);
        }
        // Leave the bar full and the next run looks like it starts finished.
        SendMessageW(ctl(ID_PROGRESS), PBM_SETPOS, 0, 0);
        g_running = false;
        EnableWindow(ctl(ID_CONVERT), TRUE);
        EnableWindow(ctl(ID_ST_APPLY), TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        // The minimum height must leave the log room to shrink, otherwise a
        // 1366x768 screen cannot show the whole window at all: the bottom,
        // with the Convert button, would sit under the taskbar.
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = kClientW + 16;
        mm->ptMinTrackSize.y = kClientH + 48 - (kLogH - kLogMinH);
        return 0;
    }

    case WM_SIZE: {
        // Only the log and the progress bar follow the height: everything
        // above them is a fixed grid of group boxes.
        if (wp == SIZE_MINIMIZED) return 0;
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        HWND prog = ctl(ID_PROGRESS);
        HWND log  = ctl(ID_LOG);
        if (!prog || !log) return 0;

        const int logY = g_logTop;
        int logH = h - logY - kMargin;
        if (logH < kLogMinH) logH = kLogMinH;

        SetWindowPos(prog, nullptr, kMargin, logY - 18, w - 2 * kMargin, 10,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(log, nullptr, kMargin, logY, w - 2 * kMargin, logH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_CLOSE:
        if (g_running &&
            MessageBoxW(hwnd, L"A conversion is still running. Quit anyway?",
                        L"gpx-slope-colors", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    LoadLibraryW(L"Msftedit.dll");        // rich edit, for coloured log text

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    g_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontBold = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontTitle = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    g_bgBrush = CreateSolidBrush(CLR_BG);
    g_panelBrush = CreateSolidBrush(CLR_PANEL);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = L"GpxSlopeColorsWnd";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    RECT want = {0, 0, kClientW, kClientH};
    AdjustWindowRectEx(&want, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES);
    int winW = want.right - want.left;
    int winH = want.bottom - want.top;

    // Never open taller than the screen. On a 1366x768 laptop the full layout
    // does not fit, and a window whose bottom is hidden behind the taskbar
    // cannot be reached: the Convert button and the log would be lost. Clamp
    // to the work area (which already excludes the taskbar) and let the log
    // shrink in WM_SIZE.
    RECT work = {0, 0, 0, 0};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const int availH = work.bottom - work.top;
        const int availW = work.right - work.left;
        if (winH > availH) winH = availH;
        if (winW > availW) winW = availW;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES, wc.lpszClassName,
        L"gpx-slope-colors", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winW, winH,
        nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Files given on the command line (drag onto the .exe icon, or "Open
    // with") land in the list straight away.
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        int added = 0, skipped = 0;
        for (int i = 1; i < argc; ++i) addPath(argv[i], added, skipped);
        LocalFree(argv);
        if (added) {
            refreshList();
            wchar_t m[64];
            wsprintfW(m, L"Added %d file%s.", added, added == 1 ? L"" : L"s");
            logLine(m, CLR_MUTED);
        }
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
