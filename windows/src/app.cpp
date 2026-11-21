#include "app.h"

#include <dbt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>

#include "imaging.h"

namespace sv {

static const wchar_t* kTitle = L"WebCam and TV Smart Vision";
static const wchar_t* kTestLabel = L"Sorgente di prova (barre + tono)";

App& App::get() {
    static App app;
    return app;
}

// ------------------------------------------------------------------ utilità

std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, 0);
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}

void comboAdd(HWND combo, const std::wstring& text) { SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str()); }
int comboSel(HWND combo) { return (int)SendMessageW(combo, CB_GETCURSEL, 0, 0); }

std::wstring comboText(HWND combo) {
    int i = comboSel(combo);
    if (i < 0) return L"";
    int n = (int)SendMessageW(combo, CB_GETLBTEXTLEN, i, 0);
    std::wstring s((size_t)n + 1, 0);
    SendMessageW(combo, CB_GETLBTEXT, i, (LPARAM)&s[0]);
    s.resize((size_t)n);
    return s;
}

bool comboSelectText(HWND combo, const std::wstring& text) {
    int i = (int)SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)text.c_str());
    if (i < 0) return false;
    SendMessageW(combo, CB_SETCURSEL, i, 0);
    return true;
}

static std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static std::wstring durationText(double seconds) {
    int s = (int)seconds;
    return format(L"%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
}

// ------------------------------------------------------- procedure finestra

static LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM w, LPARAM l) { return App::get().wndProc(h, m, w, l); }
static LRESULT CALLBACK videoProcS(HWND h, UINT m, WPARAM w, LPARAM l) { return App::get().videoProc(h, m, w, l); }
static LRESULT CALLBACK extProcS(HWND h, UINT m, WPARAM w, LPARAM l) { return App::get().extProc(h, m, w, l); }

// Pagina della barra laterale: inoltra i comandi alla finestra principale e gestisce lo scorrimento verticale.
static LRESULT CALLBACK pageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_COMMAND:
        case WM_HSCROLL:
        case WM_NOTIFY:
            return SendMessageW(App::get().hwnd_, m, w, l);
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)w;
            SetBkMode(dc, TRANSPARENT);
            if (GetDlgCtrlID((HWND)l) == IDC_NOTE) SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_SIZE:
        case WM_VSCROLL:
        case WM_MOUSEWHEEL: {
            int content = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
            RECT rc;
            GetClientRect(h, &rc);
            SCROLLINFO si = { sizeof si, SIF_ALL, 0, 0, 0, 0, 0 };
            GetScrollInfo(h, SB_VERT, &si);
            int old = si.nPos, pos = old;
            if (m == WM_VSCROLL) {
                switch (LOWORD(w)) {
                    case SB_LINEUP: pos -= 24; break;
                    case SB_LINEDOWN: pos += 24; break;
                    case SB_PAGEUP: pos -= rc.bottom; break;
                    case SB_PAGEDOWN: pos += rc.bottom; break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                }
            } else if (m == WM_MOUSEWHEEL) {
                pos -= GET_WHEEL_DELTA_WPARAM(w) / 2;
            }
            int maxPos = std::max(0, content - (int)rc.bottom);
            pos = std::max(0, std::min(pos, maxPos));
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin = 0;
            si.nMax = content > rc.bottom ? content - 1 : 0;
            si.nPage = rc.bottom;
            si.nPos = pos;
            SetScrollInfo(h, SB_VERT, &si, TRUE);
            if (pos != old) ScrollWindowEx(h, 0, old - pos, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- creazione

void App::createFonts() {
    NONCLIENTMETRICSW ncm;
    ZeroMemory(&ncm, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
        ncm.lfMessageFont.lfHeight = -MulDiv(8, dpi_, 72);
        wcsncpy(ncm.lfMessageFont.lfFaceName, L"Tahoma", LF_FACESIZE - 1);
    }
    LOGFONTW lf = ncm.lfMessageFont;
    font_ = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    fontBold_ = CreateFontIndirectW(&lf);
    lf.lfHeight = lf.lfHeight * 5 / 4;
    fontBig_ = CreateFontIndirectW(&lf);
}

void App::createMenu() {
    menu_ = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_SNAPSHOT, L"&Istantanea PNG\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_RECORD, L"Avvia/ferma &registrazione\tCtrl+R");
    AppendMenuW(file, MF_STRING, IDM_OPENFOLDER, L"&Apri cartella registrazioni");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"&Esci\tAlt+F4");
    HMENU src = CreatePopupMenu();
    AppendMenuW(src, MF_STRING, IDM_STARTSTOP, L"&Avvia/ferma sorgente\tF6");
    AppendMenuW(src, MF_STRING, IDM_REFRESH, L"Ce&rca dispositivi\tF5");
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDM_FULLSCREEN, L"&Schermo intero\tF11");
    AppendMenuW(view, MF_STRING, IDM_EXTWINDOW, L"&Finestra esterna");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_GUIDE, L"&Guida (PDF)\tF1");
    AppendMenuW(help, MF_STRING, IDM_DIAG, L"&Diagnostica dispositivi…");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&Informazioni…");
    AppendMenuW(menu_, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(menu_, MF_POPUP, (UINT_PTR)src, L"&Sorgente");
    AppendMenuW(menu_, MF_POPUP, (UINT_PTR)view, L"&Visualizza");
    AppendMenuW(menu_, MF_POPUP, (UINT_PTR)help, L"&?");
    ACCEL acc[] = {
        { FCONTROL | FVIRTKEY, 'S', IDM_SNAPSHOT }, { FCONTROL | FVIRTKEY, 'R', IDM_RECORD },
        { FVIRTKEY, VK_F5, IDM_REFRESH }, { FVIRTKEY, VK_F6, IDM_STARTSTOP }, { FVIRTKEY, VK_F11, IDM_FULLSCREEN },
        { FVIRTKEY, VK_F1, IDM_GUIDE }, { FVIRTKEY, VK_ESCAPE, IDM_ESCAPE },
    };
    accel_ = CreateAcceleratorTableW(acc, sizeof acc / sizeof acc[0]);
}

HWND App::mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, inst_, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)(id == IDC_HEADING ? fontBold_ : font_), TRUE);
    return c;
}

