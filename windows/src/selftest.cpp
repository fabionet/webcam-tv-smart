// Auto-test:  WebCamTVSmartVision.exe --self-test <cartella> [--self-test-device]
// Avvia la sorgente di prova, registra MP4 e OGG, scatta un'istantanea, salva le schermate di ogni scheda
// e scrive selftest.log. Con --self-test-device prova anche la prima periferica DirectShow reale.
#include "app.h"
#include "imaging.h"

namespace sv {

void App::selfTestTick() {
    if (GetTickCount() < selfTestNext_) return;
    auto wait = [&](DWORD ms) { selfTestNext_ = GetTickCount() + ms; };
    auto check = [&](bool ok, const std::wstring& what) {
        log((ok ? L"OK      " : L"ERRORE  ") + what);
        if (!ok) ++selfTestErrors_;
    };
    auto shot = [&](const wchar_t* name) {
        UpdateWindow(hwnd_);
        check(saveWindowPng(hwnd_, selfTestDir_ + L"\\" + name), std::wstring(L"schermata ") + name);
    };
    auto fileOk = [&](const std::wstring& path, int64_t minBytes) {
        int64_t sz = fileSize(path);
        check(sz >= minBytes, format(L"file %ls (%lld byte)", path.c_str(), (long long)sz));
    };
    int step = selfTestStep_++;
    switch (step) {
        case 0: {
            log(diagnostics());
            suppress_ = true;
            SendMessageW(item(PAGE_SOURCE, IDC_SRC_COMBO), CB_SETCURSEL, 0, 0);
            suppress_ = false;
            onSourceSelected(true);
            wait(1500);
            break;
        }
        case 1:
            check(engine_.running() && engine_.frameCount() > 10, format(L"sorgente di prova: %ld fotogrammi, %.1f fps", engine_.frameCount(), engine_.measuredFps()));
            check(engine_.level() > 0.001f, format(L"tono audio di prova (livello %.3f)", engine_.level()));
            SendMessageW(item(PAGE_REC, IDC_REC_MP4), BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(item(PAGE_REC, IDC_REC_OGG), BM_SETCHECK, BST_UNCHECKED, 0);
            toggleRecording();
            check(engine_.recorder().active(), L"registrazione MP4 avviata");
            wait(1200);
            break;
        case 2:
            showPage(PAGE_SOURCE); shot(L"win-sorgente.png");
            showPage(PAGE_WEBCAM); shot(L"win-webcam.png");
            showPage(PAGE_TV); shot(L"win-tv.png");
            showPage(PAGE_REC); shot(L"win-rec.png");
            showPage(PAGE_OUT); shot(L"win-uscite.png");
            showPage(PAGE_SOURCE);
            wait(2800);
            break;
        case 3: {
            std::wstring path = recPath_;
            long written = engine_.recorder().framesWritten();
            toggleRecording();
            check(!engine_.recorder().active(), L"registrazione MP4 fermata");
            check(written > 60, format(L"fotogrammi MP4 codificati: %ld", written));
            fileOk(path, 20000);
            SendMessageW(item(PAGE_REC, IDC_REC_MP4), BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(item(PAGE_REC, IDC_REC_OGG), BM_SETCHECK, BST_CHECKED, 0);
            wait(1100);                                            // nome file diverso (al secondo)
            break;
        }
        case 4:
            toggleRecording();
            check(engine_.recorder().active(), L"registrazione OGG avviata");
            wait(4000);
            break;
        case 5: {
            std::wstring path = recPath_;
            long written = engine_.recorder().framesWritten();
            toggleRecording();
            check(written > 40, format(L"fotogrammi OGG codificati: %ld", written));
            fileOk(path, 20000);
            wait(1100);
            takeSnapshot();
            fileOk(lastFile_, 5000);
            break;
        }
        case 6: {
            std::wstring err;
            bool bridge = engine_.startBridge(-1, err);
            log(bridge ? L"OK      ponte audio verso l'uscita predefinita" : L"NOTA    ponte audio non disponibile (nessuna uscita audio): " + err);
            toggleExtWindow(true);
            check(ext_ != nullptr, L"finestra esterna aperta");
            wait(900);
            break;
        }
        case 7:
            if (ext_) { UpdateWindow(ext_); saveWindowPng(ext_, selfTestDir_ + L"\\win-esterna.png"); }
            toggleExtWindow(false);
            engine_.stopBridge();
            SendMessageW(item(PAGE_SOURCE, IDC_SRC_ORIENT), CB_SETCURSEL, 1, 0);
            engine_.setTransform(1);
            wait(500);
            break;
        case 8:
            shot(L"win-specchiata.png");
            engine_.setTransform(0);
            SendMessageW(item(PAGE_SOURCE, IDC_SRC_ORIENT), CB_SETCURSEL, 0, 0);
            stopSource(L"Sorgente fermata.");
            wait(500);
            break;
        case 9:
            check(!engine_.running(), L"sorgente fermata");
            shot(L"win-nessuna-periferica.png");
            wait(300);
            break;
        case 10:
            // Prova facoltativa di una periferica DirectShow reale (webcam fisica o virtuale).
            if (selfTestDevice_ && !devices_.empty()) {
                suppress_ = true;
                SendMessageW(item(PAGE_SOURCE, IDC_SRC_COMBO), CB_SETCURSEL, 1, 0);
                suppress_ = false;
                onSourceSelected(true);
                log(L"        periferica: " + devices_[0].name);
                wait(5000);
            } else {
                log(selfTestDevice_ ? L"NOTA    nessuna periferica DirectShow presente: prova di cattura saltata" : L"        prova con periferica reale non richiesta");
                selfTestStep_ = 13;
            }
            break;
        case 11:
            check(engine_.running() && engine_.frameCount() > 5, format(L"cattura DirectShow: %ld fotogrammi, %dx%d, %.1f fps",
                  engine_.frameCount(), engine_.width(), engine_.height(), engine_.measuredFps()));
            showPage(PAGE_WEBCAM);
            shot(L"win-periferica.png");
            if (engine_.running()) {
                SendMessageW(item(PAGE_REC, IDC_REC_MP4), BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(item(PAGE_REC, IDC_REC_OGG), BM_SETCHECK, BST_UNCHECKED, 0);
                toggleRecording();
            }
            wait(3000);
            break;
        case 12:
            if (engine_.recorder().active()) {
                std::wstring path = recPath_;
                toggleRecording();
                fileOk(path, 5000);
            }
            stopSource(L"Sorgente fermata.");
            wait(300);
            break;
        default:
            log(selfTestErrors_ ? format(L"RISULTATO: %d ERRORI", selfTestErrors_) : L"RISULTATO: OK");
            KillTimer(hwnd_, TIMER_SELFTEST);
            exitCode_ = selfTestErrors_ ? 1 : 0;
            SendMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
    }
}

} // namespace sv
