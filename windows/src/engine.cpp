#include "engine.h"

#include <cmath>
#include <cstring>

#include "imaging.h"
#include "testpattern.h"

namespace sv {

Engine::Engine() {}

Engine::~Engine() {
    stopRecording();
    stop();
    stopBridge();
    audioIn_.close();
}

void Engine::setNotify(HWND hwnd, UINT frameMsg, UINT graphMsg) {
    hwnd_ = hwnd;
    frameMsg_ = frameMsg;
    graphMsg_ = graphMsg;
}

double Engine::nominalFps() const {
    if (source_ == Source::Test) return 30.0;
    double f = capture_.nominalFps();
    if (f < 1 && measuredFps_ > 1) f = std::floor(measuredFps_ + 0.5);
    return f >= 1 ? f : 30.0;
}

bool Engine::startTest(std::wstring& err) {
    stop();
    fpsWindowStart_ = 0; fpsWindowCount_ = 0; measuredFps_ = 0; frameCount_ = 0;
    testStop_ = false;
    source_ = Source::Test;
    testThread_ = CreateThread(nullptr, 0, testThread, this, 0, nullptr);
    if (!testThread_) { source_ = Source::None; err = L"Impossibile avviare la sorgente di prova."; return false; }
    setAudioInput(audioDevice_);
    return true;
}

bool Engine::startDevice(int formatIdx, std::wstring& err) {
    if (source_ != Source::None) stop();
    fpsWindowStart_ = 0; fpsWindowCount_ = 0; measuredFps_ = 0; frameCount_ = 0;
    source_ = Source::Device;
    if (!capture_.start(formatIdx, hwnd_, graphMsg_, dsFrame, this, err)) {
        capture_.stop();
        source_ = Source::None;
        return false;
    }
    setAudioInput(audioDevice_);
    return true;
}

void Engine::stop() {
    Source was = source_;
    if (was == Source::None) return;
    if (was == Source::Test) {
        testStop_ = true;
        WaitForSingleObject(testThread_, 3000);
        CloseHandle(testThread_);
        testThread_ = nullptr;
    } else {
        capture_.stop();
    }
    source_ = Source::None;
    audioIn_.close();
    level_ = 0;
    Guard g(frameLock_);
    fw_ = fh_ = 0;
}

void Engine::dsFrame(void* ctx, const uint8_t* data, int w, int h, int stride, double t) {
    static_cast<Engine*>(ctx)->onFrame(data, w, h, stride, t);
}

void Engine::onFrame(const uint8_t* data, int w, int h, int stride, double t) {
    if (source_ == Source::None) return;
    int mode = transform_;
    bool swap = mode == 4 || mode == 5;
    int ow = swap ? h : w, oh = swap ? w : h;
    // Riga sorgente "y dall'alto": con stride negativo il buffer è una DIB dal basso verso l'alto.
    auto srcRow = [&](int y) -> const uint8_t* { return stride < 0 ? data + (size_t)(h - 1 - y) * (size_t)(-stride) : data + (size_t)y * (size_t)stride; };
    {
        Guard g(frameLock_);
        frame_.resize((size_t)ow * oh * 4);
        uint32_t* dst = reinterpret_cast<uint32_t*>(frame_.data());
        switch (mode) {
            default:
                for (int y = 0; y < h; ++y) memcpy(dst + (size_t)y * w, srcRow(y), (size_t)w * 4);
                break;
            case 1:
                for (int y = 0; y < h; ++y) {
                    const uint32_t* s = reinterpret_cast<const uint32_t*>(srcRow(y));
                    uint32_t* d = dst + (size_t)y * w;
                    for (int x = 0; x < w; ++x) d[x] = s[w - 1 - x];
                }
                break;
            case 2:
                for (int y = 0; y < h; ++y) memcpy(dst + (size_t)y * w, srcRow(h - 1 - y), (size_t)w * 4);
                break;
            case 3:
                for (int y = 0; y < h; ++y) {
                    const uint32_t* s = reinterpret_cast<const uint32_t*>(srcRow(h - 1 - y));
                    uint32_t* d = dst + (size_t)y * w;
                    for (int x = 0; x < w; ++x) d[x] = s[w - 1 - x];
                }
                break;
            case 4:   // 90° orario: (x, y) -> (h-1-y, x)
                for (int y = 0; y < h; ++y) {
                    const uint32_t* s = reinterpret_cast<const uint32_t*>(srcRow(y));
                    for (int x = 0; x < w; ++x) dst[(size_t)x * ow + (h - 1 - y)] = s[x];
                }
                break;
            case 5:   // 90° antiorario: (x, y) -> (y, w-1-x)
                for (int y = 0; y < h; ++y) {
                    const uint32_t* s = reinterpret_cast<const uint32_t*>(srcRow(y));
                    for (int x = 0; x < w; ++x) dst[(size_t)(w - 1 - x) * ow + y] = s[x];
                }
                break;
        }
        fw_ = ow;
        fh_ = oh;
    }
    InterlockedIncrement(&frameCount_);
    ++fpsWindowCount_;
    if (fpsWindowStart_ == 0) fpsWindowStart_ = t;
    else if (t - fpsWindowStart_ >= 1.0) {
        measuredFps_ = fpsWindowCount_ / (t - fpsWindowStart_);
        fpsWindowStart_ = t;
        fpsWindowCount_ = 0;
    }
    // frame_ viene scritto solo da questo thread: la lettura qui non richiede il lock.
    if (recorder_.active()) recorder_.pushVideo(frame_.data(), ow, oh, ow * 4, t);
    if (hwnd_ && InterlockedExchange(&framePending_, 1) == 0) PostMessageW(hwnd_, frameMsg_, 0, 0);
}

bool Engine::lockFrame(const uint8_t*& bgra, int& w, int& h) {
    frameLock_.enter();
    if (fw_ <= 0 || fh_ <= 0 || frame_.size() < (size_t)fw_ * fh_ * 4) {
        frameLock_.leave();
        return false;
    }
    bgra = frame_.data();
    w = fw_;
    h = fh_;
    return true;
}

void Engine::unlockFrame() { frameLock_.leave(); }

bool Engine::snapshot(const std::wstring& pngPath, std::wstring& err) {
    std::vector<uint8_t> copy;
    int w = 0, h = 0;
    {
        const uint8_t* p = nullptr;
        if (!lockFrame(p, w, h)) { err = L"Nessun fotogramma disponibile."; return false; }
        copy.assign(p, p + (size_t)w * h * 4);
        unlockFrame();
    }
    if (!savePng(pngPath, copy.data(), w, h, w * 4)) { err = L"Impossibile salvare l'immagine:\n" + pngPath; return false; }
    return true;
}

DWORD WINAPI Engine::testThread(LPVOID self) {
    static_cast<Engine*>(self)->runTest();
    return 0;
}

void Engine::runTest() {
    const int w = 1280, h = 720, rate = 48000;
    const double fps = 30.0;
    std::vector<uint8_t> img((size_t)w * h * 4);
    std::vector<int16_t> pcm((size_t)(rate / 30 + 8) * 2);
    double phase = 0, t0 = nowSeconds();
    long n = 0, samplesDone = 0;
    while (!testStop_) {
        double target = t0 + n / fps;
        double now = nowSeconds();
        if (now < target) { Sleep((DWORD)((target - now) * 1000) + 1); continue; }
        if (now - target > 0.5) { t0 = now - n / fps; }          // sistema in affanno: riallinea
        renderTestPattern(img.data(), w, h, n, n / fps);
        onFrame(img.data(), w, h, w * 4, now);
        if (audioDevice_ != AudioNone) {                         // il tono segue il passo del video
            long want = (long)((n + 1) * rate / fps) - samplesDone;
            if (want > 0 && want <= (long)pcm.size() / 2) {
                renderTestTone(pcm.data(), (int)want, rate, phase);
                onAudio(pcm.data(), (int)want, nowSeconds());
                samplesDone += want;
            }
        }
        ++n;
    }
}

bool Engine::setAudioInput(int device) {
    audioDevice_ = device;
    audioIn_.close();
    level_ = 0;
    if (source_ != Source::Device || device == AudioNone) return true;   // prova: tono generato; fermo: nulla
    return audioIn_.open(device, audioIn, this);
}

bool Engine::hasAudio() const {
    if (source_ == Source::Test) return audioDevice_ != AudioNone;
    return audioIn_.isOpen();
}

int Engine::audioRate() const { return source_ == Source::Device && audioIn_.isOpen() ? audioIn_.rate() : 48000; }

void Engine::audioIn(void* ctx, const int16_t* stereo, int frames, double tEnd) {
    static_cast<Engine*>(ctx)->onAudio(stereo, frames, tEnd);
}

void Engine::onAudio(const int16_t* stereo, int frames, double tEnd) {
    float g = gain_;
    int peak = 0;
    gainBuf_.resize((size_t)frames * 2);
    for (int i = 0; i < frames * 2; ++i) {
        float s = stereo[i] * g;
        int v = (int)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        gainBuf_[i] = (int16_t)v;
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    float lv = peak / 32768.0f, prev = level_;
    level_ = lv > prev ? lv : prev * 0.85f + lv * 0.15f;         // salita immediata, discesa morbida
    if (recorder_.active()) recorder_.pushAudio(gainBuf_.data(), frames, tEnd);
    if (bridge_.isOpen()) bridge_.write(gainBuf_.data(), frames);
}

bool Engine::startBridge(int outDevice, std::wstring& err) {
    bridge_.close();
    if (!bridge_.open(outDevice, audioRate())) { err = L"Impossibile aprire l'uscita audio selezionata."; return false; }
    return true;
}

void Engine::stopBridge() { bridge_.close(); }

bool Engine::startRecording(const std::wstring& path, RecOptions opt, std::wstring& err) {
    if (!running() || fw_ <= 0) { err = L"Avvia prima una sorgente."; return false; }
    opt.audio = opt.audio && hasAudio();
    opt.audioRate = audioRate();
    opt.fps = nominalFps();
    return recorder_.start(path, fw_, fh_, opt, err);
}

void Engine::stopRecording() { recorder_.stop(); }

} // namespace sv