bool App::create(HINSTANCE inst, int show, const std::wstring& selfTestDir, bool selfTestDevice) {
    inst_ = inst;
    selfTestDir_ = selfTestDir;
    selfTest_ = !selfTestDir.empty();
    selfTestDevice_ = selfTestDevice;
    settings_.init(selfTest_ ? selfTestDir + L"\\config" : L"");

    HDC dc = GetDC(nullptr);
    dpi_ = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    if (dpi_ < 96) dpi_ = 96;
    createFonts();
    createMenu();

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = mainProc;
    wc.lpszClassName = L"SVMain";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = pageProc;
    wc.lpszClassName = L"SVPage";
    RegisterClassExW(&wc);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = videoProcS;
    wc.lpszClassName = L"SVVideo";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = extProcS;
    wc.lpszClassName = L"SVExt";
    RegisterClassExW(&wc);

    // Finestra principale di 800x600 (ridimensionabile), centrata sullo schermo di lavoro.
    int ww = S(800), wh = S(600);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int wx = wa.left + std::max(0, (int)(wa.right - wa.left - ww) / 2), wy = wa.top + std::max(0, (int)(wa.bottom - wa.top - wh) / 2);
    hwnd_ = CreateWindowExW(0, L"SVMain", kTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh, nullptr, menu_, inst, nullptr);
    if (!hwnd_) return false;
    createChildren();
    engine_.setNotify(hwnd_, WM_APP_FRAME, WM_APP_GRAPH);
    layout();

    // Ripristino delle impostazioni
    suppress_ = true;
    SendMessageW(item(PAGE_SOURCE, IDC_SRC_ORIENT), CB_SETCURSEL, settings_.getInt(L"orient", 0), 0);
    engine_.setTransform(settings_.getInt(L"orient", 0));
    SendMessageW(item(PAGE_SOURCE, IDC_SRC_GAIN), TBM_SETPOS, TRUE, settings_.getInt(L"gain", 100));
    engine_.setGain(settings_.getInt(L"gain", 100) / 100.0f);
    suppress_ = false;
    refreshDevices(false);

    std::wstring saved = settings_.get(L"source", L"test");
    bool missing = false;
    suppress_ = true;
    HWND combo = item(PAGE_SOURCE, IDC_SRC_COMBO);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    if (saved != L"test") {
        missing = true;
        for (size_t i = 0; i < devices_.size(); ++i)
            if (devices_[i].moniker == saved) { SendMessageW(combo, CB_SETCURSEL, (WPARAM)i + 1, 0); missing = false; }
    }
    suppress_ = false;
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);
    if (missing) {
        // La periferica usata l'ultima volta non c'è: niente avvio automatico della sorgente di prova.
        std::wstring name = settings_.get(L"source_name", L"webcam");
        updateUi();
        placeholderTitle_ = L"Periferica non rilevata: " + name;
        setStatus(L"La periferica usata l'ultima volta (" + name + L") non è collegata. Collegala e premi Cerca dispositivi (F5).");
        InvalidateRect(video_, nullptr, FALSE);
    } else {
        onSourceSelected(true);
    }
    SetTimer(hwnd_, TIMER_UI, 150, nullptr);
    if (selfTest_) {
        setLogFile(selfTestDir_ + L"\\selftest.log");
        log(format(L"WebCam and TV Smart Vision %ls (%ls) - auto-test", widen(SV_VERSION).c_str(), widen(SV_TARGET).c_str()));
        selfTestNext_ = GetTickCount() + 1500;
        SetTimer(hwnd_, TIMER_SELFTEST, 100, nullptr);
    }
    return true;
}

int App::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd_, accel_, &msg)) continue;
        if (IsDialogMessageW(hwnd_, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return exitCode_;
}

void App::createChildren() {
    tbStart_ = mk(hwnd_, L"BUTTON", L"Avvia", BS_PUSHBUTTON | WS_TABSTOP, 6, 5, 80, 26, IDC_TB_START);
    tbSnap_ = mk(hwnd_, L"BUTTON", L"Istantanea", BS_PUSHBUTTON | WS_TABSTOP, 90, 5, 84, 26, IDC_TB_SNAP);
    tbRec_ = mk(hwnd_, L"BUTTON", L"Registra", BS_PUSHBUTTON | WS_TABSTOP, 178, 5, 96, 26, IDC_TB_REC);
    tbInfo_ = mk(hwnd_, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE | SS_NOPREFIX, 282, 5, 190, 26, IDC_TB_INFO);
    video_ = CreateWindowExW(0, L"SVVideo", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, hwnd_, nullptr, inst_, nullptr);
    phRefresh_ = mk(video_, L"BUTTON", L"Cerca dispositivi", BS_PUSHBUTTON, 0, 0, 130, 28, IDC_PH_REFRESH);
    phStart_ = mk(video_, L"BUTTON", L"Avvia sorgente", BS_PUSHBUTTON, 0, 0, 120, 28, IDC_PH_START);
    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd_, nullptr, inst_, nullptr);
    SendMessageW(status_, WM_SETFONT, (WPARAM)font_, TRUE);
    tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 10, 10, hwnd_, nullptr, inst_, nullptr);
    SendMessageW(tab_, WM_SETFONT, (WPARAM)font_, TRUE);
    const wchar_t* names[PAGE_COUNT] = { L"Sorgente", L"Webcam", L"TV", L"Rec", L"Uscite" };
    for (int i = 0; i < PAGE_COUNT; ++i) {
        TCITEMW ti;
        ZeroMemory(&ti, sizeof ti);
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(names[i]);
        SendMessageW(tab_, TCM_INSERTITEMW, i, (LPARAM)&ti);
        pages_[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"SVPage", L"", WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 10, 10, hwnd_, nullptr, inst_, nullptr);
    }
    buildSourcePage(pages_[PAGE_SOURCE]);
    buildWebcamPage(pages_[PAGE_WEBCAM]);
    buildTvPage(pages_[PAGE_TV]);
    buildRecPage(pages_[PAGE_REC]);
    buildOutPage(pages_[PAGE_OUT]);
    showPage(PAGE_SOURCE);
}

void App::setPageContentHeight(HWND page, int logicalHeight) {
    SetWindowLongPtrW(page, GWLP_USERDATA, S(logicalHeight));
    SendMessageW(page, WM_SIZE, 0, 0);
}

