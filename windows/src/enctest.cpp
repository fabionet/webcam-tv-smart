// Prova dei codificatori da riga di comando:  sv-enctest <cartella> [larghezza altezza fps secondi]
// Produce prova.mp4 e prova.ogg dalla sorgente di prova e stampa la velocità di codifica.
#include <cstdio>
#include <cstdlib>

#include "recorder.h"
#include "testpattern.h"

using namespace sv;

static int runOne(const std::wstring& path, RecFormat fmt, int w, int h, double fps, int seconds) {
    Recorder rec;
    RecOptions opt;
    opt.format = fmt;
    opt.fps = fps;
    opt.videoKbps = w >= 1280 ? 4000 : 1500;
    std::wstring err;
    if (!rec.start(path, w, h, opt, err)) { wprintf(L"ERRORE avvio %ls: %ls\n", path.c_str(), err.c_str()); return 1; }
    std::vector<uint8_t> frame((size_t)w * h * 4);
    std::vector<int16_t> pcm(4800 * 2);
    double phase = 0, t0 = nowSeconds();
    int total = (int)(fps * seconds);
    int chunk = (int)(opt.audioRate / fps);
    for (int i = 0; i < total; ++i) {
        double t = i / fps;
        renderTestPattern(frame.data(), w, h, i, t);
        rec.pushVideo(frame.data(), w, h, w * 4, 1000.0 + t);
        renderTestTone(pcm.data(), chunk, opt.audioRate, phase);
        rec.pushAudio(pcm.data(), chunk, 1000.0 + t + 1.0 / fps);
        while (rec.framesWritten() + rec.framesDropped() < i - 6) Sleep(1);   // non saturare la coda
    }
    rec.stop();
    double dt = nowSeconds() - t0;
    wprintf(L"%ls: %dx%d, %d fotogrammi in %.2f s (%.1f fps), scritti %ld, saltati %ld, %lld byte%ls\n", path.c_str(),
            rec.outWidth(), rec.outHeight(), total, dt, total / dt, rec.framesWritten(), rec.framesDropped(),
            (long long)fileSize(path), rec.failed() ? L"  ERRORE DI SCRITTURA" : L"");
    return rec.failed() || fileSize(path) < 10000 ? 1 : 0;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring dir = argc > 1 ? argv[1] : L".";
    int w = argc > 3 ? _wtoi(argv[2]) : 1280, h = argc > 3 ? _wtoi(argv[3]) : 720;
    double fps = argc > 4 ? _wtof(argv[4]) : 30;
    int seconds = argc > 5 ? _wtoi(argv[5]) : 5;
    ensureDir(dir);
    int rc = runOne(dir + L"\\prova.mp4", RecFormat::Mp4, w, h, fps, seconds);
    rc |= runOne(dir + L"\\prova.ogg", RecFormat::Ogg, w, h, fps, seconds);
    return rc;
}
