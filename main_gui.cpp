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

using namespace slope;

// ----------------------------------------------------------------- ids
enum {
    ID_LIST = 1001, ID_ADD, ID_REMOVE, ID_CLEAR,
    ID_UP_SWATCH, ID_UP_EDIT, ID_DOWN_SWATCH, ID_DOWN_EDIT,
    ID_FLAT_SWATCH, ID_FLAT_EDIT, ID_NOFLAT,
    ID_PRESET, ID_THRESHOLD, ID_WINDOW, ID_MINLEN,
    ID_WIDTH, ID_ARROWS, ID_WAYPOINTS,
    ID_SUFFIX, ID_SAMEDIR, ID_OUTDIR, ID_BROWSE, ID_SPLIT,
    ID_CONVERT, ID_RESET, ID_LOG, ID_PROGRESS, ID_STATUS
};

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
const int kClientH = 736;

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

HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style,
        int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, style | WS_CHILD | WS_VISIBLE,
                             x, y, w, h, g_main, (HMENU)(INT_PTR)id,
                             g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

HWND ctl(int id) { return GetDlgItem(g_main, id); }

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

    for (const std::wstring &f : job->files) {
        Stats st;
        std::string err;
        if (process(toU8(f), job->opt, st, err)) {
            ++ok;
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

void buildUi(HWND hwnd) {
    const int M = kMargin;
    const int W = kClientW;
    int y = 12;

    HWND t = mk(L"STATIC", L"Colour GPX tracks by slope", 0, M, y, 520, 26, -1);
    SendMessageW(t, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
    y += 26;
    mk(L"STATIC", L"Uphill one colour, downhill another, following the "
                  L"direction you ride.", 0, M, y, 640, 18, -1);
    y += 26;

    // ---- files
    mk(L"BUTTON", L" Files ", BS_GROUPBOX, M, y, W - 2 * M, 150, -1);
    mk(L"STATIC", L"Drop GPX files here, or use Add files", 0,
       M + 12, y + 20, 340, 18, -1);
    mk(L"BUTTON", L"Add files\x2026", BS_PUSHBUTTON, W - M - 270, y + 16, 84, 25, ID_ADD);
    mk(L"BUTTON", L"Remove", BS_PUSHBUTTON, W - M - 180, y + 16, 80, 25, ID_REMOVE);
    mk(L"BUTTON", L"Clear", BS_PUSHBUTTON, W - M - 94, y + 16, 80, 25, ID_CLEAR);
    mk(L"LISTBOX", L"", LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT | WS_VSCROLL |
       WS_BORDER, M + 12, y + 46, W - 2 * M - 24, 92, ID_LIST);
    y += 162;

    const int colW = (W - 2 * M - 12) / 2;
    const int leftX = M, rightX = M + colW + 12;

    // ---- colours
    mk(L"BUTTON", L" Colours ", BS_GROUPBOX, leftX, y, colW, 132, -1);
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
    mk(L"BUTTON", L" Detection ", BS_GROUPBOX, rightX, y, colW, 132, -1);
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
    y += 144;

    // ---- appearance
    mk(L"BUTTON", L" Appearance ", BS_GROUPBOX, leftX, y, colW, 132, -1);
    mk(L"STATIC", L"Line width", 0, leftX + 12, y + 26, 76, 18, -1);
    HWND wcb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                  leftX + 96, y + 22, 100, 240, ID_WIDTH);
    const wchar_t *widths[] = {L"thin", L"medium", L"bold", L"4", L"8",
                               L"12", L"16", L"24"};
    for (const wchar_t *w : widths)
        SendMessageW(wcb, CB_ADDSTRING, 0, (LPARAM)w);
    SendMessageW(wcb, CB_SETCURSEL, 7, 0);
    mk(L"BUTTON", L"Show direction arrows", BS_AUTOCHECKBOX,
       leftX + 12, y + 52, 220, 20, ID_ARROWS);
    mk(L"BUTTON", L"Keep waypoints", BS_AUTOCHECKBOX,
       leftX + 12, y + 76, 220, 20, ID_WAYPOINTS);
    SendMessageW(ctl(ID_ARROWS), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(ctl(ID_WAYPOINTS), BM_SETCHECK, BST_CHECKED, 0);

    // ---- output
    mk(L"BUTTON", L" Output ", BS_GROUPBOX, rightX, y, colW, 132, -1);
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

    // Layout choices, on their own row so nothing overlaps the folder box.
    mk(L"BUTTON", L"One file per class", BS_AUTOCHECKBOX,
       rightX + 12, y + 104, 200, 20, ID_SPLIT);
    y += 144;

    // ---- actions
    mk(L"BUTTON", L"Convert", BS_DEFPUSHBUTTON, M, y, 120, 34, ID_CONVERT);
    mk(L"BUTTON", L"Reset settings", BS_PUSHBUTTON, M + 132, y, 120, 34, ID_RESET);
    mk(L"STATIC", L"No files yet.", 0, M + 266, y + 10, 280, 18, ID_STATUS);
    y += 42;

    CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
                    M, y, W - 2 * M, 10, hwnd, (HMENU)(INT_PTR)ID_PROGRESS,
                    g_inst, nullptr);
    y += 18;

    HWND log = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                               ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                               M, y, W - 2 * M, 150, hwnd,
                               (HMENU)(INT_PTR)ID_LOG, g_inst, nullptr);
    SendMessageW(log, WM_SETFONT, (WPARAM)g_fontMono, TRUE);
    SendMessageW(log, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0xfb, 0xfb, 0xfc));

    logLine(L"Ready. Drop GPX files above, or use Add files.", CLR_MUTED);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        buildUi(hwnd);
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

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
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
        case ID_CONVERT:     doConvert(); return 0;
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
        if (fail == 0) {
            wsprintfW(m, L"Finished: %d file%s converted.", ok,
                      ok == 1 ? L"" : L"s");
            logLine(m, CLR_OK);
        } else {
            wsprintfW(m, L"Finished: %d converted, %d failed.", ok, fail);
            logLine(m, CLR_ERR);
        }
        g_running = false;
        EnableWindow(ctl(ID_CONVERT), TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = kClientW + 16;
        mm->ptMinTrackSize.y = kClientH + 48;
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

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES, wc.lpszClassName,
        L"gpx-slope-colors", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        want.right - want.left, want.bottom - want.top,
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