void App::showPage(int index) {
    SendMessageW(tab_, TCM_SETCURSEL, index, 0);
    for (int i = 0; i < PAGE_COUNT; ++i) ShowWindow(pages_[i], i == index ? SW_SHOW : SW_HIDE);
    if (index >= 0 && index < PAGE_COUNT) SetWindowPos(pages_[index], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void App::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int cw = rc.right, ch = rc.bottom;
    if (fullscreen_) {
        MoveWindow(video_, 0, 0, cw, ch, TRUE);
        return;
    }
    SendMessageW(status_, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(status_, &sr);
    int statusH = sr.bottom - sr.top;
    int parts[2] = { cw - S(170), -1 };
    SendMessageW(status_, SB_SETPARTS, 2, (LPARAM)parts);
    SendMessageW(status_, SB_SETTEXTW, 1 | SBT_OWNERDRAW, 0);
    int tbH = S(36), sideW = std::min(S(308), cw / 2 + S(60));
    int videoW = std::max(S(100), cw - sideW);
    MoveWindow(tbInfo_, S(282), S(5), std::max(0, videoW - S(288)), S(26), TRUE);
    MoveWindow(video_, 0, tbH, videoW, std::max(0, ch - tbH - statusH), TRUE);
    MoveWindow(tab_, videoW, 0, cw - videoW, std::max(0, ch - statusH), TRUE);
    RECT tr = { 0, 0, cw - videoW, std::max(0, ch - statusH) };
    SendMessageW(tab_, TCM_ADJUSTRECT, FALSE, (LPARAM)&tr);
    for (int i = 0; i < PAGE_COUNT; ++i)
        MoveWindow(pages_[i], videoW + tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
}

// --------------------------------------------------------------- dispositivi

int App::selectedDeviceIndex() const {
    int sel = comboSel(item(PAGE_SOURCE, IDC_SRC_COMBO));
    return sel <= 0 ? -1 : sel - 1;
}

std::wstring App::sourceName() const {
    int d = selectedDeviceIndex();
    if (d >= 0 && d < (int)devices_.size()) return devices_[d].name;
    return kTestLabel;
}

std::wstring App::detectedText() const {
    if (devices_.empty()) return L"Nessuna periferica di cattura rilevata";
    if (devices_.size() == 1) return L"Periferica di cattura: " + devices_[0].name;
    std::wstring t = format(L"%d periferiche di cattura: ", (int)devices_.size());
    for (size_t i = 0; i < devices_.size(); ++i) t += (i ? L", " : L"") + devices_[i].name;
    return t;
}

void App::updateDeviceBar() {
    std::wstring t;
    if (devices_.empty()) t = L"Nessuna webcam rilevata.\r\nCollegala e premi Aggiorna.";
    else if (devices_.size() == 1) t = devices_[0].name;
    else {
        t = format(L"%d periferiche: ", (int)devices_.size());
        for (size_t i = 0; i < devices_.size(); ++i) t += (i ? L", " : L"") + devices_[i].name;
    }
    HWND bar = item(PAGE_SOURCE, IDC_SRC_DEVBAR);
    SendMessageW(bar, WM_SETFONT, (WPARAM)(devices_.empty() ? font_ : fontBold_), TRUE);
    SetWindowTextW(bar, t.c_str());
}

void App::refreshDevices(bool hotplug) {
    std::vector<std::wstring> before;
    for (auto& d : devices_) before.push_back(d.moniker);
    std::wstring current = selectedDeviceIndex() >= 0 && selectedDeviceIndex() < (int)devices_.size() ? devices_[selectedDeviceIndex()].moniker : L"";
    bool wasTest = comboSel(item(PAGE_SOURCE, IDC_SRC_COMBO)) == 0;
    devices_ = listVideoDevices();
    audioIns_ = listAudioInputs();
    audioOuts_ = listAudioOutputs();

    suppress_ = true;
    HWND combo = item(PAGE_SOURCE, IDC_SRC_COMBO);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    comboAdd(combo, kTestLabel);
    int keep = wasTest ? 0 : -1, fresh = -1;
    for (size_t i = 0; i < devices_.size(); ++i) {
        comboAdd(combo, devices_[i].name);
        if (devices_[i].moniker == current) keep = (int)i + 1;
        if (std::find(before.begin(), before.end(), devices_[i].moniker) == before.end() && fresh < 0) fresh = (int)i + 1;
    }
    fillAudioCombos();
    suppress_ = false;
    updateDeviceBar();

    bool lost = !current.empty() && keep < 0;
    if (lost && engine_.running()) stopSource(L"Periferica scollegata: " + settings_.get(L"source_name", L""));
    if (hotplug && fresh > 0 && !engine_.running() && !engine_.recorder().active()) {
        // Webcam appena collegata e nessuna sorgente attiva: selezionala e avviala.
        SendMessageW(combo, CB_SETCURSEL, fresh, 0);
        setStatus(L"Nuova periferica rilevata: " + devices_[fresh - 1].name);
        onSourceSelected(true);
        return;
    }
    SendMessageW(combo, CB_SETCURSEL, keep >= 0 ? keep : 0, 0);
    if (lost) {
        engine_.capture().close();
        rebuildWebcamControls();
        rebuildTvPage();
        fillFormatCombo();
    }
    updateUi();
    if (hotplug || !before.empty() || !devices_.empty()) setStatus(detectedText());
}

void App::fillAudioCombos() {
    HWND in = item(PAGE_SOURCE, IDC_SRC_AUDIO);
    std::wstring prev = comboSel(in) >= 0 ? comboText(in) : settings_.get(L"audio", L"");
    SendMessageW(in, CB_RESETCONTENT, 0, 0);
    comboAdd(in, L"Automatico (microfono della webcam)");
    comboAdd(in, L"Nessun audio");
    for (auto& a : audioIns_) comboAdd(in, a.name);
    if (!comboSelectText(in, prev)) SendMessageW(in, CB_SETCURSEL, 0, 0);

    HWND out = item(PAGE_OUT, IDC_OUT_ADEV);
    std::wstring prevOut = comboSel(out) >= 0 ? comboText(out) : settings_.get(L"audio_out", L"");
    SendMessageW(out, CB_RESETCONTENT, 0, 0);
    comboAdd(out, L"Uscita predefinita di Windows");
    for (auto& a : audioOuts_) comboAdd(out, a.name);
    if (!comboSelectText(out, prevOut)) SendMessageW(out, CB_SETCURSEL, 0, 0);
}

void App::fillFormatCombo() {
    HWND combo = item(PAGE_SOURCE, IDC_SRC_FORMAT);
    suppress_ = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    comboAdd(combo, L"Automatico");
    int d = selectedDeviceIndex();
    if (d >= 0 && engine_.capture().isOpen()) {
        for (auto& f : engine_.capture().formats()) comboAdd(combo, f.label());
        std::wstring saved = settings_.get((L"format:" + devices_[d].name).c_str(), L"Automatico");
        if (!comboSelectText(combo, saved)) SendMessageW(combo, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
    EnableWindow(combo, d >= 0);
    suppress_ = false;
}

bool App::openSelectedDevice(std::wstring& err) {
    int d = selectedDeviceIndex();
    if (d < 0 || d >= (int)devices_.size()) return false;
    DsCapture& cap = engine_.capture();
    if (cap.isOpen() && cap.device().moniker == devices_[d].moniker) return true;
    return cap.open(devices_[d], err);
}

void App::onSourceSelected(bool autoStart) {
    if (engine_.recorder().active()) return;
    if (engine_.running()) engine_.stop();
    int d = selectedDeviceIndex();
    std::wstring err;
    if (d < 0) {
        engine_.capture().close();
        settings_.set(L"source", L"test");
        settings_.set(L"source_name", kTestLabel);
    } else {
        settings_.set(L"source", devices_[d].moniker);
        settings_.set(L"source_name", devices_[d].name);
        if (!openSelectedDevice(err)) {
            fillFormatCombo();
            rebuildWebcamControls();
            rebuildTvPage();
            updateUi();
            showError(err);
            return;
        }
    }
    fillFormatCombo();
    rebuildWebcamControls();
    rebuildTvPage();
    if (d >= 0 && !selfTest_) showPage(engine_.capture().hasTuner() ? PAGE_TV : PAGE_WEBCAM);
    if (autoStart) startSelected();
    else updateUi();
}

int App::resolveAudioDevice() const {
    int sel = comboSel(item(PAGE_SOURCE, IDC_SRC_AUDIO));
    if (sel == 1) return Engine::AudioNone;
    if (sel >= 2 && sel - 2 < (int)audioIns_.size()) return audioIns_[sel - 2].id;
    // Automatico: cerca l'ingresso audio che porta il nome della webcam (es. "Microfono (HD Pro Webcam C920)").
    int d = selectedDeviceIndex();
    if (d >= 0) {
        std::wstring v = lower(devices_[d].name), cur;
        std::vector<std::wstring> tokens;
        for (wchar_t c : v + L" ") {
            if (iswalnum(c)) cur += c;
            else { if (cur.size() >= 3) tokens.push_back(cur); cur.clear(); }
        }
        static const wchar_t* generic[] = { L"usb", L"camera", L"webcam", L"video", L"audio", L"pro", L"device", L"integrated", L"capture" };
        int best = -1, bestScore = 0;
        for (auto& a : audioIns_) {
            std::wstring an = lower(a.name);
            int score = 0;
            for (auto& t : tokens) {
                bool g = false;
                for (auto* w : generic) g = g || t == w;
                if (!g && an.find(t) != std::wstring::npos) score += (int)t.size();
            }
            if (score > bestScore) { bestScore = score; best = a.id; }
        }
        if (bestScore >= 4) return best;
    }
    return Engine::AudioDefault;
}

void App::applyAudioInput() {
    int dev = resolveAudioDevice();
    if (!engine_.setAudioInput(dev) && dev != Engine::AudioNone)
        setStatus(L"Ingresso audio non disponibile: la registrazione sarà senza audio.");
    if (engine_.bridgeActive()) toggleBridge(true);          // riapre il ponte con la nuova frequenza
}

void App::startSelected() {
    if (engine_.recorder().active()) { showError(L"Ferma la registrazione prima di cambiare sorgente."); return; }
    std::wstring err;
    int d = selectedDeviceIndex();
    engine_.setAudioInput(resolveAudioDevice());
    bool ok;
    if (d < 0) {
        ok = engine_.startTest(err);
    } else {
        if (!openSelectedDevice(err)) { updateUi(); showError(err); return; }
        HWND fc = item(PAGE_SOURCE, IDC_SRC_FORMAT);
        int fsel = comboSel(fc);
        int fmt = fsel <= 0 ? engine_.capture().bestFormat() : fsel - 1;
        settings_.set((L"format:" + devices_[d].name).c_str(), comboText(fc));
        HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        ok = engine_.startDevice(fmt, err);
        SetCursor(old);
    }
    if (!ok) {
        updateUi();
        showError(err);
        return;
    }
    applyAudioInput();
    if (SendMessageW(item(PAGE_OUT, IDC_OUT_AON), BM_GETCHECK, 0, 0) == BST_CHECKED) toggleBridge(true);
    setStatus(L"In riproduzione: " + sourceName());
    updateUi();
}

void App::stopSource(const std::wstring& status) {
    if (engine_.recorder().active()) toggleRecording();
    engine_.stop();
    setStatus(status);
    updateUi();
}

void App::updateUi() {
    bool run = engine_.running(), rec = engine_.recorder().active();
    SetWindowTextW(tbStart_, run ? L"Ferma" : L"Avvia");
    SetWindowTextW(tbRec_, rec ? L"Ferma reg." : L"Registra");
    SetWindowTextW(item(PAGE_REC, IDC_REC_BUTTON), rec ? L"Ferma registrazione" : L"Avvia registrazione");
    EnableWindow(tbSnap_, run);
    EnableWindow(tbRec_, run);
    EnableWindow(item(PAGE_REC, IDC_REC_BUTTON), run);
    EnableWindow(item(PAGE_SOURCE, IDC_SRC_COMBO), !rec);
    EnableWindow(item(PAGE_SOURCE, IDC_SRC_FORMAT), !rec && selectedDeviceIndex() >= 0);
    EnableWindow(item(PAGE_SOURCE, IDC_SRC_ORIENT), !rec);
    for (int id : { IDC_REC_MP4, IDC_REC_OGG, IDC_REC_VKBPS, IDC_REC_AKBPS, IDC_REC_AUDIO, IDC_REC_FOLDER, IDC_REC_BROWSE, IDC_REC_PREFIX })
        EnableWindow(item(PAGE_REC, id), !rec);
    std::wstring info;
    if (run) {
        info = sourceName();
        if (engine_.width() > 0) info += format(L"  ·  %d×%d  ·  %.3g fps", engine_.width(), engine_.height(), engine_.measuredFps() > 1 ? engine_.measuredFps() : engine_.nominalFps());
    } else {
        info = L"Nessuna sorgente attiva";
        placeholderTitle_ = devices_.empty() ? L"Nessuna periferica di cattura rilevata" : detectedText();
        placeholderText_ = devices_.empty()
            ? L"Collega la webcam (o la scheda TV) e premi «Cerca dispositivi». Le webcam UVC come la Logitech C920 non richiedono driver aggiuntivi. In alternativa scegli la sorgente di prova nella scheda Sorgente."
            : L"Premi «Avvia sorgente» per vedere l'anteprima, oppure scegli un'altra sorgente nella scheda Sorgente.";
    }
    SetWindowTextW(tbInfo_, info.c_str());
    ShowWindow(phRefresh_, run ? SW_HIDE : SW_SHOW);
    ShowWindow(phStart_, run ? SW_HIDE : SW_SHOW);
    SendMessageW(video_, WM_SIZE, 0, 0);
    InvalidateRect(video_, nullptr, FALSE);
    if (ext_) InvalidateRect(ext_, nullptr, FALSE);
    CheckMenuItem(menu_, IDM_EXTWINDOW, ext_ ? MF_CHECKED : MF_UNCHECKED);
}

std::wstring App::diagnostics() {
    std::wstring t = format(L"WebCam and TV Smart Vision %ls (%ls)\r\n", widen(SV_VERSION).c_str(), widen(SV_TARGET).c_str());
    OSVERSIONINFOW vi;
    ZeroMemory(&vi, sizeof vi);
    vi.dwOSVersionInfoSize = sizeof vi;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GetVersionExW(&vi);
#pragma GCC diagnostic pop
    t += format(L"Windows %lu.%lu build %lu, schermo a %d dpi\r\n\r\n", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber, dpi_);
    auto devs = listVideoDevices();
    t += format(L"Periferiche di cattura video (DirectShow): %d\r\n", (int)devs.size());
    for (auto& d : devs) {
        t += L"  - " + d.name + L"\r\n";
        DsCapture cap;
        std::wstring err;
        if (cap.open(d, err)) {
            for (auto& f : cap.formats()) t += L"        " + f.label() + L"\r\n";
            auto ctl = cap.controls();
            if (!ctl.empty()) {
                t += L"        controlli:";
                for (auto& c : ctl) t += L" " + c.name + format(L"[%ld..%ld]", c.min, c.max);
                t += L"\r\n";
            }
            if (cap.hasTuner()) t += L"        sintonizzatore TV analogico presente\r\n";
            for (auto& in : cap.videoInputs()) t += L"        ingresso: " + in + L"\r\n";
        } else {
            t += L"        (non apribile: " + err + L")\r\n";
        }
    }
    auto bda = listDigitalTuners();
    t += format(L"\r\nSchede TV digitali (BDA): %d\r\n", (int)bda.size());
    for (auto& b : bda) t += L"  - " + b + L"\r\n";
    t += format(L"\r\nIngressi audio: %d\r\n", (int)audioIns_.size());
    for (auto& a : listAudioInputs()) t += L"  - " + a.name + L"\r\n";
    t += format(L"\r\nUscite audio: %d\r\n", (int)audioOuts_.size());
    for (auto& a : listAudioOutputs()) t += L"  - " + a.name + L"\r\n";
    return t;
}

// -------------------------------------------------------------------- azioni

std::wstring App::recFolder() const {
    std::wstring f = getText(item(PAGE_REC, IDC_REC_FOLDER));
    while (!f.empty() && (f.back() == L'\\' || f.back() == L' ')) f.pop_back();
    return f;
}

void App::toggleRecording() {
    if (engine_.recorder().active()) {
        HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        setStatus(L"Finalizzazione del file…");
        long dropped = engine_.recorder().framesDropped();
        bool failed = engine_.recorder().failed();
        engine_.stopRecording();
        SetCursor(old);
        lastFile_ = recPath_;
        SetWindowTextW(item(PAGE_REC, IDC_REC_LAST), (L"Ultimo file: " + lastFile_).c_str());
        if (failed) showError(L"Errore di scrittura durante la registrazione (disco pieno?). Il file potrebbe essere incompleto:\n" + recPath_);
        else setStatus(L"Registrazione salvata: " + recPath_ + (dropped > 0 ? format(L"  (%ld fotogrammi saltati: riduci risoluzione o bitrate)", dropped) : L""));
        updateUi();
        return;
    }
    if (!engine_.running()) { showError(L"Avvia prima una sorgente."); return; }
    RecOptions opt;
    bool mp4 = SendMessageW(item(PAGE_REC, IDC_REC_MP4), BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt.format = mp4 ? RecFormat::Mp4 : RecFormat::Ogg;
    opt.videoKbps = std::max(200, std::min(40000, _wtoi(getText(item(PAGE_REC, IDC_REC_VKBPS)).c_str())));
    opt.audioKbps = std::max(48, std::min(320, _wtoi(getText(item(PAGE_REC, IDC_REC_AKBPS)).c_str())));
    opt.audio = SendMessageW(item(PAGE_REC, IDC_REC_AUDIO), BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring folder = recFolder(), prefix = getText(item(PAGE_REC, IDC_REC_PREFIX));
    if (prefix.empty()) prefix = L"SmartVision";
    ensureDir(folder);
    std::wstring path = folder + L"\\" + prefix + L"-" + timestampName() + (mp4 ? L".mp4" : L".ogg");
    std::wstring err;
    bool wantAudio = opt.audio;
    if (!engine_.startRecording(path, opt, err)) { showError(err); return; }
    recPath_ = path;
    saveSettings();
    setStatus(L"Registrazione in corso: " + path + (wantAudio && !engine_.hasAudio() ? L"  (senza audio: nessun ingresso audio)" : L""));
    updateUi();
}

void App::takeSnapshot() {
    if (!engine_.running()) { showError(L"Avvia prima una sorgente."); return; }
    std::wstring folder = recFolder(), prefix = getText(item(PAGE_REC, IDC_REC_PREFIX));
    if (prefix.empty()) prefix = L"SmartVision";
    ensureDir(folder);
    std::wstring path = folder + L"\\" + prefix + L"-" + timestampName() + L".png", err;
    if (engine_.snapshot(path, err)) {
        lastFile_ = path;
        SetWindowTextW(item(PAGE_REC, IDC_REC_LAST), (L"Ultimo file: " + path).c_str());
        setStatus(L"Istantanea salvata: " + path);
    } else {
        showError(err);
    }
}

void App::openRecFolder() {
    std::wstring f = recFolder();
    ensureDir(f);
    ShellExecuteW(hwnd_, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::browseFolder() {
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof bi);
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"Cartella di destinazione per registrazioni e istantanee";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(item(PAGE_REC, IDC_REC_FOLDER), path);
    CoTaskMemFree(pidl);
}

void App::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    if (fullscreen_) {
        savedPlacement_.length = sizeof savedPlacement_;
        GetWindowPlacement(hwnd_, &savedPlacement_);
        MONITORINFO mi = { sizeof mi, {}, {}, 0 };
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetMenu(hwnd_, nullptr);
        for (HWND c : { tab_, status_, tbStart_, tbSnap_, tbRec_, tbInfo_ }) ShowWindow(c, SW_HIDE);
        for (HWND p : pages_) ShowWindow(p, SW_HIDE);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        SetMenu(hwnd_, menu_);
        for (HWND c : { tab_, status_, tbStart_, tbSnap_, tbRec_, tbInfo_ }) ShowWindow(c, SW_SHOW);
        SetWindowPlacement(hwnd_, &savedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        showPage((int)SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
    }
    layout();
}

struct MonitorList { std::vector<RECT> rects; };
static BOOL CALLBACK monitorEnum(HMONITOR, HDC, LPRECT rc, LPARAM l) {
    reinterpret_cast<MonitorList*>(l)->rects.push_back(*rc);
    return TRUE;
}

void App::placeExtWindow() {
    if (!ext_) return;
    int sel = comboSel(item(PAGE_OUT, IDC_OUT_POS));
    MonitorList ml;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnum, (LPARAM)&ml);
    if (sel >= 1 && sel - 1 < (int)ml.rects.size()) {
        const RECT& r = ml.rects[sel - 1];
        extFullscreen_ = true;
        SetWindowLongPtrW(ext_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(ext_, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        extFullscreen_ = false;
        SetWindowLongPtrW(ext_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(ext_, HWND_NOTOPMOST, 0, 0, S(640), S(400), SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

void App::toggleExtWindow(bool on) {
    if (on && !ext_) {
        ext_ = CreateWindowExW(0, L"SVExt", L"WebCam and TV Smart Vision – Uscita esterna (F11: schermo intero)", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, S(640), S(400), nullptr, nullptr, inst_, nullptr);
        ShowWindow(ext_, SW_SHOWNOACTIVATE);
        placeExtWindow();
    } else if (!on && ext_) {
        HWND e = ext_;
        ext_ = nullptr;
        DestroyWindow(e);
    }
    suppress_ = true;
    SendMessageW(item(PAGE_OUT, IDC_OUT_EXT), BM_SETCHECK, ext_ ? BST_CHECKED : BST_UNCHECKED, 0);
    suppress_ = false;
    CheckMenuItem(menu_, IDM_EXTWINDOW, ext_ ? MF_CHECKED : MF_UNCHECKED);
}

void App::toggleBridge(bool on) {
    std::wstring err;
    if (!on) { engine_.stopBridge(); return; }
    int sel = comboSel(item(PAGE_OUT, IDC_OUT_ADEV));
    int dev = (sel >= 1 && sel - 1 < (int)audioOuts_.size()) ? audioOuts_[sel - 1].id : -1;
    engine_.setBridgeVolume((float)SendMessageW(item(PAGE_OUT, IDC_OUT_AVOL), TBM_GETPOS, 0, 0) / 100.0f);
    engine_.setBridgeMuted(SendMessageW(item(PAGE_OUT, IDC_OUT_AMUTE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (!engine_.startBridge(dev, err)) {
        suppress_ = true;
        SendMessageW(item(PAGE_OUT, IDC_OUT_AON), BM_SETCHECK, BST_UNCHECKED, 0);
        suppress_ = false;
        showError(err);
    } else {
        settings_.set(L"audio_out", comboText(item(PAGE_OUT, IDC_OUT_ADEV)));
    }
}

void App::showAbout() {
    std::wstring t = format(
        L"WebCam and TV Smart Vision %ls\nEdizione Windows (%ls)\n\n"
        L"Anteprima, registrazione MP4 (H.264 + AAC) e OGG (Theora + Vorbis), istantanee PNG, controlli della webcam, "
        L"sintonizzatori TV analogici e ponte del segnale verso un secondo schermo e un'uscita audio.\n\n"
        L"Video: DirectShow · Audio: waveIn/waveOut\n"
        L"Codificatori inclusi: minih264, vo-aacenc, libtheora, libvorbis, libogg, minimp4.\n\n"
        L"© 2026 Fabio Avallone — licenza GPL-3.0-or-later\nhttps://github.com/fabionet/webcam-tv-smart",
        widen(SV_VERSION).c_str(), widen(SV_TARGET).c_str());
    MessageBoxW(hwnd_, t.c_str(), L"Informazioni", MB_OK | MB_ICONINFORMATION);
}

void App::openGuide() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    std::wstring pdf = dir + L"\\Guida-Windows-WebCam-TV-Smart-Vision.pdf";
    if (fileExists(pdf)) ShellExecuteW(hwnd_, L"open", pdf.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(hwnd_, L"open", L"https://github.com/fabionet/webcam-tv-smart/blob/main/docs/Guida-Windows-WebCam-TV-Smart-Vision.pdf", nullptr, nullptr, SW_SHOWNORMAL);
}

void App::setStatus(const std::wstring& text) {
    statusText_ = text;
    SendMessageW(status_, SB_SETTEXTW, 0, (LPARAM)text.c_str());
    if (selfTest_) log(L"  [stato] " + text);
}

void App::showError(const std::wstring& text) {
    setStatus(text);
    if (selfTest_) { log(L"  [errore] " + text); return; }
    MessageBoxW(hwnd_, text.c_str(), kTitle, MB_OK | MB_ICONWARNING);
}

void App::saveSettings() {
    settings_.set(L"rec_format", SendMessageW(item(PAGE_REC, IDC_REC_MP4), BM_GETCHECK, 0, 0) == BST_CHECKED ? L"mp4" : L"ogg");
    settings_.set(L"video_kbps", getText(item(PAGE_REC, IDC_REC_VKBPS)));
    settings_.set(L"audio_kbps", getText(item(PAGE_REC, IDC_REC_AKBPS)));
    settings_.setInt(L"rec_audio", SendMessageW(item(PAGE_REC, IDC_REC_AUDIO), BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
    settings_.set(L"folder", recFolder());
    settings_.set(L"prefix", getText(item(PAGE_REC, IDC_REC_PREFIX)));
    settings_.setInt(L"orient", std::max(0, comboSel(item(PAGE_SOURCE, IDC_SRC_ORIENT))));
    settings_.setInt(L"gain", (int)SendMessageW(item(PAGE_SOURCE, IDC_SRC_GAIN), TBM_GETPOS, 0, 0));
    settings_.set(L"audio", comboText(item(PAGE_SOURCE, IDC_SRC_AUDIO)));
}

// ------------------------------------------------------------------ messaggi

void App::onCommand(int id, int code, HWND ctl) {
    if (suppress_) return;
    if (id >= IDC_CAM_AUTO && id < IDC_CAM_AUTO + 100 && code == BN_CLICKED) { onCamAuto(id - IDC_CAM_AUTO); return; }
    switch (id) {
        case IDC_TB_START: case IDM_STARTSTOP:
            if (engine_.running()) stopSource(L"Sorgente fermata."); else startSelected();
            break;
        case IDC_SRC_START: case IDC_PH_START: startSelected(); break;
        case IDC_SRC_STOP: stopSource(L"Sorgente fermata."); break;
        case IDC_TB_SNAP: case IDM_SNAPSHOT: takeSnapshot(); break;
        case IDC_TB_REC: case IDM_RECORD: case IDC_REC_BUTTON: toggleRecording(); break;
        case IDC_SRC_REFRESH: case IDC_PH_REFRESH: case IDM_REFRESH: refreshDevices(true); break;
        case IDC_SRC_COMBO: if (code == CBN_SELCHANGE) onSourceSelected(true); break;
        case IDC_SRC_FORMAT: if (code == CBN_SELCHANGE && engine_.running()) startSelected(); break;
        case IDC_SRC_AUDIO: if (code == CBN_SELCHANGE) { settings_.set(L"audio", comboText(ctl)); applyAudioInput(); } break;
        case IDC_SRC_ORIENT:
            if (code == CBN_SELCHANGE) { engine_.setTransform(comboSel(ctl)); settings_.setInt(L"orient", comboSel(ctl)); }
            break;
        case IDC_CAM_PRESET_AUTO: applyCamPreset(0); break;
        case IDC_CAM_PRESET_50HZ: applyCamPreset(1); break;
        case IDC_CAM_PRESET_FOCUS: applyCamPreset(2); break;
        case IDC_CAM_PROPS:
            if (!engine_.capture().isOpen()) setStatus(L"Seleziona prima una webcam.");
            else if (!engine_.capture().showPropertyPages(hwnd_)) setStatus(L"Questa periferica non ha una finestra di proprietà del driver.");
            else rebuildWebcamControls();
            break;
        case IDC_TV_COUNTRY_SET:
            if (engine_.capture().setTunerCountry(_wtoi(getText(item(PAGE_TV, IDC_TV_COUNTRY)).c_str()))) { setStatus(L"Paese del sintonizzatore impostato."); rebuildTvPage(); }
            else setStatus(L"Il sintonizzatore non ha accettato il codice paese.");
            break;
        case IDC_TV_RF: if (code == CBN_SELCHANGE) { engine_.capture().setTunerCable(comboSel(ctl) == 1); rebuildTvPage(); } break;
        case IDC_TV_GO: tvSetChannel(_wtoi(getText(item(PAGE_TV, IDC_TV_CHANNEL)).c_str())); break;
        case IDC_TV_PREV: tvSetChannel(engine_.capture().tunerChannel() - 1); break;
        case IDC_TV_NEXT: tvSetChannel(engine_.capture().tunerChannel() + 1); break;
        case IDC_TV_XBAR:
            if (code == CBN_SELCHANGE) setStatus(engine_.capture().selectVideoInput(comboSel(ctl)) ? L"Ingresso video: " + comboText(ctl) : L"Ingresso non selezionabile.");
            break;
        case IDC_TV_FOUND: if (code == CBN_SELCHANGE) tvSetChannel(_wtoi(comboText(ctl).c_str() + 7)); break;   // "Canale N"
        case IDC_TV_SCAN:
            if (tvScanning_) { tvScanning_ = false; KillTimer(hwnd_, TIMER_TVSCAN); SetWindowTextW(ctl, L"Ricerca canali"); setStatus(L"Ricerca interrotta."); }
            else {
                long lo = 0, hi = 0;
                if (!engine_.capture().tunerRange(lo, hi)) { setStatus(L"Nessun sintonizzatore TV sulla periferica selezionata."); break; }
                if (!engine_.running()) startSelected();
                SendMessageW(item(PAGE_TV, IDC_TV_FOUND), CB_RESETCONTENT, 0, 0);
                tvScanCh_ = (int)lo; tvScanMax_ = (int)hi; tvScanning_ = true;
                SetWindowTextW(ctl, L"Interrompi");
                engine_.capture().setTunerChannel(tvScanCh_);
                SetTimer(hwnd_, TIMER_TVSCAN, 450, nullptr);
            }
            break;
        case IDC_REC_MP4: case IDC_REC_OGG:
            SetWindowTextW(item(PAGE_REC, IDC_REC_ENCINFO), SendMessageW(item(PAGE_REC, IDC_REC_MP4), BM_GETCHECK, 0, 0) == BST_CHECKED
                ? L"H.264 + AAC: compatibile con tutti i lettori." : L"Theora + Vorbis: libero, più lento in Full HD.");
            break;
        case IDC_REC_BROWSE: browseFolder(); break;
        case IDC_REC_OPEN: case IDM_OPENFOLDER: openRecFolder(); break;
        case IDC_OUT_EXT: toggleExtWindow(SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        case IDM_EXTWINDOW: toggleExtWindow(!ext_); break;
        case IDC_OUT_POS: if (code == CBN_SELCHANGE) placeExtWindow(); break;
        case IDC_OUT_AON: toggleBridge(SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        case IDC_OUT_ADEV: if (code == CBN_SELCHANGE && engine_.bridgeActive()) toggleBridge(true); break;
        case IDC_OUT_AMUTE: engine_.setBridgeMuted(SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        case IDM_FULLSCREEN: toggleFullscreen(); break;
        case IDM_ESCAPE: if (fullscreen_) toggleFullscreen(); break;
        case IDM_GUIDE: openGuide(); break;
        case IDM_ABOUT: showAbout(); break;
        case IDM_DIAG: {
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            std::wstring path = std::wstring(tmp) + L"SmartVision-dispositivi.txt";
            OutFile f;
            if (f.open(path)) {
                std::string utf8 = "\xEF\xBB\xBF" + narrow(diagnostics());
                f.write(utf8.data(), utf8.size());
                f.close();
                ShellExecuteW(hwnd_, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        case IDM_EXIT: SendMessageW(hwnd_, WM_CLOSE, 0, 0); break;
    }
}

void App::onHScroll(HWND ctl) {
    if (suppress_) return;
    int id = GetDlgCtrlID(ctl);
    if (id >= IDC_CAM_SLIDER && id < IDC_CAM_SLIDER + 100) onCamSlider(id - IDC_CAM_SLIDER);
    else if (id == IDC_SRC_GAIN) engine_.setGain((float)SendMessageW(ctl, TBM_GETPOS, 0, 0) / 100.0f);
    else if (id == IDC_OUT_AVOL) engine_.setBridgeVolume((float)SendMessageW(ctl, TBM_GETPOS, 0, 0) / 100.0f);
}

void App::onTimer(UINT_PTR id) {
    if (id == TIMER_DEVCHANGE) {
        KillTimer(hwnd_, TIMER_DEVCHANGE);
        refreshDevices(true);
    } else if (id == TIMER_SELFTEST) {
        selfTestTick();
    } else if (id == TIMER_TVSCAN) {
        tvScanStep();
    } else if (id == TIMER_UI) {
        float lv = engine_.running() ? engine_.level() : 0;
        if (std::fabs(lv - shownLevel_) > 0.01f) {
            shownLevel_ = lv;
            SendMessageW(status_, SB_SETTEXTW, 1 | SBT_OWNERDRAW, 0);
        }
        DWORD now = GetTickCount();
        if (now - lastSecondTick_ >= 500) {
            lastSecondTick_ = now;
            if (engine_.recorder().active()) {
                if (engine_.recorder().failed()) toggleRecording();
                RECT r = { 0, 0, S(220), S(40) };
                InvalidateRect(video_, &r, FALSE);
            }
            if (engine_.running()) {
                std::wstring info = sourceName();
                if (engine_.width() > 0) info += format(L"  ·  %d×%d  ·  %.3g fps", engine_.width(), engine_.height(), engine_.measuredFps() > 1 ? engine_.measuredFps() : engine_.nominalFps());
                if (info != getText(tbInfo_)) SetWindowTextW(tbInfo_, info.c_str());
                if (engine_.capture().hasTuner() && !tvScanning_)
                    SetWindowTextW(item(PAGE_TV, IDC_TV_SIGNAL), engine_.capture().tunerSignal() ? L"Segnale: presente" : L"Segnale: assente");
            }
        }
    }
}

void App::paintVideo(HWND h, bool external) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int cw = rc.right, ch = rc.bottom;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, std::max(1, cw), std::max(1, ch));
    HGDIOBJ oldBm = SelectObject(mem, bm);
    FillRect(mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(mem, TRANSPARENT);
    bool drawn = false;
    const uint8_t* bits = nullptr;
    int fw = 0, fh = 0;
    if (engine_.running() && engine_.lockFrame(bits, fw, fh)) {
        // Mantiene le proporzioni: bande nere sopra/sotto o ai lati.
        int dw = cw, dh = (int)((double)cw * fh / fw + 0.5);
        if (dh > ch) { dh = ch; dw = (int)((double)ch * fw / fh + 0.5); }
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = fw;
        bi.bmiHeader.biHeight = -fh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(mem, (dw < fw) ? HALFTONE : COLORONCOLOR);
        SetBrushOrgEx(mem, 0, 0, nullptr);
        StretchDIBits(mem, (cw - dw) / 2, (ch - dh) / 2, dw, dh, 0, 0, fw, fh, bits, &bi, DIB_RGB_COLORS, SRCCOPY);
        engine_.unlockFrame();
        drawn = true;
    }
    if (!external) {
        if (drawn) {
            if (engine_.recorder().active()) {
                std::wstring t = L"REC " + durationText(engine_.recorder().elapsed());
                HGDIOBJ of = SelectObject(mem, fontBold_);
                SIZE sz;
                GetTextExtentPoint32W(mem, t.c_str(), (int)t.size(), &sz);
                RECT r = { S(8), S(8), S(8) + sz.cx + S(34), S(8) + sz.cy + S(8) };
                HBRUSH red = CreateSolidBrush(RGB(224, 27, 36));
                HGDIOBJ ob = SelectObject(mem, red), op = SelectObject(mem, GetStockObject(NULL_PEN));
                RoundRect(mem, r.left, r.top, r.right, r.bottom, S(8), S(8));
                SelectObject(mem, GetStockObject(WHITE_BRUSH));
                int cy = (r.top + r.bottom) / 2, rad = S(5);
                Ellipse(mem, r.left + S(8), cy - rad, r.left + S(8) + rad * 2, cy + rad);
                SelectObject(mem, ob); SelectObject(mem, op);
                DeleteObject(red);
                SetTextColor(mem, RGB(255, 255, 255));
                TextOutW(mem, r.left + S(24), r.top + S(4), t.c_str(), (int)t.size());
                SelectObject(mem, of);
            }
            std::wstring info = sourceName() + format(L"  ·  %d×%d  ·  %.3g fps", fw, fh, engine_.measuredFps() > 1 ? engine_.measuredFps() : engine_.nominalFps());
            HGDIOBJ of = SelectObject(mem, font_);
            SIZE sz;
            GetTextExtentPoint32W(mem, info.c_str(), (int)info.size(), &sz);
            if (sz.cx + S(24) < cw) {
                RECT r = { cw - sz.cx - S(20), ch - sz.cy - S(14), cw - S(8), ch - S(8) };
                FillRect(mem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
                SetTextColor(mem, RGB(255, 255, 255));
                TextOutW(mem, r.left + S(6), r.top + S(3), info.c_str(), (int)info.size());
            }
            SelectObject(mem, of);
        } else {
            // Segnaposto: nome delle periferiche rilevate o avviso, sopra i due pulsanti.
            int mid = ch / 2;
            HGDIOBJ of = SelectObject(mem, fontBig_);
            RECT m = { S(16), 0, cw - S(16), 0 };
            int hgt = DrawTextW(mem, placeholderTitle_.c_str(), -1, &m, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
            RECT tr = { S(16), std::max<LONG>(S(4), mid - S(58) - hgt), cw - S(16), mid - S(58) };
            SetTextColor(mem, RGB(255, 255, 255));
            DrawTextW(mem, placeholderTitle_.c_str(), -1, &tr, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(mem, font_);
            SetTextColor(mem, RGB(200, 200, 200));
            RECT xr = { S(24), mid - S(48), cw - S(24), mid + S(32) };
            DrawTextW(mem, placeholderText_.c_str(), -1, &xr, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(mem, of);
        }
    } else if (!drawn) {
        HGDIOBJ of = SelectObject(mem, fontBig_);
        SetTextColor(mem, RGB(160, 160, 160));
        DrawTextW(mem, L"Nessun segnale", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, of);
    }
    BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBm);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT App::videoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paintVideo(h, false); return 0;
        case WM_COMMAND: return SendMessageW(hwnd_, m, w, l);
        case WM_LBUTTONDBLCLK: toggleFullscreen(); return 0;
        case WM_SIZE: {
            RECT rc;
            GetClientRect(h, &rc);
            int bw1 = S(130), bw2 = S(120), gap = S(8), bh = S(28);
            int x = (rc.right - bw1 - bw2 - gap) / 2, y = rc.bottom / 2 + S(40);
            if (phRefresh_) MoveWindow(phRefresh_, x, y, bw1, bh, TRUE);
            if (phStart_) MoveWindow(phStart_, x + bw1 + gap, y, bw2, bh, TRUE);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT App::extProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paintVideo(h, true); return 0;
        case WM_KEYDOWN:
            if (w == VK_F11 || w == 'F') goto toggle;
            if (w == VK_ESCAPE && extFullscreen_) goto toggle;
            return 0;
        case WM_LBUTTONDBLCLK:
        toggle: {
            extFullscreen_ = !extFullscreen_;
            if (extFullscreen_) {
                GetWindowRect(h, &extSavedRect_);
                MONITORINFO mi = { sizeof mi, {}, {}, 0 };
                GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
                SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
            } else {
                SetWindowLongPtrW(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                RECT r = extSavedRect_;
                if (r.right - r.left < S(100)) { r.left = S(80); r.top = S(80); r.right = r.left + S(640); r.bottom = r.top + S(400); }
                SetWindowPos(h, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
            }
            return 0;
        }
        case WM_CLOSE:
            PostMessageW(hwnd_, WM_APP_EXTCLOSED, 0, 0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT App::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE:
            if (status_) layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(l);
            mm->ptMinTrackSize.x = S(640);
            mm->ptMinTrackSize.y = S(440);
            return 0;
        }
        case WM_COMMAND:
            onCommand(LOWORD(w), HIWORD(w), (HWND)l);
            return 0;
        case WM_HSCROLL:
            if (l) onHScroll((HWND)l);
            return 0;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(l);
            if (nm->hwndFrom == tab_ && nm->code == TCN_SELCHANGE) showPage((int)SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
            return 0;
        }
        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (di->hwndItem == status_) {                       // indicatore del livello audio
                RECT r = di->rcItem;
                HGDIOBJ of = SelectObject(di->hDC, font_);
                SetBkMode(di->hDC, TRANSPARENT);
                SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
                RECT tr = { r.left + S(4), r.top, r.left + S(44), r.bottom };
                DrawTextW(di->hDC, L"Audio", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(di->hDC, of);
                RECT bar = { r.left + S(46), r.top + S(5), r.right - S(22), r.bottom - S(5) };
                FillRect(di->hDC, &bar, (HBRUSH)GetStockObject(GRAY_BRUSH));
                float lv = shownLevel_ < 0 ? 0 : shownLevel_;
                float db = lv > 0.001f ? 20.0f * (float)std::log10(lv) : -60.0f;      // scala in decibel: -60..0
                float frac = std::max(0.0f, std::min(1.0f, (db + 60.0f) / 60.0f));
                RECT fill = bar;
                fill.right = bar.left + (LONG)((bar.right - bar.left) * frac);
                HBRUSH g = CreateSolidBrush(frac > 0.92f ? RGB(224, 27, 36) : RGB(46, 194, 126));
                FillRect(di->hDC, &fill, g);
                DeleteObject(g);
                return TRUE;
            }
            break;
        }
        case WM_TIMER:
            onTimer((UINT_PTR)w);
            return 0;
        case WM_APP_FRAME:
            engine_.frameHandled();
            InvalidateRect(video_, nullptr, FALSE);
            if (ext_) InvalidateRect(ext_, nullptr, FALSE);
            return 0;
        case WM_APP_GRAPH:
            if (engine_.capture().pollLost() && engine_.running()) {
                stopSource(L"La periferica ha interrotto il flusso (scollegata?).");
                SetTimer(hwnd_, TIMER_DEVCHANGE, 900, nullptr);
            }
            return 0;
        case WM_APP_EXTCLOSED:
            toggleExtWindow(false);
            return 0;
        case WM_DEVICECHANGE:
            if (w == DBT_DEVNODES_CHANGED || w == DBT_DEVICEARRIVAL || w == DBT_DEVICEREMOVECOMPLETE)
                SetTimer(hwnd_, TIMER_DEVCHANGE, 900, nullptr);      // raggruppa gli eventi ravvicinati
            return TRUE;
        case WM_CLOSE:
            saveSettings();
            if (engine_.recorder().active()) toggleRecording();   // chiude correttamente il file
            toggleExtWindow(false);
            engine_.stopBridge();
            engine_.stop();
            engine_.capture().close();
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(exitCode_);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace sv
