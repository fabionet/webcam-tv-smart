// Pagine della barra laterale: Sorgente, Webcam, TV, Rec, Uscite.
#include "app.h"

#include <shlobj.h>

#include <algorithm>

namespace sv {

// Griglia logica (a 96 dpi): margine 8, etichette larghe 86, controlli da x=98 larghi 176.
enum { X0 = 8, LW = 86, CX = 98, CW = 176, FW = 266, RH = 26 };

static const DWORD kCombo = CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
static const DWORD kTrack = TBS_HORZ | TBS_NOTICKS | WS_TABSTOP;

void App::buildSourcePage(HWND p) {
    int y = 8;
    // Barra "periferica di cattura": nome della webcam rilevata (o avviso) + tasto Aggiorna
    mk(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, X0, y, 190, 34, IDC_SRC_DEVBAR);
    mk(p, L"BUTTON", L"Aggiorna", BS_PUSHBUTTON | WS_TABSTOP, 204, y + 3, 70, 26, IDC_SRC_REFRESH);
    y += 42;
    mk(p, L"STATIC", L"Sorgente video", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, WC_COMBOBOXW, L"", kCombo, X0, y, FW, 300, IDC_SRC_COMBO); y += 30;
    mk(p, L"STATIC", L"Formato", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 300, IDC_SRC_FORMAT); y += 34;

    mk(p, L"STATIC", L"Audio", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"STATIC", L"Ingresso", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 300, IDC_SRC_AUDIO); y += 30;
    mk(p, L"STATIC", L"Guadagno", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND gain = mk(p, TRACKBAR_CLASSW, L"", kTrack, CX - 6, y, CW + 10, 26, IDC_SRC_GAIN);
    SendMessageW(gain, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(gain, TBM_SETRANGEMAX, FALSE, 200);
    SendMessageW(gain, TBM_SETPOS, TRUE, 100);
    y += 36;

    mk(p, L"STATIC", L"Immagine", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"STATIC", L"Orientamento", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND orient = mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 300, IDC_SRC_ORIENT);
    for (const wchar_t* s : { L"Normale", L"Specchiata (orizzontale)", L"Capovolta (verticale)", L"Ruotata 180°", L"Ruotata 90° orario", L"Ruotata 90° antiorario" }) comboAdd(orient, s);
    SendMessageW(orient, CB_SETCURSEL, 0, 0);
    y += 38;

    mk(p, L"BUTTON", L"Avvia", BS_PUSHBUTTON | WS_TABSTOP, X0, y, 130, 28, IDC_SRC_START);
    mk(p, L"BUTTON", L"Ferma", BS_PUSHBUTTON | WS_TABSTOP, X0 + 136, y, 130, 28, IDC_SRC_STOP);
    y += 38;
    mk(p, L"STATIC", L"Le webcam UVC (Logitech C920, Brio…) funzionano con il driver incluso in Windows. "
                     L"Le schede di acquisizione e i sintonizzatori TV analogici compaiono nello stesso elenco.",
       SS_LEFT | SS_NOPREFIX, X0, y, FW, 64, IDC_NOTE);
    y += 68;
    setPageContentHeight(p, y);
}

void App::buildWebcamPage(HWND p) {
    int y = 8;
    mk(p, L"STATIC", L"Nessuna webcam selezionata.", SS_LEFT | SS_NOPREFIX, X0, y, FW, 34, IDC_CAM_INFO); y += 38;
    mk(p, L"STATIC", L"Profili rapidi", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"BUTTON", L"Auto", BS_PUSHBUTTON | WS_TABSTOP, X0, y, 84, 26, IDC_CAM_PRESET_AUTO);
    mk(p, L"BUTTON", L"50 Hz", BS_PUSHBUTTON | WS_TABSTOP, X0 + 90, y, 84, 26, IDC_CAM_PRESET_50HZ);
    mk(p, L"BUTTON", L"Fuoco fisso", BS_PUSHBUTTON | WS_TABSTOP, X0 + 180, y, 86, 26, IDC_CAM_PRESET_FOCUS);
    y += 32;
    mk(p, L"BUTTON", L"Proprietà del driver…", BS_PUSHBUTTON | WS_TABSTOP, X0, y, FW, 26, IDC_CAM_PROPS); y += 34;
    mk(p, L"STATIC", L"Controlli del dispositivo", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 22;
    mk(p, L"STATIC", L"Seleziona una webcam nella scheda Sorgente: qui compariranno luminosità, contrasto, zoom, messa a fuoco, esposizione e gli altri controlli offerti dal driver.",
       SS_LEFT | SS_NOPREFIX, X0, y, FW, 64, IDC_CAM_EMPTY);
    setPageContentHeight(p, y + 70);
}

void App::rebuildWebcamControls() {
    HWND p = pages_[PAGE_WEBCAM];
    for (HWND h : camDynamic_) DestroyWindow(h);
    camDynamic_.clear();
    camControls_.clear();
    // Riporta la pagina in cima prima di ricreare i controlli (le coordinate sono relative allo scorrimento).
    SCROLLINFO si = { sizeof si, SIF_POS, 0, 0, 0, 0, 0 };
    GetScrollInfo(p, SB_VERT, &si);
    if (si.nPos) { SetScrollPos(p, SB_VERT, 0, TRUE); ScrollWindowEx(p, 0, si.nPos, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE); }

    DsCapture& cap = engine_.capture();
    int d = selectedDeviceIndex();
    int y = 8 + 38 + 20 + 32 + 34 + 22;
    if (d < 0 || !cap.isOpen()) {
        SetWindowTextW(item(PAGE_WEBCAM, IDC_CAM_INFO), L"Nessuna webcam selezionata.");
        ShowWindow(item(PAGE_WEBCAM, IDC_CAM_EMPTY), SW_SHOW);
        setPageContentHeight(p, y + 70);
        return;
    }
    SetWindowTextW(item(PAGE_WEBCAM, IDC_CAM_INFO), (devices_[d].name + L"\r\nDriver DirectShow di Windows: nessun driver aggiuntivo.").c_str());
    camControls_ = cap.controls();
    ShowWindow(item(PAGE_WEBCAM, IDC_CAM_EMPTY), camControls_.empty() ? SW_SHOW : SW_HIDE);
    if (camControls_.empty()) {
        SetWindowTextW(item(PAGE_WEBCAM, IDC_CAM_EMPTY), L"Questa periferica non espone controlli regolabili. Prova «Proprietà del driver…».");
        setPageContentHeight(p, y + 70);
        return;
    }
    suppress_ = true;
    for (size_t i = 0; i < camControls_.size() && i < 40; ++i) {
        const DsControl& c = camControls_[i];
        camDynamic_.push_back(mk(p, L"STATIC", c.name.c_str(), SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, X0, y, LW, 24, IDC_CAM_LABEL + (int)i));
        HWND tr = mk(p, TRACKBAR_CLASSW, L"", kTrack, CX - 8, y, 112, 24, IDC_CAM_SLIDER + (int)i);
        SendMessageW(tr, TBM_SETRANGEMIN, FALSE, c.min);
        SendMessageW(tr, TBM_SETRANGEMAX, FALSE, c.max);
        SendMessageW(tr, TBM_SETLINESIZE, 0, c.step);
        SendMessageW(tr, TBM_SETPAGESIZE, 0, std::max<long>(c.step, (c.max - c.min) / 10));
        SendMessageW(tr, TBM_SETPOS, TRUE, c.value);
        EnableWindow(tr, !c.isAuto);
        camDynamic_.push_back(tr);
        camDynamic_.push_back(mk(p, L"STATIC", format(L"%ld", c.value).c_str(), SS_RIGHT | SS_CENTERIMAGE, 202, y, 30, 24, IDC_CAM_VALUE + (int)i));
        if (c.hasAuto) {
            HWND a = mk(p, L"BUTTON", L"Auto", BS_AUTOCHECKBOX | WS_TABSTOP, 236, y, 44, 24, IDC_CAM_AUTO + (int)i);
            SendMessageW(a, BM_SETCHECK, c.isAuto ? BST_CHECKED : BST_UNCHECKED, 0);
            camDynamic_.push_back(a);
        }
        y += RH;
    }
    suppress_ = false;
    setPageContentHeight(p, y + 10);
}

void App::onCamSlider(int index) {
    if (index < 0 || index >= (int)camControls_.size()) return;
    DsControl& c = camControls_[index];
    long v = (long)SendMessageW(item(PAGE_WEBCAM, IDC_CAM_SLIDER + index), TBM_GETPOS, 0, 0);
    if (c.step > 1) v = c.min + ((v - c.min + c.step / 2) / c.step) * c.step;
    v = std::max(c.min, std::min(c.max, v));
    if (v == c.value && !c.isAuto) return;
    if (engine_.capture().setControl(c.kind, c.prop, v, false)) {
        c.value = v;
        c.isAuto = false;
        SetWindowTextW(item(PAGE_WEBCAM, IDC_CAM_VALUE + index), format(L"%ld", v).c_str());
    } else {
        setStatus(L"Il driver ha rifiutato il valore per: " + c.name);
    }
}

void App::onCamAuto(int index) {
    if (index < 0 || index >= (int)camControls_.size()) return;
    DsControl& c = camControls_[index];
    bool on = SendMessageW(item(PAGE_WEBCAM, IDC_CAM_AUTO + index), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (engine_.capture().setControl(c.kind, c.prop, c.value, on)) {
        c.isAuto = on;
        EnableWindow(item(PAGE_WEBCAM, IDC_CAM_SLIDER + index), !on);
    } else {
        setStatus(L"Il driver non consente di cambiare la modalità automatica di: " + c.name);
    }
}

void App::applyCamPreset(int preset) {
    DsCapture& cap = engine_.capture();
    if (!cap.isOpen() || camControls_.empty()) { setStatus(L"Seleziona prima una webcam con controlli regolabili."); return; }
    auto find = [&](int kind, long prop) -> DsControl* {
        for (auto& c : camControls_) if (c.kind == kind && c.prop == prop) return &c;
        return nullptr;
    };
    if (preset == 0) {
        for (auto& c : camControls_) cap.setControl(c.kind, c.prop, c.def, c.hasAuto);
        setStatus(L"Controlli riportati ai valori predefiniti.");
    } else if (preset == 1) {
        DsControl* pl = find(0, 13);                          // frequenza di rete: 1 = 50 Hz
        bool ok = pl && cap.setControl(0, 13, 1, false);
        if (DsControl* e = find(1, 4)) if (e->hasAuto) cap.setControl(1, 4, e->value, true);
        if (DsControl* wb = find(0, 7)) if (wb->hasAuto) cap.setControl(0, 7, wb->value, true);
        setStatus(ok ? L"Anti-sfarfallio a 50 Hz attivato, esposizione e bilanciamento automatici."
                     : L"Questo driver non espone la frequenza di rete: usa «Proprietà del driver…» (Anti-sfarfallio / PowerLine).");
    } else {
        DsControl* f = find(1, 6);
        if (f && cap.setControl(1, 6, f->value, false)) setStatus(L"Autofocus disattivato: regola «Messa a fuoco» manualmente.");
        else setStatus(L"Questa webcam non ha la messa a fuoco regolabile.");
    }
    rebuildWebcamControls();
}

void App::buildTvPage(HWND p) {
    int y = 8;
    mk(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, X0, y, FW, 48, IDC_TV_INFO); y += 52;
    mk(p, L"STATIC", L"Sintonizzatore analogico", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"STATIC", L"Ingresso video", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 200, IDC_TV_XBAR); y += 30;
    mk(p, L"STATIC", L"Segnale RF", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND rf = mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 200, IDC_TV_RF);
    comboAdd(rf, L"Antenna");
    comboAdd(rf, L"Cavo");
    y += 30;
    mk(p, L"STATIC", L"Paese (prefisso)", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    mk(p, L"EDIT", L"39", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, CX, y, 60, 22, IDC_TV_COUNTRY, WS_EX_CLIENTEDGE);
    mk(p, L"BUTTON", L"Imposta", BS_PUSHBUTTON | WS_TABSTOP, CX + 66, y - 1, 110, 24, IDC_TV_COUNTRY_SET);
    y += 30;
    mk(p, L"STATIC", L"Canale", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND ch = mk(p, L"EDIT", L"1", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, CX, y, 60, 22, IDC_TV_CHANNEL, WS_EX_CLIENTEDGE);
    HWND spin = mk(p, UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, IDC_TV_CHSPIN);
    SendMessageW(spin, UDM_SETBUDDY, (WPARAM)ch, 0);
    SendMessageW(spin, UDM_SETRANGE32, 1, 125);
    mk(p, L"BUTTON", L"Vai", BS_PUSHBUTTON | WS_TABSTOP, CX + 66, y - 1, 50, 24, IDC_TV_GO);
    mk(p, L"BUTTON", L"−", BS_PUSHBUTTON | WS_TABSTOP, CX + 120, y - 1, 26, 24, IDC_TV_PREV);
    mk(p, L"BUTTON", L"+", BS_PUSHBUTTON | WS_TABSTOP, CX + 150, y - 1, 26, 24, IDC_TV_NEXT);
    y += 30;
    mk(p, L"STATIC", L"Segnale: —", SS_LEFT | SS_CENTERIMAGE, X0, y, FW, 20, IDC_TV_SIGNAL); y += 26;
    mk(p, L"BUTTON", L"Ricerca canali", BS_PUSHBUTTON | WS_TABSTOP, X0, y, 120, 26, IDC_TV_SCAN);
    mk(p, WC_COMBOBOXW, L"", kCombo, X0 + 126, y + 1, 140, 300, IDC_TV_FOUND);
    y += 36;
    mk(p, L"STATIC", L"Schede TV digitali (DVB-T/T2, DVB-S/S2, DVB-C): nell'edizione Windows vengono rilevate ma la sintonia "
                     L"digitale non è ancora disponibile. Per il digitale terrestre e satellitare usa l'edizione Linux.",
       SS_LEFT | SS_NOPREFIX, X0, y, FW, 80, IDC_NOTE);
    y += 84;
    setPageContentHeight(p, y);
}

void App::rebuildTvPage() {
    DsCapture& cap = engine_.capture();
    bool tuner = cap.isOpen() && cap.hasTuner();
    std::wstring info;
    int d = selectedDeviceIndex();
    if (d >= 0 && cap.isOpen()) info = devices_[d].name + (tuner ? L"\r\nSintonizzatore TV analogico rilevato." : L"\r\nNessun sintonizzatore TV su questa periferica.");
    else info = L"Nessuna scheda TV selezionata.";
    auto bda = listDigitalTuners();
    if (!bda.empty()) info += L"\r\nScheda digitale rilevata: " + bda[0];
    SetWindowTextW(item(PAGE_TV, IDC_TV_INFO), info.c_str());

    suppress_ = true;
    HWND xb = item(PAGE_TV, IDC_TV_XBAR);
    SendMessageW(xb, CB_RESETCONTENT, 0, 0);
    auto inputs = cap.isOpen() ? cap.videoInputs() : std::vector<std::wstring>();
    for (auto& in : inputs) comboAdd(xb, in);
    if (!inputs.empty()) SendMessageW(xb, CB_SETCURSEL, std::max(0, cap.currentVideoInput()), 0);
    EnableWindow(xb, !inputs.empty());
    for (int id : { IDC_TV_RF, IDC_TV_COUNTRY, IDC_TV_COUNTRY_SET, IDC_TV_CHANNEL, IDC_TV_CHSPIN, IDC_TV_GO, IDC_TV_PREV, IDC_TV_NEXT, IDC_TV_SCAN, IDC_TV_FOUND })
        EnableWindow(item(PAGE_TV, id), tuner);
    if (tuner) {
        long lo = 1, hi = 125;
        cap.tunerRange(lo, hi);
        SendMessageW(item(PAGE_TV, IDC_TV_CHSPIN), UDM_SETRANGE32, lo, hi);
        SetWindowTextW(item(PAGE_TV, IDC_TV_CHANNEL), format(L"%ld", cap.tunerChannel()).c_str());
        SetWindowTextW(item(PAGE_TV, IDC_TV_COUNTRY), format(L"%ld", cap.tunerCountry()).c_str());
        SendMessageW(item(PAGE_TV, IDC_TV_RF), CB_SETCURSEL, cap.tunerIsCable() ? 1 : 0, 0);
    } else {
        SetWindowTextW(item(PAGE_TV, IDC_TV_SIGNAL), L"Segnale: —");
    }
    suppress_ = false;
}

void App::tvSetChannel(long ch) {
    DsCapture& cap = engine_.capture();
    long lo = 1, hi = 125;
    if (!cap.tunerRange(lo, hi)) { setStatus(L"Nessun sintonizzatore TV sulla periferica selezionata."); return; }
    ch = std::max(lo, std::min(hi, ch));
    if (cap.setTunerChannel(ch)) {
        suppress_ = true;
        SetWindowTextW(item(PAGE_TV, IDC_TV_CHANNEL), format(L"%ld", ch).c_str());
        suppress_ = false;
        setStatus(format(L"Canale %ld", ch));
    } else {
        setStatus(format(L"Il sintonizzatore non ha accettato il canale %ld.", ch));
    }
}

void App::tvScanStep() {
    DsCapture& cap = engine_.capture();
    if (!tvScanning_ || !cap.hasTuner()) { tvScanning_ = false; KillTimer(hwnd_, TIMER_TVSCAN); return; }
    if (cap.tunerSignal()) comboAdd(item(PAGE_TV, IDC_TV_FOUND), format(L"Canale %d", tvScanCh_));
    if (++tvScanCh_ > tvScanMax_) {
        tvScanning_ = false;
        KillTimer(hwnd_, TIMER_TVSCAN);
        SetWindowTextW(item(PAGE_TV, IDC_TV_SCAN), L"Ricerca canali");
        HWND found = item(PAGE_TV, IDC_TV_FOUND);
        int n = (int)SendMessageW(found, CB_GETCOUNT, 0, 0);
        setStatus(format(L"Ricerca terminata: %d canali con segnale.", n));
        if (n > 0) { SendMessageW(found, CB_SETCURSEL, 0, 0); tvSetChannel(_wtoi(comboText(found).c_str() + 7)); }
        return;
    }
    cap.setTunerChannel(tvScanCh_);
    setStatus(format(L"Ricerca canali: %d di %d…", tvScanCh_, tvScanMax_));
}

void App::buildRecPage(HWND p) {
    int y = 8;
    mk(p, L"STATIC", L"Formato", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    HWND mp4 = mk(p, L"BUTTON", L"MP4 (H.264 + AAC)", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, X0, y, FW, 20, IDC_REC_MP4); y += 22;
    HWND ogg = mk(p, L"BUTTON", L"OGG (Theora + Vorbis)", BS_AUTORADIOBUTTON, X0, y, FW, 20, IDC_REC_OGG); y += 22;
    bool isOgg = settings_.get(L"rec_format", L"mp4") == L"ogg";
    SendMessageW(isOgg ? ogg : mp4, BM_SETCHECK, BST_CHECKED, 0);
    mk(p, L"STATIC", isOgg ? L"Theora + Vorbis: libero, più lento in Full HD." : L"H.264 + AAC: compatibile con tutti i lettori.",
       SS_LEFT | SS_NOPREFIX, X0, y, FW, 18, IDC_REC_ENCINFO);
    y += 26;

    mk(p, L"STATIC", L"Qualità", SS_LEFT | WS_GROUP, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"STATIC", L"Video kbit/s", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND vk = mk(p, L"EDIT", settings_.get(L"video_kbps", L"4000").c_str(), ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, CX, y, 90, 22, IDC_REC_VKBPS, WS_EX_CLIENTEDGE);
    HWND vs = mk(p, UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, IDC_REC_VSPIN);
    SendMessageW(vs, UDM_SETBUDDY, (WPARAM)vk, 0);
    SendMessageW(vs, UDM_SETRANGE32, 200, 40000);
    UDACCEL acc = { 0, 100 };
    SendMessageW(vs, UDM_SETACCEL, 1, (LPARAM)&acc);
    y += 28;
    mk(p, L"STATIC", L"Audio kbit/s", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND ak = mk(p, L"EDIT", settings_.get(L"audio_kbps", L"128").c_str(), ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, CX, y, 90, 22, IDC_REC_AKBPS, WS_EX_CLIENTEDGE);
    HWND as = mk(p, UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, IDC_REC_ASPIN);
    SendMessageW(as, UDM_SETBUDDY, (WPARAM)ak, 0);
    SendMessageW(as, UDM_SETRANGE32, 48, 320);
    UDACCEL acc2 = { 0, 16 };
    SendMessageW(as, UDM_SETACCEL, 1, (LPARAM)&acc2);
    y += 28;
    HWND au = mk(p, L"BUTTON", L"Registra anche l'audio", BS_AUTOCHECKBOX | WS_TABSTOP, X0, y, FW, 20, IDC_REC_AUDIO);
    SendMessageW(au, BM_SETCHECK, settings_.getInt(L"rec_audio", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 28;

    mk(p, L"STATIC", L"Destinazione", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    wchar_t def[MAX_PATH] = L"";
    if (SHGetFolderPathW(nullptr, CSIDL_MYVIDEO | CSIDL_FLAG_CREATE, nullptr, 0, def) != S_OK)
        SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, def);
    std::wstring folder = selfTest_ ? selfTestDir_ : settings_.get(L"folder", def);
    mk(p, L"EDIT", folder.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, X0, y, 192, 22, IDC_REC_FOLDER, WS_EX_CLIENTEDGE);
    mk(p, L"BUTTON", L"Sfoglia…", BS_PUSHBUTTON | WS_TABSTOP, X0 + 196, y - 1, 70, 24, IDC_REC_BROWSE);
    y += 28;
    mk(p, L"STATIC", L"Prefisso", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    mk(p, L"EDIT", (selfTest_ ? std::wstring(L"selftest") : settings_.get(L"prefix", L"SmartVision")).c_str(), ES_AUTOHSCROLL | WS_TABSTOP, CX, y, CW, 22, IDC_REC_PREFIX, WS_EX_CLIENTEDGE);
    y += 34;
    mk(p, L"BUTTON", L"Avvia registrazione", BS_PUSHBUTTON | WS_TABSTOP, X0, y, FW, 32, IDC_REC_BUTTON); y += 38;
    mk(p, L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS | SS_NOPREFIX, X0, y, FW, 18, IDC_REC_LAST); y += 22;
    mk(p, L"BUTTON", L"Apri cartella", BS_PUSHBUTTON | WS_TABSTOP, X0, y, FW, 26, IDC_REC_OPEN); y += 34;
    setPageContentHeight(p, y);
}

struct MonList { std::vector<RECT> r; };
static BOOL CALLBACK monEnum(HMONITOR, HDC, LPRECT rc, LPARAM l) {
    reinterpret_cast<MonList*>(l)->r.push_back(*rc);
    return TRUE;
}

void App::buildOutPage(HWND p) {
    int y = 8;
    mk(p, L"STATIC", L"Ponte video: finestra esterna", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"STATIC", L"Mostra lo stesso segnale su un secondo schermo o un televisore collegato in HDMI/VGA.",
       SS_LEFT | SS_NOPREFIX, X0, y, FW, 46, IDC_NOTE);
    y += 50;
    mk(p, L"BUTTON", L"Apri la finestra esterna", BS_AUTOCHECKBOX | WS_TABSTOP, X0, y, FW, 20, IDC_OUT_EXT); y += 26;
    mk(p, L"STATIC", L"Posizione", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND pos = mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 200, IDC_OUT_POS);
    comboAdd(pos, L"Finestra mobile");
    MonList ml;
    EnumDisplayMonitors(nullptr, nullptr, monEnum, (LPARAM)&ml);
    for (size_t i = 0; i < ml.r.size(); ++i)
        comboAdd(pos, format(L"Schermo %d intero (%ld×%ld)", (int)i + 1, ml.r[i].right - ml.r[i].left, ml.r[i].bottom - ml.r[i].top));
    SendMessageW(pos, CB_SETCURSEL, 0, 0);
    y += 38;

    mk(p, L"STATIC", L"Ponte audio: uscita hardware", SS_LEFT, X0, y, FW, 18, IDC_HEADING); y += 20;
    mk(p, L"STATIC", L"Dispositivo", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    mk(p, WC_COMBOBOXW, L"", kCombo, CX, y, CW, 300, IDC_OUT_ADEV); y += 30;
    mk(p, L"BUTTON", L"Invia l'audio a questa uscita", BS_AUTOCHECKBOX | WS_TABSTOP, X0, y, FW, 20, IDC_OUT_AON); y += 26;
    mk(p, L"STATIC", L"Volume", SS_LEFT | SS_CENTERIMAGE, X0, y, LW, 22, 0);
    HWND vol = mk(p, TRACKBAR_CLASSW, L"", kTrack, CX - 6, y, CW + 10, 26, IDC_OUT_AVOL);
    SendMessageW(vol, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(vol, TBM_SETRANGEMAX, FALSE, 150);
    SendMessageW(vol, TBM_SETPOS, TRUE, 100);
    y += 30;
    mk(p, L"BUTTON", L"Muto", BS_AUTOCHECKBOX | WS_TABSTOP, X0, y, FW, 20, IDC_OUT_AMUTE); y += 30;
    mk(p, L"STATIC", L"Con una webcam usa il ponte audio solo in cuffia, per evitare l'eco. Volume e Muto non modificano la registrazione.\r\n\r\n"
                     L"Webcam virtuale: l'edizione Windows non installa driver. Per passare il segnale ad altri programmi cattura la finestra esterna (ad esempio con OBS).",
       SS_LEFT | SS_NOPREFIX, X0, y, FW, 120, IDC_NOTE);
    y += 124;
    setPageContentHeight(p, y);
}

} // namespace sv
